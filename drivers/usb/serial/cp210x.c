/*
 * Silicon Laboratories CP210x USB to RS232 serial adaptor driver
 *
 * Copyright (C) 2005 Craig Shelley (craig@microtron.org.uk)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
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
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
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
static void cp210x_get_termios(struct tty_struct *, struct usb_serial_port *);
static void cp210x_get_termios_port(struct usb_serial_port *port,
	tcflag_t *cflagp, unsigned int *baudp);
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
static int cp210x_port_remove(struct usb_serial_port *);
static void cp210x_dtr_rts(struct usb_serial_port *p, int on);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x045B, 0x0053) }, /* Renesas RX610 RX-Stick */
	{ USB_DEVICE(0x0471, 0x066A) }, /* AKTAKOM ACE-1001 cable */
	{ USB_DEVICE(0x0489, 0xE000) }, /* Pirelli Broadband S.p.A, DP-L10 SIP/GSM Mobile */
	{ USB_DEVICE(0x0489, 0xE003) }, /* Pirelli Broadband S.p.A, DP-L10 SIP/GSM Mobile */
	{ USB_DEVICE(0x0745, 0x1000) }, /* CipherLab USB CCD Barcode Scanner 1000 */
	{ USB_DEVICE(0x0846, 0x1100) }, /* NetGear Managed Switch M4100 series, M5300 series, M7100 series */
	{ USB_DEVICE(0x08e6, 0x5501) }, /* Gemalto Prox-PU/CU contactless smartcard reader */
	{ USB_DEVICE(0x08FD, 0x000A) }, /* Digianswer A/S , ZigBee/802.15.4 MAC Device */
	{ USB_DEVICE(0x0908, 0x01FF) }, /* Siemens RUGGEDCOM USB Serial Console */
	{ USB_DEVICE(0x0BED, 0x1100) }, /* MEI (TM) Cashflow-SC Bill/Voucher Acceptor */
	{ USB_DEVICE(0x0BED, 0x1101) }, /* MEI series 2000 Combo Acceptor */
	{ USB_DEVICE(0x0FCF, 0x1003) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x0FCF, 0x1004) }, /* Dynastream ANT2USB */
	{ USB_DEVICE(0x0FCF, 0x1006) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x0FDE, 0xCA05) }, /* OWL Wireless Electricity Monitor CM-160 */
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
	{ USB_DEVICE(0x10C4, 0x88FB) }, /* CESINEL MEDCAL STII Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x8938) }, /* CESINEL MEDCAL S II Network Analyzer */
	{ USB_DEVICE(0x10C4, 0x8946) }, /* Ketra N1 Wireless Interface */
	{ USB_DEVICE(0x10C4, 0x8962) }, /* Brim Brothers charging dock */
	{ USB_DEVICE(0x10C4, 0x8977) },	/* CEL MeshWorks DevKit Device */
	{ USB_DEVICE(0x10C4, 0x8998) }, /* KCF Technologies PRN */
	{ USB_DEVICE(0x10C4, 0x89A4) }, /* CESINEL FTBC Flexible Thyristor Bridge Controller */
	{ USB_DEVICE(0x10C4, 0x8A2A) }, /* HubZ dual ZigBee and Z-Wave dongle */
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
	u8			config;
	u8			gpio_mode;
	bool			gpio_registered;
#endif
	u8			partnum;
};

