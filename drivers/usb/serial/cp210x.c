// SPDX-License-Identifier: GPL-2.0
/*
 * Silicon Laboratories CP210x USB to RS232 serial adaptor driver
 *
 * Copyright (C) 2005 Craig Shelley (craig@microtron.org.uk)
 * Copyright (C) 2010-2021 Johan Hovold (johan@kernel.org)
 *
 * Support to set flow control line levels using TIOCMGET and TIOCMSET
 * thanks to Karl Hiramoto karl@hiramoto.org. RTSCTS hardware flow
 * control thanks to Munir Nassar nassarmu@real-time.com
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/gpio/driver.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#define DRIVER_DESC "Silicon Labs CP210x RS232 serial adaptor driver"

/*
 * Function Prototypes
 */
static int cp210x_open(struct tty_struct *tty, struct usb_serial_port *);
static void cp210x_close(struct usb_serial_port *);
static void cp210x_change_speed(struct tty_struct *, struct usb_serial_port *,
							struct ktermios *);
static void cp210x_set_termios(struct tty_struct *, struct usb_serial_port *,
							struct ktermios*);
static bool cp210x_tx_empty(struct usb_serial_port *port);
static int cp210x_tiocmget(struct tty_struct *);
static int cp210x_tiocmset(struct tty_struct *, unsigned int, unsigned int);
static int cp210x_tiocmset_port(struct usb_serial_port *port,
		unsigned int, unsigned int);
