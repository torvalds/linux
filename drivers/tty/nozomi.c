/*
 * nozomi.c  -- HSDPA driver Broadband Wireless Data Card - Globe Trotter
 *
 * Written by: Ulf Jakobsson,
 *             Jan Åkerfeldt,
 *             Stefan Thomasson,
 *
 * Maintained by: Paul Hardwick (p.hardwick@option.com)
 *
 * Patches:
 *          Locking code changes for Vodafone by Sphere Systems Ltd,
 *                              Andrew Bird (ajb@spheresystems.co.uk )
 *                              & Phil Sanderson
 *
 * Source has been ported from an implementation made by Filip Aben @ Option
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2005,2006 Option Wireless Sweden AB
 * Copyright (c) 2006 Sphere Systems Ltd
 * Copyright (c) 2006 Option Wireless n/v
 * All rights Reserved.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * --------------------------------------------------------------------------
 */

/* Enable this to have a lot of debug printouts */
#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include <linux/delay.h>


#define VERSION_STRING DRIVER_DESC " 2.1d"

/*    Macros definitions */

/* Default debug printout level */
#define NOZOMI_DEBUG_LEVEL 0x00

#define P_BUF_SIZE 128
#define NFO(_err_flag_, args...)				\
do {								\
	char tmp[P_BUF_SIZE];					\
	snprintf(tmp, sizeof(tmp), ##args);			\
	printk(_err_flag_ "[%d] %s(): %s\n", __LINE__,		\
		__func__, tmp);				\
} while (0)

#define DBG1(args...) D_(0x01, ##args)
#define DBG2(args...) D_(0x02, ##args)
#define DBG3(args...) D_(0x04, ##args)
#define DBG4(args...) D_(0x08, ##args)
#define DBG5(args...) D_(0x10, ##args)
#define DBG6(args...) D_(0x20, ##args)
#define DBG7(args...) D_(0x40, ##args)
#define DBG8(args...) D_(0x80, ##args)

#ifdef DEBUG
/* Do we need this settable at runtime? */
static int debug = NOZOMI_DEBUG_LEVEL;

#define D(lvl, args...)  do \
			{if (lvl & debug) NFO(KERN_DEBUG, ##args); } \
			while (0)
#define D_(lvl, args...) D(lvl, ##args)

/* These printouts are always printed */

#else
static int debug;
#define D_(lvl, args...)
#endif

/* TODO: rewrite to optimize macros... */

#define TMP_BUF_MAX 256

#define DUMP(buf__,len__) \
  do {  \
    char tbuf[TMP_BUF_MAX] = {0};\
    if (len__ > 1) {\
	snprintf(tbuf, len__ > TMP_BUF_MAX ? TMP_BUF_MAX : len__, "%s", buf__);\
	if (tbuf[len__-2] == '\r') {\
		tbuf[len__-2] = 'r';\
	} \
	DBG1("SENDING: '%s' (%d+n)", tbuf, len__);\
    } else {\
	DBG1("SENDING: '%s' (%d)", tbuf, len__);\
    } \
} while (0)

/*    Defines */
#define NOZOMI_NAME		"nozomi"
#define NOZOMI_NAME_TTY		"nozomi_tty"
#define DRIVER_DESC		"Nozomi driver"

#define NTTY_TTY_MAXMINORS	256
#define NTTY_FIFO_BUFFER_SIZE	8192

/* Must be power of 2 */
#define FIFO_BUFFER_SIZE_UL	8192

/* Size of tmp send buffer to card */
#define SEND_BUF_MAX		1024
#define RECEIVE_BUF_MAX		4


#define R_IIR		0x0000	/* Interrupt Identity Register */
#define R_FCR		0x0000	/* Flow Control Register */
#define R_IER		0x0004	/* Interrupt Enable Register */

#define CONFIG_MAGIC	0xEFEFFEFE
#define TOGGLE_VALID	0x0000

/* Definition of interrupt tokens */
#define MDM_DL1		0x0001
#define MDM_UL1		0x0002
#define MDM_DL2		0x0004
#define MDM_UL2		0x0008
#define DIAG_DL1	0x0010
#define DIAG_DL2	0x0020
#define DIAG_UL		0x0040
#define APP1_DL		0x0080
#define APP1_UL		0x0100
#define APP2_DL		0x0200
#define APP2_UL		0x0400
#define CTRL_DL		0x0800
#define CTRL_UL		0x1000
#define RESET		0x8000

#define MDM_DL		(MDM_DL1  | MDM_DL2)
#define MDM_UL		(MDM_UL1  | MDM_UL2)
#define DIAG_DL		(DIAG_DL1 | DIAG_DL2)

/* modem signal definition */
#define CTRL_DSR	0x0001
#define CTRL_DCD	0x0002
#define CTRL_RI		0x0004
#define CTRL_CTS	0x0008

#define CTRL_DTR	0x0001
#define CTRL_RTS	0x0002

#define MAX_PORT		4
#define NOZOMI_MAX_PORTS	5
#define NOZOMI_MAX_CARDS	(NTTY_TTY_MAXMINORS / MAX_PORT)

/*    Type definitions */

/*
 * There are two types of nozomi cards,
 * one with 2048 memory and with 8192 memory
 */
enum card_type {
	F32_2 = 2048,	/* 512 bytes downlink + uplink * 2 -> 2048 */
	F32_8 = 8192,	/* 3072 bytes downl. + 1024 bytes uplink * 2 -> 8192 */
};

/* Initialization states a card can be in */
enum card_state {
	NOZOMI_STATE_UKNOWN	= 0,
	NOZOMI_STATE_ENABLED	= 1,	/* pci device enabled */
	NOZOMI_STATE_ALLOCATED	= 2,	/* config setup done */
	NOZOMI_STATE_READY	= 3,	/* flowcontrols received */
};

/* Two different toggle channels exist */
enum channel_type {
	CH_A = 0,
	CH_B = 1,
};

/* Port definition for the card regarding flow control */
enum ctrl_port_type {
	CTRL_CMD	= 0,
	CTRL_MDM	= 1,
	CTRL_DIAG	= 2,
	CTRL_APP1	= 3,
	CTRL_APP2	= 4,
	CTRL_ERROR	= -1,
};

/* Ports that the nozomi has */
enum port_type {
	PORT_MDM	= 0,
	PORT_DIAG	= 1,
	PORT_APP1	= 2,
	PORT_APP2	= 3,
	PORT_CTRL	= 4,
	PORT_ERROR	= -1,
};

#ifdef __BIG_ENDIAN
/* Big endian */

struct toggles {
	unsigned int enabled:5;	/*
				 * Toggle fields are valid if enabled is 0,
				 * else A-channels must always be used.
				 */
	unsigned int diag_dl:1;
	unsigned int mdm_dl:1;
	unsigned int mdm_ul:1;
} __attribute__ ((packed));

/* Configuration table to read at startup of card */
/* Is for now only needed during initialization phase */
struct config_table {
	u32 signature;
	u16 product_information;
	u16 version;
	u8 pad3[3];
	struct toggles toggle;
	u8 pad1[4];
	u16 dl_mdm_len1;	/*
				 * If this is 64, it can hold
				 * 60 bytes + 4 that is length field
				 */
	u16 dl_start;

	u16 dl_diag_len1;
	u16 dl_mdm_len2;	/*
				 * If this is 64, it can hold
				 * 60 bytes + 4 that is length field
				 */
	u16 dl_app1_len;

	u16 dl_diag_len2;
	u16 dl_ctrl_len;
	u16 dl_app2_len;
	u8 pad2[16];
	u16 ul_mdm_len1;
	u16 ul_start;
	u16 ul_diag_len;
	u16 ul_mdm_len2;
	u16 ul_app1_len;
	u16 ul_app2_len;
	u16 ul_ctrl_len;
} __attribute__ ((packed));

/* This stores all control downlink flags */
struct ctrl_dl {
	u8 port;
	unsigned int reserved:4;
	unsigned int CTS:1;
	unsigned int RI:1;
	unsigned int DCD:1;
	unsigned int DSR:1;
} __attribute__ ((packed));

/* This stores all control uplink flags */
struct ctrl_ul {
	u8 port;
	unsigned int reserved:6;
	unsigned int RTS:1;
	unsigned int DTR:1;
} __attribute__ ((packed));

#else
/* Little endian */

/* This represents the toggle information */
struct toggles {
	unsigned int mdm_ul:1;
	unsigned int mdm_dl:1;
	unsigned int diag_dl:1;
	unsigned int enabled:5;	/*
				 * Toggle fields are valid if enabled is 0,
				 * else A-channels must always be used.
				 */
} __attribute__ ((packed));

/* Configuration table to read at startup of card */
struct config_table {
	u32 signature;
	u16 version;
	u16 product_information;
	struct toggles toggle;
	u8 pad1[7];
	u16 dl_start;
	u16 dl_mdm_len1;	/*
				 * If this is 64, it can hold
				 * 60 bytes + 4 that is length field
				 */
	u16 dl_mdm_len2;
	u16 dl_diag_len1;
	u16 dl_diag_len2;
	u16 dl_app1_len;
	u16 dl_app2_len;
	u16 dl_ctrl_len;
	u8 pad2[16];
	u16 ul_start;
	u16 ul_mdm_len2;
	u16 ul_mdm_len1;
	u16 ul_diag_len;
	u16 ul_app1_len;
	u16 ul_app2_len;
	u16 ul_ctrl_len;
} __attribute__ ((packed));

/* This stores all control downlink flags */
struct ctrl_dl {
	unsigned int DSR:1;
	unsigned int DCD:1;
	unsigned int RI:1;
	unsigned int CTS:1;
	unsigned int reserverd:4;
	u8 port;
} __attribute__ ((packed));

/* This stores all control uplink flags */
struct ctrl_ul {
	unsigned int DTR:1;
	unsigned int RTS:1;
	unsigned int reserved:6;
	u8 port;
} __attribute__ ((packed));
#endif

/* This holds all information that is needed regarding a port */
struct port {
	struct tty_port port;
	u8 update_flow_control;
	struct ctrl_ul ctrl_ul;
	struct ctrl_dl ctrl_dl;
	struct kfifo fifo_ul;
	void __iomem *dl_addr[2];
	u32 dl_size[2];
	u8 toggle_dl;
	void __iomem *ul_addr[2];
	u32 ul_size[2];
	u8 toggle_ul;
	u16 token_dl;

	wait_queue_head_t tty_wait;
	struct async_icount tty_icount;

	struct nozomi *dc;
};

/* Private data one for each card in the system */
struct nozomi {
	void __iomem *base_addr;
	unsigned long flip;

	/* Pointers to registers */
	void __iomem *reg_iir;
	void __iomem *reg_fcr;
	void __iomem *reg_ier;

	u16 last_ier;
	enum card_type card_type;
	struct config_table config_table;	/* Configuration table */
	struct pci_dev *pdev;
	struct port port[NOZOMI_MAX_PORTS];
	u8 *send_buf;

	spinlock_t spin_mutex;	/* secures access to registers and tty */

	unsigned int index_start;
	enum card_state state;
	u32 open_ttys;
};

/* This is a data packet that is read or written to/from card */
struct buffer {
	u32 size;		/* size is the length of the data buffer */
	u8 *data;
} __attribute__ ((packed));

/*    Global variables */
static const struct pci_device_id nozomi_pci_tbl[] = {
	{PCI_DEVICE(0x1931, 0x000c)},	/* Nozomi HSDPA */
	{},
};

MODULE_DEVICE_TABLE(pci, nozomi_pci_tbl);

static struct nozomi *ndevs[NOZOMI_MAX_CARDS];
static struct tty_driver *ntty_driver;

static const struct tty_port_operations noz_tty_port_ops;

/*
 * find card by tty_index
 */
static inline struct nozomi *get_dc_by_tty(const struct tty_struct *tty)
{
	return tty ? ndevs[tty->index / MAX_PORT] : NULL;
}

static inline struct port *get_port_by_tty(const struct tty_struct *tty)
{
	struct nozomi *ndev = get_dc_by_tty(tty);
	return ndev ? &ndev->port[tty->index % MAX_PORT] : NULL;
}

/*
 * TODO:
 * -Optimize
 * -Rewrite cleaner
 */

static void read_mem32(u32 *buf, const void __iomem *mem_addr_start,
			u32 size_bytes)
{
	u32 i = 0;
	const u32 __iomem *ptr = mem_addr_start;
	u16 *buf16;

	if (unlikely(!ptr || !buf))
		goto out;

	/* shortcut for extremely often used cases */
	switch (size_bytes) {
	case 2:	/* 2 bytes */
		buf16 = (u16 *) buf;
		*buf16 = __le16_to_cpu(readw(ptr));
		goto out;
		break;
	case 4:	/* 4 bytes */
		*(buf) = __le32_to_cpu(readl(ptr));
		goto out;
		break;
	}

	while (i < size_bytes) {
		if (size_bytes - i == 2) {
			/* Handle 2 bytes in the end */
			buf16 = (u16 *) buf;
			*(buf16) = __le16_to_cpu(readw(ptr));
			i += 2;
		} else {
			/* Read 4 bytes */
			*(buf) = __le32_to_cpu(readl(ptr));
			i += 4;
		}
		buf++;
		ptr++;
	}
out:
	return;
}

/*
 * TODO:
 * -Optimize
 * -Rewrite cleaner
 */
static u32 write_mem32(void __iomem *mem_addr_start, const u32 *buf,
			u32 size_bytes)
{
	u32 i = 0;
	u32 __iomem *ptr = mem_addr_start;
	const u16 *buf16;

	if (unlikely(!ptr || !buf))
		return 0;

	/* shortcut for extremely often used cases */
	switch (size_bytes) {
	case 2:	/* 2 bytes */
		buf16 = (const u16 *)buf;
		writew(__cpu_to_le16(*buf16), ptr);
		return 2;
		break;
	case 1: /*
		 * also needs to write 4 bytes in this case
		 * so falling through..
		 */
	case 4: /* 4 bytes */
		writel(__cpu_to_le32(*buf), ptr);
		return 4;
		break;
	}

	while (i < size_bytes) {
		if (size_bytes - i == 2) {
			/* 2 bytes */
			buf16 = (const u16 *)buf;
			writew(__cpu_to_le16(*buf16), ptr);
			i += 2;
		} else {
			/* 4 bytes */
			writel(__cpu_to_le32(*buf), ptr);
			i += 4;
		}
		buf++;
		ptr++;
	}
	return i;
}

/* Setup pointers to different channels and also setup buffer sizes. */
static void setup_memory(struct nozomi *dc)
{
	void __iomem *offset = dc->base_addr + dc->config_table.dl_start;
	/* The length reported is including the length field of 4 bytes,
	 * hence subtract with 4.
	 */
	const u16 buff_offset = 4;

	/* Modem port dl configuration */
	dc->port[PORT_MDM].dl_addr[CH_A] = offset;
	dc->port[PORT_MDM].dl_addr[CH_B] =
				(offset += dc->config_table.dl_mdm_len1);
	dc->port[PORT_MDM].dl_size[CH_A] =
				dc->config_table.dl_mdm_len1 - buff_offset;
	dc->port[PORT_MDM].dl_size[CH_B] =
				dc->config_table.dl_mdm_len2 - buff_offset;

	/* Diag port dl configuration */
	dc->port[PORT_DIAG].dl_addr[CH_A] =
				(offset += dc->config_table.dl_mdm_len2);
	dc->port[PORT_DIAG].dl_size[CH_A] =
				dc->config_table.dl_diag_len1 - buff_offset;
	dc->port[PORT_DIAG].dl_addr[CH_B] =
				(offset += dc->config_table.dl_diag_len1);
	dc->port[PORT_DIAG].dl_size[CH_B] =
				dc->config_table.dl_diag_len2 - buff_offset;

	/* App1 port dl configuration */
	dc->port[PORT_APP1].dl_addr[CH_A] =
				(offset += dc->config_table.dl_diag_len2);
	dc->port[PORT_APP1].dl_size[CH_A] =
				dc->config_table.dl_app1_len - buff_offset;

	/* App2 port dl configuration */
	dc->port[PORT_APP2].dl_addr[CH_A] =
				(offset += dc->config_table.dl_app1_len);
	dc->port[PORT_APP2].dl_size[CH_A] =
				dc->config_table.dl_app2_len - buff_offset;

	/* Ctrl dl configuration */
	dc->port[PORT_CTRL].dl_addr[CH_A] =
				(offset += dc->config_table.dl_app2_len);
	dc->port[PORT_CTRL].dl_size[CH_A] =
				dc->config_table.dl_ctrl_len - buff_offset;

	offset = dc->base_addr + dc->config_table.ul_start;

	/* Modem Port ul configuration */
	dc->port[PORT_MDM].ul_addr[CH_A] = offset;
	dc->port[PORT_MDM].ul_size[CH_A] =
				dc->config_table.ul_mdm_len1 - buff_offset;
	dc->port[PORT_MDM].ul_addr[CH_B] =
				(offset += dc->config_table.ul_mdm_len1);
	dc->port[PORT_MDM].ul_size[CH_B] =
				dc->config_table.ul_mdm_len2 - buff_offset;

	/* Diag port ul configuration */
	dc->port[PORT_DIAG].ul_addr[CH_A] =
				(offset += dc->config_table.ul_mdm_len2);
	dc->port[PORT_DIAG].ul_size[CH_A] =
				dc->config_table.ul_diag_len - buff_offset;

	/* App1 port ul configuration */
	dc->port[PORT_APP1].ul_addr[CH_A] =
				(offset += dc->config_table.ul_diag_len);
	dc->port[PORT_APP1].ul_size[CH_A] =
				dc->config_table.ul_app1_len - buff_offset;

	/* App2 port ul configuration */
	dc->port[PORT_APP2].ul_addr[CH_A] =
				(offset += dc->config_table.ul_app1_len);
	dc->port[PORT_APP2].ul_size[CH_A] =
				dc->config_table.ul_app2_len - buff_offset;

	/* Ctrl ul configuration */
	dc->port[PORT_CTRL].ul_addr[CH_A] =
				(offset += dc->config_table.ul_app2_len);
	dc->port[PORT_CTRL].ul_size[CH_A] =
				dc->config_table.ul_ctrl_len - buff_offset;
}

/* Dump config table under initalization phase */
#ifdef DEBUG
static void dump_table(const struct nozomi *dc)
{
	DBG3("signature: 0x%08X", dc->config_table.signature);
	DBG3("version: 0x%04X", dc->config_table.version);
	DBG3("product_information: 0x%04X", \
				dc->config_table.product_information);
	DBG3("toggle enabled: %d", dc->config_table.toggle.enabled);
	DBG3("toggle up_mdm: %d", dc->config_table.toggle.mdm_ul);
	DBG3("toggle dl_mdm: %d", dc->config_table.toggle.mdm_dl);
	DBG3("toggle dl_dbg: %d", dc->config_table.toggle.diag_dl);

	DBG3("dl_start: 0x%04X", dc->config_table.dl_start);
	DBG3("dl_mdm_len0: 0x%04X, %d", dc->config_table.dl_mdm_len1,
	   dc->config_table.dl_mdm_len1);
	DBG3("dl_mdm_len1: 0x%04X, %d", dc->config_table.dl_mdm_len2,
	   dc->config_table.dl_mdm_len2);
	DBG3("dl_diag_len0: 0x%04X, %d", dc->config_table.dl_diag_len1,
	   dc->config_table.dl_diag_len1);
	DBG3("dl_diag_len1: 0x%04X, %d", dc->config_table.dl_diag_len2,
	   dc->config_table.dl_diag_len2);
	DBG3("dl_app1_len: 0x%04X, %d", dc->config_table.dl_app1_len,
	   dc->config_table.dl_app1_len);
	DBG3("dl_app2_len: 0x%04X, %d", dc->config_table.dl_app2_len,
	   dc->config_table.dl_app2_len);
	DBG3("dl_ctrl_len: 0x%04X, %d", dc->config_table.dl_ctrl_len,
	   dc->config_table.dl_ctrl_len);
	DBG3("ul_start: 0x%04X, %d", dc->config_table.ul_start,
	   dc->config_table.ul_start);
	DBG3("ul_mdm_len[0]: 0x%04X, %d", dc->config_table.ul_mdm_len1,
	   dc->config_table.ul_mdm_len1);
	DBG3("ul_mdm_len[1]: 0x%04X, %d", dc->config_table.ul_mdm_len2,
	   dc->config_table.ul_mdm_len2);
	DBG3("ul_diag_len: 0x%04X, %d", dc->config_table.ul_diag_len,
	   dc->config_table.ul_diag_len);
	DBG3("ul_app1_len: 0x%04X, %d", dc->config_table.ul_app1_len,
	   dc->config_table.ul_app1_len);
	DBG3("ul_app2_len: 0x%04X, %d", dc->config_table.ul_app2_len,
	   dc->config_table.ul_app2_len);
	DBG3("ul_ctrl_len: 0x%04X, %d", dc->config_table.ul_ctrl_len,
	   dc->config_table.ul_ctrl_len);
}
#else
static inline void dump_table(const struct nozomi *dc) { }
#endif

/*
 * Read configuration table from card under intalization phase
 * Returns 1 if ok, else 0
 */
static int nozomi_read_config_table(struct nozomi *dc)
{
	read_mem32((u32 *) &dc->config_table, dc->base_addr + 0,
						sizeof(struct config_table));

	if (dc->config_table.signature != CONFIG_MAGIC) {
		dev_err(&dc->pdev->dev, "ConfigTable Bad! 0x%08X != 0x%08X\n",
			dc->config_table.signature, CONFIG_MAGIC);
		return 0;
	}

	if ((dc->config_table.version == 0)
	    || (dc->config_table.toggle.enabled == TOGGLE_VALID)) {
		int i;
		DBG1("Second phase, configuring card");

		setup_memory(dc);

		dc->port[PORT_MDM].toggle_ul = dc->config_table.toggle.mdm_ul;
		dc->port[PORT_MDM].toggle_dl = dc->config_table.toggle.mdm_dl;
		dc->port[PORT_DIAG].toggle_dl = dc->config_table.toggle.diag_dl;
		DBG1("toggle ports: MDM UL:%d MDM DL:%d, DIAG DL:%d",
		   dc->port[PORT_MDM].toggle_ul,
		   dc->port[PORT_MDM].toggle_dl, dc->port[PORT_DIAG].toggle_dl);

		dump_table(dc);

		for (i = PORT_MDM; i < MAX_PORT; i++) {
			memset(&dc->port[i].ctrl_dl, 0, sizeof(struct ctrl_dl));
			memset(&dc->port[i].ctrl_ul, 0, sizeof(struct ctrl_ul));
		}

		/* Enable control channel */
		dc->last_ier = dc->last_ier | CTRL_DL;
		writew(dc->last_ier, dc->reg_ier);

		dc->state = NOZOMI_STATE_ALLOCATED;
		dev_info(&dc->pdev->dev, "Initialization OK!\n");
		return 1;
	}

	if ((dc->config_table.version > 0)
	    && (dc->config_table.toggle.enabled != TOGGLE_VALID)) {
		u32 offset = 0;
		DBG1("First phase: pushing upload buffers, clearing download");

		dev_info(&dc->pdev->dev, "Version of card: %d\n",
			 dc->config_table.version);

		/* Here we should disable all I/O over F32. */
		setup_memory(dc);

		/*
		 * We should send ALL channel pair tokens back along
		 * with reset token
		 */

		/* push upload modem buffers */
		write_mem32(dc->port[PORT_MDM].ul_addr[CH_A],
			(u32 *) &offset, 4);
		write_mem32(dc->port[PORT_MDM].ul_addr[CH_B],
			(u32 *) &offset, 4);

		writew(MDM_UL | DIAG_DL | MDM_DL, dc->reg_fcr);

		DBG1("First phase done");
	}

	return 1;
}

/* Enable uplink interrupts  */
static void enable_transmit_ul(enum port_type port, struct nozomi *dc)
{
	static const u16 mask[] = {MDM_UL, DIAG_UL, APP1_UL, APP2_UL, CTRL_UL};

	if (port < NOZOMI_MAX_PORTS) {
		dc->last_ier |= mask[port];
		writew(dc->last_ier, dc->reg_ier);
	} else {
		dev_err(&dc->pdev->dev, "Called with wrong port?\n");
	}
}

/* Disable uplink interrupts  */
static void disable_transmit_ul(enum port_type port, struct nozomi *dc)
{
	static const u16 mask[] =
		{~MDM_UL, ~DIAG_UL, ~APP1_UL, ~APP2_UL, ~CTRL_UL};

	if (port < NOZOMI_MAX_PORTS) {
		dc->last_ier &= mask[port];
		writew(dc->last_ier, dc->reg_ier);
	} else {
		dev_err(&dc->pdev->dev, "Called with wrong port?\n");
	}
}

/* Enable downlink interrupts */
static void enable_transmit_dl(enum port_type port, struct nozomi *dc)
{
	static const u16 mask[] = {MDM_DL, DIAG_DL, APP1_DL, APP2_DL, CTRL_DL};

	if (port < NOZOMI_MAX_PORTS) {
		dc->last_ier |= mask[port];
		writew(dc->last_ier, dc->reg_ier);
	} else {
		dev_err(&dc->pdev->dev, "Called with wrong port?\n");
	}
}

/* Disable downlink interrupts */
static void disable_transmit_dl(enum port_type port, struct nozomi *dc)
{
	static const u16 mask[] =
		{~MDM_DL, ~DIAG_DL, ~APP1_DL, ~APP2_DL, ~CTRL_DL};

	if (port < NOZOMI_MAX_PORTS) {
		dc->last_ier &= mask[port];
		writew(dc->last_ier, dc->reg_ier);
	} else {
		dev_err(&dc->pdev->dev, "Called with wrong port?\n");
	}
}

/*
 * Return 1 - send buffer to card and ack.
 * Return 0 - don't ack, don't send buffer to card.
 */
static int send_data(enum port_type index, struct nozomi *dc)
{
	u32 size = 0;
	struct port *port = &dc->port[index];
	const u8 toggle = port->toggle_ul;
	void __iomem *addr = port->ul_addr[toggle];
	const u32 ul_size = port->ul_size[toggle];

	/* Get data from tty and place in buf for now */
	size = kfifo_out(&port->fifo_ul, dc->send_buf,
			   ul_size < SEND_BUF_MAX ? ul_size : SEND_BUF_MAX);

	if (size == 0) {
		DBG4("No more data to send, disable link:");
		return 0;
	}

	/* DUMP(buf, size); */

	/* Write length + data */
	write_mem32(addr, (u32 *) &size, 4);
	write_mem32(addr + 4, (u32 *) dc->send_buf, size);

	tty_port_tty_wakeup(&port->port);

	return 1;
}

/* If all data has been read, return 1, else 0 */
static int receive_data(enum port_type index, struct nozomi *dc)
{
	u8 buf[RECEIVE_BUF_MAX] = { 0 };
	int size;
	u32 offset = 4;
	struct port *port = &dc->port[index];
	void __iomem *addr = port->dl_addr[port->toggle_dl];
	struct tty_struct *tty = tty_port_tty_get(&port->port);
	int i, ret;

	read_mem32((u32 *) &size, addr, 4);
	/*  DBG1( "%d bytes port: %d", size, index); */

	if (tty && test_bit(TTY_THROTTLED, &tty->flags)) {
		DBG1("No room in tty, don't read data, don't ack interrupt, "
			"disable interrupt");

		/* disable interrupt in downlink... */
		disable_transmit_dl(index, dc);
		ret = 0;
		goto put;
	}

	if (unlikely(size == 0)) {
		dev_err(&dc->pdev->dev, "size == 0?\n");
		ret = 1;
		goto put;
	}

	while (size > 0) {
		read_mem32((u32 *) buf, addr + offset, RECEIVE_BUF_MAX);

		if (size == 1) {
			tty_insert_flip_char(&port->port, buf[0], TTY_NORMAL);
			size = 0;
		} else if (size < RECEIVE_BUF_MAX) {
			size -= tty_insert_flip_string(&port->port,
					(char *)buf, size);
		} else {
			i = tty_insert_flip_string(&port->port,
					(char *)buf, RECEIVE_BUF_MAX);
			size -= i;
			offset += i;
		}
	}

	set_bit(index, &dc->flip);
	ret = 1;
put:
	tty_kref_put(tty);
	return ret;
}

/* Debug for interrupts */
#ifdef DEBUG
static char *interrupt2str(u16 interrupt)
{
	static char buf[TMP_BUF_MAX];
	char *p = buf;

	interrupt & MDM_DL1 ? p += snprintf(p, TMP_BUF_MAX, "MDM_DL1 ") : NULL;
	interrupt & MDM_DL2 ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"MDM_DL2 ") : NULL;

	interrupt & MDM_UL1 ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"MDM_UL1 ") : NULL;
	interrupt & MDM_UL2 ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"MDM_UL2 ") : NULL;

	interrupt & DIAG_DL1 ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"DIAG_DL1 ") : NULL;
	interrupt & DIAG_DL2 ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"DIAG_DL2 ") : NULL;

	interrupt & DIAG_UL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"DIAG_UL ") : NULL;

	interrupt & APP1_DL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"APP1_DL ") : NULL;
	interrupt & APP2_DL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"APP2_DL ") : NULL;

	interrupt & APP1_UL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"APP1_UL ") : NULL;
	interrupt & APP2_UL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"APP2_UL ") : NULL;

	interrupt & CTRL_DL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"CTRL_DL ") : NULL;
	interrupt & CTRL_UL ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"CTRL_UL ") : NULL;

	interrupt & RESET ? p += snprintf(p, TMP_BUF_MAX - (p - buf),
					"RESET ") : NULL;

	return buf;
}
#endif