struct cp210x_port_private {
	__u8			bInterfaceNumber;
	bool			has_swapped_line_ctl;
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
	.tiocmget		= cp210x_tiocmget,
	.tiocmset		= cp210x_tiocmset,
	.attach			= cp210x_attach,
	.disconnect		= cp210x_disconnect,
	.release		= cp210x_release,
	.port_probe		= cp210x_port_probe,
	.port_remove		= cp210x_port_remove,
	.dtr_rts		= cp210x_dtr_rts
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

/* CP210X_VENDOR_SPECIFIC values */
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

/* CP210X_GET_FLOW/CP210X_SET_FLOW read/write these 0x10 bytes */
struct cp210x_flow_ctl {
	__le32	ulControlHandshake;
	__le32	ulFlowReplace;
	__le32	ulXonLimit;
	__le32	ulXoffLimit;
} __packed;

/* cp210x_flow_ctl::ulControlHandshake */
#define CP210X_SERIAL_DTR_MASK		GENMASK(1, 0)
#define CP210X_SERIAL_DTR_SHIFT(_mode)	(_mode)
#define CP210X_SERIAL_CTS_HANDSHAKE	BIT(3)
#define CP210X_SERIAL_DSR_HANDSHAKE	BIT(4)
#define CP210X_SERIAL_DCD_HANDSHAKE	BIT(5)
#define CP210X_SERIAL_DSR_SENSITIVITY	BIT(6)

/* values for cp210x_flow_ctl::ulControlHandshake::CP210X_SERIAL_DTR_MASK */
#define CP210X_SERIAL_DTR_INACTIVE	0
#define CP210X_SERIAL_DTR_ACTIVE	1
#define CP210X_SERIAL_DTR_FLOW_CTL	2

/* cp210x_flow_ctl::ulFlowReplace */
#define CP210X_SERIAL_AUTO_TRANSMIT	BIT(0)
#define CP210X_SERIAL_AUTO_RECEIVE	BIT(1)
#define CP210X_SERIAL_ERROR_CHAR	BIT(2)
#define CP210X_SERIAL_NULL_STRIPPING	BIT(3)
#define CP210X_SERIAL_BREAK_CHAR	BIT(4)
#define CP210X_SERIAL_RTS_MASK		GENMASK(7, 6)
#define CP210X_SERIAL_RTS_SHIFT(_mode)	(_mode << 6)
#define CP210X_SERIAL_XOFF_CONTINUE	BIT(31)

/* values for cp210x_flow_ctl::ulFlowReplace::CP210X_SERIAL_RTS_MASK */
#define CP210X_SERIAL_RTS_INACTIVE	0
#define CP210X_SERIAL_RTS_ACTIVE	1
#define CP210X_SERIAL_RTS_FLOW_CTL	2

/* CP210X_VENDOR_SPECIFIC, CP210X_GET_DEVICEMODE call reads these 0x2 bytes. */
struct cp210x_pin_mode {
	u8	eci;
	u8	sci;
} __packed;

#define CP210X_PIN_MODE_MODEM		0
#define CP210X_PIN_MODE_GPIO		BIT(0)

/*
 * CP210X_VENDOR_SPECIFIC, CP210X_GET_PORTCONFIG call reads these 0xf bytes.
 * Structure needs padding due to unused/unspecified bytes.
 */
struct cp210x_config {
	__le16	gpio_mode;
	u8	__pad0[2];
	__le16	reset_state;
	u8	__pad1[4];
	__le16	suspend_state;
	u8	sci_cfg;
	u8	eci_cfg;
	u8	device_cfg;
} __packed;

/* GPIO modes */
#define CP210X_SCI_GPIO_MODE_OFFSET	9
#define CP210X_SCI_GPIO_MODE_MASK	GENMASK(11, 9)

#define CP210X_ECI_GPIO_MODE_OFFSET	2
#define CP210X_ECI_GPIO_MODE_MASK	GENMASK(3, 2)

/* CP2105 port configuration values */
#define CP2105_GPIO0_TXLED_MODE		BIT(0)
#define CP2105_GPIO1_RXLED_MODE		BIT(1)
#define CP2105_GPIO1_RS485_MODE		BIT(2)

/* CP210X_VENDOR_SPECIFIC, CP210X_WRITE_LATCH call writes these 0x2 bytes. */
struct cp210x_gpio_write {
	u8	mask;
	u8	state;
} __packed;

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
	if (!dmabuf) {
		/*
		 * FIXME Some callers don't bother to check for error,
		 * at least give them consistent junk until they are fixed
		 */
		memset(buf, 0, bufsize);
		return -ENOMEM;
	}

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			req, REQTYPE_INTERFACE_TO_HOST, 0,
			port_priv->bInterfaceNumber, dmabuf, bufsize,
			USB_CTRL_SET_TIMEOUT);
	if (result == bufsize) {
		memcpy(buf, dmabuf, bufsize);
		result = 0;
	} else {
		dev_err(&port->dev, "failed get req 0x%x size %d status: %d\n",
				req, bufsize, result);
		if (result >= 0)
			result = -EIO;

		/*
		 * FIXME Some callers don't bother to check for error,
		 * at least give them consistent junk until they are fixed
		 */
		memset(buf, 0, bufsize);
	}

	kfree(dmabuf);

	return result;
}