static void cp210x_break_ctl(struct tty_struct *, int);
static int cp210x_attach(struct usb_serial *);
static void cp210x_disconnect(struct usb_serial *);
static void cp210x_release(struct usb_serial *);
static int cp210x_port_probe(struct usb_serial_port *);
static void cp210x_port_remove(struct usb_serial_port *);
static void cp210x_dtr_rts(struct usb_serial_port *port, int on);
static void cp210x_process_read_urb(struct urb *urb);
static void cp210x_enable_event_mode(struct usb_serial_port *port);
static void cp210x_disable_event_mode(struct usb_serial_port *port);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0404, 0x034C) },	/* NCR Retail IO Box */
	{ USB_DEVICE(0x045B, 0x0053) }, /* Renesas RX610 RX-Stick */
	{ USB_DEVICE(0x0471, 0x066A) }, /* AKTAKOM ACE-1001 cable */
	{ USB_DEVICE(0x0489, 0xE000) }, /* Pirelli Broadband S.p.A, DP-L10 SIP/GSM Mobile */
	{ USB_DEVICE(0x0489, 0xE003) }, /* Pirelli Broadband S.p.A, DP-L10 SIP/GSM Mobile */
	{ USB_DEVICE(0x0745, 0x1000) }, /* CipherLab USB CCD Barcode Scanner 1000 */
	{ USB_DEVICE(0x0846, 0x1100) }, /* NetGear Managed Switch M4100 series, M5300 series, M7100 series */
	{ USB_DEVICE(0x08e6, 0x5501) }, /* Gemalto Prox-PU/CU contactless smartcard reader */
	{ USB_DEVICE(0x08FD, 0x000A) }, /* Digianswer A/S , ZigBee/802.15.4 MAC Device */
	{ USB_DEVICE(0x0908, 0x01FF) }, /* Siemens RUGGEDCOM USB Serial Console */
	{ USB_DEVICE(0x0988, 0x0578) }, /* Teraoka AD2000 */
	{ USB_DEVICE(0x0B00, 0x3070) }, /* Ingenico 3070 */
	{ USB_DEVICE(0x0BED, 0x1100) }, /* MEI (TM) Cashflow-SC Bill/Voucher Acceptor */
	{ USB_DEVICE(0x0BED, 0x1101) }, /* MEI series 2000 Combo Acceptor */
	{ USB_DEVICE(0x0FCF, 0x1003) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x0FCF, 0x1004) }, /* Dynastream ANT2USB */
	{ USB_DEVICE(0x0FCF, 0x1006) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x0FDE, 0xCA05) }, /* OWL Wireless Electricity Monitor CM-160 */
	{ USB_DEVICE(0x106F, 0x0003) },	/* CPI / Money Controls Bulk Coin Recycler */
	{ USB_DEVICE(0x10A6, 0xAA26) }, /* Knock-off DCU-11 cable */
	{ USB_DEVICE(0x10AB, 0x10C5) }, /* Siemens MC60 Cable */
	{ USB_DEVICE(0x10B5, 0xAC70) }, /* Nokia CA-42 USB */
	{ USB_DEVICE(0x10C4, 0x0F91) }, /* Vstabi */
	{ USB_DEVICE(0x10C4, 0x1101) }, /* Arkham Technology DS101 Bus Monitor */
	{ USB_DEVICE(0x10C4, 0x1601) }, /* Arkham Technology DS101 Adapter */
	{ USB_DEVICE(0x10C4, 0x800A) }, /* SPORTident BSM7-D-USB main station */
	{ USB_DEVICE(0x10C4, 0x803B) }, /* Pololu USB-serial converter */
	{ USB_DEVICE(0x10C4, 0x8044) }, /* Cygnal Debug Adapter */
	{ USB_DEVICE(0x10C4, 0x804E) }, /* Software Bisque Paramount ME build-in converter */
	{ USB_DEVICE(0x10C4, 0x8053) }, /* Enfora EDG1228 */
	{ USB_DEVICE(0x10C4, 0x8054) }, /* Enfora GSM2228 */
	{ USB_DEVICE(0x10C4, 0x8056) }, /* Lorenz Messtechnik devices */
	{ USB_DEVICE(0x10C4, 0x8066) }, /* Argussoft In-System Programmer */
	{ USB_DEVICE(0x10C4, 0x806F) }, /* IMS USB to RS422 Converter Cable */
	{ USB_DEVICE(0x10C4, 0x807A) }, /* Crumb128 board */
	{ USB_DEVICE(0x10C4, 0x80C4) }, /* Cygnal Integrated Products, Inc., Optris infrared thermometer */
	{ USB_DEVICE(0x10C4, 0x80CA) }, /* Degree Controls Inc */
	{ USB_DEVICE(0x10C4, 0x80DD) }, /* Tracient RFID */
	{ USB_DEVICE(0x10C4, 0x80F6) }, /* Suunto sports instrument */
	{ USB_DEVICE(0x10C4, 0x8115) }, /* Arygon NFC/Mifare Reader */
	{ USB_DEVICE(0x10C4, 0x813D) }, /* Burnside Telecom Deskmobile */
	{ USB_DEVICE(0x10C4, 0x813F) }, /* Tams Master Easy Control */
	{ USB_DEVICE(0x10C4, 0x814A) }, /* West Mountain Radio RIGblaster P&P */
	{ USB_DEVICE(0x10C4, 0x814B) }, /* West Mountain Radio RIGtalk */
	{ USB_DEVICE(0x2405, 0x0003) }, /* West Mountain Radio RIGblaster Advantage */
	{ USB_DEVICE(0x10C4, 0x8156) }, /* B&G H3000 link cable */
	{ USB_DEVICE(0x10C4, 0x815E) }, /* Helicomm IP-Link 1220-DVM */
	{ USB_DEVICE(0x10C4, 0x815F) }, /* Timewave HamLinkUSB */
	{ USB_DEVICE(0x10C4, 0x817C) }, /* CESINEL MEDCAL N Power Quality Monitor */
	{ USB_DEVICE(0x10C4, 0x817D) }, /* CESINEL MEDCAL NT Power Quality Monitor */
	{ USB_DEVICE(0x10C4, 0x817E) }, /* CESINEL MEDCAL S Power Quality Monitor */
	{ USB_DEVICE(0x10C4, 0x818B) }, /* AVIT Research USB to TTL */
	{ USB_DEVICE(0x10C4, 0x819F) }, /* MJS USB Toslink Switcher */
	{ USB_DEVICE(0x10C4, 0x81A6) }, /* ThinkOptics WavIt */
	{ USB_DEVICE(0x10C4, 0x81A9) }, /* Multiplex RC Interface */
	{ USB_DEVICE(0x10C4, 0x81AC) }, /* MSD Dash Hawk */
	{ USB_DEVICE(0x10C4, 0x81AD) }, /* INSYS USB Modem */
	{ USB_DEVICE(0x10C4, 0x81C8) }, /* Lipowsky Industrie Elektronik GmbH, Baby-JTAG */
	{ USB_DEVICE(0x10C4, 0x81D7) }, /* IAI Corp. RCB-CV-USB USB to RS485 Adaptor */
	{ USB_DEVICE(0x10C4, 0x81E2) }, /* Lipowsky Industrie Elektronik GmbH, Baby-LIN */
	{ USB_DEVICE(0x10C4, 0x81E7) }, /* Aerocomm Radio */
	{ USB_DEVICE(0x10C4, 0x81E8) }, /* Zephyr Bioharness */
	{ USB_DEVICE(0x10C4, 0x81F2) }, /* C1007 HF band RFID controller */
	{ USB_DEVICE(0x10C4, 0x8218) }, /* Lipowsky Industrie Elektronik GmbH, HARP-1 */
	{ USB_DEVICE(0x10C4, 0x822B) }, /* Modem EDGE(GSM) Comander 2 */
	{ USB_DEVICE(0x10C4, 0x826B) }, /* Cygnal Integrated Products, Inc., Fasttrax GPS demonstration module */
	{ USB_DEVICE(0x10C4, 0x8281) }, /* Nanotec Plug & Drive */
	{ USB_DEVICE(0x10C4, 0x8293) }, /* Telegesis ETRX2USB */
	{ USB_DEVICE(0x10C4, 0x82EF) }, /* CESINEL FALCO 6105 AC Power Supply */
	{ USB_DEVICE(0x10C4, 0x82F1) }, /* CESINEL MEDCAL EFD Earth Fault Detector */
	{ USB_DEVICE(0x10C4, 0x82F2) }, /* CESINEL MEDCAL ST Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x82F4) }, /* Starizona MicroTouch */
	{ USB_DEVICE(0x10C4, 0x82F9) }, /* Procyon AVS */
	{ USB_DEVICE(0x10C4, 0x8341) }, /* Siemens MC35PU GPRS Modem */
	{ USB_DEVICE(0x10C4, 0x8382) }, /* Cygnal Integrated Products, Inc. */
	{ USB_DEVICE(0x10C4, 0x83A8) }, /* Amber Wireless AMB2560 */
	{ USB_DEVICE(0x10C4, 0x83AA) }, /* Mark-10 Digital Force Gauge */
	{ USB_DEVICE(0x10C4, 0x83D8) }, /* DekTec DTA Plus VHF/UHF Booster/Attenuator */
	{ USB_DEVICE(0x10C4, 0x8411) }, /* Kyocera GPS Module */
	{ USB_DEVICE(0x10C4, 0x8418) }, /* IRZ Automation Teleport SG-10 GSM/GPRS Modem */
	{ USB_DEVICE(0x10C4, 0x846E) }, /* BEI USB Sensor Interface (VCP) */
	{ USB_DEVICE(0x10C4, 0x8470) }, /* Juniper Networks BX Series System Console */
	{ USB_DEVICE(0x10C4, 0x8477) }, /* Balluff RFID */
	{ USB_DEVICE(0x10C4, 0x84B6) }, /* Starizona Hyperion */
	{ USB_DEVICE(0x10C4, 0x851E) }, /* CESINEL MEDCAL PT Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x85A7) }, /* LifeScan OneTouch Verio IQ */
	{ USB_DEVICE(0x10C4, 0x85B8) }, /* CESINEL ReCon T Energy Logger */
	{ USB_DEVICE(0x10C4, 0x85EA) }, /* AC-Services IBUS-IF */
	{ USB_DEVICE(0x10C4, 0x85EB) }, /* AC-Services CIS-IBUS */
	{ USB_DEVICE(0x10C4, 0x85F8) }, /* Virtenio Preon32 */
	{ USB_DEVICE(0x10C4, 0x8664) }, /* AC-Services CAN-IF */
	{ USB_DEVICE(0x10C4, 0x8665) }, /* AC-Services OBD-IF */
	{ USB_DEVICE(0x10C4, 0x8856) },	/* CEL EM357 ZigBee USB Stick - LR */
	{ USB_DEVICE(0x10C4, 0x8857) },	/* CEL EM357 ZigBee USB Stick */
	{ USB_DEVICE(0x10C4, 0x88A4) }, /* MMB Networks ZigBee USB Device */
	{ USB_DEVICE(0x10C4, 0x88A5) }, /* Planet Innovation Ingeni ZigBee USB Device */
	{ USB_DEVICE(0x10C4, 0x88D8) }, /* Acuity Brands nLight Air Adapter */
	{ USB_DEVICE(0x10C4, 0x88FB) }, /* CESINEL MEDCAL STII Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x8938) }, /* CESINEL MEDCAL S II Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x8946) }, /* Ketra N1 Wireless Interface */
	{ USB_DEVICE(0x10C4, 0x8962) }, /* Brim Brothers charging dock */
	{ USB_DEVICE(0x10C4, 0x8977) },	/* CEL MeshWorks DevKit Device */
	{ USB_DEVICE(0x10C4, 0x8998) }, /* KCF Technologies PRN */
	{ USB_DEVICE(0x10C4, 0x89A4) }, /* CESINEL FTBC Flexible Thyristor Bridge Controller */
	{ USB_DEVICE(0x10C4, 0x89FB) }, /* Qivicon ZigBee USB Radio Stick */
	{ USB_DEVICE(0x10C4, 0x8A2A) }, /* HubZ dual ZigBee and Z-Wave dongle */
	{ USB_DEVICE(0x10C4, 0x8A5B) }, /* CEL EM3588 ZigBee USB Stick */
	{ USB_DEVICE(0x10C4, 0x8A5E) }, /* CEL EM3588 ZigBee USB Stick Long Range */
	{ USB_DEVICE(0x10C4, 0x8B34) }, /* Qivicon ZigBee USB Radio Stick */
	{ USB_DEVICE(0x10C4, 0xEA60) }, /* Silicon Labs factory default */
	{ USB_DEVICE(0x10C4, 0xEA61) }, /* Silicon Labs factory default */
	{ USB_DEVICE(0x10C4, 0xEA63) }, /* Silicon Labs Windows Update (CP2101-4/CP2102N) */
	{ USB_DEVICE(0x10C4, 0xEA70) }, /* Silicon Labs factory default */
	{ USB_DEVICE(0x10C4, 0xEA71) }, /* Infinity GPS-MIC-1 Radio Monophone */
	{ USB_DEVICE(0x10C4, 0xEA7A) }, /* Silicon Labs Windows Update (CP2105) */
	{ USB_DEVICE(0x10C4, 0xEA7B) }, /* Silicon Labs Windows Update (CP2108) */
	{ USB_DEVICE(0x10C4, 0xF001) }, /* Elan Digital Systems USBscope50 */
	{ USB_DEVICE(0x10C4, 0xF002) }, /* Elan Digital Systems USBwave12 */
	{ USB_DEVICE(0x10C4, 0xF003) }, /* Elan Digital Systems USBpulse100 */
	{ USB_DEVICE(0x10C4, 0xF004) }, /* Elan Digital Systems USBcount50 */
	{ USB_DEVICE(0x10C5, 0xEA61) }, /* Silicon Labs MobiData GPRS USB Modem */
	{ USB_DEVICE(0x10CE, 0xEA6A) }, /* Silicon Labs MobiData GPRS USB Modem 100EU */
	{ USB_DEVICE(0x12B8, 0xEC60) }, /* Link G4 ECU */
	{ USB_DEVICE(0x12B8, 0xEC62) }, /* Link G4+ ECU */
	{ USB_DEVICE(0x13AD, 0x9999) }, /* Baltech card reader */
	{ USB_DEVICE(0x1555, 0x0004) }, /* Owen AC4 USB-RS485 Converter */
	{ USB_DEVICE(0x155A, 0x1006) },	/* ELDAT Easywave RX09 */
	{ USB_DEVICE(0x166A, 0x0201) }, /* Clipsal 5500PACA C-Bus Pascal Automation Controller */
	{ USB_DEVICE(0x166A, 0x0301) }, /* Clipsal 5800PC C-Bus Wireless PC Interface */
	{ USB_DEVICE(0x166A, 0x0303) }, /* Clipsal 5500PCU C-Bus USB interface */
	{ USB_DEVICE(0x166A, 0x0304) }, /* Clipsal 5000CT2 C-Bus Black and White Touchscreen */
	{ USB_DEVICE(0x166A, 0x0305) }, /* Clipsal C-5000CT2 C-Bus Spectrum Colour Touchscreen */
	{ USB_DEVICE(0x166A, 0x0401) }, /* Clipsal L51xx C-Bus Architectural Dimmer */
	{ USB_DEVICE(0x166A, 0x0101) }, /* Clipsal 5560884 C-Bus Multi-room Audio Matrix Switcher */
	{ USB_DEVICE(0x16C0, 0x09B0) }, /* Lunatico Seletek */
	{ USB_DEVICE(0x16C0, 0x09B1) }, /* Lunatico Seletek */
	{ USB_DEVICE(0x16D6, 0x0001) }, /* Jablotron serial interface */
	{ USB_DEVICE(0x16DC, 0x0010) }, /* W-IE-NE-R Plein & Baus GmbH PL512 Power Supply */
	{ USB_DEVICE(0x16DC, 0x0011) }, /* W-IE-NE-R Plein & Baus GmbH RCM Remote Control for MARATON Power Supply */
	{ USB_DEVICE(0x16DC, 0x0012) }, /* W-IE-NE-R Plein & Baus GmbH MPOD Multi Channel Power Supply */
	{ USB_DEVICE(0x16DC, 0x0015) }, /* W-IE-NE-R Plein & Baus GmbH CML Control, Monitoring and Data Logger */
	{ USB_DEVICE(0x17A8, 0x0001) }, /* Kamstrup Optical Eye/3-wire */
	{ USB_DEVICE(0x17A8, 0x0005) }, /* Kamstrup M-Bus Master MultiPort 250D */
	{ USB_DEVICE(0x17A8, 0x0101) }, /* Kamstrup 868 MHz wM-Bus C-Mode Meter Reader (Int Ant) */
	{ USB_DEVICE(0x17A8, 0x0102) }, /* Kamstrup 868 MHz wM-Bus C-Mode Meter Reader (Ext Ant) */
	{ USB_DEVICE(0x17F4, 0xAAAA) }, /* Wavesense Jazz blood glucose meter */
	{ USB_DEVICE(0x1843, 0x0200) }, /* Vaisala USB Instrument Cable */
	{ USB_DEVICE(0x18EF, 0xE00F) }, /* ELV USB-I2C-Interface */
	{ USB_DEVICE(0x18EF, 0xE025) }, /* ELV Marble Sound Board 1 */
	{ USB_DEVICE(0x18EF, 0xE030) }, /* ELV ALC 8xxx Battery Charger */
	{ USB_DEVICE(0x18EF, 0xE032) }, /* ELV TFD500 Data Logger */
	{ USB_DEVICE(0x1901, 0x0190) }, /* GE B850 CP2105 Recorder interface */
	{ USB_DEVICE(0x1901, 0x0193) }, /* GE B650 CP2104 PMC interface */
	{ USB_DEVICE(0x1901, 0x0194) },	/* GE Healthcare Remote Alarm Box */
	{ USB_DEVICE(0x1901, 0x0195) },	/* GE B850/B650/B450 CP2104 DP UART interface */
	{ USB_DEVICE(0x1901, 0x0196) },	/* GE B850 CP2105 DP UART interface */
	{ USB_DEVICE(0x1901, 0x0197) }, /* GE CS1000 M.2 Key E serial interface */
	{ USB_DEVICE(0x1901, 0x0198) }, /* GE CS1000 Display serial interface */
	{ USB_DEVICE(0x199B, 0xBA30) }, /* LORD WSDA-200-USB */
	{ USB_DEVICE(0x19CF, 0x3000) }, /* Parrot NMEA GPS Flight Recorder */
	{ USB_DEVICE(0x1ADB, 0x0001) }, /* Schweitzer Engineering C662 Cable */
	{ USB_DEVICE(0x1B1C, 0x1C00) }, /* Corsair USB Dongle */
	{ USB_DEVICE(0x1BA4, 0x0002) },	/* Silicon Labs 358x factory default */
	{ USB_DEVICE(0x1BE3, 0x07A6) }, /* WAGO 750-923 USB Service Cable */
	{ USB_DEVICE(0x1D6F, 0x0010) }, /* Seluxit ApS RF Dongle */
	{ USB_DEVICE(0x1E29, 0x0102) }, /* Festo CPX-USB */
	{ USB_DEVICE(0x1E29, 0x0501) }, /* Festo CMSP */
	{ USB_DEVICE(0x1FB9, 0x0100) }, /* Lake Shore Model 121 Current Source */
	{ USB_DEVICE(0x1FB9, 0x0200) }, /* Lake Shore Model 218A Temperature Monitor */
	{ USB_DEVICE(0x1FB9, 0x0201) }, /* Lake Shore Model 219 Temperature Monitor */
	{ USB_DEVICE(0x1FB9, 0x0202) }, /* Lake Shore Model 233 Temperature Transmitter */
	{ USB_DEVICE(0x1FB9, 0x0203) }, /* Lake Shore Model 235 Temperature Transmitter */
	{ USB_DEVICE(0x1FB9, 0x0300) }, /* Lake Shore Model 335 Temperature Controller */
	{ USB_DEVICE(0x1FB9, 0x0301) }, /* Lake Shore Model 336 Temperature Controller */
	{ USB_DEVICE(0x1FB9, 0x0302) }, /* Lake Shore Model 350 Temperature Controller */
	{ USB_DEVICE(0x1FB9, 0x0303) }, /* Lake Shore Model 371 AC Bridge */
	{ USB_DEVICE(0x1FB9, 0x0400) }, /* Lake Shore Model 411 Handheld Gaussmeter */
	{ USB_DEVICE(0x1FB9, 0x0401) }, /* Lake Shore Model 425 Gaussmeter */
	{ USB_DEVICE(0x1FB9, 0x0402) }, /* Lake Shore Model 455A Gaussmeter */
	{ USB_DEVICE(0x1FB9, 0x0403) }, /* Lake Shore Model 475A Gaussmeter */
	{ USB_DEVICE(0x1FB9, 0x0404) }, /* Lake Shore Model 465 Three Axis Gaussmeter */
	{ USB_DEVICE(0x1FB9, 0x0600) }, /* Lake Shore Model 625A Superconducting MPS */
	{ USB_DEVICE(0x1FB9, 0x0601) }, /* Lake Shore Model 642A Magnet Power Supply */
	{ USB_DEVICE(0x1FB9, 0x0602) }, /* Lake Shore Model 648 Magnet Power Supply */
	{ USB_DEVICE(0x1FB9, 0x0700) }, /* Lake Shore Model 737 VSM Controller */
	{ USB_DEVICE(0x1FB9, 0x0701) }, /* Lake Shore Model 776 Hall Matrix */
	{ USB_DEVICE(0x2184, 0x0030) }, /* GW Instek GDM-834x Digital Multimeter */
	{ USB_DEVICE(0x2626, 0xEA60) }, /* Aruba Networks 7xxx USB Serial Console */
	{ USB_DEVICE(0x3195, 0xF190) }, /* Link Instruments MSO-19 */
	{ USB_DEVICE(0x3195, 0xF280) }, /* Link Instruments MSO-28 */
	{ USB_DEVICE(0x3195, 0xF281) }, /* Link Instruments MSO-28 */
	{ USB_DEVICE(0x3923, 0x7A0B) }, /* National Instruments USB Serial Console */
	{ USB_DEVICE(0x413C, 0x9500) }, /* DW700 GPS USB interface */
	{ } /* Terminating Entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

struct cp210x_serial_private {
#ifdef CONFIG_GPIOLIB
	struct gpio_chip	gc;
	bool			gpio_registered;
	u16			gpio_pushpull;
	u16			gpio_altfunc;
	u16			gpio_input;
#endif
	u8			partnum;
	u32			fw_version;
	speed_t			min_speed;
	speed_t			max_speed;
	bool			use_actual_rate;
	bool			no_flow_control;
	bool			no_event_mode;
};

enum cp210x_event_state {
	ES_DATA,
	ES_ESCAPE,
	ES_LSR,
	ES_LSR_DATA_0,
	ES_LSR_DATA_1,
	ES_MSR
};

struct cp210x_port_private {
	u8			bInterfaceNumber;
	bool			event_mode;
	enum cp210x_event_state event_state;
	u8			lsr;

	struct mutex		mutex;
	bool			crtscts;
	bool			dtr;
	bool			rts;
};

static struct usb_serial_driver cp210x_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"cp210x",
	},
	.id_table		= id_table,
	.num_ports		= 1,
	.bulk_in_size		= 256,
	.bulk_out_size		= 256,
	.open			= cp210x_open,
	.close			= cp210x_close,
	.break_ctl		= cp210x_break_ctl,
	.set_termios		= cp210x_set_termios,
	.tx_empty		= cp210x_tx_empty,
	.throttle		= usb_serial_generic_throttle,
	.unthrottle		= usb_serial_generic_unthrottle,
	.tiocmget		= cp210x_tiocmget,
	.tiocmset		= cp210x_tiocmset,
	.get_icount		= usb_serial_generic_get_icount,
	.attach			= cp210x_attach,
	.disconnect		= cp210x_disconnect,
	.release		= cp210x_release,
	.port_probe		= cp210x_port_probe,
	.port_remove		= cp210x_port_remove,
	.dtr_rts		= cp210x_dtr_rts,
	.process_read_urb	= cp210x_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&cp210x_device, NULL
};

/* Config request types */
#define REQTYPE_HOST_TO_INTERFACE	0x41
#define REQTYPE_INTERFACE_TO_HOST	0xc1
#define REQTYPE_HOST_TO_DEVICE	0x40
#define REQTYPE_DEVICE_TO_HOST	0xc0

