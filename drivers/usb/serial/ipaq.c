/*
 * USB Compaq iPAQ driver
 *
 *	Copyright (C) 2001 - 2002
 *	    Ganesh Varadarajan <ganesh@veritas.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define KP_RETRIES	100

#define DRIVER_AUTHOR "Ganesh Varadarajan <ganesh@veritas.com>"
#define DRIVER_DESC "USB PocketPC PDA driver"

static int connect_retries = KP_RETRIES;
static int initial_wait;

/* Function prototypes for an ipaq */
static int  ipaq_open(struct tty_struct *tty,
			struct usb_serial_port *port);
static int  ipaq_calc_num_ports(struct usb_serial *serial);
static int  ipaq_startup(struct usb_serial *serial);

static const struct usb_device_id ipaq_id_table[] = {
	{ USB_DEVICE(0x0104, 0x00BE) }, /* Socket USB Sync */
	{ USB_DEVICE(0x03F0, 0x1016) }, /* HP USB Sync */
	{ USB_DEVICE(0x03F0, 0x1116) }, /* HP USB Sync 1611 */
	{ USB_DEVICE(0x03F0, 0x1216) }, /* HP USB Sync 1612 */
	{ USB_DEVICE(0x03F0, 0x2016) }, /* HP USB Sync 1620 */
	{ USB_DEVICE(0x03F0, 0x2116) }, /* HP USB Sync 1621 */
	{ USB_DEVICE(0x03F0, 0x2216) }, /* HP USB Sync 1622 */
	{ USB_DEVICE(0x03F0, 0x3016) }, /* HP USB Sync 1630 */
	{ USB_DEVICE(0x03F0, 0x3116) }, /* HP USB Sync 1631 */
	{ USB_DEVICE(0x03F0, 0x3216) }, /* HP USB Sync 1632 */
	{ USB_DEVICE(0x03F0, 0x4016) }, /* HP USB Sync 1640 */
	{ USB_DEVICE(0x03F0, 0x4116) }, /* HP USB Sync 1641 */
	{ USB_DEVICE(0x03F0, 0x4216) }, /* HP USB Sync 1642 */
	{ USB_DEVICE(0x03F0, 0x5016) }, /* HP USB Sync 1650 */
	{ USB_DEVICE(0x03F0, 0x5116) }, /* HP USB Sync 1651 */
	{ USB_DEVICE(0x03F0, 0x5216) }, /* HP USB Sync 1652 */
	{ USB_DEVICE(0x0409, 0x00D5) }, /* NEC USB Sync */
	{ USB_DEVICE(0x0409, 0x00D6) }, /* NEC USB Sync */
	{ USB_DEVICE(0x0409, 0x00D7) }, /* NEC USB Sync */
	{ USB_DEVICE(0x0409, 0x8024) }, /* NEC USB Sync */
	{ USB_DEVICE(0x0409, 0x8025) }, /* NEC USB Sync */
	{ USB_DEVICE(0x043E, 0x9C01) }, /* LGE USB Sync */
	{ USB_DEVICE(0x045E, 0x00CE) }, /* Microsoft USB Sync */
	{ USB_DEVICE(0x045E, 0x0400) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0401) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0402) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0403) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0404) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0405) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0406) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0407) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0408) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0409) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040A) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040B) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040C) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040D) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040E) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x040F) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0410) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0411) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0412) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0413) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0414) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0415) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0416) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0417) }, /* Windows Powered Pocket PC 2002 */
	{ USB_DEVICE(0x045E, 0x0432) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0433) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0434) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0435) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0436) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0437) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0438) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0439) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043A) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043B) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043C) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043D) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043E) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x043F) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0440) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0441) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0442) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0443) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0444) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0445) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0446) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0447) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0448) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0449) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044A) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044B) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044C) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044D) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044E) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x044F) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0450) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0451) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0452) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0453) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0454) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0455) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0456) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0457) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0458) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0459) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045A) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045B) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045C) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045D) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045E) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x045F) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0460) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0461) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0462) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0463) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0464) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0465) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0466) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0467) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0468) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0469) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046A) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046B) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046C) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046D) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046E) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x046F) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0470) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0471) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0472) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0473) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0474) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0475) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0476) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0477) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0478) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x0479) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x047A) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x047B) }, /* Windows Powered Pocket PC 2003 */
	{ USB_DEVICE(0x045E, 0x04C8) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04C9) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04CA) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04CB) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04CC) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04CD) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04CE) }, /* Windows Powered Smartphone 2002 */
	{ USB_DEVICE(0x045E, 0x04D7) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04D8) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04D9) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DA) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DB) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DC) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DD) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DE) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04DF) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E0) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E1) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E2) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E3) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E4) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E5) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E6) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E7) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E8) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04E9) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x045E, 0x04EA) }, /* Windows Powered Smartphone 2003 */
	{ USB_DEVICE(0x049F, 0x0003) }, /* Compaq iPAQ USB Sync */
	{ USB_DEVICE(0x049F, 0x0032) }, /* Compaq iPAQ USB Sync */
	{ USB_DEVICE(0x04A4, 0x0014) }, /* Hitachi USB Sync */
	{ USB_DEVICE(0x04AD, 0x0301) }, /* USB Sync 0301 */
	{ USB_DEVICE(0x04AD, 0x0302) }, /* USB Sync 0302 */
	{ USB_DEVICE(0x04AD, 0x0303) }, /* USB Sync 0303 */
	{ USB_DEVICE(0x04AD, 0x0306) }, /* GPS Pocket PC USB Sync */
	{ USB_DEVICE(0x04B7, 0x0531) }, /* MyGuide 7000 XL USB Sync */
	{ USB_DEVICE(0x04C5, 0x1058) }, /* FUJITSU USB Sync */
	{ USB_DEVICE(0x04C5, 0x1079) }, /* FUJITSU USB Sync */
	{ USB_DEVICE(0x04DA, 0x2500) }, /* Panasonic USB Sync */
	{ USB_DEVICE(0x04DD, 0x9102) }, /* SHARP WS003SH USB Modem */
	{ USB_DEVICE(0x04DD, 0x9121) }, /* SHARP WS004SH USB Modem */
	{ USB_DEVICE(0x04DD, 0x9123) }, /* SHARP WS007SH USB Modem */
	{ USB_DEVICE(0x04DD, 0x9151) }, /* SHARP S01SH USB Modem */
	{ USB_DEVICE(0x04DD, 0x91AC) }, /* SHARP WS011SH USB Modem */
	{ USB_DEVICE(0x04E8, 0x5F00) }, /* Samsung NEXiO USB Sync */
	{ USB_DEVICE(0x04E8, 0x5F01) }, /* Samsung NEXiO USB Sync */
	{ USB_DEVICE(0x04E8, 0x5F02) }, /* Samsung NEXiO USB Sync */
	{ USB_DEVICE(0x04E8, 0x5F03) }, /* Samsung NEXiO USB Sync */
	{ USB_DEVICE(0x04E8, 0x5F04) }, /* Samsung NEXiO USB Sync */
	{ USB_DEVICE(0x04E8, 0x6611) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6613) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6615) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6617) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6619) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x661B) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x662E) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6630) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04E8, 0x6632) }, /* Samsung MITs USB Sync */
	{ USB_DEVICE(0x04f1, 0x3011) }, /* JVC USB Sync */
	{ USB_DEVICE(0x04F1, 0x3012) }, /* JVC USB Sync */
	{ USB_DEVICE(0x0502, 0x1631) }, /* c10 Series */
	{ USB_DEVICE(0x0502, 0x1632) }, /* c20 Series */
	{ USB_DEVICE(0x0502, 0x16E1) }, /* Acer n10 Handheld USB Sync */
	{ USB_DEVICE(0x0502, 0x16E2) }, /* Acer n20 Handheld USB Sync */
	{ USB_DEVICE(0x0502, 0x16E3) }, /* Acer n30 Handheld USB Sync */
	{ USB_DEVICE(0x0536, 0x01A0) }, /* HHP PDT */
	{ USB_DEVICE(0x0543, 0x0ED9) }, /* ViewSonic Color Pocket PC V35 */
	{ USB_DEVICE(0x0543, 0x1527) }, /* ViewSonic Color Pocket PC V36 */
	{ USB_DEVICE(0x0543, 0x1529) }, /* ViewSonic Color Pocket PC V37 */
	{ USB_DEVICE(0x0543, 0x152B) }, /* ViewSonic Color Pocket PC V38 */
	{ USB_DEVICE(0x0543, 0x152E) }, /* ViewSonic Pocket PC */
	{ USB_DEVICE(0x0543, 0x1921) }, /* ViewSonic Communicator Pocket PC */
	{ USB_DEVICE(0x0543, 0x1922) }, /* ViewSonic Smartphone */
	{ USB_DEVICE(0x0543, 0x1923) }, /* ViewSonic Pocket PC V30 */
	{ USB_DEVICE(0x05E0, 0x2000) }, /* Symbol USB Sync */
	{ USB_DEVICE(0x05E0, 0x2001) }, /* Symbol USB Sync 0x2001 */
	{ USB_DEVICE(0x05E0, 0x2002) }, /* Symbol USB Sync 0x2002 */
	{ USB_DEVICE(0x05E0, 0x2003) }, /* Symbol USB Sync 0x2003 */
	{ USB_DEVICE(0x05E0, 0x2004) }, /* Symbol USB Sync 0x2004 */
	{ USB_DEVICE(0x05E0, 0x2005) }, /* Symbol USB Sync 0x2005 */
	{ USB_DEVICE(0x05E0, 0x2006) }, /* Symbol USB Sync 0x2006 */
	{ USB_DEVICE(0x05E0, 0x2007) }, /* Symbol USB Sync 0x2007 */
	{ USB_DEVICE(0x05E0, 0x2008) }, /* Symbol USB Sync 0x2008 */
	{ USB_DEVICE(0x05E0, 0x2009) }, /* Symbol USB Sync 0x2009 */
	{ USB_DEVICE(0x05E0, 0x200A) }, /* Symbol USB Sync 0x200A */
	{ USB_DEVICE(0x067E, 0x1001) }, /* Intermec Mobile Computer */
	{ USB_DEVICE(0x07CF, 0x2001) }, /* CASIO USB Sync 2001 */
	{ USB_DEVICE(0x07CF, 0x2002) }, /* CASIO USB Sync 2002 */
	{ USB_DEVICE(0x07CF, 0x2003) }, /* CASIO USB Sync 2003 */
	{ USB_DEVICE(0x0930, 0x0700) }, /* TOSHIBA USB Sync 0700 */
	{ USB_DEVICE(0x0930, 0x0705) }, /* TOSHIBA Pocket PC e310 */
	{ USB_DEVICE(0x0930, 0x0706) }, /* TOSHIBA Pocket PC e740 */
	{ USB_DEVICE(0x0930, 0x0707) }, /* TOSHIBA Pocket PC e330 Series */
	{ USB_DEVICE(0x0930, 0x0708) }, /* TOSHIBA Pocket PC e350 Series */
	{ USB_DEVICE(0x0930, 0x0709) }, /* TOSHIBA Pocket PC e750 Series */
	{ USB_DEVICE(0x0930, 0x070A) }, /* TOSHIBA Pocket PC e400 Series */
	{ USB_DEVICE(0x0930, 0x070B) }, /* TOSHIBA Pocket PC e800 Series */
	{ USB_DEVICE(0x094B, 0x0001) }, /* Linkup Systems USB Sync */
	{ USB_DEVICE(0x0960, 0x0065) }, /* BCOM USB Sync 0065 */
	{ USB_DEVICE(0x0960, 0x0066) }, /* BCOM USB Sync 0066 */
	{ USB_DEVICE(0x0960, 0x0067) }, /* BCOM USB Sync 0067 */
	{ USB_DEVICE(0x0961, 0x0010) }, /* Portatec USB Sync */
	{ USB_DEVICE(0x099E, 0x0052) }, /* Trimble GeoExplorer */
	{ USB_DEVICE(0x099E, 0x4000) }, /* TDS Data Collector */
	{ USB_DEVICE(0x0B05, 0x4200) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0B05, 0x4201) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0B05, 0x4202) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0B05, 0x420F) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0B05, 0x9200) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0B05, 0x9202) }, /* ASUS USB Sync */
	{ USB_DEVICE(0x0BB4, 0x00CE) }, /* HTC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x00CF) }, /* HTC USB Modem */
	{ USB_DEVICE(0x0BB4, 0x0A01) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A02) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A03) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A04) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A05) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A06) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A07) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A08) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A09) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0A) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0B) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0C) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0D) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0E) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A0F) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A10) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A11) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A12) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A13) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A14) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A15) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A16) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A17) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A18) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A19) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1A) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1B) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1C) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1D) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1E) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A1F) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A20) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A21) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A22) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A23) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A24) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A25) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A26) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A27) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A28) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A29) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2A) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2B) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2C) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2D) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2E) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A2F) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A30) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A31) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A32) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A33) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A34) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A35) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A36) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A37) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A38) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A39) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3A) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3B) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3C) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3D) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3E) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A3F) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A40) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A41) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A42) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A43) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A44) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A45) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A46) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A47) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A48) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A49) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4A) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4B) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4C) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4D) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4E) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A4F) }, /* PocketPC USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A50) }, /* HTC SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A51) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A52) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A53) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A54) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A55) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A56) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A57) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A58) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A59) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5A) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5B) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5C) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5D) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5E) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A5F) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A60) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A61) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A62) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A63) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A64) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A65) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A66) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A67) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A68) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A69) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6A) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6B) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6C) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6D) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6E) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A6F) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A70) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A71) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A72) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A73) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A74) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A75) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A76) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A77) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A78) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A79) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7A) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7B) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7C) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7D) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7E) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A7F) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A80) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A81) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A82) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A83) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A84) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A85) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A86) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A87) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A88) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A89) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8A) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8B) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8C) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8D) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8E) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A8F) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A90) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A91) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A92) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A93) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A94) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A95) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A96) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A97) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A98) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A99) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9A) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9B) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9C) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9D) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9E) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0A9F) }, /* SmartPhone USB Sync */
	{ USB_DEVICE(0x0BB4, 0x0BCE) }, /* "High Tech Computer Corp" */
	{ USB_DEVICE(0x0BF8, 0x1001) }, /* Fujitsu Siemens Computers USB Sync */
	{ USB_DEVICE(0x0C44, 0x03A2) }, /* Motorola iDEN Smartphone */
	{ USB_DEVICE(0x0C8E, 0x6000) }, /* Cesscom Luxian Series */
	{ USB_DEVICE(0x0CAD, 0x9001) }, /* Motorola PowerPad Pocket PC Device */
	{ USB_DEVICE(0x0F4E, 0x0200) }, /* Freedom Scientific USB Sync */
	{ USB_DEVICE(0x0F98, 0x0201) }, /* Cyberbank USB Sync */
	{ USB_DEVICE(0x0FB8, 0x3001) }, /* Wistron USB Sync */
	{ USB_DEVICE(0x0FB8, 0x3002) }, /* Wistron USB Sync */
	{ USB_DEVICE(0x0FB8, 0x3003) }, /* Wistron USB Sync */
	{ USB_DEVICE(0x0FB8, 0x4001) }, /* Wistron USB Sync */
	{ USB_DEVICE(0x1066, 0x00CE) }, /* E-TEN USB Sync */
	{ USB_DEVICE(0x1066, 0x0300) }, /* E-TEN P3XX Pocket PC */
	{ USB_DEVICE(0x1066, 0x0500) }, /* E-TEN P5XX Pocket PC */
	{ USB_DEVICE(0x1066, 0x0600) }, /* E-TEN P6XX Pocket PC */
	{ USB_DEVICE(0x1066, 0x0700) }, /* E-TEN P7XX Pocket PC */
	{ USB_DEVICE(0x1114, 0x0001) }, /* Psion Teklogix Sync 753x */
	{ USB_DEVICE(0x1114, 0x0004) }, /* Psion Teklogix Sync netBookPro */
	{ USB_DEVICE(0x1114, 0x0006) }, /* Psion Teklogix Sync 7525 */
	{ USB_DEVICE(0x1182, 0x1388) }, /* VES USB Sync */
	{ USB_DEVICE(0x11D9, 0x1002) }, /* Rugged Pocket PC 2003 */
	{ USB_DEVICE(0x11D9, 0x1003) }, /* Rugged Pocket PC 2003 */
	{ USB_DEVICE(0x1231, 0xCE01) }, /* USB Sync 03 */
	{ USB_DEVICE(0x1231, 0xCE02) }, /* USB Sync 03 */
	{ USB_DEVICE(0x1690, 0x0601) }, /* Askey USB Sync */
	{ USB_DEVICE(0x22B8, 0x4204) }, /* Motorola MPx200 Smartphone */
	{ USB_DEVICE(0x22B8, 0x4214) }, /* Motorola MPc GSM */
	{ USB_DEVICE(0x22B8, 0x4224) }, /* Motorola MPx220 Smartphone */
	{ USB_DEVICE(0x22B8, 0x4234) }, /* Motorola MPc CDMA */
	{ USB_DEVICE(0x22B8, 0x4244) }, /* Motorola MPx100 Smartphone */
	{ USB_DEVICE(0x3340, 0x011C) }, /* Mio DigiWalker PPC StrongARM */
	{ USB_DEVICE(0x3340, 0x0326) }, /* Mio DigiWalker 338 */
	{ USB_DEVICE(0x3340, 0x0426) }, /* Mio DigiWalker 338 */
	{ USB_DEVICE(0x3340, 0x043A) }, /* Mio DigiWalker USB Sync */
	{ USB_DEVICE(0x3340, 0x051C) }, /* MiTAC USB Sync 528 */
	{ USB_DEVICE(0x3340, 0x053A) }, /* Mio DigiWalker SmartPhone USB Sync */
	{ USB_DEVICE(0x3340, 0x071C) }, /* MiTAC USB Sync */
	{ USB_DEVICE(0x3340, 0x0B1C) }, /* Generic PPC StrongARM */
	{ USB_DEVICE(0x3340, 0x0E3A) }, /* Generic PPC USB Sync */
	{ USB_DEVICE(0x3340, 0x0F1C) }, /* Itautec USB Sync */
	{ USB_DEVICE(0x3340, 0x0F3A) }, /* Generic SmartPhone USB Sync */
	{ USB_DEVICE(0x3340, 0x1326) }, /* Itautec USB Sync */
	{ USB_DEVICE(0x3340, 0x191C) }, /* YAKUMO USB Sync */
	{ USB_DEVICE(0x3340, 0x2326) }, /* Vobis USB Sync */
	{ USB_DEVICE(0x3340, 0x3326) }, /* MEDION Winodws Moble USB Sync */
	{ USB_DEVICE(0x3708, 0x20CE) }, /* Legend USB Sync */
	{ USB_DEVICE(0x3708, 0x21CE) }, /* Lenovo USB Sync */
	{ USB_DEVICE(0x4113, 0x0210) }, /* Mobile Media Technology USB Sync */
	{ USB_DEVICE(0x4113, 0x0211) }, /* Mobile Media Technology USB Sync */
	{ USB_DEVICE(0x4113, 0x0400) }, /* Mobile Media Technology USB Sync */
	{ USB_DEVICE(0x4113, 0x0410) }, /* Mobile Media Technology USB Sync */
	{ USB_DEVICE(0x413C, 0x4001) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4002) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4003) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4004) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4005) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4006) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4007) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4008) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x413C, 0x4009) }, /* Dell Axim USB Sync */
	{ USB_DEVICE(0x4505, 0x0010) }, /* Smartphone */
	{ USB_DEVICE(0x5E04, 0xCE00) }, /* SAGEM Wireless Assistant */
	{ }                             /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ipaq_id_table);


