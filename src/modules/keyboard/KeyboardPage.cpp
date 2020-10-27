/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2007 Free Software Foundation, Inc.
 *   SPDX-FileCopyrightText: 2014-2016 Teo Mrnjavac <teo@kde.org>
 *   SPDX-FileCopyrightText: 2017-2018 Adriaan de Groot <groot@kde.org>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Portions from the Manjaro Installation Framework
 *   by Roland Singer <roland@manjaro.org>
 *   Copyright (C) 2007 Free Software Foundation, Inc.
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 */

#include "KeyboardPage.h"

#include "KeyboardLayoutModel.h"
#include "SetKeyboardLayoutJob.h"
#include "keyboardwidget/keyboardpreview.h"
#include "ui_KeyboardPage.h"

#include "GlobalStorage.h"
#include "JobQueue.h"
#include "utils/Logger.h"
#include "utils/Retranslator.h"
#include "utils/String.h"

#include <QComboBox>
#include <QProcess>
#include <QPushButton>

class LayoutItem : public QListWidgetItem
{
public:
    QString data;

    ~LayoutItem() override;
};

LayoutItem::~LayoutItem() {}

static QPersistentModelIndex
findLayout( const KeyboardLayoutModel* klm, const QString& currentLayout )
{
    QPersistentModelIndex currentLayoutItem;

    for ( int i = 0; i < klm->rowCount(); ++i )
    {
        QModelIndex idx = klm->index( i );
        if ( idx.isValid() && idx.data( KeyboardLayoutModel::KeyboardLayoutKeyRole ).toString() == currentLayout )
        {
            currentLayoutItem = idx;
        }
    }

    return currentLayoutItem;
}

KeyboardPage::KeyboardPage( QWidget* parent )
    : QWidget( parent )
    , ui( new Ui::Page_Keyboard )
    , m_keyboardPreview( new KeyBoardPreview( this ) )
    , m_defaultIndex( 0 )
{
    ui->setupUi( this );

    // Keyboard Preview
    ui->KBPreviewLayout->addWidget( m_keyboardPreview );

    m_setxkbmapTimer.setSingleShot( true );

    // Connect signals and slots
    connect( ui->listVariant, &QListWidget::currentItemChanged, this, &KeyboardPage::onListVariantCurrentItemChanged );

    connect(
        ui->buttonRestore, &QPushButton::clicked, [this] { ui->comboBoxModel->setCurrentIndex( m_defaultIndex ); } );

    connect( ui->comboBoxModel, &QComboBox::currentTextChanged, [this]( const QString& text ) {
        QString model = m_models.value( text, "pc105" );

        // Set Xorg keyboard model
        QProcess::execute( "setxkbmap", QStringList { "-model", model } );
    } );

    CALAMARES_RETRANSLATE( ui->retranslateUi( this ); )
}


KeyboardPage::~KeyboardPage()
{
    delete ui;
}




QString
KeyboardPage::prettyStatus() const
{
    QString status;
    status += tr( "Set keyboard model to %1.<br/>" ).arg( ui->comboBoxModel->currentText() );

    QString layout = ui->listLayout->currentIndex().data().toString();
    QString variant = ui->listVariant->currentItem() ? ui->listVariant->currentItem()->text() : QString( "<default>" );
    status += tr( "Set keyboard layout to %1/%2." ).arg( layout, variant );

    return status;
}


QList< Calamares::job_ptr >
KeyboardPage::createJobs( const QString& xOrgConfFileName,
                          const QString& convertedKeymapPath,
                          bool writeEtcDefaultKeyboard )
{
    QList< Calamares::job_ptr > list;
    QString selectedModel = m_models.value( ui->comboBoxModel->currentText(), "pc105" );

    Calamares::Job* j = new SetKeyboardLayoutJob( selectedModel,
                                                  m_selectedLayout,
                                                  m_selectedVariant,
                                                  xOrgConfFileName,
                                                  convertedKeymapPath,
                                                  writeEtcDefaultKeyboard );
    list.append( Calamares::job_ptr( j ) );

    return list;
}