/*
 * Reads any 32-bit CP210X_ register identified by req.
 */
static int cp210x_read_u32_reg(struct usb_serial_port *port, u8 req, u32 *val)
{
	__le32 le32_val;
	int err;

	err = cp210x_read_reg_block(port, req, &le32_val, sizeof(le32_val));
	if (err) {
		/*
		 * FIXME Some callers don't bother to check for error,
		 * at least give them consistent junk until they are fixed
		 */
		*val = 0;
		return err;
	}

	*val = le32_to_cpu(le32_val);

	return 0;
}

/*
 * Reads any 16-bit CP210X_ register identified by req.
 */
static int cp210x_read_u16_reg(struct usb_serial_port *port, u8 req, u16 *val)
{
	__le16 le16_val;
	int err;

	err = cp210x_read_reg_block(port, req, &le16_val, sizeof(le16_val));
	if (err)
		return err;

	*val = le16_to_cpu(le16_val);

	return 0;
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

	if (result == bufsize) {
		result = 0;
	} else {
		dev_err(&port->dev, "failed set req 0x%x size %d status: %d\n",
				req, bufsize, result);
		if (result >= 0)
			result = -EIO;
	}

	return result;
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

	if (result == bufsize) {
		result = 0;
	} else {
		dev_err(&serial->interface->dev,
			"failed to set vendor val 0x%04x size %d: %d\n", val,
			bufsize, result);
		if (result >= 0)
			result = -EIO;
	}

	return result;
}
#endif

/*
 * Detect CP2108 GET_LINE_CTL bug and activate workaround.
 * Write a known good value 0x800, read it back.
 * If it comes back swapped the bug is detected.
 * Preserve the original register value.
 */
static int cp210x_detect_swapped_line_ctl(struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	u16 line_ctl_save;
	u16 line_ctl_test;
	int err;

	err = cp210x_read_u16_reg(port, CP210X_GET_LINE_CTL, &line_ctl_save);
	if (err)
		return err;

	err = cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, 0x800);
	if (err)
		return err;

	err = cp210x_read_u16_reg(port, CP210X_GET_LINE_CTL, &line_ctl_test);
	if (err)
		return err;

	if (line_ctl_test == 8) {
		port_priv->has_swapped_line_ctl = true;
		line_ctl_save = swab16(line_ctl_save);
	}

	return cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, line_ctl_save);
}

/*
 * Must always be called instead of cp210x_read_u16_reg(CP210X_GET_LINE_CTL)
 * to workaround cp2108 bug and get correct value.
 */
static int cp210x_get_line_ctl(struct usb_serial_port *port, u16 *ctl)
{
	struct cp210x_port_private *port_priv = usb_get_serial_port_data(port);
	int err;

	err = cp210x_read_u16_reg(port, CP210X_GET_LINE_CTL, ctl);
	if (err)
		return err;

	/* Workaround swapped bytes in 16-bit value from CP210X_GET_LINE_CTL */
	if (port_priv->has_swapped_line_ctl)
		*ctl = swab16(*ctl);

	return 0;
}