/* Config request codes */
#define CP210X_IFC_ENABLE	0x00
#define CP210X_SET_BAUDDIV	0x01
#define CP210X_GET_BAUDDIV	0x02
#define CP210X_SET_LINE_CTL	0x03
#define CP210X_GET_LINE_CTL	0x04
#define CP210X_SET_BREAK	0x05
#define CP210X_IMM_CHAR		0x06
#define CP210X_SET_MHS		0x07
#define CP210X_GET_MDMSTS	0x08
#define CP210X_SET_XON		0x09
#define CP210X_SET_XOFF		0x0A
#define CP210X_SET_EVENTMASK	0x0B
#define CP210X_GET_EVENTMASK	0x0C
#define CP210X_SET_CHAR		0x0D
#define CP210X_GET_CHARS	0x0E
#define CP210X_GET_PROPS	0x0F
#define CP210X_GET_COMM_STATUS	0x10
#define CP210X_RESET		0x11
#define CP210X_PURGE		0x12
#define CP210X_SET_FLOW		0x13
#define CP210X_GET_FLOW		0x14
#define CP210X_EMBED_EVENTS	0x15
#define CP210X_GET_EVENTSTATE	0x16
#define CP210X_SET_CHARS	0x19
#define CP210X_GET_BAUDRATE	0x1D
#define CP210X_SET_BAUDRATE	0x1E
#define CP210X_VENDOR_SPECIFIC	0xFF

/* CP210X_IFC_ENABLE */
#define UART_ENABLE		0x0001
#define UART_DISABLE		0x0000

/* CP210X_(SET|GET)_BAUDDIV */
#define BAUD_RATE_GEN_FREQ	0x384000

/* CP210X_(SET|GET)_LINE_CTL */
#define BITS_DATA_MASK		0X0f00
#define BITS_DATA_5		0X0500
#define BITS_DATA_6		0X0600
#define BITS_DATA_7		0X0700
#define BITS_DATA_8		0X0800
#define BITS_DATA_9		0X0900

#define BITS_PARITY_MASK	0x00f0
#define BITS_PARITY_NONE	0x0000
#define BITS_PARITY_ODD		0x0010
#define BITS_PARITY_EVEN	0x0020
#define BITS_PARITY_MARK	0x0030
#define BITS_PARITY_SPACE	0x0040

#define BITS_STOP_MASK		0x000f
#define BITS_STOP_1		0x0000
#define BITS_STOP_1_5		0x0001
#define BITS_STOP_2		0x0002

/* CP210X_SET_BREAK */
#define BREAK_ON		0x0001
#define BREAK_OFF		0x0000

/* CP210X_(SET_MHS|GET_MDMSTS) */
#define CONTROL_DTR		0x0001
#define CONTROL_RTS		0x0002
#define CONTROL_CTS		0x0010
#define CONTROL_DSR		0x0020
#define CONTROL_RING		0x0040
#define CONTROL_DCD		0x0080
#define CONTROL_WRITE_DTR	0x0100
#define CONTROL_WRITE_RTS	0x0200

/* CP210X_(GET|SET)_CHARS */
struct cp210x_special_chars {
	u8	bEofChar;
	u8	bErrorChar;
	u8	bBreakChar;
	u8	bEventChar;
	u8	bXonChar;
	u8	bXoffChar;
};

/* CP210X_VENDOR_SPECIFIC values */
#define CP210X_GET_FW_VER	0x000E
#define CP210X_READ_2NCONFIG	0x000E
#define CP210X_GET_FW_VER_2N	0x0010
#define CP210X_READ_LATCH	0x00C2
#define CP210X_GET_PARTNUM	0x370B
#define CP210X_GET_PORTCONFIG	0x370C
#define CP210X_GET_DEVICEMODE	0x3711
#define CP210X_WRITE_LATCH	0x37E1

/* Part number definitions */
#define CP210X_PARTNUM_CP2101	0x01
#define CP210X_PARTNUM_CP2102	0x02
#define CP210X_PARTNUM_CP2103	0x03
#define CP210X_PARTNUM_CP2104	0x04
#define CP210X_PARTNUM_CP2105	0x05
#define CP210X_PARTNUM_CP2108	0x08
#define CP210X_PARTNUM_CP2102N_QFN28	0x20
#define CP210X_PARTNUM_CP2102N_QFN24	0x21
#define CP210X_PARTNUM_CP2102N_QFN20	0x22
#define CP210X_PARTNUM_UNKNOWN	0xFF

/* CP210X_GET_COMM_STATUS returns these 0x13 bytes */
struct cp210x_comm_status {
	__le32   ulErrors;
	__le32   ulHoldReasons;
	__le32   ulAmountInInQueue;
	__le32   ulAmountInOutQueue;
	u8       bEofReceived;
	u8       bWaitForImmediate;
	u8       bReserved;
} __packed;

/*
 * CP210X_PURGE - 16 bits passed in wValue of USB request.
 * SiLabs app note AN571 gives a strange description of the 4 bits:
 * bit 0 or bit 2 clears the transmit queue and 1 or 3 receive.
 * writing 1 to all, however, purges cp2108 well enough to avoid the hang.
 */
#define PURGE_ALL		0x000f

/* CP210X_EMBED_EVENTS */
#define CP210X_ESCCHAR		0xec

#define CP210X_LSR_OVERRUN	BIT(1)
#define CP210X_LSR_PARITY	BIT(2)
#define CP210X_LSR_FRAME	BIT(3)
#define CP210X_LSR_BREAK	BIT(4)


/* CP210X_GET_FLOW/CP210X_SET_FLOW read/write these 0x10 bytes */
struct cp210x_flow_ctl {
	__le32	ulControlHandshake;
	__le32	ulFlowReplace;
	__le32	ulXonLimit;
	__le32	ulXoffLimit;
};

