/*
 * Driver for USB Windows Media Center Ed. eHome Infrared Transceivers
 *
 * Copyright (c) 2010-2011, Jarod Wilson <jarod@redhat.com>
 *
 * Based on the original lirc_mceusb and lirc_mceusb2 drivers, by Dan
 * Conti, Martin Blatter and Daniel Melander, the latter of which was
 * in turn also based on the lirc_atiusb driver by Paul Miller. The
 * two mce drivers were merged into one by Jarod Wilson, with transmit
 * support for the 1st-gen device added primarily by Patrick Calhoun,
 * with a bit of tweaks by Jarod. Debugging improvements and proper
 * support for what appears to be 3rd-gen hardware added by Jarod.
 * Initial port from lirc driver to ir-core drivery by Jarod, based
 * partially on a port to an earlier proposed IR infrastructure by
 * Jon Smirl, which included enhancements and simplifications to the
 * incoming IR buffer parsing routines.
 *
 * Updated in July of 2011 with the aid of Microsoft's official
 * remote/transceiver requirements and specification document, found at
 * download.microsoft.com, title
 * Windows-Media-Center-RC-IR-Collection-Green-Button-Specification-03-08-2011-V2.pdf
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/pm_wakeup.h>
#include <media/rc-core.h>

#define DRIVER_VERSION	"1.92"
#define DRIVER_AUTHOR	"Jarod Wilson <jarod@redhat.com>"
#define DRIVER_DESC	"Windows Media Center Ed. eHome Infrared Transceiver " \
			"device driver"
#define DRIVER_NAME	"mceusb"

#define USB_BUFLEN		32 /* USB reception buffer length */
#define USB_CTRL_MSG_SZ		2  /* Size of usb ctrl msg on gen1 hw */
#define MCE_G1_INIT_MSGS	40 /* Init messages on gen1 hw to throw out */

/* MCE constants */
#define MCE_CMDBUF_SIZE		384  /* MCE Command buffer length */
#define MCE_TIME_UNIT		50   /* Approx 50us resolution */
#define MCE_CODE_LENGTH		5    /* Normal length of packet (with header) */
#define MCE_PACKET_SIZE		4    /* Normal length of packet (without header) */
#define MCE_IRDATA_HEADER	0x84 /* Actual header format is 0x80 + num_bytes */
#define MCE_IRDATA_TRAILER	0x80 /* End of IR data */
#define MCE_MAX_CHANNELS	2    /* Two transmitters, hardware dependent? */
#define MCE_DEFAULT_TX_MASK	0x03 /* Vals: TX1=0x01, TX2=0x02, ALL=0x03 */
#define MCE_PULSE_BIT		0x80 /* Pulse bit, MSB set == PULSE else SPACE */
#define MCE_PULSE_MASK		0x7f /* Pulse mask */
#define MCE_MAX_PULSE_LENGTH	0x7f /* Longest transmittable pulse symbol */

/*
 * The interface between the host and the IR hardware is command-response
 * based. All commands and responses have a consistent format, where a lead
 * byte always identifies the type of data following it. The lead byte has
 * a port value in the 3 highest bits and a length value in the 5 lowest
 * bits.
 *
 * The length field is overloaded, with a value of 11111 indicating that the
 * following byte is a command or response code, and the length of the entire
 * message is determined by the code. If the length field is not 11111, then
 * it specifies the number of bytes of port data that follow.
 */
#define MCE_CMD			0x1f
#define MCE_PORT_IR		0x4	/* (0x4 << 5) | MCE_CMD = 0x9f */
#define MCE_PORT_SYS		0x7	/* (0x7 << 5) | MCE_CMD = 0xff */
#define MCE_PORT_SER		0x6	/* 0xc0 thru 0xdf flush & 0x1f bytes */
#define MCE_PORT_MASK		0xe0	/* Mask out command bits */

/* Command port headers */
#define MCE_CMD_PORT_IR		0x9f	/* IR-related cmd/rsp */
#define MCE_CMD_PORT_SYS	0xff	/* System (non-IR) device cmd/rsp */

/* Commands that set device state  (2-4 bytes in length) */
#define MCE_CMD_RESET		0xfe	/* Reset device, 2 bytes */
#define MCE_CMD_RESUME		0xaa	/* Resume device after error, 2 bytes */
#define MCE_CMD_SETIRCFS	0x06	/* Set tx carrier, 4 bytes */
#define MCE_CMD_SETIRTIMEOUT	0x0c	/* Set timeout, 4 bytes */
#define MCE_CMD_SETIRTXPORTS	0x08	/* Set tx ports, 3 bytes */
#define MCE_CMD_SETIRRXPORTEN	0x14	/* Set rx ports, 3 bytes */
#define MCE_CMD_FLASHLED	0x23	/* Flash receiver LED, 2 bytes */

/* Commands that query device state (all 2 bytes, unless noted) */
#define MCE_CMD_GETIRCFS	0x07	/* Get carrier */
#define MCE_CMD_GETIRTIMEOUT	0x0d	/* Get timeout */
#define MCE_CMD_GETIRTXPORTS	0x13	/* Get tx ports */
#define MCE_CMD_GETIRRXPORTEN	0x15	/* Get rx ports */
#define MCE_CMD_GETPORTSTATUS	0x11	/* Get tx port status, 3 bytes */
#define MCE_CMD_GETIRNUMPORTS	0x16	/* Get number of ports */
#define MCE_CMD_GETWAKESOURCE	0x17	/* Get wake source */
#define MCE_CMD_GETEMVER	0x22	/* Get emulator interface version */
#define MCE_CMD_GETDEVDETAILS	0x21	/* Get device details (em ver2 only) */
#define MCE_CMD_GETWAKESUPPORT	0x20	/* Get wake details (em ver2 only) */
#define MCE_CMD_GETWAKEVERSION	0x18	/* Get wake pattern (em ver2 only) */

/* Misc commands */
#define MCE_CMD_NOP		0xff	/* No operation */

/* Responses to commands (non-error cases) */
#define MCE_RSP_EQIRCFS		0x06	/* tx carrier, 4 bytes */
#define MCE_RSP_EQIRTIMEOUT	0x0c	/* rx timeout, 4 bytes */
#define MCE_RSP_GETWAKESOURCE	0x17	/* wake source, 3 bytes */
#define MCE_RSP_EQIRTXPORTS	0x08	/* tx port mask, 3 bytes */
#define MCE_RSP_EQIRRXPORTEN	0x14	/* rx port mask, 3 bytes */
#define MCE_RSP_GETPORTSTATUS	0x11	/* tx port status, 7 bytes */
#define MCE_RSP_EQIRRXCFCNT	0x15	/* rx carrier count, 4 bytes */
#define MCE_RSP_EQIRNUMPORTS	0x16	/* number of ports, 4 bytes */
#define MCE_RSP_EQWAKESUPPORT	0x20	/* wake capabilities, 3 bytes */
#define MCE_RSP_EQWAKEVERSION	0x18	/* wake pattern details, 6 bytes */
#define MCE_RSP_EQDEVDETAILS	0x21	/* device capabilities, 3 bytes */
#define MCE_RSP_EQEMVER		0x22	/* emulator interface ver, 3 bytes */
#define MCE_RSP_FLASHLED	0x23	/* success flashing LED, 2 bytes */