/*
 * cp210x_quantise_baudrate
 * Quantises the baud rate as per AN205 Table 1
 */
static unsigned int cp210x_quantise_baudrate(unsigned int baud)
{
	if (baud <= 300)
		baud = 300;
	else if (baud <= 600)      baud = 600;
	else if (baud <= 1200)     baud = 1200;
	else if (baud <= 1800)     baud = 1800;
	else if (baud <= 2400)     baud = 2400;
	else if (baud <= 4000)     baud = 4000;
	else if (baud <= 4803)     baud = 4800;
	else if (baud <= 7207)     baud = 7200;
	else if (baud <= 9612)     baud = 9600;
	else if (baud <= 14428)    baud = 14400;
	else if (baud <= 16062)    baud = 16000;
	else if (baud <= 19250)    baud = 19200;
	else if (baud <= 28912)    baud = 28800;
	else if (baud <= 38601)    baud = 38400;
	else if (baud <= 51558)    baud = 51200;
	else if (baud <= 56280)    baud = 56000;
	else if (baud <= 58053)    baud = 57600;
	else if (baud <= 64111)    baud = 64000;
	else if (baud <= 77608)    baud = 76800;
	else if (baud <= 117028)   baud = 115200;
	else if (baud <= 129347)   baud = 128000;
	else if (baud <= 156868)   baud = 153600;
	else if (baud <= 237832)   baud = 230400;
	else if (baud <= 254234)   baud = 250000;
	else if (baud <= 273066)   baud = 256000;
	else if (baud <= 491520)   baud = 460800;
	else if (baud <= 567138)   baud = 500000;
	else if (baud <= 670254)   baud = 576000;
	else if (baud < 1000000)
		baud = 921600;
	else if (baud > 2000000)
		baud = 2000000;
	return baud;
}

static int cp210x_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int result;

	result = cp210x_write_u16_reg(port, CP210X_IFC_ENABLE, UART_ENABLE);
	if (result) {
		dev_err(&port->dev, "%s - Unable to enable UART\n", __func__);
		return result;
	}

	/* Configure the termios structure */
	cp210x_get_termios(tty, port);

	/* The baud rate must be initialised on cp2104 */
	if (tty)
		cp210x_change_speed(tty, port, NULL);

	return usb_serial_generic_open(tty, port);
}

static void cp210x_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);

	/* Clear both queues; cp2108 needs this to avoid an occasional hang */
	cp210x_write_u16_reg(port, CP210X_PURGE, PURGE_ALL);

	cp210x_write_u16_reg(port, CP210X_IFC_ENABLE, UART_DISABLE);
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

/*
 * cp210x_get_termios
 * Reads the baud rate, data bits, parity, stop bits and flow control mode
 * from the device, corrects any unsupported values, and configures the
 * termios structure to reflect the state of the device
 */
static void cp210x_get_termios(struct tty_struct *tty,
	struct usb_serial_port *port)
{
	unsigned int baud;

	if (tty) {
		cp210x_get_termios_port(tty->driver_data,
			&tty->termios.c_cflag, &baud);
		tty_encode_baud_rate(tty, baud, baud);
	} else {
		tcflag_t cflag;
		cflag = 0;
		cp210x_get_termios_port(port, &cflag, &baud);
	}
}

/*
 * cp210x_get_termios_port
 * This is the heart of cp210x_get_termios which always uses a &usb_serial_port.
 */