/* cp210x_flow_ctl::ulControlHandshake */
#define CP210X_SERIAL_DTR_MASK		GENMASK(1, 0)
#define CP210X_SERIAL_DTR_INACTIVE	(0 << 0)
#define CP210X_SERIAL_DTR_ACTIVE	(1 << 0)
#define CP210X_SERIAL_DTR_FLOW_CTL	(2 << 0)
#define CP210X_SERIAL_CTS_HANDSHAKE	BIT(3)
#define CP210X_SERIAL_DSR_HANDSHAKE	BIT(4)
#define CP210X_SERIAL_DCD_HANDSHAKE	BIT(5)
#define CP210X_SERIAL_DSR_SENSITIVITY	BIT(6)

/* cp210x_flow_ctl::ulFlowReplace */
#define CP210X_SERIAL_AUTO_TRANSMIT	BIT(0)
#define CP210X_SERIAL_AUTO_RECEIVE	BIT(1)
#define CP210X_SERIAL_ERROR_CHAR	BIT(2)
#define CP210X_SERIAL_NULL_STRIPPING	BIT(3)
#define CP210X_SERIAL_BREAK_CHAR	BIT(4)
#define CP210X_SERIAL_RTS_MASK		GENMASK(7, 6)
#define CP210X_SERIAL_RTS_INACTIVE	(0 << 6)
#define CP210X_SERIAL_RTS_ACTIVE	(1 << 6)
#define CP210X_SERIAL_RTS_FLOW_CTL	(2 << 6)
#define CP210X_SERIAL_XOFF_CONTINUE	BIT(31)

/* CP210X_VENDOR_SPECIFIC, CP210X_GET_DEVICEMODE call reads these 0x2 bytes. */
struct cp210x_pin_mode {
	u8	eci;
	u8	sci;
};

#define CP210X_PIN_MODE_MODEM		0
#define CP210X_PIN_MODE_GPIO		BIT(0)

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_GET_PORTCONFIG call reads these 0xf bytes
 * on a CP2105 chip. Structure needs padding due to unused/unspecified bytes.
 */
struct cp210x_dual_port_config {
	__le16	gpio_mode;
	u8	__pad0[2];
	__le16	reset_state;
	u8	__pad1[4];
	__le16	suspend_state;
	u8	sci_cfg;
	u8	eci_cfg;
	u8	device_cfg;
} __packed;

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_GET_PORTCONFIG call reads these 0xd bytes
 * on a CP2104 chip. Structure needs padding due to unused/unspecified bytes.
 */
struct cp210x_single_port_config {
	__le16	gpio_mode;
	u8	__pad0[2];
	__le16	reset_state;
	u8	__pad1[4];
	__le16	suspend_state;
	u8	device_cfg;
} __packed;

/* GPIO modes */
#define CP210X_SCI_GPIO_MODE_OFFSET	9
#define CP210X_SCI_GPIO_MODE_MASK	GENMASK(11, 9)

#define CP210X_ECI_GPIO_MODE_OFFSET	2
#define CP210X_ECI_GPIO_MODE_MASK	GENMASK(3, 2)

#define CP210X_GPIO_MODE_OFFSET		8
#define CP210X_GPIO_MODE_MASK		GENMASK(11, 8)

/* CP2105 port configuration values */
#define CP2105_GPIO0_TXLED_MODE		BIT(0)
#define CP2105_GPIO1_RXLED_MODE		BIT(1)
#define CP2105_GPIO1_RS485_MODE		BIT(2)

/* CP2104 port configuration values */
#define CP2104_GPIO0_TXLED_MODE		BIT(0)
#define CP2104_GPIO1_RXLED_MODE		BIT(1)
#define CP2104_GPIO2_RS485_MODE		BIT(2)

struct cp210x_quad_port_state {
	__le16 gpio_mode_pb0;
	__le16 gpio_mode_pb1;
	__le16 gpio_mode_pb2;
	__le16 gpio_mode_pb3;
	__le16 gpio_mode_pb4;

	__le16 gpio_lowpower_pb0;
	__le16 gpio_lowpower_pb1;
	__le16 gpio_lowpower_pb2;
	__le16 gpio_lowpower_pb3;
	__le16 gpio_lowpower_pb4;

	__le16 gpio_latch_pb0;
	__le16 gpio_latch_pb1;
	__le16 gpio_latch_pb2;
	__le16 gpio_latch_pb3;
	__le16 gpio_latch_pb4;
};

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_GET_PORTCONFIG call reads these 0x49 bytes
 * on a CP2108 chip.
 *
 * See https://www.silabs.com/documents/public/application-notes/an978-cp210x-usb-to-uart-api-specification.pdf
 */
struct cp210x_quad_port_config {
	struct cp210x_quad_port_state reset_state;
	struct cp210x_quad_port_state suspend_state;
	u8 ipdelay_ifc[4];
	u8 enhancedfxn_ifc[4];
	u8 enhancedfxn_device;
	u8 extclkfreq[4];
} __packed;

#define CP2108_EF_IFC_GPIO_TXLED		0x01
#define CP2108_EF_IFC_GPIO_RXLED		0x02
#define CP2108_EF_IFC_GPIO_RS485		0x04
#define CP2108_EF_IFC_GPIO_RS485_LOGIC		0x08
#define CP2108_EF_IFC_GPIO_CLOCK		0x10
#define CP2108_EF_IFC_DYNAMIC_SUSPEND		0x40

/* CP2102N configuration array indices */
#define CP210X_2NCONFIG_CONFIG_VERSION_IDX	2
#define CP210X_2NCONFIG_GPIO_MODE_IDX		581
#define CP210X_2NCONFIG_GPIO_RSTLATCH_IDX	587
#define CP210X_2NCONFIG_GPIO_CONTROL_IDX	600

/* CP2102N QFN20 port configuration values */
#define CP2102N_QFN20_GPIO2_TXLED_MODE		BIT(2)
#define CP2102N_QFN20_GPIO3_RXLED_MODE		BIT(3)
#define CP2102N_QFN20_GPIO1_RS485_MODE		BIT(4)
#define CP2102N_QFN20_GPIO0_CLK_MODE		BIT(6)

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_WRITE_LATCH call writes these 0x02 bytes
 * for CP2102N, CP2103, CP2104 and CP2105.
 */
struct cp210x_gpio_write {
	u8	mask;
	u8	state;
};

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_WRITE_LATCH call writes these 0x04 bytes
 * for CP2108.
 */
struct cp210x_gpio_write16 {
	__le16	mask;
	__le16	state;
};

/*
 * Helper to get interface number when we only have struct usb_serial.
 */
static u8 cp210x_interface_num(struct usb_serial *serial)
{
	struct usb_host_interface *cur_altsetting;

	cur_altsetting = serial->interface->cur_altsetting;

	return cur_altsetting->desc.bInterfaceNumber;
}

/*
 * Reads a variable-sized block of CP210X_ registers, identified by req.
 * Returns data into buf in native USB byte order.
 */
static int cp210x_read_reg_block(struct usb_serial_port *port, u8 req,
		void *buf, int bufsize)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	void *dmabuf;
	int result;

	dmabuf = kmalloc(bufsize, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			req, REQTYPE_INTERFACE_TO_HOST, 0,
			port_priv->bInterfaceNumber, dmabuf, bufsize,
			USB_CTRL_GET_TIMEOUT);
	if (result == bufsize) {
		memcpy(buf, dmabuf, bufsize);
		result = 0;
	} else {
		dev_err(&port->dev, "failed get req 0x%x size %d status: %d\n",
				req, bufsize, result);
		if (result >= 0)
			result = -EIO;
	}

	kfree(dmabuf);

	return result;
}

/*
 * Reads any 8-bit CP210X_ register identified by req.
 */
static int cp210x_read_u8_reg(struct usb_serial_port *port, u8 req, u8 *val)
{
	return cp210x_read_reg_block(port, req, val, sizeof(*val));
}

/*
 * Reads a variable-sized vendor block of CP210X_ registers, identified by val.
 * Returns data into buf in native USB byte order.
 */
static int cp210x_read_vendor_block(struct usb_serial *serial, u8 type, u16 val,
				    void *buf, int bufsize)
{
	void *dmabuf;
	int result;

	dmabuf = kmalloc(bufsize, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 CP210X_VENDOR_SPECIFIC, type, val,
				 cp210x_interface_num(serial), dmabuf, bufsize,
				 USB_CTRL_GET_TIMEOUT);
	if (result == bufsize) {
		memcpy(buf, dmabuf, bufsize);
		result = 0;
	} else {
		dev_err(&serial->interface->dev,
			"failed to get vendor val 0x%04x size %d: %d\n", val,
			bufsize, result);
		if (result >= 0)
			result = -EIO;
	}

	kfree(dmabuf);

	return result;
}

/*
 * Writes any 16-bit CP210X_ register (req) whose value is passed
 * entirely in the wValue field of the USB request.
 */
static int cp210x_write_u16_reg(struct usb_serial_port *port, u8 req, u16 val)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	int result;

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			req, REQTYPE_HOST_TO_INTERFACE, val,
			port_priv->bInterfaceNumber, NULL, 0,
			USB_CTRL_SET_TIMEOUT);
	if (result < 0) {
		dev_err(&port->dev, "failed set request 0x%x status: %d\n",
				req, result);
	}

	return result;
}

/*
 * Writes a variable-sized block of CP210X_ registers, identified by req.
 * Data in buf must be in native USB byte order.
 */
static int cp210x_write_reg_block(struct usb_serial_port *port, u8 req,
		void *buf, int bufsize)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	void *dmabuf;
	int result;

	dmabuf = kmemdup(buf, bufsize, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			req, REQTYPE_HOST_TO_INTERFACE, 0,
			port_priv->bInterfaceNumber, dmabuf, bufsize,
			USB_CTRL_SET_TIMEOUT);

	kfree(dmabuf);

	if (result < 0) {
		dev_err(&port->dev, "failed set req 0x%x size %d status: %d\n",
				req, bufsize, result);
		return result;
	}

	return 0;
}

/*
 * Writes any 32-bit CP210X_ register identified by req.
 */
static int cp210x_write_u32_reg(struct usb_serial_port *port, u8 req, u32 val)
{
	__le32 le32_val;

	le32_val = cpu_to_le32(val);

	return cp210x_write_reg_block(port, req, &le32_val, sizeof(le32_val));
}

#ifdef CONFIG_GPIOLIB
/*
 * Writes a variable-sized vendor block of CP210X_ registers, identified by val.
 * Data in buf must be in native USB byte order.
 */