void
KeyboardPage::guessLayout( const QStringList& langParts )
{
    const KeyboardLayoutModel* klm = dynamic_cast< KeyboardLayoutModel* >( ui->listLayout->model() );
    bool foundCountryPart = false;
    for ( auto countryPart = langParts.rbegin(); !foundCountryPart && countryPart != langParts.rend(); ++countryPart )
    {
        cDebug() << Logger::SubEntry << "looking for locale part" << *countryPart;
        for ( int i = 0; i < klm->rowCount(); ++i )
        {
            QModelIndex idx = klm->index( i );
            QString name
                = idx.isValid() ? idx.data( KeyboardLayoutModel::KeyboardLayoutKeyRole ).toString() : QString();
            if ( idx.isValid() && ( name.compare( *countryPart, Qt::CaseInsensitive ) == 0 ) )
            {
                cDebug() << Logger::SubEntry << "matched" << name;
                ui->listLayout->setCurrentIndex( idx );
                foundCountryPart = true;
                break;
            }
        }
        if ( foundCountryPart )
        {
            ++countryPart;
            if ( countryPart != langParts.rend() )
            {
                cDebug() << "Next level:" << *countryPart;
                for ( int variantnumber = 0; variantnumber < ui->listVariant->count(); ++variantnumber )
                {
                    LayoutItem* variantdata = dynamic_cast< LayoutItem* >( ui->listVariant->item( variantnumber ) );
                    if ( variantdata && ( variantdata->data.compare( *countryPart, Qt::CaseInsensitive ) == 0 ) )
                    {
                        ui->listVariant->setCurrentItem( variantdata );
                        cDebug() << Logger::SubEntry << "matched variant" << variantdata->data << ' '
                                 << variantdata->text();
                    }
                }
            }
        }
    }
}


void
KeyboardPage::onActivate()
{
    /* Guessing a keyboard layout based on the locale means
     * mapping between language identifiers in <lang>_<country>
     * format to keyboard mappings, which are <country>_<layout>
     * format; in addition, some countries have multiple languages,
     * so fr_BE and nl_BE want different layouts (both Belgian)
     * and sometimes the language-country name doesn't match the
     * keyboard-country name at all (e.g. Ellas vs. Greek).
     *
     * This is a table of language-to-keyboard mappings. The
     * language identifier is the key, while the value is
     * a string that is used instead of the real language
     * identifier in guessing -- so it should be something
     * like <layout>_<country>.
     */
    static constexpr char arabic[] = "ara";
    static const auto specialCaseMap = QMap< std::string, std::string >( {
        /* Most Arab countries map to Arabic keyboard (Default) */
        { "ar_AE", arabic },
        { "ar_BH", arabic },
        { "ar_DZ", arabic },
        { "ar_EG", arabic },
        { "ar_IN", arabic },
        { "ar_IQ", arabic },
        { "ar_JO", arabic },
        { "ar_KW", arabic },
        { "ar_LB", arabic },
        { "ar_LY", arabic },
        /* Not Morocco: use layout ma */
        { "ar_OM", arabic },
        { "ar_QA", arabic },
        { "ar_SA", arabic },
        { "ar_SD", arabic },
        { "ar_SS", arabic },
        /* Not Syria: use layout sy */
        { "ar_TN", arabic },
        { "ar_YE", arabic },
        { "ca_ES", "cat_ES" }, /* Catalan */
        { "as_ES", "ast_ES" }, /* Asturian */
        { "en_CA", "us" }, /* Canadian English */
        { "el_CY", "gr" }, /* Greek in Cyprus */
        { "el_GR", "gr" }, /* Greek in Greeze */
        { "ig_NG", "igbo_NG" }, /* Igbo in Nigeria */
        { "ha_NG", "hausa_NG" } /* Hausa */
    } );

    ui->listLayout->setFocus();

    // Try to preselect a layout, depending on language and locale
    Calamares::GlobalStorage* gs = Calamares::JobQueue::instance()->globalStorage();
    QString lang = gs->value( "localeConf" ).toMap().value( "LANG" ).toString();

    cDebug() << "Got locale language" << lang;
    if ( !lang.isEmpty() )
    {
        // Chop off .codeset and @modifier
        int index = lang.indexOf( '.' );
        if ( index >= 0 )
        {
            lang.truncate( index );
        }
        index = lang.indexOf( '@' );
        if ( index >= 0 )
        {
            lang.truncate( index );
        }

        lang.replace( '-', '_' );  // Normalize separators
    }
    if ( !lang.isEmpty() )
    {
        std::string lang_s = lang.toStdString();
        if ( specialCaseMap.contains( lang_s ) )
        {
            QString newLang = QString::fromStdString( specialCaseMap.value( lang_s ) );
            cDebug() << Logger::SubEntry << "special case language" << lang << "becomes" << newLang;
            lang = newLang;
        }
    }
    if ( !lang.isEmpty() )
    {
        const auto langParts = lang.split( '_', SplitSkipEmptyParts );

        // Note that this his string is not fit for display purposes!
        // It doesn't come from QLocale::nativeCountryName.
        QString country = QLocale::countryToString( QLocale( lang ).country() );
        cDebug() << Logger::SubEntry << "extracted country" << country << "::" << langParts;

        guessLayout( langParts );
    }
}