/* Responses to error cases, must send MCE_CMD_RESUME to clear them */
#define MCE_RSP_CMD_ILLEGAL	0xfe	/* illegal command for port, 2 bytes */
#define MCE_RSP_TX_TIMEOUT	0x81	/* tx timed out, 2 bytes */

/* Misc commands/responses not defined in the MCE remote/transceiver spec */
#define MCE_CMD_SIG_END		0x01	/* End of signal */
#define MCE_CMD_PING		0x03	/* Ping device */
#define MCE_CMD_UNKNOWN		0x04	/* Unknown */
#define MCE_CMD_UNKNOWN2	0x05	/* Unknown */
#define MCE_CMD_UNKNOWN3	0x09	/* Unknown */
#define MCE_CMD_UNKNOWN4	0x0a	/* Unknown */
#define MCE_CMD_G_REVISION	0x0b	/* Get hw/sw revision */
#define MCE_CMD_UNKNOWN5	0x0e	/* Unknown */
#define MCE_CMD_UNKNOWN6	0x0f	/* Unknown */
#define MCE_CMD_UNKNOWN8	0x19	/* Unknown */
#define MCE_CMD_UNKNOWN9	0x1b	/* Unknown */
#define MCE_CMD_NULL		0x00	/* These show up various places... */

/* if buf[i] & MCE_PORT_MASK == 0x80 and buf[i] != MCE_CMD_PORT_IR,
 * then we're looking at a raw IR data sample */
#define MCE_COMMAND_IRDATA	0x80
#define MCE_PACKET_LENGTH_MASK	0x1f /* Packet length mask */

/* general constants */
#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE	2
#define RECV_FLAG_IN_PROGRESS	3
#define RECV_FLAG_COMPLETE	4

#define MCEUSB_RX		1
#define MCEUSB_TX		2

#define VENDOR_PHILIPS		0x0471
#define VENDOR_SMK		0x0609
#define VENDOR_TATUNG		0x1460
#define VENDOR_GATEWAY		0x107b
#define VENDOR_SHUTTLE		0x1308
#define VENDOR_SHUTTLE2		0x051c
#define VENDOR_MITSUMI		0x03ee
#define VENDOR_TOPSEED		0x1784
#define VENDOR_RICAVISION	0x179d
#define VENDOR_ITRON		0x195d
#define VENDOR_FIC		0x1509
#define VENDOR_LG		0x043e
#define VENDOR_MICROSOFT	0x045e
#define VENDOR_FORMOSA		0x147a
#define VENDOR_FINTEK		0x1934
#define VENDOR_PINNACLE		0x2304
#define VENDOR_ECS		0x1019
#define VENDOR_WISTRON		0x0fb8
#define VENDOR_COMPRO		0x185b
#define VENDOR_NORTHSTAR	0x04eb
#define VENDOR_REALTEK		0x0bda
#define VENDOR_TIVO		0x105a
#define VENDOR_CONEXANT		0x0572
#define VENDOR_TWISTEDMELON	0x2596
#define VENDOR_HAUPPAUGE	0x2040
#define VENDOR_PCTV		0x2013

enum mceusb_model_type {
	MCE_GEN2 = 0,		/* Most boards */
	MCE_GEN1,
	MCE_GEN3,
	MCE_GEN2_TX_INV,
	POLARIS_EVK,
	CX_HYBRID_TV,
	MULTIFUNCTION,
	TIVO_KIT,
	MCE_GEN2_NO_TX,
	HAUPPAUGE_CX_HYBRID_TV,
};

struct mceusb_model {
	u32 mce_gen1:1;
	u32 mce_gen2:1;
	u32 mce_gen3:1;
	u32 tx_mask_normal:1;
	u32 no_tx:1;

	int ir_intfnum;

	const char *rc_map;	/* Allow specify a per-board map */
	const char *name;	/* per-board name */
};

static const struct mceusb_model mceusb_model[] = {
	[MCE_GEN1] = {
		.mce_gen1 = 1,
		.tx_mask_normal = 1,
	},
	[MCE_GEN2] = {
		.mce_gen2 = 1,
	},
	[MCE_GEN2_NO_TX] = {
		.mce_gen2 = 1,
		.no_tx = 1,
	},
	[MCE_GEN2_TX_INV] = {
		.mce_gen2 = 1,
		.tx_mask_normal = 1,
	},
	[MCE_GEN3] = {
		.mce_gen3 = 1,
		.tx_mask_normal = 1,
	},
	[POLARIS_EVK] = {
		/*
		 * In fact, the EVK is shipped without
		 * remotes, but we should have something handy,
		 * to allow testing it
		 */
		.name = "Conexant Hybrid TV (cx231xx) MCE IR",
	},
	[CX_HYBRID_TV] = {
		.no_tx = 1, /* tx isn't wired up at all */
		.name = "Conexant Hybrid TV (cx231xx) MCE IR",
	},
	[HAUPPAUGE_CX_HYBRID_TV] = {
		.no_tx = 1, /* eeprom says it has no tx */
		.name = "Conexant Hybrid TV (cx231xx) MCE IR no TX",
	},
	[MULTIFUNCTION] = {
		.mce_gen2 = 1,
		.ir_intfnum = 2,
	},
	[TIVO_KIT] = {
		.mce_gen2 = 1,
		.rc_map = RC_MAP_TIVO,
	},
};