static int cp210x_write_vendor_block(struct usb_serial *serial, u8 type,
				     u16 val, void *buf, int bufsize)
{
	void *dmabuf;
	int result;

	dmabuf = kmemdup(buf, bufsize, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 CP210X_VENDOR_SPECIFIC, type, val,
				 cp210x_interface_num(serial), dmabuf, bufsize,
				 USB_CTRL_SET_TIMEOUT);

	kfree(dmabuf);

	if (result < 0) {
		dev_err(&serial->interface->dev,
			"failed to set vendor val 0x%04x size %d: %d\n", val,
			bufsize, result);
		return result;
	}

	return 0;
}
#endif

static int cp210x_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	int result;

	result = cp210x_write_u16_reg(port, CP210X_IFC_ENABLE, UART_ENABLE);
	if (result) {
		dev_err(&port->dev, "%s - Unable to enable UART\n", __func__);
		return result;
	}

	if (tty)
		cp210x_set_termios(tty, port, NULL);

	result = usb_serial_generic_open(tty, port);
	if (result)
		goto err_disable;

	return 0;

err_disable:
	cp210x_write_u16_reg(port, CP210X_IFC_ENABLE, UART_DISABLE);
	port_priv->event_mode = false;

	return result;
}

static void cp210x_close(struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);

	usb_serial_generic_close(port);

	/* Clear both queues; cp2108 needs this to avoid an occasional hang */
	cp210x_write_u16_reg(port, CP210X_PURGE, PURGE_ALL);

	cp210x_write_u16_reg(port, CP210X_IFC_ENABLE, UART_DISABLE);

	/* Disabling the interface disables event-insertion mode. */
	port_priv->event_mode = false;
}

static void cp210x_process_lsr(struct usb_serial_port *port, unsigned char lsr, char *flag)
{
	if (lsr & CP210X_LSR_BREAK) {
		port->icount.brk++;
		*flag = TTY_BREAK;
	} else if (lsr & CP210X_LSR_PARITY) {
		port->icount.parity++;
		*flag = TTY_PARITY;
	} else if (lsr & CP210X_LSR_FRAME) {
		port->icount.frame++;
		*flag = TTY_FRAME;
	}

	if (lsr & CP210X_LSR_OVERRUN) {
		port->icount.overrun++;
		tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);
	}
}

static bool cp210x_process_char(struct usb_serial_port *port, unsigned char *ch, char *flag)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);

	switch (port_priv->event_state) {
	case ES_DATA:
		if (*ch == CP210X_ESCCHAR) {
			port_priv->event_state = ES_ESCAPE;
			break;
		}
		return false;
	case ES_ESCAPE:
		switch (*ch) {
		case 0:
			dev_dbg(&port->dev, "%s - escape char\n", __func__);
			*ch = CP210X_ESCCHAR;
			port_priv->event_state = ES_DATA;
			return false;
		case 1:
			port_priv->event_state = ES_LSR_DATA_0;
			break;
		case 2:
			port_priv->event_state = ES_LSR;
			break;
		case 3:
			port_priv->event_state = ES_MSR;
			break;
		default:
			dev_err(&port->dev, "malformed event 0x%02x\n", *ch);
			port_priv->event_state = ES_DATA;
			break;
		}
		break;
	case ES_LSR_DATA_0:
		port_priv->lsr = *ch;
		port_priv->event_state = ES_LSR_DATA_1;
		break;
	case ES_LSR_DATA_1:
		dev_dbg(&port->dev, "%s - lsr = 0x%02x, data = 0x%02x\n",
				__func__, port_priv->lsr, *ch);
		cp210x_process_lsr(port, port_priv->lsr, flag);
		port_priv->event_state = ES_DATA;
		return false;
	case ES_LSR:
		dev_dbg(&port->dev, "%s - lsr = 0x%02x\n", __func__, *ch);
		port_priv->lsr = *ch;
		cp210x_process_lsr(port, port_priv->lsr, flag);
		port_priv->event_state = ES_DATA;
		break;
	case ES_MSR:
		dev_dbg(&port->dev, "%s - msr = 0x%02x\n", __func__, *ch);
		/* unimplemented */
		port_priv->event_state = ES_DATA;
		break;
	}

	return true;
}

static void cp210x_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	unsigned char *ch = urb->transfer_buffer;
	char flag;
	int i;

	if (!urb->actual_length)
		return;

	if (port_priv->event_mode) {
		for (i = 0; i < urb->actual_length; i++, ch++) {
			flag = TTY_NORMAL;

			if (cp210x_process_char(port, ch, &flag))
				continue;

			tty_insert_flip_char(&port->port, *ch, flag);
		}
	} else {
		tty_insert_flip_string(&port->port, ch, urb->actual_length);
	}
	tty_flip_buffer_push(&port->port);
}

/*
 * Read how many bytes are waiting in the TX queue.
 */
static int cp210x_get_tx_queue_byte_count(struct usb_serial_port *port,
		u32 *count)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	struct cp210x_comm_status *sts;
	int result;

	sts = kmalloc(sizeof(*sts), GFP_KERNEL);
	if (!sts)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			CP210X_GET_COMM_STATUS, REQTYPE_INTERFACE_TO_HOST,
			0, port_priv->bInterfaceNumber, sts, sizeof(*sts),
			USB_CTRL_GET_TIMEOUT);
	if (result == sizeof(*sts)) {
		*count = le32_to_cpu(sts->ulAmountInOutQueue);
		result = 0;
	} else {
		dev_err(&port->dev, "failed to get comm status: %d\n", result);
		if (result >= 0)
			result = -EIO;
	}

	kfree(sts);

	return result;
}

static bool cp210x_tx_empty(struct usb_serial_port *port)
{
	int err;
	u32 count;

	err = cp210x_get_tx_queue_byte_count(port, &count);
	if (err)
		return true;

	return !count;
}

struct cp210x_rate {
	speed_t rate;
	speed_t high;
};

static const struct cp210x_rate cp210x_an205_table1[] = {
	{ 300, 300 },
	{ 600, 600 },
	{ 1200, 1200 },
	{ 1800, 1800 },
	{ 2400, 2400 },
	{ 4000, 4000 },
	{ 4800, 4803 },
	{ 7200, 7207 },
	{ 9600, 9612 },
	{ 14400, 14428 },
	{ 16000, 16062 },
	{ 19200, 19250 },
	{ 28800, 28912 },
	{ 38400, 38601 },
	{ 51200, 51558 },
	{ 56000, 56280 },
	{ 57600, 58053 },
	{ 64000, 64111 },
	{ 76800, 77608 },
	{ 115200, 117028 },
	{ 128000, 129347 },
	{ 153600, 156868 },
	{ 230400, 237832 },
	{ 250000, 254234 },
	{ 256000, 273066 },
	{ 460800, 491520 },
	{ 500000, 567138 },
	{ 576000, 670254 },
	{ 921600, UINT_MAX }
};

/*
 * Quantises the baud rate as per AN205 Table 1
 */
static speed_t cp210x_get_an205_rate(speed_t baud)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cp210x_an205_table1); ++i) {
		if (baud <= cp210x_an205_table1[i].high)
			break;
	}

	return cp210x_an205_table1[i].rate;
}

static speed_t cp210x_get_actual_rate(speed_t baud)
{
	unsigned int prescale = 1;
	unsigned int div;

	if (baud <= 365)
		prescale = 4;

	div = DIV_ROUND_CLOSEST(48000000, 2 * prescale * baud);
	baud = 48000000 / (2 * prescale * div);

	return baud;
}

/*
 * CP2101 supports the following baud rates:
 *
 *	300, 600, 1200, 1800, 2400, 4800, 7200, 9600, 14400, 19200, 28800,
 *	38400, 56000, 57600, 115200, 128000, 230400, 460800, 921600
 *
 * CP2102 and CP2103 support the following additional rates:
 *
 *	4000, 16000, 51200, 64000, 76800, 153600, 250000, 256000, 500000,
 *	576000
 *
 * The device will map a requested rate to a supported one, but the result
 * of requests for rates greater than 1053257 is undefined (see AN205).
 *
 * CP2104, CP2105 and CP2110 support most rates up to 2M, 921k and 1M baud,
 * respectively, with an error less than 1%. The actual rates are determined
 * by
 *
 *	div = round(freq / (2 x prescale x request))
 *	actual = freq / (2 x prescale x div)
 *
 * For CP2104 and CP2105 freq is 48Mhz and prescale is 4 for request <= 365bps
 * or 1 otherwise.
 * For CP2110 freq is 24Mhz and prescale is 4 for request <= 300bps or 1
 * otherwise.
 */
static void cp210x_change_speed(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	u32 baud;

	/*
	 * This maps the requested rate to the actual rate, a valid rate on
	 * cp2102 or cp2103, or to an arbitrary rate in [1M, max_speed].
	 *
	 * NOTE: B0 is not implemented.
	 */
	baud = clamp(tty->termios.c_ospeed, priv->min_speed, priv->max_speed);

	if (priv->use_actual_rate)
		baud = cp210x_get_actual_rate(baud);
	else if (baud < 1000000)
		baud = cp210x_get_an205_rate(baud);

	dev_dbg(&port->dev, "%s - setting baud rate to %u\n", __func__, baud);
	if (cp210x_write_u32_reg(port, CP210X_SET_BAUDRATE, baud)) {
		dev_warn(&port->dev, "failed to set baud rate to %u\n", baud);
		if (old_termios)
			baud = old_termios->c_ospeed;
		else
			baud = 9600;
	}

	tty_encode_baud_rate(tty, baud, baud);
}

static void cp210x_enable_event_mode(struct usb_serial_port *port)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(port->serial);
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	int ret;

	if (port_priv->event_mode)
		return;

	if (priv->no_event_mode)
		return;

	port_priv->event_state = ES_DATA;
	port_priv->event_mode = true;

	ret = cp210x_write_u16_reg(port, CP210X_EMBED_EVENTS, CP210X_ESCCHAR);
	if (ret) {
		dev_err(&port->dev, "failed to enable events: %d\n", ret);
		port_priv->event_mode = false;
	}
}

static void cp210x_disable_event_mode(struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	int ret;

	if (!port_priv->event_mode)
		return;

	ret = cp210x_write_u16_reg(port, CP210X_EMBED_EVENTS, 0);
	if (ret) {
		dev_err(&port->dev, "failed to disable events: %d\n", ret);
		return;
	}

	port_priv->event_mode = false;
}

static bool cp210x_termios_change(const struct ktermios *a, const struct ktermios *b)
{
	bool iflag_change, cc_change;

	iflag_change = ((a->c_iflag ^ b->c_iflag) & (INPCK | IXON | IXOFF));
	cc_change = a->c_cc[VSTART] != b->c_cc[VSTART] ||
			a->c_cc[VSTOP] != b->c_cc[VSTOP];

	return tty_termios_hw_change(a, b) || iflag_change || cc_change;
}