/*
 * Receive flow control
 * Return 1 - If ok, else 0
 */
static int receive_flow_control(struct nozomi *dc)
{
	enum port_type port = PORT_MDM;
	struct ctrl_dl ctrl_dl;
	struct ctrl_dl old_ctrl;
	u16 enable_ier = 0;

	read_mem32((u32 *) &ctrl_dl, dc->port[PORT_CTRL].dl_addr[CH_A], 2);

	switch (ctrl_dl.port) {
	case CTRL_CMD:
		DBG1("The Base Band sends this value as a response to a "
			"request for IMSI detach sent over the control "
			"channel uplink (see section 7.6.1).");
		break;
	case CTRL_MDM:
		port = PORT_MDM;
		enable_ier = MDM_DL;
		break;
	case CTRL_DIAG:
		port = PORT_DIAG;
		enable_ier = DIAG_DL;
		break;
	case CTRL_APP1:
		port = PORT_APP1;
		enable_ier = APP1_DL;
		break;
	case CTRL_APP2:
		port = PORT_APP2;
		enable_ier = APP2_DL;
		if (dc->state == NOZOMI_STATE_ALLOCATED) {
			/*
			 * After card initialization the flow control
			 * received for APP2 is always the last
			 */
			dc->state = NOZOMI_STATE_READY;
			dev_info(&dc->pdev->dev, "Device READY!\n");
		}
		break;
	default:
		dev_err(&dc->pdev->dev,
			"ERROR: flow control received for non-existing port\n");
		return 0;
	}

	DBG1("0x%04X->0x%04X", *((u16 *)&dc->port[port].ctrl_dl),
	   *((u16 *)&ctrl_dl));

	old_ctrl = dc->port[port].ctrl_dl;
	dc->port[port].ctrl_dl = ctrl_dl;

	if (old_ctrl.CTS == 1 && ctrl_dl.CTS == 0) {
		DBG1("Disable interrupt (0x%04X) on port: %d",
			enable_ier, port);
		disable_transmit_ul(port, dc);

	} else if (old_ctrl.CTS == 0 && ctrl_dl.CTS == 1) {

		if (kfifo_len(&dc->port[port].fifo_ul)) {
			DBG1("Enable interrupt (0x%04X) on port: %d",
				enable_ier, port);
			DBG1("Data in buffer [%d], enable transmit! ",
				kfifo_len(&dc->port[port].fifo_ul));
			enable_transmit_ul(port, dc);
		} else {
			DBG1("No data in buffer...");
		}
	}

	if (*(u16 *)&old_ctrl == *(u16 *)&ctrl_dl) {
		DBG1(" No change in mctrl");
		return 1;
	}
	/* Update statistics */
	if (old_ctrl.CTS != ctrl_dl.CTS)
		dc->port[port].tty_icount.cts++;
	if (old_ctrl.DSR != ctrl_dl.DSR)
		dc->port[port].tty_icount.dsr++;
	if (old_ctrl.RI != ctrl_dl.RI)
		dc->port[port].tty_icount.rng++;
	if (old_ctrl.DCD != ctrl_dl.DCD)
		dc->port[port].tty_icount.dcd++;

	wake_up_interruptible(&dc->port[port].tty_wait);

	DBG1("port: %d DCD(%d), CTS(%d), RI(%d), DSR(%d)",
	   port,
	   dc->port[port].tty_icount.dcd, dc->port[port].tty_icount.cts,
	   dc->port[port].tty_icount.rng, dc->port[port].tty_icount.dsr);

	return 1;
}