static struct usb_device_id mceusb_dev_table[] = {
	/* Original Microsoft MCE IR Transceiver (often HP-branded) */
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x006d),
	  .driver_info = MCE_GEN1 },
	/* Philips Infrared Transceiver - Sahara branded */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0608) },
	/* Philips Infrared Transceiver - HP branded */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060c),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Philips SRM5100 */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060d) },
	/* Philips Infrared Transceiver - Omaura */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060f) },
	/* Philips Infrared Transceiver - Spinel plus */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0613) },
	/* Philips eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0815) },
	/* Philips/Spinel plus IR transceiver for ASUS */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x206c) },
	/* Philips/Spinel plus IR transceiver for ASUS */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x2088) },
	/* Philips IR transceiver (Dell branded) */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x2093),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Realtek MCE IR Receiver and card reader */
	{ USB_DEVICE(VENDOR_REALTEK, 0x0161),
	  .driver_info = MULTIFUNCTION },
	/* SMK/Toshiba G83C0004D410 */
	{ USB_DEVICE(VENDOR_SMK, 0x031d),
	  .driver_info = MCE_GEN2_TX_INV },
	/* SMK eHome Infrared Transceiver (Sony VAIO) */
	{ USB_DEVICE(VENDOR_SMK, 0x0322),
	  .driver_info = MCE_GEN2_TX_INV },
	/* bundled with Hauppauge PVR-150 */
	{ USB_DEVICE(VENDOR_SMK, 0x0334),
	  .driver_info = MCE_GEN2_TX_INV },
	/* SMK eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SMK, 0x0338) },
	/* SMK/I-O Data GV-MC7/RCKIT Receiver */
	{ USB_DEVICE(VENDOR_SMK, 0x0353),
	  .driver_info = MCE_GEN2_NO_TX },
	/* Tatung eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TATUNG, 0x9150) },
	/* Shuttle eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SHUTTLE, 0xc001) },
	/* Shuttle eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SHUTTLE2, 0xc001) },
	/* Gateway eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_GATEWAY, 0x3009) },
	/* Mitsumi */
	{ USB_DEVICE(VENDOR_MITSUMI, 0x2501) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0001),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Topseed HP eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0006),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0007),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0008),
	  .driver_info = MCE_GEN3 },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x000a),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0011),
	  .driver_info = MCE_GEN3 },
	/* Ricavision internal Infrared Transceiver */
	{ USB_DEVICE(VENDOR_RICAVISION, 0x0010) },
	/* Itron ione Libra Q-11 */
	{ USB_DEVICE(VENDOR_ITRON, 0x7002) },
	/* FIC eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FIC, 0x9242) },
	/* LG eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_LG, 0x9803) },
	/* Microsoft MCE Infrared Transceiver */
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x00a0) },
	/* Formosa eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe015) },
	/* Formosa21 / eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe016) },
	/* Formosa aim / Trust MCE Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe017),
	  .driver_info = MCE_GEN2_NO_TX },
	/* Formosa Industrial Computing / Beanbag Emulation Device */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe018) },
	/* Formosa21 / eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03a) },
	/* Formosa Industrial Computing AIM IR605/A */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03c) },
	/* Formosa Industrial Computing */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03e) },
	/* Formosa Industrial Computing */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe042) },
	/* Fintek eHome Infrared Transceiver (HP branded) */
	{ USB_DEVICE(VENDOR_FINTEK, 0x5168),
	  .driver_info = MCE_GEN2_TX_INV },
	/* Fintek eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FINTEK, 0x0602) },
	/* Fintek eHome Infrared Transceiver (in the AOpen MP45) */
	{ USB_DEVICE(VENDOR_FINTEK, 0x0702) },
	/* Pinnacle Remote Kit */
	{ USB_DEVICE(VENDOR_PINNACLE, 0x0225),
	  .driver_info = MCE_GEN3 },
	/* Elitegroup Computer Systems IR */
	{ USB_DEVICE(VENDOR_ECS, 0x0f38) },
	/* Wistron Corp. eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_WISTRON, 0x0002) },
	/* Compro K100 */
	{ USB_DEVICE(VENDOR_COMPRO, 0x3020) },
	/* Compro K100 v2 */
	{ USB_DEVICE(VENDOR_COMPRO, 0x3082) },
	/* Northstar Systems, Inc. eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_NORTHSTAR, 0xe004) },
	/* TiVo PC IR Receiver */
	{ USB_DEVICE(VENDOR_TIVO, 0x2000),
	  .driver_info = TIVO_KIT },
	/* Conexant Hybrid TV "Shelby" Polaris SDK */
	{ USB_DEVICE(VENDOR_CONEXANT, 0x58a1),
	  .driver_info = POLARIS_EVK },
	/* Conexant Hybrid TV RDU253S Polaris */
	{ USB_DEVICE(VENDOR_CONEXANT, 0x58a5),
	  .driver_info = CX_HYBRID_TV },
	/* Twisted Melon Inc. - Manta Mini Receiver */
	{ USB_DEVICE(VENDOR_TWISTEDMELON, 0x8008) },
	/* Twisted Melon Inc. - Manta Pico Receiver */
	{ USB_DEVICE(VENDOR_TWISTEDMELON, 0x8016) },
	/* Twisted Melon Inc. - Manta Transceiver */
	{ USB_DEVICE(VENDOR_TWISTEDMELON, 0x8042) },
	/* Hauppauge WINTV-HVR-HVR 930C-HD - based on cx231xx */
	{ USB_DEVICE(VENDOR_HAUPPAUGE, 0xb130),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },
	{ USB_DEVICE(VENDOR_HAUPPAUGE, 0xb131),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },
	{ USB_DEVICE(VENDOR_HAUPPAUGE, 0xb138),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },
	{ USB_DEVICE(VENDOR_HAUPPAUGE, 0xb139),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },
	{ USB_DEVICE(VENDOR_PCTV, 0x0259),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },
	{ USB_DEVICE(VENDOR_PCTV, 0x025e),
	  .driver_info = HAUPPAUGE_CX_HYBRID_TV },

	/* Terminating entry */
	{ }
};

/* data structure for each usb transceiver */
struct mceusb_dev {
	/* ir-core bits */
	struct rc_dev *rc;

	/* optional features we can enable */
	bool carrier_report_enabled;
	bool learning_enabled;

	/* core device bits */
	struct device *dev;

	/* usb */
	struct usb_device *usbdev;
	struct urb *urb_in;
	struct usb_endpoint_descriptor *usb_ep_out;

	/* buffers and dma */
	unsigned char *buf_in;
	unsigned int len_in;
	dma_addr_t dma_in;

	enum {
		CMD_HEADER = 0,
		SUBCMD,
		CMD_DATA,
		PARSE_IRDATA,
	} parser_state;

	u8 cmd, rem;		/* Remaining IR data bytes in packet */

	struct {
		u32 connected:1;
		u32 tx_mask_normal:1;
		u32 microsoft_gen1:1;
		u32 no_tx:1;
	} flags;

	/* transmit support */
	int send_flags;
	u32 carrier;
	unsigned char tx_mask;

	char name[128];
	char phys[64];
	enum mceusb_model_type model;

	bool need_reset;	/* flag to issue a device resume cmd */
	u8 emver;		/* emulator interface version */
	u8 num_txports;		/* number of transmit ports */
	u8 num_rxports;		/* number of receive sensors */
	u8 txports_cabled;	/* bitmask of transmitters with cable */
	u8 rxports_active;	/* bitmask of active receive sensors */
};

/* MCE Device Command Strings, generally a port and command pair */
static char DEVICE_RESUME[]	= {MCE_CMD_NULL, MCE_CMD_PORT_SYS,
				   MCE_CMD_RESUME};