void
KeyboardPage::finalize()
{
    Calamares::GlobalStorage* gs = Calamares::JobQueue::instance()->globalStorage();
    if ( !m_selectedLayout.isEmpty() )
    {
        gs->insert( "keyboardLayout", m_selectedLayout );
        gs->insert( "keyboardVariant", m_selectedVariant );  //empty means default variant
    }

    //FIXME: also store keyboard model for something?
}


void
KeyboardPage::updateVariants( const QPersistentModelIndex& currentItem, QString currentVariant )
{
    // Block signals
    ui->listVariant->blockSignals( true );

    QMap< QString, QString > variants
        = currentItem.data( KeyboardLayoutModel::KeyboardVariantsRole ).value< QMap< QString, QString > >();
    QMapIterator< QString, QString > li( variants );
    LayoutItem* defaultItem = nullptr;

    ui->listVariant->clear();

    while ( li.hasNext() )
    {
        li.next();

        LayoutItem* item = new LayoutItem();
        item->setText( li.key() );
        item->data = li.value();
        ui->listVariant->addItem( item );

        // currentVariant defaults to QString(). It is only non-empty during the
        // initial setup.
        if ( li.value() == currentVariant )
        {
            defaultItem = item;
        }
    }

    // Unblock signals
    ui->listVariant->blockSignals( false );

    // Set to default value
    if ( defaultItem )
    {
        ui->listVariant->setCurrentItem( defaultItem );
    }
}


void
KeyboardPage::onListLayoutCurrentItemChanged( const QModelIndex& current, const QModelIndex& previous )
{
    Q_UNUSED( previous )
    if ( !current.isValid() )
    {
        return;
    }

    updateVariants( QPersistentModelIndex( current ) );
}

/* Returns stringlist with suitable setxkbmap command-line arguments
 * to set the given @p layout and @p variant.
 */
static inline QStringList
xkbmap_args( const QString& layout, const QString& variant )
{
    QStringList r { "-layout", layout };
    if ( !variant.isEmpty() )
    {
        r << "-variant" << variant;
    }
    return r;
}

void
KeyboardPage::onListVariantCurrentItemChanged( QListWidgetItem* current, QListWidgetItem* previous )
{
    Q_UNUSED( previous )

    QPersistentModelIndex layoutIndex = ui->listLayout->currentIndex();
    LayoutItem* variantItem = dynamic_cast< LayoutItem* >( current );

    if ( !layoutIndex.isValid() || !variantItem )
    {
        return;
    }

    QString layout = layoutIndex.data( KeyboardLayoutModel::KeyboardLayoutKeyRole ).toString();
    QString variant = variantItem->data;

    m_keyboardPreview->setLayout( layout );
    m_keyboardPreview->setVariant( variant );

    //emit checkReady();

    // Set Xorg keyboard layout
    if ( m_setxkbmapTimer.isActive() )
    {
        m_setxkbmapTimer.stop();
        m_setxkbmapTimer.disconnect( this );
    }

    connect( &m_setxkbmapTimer, &QTimer::timeout, this, [=] {
        QProcess::execute( "setxkbmap", xkbmap_args( layout, variant ) );
        cDebug() << "xkbmap selection changed to: " << layout << '-' << variant;
        m_setxkbmapTimer.disconnect( this );
    } );
    m_setxkbmapTimer.start( QApplication::keyboardInputInterval() );

    m_selectedLayout = layout;
    m_selectedVariant = variant;
}