static enum ctrl_port_type port2ctrl(enum port_type port,
					const struct nozomi *dc)
{
	switch (port) {
	case PORT_MDM:
		return CTRL_MDM;
	case PORT_DIAG:
		return CTRL_DIAG;
	case PORT_APP1:
		return CTRL_APP1;
	case PORT_APP2:
		return CTRL_APP2;
	default:
		dev_err(&dc->pdev->dev,
			"ERROR: send flow control " \
			"received for non-existing port\n");
	}
	return CTRL_ERROR;
}

/*
 * Send flow control, can only update one channel at a time
 * Return 0 - If we have updated all flow control
 * Return 1 - If we need to update more flow control, ack current enable more
 */
static int send_flow_control(struct nozomi *dc)
{
	u32 i, more_flow_control_to_be_updated = 0;
	u16 *ctrl;

	for (i = PORT_MDM; i < MAX_PORT; i++) {
		if (dc->port[i].update_flow_control) {
			if (more_flow_control_to_be_updated) {
				/* We have more flow control to be updated */
				return 1;
			}
			dc->port[i].ctrl_ul.port = port2ctrl(i, dc);
			ctrl = (u16 *)&dc->port[i].ctrl_ul;
			write_mem32(dc->port[PORT_CTRL].ul_addr[0], \
				(u32 *) ctrl, 2);
			dc->port[i].update_flow_control = 0;
			more_flow_control_to_be_updated = 1;
		}
	}
	return 0;
}