static char GET_REVISION[]	= {MCE_CMD_PORT_SYS, MCE_CMD_G_REVISION};
static char GET_EMVER[]		= {MCE_CMD_PORT_SYS, MCE_CMD_GETEMVER};
static char GET_WAKEVERSION[]	= {MCE_CMD_PORT_SYS, MCE_CMD_GETWAKEVERSION};
static char FLASH_LED[]		= {MCE_CMD_PORT_SYS, MCE_CMD_FLASHLED};
static char GET_UNKNOWN2[]	= {MCE_CMD_PORT_IR, MCE_CMD_UNKNOWN2};
static char GET_CARRIER_FREQ[]	= {MCE_CMD_PORT_IR, MCE_CMD_GETIRCFS};
static char GET_RX_TIMEOUT[]	= {MCE_CMD_PORT_IR, MCE_CMD_GETIRTIMEOUT};
static char GET_NUM_PORTS[]	= {MCE_CMD_PORT_IR, MCE_CMD_GETIRNUMPORTS};
static char GET_TX_BITMASK[]	= {MCE_CMD_PORT_IR, MCE_CMD_GETIRTXPORTS};
static char GET_RX_SENSOR[]	= {MCE_CMD_PORT_IR, MCE_CMD_GETIRRXPORTEN};
/* sub in desired values in lower byte or bytes for full command */
/* FIXME: make use of these for transmit.
static char SET_CARRIER_FREQ[]	= {MCE_CMD_PORT_IR,
				   MCE_CMD_SETIRCFS, 0x00, 0x00};
static char SET_TX_BITMASK[]	= {MCE_CMD_PORT_IR, MCE_CMD_SETIRTXPORTS, 0x00};
static char SET_RX_TIMEOUT[]	= {MCE_CMD_PORT_IR,
				   MCE_CMD_SETIRTIMEOUT, 0x00, 0x00};
static char SET_RX_SENSOR[]	= {MCE_CMD_PORT_IR,
				   MCE_RSP_EQIRRXPORTEN, 0x00};
*/

static int mceusb_cmd_datasize(u8 cmd, u8 subcmd)
{
	int datasize = 0;

	switch (cmd) {
	case MCE_CMD_NULL:
		if (subcmd == MCE_CMD_PORT_SYS)
			datasize = 1;
		break;
	case MCE_CMD_PORT_SYS:
		switch (subcmd) {
		case MCE_RSP_GETPORTSTATUS:
			datasize = 5;
			break;
		case MCE_RSP_EQWAKEVERSION:
			datasize = 4;
			break;
		case MCE_CMD_G_REVISION:
			datasize = 2;
			break;
		case MCE_RSP_EQWAKESUPPORT:
		case MCE_RSP_GETWAKESOURCE:
		case MCE_RSP_EQDEVDETAILS:
		case MCE_RSP_EQEMVER:
			datasize = 1;
			break;
		}
	case MCE_CMD_PORT_IR:
		switch (subcmd) {
		case MCE_CMD_UNKNOWN:
		case MCE_RSP_EQIRCFS:
		case MCE_RSP_EQIRTIMEOUT:
		case MCE_RSP_EQIRRXCFCNT:
		case MCE_RSP_EQIRNUMPORTS:
			datasize = 2;
			break;
		case MCE_CMD_SIG_END:
		case MCE_RSP_EQIRTXPORTS:
		case MCE_RSP_EQIRRXPORTEN:
			datasize = 1;
			break;
		}
	}
	return datasize;
}

static void mceusb_dev_printdata(struct mceusb_dev *ir, char *buf,
				 int offset, int len, bool out)
{
#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
	char *inout;
	u8 cmd, subcmd, data1, data2, data3, data4;
	struct device *dev = ir->dev;
	int start, skip = 0;
	u32 carrier, period;

	/* skip meaningless 0xb1 0x60 header bytes on orig receiver */
	if (ir->flags.microsoft_gen1 && !out && !offset)
		skip = 2;

	if (len <= skip)
		return;

	dev_dbg(dev, "%cx data: %*ph (length=%d)",
		(out ? 't' : 'r'), min(len, USB_BUFLEN), buf, len);

	inout = out ? "Request" : "Got";

	start  = offset + skip;
	cmd    = buf[start] & 0xff;
	subcmd = buf[start + 1] & 0xff;
	data1  = buf[start + 2] & 0xff;
	data2  = buf[start + 3] & 0xff;
	data3  = buf[start + 4] & 0xff;
	data4  = buf[start + 5] & 0xff;

	switch (cmd) {
	case MCE_CMD_NULL:
		if (subcmd == MCE_CMD_NULL)
			break;
		if ((subcmd == MCE_CMD_PORT_SYS) &&
		    (data1 == MCE_CMD_RESUME))
			dev_dbg(dev, "Device resume requested");
		else
			dev_dbg(dev, "Unknown command 0x%02x 0x%02x",
				 cmd, subcmd);
		break;
	case MCE_CMD_PORT_SYS:
		switch (subcmd) {
		case MCE_RSP_EQEMVER:
			if (!out)
				dev_dbg(dev, "Emulator interface version %x",
					 data1);
			break;
		case MCE_CMD_G_REVISION:
			if (len == 2)
				dev_dbg(dev, "Get hw/sw rev?");
			else
				dev_dbg(dev, "hw/sw rev 0x%02x 0x%02x 0x%02x 0x%02x",
					 data1, data2,
					 buf[start + 4], buf[start + 5]);
			break;
		case MCE_CMD_RESUME:
			dev_dbg(dev, "Device resume requested");
			break;
		case MCE_RSP_CMD_ILLEGAL:
			dev_dbg(dev, "Illegal PORT_SYS command");
			break;
		case MCE_RSP_EQWAKEVERSION:
			if (!out)
				dev_dbg(dev, "Wake version, proto: 0x%02x, "
					 "payload: 0x%02x, address: 0x%02x, "
					 "version: 0x%02x",
					 data1, data2, data3, data4);
			break;
		case MCE_RSP_GETPORTSTATUS:
			if (!out)
				/* We use data1 + 1 here, to match hw labels */
				dev_dbg(dev, "TX port %d: blaster is%s connected",
					 data1 + 1, data4 ? " not" : "");
			break;
		case MCE_CMD_FLASHLED:
			dev_dbg(dev, "Attempting to flash LED");
			break;
		default:
			dev_dbg(dev, "Unknown command 0x%02x 0x%02x",
				 cmd, subcmd);
			break;
		}
		break;
	case MCE_CMD_PORT_IR:
		switch (subcmd) {
		case MCE_CMD_SIG_END:
			dev_dbg(dev, "End of signal");
			break;
		case MCE_CMD_PING:
			dev_dbg(dev, "Ping");
			break;
		case MCE_CMD_UNKNOWN:
			dev_dbg(dev, "Resp to 9f 05 of 0x%02x 0x%02x",
				 data1, data2);
			break;
		case MCE_RSP_EQIRCFS:
			period = DIV_ROUND_CLOSEST(
					(1U << data1 * 2) * (data2 + 1), 10);
			if (!period)
				break;
			carrier = (1000 * 1000) / period;
			dev_dbg(dev, "%s carrier of %u Hz (period %uus)",
				 inout, carrier, period);
			break;
		case MCE_CMD_GETIRCFS:
			dev_dbg(dev, "Get carrier mode and freq");
			break;
		case MCE_RSP_EQIRTXPORTS:
			dev_dbg(dev, "%s transmit blaster mask of 0x%02x",
				 inout, data1);
			break;
		case MCE_RSP_EQIRTIMEOUT:
			/* value is in units of 50us, so x*50/1000 ms */
			period = ((data1 << 8) | data2) * MCE_TIME_UNIT / 1000;
			dev_dbg(dev, "%s receive timeout of %d ms",
				 inout, period);
			break;
		case MCE_CMD_GETIRTIMEOUT:
			dev_dbg(dev, "Get receive timeout");
			break;
		case MCE_CMD_GETIRTXPORTS:
			dev_dbg(dev, "Get transmit blaster mask");
			break;
		case MCE_RSP_EQIRRXPORTEN:
			dev_dbg(dev, "%s %s-range receive sensor in use",
				 inout, data1 == 0x02 ? "short" : "long");
			break;
		case MCE_CMD_GETIRRXPORTEN:
		/* aka MCE_RSP_EQIRRXCFCNT */
			if (out)
				dev_dbg(dev, "Get receive sensor");
			else if (ir->learning_enabled)
				dev_dbg(dev, "RX pulse count: %d",
					 ((data1 << 8) | data2));
			break;
		case MCE_RSP_EQIRNUMPORTS:
			if (out)
				break;
			dev_dbg(dev, "Num TX ports: %x, num RX ports: %x",
				 data1, data2);
			break;
		case MCE_RSP_CMD_ILLEGAL:
			dev_dbg(dev, "Illegal PORT_IR command");
			break;
		default:
			dev_dbg(dev, "Unknown command 0x%02x 0x%02x",
				 cmd, subcmd);
			break;
		}
		break;
	default:
		break;
	}

	if (cmd == MCE_IRDATA_TRAILER)
		dev_dbg(dev, "End of raw IR data");
	else if ((cmd != MCE_CMD_PORT_IR) &&
		 ((cmd & MCE_PORT_MASK) == MCE_COMMAND_IRDATA))
		dev_dbg(dev, "Raw IR data, %d pulse/space samples", ir->rem);
#endif
}