static void cp210x_get_termios_port(struct usb_serial_port *port,
	tcflag_t *cflagp, unsigned int *baudp)
{
	struct device *dev = &port->dev;
	tcflag_t cflag;
	struct cp210x_flow_ctl flow_ctl;
	u32 baud;
	u16 bits;
	u32 ctl_hs;

	cp210x_read_u32_reg(port, CP210X_GET_BAUDRATE, &baud);

	dev_dbg(dev, "%s - baud rate = %d\n", __func__, baud);
	*baudp = baud;

	cflag = *cflagp;

	cp210x_get_line_ctl(port, &bits);
	cflag &= ~CSIZE;
	switch (bits & BITS_DATA_MASK) {
	case BITS_DATA_5:
		dev_dbg(dev, "%s - data bits = 5\n", __func__);
		cflag |= CS5;
		break;
	case BITS_DATA_6:
		dev_dbg(dev, "%s - data bits = 6\n", __func__);
		cflag |= CS6;
		break;
	case BITS_DATA_7:
		dev_dbg(dev, "%s - data bits = 7\n", __func__);
		cflag |= CS7;
		break;
	case BITS_DATA_8:
		dev_dbg(dev, "%s - data bits = 8\n", __func__);
		cflag |= CS8;
		break;
	case BITS_DATA_9:
		dev_dbg(dev, "%s - data bits = 9 (not supported, using 8 data bits)\n", __func__);
		cflag |= CS8;
		bits &= ~BITS_DATA_MASK;
		bits |= BITS_DATA_8;
		cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
		break;
	default:
		dev_dbg(dev, "%s - Unknown number of data bits, using 8\n", __func__);
		cflag |= CS8;
		bits &= ~BITS_DATA_MASK;
		bits |= BITS_DATA_8;
		cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
		break;
	}

	switch (bits & BITS_PARITY_MASK) {
	case BITS_PARITY_NONE:
		dev_dbg(dev, "%s - parity = NONE\n", __func__);
		cflag &= ~PARENB;
		break;
	case BITS_PARITY_ODD:
		dev_dbg(dev, "%s - parity = ODD\n", __func__);
		cflag |= (PARENB|PARODD);
		break;
	case BITS_PARITY_EVEN:
		dev_dbg(dev, "%s - parity = EVEN\n", __func__);
		cflag &= ~PARODD;
		cflag |= PARENB;
		break;
	case BITS_PARITY_MARK:
		dev_dbg(dev, "%s - parity = MARK\n", __func__);
		cflag |= (PARENB|PARODD|CMSPAR);
		break;
	case BITS_PARITY_SPACE:
		dev_dbg(dev, "%s - parity = SPACE\n", __func__);
		cflag &= ~PARODD;
		cflag |= (PARENB|CMSPAR);
		break;
	default:
		dev_dbg(dev, "%s - Unknown parity mode, disabling parity\n", __func__);
		cflag &= ~PARENB;
		bits &= ~BITS_PARITY_MASK;
		cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
		break;
	}

	cflag &= ~CSTOPB;
	switch (bits & BITS_STOP_MASK) {
	case BITS_STOP_1:
		dev_dbg(dev, "%s - stop bits = 1\n", __func__);
		break;
	case BITS_STOP_1_5:
		dev_dbg(dev, "%s - stop bits = 1.5 (not supported, using 1 stop bit)\n", __func__);
		bits &= ~BITS_STOP_MASK;
		cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
		break;
	case BITS_STOP_2:
		dev_dbg(dev, "%s - stop bits = 2\n", __func__);
		cflag |= CSTOPB;
		break;
	default:
		dev_dbg(dev, "%s - Unknown number of stop bits, using 1 stop bit\n", __func__);
		bits &= ~BITS_STOP_MASK;
		cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits);
		break;
	}

	cp210x_read_reg_block(port, CP210X_GET_FLOW, &flow_ctl,
			sizeof(flow_ctl));
	ctl_hs = le32_to_cpu(flow_ctl.ulControlHandshake);
	if (ctl_hs & CP210X_SERIAL_CTS_HANDSHAKE) {
		dev_dbg(dev, "%s - flow control = CRTSCTS\n", __func__);
		cflag |= CRTSCTS;
	} else {
		dev_dbg(dev, "%s - flow control = NONE\n", __func__);
		cflag &= ~CRTSCTS;
	}

	*cflagp = cflag;
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
	u32 baud;

	baud = tty->termios.c_ospeed;

	/* This maps the requested rate to a rate valid on cp2102 or cp2103,
	 * or to an arbitrary rate in [1M,2M].
	 *
	 * NOTE: B0 is not implemented.
	 */
	baud = cp210x_quantise_baudrate(baud);

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