/*
 * Handle downlink data, ports that are handled are modem and diagnostics
 * Return 1 - ok
 * Return 0 - toggle fields are out of sync
 */
static int handle_data_dl(struct nozomi *dc, enum port_type port, u8 *toggle,
			u16 read_iir, u16 mask1, u16 mask2)
{
	if (*toggle == 0 && read_iir & mask1) {
		if (receive_data(port, dc)) {
			writew(mask1, dc->reg_fcr);
			*toggle = !(*toggle);
		}

		if (read_iir & mask2) {
			if (receive_data(port, dc)) {
				writew(mask2, dc->reg_fcr);
				*toggle = !(*toggle);
			}
		}
	} else if (*toggle == 1 && read_iir & mask2) {
		if (receive_data(port, dc)) {
			writew(mask2, dc->reg_fcr);
			*toggle = !(*toggle);
		}

		if (read_iir & mask1) {
			if (receive_data(port, dc)) {
				writew(mask1, dc->reg_fcr);
				*toggle = !(*toggle);
			}
		}
	} else {
		dev_err(&dc->pdev->dev, "port out of sync!, toggle:%d\n",
			*toggle);
		return 0;
	}
	return 1;
}

/*
 * Handle uplink data, this is currently for the modem port
 * Return 1 - ok
 * Return 0 - toggle field are out of sync
 */