static void mce_async_callback(struct urb *urb)
{
	struct mceusb_dev *ir;
	int len;

	if (!urb)
		return;

	ir = urb->context;

	switch (urb->status) {
	/* success */
	case 0:
		len = urb->actual_length;

		mceusb_dev_printdata(ir, urb->transfer_buffer, 0, len, true);
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -EILSEQ:
	case -ESHUTDOWN:
		break;

	case -EPIPE:
	default:
		dev_err(ir->dev, "Error: request urb status = %d", urb->status);
		break;
	}

	/* the transfer buffer and urb were allocated in mce_request_packet */
	kfree(urb->transfer_buffer);
	usb_free_urb(urb);
}

/* request incoming or send outgoing usb packet - used to initialize remote */
static void mce_request_packet(struct mceusb_dev *ir, unsigned char *data,
			       int size, int urb_type)
{
	int res, pipe;
	struct urb *async_urb;
	struct device *dev = ir->dev;
	unsigned char *async_buf;

	if (urb_type == MCEUSB_TX) {
		async_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (unlikely(!async_urb)) {
			dev_err(dev, "Error, couldn't allocate urb!\n");
			return;
		}

		async_buf = kzalloc(size, GFP_KERNEL);
		if (!async_buf) {
			dev_err(dev, "Error, couldn't allocate buf!\n");
			usb_free_urb(async_urb);
			return;
		}

		/* outbound data */
		if (usb_endpoint_xfer_int(ir->usb_ep_out)) {
			pipe = usb_sndintpipe(ir->usbdev,
					 ir->usb_ep_out->bEndpointAddress);
			usb_fill_int_urb(async_urb, ir->usbdev, pipe, async_buf,
					 size, mce_async_callback, ir,
					 ir->usb_ep_out->bInterval);
		} else {
			pipe = usb_sndbulkpipe(ir->usbdev,
					 ir->usb_ep_out->bEndpointAddress);
			usb_fill_bulk_urb(async_urb, ir->usbdev, pipe,
					 async_buf, size, mce_async_callback,
					 ir);
		}
		memcpy(async_buf, data, size);

	} else if (urb_type == MCEUSB_RX) {
		/* standard request */
		async_urb = ir->urb_in;
		ir->send_flags = RECV_FLAG_IN_PROGRESS;

	} else {
		dev_err(dev, "Error! Unknown urb type %d\n", urb_type);
		return;
	}

	dev_dbg(dev, "receive request called (size=%#x)", size);

	async_urb->transfer_buffer_length = size;
	async_urb->dev = ir->usbdev;

	res = usb_submit_urb(async_urb, GFP_ATOMIC);
	if (res) {
		dev_err(dev, "receive request FAILED! (res=%d)", res);
		return;
	}
	dev_dbg(dev, "receive request complete (res=%d)", res);
}

static void mce_async_out(struct mceusb_dev *ir, unsigned char *data, int size)
{
	int rsize = sizeof(DEVICE_RESUME);

	if (ir->need_reset) {
		ir->need_reset = false;
		mce_request_packet(ir, DEVICE_RESUME, rsize, MCEUSB_TX);
		msleep(10);
	}

	mce_request_packet(ir, data, size, MCEUSB_TX);
	msleep(10);
}

static void mce_flush_rx_buffer(struct mceusb_dev *ir, int size)
{
	mce_request_packet(ir, NULL, size, MCEUSB_RX);
}

/* Send data out the IR blaster port(s) */
static int mceusb_tx_ir(struct rc_dev *dev, unsigned *txbuf, unsigned count)
{
	struct mceusb_dev *ir = dev->priv;
	int i, length, ret = 0;
	int cmdcount = 0;
	unsigned char cmdbuf[MCE_CMDBUF_SIZE];

	/* MCE tx init header */
	cmdbuf[cmdcount++] = MCE_CMD_PORT_IR;
	cmdbuf[cmdcount++] = MCE_CMD_SETIRTXPORTS;
	cmdbuf[cmdcount++] = ir->tx_mask;

	/* Send the set TX ports command */
	mce_async_out(ir, cmdbuf, cmdcount);
	cmdcount = 0;

	/* Generate mce packet data */
	for (i = 0; (i < count) && (cmdcount < MCE_CMDBUF_SIZE); i++) {
		txbuf[i] = txbuf[i] / MCE_TIME_UNIT;

		do { /* loop to support long pulses/spaces > 127*50us=6.35ms */

			/* Insert mce packet header every 4th entry */
			if ((cmdcount < MCE_CMDBUF_SIZE) &&
			    (cmdcount % MCE_CODE_LENGTH) == 0)
				cmdbuf[cmdcount++] = MCE_IRDATA_HEADER;

			/* Insert mce packet data */
			if (cmdcount < MCE_CMDBUF_SIZE)
				cmdbuf[cmdcount++] =
					(txbuf[i] < MCE_PULSE_BIT ?
					 txbuf[i] : MCE_MAX_PULSE_LENGTH) |
					 (i & 1 ? 0x00 : MCE_PULSE_BIT);
			else {
				ret = -EINVAL;
				goto out;
			}

		} while ((txbuf[i] > MCE_MAX_PULSE_LENGTH) &&
			 (txbuf[i] -= MCE_MAX_PULSE_LENGTH));
	}

	/* Check if we have room for the empty packet at the end */
	if (cmdcount >= MCE_CMDBUF_SIZE) {
		ret = -EINVAL;
		goto out;
	}

	/* Fix packet length in last header */
	length = cmdcount % MCE_CODE_LENGTH;
	cmdbuf[cmdcount - length] -= MCE_CODE_LENGTH - length;

	/* All mce commands end with an empty packet (0x80) */
	cmdbuf[cmdcount++] = MCE_IRDATA_TRAILER;

	/* Transmit the command to the mce device */
	mce_async_out(ir, cmdbuf, cmdcount);

out:
	return ret ? ret : count;
}

