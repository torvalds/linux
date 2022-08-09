/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for USB Mass Storage compliant devices
 * Unusual Devices File
 *
 * Current development and maintenance by:
 *   (c) 2000-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 2000 Adam J. Richter (adam@yggdrasil.com), Yggdrasil Computing, Inc.
 */

/*
 * IMPORTANT NOTE: This file must be included in another file which does
 * the following thing for it to work:
 * The UNUSUAL_DEV, COMPLIANT_DEV, and USUAL_DEV macros must be defined
 * before this file is included.
 */

/*
 * If you edit this file, please try to keep it sorted first by VendorID,
 * then by ProductID.
 *
 * If you want to add an entry for this file, be sure to include the
 * following information:
 *	- a patch that adds the entry for your device, including your
 *	  email address right above the entry (plus maybe a brief
 *	  explanation of the reason for the entry),
 *	- a copy of /sys/kernel/debug/usb/devices with your device plugged in
 *	  running with this patch.
 * Send your submission to the USB development list <linux-usb@vger.kernel.org>
 */

/*
 * Note: If you add an entry only in order to set the CAPACITY_OK flag,
 * use the COMPLIANT_DEV macro instead of UNUSUAL_DEV.  This is
 * because such entries mark devices which actually work correctly,
 * as opposed to devices that do something strangely or wrongly.
 */

/*
 * In-kernel mode switching is deprecated.  Do not add new devices to
 * this list for the sole purpose of switching them to a different
 * mode.  Existing userspace solutions are superior.
 *
 * New mode switching devices should instead be added to the database
 * maintained at https://www.draisberghof.de/usb_modeswitch/
 */

#if !defined(CONFIG_USB_STORAGE_SDDR09) && \
		!defined(CONFIG_USB_STORAGE_SDDR09_MODULE)
#define NO_SDDR09
#endif