static void cp210x_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct device *dev = &port->dev;
	unsigned int cflag, old_cflag;
	u16 bits;

	cflag = tty->termios.c_cflag;
	old_cflag = old_termios->c_cflag;

	if (tty->termios.c_ospeed != old_termios->c_ospeed)
		cp210x_change_speed(tty, port, old_termios);

	/* If the number of data bits is to be updated */
	if ((cflag & CSIZE) != (old_cflag & CSIZE)) {
		cp210x_get_line_ctl(port, &bits);
		bits &= ~BITS_DATA_MASK;
		switch (cflag & CSIZE) {
		case CS5:
			bits |= BITS_DATA_5;
			dev_dbg(dev, "%s - data bits = 5\n", __func__);
			break;
		case CS6:
			bits |= BITS_DATA_6;
			dev_dbg(dev, "%s - data bits = 6\n", __func__);
			break;
		case CS7:
			bits |= BITS_DATA_7;
			dev_dbg(dev, "%s - data bits = 7\n", __func__);
			break;
		case CS8:
		default:
			bits |= BITS_DATA_8;
			dev_dbg(dev, "%s - data bits = 8\n", __func__);
			break;
		}
		if (cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits))
			dev_dbg(dev, "Number of data bits requested not supported by device\n");
	}

	if ((cflag     & (PARENB|PARODD|CMSPAR)) !=
	    (old_cflag & (PARENB|PARODD|CMSPAR))) {
		cp210x_get_line_ctl(port, &bits);
		bits &= ~BITS_PARITY_MASK;
		if (cflag & PARENB) {
			if (cflag & CMSPAR) {
				if (cflag & PARODD) {
					bits |= BITS_PARITY_MARK;
					dev_dbg(dev, "%s - parity = MARK\n", __func__);
				} else {
					bits |= BITS_PARITY_SPACE;
					dev_dbg(dev, "%s - parity = SPACE\n", __func__);
				}
			} else {
				if (cflag & PARODD) {
					bits |= BITS_PARITY_ODD;
					dev_dbg(dev, "%s - parity = ODD\n", __func__);
				} else {
					bits |= BITS_PARITY_EVEN;
					dev_dbg(dev, "%s - parity = EVEN\n", __func__);
				}
			}
		}
		if (cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits))
			dev_dbg(dev, "Parity mode not supported by device\n");
	}

	if ((cflag & CSTOPB) != (old_cflag & CSTOPB)) {
		cp210x_get_line_ctl(port, &bits);
		bits &= ~BITS_STOP_MASK;
		if (cflag & CSTOPB) {
			bits |= BITS_STOP_2;
			dev_dbg(dev, "%s - stop bits = 2\n", __func__);
		} else {
			bits |= BITS_STOP_1;
			dev_dbg(dev, "%s - stop bits = 1\n", __func__);
		}
		if (cp210x_write_u16_reg(port, CP210X_SET_LINE_CTL, bits))
			dev_dbg(dev, "Number of stop bits requested not supported by device\n");
	}

	if ((cflag & CRTSCTS) != (old_cflag & CRTSCTS)) {
		struct cp210x_flow_ctl flow_ctl;
		u32 ctl_hs;
		u32 flow_repl;

		cp210x_read_reg_block(port, CP210X_GET_FLOW, &flow_ctl,
				sizeof(flow_ctl));
		ctl_hs = le32_to_cpu(flow_ctl.ulControlHandshake);
		flow_repl = le32_to_cpu(flow_ctl.ulFlowReplace);
		dev_dbg(dev, "%s - read ulControlHandshake=0x%08x, ulFlowReplace=0x%08x\n",
				__func__, ctl_hs, flow_repl);

		ctl_hs &= ~CP210X_SERIAL_DSR_HANDSHAKE;
		ctl_hs &= ~CP210X_SERIAL_DCD_HANDSHAKE;
		ctl_hs &= ~CP210X_SERIAL_DSR_SENSITIVITY;
		ctl_hs &= ~CP210X_SERIAL_DTR_MASK;
		ctl_hs |= CP210X_SERIAL_DTR_SHIFT(CP210X_SERIAL_DTR_ACTIVE);
		if (cflag & CRTSCTS) {
			ctl_hs |= CP210X_SERIAL_CTS_HANDSHAKE;

			flow_repl &= ~CP210X_SERIAL_RTS_MASK;
			flow_repl |= CP210X_SERIAL_RTS_SHIFT(
					CP210X_SERIAL_RTS_FLOW_CTL);
			dev_dbg(dev, "%s - flow control = CRTSCTS\n", __func__);
		} else {
			ctl_hs &= ~CP210X_SERIAL_CTS_HANDSHAKE;

			flow_repl &= ~CP210X_SERIAL_RTS_MASK;
			flow_repl |= CP210X_SERIAL_RTS_SHIFT(
					CP210X_SERIAL_RTS_ACTIVE);
			dev_dbg(dev, "%s - flow control = NONE\n", __func__);
		}

		dev_dbg(dev, "%s - write ulControlHandshake=0x%08x, ulFlowReplace=0x%08x\n",
				__func__, ctl_hs, flow_repl);
		flow_ctl.ulControlHandshake = cpu_to_le32(ctl_hs);
		flow_ctl.ulFlowReplace = cpu_to_le32(flow_repl);
		cp210x_write_reg_block(port, CP210X_SET_FLOW, &flow_ctl,
				sizeof(flow_ctl));
	}

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
	u16 control = 0;

	if (set & TIOCM_RTS) {
		control |= CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (set & TIOCM_DTR) {
		control |= CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}
	if (clear & TIOCM_RTS) {
		control &= ~CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (clear & TIOCM_DTR) {
		control &= ~CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}

	dev_dbg(&port->dev, "%s - control = 0x%.4x\n", __func__, control);

	return cp210x_write_u16_reg(port, CP210X_SET_MHS, control);
}

static void cp210x_dtr_rts(struct usb_serial_port *p, int on)
{
	if (on)
		cp210x_tiocmset_port(p, TIOCM_DTR|TIOCM_RTS, 0);
	else
		cp210x_tiocmset_port(p, 0, TIOCM_DTR|TIOCM_RTS);
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

	dev_dbg(&port->dev, "%s - control = 0x%.2x\n", __func__, control);

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
static int cp210x_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);

	switch (offset) {
	case 0:
		if (priv->config & CP2105_GPIO0_TXLED_MODE)
			return -ENODEV;
		break;
	case 1:
		if (priv->config & (CP2105_GPIO1_RXLED_MODE |
				    CP2105_GPIO1_RS485_MODE))
			return -ENODEV;
		break;
	}

	return 0;
}