static int handle_data_ul(struct nozomi *dc, enum port_type port, u16 read_iir)
{
	u8 *toggle = &(dc->port[port].toggle_ul);

	if (*toggle == 0 && read_iir & MDM_UL1) {
		dc->last_ier &= ~MDM_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_data(port, dc)) {
			writew(MDM_UL1, dc->reg_fcr);
			dc->last_ier = dc->last_ier | MDM_UL;
			writew(dc->last_ier, dc->reg_ier);
			*toggle = !*toggle;
		}

		if (read_iir & MDM_UL2) {
			dc->last_ier &= ~MDM_UL;
			writew(dc->last_ier, dc->reg_ier);
			if (send_data(port, dc)) {
				writew(MDM_UL2, dc->reg_fcr);
				dc->last_ier = dc->last_ier | MDM_UL;
				writew(dc->last_ier, dc->reg_ier);
				*toggle = !*toggle;
			}
		}

	} else if (*toggle == 1 && read_iir & MDM_UL2) {
		dc->last_ier &= ~MDM_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_data(port, dc)) {
			writew(MDM_UL2, dc->reg_fcr);
			dc->last_ier = dc->last_ier | MDM_UL;
			writew(dc->last_ier, dc->reg_ier);
			*toggle = !*toggle;
		}

		if (read_iir & MDM_UL1) {
			dc->last_ier &= ~MDM_UL;
			writew(dc->last_ier, dc->reg_ier);
			if (send_data(port, dc)) {
				writew(MDM_UL1, dc->reg_fcr);
				dc->last_ier = dc->last_ier | MDM_UL;
				writew(dc->last_ier, dc->reg_ier);
				*toggle = !*toggle;
			}
		}
	} else {
		writew(read_iir & MDM_UL, dc->reg_fcr);
		dev_err(&dc->pdev->dev, "port out of sync!\n");
		return 0;
	}
	return 1;
}