static void cp210x_set_flow_control(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(port->serial);
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	struct cp210x_special_chars chars;
	struct cp210x_flow_ctl flow_ctl;
	u32 flow_repl;
	u32 ctl_hs;
	bool crtscts;
	int ret;

	/*
	 * Some CP2102N interpret ulXonLimit as ulFlowReplace (erratum
	 * CP2102N_E104). Report back that flow control is not supported.
	 */
	if (priv->no_flow_control) {
		tty->termios.c_cflag &= ~CRTSCTS;
		tty->termios.c_iflag &= ~(IXON | IXOFF);
	}

	if (old_termios &&
			C_CRTSCTS(tty) == (old_termios->c_cflag & CRTSCTS) &&
			I_IXON(tty) == (old_termios->c_iflag & IXON) &&
			I_IXOFF(tty) == (old_termios->c_iflag & IXOFF) &&
			START_CHAR(tty) == old_termios->c_cc[VSTART] &&
			STOP_CHAR(tty) == old_termios->c_cc[VSTOP]) {
		return;
	}

	if (I_IXON(tty) || I_IXOFF(tty)) {
		memset(&chars, 0, sizeof(chars));

		chars.bXonChar = START_CHAR(tty);
		chars.bXoffChar = STOP_CHAR(tty);

		ret = cp210x_write_reg_block(port, CP210X_SET_CHARS, &chars,
				sizeof(chars));
		if (ret) {
			dev_err(&port->dev, "failed to set special chars: %d\n",
					ret);
		}
	}

	mutex_lock(&port_priv->mutex);

	ret = cp210x_read_reg_block(port, CP210X_GET_FLOW, &flow_ctl,
			sizeof(flow_ctl));
	if (ret)
		goto out_unlock;

	ctl_hs = le32_to_cpu(flow_ctl.ulControlHandshake);
	flow_repl = le32_to_cpu(flow_ctl.ulFlowReplace);

	ctl_hs &= ~CP210X_SERIAL_DSR_HANDSHAKE;
	ctl_hs &= ~CP210X_SERIAL_DCD_HANDSHAKE;
	ctl_hs &= ~CP210X_SERIAL_DSR_SENSITIVITY;
	ctl_hs &= ~CP210X_SERIAL_DTR_MASK;
	if (port_priv->dtr)
		ctl_hs |= CP210X_SERIAL_DTR_ACTIVE;
	else
		ctl_hs |= CP210X_SERIAL_DTR_INACTIVE;

	flow_repl &= ~CP210X_SERIAL_RTS_MASK;
	if (C_CRTSCTS(tty)) {
		ctl_hs |= CP210X_SERIAL_CTS_HANDSHAKE;
		if (port_priv->rts)
			flow_repl |= CP210X_SERIAL_RTS_FLOW_CTL;
		else
			flow_repl |= CP210X_SERIAL_RTS_INACTIVE;
		crtscts = true;
	} else {
		ctl_hs &= ~CP210X_SERIAL_CTS_HANDSHAKE;
		if (port_priv->rts)
			flow_repl |= CP210X_SERIAL_RTS_ACTIVE;
		else
			flow_repl |= CP210X_SERIAL_RTS_INACTIVE;
		crtscts = false;
	}

	if (I_IXOFF(tty)) {
		flow_repl |= CP210X_SERIAL_AUTO_RECEIVE;

		flow_ctl.ulXonLimit = cpu_to_le32(128);
		flow_ctl.ulXoffLimit = cpu_to_le32(128);
	} else {
		flow_repl &= ~CP210X_SERIAL_AUTO_RECEIVE;
	}

	if (I_IXON(tty))
		flow_repl |= CP210X_SERIAL_AUTO_TRANSMIT;
	else
		flow_repl &= ~CP210X_SERIAL_AUTO_TRANSMIT;

	dev_dbg(&port->dev, "%s - ctrl = 0x%02x, flow = 0x%02x\n", __func__,
			ctl_hs, flow_repl);

	flow_ctl.ulControlHandshake = cpu_to_le32(ctl_hs);
	flow_ctl.ulFlowReplace = cpu_to_le32(flow_repl);

	ret = cp210x_write_reg_block(port, CP210X_SET_FLOW, &flow_ctl,
			sizeof(flow_ctl));
	if (ret)
		goto out_unlock;

	port_priv->crtscts = crtscts;
out_unlock:
	mutex_unlock(&port_priv->mutex);
}

static void cp210x_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(port->serial);
	u16 bits;
	int ret;

	if (old_termios && !cp210x_termios_change(&tty->termios, old_termios))
		return;

	if (!old_termios || tty->termios.c_ospeed != old_termios->c_ospeed)
		cp210x_change_speed(tty, port, old_termios);

	/* CP2101 only supports CS8, 1 stop bit and non-stick parity. */
	if (priv->partnum == CP210X_PARTNUM_CP2101) {
		tty->termios.c_cflag &= ~(CSIZE | CSTOPB | CMSPAR);
		tty->termios.c_cflag |= CS8;
	}

	bits = 0;

	switch (C_CSIZE(tty)) {
	case CS5:
		bits |= BITS_DATA_5;
		break;
	case CS6:
		bits |= BITS_DATA_6;
		break;
	case CS7:
		bits |= BITS_DATA_7;
		break;
	case CS8:
	default:
		bits |= BITS_DATA_8;
		break;
	}

	if (C_PARENB(tty)) {
		if (C_CMSPAR(tty)) {
			if (C_PARODD(tty))
				bits |= BITS_PARITY_MARK;
			else
				bits |= BITS_PARITY_SPACE;
		} else {
			if (C_PARODD(tty))
				bits |= BITS_PARITY_ODD;
			else
				bits |= BITS_PARITY_EVEN;
		}
	}

	if (C_CSTOPB(tty))
		bits |= BITS_STOP_2;
	else
		bits |= BITS_STOP_1;

	ret = cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
	if (ret)
		dev_err(&port->dev, "failed to set line control: %d\n", ret);

	cp210x_set_flow_control(tty, port, old_termios);

	/*
	 * Enable event-insertion mode only if input parity checking is
	 * enabled for now.
	 */
	if (I_INPCK(tty))
		cp210x_enable_event_mode(port);
	else
		cp210x_disable_event_mode(port);
}

static int cp210x_tiocmset(struct tty_struct *tty,
		unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	return cp210x_tiocmset_port(port, set, clear);
}

static int cp210x_tiocmset_port(struct usb_serial_port *port,
		unsigned int set, unsigned int clear)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	struct cp210x_flow_ctl flow_ctl;
	u32 ctl_hs, flow_repl;
	u16 control = 0;
	int ret;

	mutex_lock(&port_priv->mutex);

	if (set & TIOCM_RTS) {
		port_priv->rts = true;
		control |= CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (set & TIOCM_DTR) {
		port_priv->dtr = true;
		control |= CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}
	if (clear & TIOCM_RTS) {
		port_priv->rts = false;
		control &= ~CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (clear & TIOCM_DTR) {
		port_priv->dtr = false;
		control &= ~CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}

	/*
	 * Use SET_FLOW to set DTR and enable/disable auto-RTS when hardware
	 * flow control is enabled.
	 */
	if (port_priv->crtscts && control & CONTROL_WRITE_RTS) {
		ret = cp210x_read_reg_block(port, CP210X_GET_FLOW, &flow_ctl,
				sizeof(flow_ctl));
		if (ret)
			goto out_unlock;

		ctl_hs = le32_to_cpu(flow_ctl.ulControlHandshake);
		flow_repl = le32_to_cpu(flow_ctl.ulFlowReplace);

		ctl_hs &= ~CP210X_SERIAL_DTR_MASK;
		if (port_priv->dtr)
			ctl_hs |= CP210X_SERIAL_DTR_ACTIVE;
		else
			ctl_hs |= CP210X_SERIAL_DTR_INACTIVE;

		flow_repl &= ~CP210X_SERIAL_RTS_MASK;
		if (port_priv->rts)
			flow_repl |= CP210X_SERIAL_RTS_FLOW_CTL;
		else
			flow_repl |= CP210X_SERIAL_RTS_INACTIVE;

		flow_ctl.ulControlHandshake = cpu_to_le32(ctl_hs);
		flow_ctl.ulFlowReplace = cpu_to_le32(flow_repl);

		dev_dbg(&port->dev, "%s - ctrl = 0x%02x, flow = 0x%02x\n",
				__func__, ctl_hs, flow_repl);

		ret = cp210x_write_reg_block(port, CP210X_SET_FLOW, &flow_ctl,
				sizeof(flow_ctl));
	} else {
		dev_dbg(&port->dev, "%s - control = 0x%04x\n", __func__, control);

		ret = cp210x_write_u16_reg(port, CP210X_SET_MHS, control);
	}
out_unlock:
	mutex_unlock(&port_priv->mutex);

	return ret;
}

static void cp210x_dtr_rts(struct usb_serial_port *port, int on)
{
	if (on)
		cp210x_tiocmset_port(port, TIOCM_DTR | TIOCM_RTS, 0);
	else
		cp210x_tiocmset_port(port, 0, TIOCM_DTR | TIOCM_RTS);
}

static int cp210x_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	u8 control;
	int result;

	result = cp210x_read_u8_reg(port, CP210X_GET_MDMSTS, &control);
	if (result)
		return result;

	result = ((control & CONTROL_DTR) ? TIOCM_DTR : 0)
		|((control & CONTROL_RTS) ? TIOCM_RTS : 0)
		|((control & CONTROL_CTS) ? TIOCM_CTS : 0)
		|((control & CONTROL_DSR) ? TIOCM_DSR : 0)
		|((control & CONTROL_RING)? TIOCM_RI  : 0)
		|((control & CONTROL_DCD) ? TIOCM_CD  : 0);

	dev_dbg(&port->dev, "%s - control = 0x%02x\n", __func__, control);

	return result;
}

static void cp210x_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	u16 state;

	if (break_state == 0)
		state = BREAK_OFF;
	else
		state = BREAK_ON;
	dev_dbg(&port->dev, "%s - turning break %s\n", __func__,
		state == BREAK_OFF ? "off" : "on");
	cp210x_write_u16_reg(port, CP210X_SET_BREAK, state);
}

#ifdef CONFIG_GPIOLIB
static int cp210x_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	u8 req_type;
	u16 mask;
	int result;
	int len;

	result = usb_autopm_get_interface(serial->interface);
	if (result)
		return result;

	switch (priv->partnum) {
	case CP210X_PARTNUM_CP2105:
		req_type = REQTYPE_INTERFACE_TO_HOST;
		len = 1;
		break;
	case CP210X_PARTNUM_CP2108:
		req_type = REQTYPE_INTERFACE_TO_HOST;
		len = 2;
		break;
	default:
		req_type = REQTYPE_DEVICE_TO_HOST;
		len = 1;
		break;
	}

	mask = 0;
	result = cp210x_read_vendor_block(serial, req_type, CP210X_READ_LATCH,
					  &mask, len);

	usb_autopm_put_interface(serial->interface);

	if (result < 0)
		return result;

	le16_to_cpus(&mask);

	return !!(mask & BIT(gpio));
}