static int cp210x_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	int result;
	u8 buf;

	result = cp210x_read_vendor_block(serial, REQTYPE_INTERFACE_TO_HOST,
					  CP210X_READ_LATCH, &buf, sizeof(buf));
	if (result < 0)
		return result;

	return !!(buf & BIT(gpio));
}

static void cp210x_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct usb_serial *serial = gpiochip_get_data(gc);
	struct cp210x_gpio_write buf;

	if (value == 1)
		buf.state = BIT(gpio);
	else
		buf.state = 0;

	buf.mask = BIT(gpio);

	cp210x_write_vendor_block(serial, REQTYPE_HOST_TO_INTERFACE,
				  CP210X_WRITE_LATCH, &buf, sizeof(buf));
}

static int cp210x_gpio_direction_get(struct gpio_chip *gc, unsigned int gpio)
{
	/* Hardware does not support an input mode */
	return 0;
}

static int cp210x_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	/* Hardware does not support an input mode */
	return -ENOTSUPP;
}

static int cp210x_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
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
	    (priv->gpio_mode & BIT(gpio)))
		return 0;

	if ((param == PIN_CONFIG_DRIVE_OPEN_DRAIN) &&
	    !(priv->gpio_mode & BIT(gpio)))
		return 0;

	return -ENOTSUPP;
}