static irqreturn_t interrupt_handler(int irq, void *dev_id)
{
	struct nozomi *dc = dev_id;
	unsigned int a;
	u16 read_iir;

	if (!dc)
		return IRQ_NONE;

	spin_lock(&dc->spin_mutex);
	read_iir = readw(dc->reg_iir);

	/* Card removed */
	if (read_iir == (u16)-1)
		goto none;
	/*
	 * Just handle interrupt enabled in IER
	 * (by masking with dc->last_ier)
	 */
	read_iir &= dc->last_ier;

	if (read_iir == 0)
		goto none;


	DBG4("%s irq:0x%04X, prev:0x%04X", interrupt2str(read_iir), read_iir,
		dc->last_ier);

	if (read_iir & RESET) {
		if (unlikely(!nozomi_read_config_table(dc))) {
			dc->last_ier = 0x0;
			writew(dc->last_ier, dc->reg_ier);
			dev_err(&dc->pdev->dev, "Could not read status from "
				"card, we should disable interface\n");
		} else {
			writew(RESET, dc->reg_fcr);
		}
		/* No more useful info if this was the reset interrupt. */
		goto exit_handler;
	}
	if (read_iir & CTRL_UL) {
		DBG1("CTRL_UL");
		dc->last_ier &= ~CTRL_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_flow_control(dc)) {
			writew(CTRL_UL, dc->reg_fcr);
			dc->last_ier = dc->last_ier | CTRL_UL;
			writew(dc->last_ier, dc->reg_ier);
		}
	}
	if (read_iir & CTRL_DL) {
		receive_flow_control(dc);
		writew(CTRL_DL, dc->reg_fcr);
	}
	if (read_iir & MDM_DL) {
		if (!handle_data_dl(dc, PORT_MDM,
				&(dc->port[PORT_MDM].toggle_dl), read_iir,
				MDM_DL1, MDM_DL2)) {
			dev_err(&dc->pdev->dev, "MDM_DL out of sync!\n");
			goto exit_handler;
		}
	}
	if (read_iir & MDM_UL) {
		if (!handle_data_ul(dc, PORT_MDM, read_iir)) {
			dev_err(&dc->pdev->dev, "MDM_UL out of sync!\n");
			goto exit_handler;
		}
	}
	if (read_iir & DIAG_DL) {
		if (!handle_data_dl(dc, PORT_DIAG,
				&(dc->port[PORT_DIAG].toggle_dl), read_iir,
				DIAG_DL1, DIAG_DL2)) {
			dev_err(&dc->pdev->dev, "DIAG_DL out of sync!\n");
			goto exit_handler;
		}
	}
	if (read_iir & DIAG_UL) {
		dc->last_ier &= ~DIAG_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_data(PORT_DIAG, dc)) {
			writew(DIAG_UL, dc->reg_fcr);
			dc->last_ier = dc->last_ier | DIAG_UL;
			writew(dc->last_ier, dc->reg_ier);
		}
	}
	if (read_iir & APP1_DL) {
		if (receive_data(PORT_APP1, dc))
			writew(APP1_DL, dc->reg_fcr);
	}
	if (read_iir & APP1_UL) {
		dc->last_ier &= ~APP1_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_data(PORT_APP1, dc)) {
			writew(APP1_UL, dc->reg_fcr);
			dc->last_ier = dc->last_ier | APP1_UL;
			writew(dc->last_ier, dc->reg_ier);
		}
	}
	if (read_iir & APP2_DL) {
		if (receive_data(PORT_APP2, dc))
			writew(APP2_DL, dc->reg_fcr);
	}
	if (read_iir & APP2_UL) {
		dc->last_ier &= ~APP2_UL;
		writew(dc->last_ier, dc->reg_ier);
		if (send_data(PORT_APP2, dc)) {
			writew(APP2_UL, dc->reg_fcr);
			dc->last_ier = dc->last_ier | APP2_UL;
			writew(dc->last_ier, dc->reg_ier);
		}
	}

exit_handler:
	spin_unlock(&dc->spin_mutex);

	for (a = 0; a < NOZOMI_MAX_PORTS; a++)
		if (test_and_clear_bit(a, &dc->flip))
			tty_flip_buffer_push(&dc->port[a].port);

	return IRQ_HANDLED;
none:
	spin_unlock(&dc->spin_mutex);
	return IRQ_NONE;
}

static void nozomi_get_card_type(struct nozomi *dc)
{
	int i;
	u32 size = 0;

	for (i = 0; i < 6; i++)
		size += pci_resource_len(dc->pdev, i);

	/* Assume card type F32_8 if no match */
	dc->card_type = size == 2048 ? F32_2 : F32_8;

	dev_info(&dc->pdev->dev, "Card type is: %d\n", dc->card_type);
}

static void nozomi_setup_private_data(struct nozomi *dc)
{
	void __iomem *offset = dc->base_addr + dc->card_type / 2;
	unsigned int i;

	dc->reg_fcr = (void __iomem *)(offset + R_FCR);
	dc->reg_iir = (void __iomem *)(offset + R_IIR);
	dc->reg_ier = (void __iomem *)(offset + R_IER);
	dc->last_ier = 0;
	dc->flip = 0;

	dc->port[PORT_MDM].token_dl = MDM_DL;
	dc->port[PORT_DIAG].token_dl = DIAG_DL;
	dc->port[PORT_APP1].token_dl = APP1_DL;
	dc->port[PORT_APP2].token_dl = APP2_DL;

	for (i = 0; i < MAX_PORT; i++)
		init_waitqueue_head(&dc->port[i].tty_wait);
}

static ssize_t card_type_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	const struct nozomi *dc = pci_get_drvdata(to_pci_dev(dev));

	return sprintf(buf, "%d\n", dc->card_type);
}
static DEVICE_ATTR(card_type, S_IRUGO, card_type_show, NULL);

static ssize_t open_ttys_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	const struct nozomi *dc = pci_get_drvdata(to_pci_dev(dev));

	return sprintf(buf, "%u\n", dc->open_ttys);
}
static DEVICE_ATTR(open_ttys, S_IRUGO, open_ttys_show, NULL);

static void make_sysfs_files(struct nozomi *dc)
{
	if (device_create_file(&dc->pdev->dev, &dev_attr_card_type))
		dev_err(&dc->pdev->dev,
			"Could not create sysfs file for card_type\n");
	if (device_create_file(&dc->pdev->dev, &dev_attr_open_ttys))
		dev_err(&dc->pdev->dev,
			"Could not create sysfs file for open_ttys\n");
}

static void remove_sysfs_files(struct nozomi *dc)
{
	device_remove_file(&dc->pdev->dev, &dev_attr_card_type);
	device_remove_file(&dc->pdev->dev, &dev_attr_open_ttys);
}

/* Allocate memory for one device */
static int nozomi_card_init(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	resource_size_t start;
	int ret;
	struct nozomi *dc = NULL;
	int ndev_idx;
	int i;

	dev_dbg(&pdev->dev, "Init, new card found\n");

	for (ndev_idx = 0; ndev_idx < ARRAY_SIZE(ndevs); ndev_idx++)
		if (!ndevs[ndev_idx])
			break;

	if (ndev_idx >= ARRAY_SIZE(ndevs)) {
		dev_err(&pdev->dev, "no free tty range for this card left\n");
		ret = -EIO;
		goto err;
	}

	dc = kzalloc(sizeof(struct nozomi), GFP_KERNEL);
	if (unlikely(!dc)) {
		dev_err(&pdev->dev, "Could not allocate memory\n");
		ret = -ENOMEM;
		goto err_free;
	}

	dc->pdev = pdev;

	ret = pci_enable_device(dc->pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI Device\n");
		goto err_free;
	}

	ret = pci_request_regions(dc->pdev, NOZOMI_NAME);
	if (ret) {
		dev_err(&pdev->dev, "I/O address 0x%04x already in use\n",
			(int) /* nozomi_private.io_addr */ 0);
		goto err_disable_device;
	}

	start = pci_resource_start(dc->pdev, 0);
	if (start == 0) {
		dev_err(&pdev->dev, "No I/O address for card detected\n");
		ret = -ENODEV;
		goto err_rel_regs;
	}

	/* Find out what card type it is */
	nozomi_get_card_type(dc);

	dc->base_addr = ioremap_nocache(start, dc->card_type);
	if (!dc->base_addr) {
		dev_err(&pdev->dev, "Unable to map card MMIO\n");
		ret = -ENODEV;
		goto err_rel_regs;
	}

	dc->send_buf = kmalloc(SEND_BUF_MAX, GFP_KERNEL);
	if (!dc->send_buf) {
		dev_err(&pdev->dev, "Could not allocate send buffer?\n");
		ret = -ENOMEM;
		goto err_free_sbuf;
	}

	for (i = PORT_MDM; i < MAX_PORT; i++) {
		if (kfifo_alloc(&dc->port[i].fifo_ul, FIFO_BUFFER_SIZE_UL,
					GFP_KERNEL)) {
			dev_err(&pdev->dev,
					"Could not allocate kfifo buffer\n");
			ret = -ENOMEM;
			goto err_free_kfifo;
		}
	}

	spin_lock_init(&dc->spin_mutex);

	nozomi_setup_private_data(dc);

	/* Disable all interrupts */
	dc->last_ier = 0;
	writew(dc->last_ier, dc->reg_ier);

	ret = request_irq(pdev->irq, &interrupt_handler, IRQF_SHARED,
			NOZOMI_NAME, dc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "can't request irq %d\n", pdev->irq);
		goto err_free_kfifo;
	}

	DBG1("base_addr: %p", dc->base_addr);

	make_sysfs_files(dc);

	dc->index_start = ndev_idx * MAX_PORT;
	ndevs[ndev_idx] = dc;

	pci_set_drvdata(pdev, dc);

	/* Enable RESET interrupt */
	dc->last_ier = RESET;
	iowrite16(dc->last_ier, dc->reg_ier);

	dc->state = NOZOMI_STATE_ENABLED;

	for (i = 0; i < MAX_PORT; i++) {
		struct device *tty_dev;
		struct port *port = &dc->port[i];
		port->dc = dc;
		tty_port_init(&port->port);
		port->port.ops = &noz_tty_port_ops;
		tty_dev = tty_port_register_device(&port->port, ntty_driver,
				dc->index_start + i, &pdev->dev);

		if (IS_ERR(tty_dev)) {
			ret = PTR_ERR(tty_dev);
			dev_err(&pdev->dev, "Could not allocate tty?\n");
			tty_port_destroy(&port->port);
			goto err_free_tty;
		}
	}

	return 0;