/* patch submitted by Vivian Bregier <Vivian.Bregier@imag.fr> */
UNUSUAL_DEV(  0x03eb, 0x2002, 0x0100, 0x0100,
		"ATMEL",
		"SND1 Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE),

/* Reported by Rodolfo Quesada <rquesada@roqz.net> */
UNUSUAL_DEV(  0x03ee, 0x6906, 0x0003, 0x0003,
		"VIA Technologies Inc.",
		"Mitsumi multi cardreader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

UNUSUAL_DEV(  0x03f0, 0x0107, 0x0200, 0x0200,
		"HP",
		"CD-Writer+",
		USB_SC_8070, USB_PR_CB, NULL, 0),

/* Reported by Ben Efros <ben@pc-doctor.com> */
UNUSUAL_DEV(  0x03f0, 0x070c, 0x0000, 0x0000,
		"HP",
		"Personal Media Drive",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SANE_SENSE ),

/*
 * Reported by Grant Grundler <grundler@parisc-linux.org>
 * HP r707 camera in "Disk" mode with 2.00.23 or 2.00.24 firmware.
 */
UNUSUAL_DEV(  0x03f0, 0x4002, 0x0001, 0x0001,
		"HP",
		"PhotoSmart R707",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_FIX_CAPACITY),

UNUSUAL_DEV(  0x03f3, 0x0001, 0x0000, 0x9999,
		"Adaptec",
		"USBConnect 2000",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Reported by Sebastian Kapfer <sebastian_kapfer@gmx.net>
 * and Olaf Hering <olh@suse.de> (different bcd's, same vendor/product)
 * for USB floppies that need the SINGLE_LUN enforcement.
 */
UNUSUAL_DEV(  0x0409, 0x0040, 0x0000, 0x9999,
		"NEC",
		"NEC USB UF000x",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Patch submitted by Mihnea-Costin Grigore <mihnea@zulu.ro> */
UNUSUAL_DEV(  0x040d, 0x6205, 0x0003, 0x0003,
		"VIA Technologies Inc.",
		"USB 2.0 Card Reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Deduced by Jonathan Woithe <jwoithe@just42.net>
 * Entry needed for flags: US_FL_FIX_INQUIRY because initial inquiry message
 * always fails and confuses drive.
 */
UNUSUAL_DEV(  0x0411, 0x001c, 0x0113, 0x0113,
		"Buffalo",
		"DUB-P40G HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/* Submitted by Ernestas Vaiciukevicius <ernisv@gmail.com> */
UNUSUAL_DEV(  0x0419, 0x0100, 0x0100, 0x0100,
		"Samsung Info. Systems America, Inc.",
		"MP3 Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Orgad Shaneh <orgads@gmail.com> */
UNUSUAL_DEV(  0x0419, 0xaace, 0x0100, 0x0100,
		"Samsung", "MP3 Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Christian Leber <christian@leber.de> */
UNUSUAL_DEV(  0x0419, 0xaaf5, 0x0100, 0x0100,
		"TrekStor",
		"i.Beat 115 2.0",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_NOT_LOCKABLE ),

/* Reported by Stefan Werner <dustbln@gmx.de> */
UNUSUAL_DEV(  0x0419, 0xaaf6, 0x0100, 0x0100,
		"TrekStor",
		"i.Beat Joy 2.0",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Pete Zaitcev <zaitcev@redhat.com>, bz#176584 */
UNUSUAL_DEV(  0x0420, 0x0001, 0x0100, 0x0100,
		"GENERIC", "MP3 PLAYER", /* MyMusix PD-205 on the outside. */
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Andrew Nayenko <relan@bk.ru>
 * Updated for new firmware by Phillip Potter <phil@philpotter.co.uk>
 */
UNUSUAL_DEV(  0x0421, 0x0019, 0x0592, 0x0610,
		"Nokia",
		"Nokia 6288",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Mario Rettig <mariorettig@web.de> */
UNUSUAL_DEV(  0x0421, 0x042e, 0x0100, 0x0100,
		"Nokia",
		"Nokia 3250",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/* Reported by <honkkis@gmail.com> */
UNUSUAL_DEV(  0x0421, 0x0433, 0x0100, 0x0100,
		"Nokia",
		"E70",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/* Reported by Jon Hart <Jon.Hart@web.de> */
UNUSUAL_DEV(  0x0421, 0x0434, 0x0100, 0x0100,
		"Nokia",
		"E60",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Sumedha Swamy <sumedhaswamy@gmail.com> and
 * Einar Th. Einarsson <einarthered@gmail.com>
 */
UNUSUAL_DEV(  0x0421, 0x0444, 0x0100, 0x0100,
		"Nokia",
		"N91",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/*
 * Reported by Jiri Slaby <jirislaby@gmail.com> and
 * Rene C. Castberg <Rene@Castberg.org>
 */
UNUSUAL_DEV(  0x0421, 0x0446, 0x0100, 0x0100,
		"Nokia",
		"N80",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/* Reported by Matthew Bloch <matthew@bytemark.co.uk> */
UNUSUAL_DEV(  0x0421, 0x044e, 0x0100, 0x0100,
		"Nokia",
		"E61",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/* Reported by Bardur Arantsson <bardur@scientician.net> */
UNUSUAL_DEV(  0x0421, 0x047c, 0x0370, 0x0610,
		"Nokia",
		"6131",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Manuel Osdoba <manuel.osdoba@tu-ilmenau.de> */
UNUSUAL_DEV( 0x0421, 0x0492, 0x0452, 0x9999,
		"Nokia",
		"Nokia 6233",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Alex Corcoles <alex@corcoles.net> */
UNUSUAL_DEV(  0x0421, 0x0495, 0x0370, 0x0370,
		"Nokia",
		"6234",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Daniele Forsi <dforsi@gmail.com> */
UNUSUAL_DEV(  0x0421, 0x04b9, 0x0350, 0x0350,
		"Nokia",
		"5300",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Patch submitted by Victor A. Santos <victoraur.santos@gmail.com> */
UNUSUAL_DEV(  0x0421, 0x05af, 0x0742, 0x0742,
		"Nokia",
		"305",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64),

/* Patch submitted by Mikhail Zolotaryov <lebon@lebon.org.ua> */
UNUSUAL_DEV(  0x0421, 0x06aa, 0x1110, 0x1110,
		"Nokia",
		"502",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

#ifdef NO_SDDR09
UNUSUAL_DEV(  0x0436, 0x0005, 0x0100, 0x0100,
		"Microtech",
		"CameraMate",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN ),
#endif

/*
 * Patch submitted by Daniel Drake <dsd@gentoo.org>
 * Device reports nonsense bInterfaceProtocol 6 when connected over USB2
 */
UNUSUAL_DEV(  0x0451, 0x5416, 0x0100, 0x0100,
		"Neuros Audio",
		"USB 2.0 HD 2.5",
		USB_SC_DEVICE, USB_PR_BULK, NULL,
		US_FL_NEED_OVERRIDE ),

/*
 * Pete Zaitcev <zaitcev@yahoo.com>, from Patrick C. F. Ernzer, bz#162559.
 * The key does not actually break, but it returns zero sense which
 * makes our SCSI stack to print confusing messages.
 */
UNUSUAL_DEV(  0x0457, 0x0150, 0x0100, 0x0100,
		"USBest Technology",	/* sold by Transcend */
		"USB Mass Storage Device",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_NOT_LOCKABLE ),

/*
 * Bohdan Linda <bohdan.linda@gmail.com>
 * 1GB USB sticks MyFlash High Speed. I have restricted
 * the revision to my model only
 */
UNUSUAL_DEV(  0x0457, 0x0151, 0x0100, 0x0100,
		"USB 2.0",
		"Flash Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NOT_LOCKABLE ),

/*
 * Reported by Tamas Kerecsen <kerecsen@bigfoot.com>
 * Obviously the PROM has not been customized by the VAR;
 * the Vendor and Product string descriptors are:
 *	Generic Mass Storage (PROTOTYPE--Remember to change idVendor)
 *	Generic Manufacturer (PROTOTYPE--Remember to change idVendor)
 */
UNUSUAL_DEV(  0x045e, 0xffff, 0x0000, 0x0000,
		"Mitac",
		"GPS",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/*
 * This virtual floppy is found in Sun equipment (x4600, x4200m2, etc.)
 * Reported by Pete Zaitcev <zaitcev@redhat.com>
 * This device chokes on both version of MODE SENSE which we have, so
 * use_10_for_ms is not effective, and we use US_FL_NO_WP_DETECT.
 */
UNUSUAL_DEV(  0x046b, 0xff40, 0x0100, 0x0100,
		"AMI",
		"Virtual Floppy",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT),

/* Reported by Egbert Eich <eich@suse.com> */
UNUSUAL_DEV(  0x0480, 0xd010, 0x0100, 0x9999,
		"Toshiba",
		"External USB 3.0",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_ALWAYS_SYNC),

/* Patch submitted by Philipp Friedrich <philipp@void.at> */
UNUSUAL_DEV(  0x0482, 0x0100, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S3x",
		USB_SC_8070, USB_PR_CB, NULL, US_FL_FIX_INQUIRY),

/* Patch submitted by Philipp Friedrich <philipp@void.at> */
UNUSUAL_DEV(  0x0482, 0x0101, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S4",
		USB_SC_8070, USB_PR_CB, NULL, US_FL_FIX_INQUIRY),

/* Patch submitted by Stephane Galles <stephane.galles@free.fr> */
UNUSUAL_DEV(  0x0482, 0x0103, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S5",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_FIX_INQUIRY),

/* Patch submitted by Jens Taprogge <jens.taprogge@taprogge.org> */
UNUSUAL_DEV(  0x0482, 0x0107, 0x0100, 0x0100,
		"Kyocera",
		"CONTAX SL300R T*",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_NOT_LOCKABLE),

/*
 * Reported by Paul Stewart <stewart@wetlogic.net>
 * This entry is needed because the device reports Sub=ff
 */
UNUSUAL_DEV(  0x04a4, 0x0004, 0x0001, 0x0001,
		"Hitachi",
		"DVD-CAM DZ-MV100A Camcorder",
		USB_SC_SCSI, USB_PR_CB, NULL, US_FL_SINGLE_LUN),

/*
 * BENQ DC5330
 * Reported by Manuel Fombuena <mfombuena@ya.com> and
 * Frank Copeland <fjc@thingy.apana.org.au>
 */
UNUSUAL_DEV(  0x04a5, 0x3010, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Patch for Nikon coolpix 2000
 * Submitted by Fabien Cosse <fabien.cosse@wanadoo.fr>
 */
UNUSUAL_DEV(  0x04b0, 0x0301, 0x0010, 0x0010,
		"NIKON",
		"NIKON DSC E2000",
		USB_SC_DEVICE, USB_PR_DEVICE,NULL,
		US_FL_NOT_LOCKABLE ),

/* Reported by Doug Maxey (dwm@austin.ibm.com) */
UNUSUAL_DEV(  0x04b3, 0x4001, 0x0110, 0x0110,
		"IBM",
		"IBM RSA2",
		USB_SC_DEVICE, USB_PR_CB, NULL,
		US_FL_MAX_SECTORS_MIN),

/*
 * Reported by Simon Levitt <simon@whattf.com>
 * This entry needs Sub and Proto fields
 */
UNUSUAL_DEV(  0x04b8, 0x0601, 0x0100, 0x0100,
		"Epson",
		"875DC Storage",
		USB_SC_SCSI, USB_PR_CB, NULL, US_FL_FIX_INQUIRY),

/*
 * Reported by Khalid Aziz <khalid@gonehiking.org>
 * This entry is needed because the device reports Sub=ff
 */
UNUSUAL_DEV(  0x04b8, 0x0602, 0x0110, 0x0110,
		"Epson",
		"785EPX Storage",
		USB_SC_SCSI, USB_PR_BULK, NULL, US_FL_SINGLE_LUN),

/*
 * Reported by James Buren <braewoods+lkml@braewoods.net>
 * Virtual ISOs cannot be remounted if ejected while the device is locked
 * Disable locking to mimic Windows behavior that bypasses the issue
 */
UNUSUAL_DEV(  0x04c5, 0x2028, 0x0001, 0x0001,
		"iODD",
		"2531/2541",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_NOT_LOCKABLE),

/*
 * Not sure who reported this originally but
 * Pavel Machek <pavel@ucw.cz> reported that the extra US_FL_SINGLE_LUN
 * flag be added */
UNUSUAL_DEV(  0x04cb, 0x0100, 0x0000, 0x2210,
		"Fujifilm",
		"FinePix 1400Zoom",
		USB_SC_UFI, USB_PR_DEVICE, NULL, US_FL_FIX_INQUIRY | US_FL_SINGLE_LUN),

/*
 * Reported by Ondrej Zary <linux@zary.sk>
 * The device reports one sector more and breaks when that sector is accessed
 * Firmwares older than 2.6c (the latest one and the only that claims Linux
 * support) have also broken tag handling
 */
UNUSUAL_DEV(  0x04ce, 0x0002, 0x0000, 0x026b,
		"ScanLogic",
		"SL11R-IDE",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_BULK_IGNORE_TAG),
UNUSUAL_DEV(  0x04ce, 0x0002, 0x026c, 0x026c,
		"ScanLogic",
		"SL11R-IDE",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/*
 * Reported by Kriston Fincher <kriston@airmail.net>
 * Patch submitted by Sean Millichamp <sean@bruenor.org>
 * This is to support the Panasonic PalmCam PV-SD4090
 * This entry is needed because the device reports Sub=ff 
 */
UNUSUAL_DEV(  0x04da, 0x0901, 0x0100, 0x0200,
		"Panasonic",
		"LS-120 Camera",
		USB_SC_UFI, USB_PR_DEVICE, NULL, 0),

/*
 * From Yukihiro Nakai, via zaitcev@yahoo.com.
 * This is needed for CB instead of CBI
 */
UNUSUAL_DEV(  0x04da, 0x0d05, 0x0000, 0x0000,
		"Sharp CE-CW05",
		"CD-R/RW Drive",
		USB_SC_8070, USB_PR_CB, NULL, 0),

/* Reported by Adriaan Penning <a.penning@luon.net> */
UNUSUAL_DEV(  0x04da, 0x2372, 0x0000, 0x9999,
		"Panasonic",
		"DMC-LCx Camera",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_NOT_LOCKABLE ),

/* Reported by Simeon Simeonov <simeonov_2000@yahoo.com> */
UNUSUAL_DEV(  0x04da, 0x2373, 0x0000, 0x9999,
		"LEICA",
		"D-LUX Camera",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_NOT_LOCKABLE ),

/*
 * Most of the following entries were developed with the help of
 * Shuttle/SCM directly.
 */
UNUSUAL_DEV(  0x04e6, 0x0001, 0x0200, 0x0200,
		"Matshita",
		"LS-120",
		USB_SC_8020, USB_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x0002, 0x0100, 0x0100,
		"Shuttle",
		"eUSCSI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

#ifdef NO_SDDR09
UNUSUAL_DEV(  0x04e6, 0x0005, 0x0100, 0x0208,
		"SCM Microsystems",
		"eUSB CompactFlash Adapter",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN),
#endif

/* Reported by Markus Demleitner <msdemlei@cl.uni-heidelberg.de> */
UNUSUAL_DEV(  0x04e6, 0x0006, 0x0100, 0x0100,
		"SCM Microsystems Inc.",
		"eUSB MMC Adapter",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN),

/* Reported by Daniel Nouri <dpunktnpunkt@web.de> */
UNUSUAL_DEV(  0x04e6, 0x0006, 0x0205, 0x0205,
		"Shuttle",
		"eUSB MMC Adapter",
		USB_SC_SCSI, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV(  0x04e6, 0x0007, 0x0100, 0x0200,
		"Sony",
		"Hifd",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV(  0x04e6, 0x0009, 0x0200, 0x0200,
		"Shuttle",
		"eUSB ATA/ATAPI Adapter",
		USB_SC_8020, USB_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x000a, 0x0200, 0x0200,
		"Shuttle",
		"eUSB CompactFlash Adapter",
		USB_SC_8020, USB_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x000b, 0x0100, 0x0100,
		"Shuttle",
		"eUSCSI Bridge",
		USB_SC_SCSI, USB_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ), 

UNUSUAL_DEV(  0x04e6, 0x000c, 0x0100, 0x0100,
		"Shuttle",
		"eUSCSI Bridge",
		USB_SC_SCSI, USB_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

UNUSUAL_DEV(  0x04e6, 0x000f, 0x0000, 0x9999,
		"SCM Microsystems",
		"eUSB SCSI Adapter (Bus Powered)",
		USB_SC_SCSI, USB_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

UNUSUAL_DEV(  0x04e6, 0x0101, 0x0200, 0x0200,
		"Shuttle",
		"CD-RW Device",
		USB_SC_8020, USB_PR_CB, NULL, 0),

/* Reported by Dmitry Khlystov <adminimus@gmail.com> */
UNUSUAL_DEV(  0x04e8, 0x507c, 0x0220, 0x0220,
		"Samsung",
		"YP-U3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64),

/* Reported by Vitaly Kuznetsov <vitty@altlinux.ru> */
UNUSUAL_DEV(  0x04e8, 0x5122, 0x0000, 0x9999,
		"Samsung",
		"YP-CP3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 | US_FL_BULK_IGNORE_TAG),

/* Added by Dmitry Artamonow <mad_soft@inbox.ru> */
UNUSUAL_DEV(  0x04e8, 0x5136, 0x0000, 0x9999,
		"Samsung",
		"YP-Z3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64),

/*
 * Entry and supporting patch by Theodore Kilgore <kilgota@auburn.edu>.
 * Device uses standards-violating 32-byte Bulk Command Block Wrappers and
 * reports itself as "Proprietary SCSI Bulk." Cf. device entry 0x084d:0x0011.
 */
UNUSUAL_DEV(  0x04fc, 0x80c2, 0x0100, 0x0100,
		"Kobian Mercury",
		"Binocam DCB-132",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BULK32),

/* Reported by Bob Sass <rls@vectordb.com> -- only rev 1.33 tested */
UNUSUAL_DEV(  0x050d, 0x0115, 0x0133, 0x0133,
		"Belkin",
		"USB SCSI Adaptor",
		USB_SC_SCSI, USB_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Iomega Clik! Drive 
 * Reported by David Chatenay <dchatenay@hotmail.com>
 * The reason this is needed is not fully known.
 */
UNUSUAL_DEV(  0x0525, 0xa140, 0x0100, 0x0100,
		"Iomega",
		"USB Clik! 40",
		USB_SC_8070, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/* Added by Alan Stern <stern@rowland.harvard.edu> */
COMPLIANT_DEV(0x0525, 0xa4a5, 0x0000, 0x9999,
		"Linux",
		"File-backed Storage Gadget",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_CAPACITY_OK ),

/*
 * Yakumo Mega Image 37
 * Submitted by Stephan Fuhrmann <atomenergie@t-online.de> */
UNUSUAL_DEV(  0x052b, 0x1801, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Another Yakumo camera.
 * Reported by Michele Alzetta <michele.alzetta@aliceposta.it>
 */
UNUSUAL_DEV(  0x052b, 0x1804, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Iacopo Spalletti <avvisi@spalletti.it> */
UNUSUAL_DEV(  0x052b, 0x1807, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Yakumo Mega Image 47
 * Reported by Bjoern Paetzel <kolrabi@kolrabi.de>
 */
UNUSUAL_DEV(  0x052b, 0x1905, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"400_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Paul Ortyl <ortylp@3miasto.net>
 * Note that it's similar to the device above, only different prodID
 */
UNUSUAL_DEV(  0x052b, 0x1911, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"400_CAMERA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

UNUSUAL_DEV(  0x054c, 0x0010, 0x0106, 0x0450,
		"Sony",
		"DSC-S30/S70/S75/505V/F505/F707/F717/P8",
		USB_SC_SCSI, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN | US_FL_NOT_LOCKABLE | US_FL_NO_WP_DETECT ),

/*
 * Submitted by Lars Jacob <jacob.lars@googlemail.com>
 * This entry is needed because the device reports Sub=ff
 */
UNUSUAL_DEV(  0x054c, 0x0010, 0x0500, 0x0610,
		"Sony",
		"DSC-T1/T5/H5",
		USB_SC_8070, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),


/* Reported by wim@geeks.nl */
UNUSUAL_DEV(  0x054c, 0x0025, 0x0100, 0x0100,
		"Sony",
		"Memorystick NW-MS7",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Olaf Hering, <olh@suse.de> SuSE Bugzilla #49049 */
UNUSUAL_DEV(  0x054c, 0x002c, 0x0501, 0x2000,
		"Sony",
		"USB Floppy Drive",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

UNUSUAL_DEV(  0x054c, 0x002d, 0x0100, 0x0100,
		"Sony",
		"Memorystick MSAC-US1",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Klaus Mueller <k.mueller@intershop.de> */
UNUSUAL_DEV(  0x054c, 0x002e, 0x0106, 0x0310,
		"Sony",
		"Handycam",
		USB_SC_SCSI, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Rajesh Kumble Nayak <nayak@obs-nice.fr> */
UNUSUAL_DEV(  0x054c, 0x002e, 0x0500, 0x0500,
		"Sony",
		"Handycam HC-85",
		USB_SC_UFI, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

UNUSUAL_DEV(  0x054c, 0x0032, 0x0000, 0x9999,
		"Sony",
		"Memorystick MSC-U01N",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Michal Mlotek <mlotek@foobar.pl> */
UNUSUAL_DEV(  0x054c, 0x0058, 0x0000, 0x9999,
		"Sony",
		"PEG N760c Memorystick",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),
		
UNUSUAL_DEV(  0x054c, 0x0069, 0x0000, 0x9999,
		"Sony",
		"Memorystick MSC-U03",
		USB_SC_UFI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Nathan Babb <nathan@lexi.com> */
UNUSUAL_DEV(  0x054c, 0x006d, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/* Submitted by Frank Engel <frankie@cse.unsw.edu.au> */
UNUSUAL_DEV(  0x054c, 0x0099, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/* Submitted by Mike Alborn <malborn@deandra.homeip.net> */
UNUSUAL_DEV(  0x054c, 0x016a, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/* Submitted by Ren Bigcren <bigcren.ren@sonymobile.com> */
UNUSUAL_DEV(  0x054c, 0x02a5, 0x0100, 0x0100,
		"Sony Corp.",
		"MicroVault Flash Drive",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_READ_CAPACITY_16 ),

/* floppy reports multiple luns */
UNUSUAL_DEV(  0x055d, 0x2020, 0x0000, 0x0210,
		"SAMSUNG",
		"SFD-321U [FW 0C]",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* We keep this entry to force the transport; firmware 3.00 and later is ok. */
UNUSUAL_DEV(  0x057b, 0x0000, 0x0000, 0x0299,
		"Y-E Data",
		"Flashbuster-U",
		USB_SC_DEVICE,  USB_PR_CB, NULL,
		US_FL_SINGLE_LUN),

/*
 * Reported by Johann Cardon <johann.cardon@free.fr>
 * This entry is needed only because the device reports
 * bInterfaceClass = 0xff (vendor-specific)
 */
UNUSUAL_DEV(  0x057b, 0x0022, 0x0000, 0x9999,
		"Y-E Data",
		"Silicon Media R/W",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, 0),

/* Reported by RTE <raszilki@yandex.ru> */
UNUSUAL_DEV(  0x058f, 0x6387, 0x0141, 0x0141,
		"JetFlash",
		"TS1GJF2A/120",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Fabrizio Fellini <fello@libero.it> */
UNUSUAL_DEV(  0x0595, 0x4343, 0x0000, 0x2210,
		"Fujifilm",
		"Digital Camera EX-20 DSC",
		USB_SC_8070, USB_PR_DEVICE, NULL, 0 ),

/*
 * Reported by Andre Welter <a.r.welter@gmx.de>
 * This antique device predates the release of the Bulk-only Transport
 * spec, and if it gets a Get-Max-LUN then it requires the host to do a
 * Clear-Halt on the bulk endpoints.  The SINGLE_LUN flag will prevent
 * us from sending the request.
 */
UNUSUAL_DEV(  0x059b, 0x0001, 0x0100, 0x0100,
		"Iomega",
		"ZIP 100",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

UNUSUAL_DEV(  0x059b, 0x0040, 0x0100, 0x0100,
		"Iomega",
		"Jaz USB Adapter",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Reported by <Hendryk.Pfeiffer@gmx.de> */
UNUSUAL_DEV(  0x059f, 0x0643, 0x0000, 0x0000,
		"LaCie",
		"DVD+-RW",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_GO_SLOW ),

/* Reported by Christian Schaller <cschalle@redhat.com> */
UNUSUAL_DEV(  0x059f, 0x0651, 0x0000, 0x0000,
		"LaCie",
		"External HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT ),

/*
 * Submitted by Joel Bourquard <numlock@freesurf.ch>
 * Some versions of this device need the SubClass and Protocol overrides
 * while others don't.
 */
UNUSUAL_DEV(  0x05ab, 0x0060, 0x1104, 0x1110,
		"In-System",
		"PyroGate External CD-ROM Enclosure (FCD-523)",
		USB_SC_SCSI, USB_PR_BULK, NULL,
		US_FL_NEED_OVERRIDE ),

/*
 * Submitted by Sven Anderson <sven-linux@anderson.de>
 * There are at least four ProductIDs used for iPods, so I added 0x1202 and
 * 0x1204. They just need the US_FL_FIX_CAPACITY. As the bcdDevice appears
 * to change with firmware updates, I changed the range to maximum for all
 * iPod entries.
 */
UNUSUAL_DEV( 0x05ac, 0x1202, 0x0000, 0x9999,
		"Apple",
		"iPod",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/* Reported by Avi Kivity <avi@argo.co.il> */
UNUSUAL_DEV( 0x05ac, 0x1203, 0x0000, 0x9999,
		"Apple",
		"iPod",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

UNUSUAL_DEV( 0x05ac, 0x1204, 0x0000, 0x9999,
		"Apple",
		"iPod",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_NOT_LOCKABLE ),

UNUSUAL_DEV( 0x05ac, 0x1205, 0x0000, 0x9999,
		"Apple",
		"iPod",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/*
 * Reported by Tyson Vinson <lornoss@gmail.com>
 * This particular productId is the iPod Nano
 */
UNUSUAL_DEV( 0x05ac, 0x120a, 0x0000, 0x9999,
		"Apple",
		"iPod",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/*
 * Reported by Dan Williams <dcbw@redhat.com>
 * Option N.V. mobile broadband modems
 * Ignore driver CD mode and force into modem mode by default.
 */

/* Globetrotter HSDPA; mass storage shows up as Qualcomm for vendor */
UNUSUAL_DEV(  0x05c6, 0x1000, 0x0000, 0x9999,
		"Option N.V.",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, option_ms_init,
		0),

/* Reported by Blake Matheny <bmatheny@purdue.edu> */
UNUSUAL_DEV(  0x05dc, 0xb002, 0x0000, 0x0113,
		"Lexar",
		"USB CF Reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/*
 * The following two entries are for a Genesys USB to IDE
 * converter chip, but it changes its ProductId depending
 * on whether or not a disk or an optical device is enclosed
 * They were originally reported by Alexander Oltu
 * <alexander@all-2.com> and Peter Marks <peter.marks@turner.com>
 * respectively.
 *
 * US_FL_GO_SLOW and US_FL_MAX_SECTORS_64 added by Phil Dibowitz
 * <phil@ipom.com> as these flags were made and hard-coded
 * special-cases were pulled from scsiglue.c.
 */
UNUSUAL_DEV(  0x05e3, 0x0701, 0x0000, 0xffff,
		"Genesys Logic",
		"USB to IDE Optical",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_GO_SLOW | US_FL_MAX_SECTORS_64 | US_FL_IGNORE_RESIDUE ),

UNUSUAL_DEV(  0x05e3, 0x0702, 0x0000, 0xffff,
		"Genesys Logic",
		"USB to IDE Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_GO_SLOW | US_FL_MAX_SECTORS_64 | US_FL_IGNORE_RESIDUE ),

/* Reported by Ben Efros <ben@pc-doctor.com> */
UNUSUAL_DEV(  0x05e3, 0x0723, 0x9451, 0x9451,
		"Genesys Logic",
		"USB to SATA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SANE_SENSE ),

/*
 * Reported by Hanno Boeck <hanno@gmx.de>
 * Taken from the Lycoris Kernel
 */
UNUSUAL_DEV(  0x0636, 0x0003, 0x0000, 0x9999,
		"Vivitar",
		"Vivicam 35Xx",
		USB_SC_SCSI, USB_PR_BULK, NULL,
		US_FL_FIX_INQUIRY ),

UNUSUAL_DEV(  0x0644, 0x0000, 0x0100, 0x0100,
		"TEAC",
		"Floppy Drive",
		USB_SC_UFI, USB_PR_CB, NULL, 0 ),

/* Reported by Darsen Lu <darsen@micro.ee.nthu.edu.tw> */
UNUSUAL_DEV( 0x066f, 0x8000, 0x0001, 0x0001,
		"SigmaTel",
		"USBMSC Audio Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/* Reported by Daniel Kukula <daniel.kuku@gmail.com> */
UNUSUAL_DEV( 0x067b, 0x1063, 0x0100, 0x0100,
		"Prolific Technology, Inc.",
		"Prolific Storage Gadget",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BAD_SENSE ),

/* Reported by Rogerio Brito <rbrito@ime.usp.br> */
UNUSUAL_DEV( 0x067b, 0x2317, 0x0001, 0x001,
		"Prolific Technology, Inc.",
		"Mass Storage Device",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NOT_LOCKABLE ),

/* Reported by Richard -=[]=- <micro_flyer@hotmail.com> */
/*
 * Change to bcdDeviceMin (0x0100 to 0x0001) reported by
 * Thomas Bartosik <tbartdev@gmx-topmail.de>
 */
UNUSUAL_DEV( 0x067b, 0x2507, 0x0001, 0x0100,
		"Prolific Technology Inc.",
		"Mass Storage Device",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_GO_SLOW ),

/* Reported by Alex Butcher <alex.butcher@assursys.co.uk> */
UNUSUAL_DEV( 0x067b, 0x3507, 0x0001, 0x0101,
		"Prolific Technology Inc.",
		"ATAPI-6 Bridge Controller",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_GO_SLOW ),

/* Submitted by Benny Sjostrand <benny@hostmobility.com> */
UNUSUAL_DEV( 0x0686, 0x4011, 0x0001, 0x0001,
		"Minolta",
		"Dimage F300",
		USB_SC_SCSI, USB_PR_BULK, NULL, 0 ),

/* Reported by Miguel A. Fosas <amn3s1a@ono.com> */
UNUSUAL_DEV(  0x0686, 0x4017, 0x0001, 0x0001,
		"Minolta",
		"DIMAGE E223",
		USB_SC_SCSI, USB_PR_DEVICE, NULL, 0 ),

UNUSUAL_DEV(  0x0693, 0x0005, 0x0100, 0x0100,
		"Hagiwara",
		"Flashgate",
		USB_SC_SCSI, USB_PR_BULK, NULL, 0 ),

/* Reported by David Hamilton <niftimusmaximus@lycos.com> */
UNUSUAL_DEV(  0x069b, 0x3004, 0x0001, 0x0001,
		"Thomson Multimedia Inc.",
		"RCA RD1080 MP3 Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

UNUSUAL_DEV(  0x06ca, 0x2003, 0x0100, 0x0100,
		"Newer Technology",
		"uSCSI",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/* Reported by Adrian Pilchowiec <adi1981@epf.pl> */
UNUSUAL_DEV(  0x071b, 0x3203, 0x0000, 0x0000,
		"RockChip",
		"MP3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT | US_FL_MAX_SECTORS_64 |
		US_FL_NO_READ_CAPACITY_16),

/*
 * Reported by Jean-Baptiste Onofre <jb@nanthrax.net>
 * Support the following product :
 *    "Dane-Elec MediaTouch"
 */
UNUSUAL_DEV(  0x071b, 0x32bb, 0x0000, 0x0000,
		"RockChip",
		"MTP",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT | US_FL_MAX_SECTORS_64),

/*
 * Reported by Massimiliano Ghilardi <massimiliano.ghilardi@gmail.com>
 * This USB MP3/AVI player device fails and disconnects if more than 128
 * sectors (64kB) are read/written in a single command, and may be present
 * at least in the following products:
 *   "Magnex Digital Video Panel DVP 1800"
 *   "MP4 AIGO 4GB SLOT SD"
 *   "Teclast TL-C260 MP3"
 *   "i.Meizu PMP MP3/MP4"
 *   "Speed MV8 MP4 Audio Player"
 */
UNUSUAL_DEV(  0x071b, 0x3203, 0x0100, 0x0100,
		"RockChip",
		"ROCK MP3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64),

/* Reported by Olivier Blondeau <zeitoun@gmail.com> */
UNUSUAL_DEV(  0x0727, 0x0306, 0x0100, 0x0100,
		"ATMEL",
		"SND1 Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE),

/* Submitted by Roman Hodek <roman@hodek.net> */
UNUSUAL_DEV(  0x0781, 0x0001, 0x0200, 0x0200,
		"Sandisk",
		"ImageMate SDDR-05a",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN ),

UNUSUAL_DEV(  0x0781, 0x0002, 0x0009, 0x0009,
		"SanDisk Corporation",
		"ImageMate CompactFlash USB",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

UNUSUAL_DEV(  0x0781, 0x0100, 0x0100, 0x0100,
		"Sandisk",
		"ImageMate SDDR-12",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN ),

/* Reported by Eero Volotinen <eero@ping-viini.org> */
UNUSUAL_DEV(  0x07ab, 0xfccd, 0x0000, 0x9999,
		"Freecom Technologies",
		"FHD-Classic",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

UNUSUAL_DEV(  0x07af, 0x0004, 0x0100, 0x0133,
		"Microtech",
		"USB-SCSI-DB25",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ), 

UNUSUAL_DEV(  0x07af, 0x0005, 0x0100, 0x0100,
		"Microtech",
		"USB-SCSI-HD50",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

#ifdef NO_SDDR09
UNUSUAL_DEV(  0x07af, 0x0006, 0x0100, 0x0100,
		"Microtech",
		"CameraMate",
		USB_SC_SCSI, USB_PR_CB, NULL,
		US_FL_SINGLE_LUN ),
#endif

/*
 * Datafab KECF-USB / Sagatek DCS-CF / Simpletech Flashlink UCF-100
 * Only revision 1.13 tested (same for all of the above devices,
 * based on the Datafab DF-UG-07 chip).  Needed for US_FL_FIX_INQUIRY.
 * Submitted by Marek Michalkiewicz <marekm@amelek.gda.pl>.
 * See also http://martin.wilck.bei.t-online.de/#kecf .
 */
UNUSUAL_DEV(  0x07c4, 0xa400, 0x0000, 0xffff,
		"Datafab",
		"KECF-USB",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY | US_FL_FIX_CAPACITY ),

/*
 * Reported by Rauch Wolke <rauchwolke@gmx.net>
 * and augmented by binbin <binbinsh@gmail.com> (Bugzilla #12882)
 */
UNUSUAL_DEV(  0x07c4, 0xa4a5, 0x0000, 0xffff,
		"Simple Tech/Datafab",
		"CF+SM Reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_MAX_SECTORS_64 ),

/*
 * Casio QV 2x00/3x00/4000/8000 digital still cameras are not conformant
 * to the USB storage specification in two ways:
 * - They tell us they are using transport protocol CBI. In reality they
 *   are using transport protocol CB.
 * - They don't like the INQUIRY command. So we must handle this command
 *   of the SCSI layer ourselves.
 * - Some cameras with idProduct=0x1001 and bcdDevice=0x1000 have
 *   bInterfaceProtocol=0x00 (USB_PR_CBI) while others have 0x01 (USB_PR_CB).
 *   So don't remove the USB_PR_CB override!
 * - Cameras with bcdDevice=0x9009 require the USB_SC_8070 override.
 */
UNUSUAL_DEV( 0x07cf, 0x1001, 0x1000, 0x9999,
		"Casio",
		"QV DigitalCamera",
		USB_SC_8070, USB_PR_CB, NULL,
		US_FL_NEED_OVERRIDE | US_FL_FIX_INQUIRY ),

/* Submitted by Oleksandr Chumachenko <ledest@gmail.com> */
UNUSUAL_DEV( 0x07cf, 0x1167, 0x0100, 0x0100,
		"Casio",
		"EX-N1 DigitalCamera",
		USB_SC_8070, USB_PR_DEVICE, NULL, 0),

/* Submitted by Hartmut Wahl <hwahl@hwahl.de>*/
UNUSUAL_DEV( 0x0839, 0x000a, 0x0001, 0x0001,
		"Samsung",
		"Digimax 410",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY),

/* Reported by Luciano Rocha <luciano@eurotux.com> */
UNUSUAL_DEV( 0x0840, 0x0082, 0x0001, 0x0001,
		"Argosy",
		"Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/* Reported and patched by Nguyen Anh Quynh <aquynh@gmail.com> */
UNUSUAL_DEV( 0x0840, 0x0084, 0x0001, 0x0001,
		"Argosy",
		"Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/* Reported by Martijn Hijdra <martijn.hijdra@gmail.com> */
UNUSUAL_DEV( 0x0840, 0x0085, 0x0001, 0x0001,
		"Argosy",
		"Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/* Supplied with some Castlewood ORB removable drives */
UNUSUAL_DEV(  0x084b, 0xa001, 0x0000, 0x9999,
		"Castlewood Systems",
		"USB to SCSI cable",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Entry and supporting patch by Theodore Kilgore <kilgota@auburn.edu>.
 * Flag will support Bulk devices which use a standards-violating 32-byte
 * Command Block Wrapper. Here, the "DC2MEGA" cameras (several brands) with
 * Grandtech GT892x chip, which request "Proprietary SCSI Bulk" support.
 */

UNUSUAL_DEV(  0x084d, 0x0011, 0x0110, 0x0110,
		"Grandtech",
		"DC2MEGA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BULK32),

/*
 * Reported by <ttkspam@free.fr>
 * The device reports a vendor-specific device class, requiring an
 * explicit vendor/product match.
 */
UNUSUAL_DEV(  0x0851, 0x1542, 0x0002, 0x0002,
		"MagicPixel",
		"FW_Omega2",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, 0),

/*
 * Andrew Lunn <andrew@lunn.ch>
 * PanDigital Digital Picture Frame. Does not like ALLOW_MEDIUM_REMOVAL
 * on LUN 4.
 * Note: Vend:Prod clash with "Ltd Maxell WS30 Slim Digital Camera"
 */
UNUSUAL_DEV(  0x0851, 0x1543, 0x0200, 0x0200,
		"PanDigital",
		"Photo Frame",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NOT_LOCKABLE),

UNUSUAL_DEV(  0x085a, 0x0026, 0x0100, 0x0133,
		"Xircom",
		"PortGear USB-SCSI (Mac USB Dock)",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

UNUSUAL_DEV(  0x085a, 0x0028, 0x0100, 0x0133,
		"Xircom",
		"PortGear USB to SCSI Converter",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/* Submitted by Jan De Luyck <lkml@kcore.org> */
UNUSUAL_DEV(  0x08bd, 0x1100, 0x0000, 0x0000,
		"CITIZEN",
		"X1DE-USB",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN),

/*
 * Submitted by Dylan Taft <d13f00l@gmail.com>
 * US_FL_IGNORE_RESIDUE Needed
 */
UNUSUAL_DEV(  0x08ca, 0x3103, 0x0100, 0x0100,
		"AIPTEK",
		"Aiptek USB Keychain MP3 Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE),

/*
 * Entry needed for flags. Moreover, all devices with this ID use
 * bulk-only transport, but _some_ falsely report Control/Bulk instead.
 * One example is "Trumpion Digital Research MYMP3".
 * Submitted by Bjoern Brill <brill(at)fs.math.uni-frankfurt.de>
 */
UNUSUAL_DEV(  0x090a, 0x1001, 0x0100, 0x0100,
		"Trumpion",
		"t33520 USB Flash Card Controller",
		USB_SC_DEVICE, USB_PR_BULK, NULL,
		US_FL_NEED_OVERRIDE ),

/*
 * Reported by Filippo Bardelli <filibard@libero.it>
 * The device reports a subclass of RBC, which is wrong.
 */
UNUSUAL_DEV(  0x090a, 0x1050, 0x0100, 0x0100,
		"Trumpion Microelectronics, Inc.",
		"33520 USB Digital Voice Recorder",
		USB_SC_UFI, USB_PR_DEVICE, NULL,
		0),

/* Trumpion Microelectronics MP3 player (felipe_alfaro@linuxmail.org) */
UNUSUAL_DEV( 0x090a, 0x1200, 0x0000, 0x9999,
		"Trumpion",
		"MP3 player",
		USB_SC_RBC, USB_PR_BULK, NULL,
		0 ),

UNUSUAL_DEV(0x090c, 0x1000, 0x1100, 0x1100,
		"Samsung",
		"Flash Drive FIT",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64),

/* aeb */
UNUSUAL_DEV( 0x090c, 0x1132, 0x0000, 0xffff,
		"Feiya",
		"5-in-1 Card Reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/*
 * Reported by Icenowy Zheng <icenowy@aosc.io>
 * The SMI SM3350 USB-UFS bridge controller will enter a wrong state
 * that do not process read/write command if a long sense is requested,
 * so force to use 18-byte sense.
 */
UNUSUAL_DEV(  0x090c, 0x3350, 0x0000, 0xffff,
		"SMI",
		"SM3350 UFS-to-USB-Mass-Storage bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BAD_SENSE ),

/*
 * Reported by Paul Hartman <paul.hartman+linux@gmail.com>
 * This card reader returns "Illegal Request, Logical Block Address
 * Out of Range" for the first READ(10) after a new card is inserted.
 */
UNUSUAL_DEV(  0x090c, 0x6000, 0x0100, 0x0100,
		"Feiya",
		"SD/SDHC Card Reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_INITIAL_READ10 ),

/*
 * This Pentax still camera is not conformant
 * to the USB storage specification: -
 * - It does not like the INQUIRY command. So we must handle this command
 *   of the SCSI layer ourselves.
 * Tested on Rev. 10.00 (0x1000)
 * Submitted by James Courtier-Dutton <James@superbug.demon.co.uk>
 */
UNUSUAL_DEV( 0x0a17, 0x0004, 0x1000, 0x1000,
		"Pentax",
		"Optio 2/3/400",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/*
 * These are virtual windows driver CDs, which the zd1211rw driver
 * automatically converts into WLAN devices.
 */
UNUSUAL_DEV( 0x0ace, 0x2011, 0x0101, 0x0101,
		"ZyXEL",
		"G-220F USB-WLAN Install",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_DEVICE ),

UNUSUAL_DEV( 0x0ace, 0x20ff, 0x0101, 0x0101,
		"SiteCom",
		"WL-117 USB-WLAN Install",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_DEVICE ),

/*
 * Reported by Dan Williams <dcbw@redhat.com>
 * Option N.V. mobile broadband modems
 * Ignore driver CD mode and force into modem mode by default.
 */

/* iCON 225 */
UNUSUAL_DEV(  0x0af0, 0x6971, 0x0000, 0x9999,
		"Option N.V.",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, option_ms_init,
		0),

/*
 * Reported by F. Aben <f.aben@option.com>
 * This device (wrongly) has a vendor-specific device descriptor.
 * The entry is needed so usb-storage can bind to it's mass-storage
 * interface as an interface driver
 */
UNUSUAL_DEV( 0x0af0, 0x7401, 0x0000, 0x0000,
		"Option",
		"GI 0401 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

/*
 * Reported by Jan Dumon <j.dumon@option.com>
 * These devices (wrongly) have a vendor-specific device descriptor.
 * These entries are needed so usb-storage can bind to their mass-storage
 * interface as an interface driver
 */
UNUSUAL_DEV( 0x0af0, 0x7501, 0x0000, 0x0000,
		"Option",
		"GI 0431 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x7701, 0x0000, 0x0000,
		"Option",
		"GI 0451 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x7706, 0x0000, 0x0000,
		"Option",
		"GI 0451 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x7901, 0x0000, 0x0000,
		"Option",
		"GI 0452 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x7A01, 0x0000, 0x0000,
		"Option",
		"GI 0461 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x7A05, 0x0000, 0x0000,
		"Option",
		"GI 0461 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x8300, 0x0000, 0x0000,
		"Option",
		"GI 033x SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x8302, 0x0000, 0x0000,
		"Option",
		"GI 033x SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0x8304, 0x0000, 0x0000,
		"Option",
		"GI 033x SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xc100, 0x0000, 0x0000,
		"Option",
		"GI 070x SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xd057, 0x0000, 0x0000,
		"Option",
		"GI 1505 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xd058, 0x0000, 0x0000,
		"Option",
		"GI 1509 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xd157, 0x0000, 0x0000,
		"Option",
		"GI 1515 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xd257, 0x0000, 0x0000,
		"Option",
		"GI 1215 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

UNUSUAL_DEV( 0x0af0, 0xd357, 0x0000, 0x0000,
		"Option",
		"GI 1505 SD-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

/* Reported by Namjae Jeon <namjae.jeon@samsung.com> */
UNUSUAL_DEV(0x0bc2, 0x2300, 0x0000, 0x9999,
		"Seagate",
		"Portable HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_WRITE_CACHE),

/* Reported by Ben Efros <ben@pc-doctor.com> */
UNUSUAL_DEV( 0x0bc2, 0x3010, 0x0000, 0x0000,
		"Seagate",
		"FreeAgent Pro",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SANE_SENSE ),

/* Reported by Kris Lindgren <kris.lindgren@gmail.com> */
UNUSUAL_DEV( 0x0bc2, 0x3332, 0x0000, 0x9999,
		"Seagate",
		"External",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT ),

UNUSUAL_DEV(  0x0d49, 0x7310, 0x0000, 0x9999,
		"Maxtor",
		"USB to SATA",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SANE_SENSE),

/*
 * Pete Zaitcev <zaitcev@yahoo.com>, bz#164688.
 * The device blatantly ignores LUN and returns 1 in GetMaxLUN.
 */
UNUSUAL_DEV( 0x0c45, 0x1060, 0x0100, 0x0100,
		"Unknown",
		"Unknown",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/* Submitted by Joris Struyve <joris@struyve.be> */
UNUSUAL_DEV( 0x0d96, 0x410a, 0x0001, 0xffff,
		"Medion",
		"MD 7425",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY),

/*
 * Entry for Jenoptik JD 5200z3
 *
 * email: car.busse@gmx.de
 */
UNUSUAL_DEV(  0x0d96, 0x5200, 0x0001, 0x0200,
		"Jenoptik",
		"JD 5200 z3",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_FIX_INQUIRY),

/* Reported by  Jason Johnston <killean@shaw.ca> */
UNUSUAL_DEV(  0x0dc4, 0x0073, 0x0000, 0x0000,
		"Macpower Technology Co.LTD.",
		"USB 2.0 3.5\" DEVICE",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/*
 * Reported by Lubomir Blaha <tritol@trilogic.cz>
 * I _REALLY_ don't know what 3rd, 4th number and all defines mean, but this
 * works for me. Can anybody correct these values? (I able to test corrected
 * version.)
 */
UNUSUAL_DEV( 0x0dd8, 0x1060, 0x0000, 0xffff,
		"Netac",
		"USB-CF-Card",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/*
 * Reported by Edward Chapman (taken from linux-usb mailing list)
 * Netac OnlyDisk Mini U2CV2 512MB USB 2.0 Flash Drive
 */
UNUSUAL_DEV( 0x0dd8, 0xd202, 0x0000, 0x9999,
		"Netac",
		"USB Flash Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),


/*
 * Patch by Stephan Walter <stephan.walter@epfl.ch>
 * I don't know why, but it works...
 */
UNUSUAL_DEV( 0x0dda, 0x0001, 0x0012, 0x0012,
		"WINWARD",
		"Music Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Ian McConnell <ian at emit.demon.co.uk> */
UNUSUAL_DEV(  0x0dda, 0x0301, 0x0012, 0x0012,
		"PNP_MP3",
		"PNP_MP3 PLAYER",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Jim McCloskey <mcclosk@ucsc.edu> */
UNUSUAL_DEV( 0x0e21, 0x0520, 0x0100, 0x0100,
		"Cowon Systems",
		"iAUDIO M5",
		USB_SC_DEVICE, USB_PR_BULK, NULL,
		US_FL_NEED_OVERRIDE ),

/* Submitted by Antoine Mairesse <antoine.mairesse@free.fr> */
UNUSUAL_DEV( 0x0ed1, 0x6660, 0x0100, 0x0300,
		"USB",
		"Solid state disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY ),

/*
 * Submitted by Daniel Drake <dsd@gentoo.org>
 * Reported by dayul on the Gentoo Forums
 */
UNUSUAL_DEV(  0x0ea0, 0x2168, 0x0110, 0x0110,
		"Ours Technology",
		"Flash Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Rastislav Stanik <rs_kernel@yahoo.com> */
UNUSUAL_DEV(  0x0ea0, 0x6828, 0x0110, 0x0110,
		"USB",
		"Flash Disk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Benjamin Schiller <sbenni@gmx.de>
 * It is also sold by Easylite as DJ 20
 */
UNUSUAL_DEV(  0x0ed1, 0x7636, 0x0103, 0x0103,
		"Typhoon",
		"My DJ 1820",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_GO_SLOW | US_FL_MAX_SECTORS_64),

/*
 * Patch by Leonid Petrov mail at lpetrov.net
 * Reported by Robert Spitzenpfeil <robert@spitzenpfeil.org>
 * http://www.qbik.ch/usb/devices/showdev.php?id=1705
 * Updated to 103 device by MJ Ray mjr at phonecoop.coop
 */
UNUSUAL_DEV(  0x0f19, 0x0103, 0x0100, 0x0100,
		"Oracom Co., Ltd",
		"ORC-200M",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * David Kuehling <dvdkhlng@gmx.de>:
 * for MP3-Player AVOX WSX-300ER (bought in Japan).  Reports lots of SCSI
 * errors when trying to write.
 */
UNUSUAL_DEV(  0x0f19, 0x0105, 0x0100, 0x0100,
		"C-MEX",
		"A-VOX",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Submitted by Nick Holloway */
UNUSUAL_DEV( 0x0f88, 0x042e, 0x0100, 0x0100,
		"VTech",
		"Kidizoom",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY ),

/* Reported by Moritz Moeller-Herrmann <moritz-kernel@moeller-herrmann.de> */
UNUSUAL_DEV(  0x0fca, 0x8004, 0x0201, 0x0201,
		"Research In Motion",
		"BlackBerry Bold 9000",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Michael Stattmann <michael@stattmann.com> */
UNUSUAL_DEV(  0x0fce, 0xd008, 0x0000, 0x0000,
		"Sony Ericsson",
		"V800-Vodafone 802",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_WP_DETECT ),

/* Reported by The Solutor <thesolutor@gmail.com> */
UNUSUAL_DEV(  0x0fce, 0xd0e1, 0x0000, 0x0000,
		"Sony Ericsson",
		"MD400",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_DEVICE),

/*
 * Reported by Jan Mate <mate@fiit.stuba.sk>
 * and by Soeren Sonnenburg <kernel@nn7.de>
 */
UNUSUAL_DEV(  0x0fce, 0xe030, 0x0000, 0x0000,
		"Sony Ericsson",
		"P990i",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_IGNORE_RESIDUE ),

/* Reported by Emmanuel Vasilakis <evas@forthnet.gr> */
UNUSUAL_DEV(  0x0fce, 0xe031, 0x0000, 0x0000,
		"Sony Ericsson",
		"M600i",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_FIX_CAPACITY ),

/* Reported by Ricardo Barberis <ricardo@dattatec.com> */
UNUSUAL_DEV(  0x0fce, 0xe092, 0x0000, 0x0000,
		"Sony Ericsson",
		"P1i",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Kevin Cernekee <kpc-usbdev@gelato.uiuc.edu>
 * Tested on hardware version 1.10.
 * Entry is needed only for the initializer function override.
 * Devices with bcd > 110 seem to not need it while those
 * with bcd < 110 appear to need it.
 */
UNUSUAL_DEV(  0x1019, 0x0c55, 0x0000, 0x0110,
		"Desknote",
		"UCR-61S2B",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_ucr61s2b_init,
		0 ),

UNUSUAL_DEV(  0x1058, 0x0704, 0x0000, 0x9999,
		"Western Digital",
		"External HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SANE_SENSE),

/* Reported by Namjae Jeon <namjae.jeon@samsung.com> */
UNUSUAL_DEV(0x1058, 0x070a, 0x0000, 0x9999,
		"Western Digital",
		"My Passport HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_WRITE_CACHE),

/*
 * Reported by Fabio Venturi <f.venturi@tdnet.it>
 * The device reports a vendor-specific bDeviceClass.
 */
UNUSUAL_DEV(  0x10d6, 0x2200, 0x0100, 0x0100,
		"Actions Semiconductor",
		"Mtp device",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0),

/*
 * Reported by Pascal Terjan <pterjan@mandriva.com>
 * Ignore driver CD mode and force into modem mode by default.
 */
UNUSUAL_DEV(  0x1186, 0x3e04, 0x0000, 0x0000,
           "D-Link",
           "USB Mass Storage",
           USB_SC_DEVICE, USB_PR_DEVICE, option_ms_init, US_FL_IGNORE_DEVICE),

/*
 * Reported by Kevin Lloyd <linux@sierrawireless.com>
 * Entry is needed for the initializer function override,
 * which instructs the device to load as a modem
 * device.
 */
UNUSUAL_DEV(  0x1199, 0x0fff, 0x0000, 0x9999,
		"Sierra Wireless",
		"USB MMC Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, sierra_ms_init,
		0),

/*
 * Reported by Jaco Kroon <jaco@kroon.co.za>
 * The usb-storage module found on the Digitech GNX4 (and supposedly other
 * devices) misbehaves and causes a bunch of invalid I/O errors.
 */
UNUSUAL_DEV(  0x1210, 0x0003, 0x0100, 0x0100,
		"Digitech HMG",
		"DigiTech Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by fangxiaozhi <huananhu@huawei.com>
 * This brings the HUAWEI data card devices into multi-port mode
 */
UNUSUAL_DEV(  0x12d1, 0x1001, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1003, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1004, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1401, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1402, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1403, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1404, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1405, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1406, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1407, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1408, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1409, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140A, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140B, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140C, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140D, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140E, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x140F, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1410, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1411, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1412, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1413, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1414, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1415, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1416, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1417, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1418, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1419, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141A, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141B, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141C, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141D, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141E, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x141F, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1420, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1421, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1422, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1423, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1424, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1425, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1426, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1427, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1428, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1429, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142A, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142B, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142C, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142D, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142E, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x142F, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1430, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1431, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1432, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1433, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1434, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1435, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1436, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1437, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1438, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x1439, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143A, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143B, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143C, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143D, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143E, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),
UNUSUAL_DEV(  0x12d1, 0x143F, 0x0000, 0x0000,
		"HUAWEI MOBILE",
		"Mass Storage",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_huawei_e220_init,
		0),

/* Reported by Vilius Bilinkevicius <vilisas AT xxx DOT lt) */
UNUSUAL_DEV(  0x132b, 0x000b, 0x0001, 0x0001,
		"Minolta",
		"Dimage Z10",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		0 ),

/* Reported by Kotrla Vitezslav <kotrla@ceb.cz> */
UNUSUAL_DEV(  0x1370, 0x6828, 0x0110, 0x0110,
		"SWISSBIT",
		"Black Silver",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/*
 * Reported by Tobias Jakobi <tjakobi@math.uni-bielefeld.de>
 * The INIC-3619 bridge is used in the StarTech SLSODDU33B
 * SATA-USB enclosure for slimline optical drives.
 *
 * The quirk enables MakeMKV to properly exchange keys with
 * an installed BD drive.
 */
UNUSUAL_DEV(  0x13fd, 0x3609, 0x0209, 0x0209,
		"Initio Corporation",
		"INIC-3619",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Qinglin Ye <yestyle@gmail.com> */
UNUSUAL_DEV(  0x13fe, 0x3600, 0x0100, 0x0100,
		"Kingston",
		"DT 101 G2",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BULK_IGNORE_TAG ),

/* Reported by Francesco Foresti <frafore@tiscali.it> */
UNUSUAL_DEV(  0x14cd, 0x6600, 0x0201, 0x0201,
		"Super Top",
		"IDE DEVICE",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Michael Büsch <m@bues.ch> */
UNUSUAL_DEV(  0x152d, 0x0567, 0x0114, 0x0117,
		"JMicron",
		"USB to ATA/ATAPI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BROKEN_FUA ),

/* Reported by David Kozub <zub@linux.fjfi.cvut.cz> */
UNUSUAL_DEV(0x152d, 0x0578, 0x0000, 0x9999,
		"JMicron",
		"JMS567",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BROKEN_FUA),

/*
 * Reported by Alexandre Oliva <oliva@lsd.ic.unicamp.br>
 * JMicron responds to USN and several other SCSI ioctls with a
 * residue that causes subsequent I/O requests to fail.  */
UNUSUAL_DEV(  0x152d, 0x2329, 0x0100, 0x0100,
		"JMicron",
		"USB to ATA/ATAPI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_SANE_SENSE ),

/* Reported by Dmitry Nezhevenko <dion@dion.org.ua> */
UNUSUAL_DEV(  0x152d, 0x2566, 0x0114, 0x0114,
		"JMicron",
		"USB to ATA/ATAPI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BROKEN_FUA ),

/* Reported by Teijo Kinnunen <teijo.kinnunen@code-q.fi> */
UNUSUAL_DEV(  0x152d, 0x2567, 0x0117, 0x0117,
		"JMicron",
		"USB to ATA/ATAPI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BROKEN_FUA ),

/* Reported-by George Cherian <george.cherian@cavium.com> */
UNUSUAL_DEV(0x152d, 0x9561, 0x0000, 0x9999,
		"JMicron",
		"JMS56x",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_REPORT_OPCODES),

/*
 * Entrega Technologies U1-SC25 (later Xircom PortGear PGSCSI)
 * and Mac USB Dock USB-SCSI */
UNUSUAL_DEV(  0x1645, 0x0007, 0x0100, 0x0133,
		"Entrega Technologies",
		"USB to SCSI Converter",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Reported by Robert Schedel <r.schedel@yahoo.de>
 * Note: this is a 'super top' device like the above 14cd/6600 device
 */
UNUSUAL_DEV(  0x1652, 0x6600, 0x0201, 0x0201,
		"Teac",
		"HD-35PUK-B",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Oliver Neukum <oneukum@suse.com> */
UNUSUAL_DEV(  0x174c, 0x55aa, 0x0100, 0x0100,
		"ASMedia",
		"AS2105",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NEEDS_CAP16),

/* Reported by Jesse Feddema <jdfeddema@gmail.com> */
UNUSUAL_DEV(  0x177f, 0x0400, 0x0000, 0x0000,
		"Yarvik",
		"PMP400",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BULK_IGNORE_TAG | US_FL_MAX_SECTORS_64 ),

UNUSUAL_DEV(  0x1822, 0x0001, 0x0000, 0x9999,
		"Ariston Technologies",
		"iConnect USB to SCSI adapter",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Reported by Hans de Goede <hdegoede@redhat.com>
 * These Appotech controllers are found in Picture Frames, they provide a
 * (buggy) emulation of a cdrom drive which contains the windows software
 * Uploading of pictures happens over the corresponding /dev/sg device.
 */
UNUSUAL_DEV( 0x1908, 0x1315, 0x0000, 0x0000,
		"BUILDWIN",
		"Photo Frame",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BAD_SENSE ),
UNUSUAL_DEV( 0x1908, 0x1320, 0x0000, 0x0000,
		"BUILDWIN",
		"Photo Frame",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BAD_SENSE ),
UNUSUAL_DEV( 0x1908, 0x3335, 0x0200, 0x0200,
		"BUILDWIN",
		"Photo Frame",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_READ_DISC_INFO ),

/*
 * Reported by Matthias Schwarzott <zzam@gentoo.org>
 * The Amazon Kindle treats SYNCHRONIZE CACHE as an indication that
 * the host may be finished with it, and automatically ejects its
 * emulated media unless it receives another command within one second.
 */
UNUSUAL_DEV( 0x1949, 0x0004, 0x0000, 0x9999,
		"Amazon",
		"Kindle",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SENSE_AFTER_SYNC ),

/*
 * Reported by Oliver Neukum <oneukum@suse.com>
 * This device morphes spontaneously into another device if the access
 * pattern of Windows isn't followed. Thus writable media would be dirty
 * if the initial instance is used. So the device is limited to its
 * virtual CD.
 * And yes, the concept that BCD goes up to 9 is not heeded
 */
UNUSUAL_DEV( 0x19d2, 0x1225, 0x0000, 0xffff,
		"ZTE,Incorporated",
		"ZTE WCDMA Technologies MSM",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_SINGLE_LUN ),

/*
 * Reported by Sven Geggus <sven-usbst@geggus.net>
 * This encrypted pen drive returns bogus data for the initial READ(10).
 */
UNUSUAL_DEV(  0x1b1c, 0x1ab5, 0x0200, 0x0200,
		"Corsair",
		"Padlock v2",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_INITIAL_READ10 ),

/*
 * Reported by Hans de Goede <hdegoede@redhat.com>
 * These are mini projectors using USB for both power and video data transport
 * The usb-storage interface is a virtual windows driver CD, which the gm12u320
 * driver automatically converts into framebuffer & kms dri device nodes.
 */
UNUSUAL_DEV( 0x1de1, 0xc102, 0x0000, 0xffff,
		"Grain-media Technology Corp.",
		"USB3.0 Device GM12U320",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_DEVICE ),

/*
 * Patch by Richard Schütz <r.schtz@t-online.de>
 * This external hard drive enclosure uses a JMicron chip which
 * needs the US_FL_IGNORE_RESIDUE flag to work properly.
 */
UNUSUAL_DEV(  0x1e68, 0x001b, 0x0000, 0x0000,
		"TrekStor GmbH & Co. KG",
		"DataStation maxi g.u",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE | US_FL_SANE_SENSE ),

/* Reported by Jasper Mackenzie <scarletpimpernal@hotmail.com> */
UNUSUAL_DEV( 0x1e74, 0x4621, 0x0000, 0x0000,
		"Coby Electronics",
		"MP3 Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BULK_IGNORE_TAG | US_FL_MAX_SECTORS_64 ),

/* Reported by Witold Lipieta <witold.lipieta@thaumatec.com> */
UNUSUAL_DEV( 0x1fc9, 0x0117, 0x0100, 0x0100,
		"NXP Semiconductors",
		"PN7462AU",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Supplied with some Castlewood ORB removable drives */
UNUSUAL_DEV(  0x2027, 0xa001, 0x0000, 0x9999,
		"Double-H Technology",
		"USB to SCSI Intelligent Cable",
		USB_SC_DEVICE, USB_PR_DEVICE, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ),

/*
 * Reported by DocMAX <mail@vacharakis.de>
 * and Thomas Weißschuh <linux@weissschuh.net>
 */
UNUSUAL_DEV( 0x2109, 0x0715, 0x9999, 0x9999,
		"VIA Labs, Inc.",
		"VL817 SATA Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_UAS),

UNUSUAL_DEV( 0x2116, 0x0320, 0x0001, 0x0001,
		"ST",
		"2A",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY),

/*
 * patch submitted by Davide Perini <perini.davide@dpsoftware.org>
 * and Renato Perini <rperini@email.it>
 */
UNUSUAL_DEV(  0x22b8, 0x3010, 0x0001, 0x0001,
		"Motorola",
		"RAZR V3x",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_CAPACITY | US_FL_IGNORE_RESIDUE ),

/*
 * Patch by Constantin Baranov <const@tltsu.ru>
 * Report by Andreas Koenecke.
 * Motorola ROKR Z6.
 */
UNUSUAL_DEV(  0x22b8, 0x6426, 0x0101, 0x0101,
		"Motorola",
		"MSnc.",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_FIX_INQUIRY | US_FL_FIX_CAPACITY | US_FL_BULK_IGNORE_TAG),

/* Reported by Radovan Garabik <garabik@kassiopeia.juls.savba.sk> */
UNUSUAL_DEV(  0x2735, 0x100b, 0x0000, 0x9999,
		"MPIO",
		"HS200",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_GO_SLOW ),

/* Reported-by: Tim Anderson <tsa@biglakesoftware.com> */
UNUSUAL_DEV(  0x2ca3, 0x0031, 0x0000, 0x9999,
		"DJI",
		"CineSSD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_ATA_1X),

/*
 * Reported by Frederic Marchal <frederic.marchal@wowcompany.com>
 * Mio Moov 330
 */
UNUSUAL_DEV(  0x3340, 0xffff, 0x0000, 0x0000,
		"Mitac",
		"Mio DigiWalker USB Sync",
		USB_SC_DEVICE,USB_PR_DEVICE,NULL,
		US_FL_MAX_SECTORS_64 ),

/* Reported by Cyril Roelandt <tipecaml@gmail.com> */
UNUSUAL_DEV(  0x357d, 0x7788, 0x0114, 0x0114,
		"JMicron",
		"USB to ATA/ATAPI Bridge",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_BROKEN_FUA | US_FL_IGNORE_UAS ),

/* Reported by Andrey Rahmatullin <wrar@altlinux.org> */
UNUSUAL_DEV(  0x4102, 0x1020, 0x0100,  0x0100,
		"iRiver",
		"MP3 T10",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_IGNORE_RESIDUE ),

/* Reported by Sergey Pinaev <dfo@antex.ru> */
UNUSUAL_DEV(  0x4102, 0x1059, 0x0000,  0x0000,
               "iRiver",
               "P7K",
               USB_SC_DEVICE, USB_PR_DEVICE, NULL,
               US_FL_MAX_SECTORS_64 ),

/*
 * David Härdeman <david@2gen.com>
 * The key makes the SCSI stack print confusing (but harmless) messages
 */
UNUSUAL_DEV(  0x4146, 0xba01, 0x0100, 0x0100,
		"Iomega",
		"Micro Mini 1GB",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_NOT_LOCKABLE ),

/* "G-DRIVE" external HDD hangs on write without these.
 * Patch submitted by Alexander Kappner <agk@godking.net>
 */
UNUSUAL_DEV(0x4971, 0x8024, 0x0000, 0x9999,
		"SimpleTech",
		"External HDD",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_ALWAYS_SYNC),

/*
 * Nick Bowler <nbowler@elliptictech.com>
 * SCSI stack spams (otherwise harmless) error messages.
 */
UNUSUAL_DEV(  0xc251, 0x4003, 0x0100, 0x0100,
		"Keil Software, Inc.",
		"V2M MotherBoard",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NOT_LOCKABLE),

/* Reported by Andrew Simmons <andrew.simmons@gmail.com> */
UNUSUAL_DEV(  0xed06, 0x4500, 0x0001, 0x0001,
		"DataStor",
		"USB4500 FW1.04",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_CAPACITY_HEURISTICS),

/* Reported by Alessio Treglia <quadrispro@ubuntu.com> */
UNUSUAL_DEV( 0xed10, 0x7636, 0x0001, 0x0001,
		"TGE",
		"Digital MP3 Audio Player",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, US_FL_NOT_LOCKABLE ),

/* Unusual uas devices */
#if IS_ENABLED(CONFIG_USB_UAS)
#include "unusual_uas.h"
#endif

/* Control/Bulk transport for all SubClass values */
USUAL_DEV(USB_SC_RBC, USB_PR_CB),
USUAL_DEV(USB_SC_8020, USB_PR_CB),
USUAL_DEV(USB_SC_QIC, USB_PR_CB),
USUAL_DEV(USB_SC_UFI, USB_PR_CB),
USUAL_DEV(USB_SC_8070, USB_PR_CB),
USUAL_DEV(USB_SC_SCSI, USB_PR_CB),

/* Control/Bulk/Interrupt transport for all SubClass values */
USUAL_DEV(USB_SC_RBC, USB_PR_CBI),
USUAL_DEV(USB_SC_8020, USB_PR_CBI),
USUAL_DEV(USB_SC_QIC, USB_PR_CBI),
USUAL_DEV(USB_SC_UFI, USB_PR_CBI),
USUAL_DEV(USB_SC_8070, USB_PR_CBI),
USUAL_DEV(USB_SC_SCSI, USB_PR_CBI),

/* Bulk-only transport for all SubClass values */
USUAL_DEV(USB_SC_RBC, USB_PR_BULK),
USUAL_DEV(USB_SC_8020, USB_PR_BULK),
USUAL_DEV(USB_SC_QIC, USB_PR_BULK),
USUAL_DEV(USB_SC_UFI, USB_PR_BULK),
USUAL_DEV(USB_SC_8070, USB_PR_BULK),
USUAL_DEV(USB_SC_SCSI, USB_PR_BULK),