/*
 * This function is for configuring GPIO using shared pins, where other signals
 * are made unavailable by configuring the use of GPIO. This is believed to be
 * only applicable to the cp2105 at this point, the other devices supported by
 * this driver that provide GPIO do so in a way that does not impact other
 * signals and are thus expected to have very different initialisation.
 */
static int cp2105_shared_gpio_init(struct usb_serial *serial)
{
	struct cp210x_serial_private *priv = usb_get_serial_data(serial);
	struct cp210x_pin_mode mode;
	struct cp210x_config config;
	u8 intf_num = cp210x_interface_num(serial);
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
		if (mode.eci == CP210X_PIN_MODE_MODEM)
			return 0;

		priv->config = config.eci_cfg;
		priv->gpio_mode = (u8)((le16_to_cpu(config.gpio_mode) &
						CP210X_ECI_GPIO_MODE_MASK) >>
						CP210X_ECI_GPIO_MODE_OFFSET);
		priv->gc.ngpio = 2;
	} else if (intf_num == 1) {
		if (mode.sci == CP210X_PIN_MODE_MODEM)
			return 0;

		priv->config = config.sci_cfg;
		priv->gpio_mode = (u8)((le16_to_cpu(config.gpio_mode) &
						CP210X_SCI_GPIO_MODE_MASK) >>
						CP210X_SCI_GPIO_MODE_OFFSET);
		priv->gc.ngpio = 3;
	} else {
		return -ENODEV;
	}

	priv->gc.label = "cp210x";
	priv->gc.request = cp210x_gpio_request;
	priv->gc.get_direction = cp210x_gpio_direction_get;
	priv->gc.direction_input = cp210x_gpio_direction_input;
	priv->gc.direction_output = cp210x_gpio_direction_output;
	priv->gc.get = cp210x_gpio_get;
	priv->gc.set = cp210x_gpio_set;
	priv->gc.set_config = cp210x_gpio_set_config;
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

static int cp2105_shared_gpio_init(struct usb_serial *serial)
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
	int ret;

	port_priv = kzalloc(sizeof(*port_priv), GFP_KERNEL);
	if (!port_priv)
		return -ENOMEM;

	port_priv->bInterfaceNumber = cp210x_interface_num(serial);

	usb_set_serial_port_data(port, port_priv);

	ret = cp210x_detect_swapped_line_ctl(port);
	if (ret) {
		kfree(port_priv);
		return ret;
	}

	return 0;
}

static int cp210x_port_remove(struct usb_serial_port *port)
{
	struct cp210x_port_private *port_priv;

	port_priv = usb_get_serial_port_data(port);
	kfree(port_priv);

	return 0;
}

static int cp210x_attach(struct usb_serial *serial)
{
	int result;
	struct cp210x_serial_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	result = cp210x_read_vendor_block(serial, REQTYPE_DEVICE_TO_HOST,
					  CP210X_GET_PARTNUM, &priv->partnum,
					  sizeof(priv->partnum));
	if (result < 0) {
		dev_warn(&serial->interface->dev,
			 "querying part number failed\n");
		priv->partnum = CP210X_PARTNUM_UNKNOWN;
	}

	usb_set_serial_data(serial, priv);

	if (priv->partnum == CP210X_PARTNUM_CP2105) {
		result = cp2105_shared_gpio_init(serial);
		if (result < 0) {
			dev_err(&serial->interface->dev,
				"GPIO initialisation failed, continuing without GPIO support\n");
		}
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
MODULE_LICENSE("GPL");