/* Sets active IR outputs -- mce devices typically have two */
static int mceusb_set_tx_mask(struct rc_dev *dev, u32 mask)
{
	struct mceusb_dev *ir = dev->priv;

	if (ir->flags.tx_mask_normal)
		ir->tx_mask = mask;
	else
		ir->tx_mask = (mask != MCE_DEFAULT_TX_MASK ?
				mask ^ MCE_DEFAULT_TX_MASK : mask) << 1;

	return 0;
}

/* Sets the send carrier frequency and mode */
static int mceusb_set_tx_carrier(struct rc_dev *dev, u32 carrier)
{
	struct mceusb_dev *ir = dev->priv;
	int clk = 10000000;
	int prescaler = 0, divisor = 0;
	unsigned char cmdbuf[4] = { MCE_CMD_PORT_IR,
				    MCE_CMD_SETIRCFS, 0x00, 0x00 };

	/* Carrier has changed */
	if (ir->carrier != carrier) {

		if (carrier == 0) {
			ir->carrier = carrier;
			cmdbuf[2] = MCE_CMD_SIG_END;
			cmdbuf[3] = MCE_IRDATA_TRAILER;
			dev_dbg(ir->dev, "disabling carrier modulation");
			mce_async_out(ir, cmdbuf, sizeof(cmdbuf));
			return carrier;
		}

		for (prescaler = 0; prescaler < 4; ++prescaler) {
			divisor = (clk >> (2 * prescaler)) / carrier;
			if (divisor <= 0xff) {
				ir->carrier = carrier;
				cmdbuf[2] = prescaler;
				cmdbuf[3] = divisor;
				dev_dbg(ir->dev, "requesting %u HZ carrier",
								carrier);

				/* Transmit new carrier to mce device */
				mce_async_out(ir, cmdbuf, sizeof(cmdbuf));
				return carrier;
			}
		}

		return -EINVAL;

	}

	return carrier;
}

/*
 * We don't do anything but print debug spew for many of the command bits
 * we receive from the hardware, but some of them are useful information
 * we want to store so that we can use them.
 */
static void mceusb_handle_command(struct mceusb_dev *ir, int index)
{
	u8 hi = ir->buf_in[index + 1] & 0xff;
	u8 lo = ir->buf_in[index + 2] & 0xff;

	switch (ir->buf_in[index]) {
	/* the one and only 5-byte return value command */
	case MCE_RSP_GETPORTSTATUS:
		if ((ir->buf_in[index + 4] & 0xff) == 0x00)
			ir->txports_cabled |= 1 << hi;
		break;

	/* 2-byte return value commands */
	case MCE_RSP_EQIRTIMEOUT:
		ir->rc->timeout = US_TO_NS((hi << 8 | lo) * MCE_TIME_UNIT);
		break;
	case MCE_RSP_EQIRNUMPORTS:
		ir->num_txports = hi;
		ir->num_rxports = lo;
		break;

	/* 1-byte return value commands */
	case MCE_RSP_EQEMVER:
		ir->emver = hi;
		break;
	case MCE_RSP_EQIRTXPORTS:
		ir->tx_mask = hi;
		break;
	case MCE_RSP_EQIRRXPORTEN:
		ir->learning_enabled = ((hi & 0x02) == 0x02);
		ir->rxports_active = hi;
		break;
	case MCE_RSP_CMD_ILLEGAL:
		ir->need_reset = true;
		break;
	default:
		break;
	}
}

static void mceusb_process_ir_data(struct mceusb_dev *ir, int buf_len)
{
	DEFINE_IR_RAW_EVENT(rawir);
	bool event = false;
	int i = 0;

	/* skip meaningless 0xb1 0x60 header bytes on orig receiver */
	if (ir->flags.microsoft_gen1)
		i = 2;

	/* if there's no data, just return now */
	if (buf_len <= i)
		return;

	for (; i < buf_len; i++) {
		switch (ir->parser_state) {
		case SUBCMD:
			ir->rem = mceusb_cmd_datasize(ir->cmd, ir->buf_in[i]);
			mceusb_dev_printdata(ir, ir->buf_in, i - 1,
					     ir->rem + 2, false);
			mceusb_handle_command(ir, i);
			ir->parser_state = CMD_DATA;
			break;
		case PARSE_IRDATA:
			ir->rem--;
			init_ir_raw_event(&rawir);
			rawir.pulse = ((ir->buf_in[i] & MCE_PULSE_BIT) != 0);
			rawir.duration = (ir->buf_in[i] & MCE_PULSE_MASK)
					 * US_TO_NS(MCE_TIME_UNIT);

			dev_dbg(ir->dev, "Storing %s with duration %d",
				rawir.pulse ? "pulse" : "space",
				rawir.duration);

			if (ir_raw_event_store_with_filter(ir->rc, &rawir))
				event = true;
			break;
		case CMD_DATA:
			ir->rem--;
			break;
		case CMD_HEADER:
			/* decode mce packets of the form (84),AA,BB,CC,DD */
			/* IR data packets can span USB messages - rem */
			ir->cmd = ir->buf_in[i];
			if ((ir->cmd == MCE_CMD_PORT_IR) ||
			    ((ir->cmd & MCE_PORT_MASK) !=
			     MCE_COMMAND_IRDATA)) {
				ir->parser_state = SUBCMD;
				continue;
			}
			ir->rem = (ir->cmd & MCE_PACKET_LENGTH_MASK);
			mceusb_dev_printdata(ir, ir->buf_in,
					     i, ir->rem + 1, false);
			if (ir->rem)
				ir->parser_state = PARSE_IRDATA;
			else
				ir_raw_event_reset(ir->rc);
			break;
		}

		if (ir->parser_state != CMD_HEADER && !ir->rem)
			ir->parser_state = CMD_HEADER;
	}
	if (event) {
		dev_dbg(ir->dev, "processed IR data");
		ir_raw_event_handle(ir->rc);
	}
}