err_free_tty:
	for (i = 0; i < MAX_PORT; ++i) {
		tty_unregister_device(ntty_driver, dc->index_start + i);
		tty_port_destroy(&dc->port[i].port);
	}
err_free_kfifo:
	for (i = 0; i < MAX_PORT; i++)
		kfifo_free(&dc->port[i].fifo_ul);
err_free_sbuf:
	kfree(dc->send_buf);
	iounmap(dc->base_addr);
err_rel_regs:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_free:
	kfree(dc);
err:
	return ret;
}

static void tty_exit(struct nozomi *dc)
{
	unsigned int i;

	DBG1(" ");

	for (i = 0; i < MAX_PORT; ++i)
		tty_port_tty_hangup(&dc->port[i].port, false);

	/* Racy below - surely should wait for scheduled work to be done or
	   complete off a hangup method ? */
	while (dc->open_ttys)
		msleep(1);
	for (i = 0; i < MAX_PORT; ++i) {
		tty_unregister_device(ntty_driver, dc->index_start + i);
		tty_port_destroy(&dc->port[i].port);
	}
}

/* Deallocate memory for one device */
static void nozomi_card_exit(struct pci_dev *pdev)
{
	int i;
	struct ctrl_ul ctrl;
	struct nozomi *dc = pci_get_drvdata(pdev);

	/* Disable all interrupts */
	dc->last_ier = 0;
	writew(dc->last_ier, dc->reg_ier);

	tty_exit(dc);

	/* Send 0x0001, command card to resend the reset token.  */
	/* This is to get the reset when the module is reloaded. */
	ctrl.port = 0x00;
	ctrl.reserved = 0;
	ctrl.RTS = 0;
	ctrl.DTR = 1;
	DBG1("sending flow control 0x%04X", *((u16 *)&ctrl));

	/* Setup dc->reg addresses to we can use defines here */
	write_mem32(dc->port[PORT_CTRL].ul_addr[0], (u32 *)&ctrl, 2);
	writew(CTRL_UL, dc->reg_fcr);	/* push the token to the card. */

	remove_sysfs_files(dc);

	free_irq(pdev->irq, dc);

	for (i = 0; i < MAX_PORT; i++)
		kfifo_free(&dc->port[i].fifo_ul);

	kfree(dc->send_buf);

	iounmap(dc->base_addr);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	ndevs[dc->index_start / MAX_PORT] = NULL;

	kfree(dc);
}

static void set_rts(const struct tty_struct *tty, int rts)
{
	struct port *port = get_port_by_tty(tty);

	port->ctrl_ul.RTS = rts;
	port->update_flow_control = 1;
	enable_transmit_ul(PORT_CTRL, get_dc_by_tty(tty));
}

static void set_dtr(const struct tty_struct *tty, int dtr)
{
	struct port *port = get_port_by_tty(tty);

	DBG1("SETTING DTR index: %d, dtr: %d", tty->index, dtr);

	port->ctrl_ul.DTR = dtr;
	port->update_flow_control = 1;
	enable_transmit_ul(PORT_CTRL, get_dc_by_tty(tty));
}

/*
 * ----------------------------------------------------------------------------
 * TTY code
 * ----------------------------------------------------------------------------
 */

static int ntty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct port *port = get_port_by_tty(tty);
	struct nozomi *dc = get_dc_by_tty(tty);
	int ret;
	if (!port || !dc || dc->state != NOZOMI_STATE_READY)
		return -ENODEV;
	ret = tty_standard_install(driver, tty);
	if (ret == 0)
		tty->driver_data = port;
	return ret;
}

static void ntty_cleanup(struct tty_struct *tty)
{
	tty->driver_data = NULL;
}

static int ntty_activate(struct tty_port *tport, struct tty_struct *tty)
{
	struct port *port = container_of(tport, struct port, port);
	struct nozomi *dc = port->dc;
	unsigned long flags;

	DBG1("open: %d", port->token_dl);
	spin_lock_irqsave(&dc->spin_mutex, flags);
	dc->last_ier = dc->last_ier | port->token_dl;
	writew(dc->last_ier, dc->reg_ier);
	dc->open_ttys++;
	spin_unlock_irqrestore(&dc->spin_mutex, flags);
	printk("noz: activated %d: %p\n", tty->index, tport);
	return 0;
}

static int ntty_open(struct tty_struct *tty, struct file *filp)
{
	struct port *port = tty->driver_data;
	return tty_port_open(&port->port, tty, filp);
}

static void ntty_shutdown(struct tty_port *tport)
{
	struct port *port = container_of(tport, struct port, port);
	struct nozomi *dc = port->dc;
	unsigned long flags;

	DBG1("close: %d", port->token_dl);
	spin_lock_irqsave(&dc->spin_mutex, flags);
	dc->last_ier &= ~(port->token_dl);
	writew(dc->last_ier, dc->reg_ier);
	dc->open_ttys--;
	spin_unlock_irqrestore(&dc->spin_mutex, flags);
	printk("noz: shutdown %p\n", tport);
}

static void ntty_close(struct tty_struct *tty, struct file *filp)
{
	struct port *port = tty->driver_data;
	if (port)
		tty_port_close(&port->port, tty, filp);
}

static void ntty_hangup(struct tty_struct *tty)
{
	struct port *port = tty->driver_data;
	tty_port_hangup(&port->port);
}

/*
 * called when the userspace process writes to the tty (/dev/noz*).
 * Data is inserted into a fifo, which is then read and transferred to the modem.
 */
static int ntty_write(struct tty_struct *tty, const unsigned char *buffer,
		      int count)
{
	int rval = -EINVAL;
	struct nozomi *dc = get_dc_by_tty(tty);
	struct port *port = tty->driver_data;
	unsigned long flags;

	/* DBG1( "WRITEx: %d, index = %d", count, index); */

	if (!dc || !port)
		return -ENODEV;

	rval = kfifo_in(&port->fifo_ul, (unsigned char *)buffer, count);

	spin_lock_irqsave(&dc->spin_mutex, flags);
	/* CTS is only valid on the modem channel */
	if (port == &(dc->port[PORT_MDM])) {
		if (port->ctrl_dl.CTS) {
			DBG4("Enable interrupt");
			enable_transmit_ul(tty->index % MAX_PORT, dc);
		} else {
			dev_err(&dc->pdev->dev,
				"CTS not active on modem port?\n");
		}
	} else {
		enable_transmit_ul(tty->index % MAX_PORT, dc);
	}
	spin_unlock_irqrestore(&dc->spin_mutex, flags);

	return rval;
}