static void cp210x_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct cp210x_gpio_write16 buf16;
	struct cp210x_gpio_write buf;
	u16 mask, state;
	u16 wIndex;
	int result;

	if (value == 1)
		state = BIT(gpio);
	else
		state = 0;

	mask = BIT(gpio);

	result = usb_autopm_get_interface(serial->interface);
	if (result)
		goto out;

	switch (priv->partnum) {
	case CP210X_PARTNUM_CP2105:
		buf.mask = (u8)mask;
		buf.state = (u8)state;
		result = cp210x_write_vendor_block(serial,
						   REQTYPE_HOST_TO_INTERFACE,
						   CP210X_WRITE_LATCH, &buf,
						   sizeof(buf));
		break;
	case CP210X_PARTNUM_CP2108:
		buf16.mask = cpu_to_le16(mask);
		buf16.state = cpu_to_le16(state);
		result = cp210x_write_vendor_block(serial,
						   REQTYPE_HOST_TO_INTERFACE,
						   CP210X_WRITE_LATCH, &buf16,
						   sizeof(buf16));
		break;
	default:
		wIndex = state << 8 | mask;
		result = usb_control_msg(serial->dev,
					 usb_sndctrlpipe(serial->dev, 0),
					 CP210X_VENDOR_SPECIFIC,
					 REQTYPE_HOST_TO_DEVICE,
					 CP210X_WRITE_LATCH,
					 wIndex,
					 NULL, 0, USB_CTRL_SET_TIMEOUT);
		break;
	}

	usb_autopm_put_interface(serial->interface);
out:
	if (result < 0) {
		dev_err(&serial->interface->dev, "failed to set GPIO value: %d\n",
				result);
	}
}

static int cp210x_gpio_direction_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	return priv->gpio_input & BIT(gpio);
}

static int cp210x_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	if (priv->partnum == CP210X_PARTNUM_CP2105) {
		/* hardware does not support an input mode */
		return -ENOTSUPP;
	}

	/* push-pull pins cannot be changed to be inputs */
	if (priv->gpio_pushpull & BIT(gpio))
		return -EINVAL;

	/* make sure to release pin if it is being driven low */
	cp210x_gpio_set(gc, gpio, 1);

	priv->gpio_input |= BIT(gpio);

	return 0;
}

static int cp210x_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	priv->gpio_input &= ~BIT(gpio);
	cp210x_gpio_set(gc, gpio, value);

	return 0;
}

static int cp210x_gpio_set_config(struct gpio_chip *gc, unsigned int gpio,
				  unsigned long config)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	enum pin_config_param param = pinconf_to_config_param(config);

	/* Succeed only if in correct mode (this can't be set at runtime) */
	if ((param == PIN_CONFIG_DRIVE_PUSH_PULL) &&
	    (priv->gpio_pushpull & BIT(gpio)))
		return 0;

	if ((param == PIN_CONFIG_DRIVE_OPEN_DRAIN) &&
	    !(priv->gpio_pushpull & BIT(gpio)))
		return 0;

	return -ENOTSUPP;
}

static int cp210x_gpio_init_valid_mask(struct gpio_chip *gc,
		unsigned long *valid_mask, unsigned int ngpios)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct device *dev = &serial->interface->dev;
	unsigned long altfunc_mask = priv->gpio_altfunc;

	bitmap_complement(valid_mask, &altfunc_mask, ngpios);

	if (bitmap_empty(valid_mask, ngpios))
		dev_dbg(dev, "no pin configured for GPIO\n");
	else
		dev_dbg(dev, "GPIO.%*pbl configured for GPIO\n", ngpios,
				valid_mask);
	return 0;
}

/*
 * This function is for configuring GPIO using shared pins, where other signals
 * are made unavailable by configuring the use of GPIO. This is believed to be
 * only applicable to the cp2105 at this point, the other devices supported by
 * this driver that provide GPIO do so in a way that does not impact other
 * signals and are thus expected to have very different initialisation.
 */
static int cp2105_gpioconf_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct cp210x_pin_mode mode;
	struct cp210x_dual_port_config config;
	u8 intf_num = cp210x_interface_num(serial);
	u8 iface_config;
	int result;

	result = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
					  CP210X_GET_DEVICEMODE, &mode,
					  sizeof(mode));
	if (result < 0)
		return result;

	result = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
					  CP210X_GET_PORTCONFIG, &config,
					  sizeof(config));
	if (result < 0)
		return result;

	/*  2 banks of GPIO - One for the pins taken from each serial port */
	if (intf_num == 0) {
		priv->gc.ngpio = 2;

		if (mode.eci == CP210X_PIN_MODE_MODEM) {
			/* mark all GPIOs of this interface as reserved */
			priv->gpio_altfunc = 0xff;
			return 0;
		}

		iface_config = config.eci_cfg;
		priv->gpio_pushpull = (u8)((le16_to_cpu(config.gpio_mode) &
						CP210X_ECI_GPIO_MODE_MASK) >>
						CP210X_ECI_GPIO_MODE_OFFSET);
	} else if (intf_num == 1) {
		priv->gc.ngpio = 3;

		if (mode.sci == CP210X_PIN_MODE_MODEM) {
			/* mark all GPIOs of this interface as reserved */
			priv->gpio_altfunc = 0xff;
			return 0;
		}

		iface_config = config.sci_cfg;
		priv->gpio_pushpull = (u8)((le16_to_cpu(config.gpio_mode) &
						CP210X_SCI_GPIO_MODE_MASK) >>
						CP210X_SCI_GPIO_MODE_OFFSET);
	} else {
		return -ENODEV;
	}

	/* mark all pins which are not in GPIO mode */
	if (iface_config & CP2105_GPIO0_TXLED_MODE)	/* GPIO 0 */
		priv->gpio_altfunc |= BIT(0);
	if (iface_config & (CP2105_GPIO1_RXLED_MODE |	/* GPIO 1 */
			CP2105_GPIO1_RS485_MODE))
		priv->gpio_altfunc |= BIT(1);

	/* driver implementation for CP2105 only supports outputs */
	priv->gpio_input = 0;

	return 0;
}

static int cp2104_gpioconf_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct cp210x_single_port_config config;
	u8 iface_config;
	u8 gpio_latch;
	int result;
	u8 i;

	result = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
					  CP210X_GET_PORTCONFIG, &config,
					  sizeof(config));
	if (result < 0)
		return result;

	priv->gc.ngpio = 4;

	iface_config = config.device_cfg;
	priv->gpio_pushpull = (u8)((le16_to_cpu(config.gpio_mode) &
					CP210X_GPIO_MODE_MASK) >>
					CP210X_GPIO_MODE_OFFSET);
	gpio_latch = (u8)((le16_to_cpu(config.reset_state) &
					CP210X_GPIO_MODE_MASK) >>
					CP210X_GPIO_MODE_OFFSET);

	/* mark all pins which are not in GPIO mode */
	if (iface_config & CP2104_GPIO0_TXLED_MODE)	/* GPIO 0 */
		priv->gpio_altfunc |= BIT(0);
	if (iface_config & CP2104_GPIO1_RXLED_MODE)	/* GPIO 1 */
		priv->gpio_altfunc |= BIT(1);
	if (iface_config & CP2104_GPIO2_RS485_MODE)	/* GPIO 2 */
		priv->gpio_altfunc |= BIT(2);

	/*
	 * Like CP2102N, CP2104 has also no strict input and output pin
	 * modes.
	 * Do the same input mode emulation as CP2102N.
	 */
	for (i = 0; i < priv->gc.ngpio; ++i) {
		/*
		 * Set direction to "input" iff pin is open-drain and reset
		 * value is 1.
		 */
		if (!(priv->gpio_pushpull & BIT(i)) && (gpio_latch & BIT(i)))
			priv->gpio_input |= BIT(i);
	}

	return 0;
}

static int cp2108_gpio_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct cp210x_quad_port_config config;
	u16 gpio_latch;
	int result;
	u8 i;

	result = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
					  CP210X_GET_PORTCONFIG, &config,
					  sizeof(config));
	if (result < 0)
		return result;

	priv->gc.ngpio = 16;
	priv->gpio_pushpull = le16_to_cpu(config.reset_state.gpio_mode_pb1);
	gpio_latch = le16_to_cpu(config.reset_state.gpio_latch_pb1);

	/*
	 * Mark all pins which are not in GPIO mode.
	 *
	 * Refer to table 9.1 "GPIO Mode alternate Functions" in the datasheet:
	 * https://www.silabs.com/documents/public/data-sheets/cp2108-datasheet.pdf
	 *
	 * Alternate functions of GPIO0 to GPIO3 are determine by enhancedfxn_ifc[0]
	 * and the similarly for the other pins; enhancedfxn_ifc[1]: GPIO4 to GPIO7,
	 * enhancedfxn_ifc[2]: GPIO8 to GPIO11, enhancedfxn_ifc[3]: GPIO12 to GPIO15.
	 */
	for (i = 0; i < 4; i++) {
		if (config.enhancedfxn_ifc[i] & CP2108_EF_IFC_GPIO_TXLED)
			priv->gpio_altfunc |= BIT(i * 4);
		if (config.enhancedfxn_ifc[i] & CP2108_EF_IFC_GPIO_RXLED)
			priv->gpio_altfunc |= BIT((i * 4) + 1);
		if (config.enhancedfxn_ifc[i] & CP2108_EF_IFC_GPIO_RS485)
			priv->gpio_altfunc |= BIT((i * 4) + 2);
		if (config.enhancedfxn_ifc[i] & CP2108_EF_IFC_GPIO_CLOCK)
			priv->gpio_altfunc |= BIT((i * 4) + 3);
	}

	/*
	 * Like CP2102N, CP2108 has also no strict input and output pin
	 * modes. Do the same input mode emulation as CP2102N.
	 */
	for (i = 0; i < priv->gc.ngpio; ++i) {
		/*
		 * Set direction to "input" iff pin is open-drain and reset
		 * value is 1.
		 */
		if (!(priv->gpio_pushpull & BIT(i)) && (gpio_latch & BIT(i)))
			priv->gpio_input |= BIT(i);
	}

	return 0;
}