static void mceusb_dev_recv(struct urb *urb)
{
	struct mceusb_dev *ir;
	int buf_len;

	if (!urb)
		return;

	ir = urb->context;
	if (!ir) {
		usb_unlink_urb(urb);
		return;
	}

	buf_len = urb->actual_length;

	if (ir->send_flags == RECV_FLAG_IN_PROGRESS) {
		ir->send_flags = SEND_FLAG_COMPLETE;
		dev_dbg(ir->dev, "setup answer received %d bytes\n",
			buf_len);
	}

	switch (urb->status) {
	/* success */
	case 0:
		mceusb_process_ir_data(ir, buf_len);
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -EILSEQ:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;

	case -EPIPE:
	default:
		dev_err(ir->dev, "Error: urb status = %d", urb->status);
		break;
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

static void mceusb_get_emulator_version(struct mceusb_dev *ir)
{
	/* If we get no reply or an illegal command reply, its ver 1, says MS */
	ir->emver = 1;
	mce_async_out(ir, GET_EMVER, sizeof(GET_EMVER));
}

static void mceusb_gen1_init(struct mceusb_dev *ir)
{
	int ret;
	struct device *dev = ir->dev;
	char *data;

	data = kzalloc(USB_CTRL_MSG_SZ, GFP_KERNEL);
	if (!data) {
		dev_err(dev, "%s: memory allocation failed!", __func__);
		return;
	}

	/*
	 * This is a strange one. Windows issues a set address to the device
	 * on the receive control pipe and expect a certain value pair back
	 */
	ret = usb_control_msg(ir->usbdev, usb_rcvctrlpipe(ir->usbdev, 0),
			      USB_REQ_SET_ADDRESS, USB_TYPE_VENDOR, 0, 0,
			      data, USB_CTRL_MSG_SZ, HZ * 3);
	dev_dbg(dev, "set address - ret = %d", ret);
	dev_dbg(dev, "set address - data[0] = %d, data[1] = %d",
						data[0], data[1]);

	/* set feature: bit rate 38400 bps */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      USB_REQ_SET_FEATURE, USB_TYPE_VENDOR,
			      0xc04e, 0x0000, NULL, 0, HZ * 3);

	dev_dbg(dev, "set feature - ret = %d", ret);

	/* bRequest 4: set char length to 8 bits */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      4, USB_TYPE_VENDOR,
			      0x0808, 0x0000, NULL, 0, HZ * 3);
	dev_dbg(dev, "set char length - retB = %d", ret);

	/* bRequest 2: set handshaking to use DTR/DSR */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      2, USB_TYPE_VENDOR,
			      0x0000, 0x0100, NULL, 0, HZ * 3);
	dev_dbg(dev, "set handshake  - retC = %d", ret);

	/* device resume */
	mce_async_out(ir, DEVICE_RESUME, sizeof(DEVICE_RESUME));

	/* get hw/sw revision? */
	mce_async_out(ir, GET_REVISION, sizeof(GET_REVISION));

	kfree(data);
}

static void mceusb_gen2_init(struct mceusb_dev *ir)
{
	/* device resume */
	mce_async_out(ir, DEVICE_RESUME, sizeof(DEVICE_RESUME));

	/* get wake version (protocol, key, address) */
	mce_async_out(ir, GET_WAKEVERSION, sizeof(GET_WAKEVERSION));

	/* unknown what this one actually returns... */
	mce_async_out(ir, GET_UNKNOWN2, sizeof(GET_UNKNOWN2));
}

static void mceusb_get_parameters(struct mceusb_dev *ir)
{
	int i;
	unsigned char cmdbuf[3] = { MCE_CMD_PORT_SYS,
				    MCE_CMD_GETPORTSTATUS, 0x00 };

	/* defaults, if the hardware doesn't support querying */
	ir->num_txports = 2;
	ir->num_rxports = 2;

	/* get number of tx and rx ports */
	mce_async_out(ir, GET_NUM_PORTS, sizeof(GET_NUM_PORTS));

	/* get the carrier and frequency */
	mce_async_out(ir, GET_CARRIER_FREQ, sizeof(GET_CARRIER_FREQ));

	if (ir->num_txports && !ir->flags.no_tx)
		/* get the transmitter bitmask */
		mce_async_out(ir, GET_TX_BITMASK, sizeof(GET_TX_BITMASK));

	/* get receiver timeout value */
	mce_async_out(ir, GET_RX_TIMEOUT, sizeof(GET_RX_TIMEOUT));

	/* get receiver sensor setting */
	mce_async_out(ir, GET_RX_SENSOR, sizeof(GET_RX_SENSOR));

	for (i = 0; i < ir->num_txports; i++) {
		cmdbuf[2] = i;
		mce_async_out(ir, cmdbuf, sizeof(cmdbuf));
	}
}

static void mceusb_flash_led(struct mceusb_dev *ir)
{
	if (ir->emver < 2)
		return;

	mce_async_out(ir, FLASH_LED, sizeof(FLASH_LED));
}

static struct rc_dev *mceusb_init_rc_dev(struct mceusb_dev *ir)
{
	struct usb_device *udev = ir->usbdev;
	struct device *dev = ir->dev;
	struct rc_dev *rc;
	int ret;

	rc = rc_allocate_device();
	if (!rc) {
		dev_err(dev, "remote dev allocation failed");
		goto out;
	}

	snprintf(ir->name, sizeof(ir->name), "%s (%04x:%04x)",
		 mceusb_model[ir->model].name ?
			mceusb_model[ir->model].name :
			"Media Center Ed. eHome Infrared Remote Transceiver",
		 le16_to_cpu(ir->usbdev->descriptor.idVendor),
		 le16_to_cpu(ir->usbdev->descriptor.idProduct));

	usb_make_path(ir->usbdev, ir->phys, sizeof(ir->phys));

	rc->input_name = ir->name;
	rc->input_phys = ir->phys;
	usb_to_input_id(ir->usbdev, &rc->input_id);
	rc->dev.parent = dev;
	rc->priv = ir;
	rc->driver_type = RC_DRIVER_IR_RAW;
	rc->allowed_protocols = RC_BIT_ALL;
	rc->timeout = MS_TO_NS(100);
	if (!ir->flags.no_tx) {
		rc->s_tx_mask = mceusb_set_tx_mask;
		rc->s_tx_carrier = mceusb_set_tx_carrier;
		rc->tx_ir = mceusb_tx_ir;
	}
	rc->driver_name = DRIVER_NAME;

	switch (le16_to_cpu(udev->descriptor.idVendor)) {
	case VENDOR_HAUPPAUGE:
		rc->map_name = RC_MAP_HAUPPAUGE;
		break;
	case VENDOR_PCTV:
		rc->map_name = RC_MAP_PINNACLE_PCTV_HD;
		break;
	default:
		rc->map_name = RC_MAP_RC6_MCE;
	}
	if (mceusb_model[ir->model].rc_map)
		rc->map_name = mceusb_model[ir->model].rc_map;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(dev, "remote dev registration failed");
		goto out;
	}

	return rc;

out:
	rc_free_device(rc);
	return NULL;
}