/*
 * Calculate how much is left in device
 * This method is called by the upper tty layer.
 *   #according to sources N_TTY.c it expects a value >= 0 and
 *    does not check for negative values.
 *
 * If the port is unplugged report lots of room and let the bits
 * dribble away so we don't block anything.
 */
static int ntty_write_room(struct tty_struct *tty)
{
	struct port *port = tty->driver_data;
	int room = 4096;
	const struct nozomi *dc = get_dc_by_tty(tty);

	if (dc)
		room = kfifo_avail(&port->fifo_ul);

	return room;
}

/* Gets io control parameters */
static int ntty_tiocmget(struct tty_struct *tty)
{
	const struct port *port = tty->driver_data;
	const struct ctrl_dl *ctrl_dl = &port->ctrl_dl;
	const struct ctrl_ul *ctrl_ul = &port->ctrl_ul;

	/* Note: these could change under us but it is not clear this
	   matters if so */
	return	(ctrl_ul->RTS ? TIOCM_RTS : 0) |
		(ctrl_ul->DTR ? TIOCM_DTR : 0) |
		(ctrl_dl->DCD ? TIOCM_CAR : 0) |
		(ctrl_dl->RI  ? TIOCM_RNG : 0) |
		(ctrl_dl->DSR ? TIOCM_DSR : 0) |
		(ctrl_dl->CTS ? TIOCM_CTS : 0);
}

/* Sets io controls parameters */
static int ntty_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	struct nozomi *dc = get_dc_by_tty(tty);
	unsigned long flags;

	spin_lock_irqsave(&dc->spin_mutex, flags);
	if (set & TIOCM_RTS)
		set_rts(tty, 1);
	else if (clear & TIOCM_RTS)
		set_rts(tty, 0);

	if (set & TIOCM_DTR)
		set_dtr(tty, 1);
	else if (clear & TIOCM_DTR)
		set_dtr(tty, 0);
	spin_unlock_irqrestore(&dc->spin_mutex, flags);

	return 0;
}

static int ntty_cflags_changed(struct port *port, unsigned long flags,
		struct async_icount *cprev)
{
	const struct async_icount cnow = port->tty_icount;
	int ret;

	ret =	((flags & TIOCM_RNG) && (cnow.rng != cprev->rng)) ||
		((flags & TIOCM_DSR) && (cnow.dsr != cprev->dsr)) ||
		((flags & TIOCM_CD)  && (cnow.dcd != cprev->dcd)) ||
		((flags & TIOCM_CTS) && (cnow.cts != cprev->cts));

	*cprev = cnow;

	return ret;
}

static int ntty_tiocgicount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct port *port = tty->driver_data;
	const struct async_icount cnow = port->tty_icount;

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;
	return 0;
}

static int ntty_ioctl(struct tty_struct *tty,
		      unsigned int cmd, unsigned long arg)
{
	struct port *port = tty->driver_data;
	int rval = -ENOIOCTLCMD;

	DBG1("******** IOCTL, cmd: %d", cmd);

	switch (cmd) {
	case TIOCMIWAIT: {
		struct async_icount cprev = port->tty_icount;

		rval = wait_event_interruptible(port->tty_wait,
				ntty_cflags_changed(port, arg, &cprev));
		break;
	}
	default:
		DBG1("ERR: 0x%08X, %d", cmd, cmd);
		break;
	}

	return rval;
}

/*
 * Called by the upper tty layer when tty buffers are ready
 * to receive data again after a call to throttle.
 */
static void ntty_unthrottle(struct tty_struct *tty)
{
	struct nozomi *dc = get_dc_by_tty(tty);
	unsigned long flags;

	DBG1("UNTHROTTLE");
	spin_lock_irqsave(&dc->spin_mutex, flags);
	enable_transmit_dl(tty->index % MAX_PORT, dc);
	set_rts(tty, 1);

	spin_unlock_irqrestore(&dc->spin_mutex, flags);
}

/*
 * Called by the upper tty layer when the tty buffers are almost full.
 * The driver should stop send more data.
 */
static void ntty_throttle(struct tty_struct *tty)
{
	struct nozomi *dc = get_dc_by_tty(tty);
	unsigned long flags;

	DBG1("THROTTLE");
	spin_lock_irqsave(&dc->spin_mutex, flags);
	set_rts(tty, 0);
	spin_unlock_irqrestore(&dc->spin_mutex, flags);
}

/* Returns number of chars in buffer, called by tty layer */
static s32 ntty_chars_in_buffer(struct tty_struct *tty)
{
	struct port *port = tty->driver_data;
	struct nozomi *dc = get_dc_by_tty(tty);
	s32 rval = 0;

	if (unlikely(!dc || !port)) {
		goto exit_in_buffer;
	}

	rval = kfifo_len(&port->fifo_ul);

exit_in_buffer:
	return rval;
}

static const struct tty_port_operations noz_tty_port_ops = {
	.activate = ntty_activate,
	.shutdown = ntty_shutdown,
};

static const struct tty_operations tty_ops = {
	.ioctl = ntty_ioctl,
	.open = ntty_open,
	.close = ntty_close,
	.hangup = ntty_hangup,
	.write = ntty_write,
	.write_room = ntty_write_room,
	.unthrottle = ntty_unthrottle,
	.throttle = ntty_throttle,
	.chars_in_buffer = ntty_chars_in_buffer,
	.tiocmget = ntty_tiocmget,
	.tiocmset = ntty_tiocmset,
	.get_icount = ntty_tiocgicount,
	.install = ntty_install,
	.cleanup = ntty_cleanup,
};

/* Module initialization */
static struct pci_driver nozomi_driver = {
	.name = NOZOMI_NAME,
	.id_table = nozomi_pci_tbl,
	.probe = nozomi_card_init,
	.remove = nozomi_card_exit,
};

static __init int nozomi_init(void)
{
	int ret;

	printk(KERN_INFO "Initializing %s\n", VERSION_STRING);

	ntty_driver = alloc_tty_driver(NTTY_TTY_MAXMINORS);
	if (!ntty_driver)
		return -ENOMEM;

	ntty_driver->driver_name = NOZOMI_NAME_TTY;
	ntty_driver->name = "noz";
	ntty_driver->major = 0;
	ntty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	ntty_driver->subtype = SERIAL_TYPE_NORMAL;
	ntty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	ntty_driver->init_termios = tty_std_termios;
	ntty_driver->init_termios.c_cflag = B115200 | CS8 | CREAD | \
						HUPCL | CLOCAL;
	ntty_driver->init_termios.c_ispeed = 115200;
	ntty_driver->init_termios.c_ospeed = 115200;
	tty_set_operations(ntty_driver, &tty_ops);

	ret = tty_register_driver(ntty_driver);
	if (ret) {
		printk(KERN_ERR "Nozomi: failed to register ntty driver\n");
		goto free_tty;
	}

	ret = pci_register_driver(&nozomi_driver);
	if (ret) {
		printk(KERN_ERR "Nozomi: can't register pci driver\n");
		goto unr_tty;
	}

	return 0;
unr_tty:
	tty_unregister_driver(ntty_driver);
free_tty:
	put_tty_driver(ntty_driver);
	return ret;
}

static __exit void nozomi_exit(void)
{
	printk(KERN_INFO "Unloading %s\n", DRIVER_DESC);
	pci_unregister_driver(&nozomi_driver);
	tty_unregister_driver(ntty_driver);
	put_tty_driver(ntty_driver);
}

module_init(nozomi_init);
module_exit(nozomi_exit);

module_param(debug, int, S_IRUGO | S_IWUSR);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