/* All of the device info needed for the Compaq iPAQ */
static struct usb_serial_driver ipaq_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ipaq",
	},
	.description =		"PocketPC PDA",
	.id_table =		ipaq_id_table,
	.bulk_in_size =		256,
	.bulk_out_size =	256,
	.open =			ipaq_open,
	.attach =		ipaq_startup,
	.calc_num_ports =	ipaq_calc_num_ports,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ipaq_device, NULL
};

static int ipaq_open(struct tty_struct *tty,
			struct usb_serial_port *port)
{
	struct usb_serial	*serial = port->serial;
	int			result = 0;
	int			retries = connect_retries;

	msleep(1000*initial_wait);

	/*
	 * Send out control message observed in win98 sniffs. Not sure what
	 * it does, but from empirical observations, it seems that the device
	 * will start the chat sequence once one of these messages gets
	 * through. Since this has a reasonably high failure rate, we retry
	 * several times.
	 */
	while (retries) {
		retries--;
		result = usb_control_msg(serial->dev,
				usb_sndctrlpipe(serial->dev, 0), 0x22, 0x21,
				0x1, 0, NULL, 0, 100);
		if (!result)
			break;

		msleep(1000);
	}
	if (!retries && result) {
		dev_err(&port->dev, "%s - failed doing control urb, error %d\n",
							__func__, result);
		return result;
	}