static int cp2102n_gpioconf_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	const u16 config_size = 0x02a6;
	u8 gpio_rst_latch;
	u8 config_version;
	u8 gpio_pushpull;
	u8 *config_buf;
	u8 gpio_latch;
	u8 gpio_ctrl;
	int result;
	u8 i;

	/*
	 * Retrieve device configuration from the device.
	 * The array received contains all customization settings done at the
	 * factory/manufacturer. Format of the array is documented at the
	 * time of writing at:
	 * https://www.silabs.com/community/interface/knowledge-base.entry.html/2017/03/31/cp2102n_setconfig-xsfa
	 */
	config_buf = kmalloc(config_size, GFP_KERNEL);
	if (!config_buf)
		return -ENOMEM;

	result = cp210x_read_vendor_block(serial,
					  REQTYPE_DEVICE_TO_HOST,
					  CP210X_READ_2NCONFIG,
					  config_buf,
					  config_size);
	if (result < 0) {
		kfree(config_buf);
		return result;
	}

	config_version = config_buf[CP210X_2NCONFIG_CONFIG_VERSION_IDX];
	gpio_pushpull = config_buf[CP210X_2NCONFIG_GPIO_MODE_IDX];
	gpio_ctrl = config_buf[CP210X_2NCONFIG_GPIO_CONTROL_IDX];
	gpio_rst_latch = config_buf[CP210X_2NCONFIG_GPIO_RSTLATCH_IDX];

	kfree(config_buf);

	/* Make sure this is a config format we understand. */
	if (config_version != 0x01)
		return -ENOTSUPP;

	priv->gc.ngpio = 4;

	/*
	 * Get default pin states after reset. Needed so we can determine
	 * the direction of an open-drain pin.
	 */
	gpio_latch = (gpio_rst_latch >> 3) & 0x0f;

	/* 0 indicates open-drain mode, 1 is push-pull */
	priv->gpio_pushpull = (gpio_pushpull >> 3) & 0x0f;

	/* 0 indicates GPIO mode, 1 is alternate function */
	if (priv->partnum == CP210X_PARTNUM_CP2102N_QFN20) {
		/* QFN20 is special... */
		if (gpio_ctrl & CP2102N_QFN20_GPIO0_CLK_MODE)   /* GPIO 0 */
			priv->gpio_altfunc |= BIT(0);
		if (gpio_ctrl & CP2102N_QFN20_GPIO1_RS485_MODE) /* GPIO 1 */
			priv->gpio_altfunc |= BIT(1);
		if (gpio_ctrl & CP2102N_QFN20_GPIO2_TXLED_MODE) /* GPIO 2 */
			priv->gpio_altfunc |= BIT(2);
		if (gpio_ctrl & CP2102N_QFN20_GPIO3_RXLED_MODE) /* GPIO 3 */
			priv->gpio_altfunc |= BIT(3);
	} else {
		priv->gpio_altfunc = (gpio_ctrl >> 2) & 0x0f;
	}

	if (priv->partnum == CP210X_PARTNUM_CP2102N_QFN28) {
		/*
		 * For the QFN28 package, GPIO4-6 are controlled by
		 * the low three bits of the mode/latch fields.
		 * Contrary to the document linked above, the bits for
		 * the SUSPEND pins are elsewhere.  No alternate
		 * function is available for these pins.
		 */
		priv->gc.ngpio = 7;
		gpio_latch |= (gpio_rst_latch & 7) << 4;
		priv->gpio_pushpull |= (gpio_pushpull & 7) << 4;
	}

	/*
	 * The CP2102N does not strictly has input and output pin modes,
	 * it only knows open-drain and push-pull modes which is set at
	 * factory. An open-drain pin can function both as an
	 * input or an output. We emulate input mode for open-drain pins
	 * by making sure they are not driven low, and we do not allow
	 * push-pull pins to be set as an input.
	 */
	for (i = 0; i < priv->gc.ngpio; ++i) {
		/*
		 * Set direction to "input" iff pin is open-drain and reset
		 * value is 1.
		 */
		if (!(priv->gpio_pushpull & BIT(i)) && (gpio_latch & BIT(i)))
			priv->gpio_input |= BIT(i);
	}

	return 0;
}

static int cp210x_gpio_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	int result;

	switch (priv->partnum) {
	case CP210X_PARTNUM_CP2104:
		result = cp2104_gpioconf_init(serial);
		break;
	case CP210X_PARTNUM_CP2105:
		result = cp2105_gpioconf_init(serial);
		break;
	case CP210X_PARTNUM_CP2108:
		/*
		 * The GPIOs are not tied to any specific port so only register
		 * once for interface 0.
		 */
		if (cp210x_interface_num(serial) != 0)
			return 0;
		result = cp2108_gpio_init(serial);
		break;
	case CP210X_PARTNUM_CP2102N_QFN28:
	case CP210X_PARTNUM_CP2102N_QFN24:
	case CP210X_PARTNUM_CP2102N_QFN20:
		result = cp2102n_gpioconf_init(serial);
		break;
	default:
		return 0;
	}

	if (result < 0)
		return result;

	priv->gc.label = "cp210x";
	priv->gc.get_direction = cp210x_gpio_direction_get;
	priv->gc.direction_input = cp210x_gpio_direction_input;
	priv->gc.direction_output = cp210x_gpio_direction_output;
	priv->gc.get = cp210x_gpio_get;
	priv->gc.set = cp210x_gpio_set;
	priv->gc.set_config = cp210x_gpio_set_config;
	priv->gc.init_valid_mask = cp210x_gpio_init_valid_mask;
	priv->gc.owner = THIS_MODULE;
	priv->gc.parent = &serial->interface->dev;
	priv->gc.base = -1;
	priv->gc.can_sleep = true;

	result = gpiochip_add_data(&priv->gc, serial);
	if (!result)
		priv->gpio_registered = true;

	return result;
}

static void cp210x_gpio_remove(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	if (priv->gpio_registered) {
		gpiochip_remove(&priv->gc);
		priv->gpio_registered = false;
	}
}

#else

static int cp210x_gpio_init(struct usb_serial *serial)
{
	return 0;
}

static void cp210x_gpio_remove(struct usb_serial *serial)
{
	/* Nothing to do */
}

#endif

static int cp210x_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct cp210x_port_private *port_priv;

	port_priv = kzalloc(sizeof(*port_priv), GFP_KERNEL);
	if (!port_priv)
		return -ENOMEM;

	port_priv->bInterfaceNumber = cp210x_interface_num(serial);
	mutex_init(&port_priv->mutex);

	usb_set_serial_port_data(port, port_priv);

	return 0;
}

static void cp210x_port_remove(struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv;

	port_priv = usb_get_serial_port_data(port);
	kfree(port_priv);
}

static void cp210x_init_max_speed(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	bool use_actual_rate = false;
	speed_t min = 300;
	speed_t max;

	switch (priv->partnum) {
	case CP210X_PARTNUM_CP2101:
		max = 921600;
		break;
	case CP210X_PARTNUM_CP2102:
	case CP210X_PARTNUM_CP2103:
		max = 1000000;
		break;
	case CP210X_PARTNUM_CP2104:
		use_actual_rate = true;
		max = 2000000;
		break;
	case CP210X_PARTNUM_CP2108:
		max = 2000000;
		break;
	case CP210X_PARTNUM_CP2105:
		if (cp210x_interface_num(serial) == 0) {
			use_actual_rate = true;
			max = 2000000;	/* ECI */
		} else {
			min = 2400;
			max = 921600;	/* SCI */
		}
		break;
	case CP210X_PARTNUM_CP2102N_QFN28:
	case CP210X_PARTNUM_CP2102N_QFN24:
	case CP210X_PARTNUM_CP2102N_QFN20:
		use_actual_rate = true;
		max = 3000000;
		break;
	default:
		max = 2000000;
		break;
	}

	priv->min_speed = min;
	priv->max_speed = max;
	priv->use_actual_rate = use_actual_rate;
}

static void cp2102_determine_quirks(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	u8 *buf;
	int ret;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		return;
	/*
	 * Some (possibly counterfeit) CP2102 do not support event-insertion
	 * mode and respond differently to malformed vendor requests.
	 * Specifically, they return one instead of two bytes when sent a
	 * two-byte part-number request.
	 */
	ret = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			CP210X_VENDOR_SPECIFIC, REQTYPE_DEVICE_TO_HOST,
			CP210X_GET_PARTNUM, 0, buf, 2, USB_CTRL_GET_TIMEOUT);
	if (ret == 1) {
		dev_dbg(&serial->interface->dev,
				"device does not support event-insertion mode\n");
		priv->no_event_mode = true;
	}

	kfree(buf);
}

static int cp210x_get_fw_version(struct usb_serial *serial, u16 value)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	u8 ver[3];
	int ret;

	ret = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST, value,
			ver, sizeof(ver));
	if (ret)
		return ret;

	dev_dbg(&serial->interface->dev, "%s - %d.%d.%d\n", __func__,
			ver[0], ver[1], ver[2]);

	priv->fw_version = ver[0] << 16 | ver[1] << 8 | ver[2];

	return 0;
}

static void cp210x_determine_type(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	int ret;

	ret = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
			CP210X_GET_PARTNUM, &priv->partnum,
			sizeof(priv->partnum));
	if (ret < 0) {
		dev_warn(&serial->interface->dev,
				"querying part number failed\n");
		priv->partnum = CP210X_PARTNUM_UNKNOWN;
		return;
	}

	dev_dbg(&serial->interface->dev, "partnum = 0x%02x\n", priv->partnum);

	switch (priv->partnum) {
	case CP210X_PARTNUM_CP2102:
		cp2102_determine_quirks(serial);
		break;
	case CP210X_PARTNUM_CP2105:
	case CP210X_PARTNUM_CP2108:
		cp210x_get_fw_version(serial, CP210X_GET_FW_VER);
		break;
	case CP210X_PARTNUM_CP2102N_QFN28:
	case CP210X_PARTNUM_CP2102N_QFN24:
	case CP210X_PARTNUM_CP2102N_QFN20:
		ret = cp210x_get_fw_version(serial, CP210X_GET_FW_VER_2N);
		if (ret)
			break;
		if (priv->fw_version <= 0x10004)
			priv->no_flow_control = true;
		break;
	default:
		break;
	}
}

static int cp210x_attach(struct usb_serial *serial)
{
	int result;
	struct cp210x_serial_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	usb_set_serial_data(serial, priv);

	cp210x_determine_type(serial);
	cp210x_init_max_speed(serial);

	result = cp210x_gpio_init(serial);
	if (result < 0) {
		dev_err(&serial->interface->dev, "GPIO initialisation failed: %d\n",
				result);
	}

	return 0;
}

static void cp210x_disconnect(struct usb_serial *serial)
{
	cp210x_gpio_remove(serial);
}

static void cp210x_release(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	cp210x_gpio_remove(serial);

	kfree(priv);
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