static int mceusb_dev_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *idesc;
	struct usb_endpoint_descriptor *ep = NULL;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct mceusb_dev *ir = NULL;
	int pipe, maxp, i;
	char buf[63], name[128] = "";
	enum mceusb_model_type model = id->driver_info;
	bool is_gen3;
	bool is_microsoft_gen1;
	bool tx_mask_normal;
	int ir_intfnum;

	dev_dbg(&intf->dev, "%s called", __func__);

	idesc  = intf->cur_altsetting;

	is_gen3 = mceusb_model[model].mce_gen3;
	is_microsoft_gen1 = mceusb_model[model].mce_gen1;
	tx_mask_normal = mceusb_model[model].tx_mask_normal;
	ir_intfnum = mceusb_model[model].ir_intfnum;

	/* There are multi-function devices with non-IR interfaces */
	if (idesc->desc.bInterfaceNumber != ir_intfnum)
		return -ENODEV;

	/* step through the endpoints to find first bulk in and out endpoint */
	for (i = 0; i < idesc->desc.bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i].desc;

		if (ep_in == NULL) {
			if (usb_endpoint_is_bulk_in(ep)) {
				ep_in = ep;
				dev_dbg(&intf->dev, "acceptable bulk inbound endpoint found\n");
			} else if (usb_endpoint_is_int_in(ep)) {
				ep_in = ep;
				ep_in->bInterval = 1;
				dev_dbg(&intf->dev, "acceptable interrupt inbound endpoint found\n");
			}
		}

		if (ep_out == NULL) {
			if (usb_endpoint_is_bulk_out(ep)) {
				ep_out = ep;
				dev_dbg(&intf->dev, "acceptable bulk outbound endpoint found\n");
			} else if (usb_endpoint_is_int_out(ep)) {
				ep_out = ep;
				ep_out->bInterval = 1;
				dev_dbg(&intf->dev, "acceptable interrupt outbound endpoint found\n");
			}
		}
	}
	if (!ep_in || !ep_out) {
		dev_dbg(&intf->dev, "required endpoints not found\n");
		return -ENODEV;
	}

	if (usb_endpoint_xfer_int(ep_in))
		pipe = usb_rcvintpipe(dev, ep_in->bEndpointAddress);
	else
		pipe = usb_rcvbulkpipe(dev, ep_in->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	ir = kzalloc(sizeof(struct mceusb_dev), GFP_KERNEL);
	if (!ir)
		goto mem_alloc_fail;

	ir->buf_in = usb_alloc_coherent(dev, maxp, GFP_ATOMIC, &ir->dma_in);
	if (!ir->buf_in)
		goto buf_in_alloc_fail;

	ir->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!ir->urb_in)
		goto urb_in_alloc_fail;

	ir->usbdev = usb_get_dev(dev);
	ir->dev = &intf->dev;
	ir->len_in = maxp;
	ir->flags.microsoft_gen1 = is_microsoft_gen1;
	ir->flags.tx_mask_normal = tx_mask_normal;
	ir->flags.no_tx = mceusb_model[model].no_tx;
	ir->model = model;

	/* Saving usb interface data for use by the transmitter routine */
	ir->usb_ep_out = ep_out;

	if (dev->descriptor.iManufacturer
	    && usb_string(dev, dev->descriptor.iManufacturer,
			  buf, sizeof(buf)) > 0)
		strlcpy(name, buf, sizeof(name));
	if (dev->descriptor.iProduct
	    && usb_string(dev, dev->descriptor.iProduct,
			  buf, sizeof(buf)) > 0)
		snprintf(name + strlen(name), sizeof(name) - strlen(name),
			 " %s", buf);

	ir->rc = mceusb_init_rc_dev(ir);
	if (!ir->rc)
		goto rc_dev_fail;

	/* wire up inbound data handler */
	usb_fill_int_urb(ir->urb_in, dev, pipe, ir->buf_in, maxp,
				mceusb_dev_recv, ir, ep_in->bInterval);
	ir->urb_in->transfer_dma = ir->dma_in;
	ir->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* flush buffers on the device */
	dev_dbg(&intf->dev, "Flushing receive buffers\n");
	mce_flush_rx_buffer(ir, maxp);

	/* figure out which firmware/emulator version this hardware has */
	mceusb_get_emulator_version(ir);

	/* initialize device */
	if (ir->flags.microsoft_gen1)
		mceusb_gen1_init(ir);
	else if (!is_gen3)
		mceusb_gen2_init(ir);

	mceusb_get_parameters(ir);

	mceusb_flash_led(ir);

	if (!ir->flags.no_tx)
		mceusb_set_tx_mask(ir->rc, MCE_DEFAULT_TX_MASK);

	usb_set_intfdata(intf, ir);

	/* enable wake via this device */
	device_set_wakeup_capable(ir->dev, true);
	device_set_wakeup_enable(ir->dev, true);

	dev_info(&intf->dev, "Registered %s with mce emulator interface version %x",
		name, ir->emver);
	dev_info(&intf->dev, "%x tx ports (0x%x cabled) and %x rx sensors (0x%x active)",
		 ir->num_txports, ir->txports_cabled,
		 ir->num_rxports, ir->rxports_active);

	return 0;

	/* Error-handling path */
rc_dev_fail:
	usb_put_dev(ir->usbdev);
	usb_free_urb(ir->urb_in);
urb_in_alloc_fail:
	usb_free_coherent(dev, maxp, ir->buf_in, ir->dma_in);
buf_in_alloc_fail:
	kfree(ir);
mem_alloc_fail:
	dev_err(&intf->dev, "%s: device setup failed!", __func__);

	return -ENOMEM;
}


static void mceusb_dev_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct mceusb_dev *ir = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!ir)
		return;

	ir->usbdev = NULL;
	rc_unregister_device(ir->rc);
	usb_kill_urb(ir->urb_in);
	usb_free_urb(ir->urb_in);
	usb_free_coherent(dev, ir->len_in, ir->buf_in, ir->dma_in);
	usb_put_dev(dev);

	kfree(ir);
}

static int mceusb_dev_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct mceusb_dev *ir = usb_get_intfdata(intf);
	dev_info(ir->dev, "suspend");
	usb_kill_urb(ir->urb_in);
	return 0;
}

static int mceusb_dev_resume(struct usb_interface *intf)
{
	struct mceusb_dev *ir = usb_get_intfdata(intf);
	dev_info(ir->dev, "resume");
	if (usb_submit_urb(ir->urb_in, GFP_ATOMIC))
		return -EIO;
	return 0;
}

static struct usb_driver mceusb_dev_driver = {
	.name =		DRIVER_NAME,
	.probe =	mceusb_dev_probe,
	.disconnect =	mceusb_dev_disconnect,
	.suspend =	mceusb_dev_suspend,
	.resume =	mceusb_dev_resume,
	.reset_resume =	mceusb_dev_resume,
	.id_table =	mceusb_dev_table
};

module_usb_driver(mceusb_dev_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, mceusb_dev_table);