	return usb_serial_generic_open(tty, port);
}

static int ipaq_calc_num_ports(struct usb_serial *serial)
{
	/*
	 * some devices have 3 endpoints, the 3rd of which
	 * must be ignored as it would make the core
	 * create a second port which oopses when used
	 */
	int ipaq_num_ports = 1;

	dev_dbg(&serial->dev->dev, "%s - numberofendpoints: %d\n", __func__,
		(int)serial->interface->cur_altsetting->desc.bNumEndpoints);

	/*
	 * a few devices have 4 endpoints, seemingly Yakuma devices,
	 * and we need the second pair, so let them have 2 ports
	 *
	 * TODO: can we drop port 1 ?
	 */
	if (serial->interface->cur_altsetting->desc.bNumEndpoints > 3) {
		ipaq_num_ports = 2;
	}

	return ipaq_num_ports;
}


static int ipaq_startup(struct usb_serial *serial)
{
	/* Some of the devices in ipaq_id_table[] are composite, and we
	 * shouldn't bind to all the interfaces.  This test will rule out
	 * some obviously invalid possibilities.
	 */
	if (serial->num_bulk_in < serial->num_ports ||
			serial->num_bulk_out < serial->num_ports)
		return -ENODEV;

	if (serial->dev->actconfig->desc.bConfigurationValue != 1) {
		/*
		 * FIXME: HP iPaq rx3715, possibly others, have 1 config that
		 * is labeled as 2
		 */

		dev_err(&serial->dev->dev, "active config #%d != 1 ??\n",
			serial->dev->actconfig->desc.bConfigurationValue);
		return -ENODEV;
	}

	dev_dbg(&serial->dev->dev,
		"%s - iPAQ module configured for %d ports\n", __func__,
		serial->num_ports);

	return usb_reset_configuration(serial->dev);
}

module_usb_serial_driver(serial_drivers, ipaq_id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(connect_retries, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(connect_retries,
		"Maximum number of connect retries (one second each)");

module_param(initial_wait, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(initial_wait,
		"Time to wait before attempting a connection (in seconds)");
