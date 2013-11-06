/*****************************************************************************/

/*
 *    yam.c  -- YAM radio modem driver.
 *
 *      Copyright (C) 1998 Frederic Rible F1OAT (frible@teaser.fr)
 *      Adapted from baycom.c driver written by Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 *
 *  History:
 *   0.0 F1OAT 06.06.98  Begin of work with baycom.c source code V 0.3
 *   0.1 F1OAT 07.06.98  Add timer polling routine for channel arbitration
 *   0.2 F6FBB 08.06.98  Added delay after FPGA programming
 *   0.3 F6FBB 29.07.98  Delayed PTT implementation for dupmode=2
 *   0.4 F6FBB 30.07.98  Added TxTail, Slottime and Persistence
 *   0.5 F6FBB 01.08.98  Shared IRQs, /proc/net and network statistics
 *   0.6 F6FBB 25.08.98  Added 1200Bds format
 *   0.7 F6FBB 12.09.98  Added to the kernel configuration
 *   0.8 F6FBB 14.10.98  Fixed slottime/persistence timing bug
 *       OK1ZIA 2.09.01  Fixed "kfree_skb on hard IRQ" 
 *                       using dev_kfree_skb_any(). (important in 2.4 kernel)
 *   
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/random.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/ax25.h>

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/net_namespace.h>

#include <asm/uaccess.h>
#include <linux/init.h>

#include <linux/yam.h>

/* --------------------------------------------------------------------- */

static const char yam_drvname[] = "yam";
static const char yam_drvinfo[] __initconst = KERN_INFO \
	"YAM driver version 0.8 by F1OAT/F6FBB\n";

/* --------------------------------------------------------------------- */

#define FIRMWARE_9600	"yam/9600.bin"
#define FIRMWARE_1200	"yam/1200.bin"

#define YAM_9600	1
#define YAM_1200	2

#define NR_PORTS	4
#define YAM_MAGIC	0xF10A7654

/* Transmitter states */

#define TX_OFF		0
#define TX_HEAD		1
#define TX_DATA		2
#define TX_CRC1		3
#define TX_CRC2		4
#define TX_TAIL		5

#define YAM_MAX_FRAME	1024

#define DEFAULT_BITRATE	9600			/* bps */
#define DEFAULT_HOLDD	10			/* sec */
#define DEFAULT_TXD	300			/* ms */
#define DEFAULT_TXTAIL	10			/* ms */
#define DEFAULT_SLOT	100			/* ms */
#define DEFAULT_PERS	64			/* 0->255 */

struct yam_port {
	int magic;
	int bitrate;
	int baudrate;
	int iobase;
	int irq;
	int dupmode;

	struct net_device *dev;

	int nb_rxint;
	int nb_mdint;

	/* Parameters section */

	int txd;				/* tx delay */
	int holdd;				/* duplex ptt delay */
	int txtail;				/* txtail delay */
	int slot;				/* slottime */
	int pers;				/* persistence */

	/* Tx section */

	int tx_state;
	int tx_count;
	int slotcnt;
	unsigned char tx_buf[YAM_MAX_FRAME];
	int tx_len;
	int tx_crcl, tx_crch;
	struct sk_buff_head send_queue;		/* Packets awaiting transmission */

	/* Rx section */

	int dcd;
	unsigned char rx_buf[YAM_MAX_FRAME];
	int rx_len;
	int rx_crcl, rx_crch;
};

struct yam_mcs {
	unsigned char bits[YAM_FPGA_SIZE];
	int bitrate;
	struct yam_mcs *next;
};

static struct net_device *yam_devs[NR_PORTS];

static struct yam_mcs *yam_data;

static DEFINE_TIMER(yam_timer, NULL, 0, 0);

/* --------------------------------------------------------------------- */

#define RBR(iobase)	(iobase+0)
#define THR(iobase)	(iobase+0)
#define IER(iobase)	(iobase+1)
#define IIR(iobase)	(iobase+2)
#define FCR(iobase)	(iobase+2)
#define LCR(iobase)	(iobase+3)
#define MCR(iobase)	(iobase+4)
#define LSR(iobase)	(iobase+5)
#define MSR(iobase)	(iobase+6)
#define SCR(iobase)	(iobase+7)
#define DLL(iobase)	(iobase+0)
#define DLM(iobase)	(iobase+1)

#define YAM_EXTENT	8

/* Interrupt Identification Register Bit Masks */
#define IIR_NOPEND	1
#define IIR_MSR		0
#define IIR_TX		2
#define IIR_RX		4
#define IIR_LSR		6
#define IIR_TIMEOUT	12			/* Fifo mode only */

#define IIR_MASK	0x0F

/* Interrupt Enable Register Bit Masks */
#define IER_RX		1			/* enable rx interrupt */
#define IER_TX		2			/* enable tx interrupt */
#define IER_LSR		4			/* enable line status interrupts */
#define IER_MSR		8			/* enable modem status interrupts */

/* Modem Control Register Bit Masks */
#define MCR_DTR		0x01			/* DTR output */
#define MCR_RTS		0x02			/* RTS output */
#define MCR_OUT1	0x04			/* OUT1 output (not accessible in RS232) */
#define MCR_OUT2	0x08			/* Master Interrupt enable (must be set on PCs) */
#define MCR_LOOP	0x10			/* Loopback enable */

/* Modem Status Register Bit Masks */
#define MSR_DCTS	0x01			/* Delta CTS input */
#define MSR_DDSR	0x02			/* Delta DSR */
#define MSR_DRIN	0x04			/* Delta RI */
#define MSR_DDCD	0x08			/* Delta DCD */
#define MSR_CTS		0x10			/* CTS input */
#define MSR_DSR		0x20			/* DSR input */
#define MSR_RING	0x40			/* RI  input */
#define MSR_DCD		0x80			/* DCD input */

/* line status register bit mask */
#define LSR_RXC		0x01
#define LSR_OE		0x02
#define LSR_PE		0x04
#define LSR_FE		0x08
#define LSR_BREAK	0x10
#define LSR_THRE	0x20
#define LSR_TSRE	0x40

/* Line Control Register Bit Masks */
#define LCR_DLAB	0x80
#define LCR_BREAK	0x40
#define LCR_PZERO	0x28
#define LCR_PEVEN	0x18
#define LCR_PODD	0x08
#define LCR_STOP1	0x00
#define LCR_STOP2	0x04
#define LCR_BIT5	0x00
#define LCR_BIT6	0x02
#define LCR_BIT7	0x01
#define LCR_BIT8	0x03

/* YAM Modem <-> UART Port mapping */

#define TX_RDY		MSR_DCTS		/* transmitter ready to send */
#define RX_DCD		MSR_DCD			/* carrier detect */
#define RX_FLAG		MSR_RING		/* hdlc flag received */
#define FPGA_DONE	MSR_DSR			/* FPGA is configured */
#define PTT_ON		(MCR_RTS|MCR_OUT2)	/* activate PTT */
#define PTT_OFF		(MCR_DTR|MCR_OUT2)	/* release PTT */

#define ENABLE_RXINT	IER_RX			/* enable uart rx interrupt during rx */
#define ENABLE_TXINT	IER_MSR			/* enable uart ms interrupt during tx */
#define ENABLE_RTXINT	(IER_RX|IER_MSR)	/* full duplex operations */


/*************************************************************************
* CRC Tables
************************************************************************/

static const unsigned char chktabl[256] =
{0x00, 0x89, 0x12, 0x9b, 0x24, 0xad, 0x36, 0xbf, 0x48, 0xc1, 0x5a, 0xd3, 0x6c, 0xe5, 0x7e,
 0xf7, 0x81, 0x08, 0x93, 0x1a, 0xa5, 0x2c, 0xb7, 0x3e, 0xc9, 0x40, 0xdb, 0x52, 0xed, 0x64,
 0xff, 0x76, 0x02, 0x8b, 0x10, 0x99, 0x26, 0xaf, 0x34, 0xbd, 0x4a, 0xc3, 0x58, 0xd1, 0x6e,
 0xe7, 0x7c, 0xf5, 0x83, 0x0a, 0x91, 0x18, 0xa7, 0x2e, 0xb5, 0x3c, 0xcb, 0x42, 0xd9, 0x50,
 0xef, 0x66, 0xfd, 0x74, 0x04, 0x8d, 0x16, 0x9f, 0x20, 0xa9, 0x32, 0xbb, 0x4c, 0xc5, 0x5e,
 0xd7, 0x68, 0xe1, 0x7a, 0xf3, 0x85, 0x0c, 0x97, 0x1e, 0xa1, 0x28, 0xb3, 0x3a, 0xcd, 0x44,
 0xdf, 0x56, 0xe9, 0x60, 0xfb, 0x72, 0x06, 0x8f, 0x14, 0x9d, 0x22, 0xab, 0x30, 0xb9, 0x4e,
 0xc7, 0x5c, 0xd5, 0x6a, 0xe3, 0x78, 0xf1, 0x87, 0x0e, 0x95, 0x1c, 0xa3, 0x2a, 0xb1, 0x38,
 0xcf, 0x46, 0xdd, 0x54, 0xeb, 0x62, 0xf9, 0x70, 0x08, 0x81, 0x1a, 0x93, 0x2c, 0xa5, 0x3e,
 0xb7, 0x40, 0xc9, 0x52, 0xdb, 0x64, 0xed, 0x76, 0xff, 0x89, 0x00, 0x9b, 0x12, 0xad, 0x24,
 0xbf, 0x36, 0xc1, 0x48, 0xd3, 0x5a, 0xe5, 0x6c, 0xf7, 0x7e, 0x0a, 0x83, 0x18, 0x91, 0x2e,
 0xa7, 0x3c, 0xb5, 0x42, 0xcb, 0x50, 0xd9, 0x66, 0xef, 0x74, 0xfd, 0x8b, 0x02, 0x99, 0x10,
 0xaf, 0x26, 0xbd, 0x34, 0xc3, 0x4a, 0xd1, 0x58, 0xe7, 0x6e, 0xf5, 0x7c, 0x0c, 0x85, 0x1e,
 0x97, 0x28, 0xa1, 0x3a, 0xb3, 0x44, 0xcd, 0x56, 0xdf, 0x60, 0xe9, 0x72, 0xfb, 0x8d, 0x04,
 0x9f, 0x16, 0xa9, 0x20, 0xbb, 0x32, 0xc5, 0x4c, 0xd7, 0x5e, 0xe1, 0x68, 0xf3, 0x7a, 0x0e,
 0x87, 0x1c, 0x95, 0x2a, 0xa3, 0x38, 0xb1, 0x46, 0xcf, 0x54, 0xdd, 0x62, 0xeb, 0x70, 0xf9,
 0x8f, 0x06, 0x9d, 0x14, 0xab, 0x22, 0xb9, 0x30, 0xc7, 0x4e, 0xd5, 0x5c, 0xe3, 0x6a, 0xf1,
 0x78};
static const unsigned char chktabh[256] =
{0x00, 0x11, 0x23, 0x32, 0x46, 0x57, 0x65, 0x74, 0x8c, 0x9d, 0xaf, 0xbe, 0xca, 0xdb, 0xe9,
 0xf8, 0x10, 0x01, 0x33, 0x22, 0x56, 0x47, 0x75, 0x64, 0x9c, 0x8d, 0xbf, 0xae, 0xda, 0xcb,
 0xf9, 0xe8, 0x21, 0x30, 0x02, 0x13, 0x67, 0x76, 0x44, 0x55, 0xad, 0xbc, 0x8e, 0x9f, 0xeb,
 0xfa, 0xc8, 0xd9, 0x31, 0x20, 0x12, 0x03, 0x77, 0x66, 0x54, 0x45, 0xbd, 0xac, 0x9e, 0x8f,
 0xfb, 0xea, 0xd8, 0xc9, 0x42, 0x53, 0x61, 0x70, 0x04, 0x15, 0x27, 0x36, 0xce, 0xdf, 0xed,
 0xfc, 0x88, 0x99, 0xab, 0xba, 0x52, 0x43, 0x71, 0x60, 0x14, 0x05, 0x37, 0x26, 0xde, 0xcf,
 0xfd, 0xec, 0x98, 0x89, 0xbb, 0xaa, 0x63, 0x72, 0x40, 0x51, 0x25, 0x34, 0x06, 0x17, 0xef,
 0xfe, 0xcc, 0xdd, 0xa9, 0xb8, 0x8a, 0x9b, 0x73, 0x62, 0x50, 0x41, 0x35, 0x24, 0x16, 0x07,
 0xff, 0xee, 0xdc, 0xcd, 0xb9, 0xa8, 0x9a, 0x8b, 0x84, 0x95, 0xa7, 0xb6, 0xc2, 0xd3, 0xe1,
 0xf0, 0x08, 0x19, 0x2b, 0x3a, 0x4e, 0x5f, 0x6d, 0x7c, 0x94, 0x85, 0xb7, 0xa6, 0xd2, 0xc3,
 0xf1, 0xe0, 0x18, 0x09, 0x3b, 0x2a, 0x5e, 0x4f, 0x7d, 0x6c, 0xa5, 0xb4, 0x86, 0x97, 0xe3,
 0xf2, 0xc0, 0xd1, 0x29, 0x38, 0x0a, 0x1b, 0x6f, 0x7e, 0x4c, 0x5d, 0xb5, 0xa4, 0x96, 0x87,
 0xf3, 0xe2, 0xd0, 0xc1, 0x39, 0x28, 0x1a, 0x0b, 0x7f, 0x6e, 0x5c, 0x4d, 0xc6, 0xd7, 0xe5,
 0xf4, 0x80, 0x91, 0xa3, 0xb2, 0x4a, 0x5b, 0x69, 0x78, 0x0c, 0x1d, 0x2f, 0x3e, 0xd6, 0xc7,
 0xf5, 0xe4, 0x90, 0x81, 0xb3, 0xa2, 0x5a, 0x4b, 0x79, 0x68, 0x1c, 0x0d, 0x3f, 0x2e, 0xe7,
 0xf6, 0xc4, 0xd5, 0xa1, 0xb0, 0x82, 0x93, 0x6b, 0x7a, 0x48, 0x59, 0x2d, 0x3c, 0x0e, 0x1f,
 0xf7, 0xe6, 0xd4, 0xc5, 0xb1, 0xa0, 0x92, 0x83, 0x7b, 0x6a, 0x58, 0x49, 0x3d, 0x2c, 0x1e,
 0x0f};

/*************************************************************************
* FPGA functions
************************************************************************/

static void delay(int ms)
{
	unsigned long timeout = jiffies + ((ms * HZ) / 1000);
	while (time_before(jiffies, timeout))
		cpu_relax();
}

/*
 * reset FPGA
 */

static void fpga_reset(int iobase)
{
	outb(0, IER(iobase));
	outb(LCR_DLAB | LCR_BIT5, LCR(iobase));
	outb(1, DLL(iobase));
	outb(0, DLM(iobase));

	outb(LCR_BIT5, LCR(iobase));
	inb(LSR(iobase));
	inb(MSR(iobase));
	/* turn off FPGA supply voltage */
	outb(MCR_OUT1 | MCR_OUT2, MCR(iobase));
	delay(100);
	/* turn on FPGA supply voltage again */
	outb(MCR_DTR | MCR_RTS | MCR_OUT1 | MCR_OUT2, MCR(iobase));
	delay(100);
}

/*
 * send one byte to FPGA
 */

static int fpga_write(int iobase, unsigned char wrd)
{
	unsigned char bit;
	int k;
	unsigned long timeout = jiffies + HZ / 10;

	for (k = 0; k < 8; k++) {
		bit = (wrd & 0x80) ? (MCR_RTS | MCR_DTR) : MCR_DTR;
		outb(bit | MCR_OUT1 | MCR_OUT2, MCR(iobase));
		wrd <<= 1;
		outb(0xfc, THR(iobase));
		while ((inb(LSR(iobase)) & LSR_TSRE) == 0)
			if (time_after(jiffies, timeout))
				return -1;
	}

	return 0;
}

/*
 * predef should be 0 for loading user defined mcs
 * predef should be YAM_1200 for loading predef 1200 mcs
 * predef should be YAM_9600 for loading predef 9600 mcs
 */
static unsigned char *add_mcs(unsigned char *bits, int bitrate,
			      unsigned int predef)
{
	const char *fw_name[2] = {FIRMWARE_9600, FIRMWARE_1200};
	const struct firmware *fw;
	struct platform_device *pdev;
	struct yam_mcs *p;
	int err;

	switch (predef) {
	case 0:
		fw = NULL;
		break;
	case YAM_1200:
	case YAM_9600:
		predef--;
		pdev = platform_device_register_simple("yam", 0, NULL, 0);
		if (IS_ERR(pdev)) {
			printk(KERN_ERR "yam: Failed to register firmware\n");
			return NULL;
		}
		err = request_firmware(&fw, fw_name[predef], &pdev->dev);
		platform_device_unregister(pdev);
		if (err) {
			printk(KERN_ERR "Failed to load firmware \"%s\"\n",
			       fw_name[predef]);
			return NULL;
		}
		if (fw->size != YAM_FPGA_SIZE) {
			printk(KERN_ERR "Bogus length %zu in firmware \"%s\"\n",
			       fw->size, fw_name[predef]);
			release_firmware(fw);
			return NULL;
		}
		bits = (unsigned char *)fw->data;
		break;
	default:
		printk(KERN_ERR "yam: Invalid predef number %u\n", predef);
		return NULL;
	}

	/* If it already exists, replace the bit data */
	p = yam_data;
	while (p) {
		if (p->bitrate == bitrate) {
			memcpy(p->bits, bits, YAM_FPGA_SIZE);
			goto out;
		}
		p = p->next;
	}

	/* Allocate a new mcs */
	if ((p = kmalloc(sizeof(struct yam_mcs), GFP_KERNEL)) == NULL) {
		release_firmware(fw);
		return NULL;
	}
	memcpy(p->bits, bits, YAM_FPGA_SIZE);
	p->bitrate = bitrate;
	p->next = yam_data;
	yam_data = p;
 out:
	release_firmware(fw);
	return p->bits;
}

static unsigned char *get_mcs(int bitrate)
{
	struct yam_mcs *p;

	p = yam_data;
	while (p) {
		if (p->bitrate == bitrate)
			return p->bits;
		p = p->next;
	}

	/* Load predefined mcs data */
	switch (bitrate) {
	case 1200:
		/* setting predef as YAM_1200 for loading predef 1200 mcs */
		return add_mcs(NULL, bitrate, YAM_1200);
	default:
		/* setting predef as YAM_9600 for loading predef 9600 mcs */
		return add_mcs(NULL, bitrate, YAM_9600);
	}
}

/*
 * download bitstream to FPGA
 * data is contained in bits[] array in yam1200.h resp. yam9600.h
 */

static int fpga_download(int iobase, int bitrate)
{
	int i, rc;
	unsigned char *pbits;

	pbits = get_mcs(bitrate);
	if (pbits == NULL)
		return -1;

	fpga_reset(iobase);
	for (i = 0; i < YAM_FPGA_SIZE; i++) {
		if (fpga_write(iobase, pbits[i])) {
			printk(KERN_ERR "yam: error in write cycle\n");
			return -1;			/* write... */
		}
	}

	fpga_write(iobase, 0xFF);
	rc = inb(MSR(iobase));		/* check DONE signal */

	/* Needed for some hardwares */
	delay(50);

	return (rc & MSR_DSR) ? 0 : -1;
}


/************************************************************************
* Serial port init 
************************************************************************/

static void yam_set_uart(struct net_device *dev)
{
	struct yam_port *yp = netdev_priv(dev);
	int divisor = 115200 / yp->baudrate;

	outb(0, IER(dev->base_addr));
	outb(LCR_DLAB | LCR_BIT8, LCR(dev->base_addr));
	outb(divisor, DLL(dev->base_addr));
	outb(0, DLM(dev->base_addr));
	outb(LCR_BIT8, LCR(dev->base_addr));
	outb(PTT_OFF, MCR(dev->base_addr));
	outb(0x00, FCR(dev->base_addr));

	/* Flush pending irq */

	inb(RBR(dev->base_addr));
	inb(MSR(dev->base_addr));

	/* Enable rx irq */

	outb(ENABLE_RTXINT, IER(dev->base_addr));
}


/* --------------------------------------------------------------------- */

enum uart {
	c_uart_unknown, c_uart_8250,
	c_uart_16450, c_uart_16550, c_uart_16550A
};

static const char *uart_str[] =
{"unknown", "8250", "16450", "16550", "16550A"};

static enum uart yam_check_uart(unsigned int iobase)
{
	unsigned char b1, b2, b3;
	enum uart u;
	enum uart uart_tab[] =
	{c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A};

	b1 = inb(MCR(iobase));
	outb(b1 | 0x10, MCR(iobase));	/* loopback mode */
	b2 = inb(MSR(iobase));
	outb(0x1a, MCR(iobase));
	b3 = inb(MSR(iobase)) & 0xf0;
	outb(b1, MCR(iobase));		/* restore old values */
	outb(b2, MSR(iobase));
	if (b3 != 0x90)
		return c_uart_unknown;
	inb(RBR(iobase));
	inb(RBR(iobase));
	outb(0x01, FCR(iobase));	/* enable FIFOs */
	u = uart_tab[(inb(IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, SCR(iobase));
		b1 = inb(SCR(iobase));
		outb(0xa5, SCR(iobase));
		b2 = inb(SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5))
			u = c_uart_8250;
	}
	return u;
}

/******************************************************************************
* Rx Section
******************************************************************************/
static inline void yam_rx_flag(struct net_device *dev, struct yam_port *yp)
{
	if (yp->dcd && yp->rx_len >= 3 && yp->rx_len < YAM_MAX_FRAME) {
		int pkt_len = yp->rx_len - 2 + 1;	/* -CRC + kiss */
		struct sk_buff *skb;

		if ((yp->rx_crch & yp->rx_crcl) != 0xFF) {
			/* Bad crc */
		} else {
			if (!(skb = dev_alloc_skb(pkt_len))) {
				printk(KERN_WARNING "%s: memory squeeze, dropping packet\n", dev->name);
				++dev->stats.rx_dropped;
			} else {
				unsigned char *cp;
				cp = skb_put(skb, pkt_len);
				*cp++ = 0;		/* KISS kludge */
				memcpy(cp, yp->rx_buf, pkt_len - 1);
				skb->protocol = ax25_type_trans(skb, dev);
				netif_rx(skb);
				++dev->stats.rx_packets;
			}
		}
	}
	yp->rx_len = 0;
	yp->rx_crcl = 0x21;
	yp->rx_crch = 0xf3;
}

static inline void yam_rx_byte(struct net_device *dev, struct yam_port *yp, unsigned char rxb)
{
	if (yp->rx_len < YAM_MAX_FRAME) {
		unsigned char c = yp->rx_crcl;
		yp->rx_crcl = (chktabl[c] ^ yp->rx_crch);
		yp->rx_crch = (chktabh[c] ^ rxb);
		yp->rx_buf[yp->rx_len++] = rxb;
	}
}

/********************************************************************************
* TX Section
********************************************************************************/

static void ptt_on(struct net_device *dev)
{
	outb(PTT_ON, MCR(dev->base_addr));
}

static void ptt_off(struct net_device *dev)
{
	outb(PTT_OFF, MCR(dev->base_addr));
}

static netdev_tx_t yam_send_packet(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct yam_port *yp = netdev_priv(dev);

	skb_queue_tail(&yp->send_queue, skb);
	dev->trans_start = jiffies;
	return NETDEV_TX_OK;
}

static void yam_start_tx(struct net_device *dev, struct yam_port *yp)
{
	if ((yp->tx_state == TX_TAIL) || (yp->txd == 0))
		yp->tx_count = 1;
	else
		yp->tx_count = (yp->bitrate * yp->txd) / 8000;
	yp->tx_state = TX_HEAD;
	ptt_on(dev);
}

static void yam_arbitrate(struct net_device *dev)
{
	struct yam_port *yp = netdev_priv(dev);

	if (yp->magic != YAM_MAGIC || yp->tx_state != TX_OFF ||
	    skb_queue_empty(&yp->send_queue))
		return;
	/* tx_state is TX_OFF and there is data to send */

	if (yp->dupmode) {
		/* Full duplex mode, don't wait */
		yam_start_tx(dev, yp);
		return;
	}
	if (yp->dcd) {
		/* DCD on, wait slotime ... */
		yp->slotcnt = yp->slot / 10;
		return;
	}
	/* Is slottime passed ? */
	if ((--yp->slotcnt) > 0)
		return;

	yp->slotcnt = yp->slot / 10;

	/* is random > persist ? */
	if ((prandom_u32() % 256) > yp->pers)
		return;

	yam_start_tx(dev, yp);
}

static void yam_dotimer(unsigned long dummy)
{
	int i;

	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = yam_devs[i];
		if (dev && netif_running(dev))
			yam_arbitrate(dev);
	}
	yam_timer.expires = jiffies + HZ / 100;
	add_timer(&yam_timer);
}

static void yam_tx_byte(struct net_device *dev, struct yam_port *yp)
{
	struct sk_buff *skb;
	unsigned char b, temp;

	switch (yp->tx_state) {
	case TX_OFF:
		break;
	case TX_HEAD:
		if (--yp->tx_count <= 0) {
			if (!(skb = skb_dequeue(&yp->send_queue))) {
				ptt_off(dev);
				yp->tx_state = TX_OFF;
				break;
			}
			yp->tx_state = TX_DATA;
			if (skb->data[0] != 0) {
/*                              do_kiss_params(s, skb->data, skb->len); */
				dev_kfree_skb_any(skb);
				break;
			}
			yp->tx_len = skb->len - 1;	/* strip KISS byte */
			if (yp->tx_len >= YAM_MAX_FRAME || yp->tx_len < 2) {
        			dev_kfree_skb_any(skb);
				break;
			}
			skb_copy_from_linear_data_offset(skb, 1,
							 yp->tx_buf,
							 yp->tx_len);
			dev_kfree_skb_any(skb);
			yp->tx_count = 0;
			yp->tx_crcl = 0x21;
			yp->tx_crch = 0xf3;
			yp->tx_state = TX_DATA;
		}
		break;
	case TX_DATA:
		b = yp->tx_buf[yp->tx_count++];
		outb(b, THR(dev->base_addr));
		temp = yp->tx_crcl;
		yp->tx_crcl = chktabl[temp] ^ yp->tx_crch;
		yp->tx_crch = chktabh[temp] ^ b;
		if (yp->tx_count >= yp->tx_len) {
			yp->tx_state = TX_CRC1;
		}
		break;
	case TX_CRC1:
		yp->tx_crch = chktabl[yp->tx_crcl] ^ yp->tx_crch;
		yp->tx_crcl = chktabh[yp->tx_crcl] ^ chktabl[yp->tx_crch] ^ 0xff;
		outb(yp->tx_crcl, THR(dev->base_addr));
		yp->tx_state = TX_CRC2;
		break;
	case TX_CRC2:
		outb(chktabh[yp->tx_crch] ^ 0xFF, THR(dev->base_addr));
		if (skb_queue_empty(&yp->send_queue)) {
			yp->tx_count = (yp->bitrate * yp->txtail) / 8000;
			if (yp->dupmode == 2)
				yp->tx_count += (yp->bitrate * yp->holdd) / 8;
			if (yp->tx_count == 0)
				yp->tx_count = 1;
			yp->tx_state = TX_TAIL;
		} else {
			yp->tx_count = 1;
			yp->tx_state = TX_HEAD;
		}
		++dev->stats.tx_packets;
		break;
	case TX_TAIL:
		if (--yp->tx_count <= 0) {
			yp->tx_state = TX_OFF;
			ptt_off(dev);
		}
		break;
	}
}

/***********************************************************************************
* ISR routine
************************************************************************************/

static irqreturn_t yam_interrupt(int irq, void *dev_id)
{
	struct net_device *dev;
	struct yam_port *yp;
	unsigned char iir;
	int counter = 100;
	int i;
	int handled = 0;

	for (i = 0; i < NR_PORTS; i++) {
		dev = yam_devs[i];
		yp = netdev_priv(dev);

		if (!netif_running(dev))
			continue;

		while ((iir = IIR_MASK & inb(IIR(dev->base_addr))) != IIR_NOPEND) {
			unsigned char msr = inb(MSR(dev->base_addr));
			unsigned char lsr = inb(LSR(dev->base_addr));
			unsigned char rxb;

			handled = 1;

			if (lsr & LSR_OE)
				++dev->stats.rx_fifo_errors;

			yp->dcd = (msr & RX_DCD) ? 1 : 0;

			if (--counter <= 0) {
				printk(KERN_ERR "%s: too many irq iir=%d\n",
						dev->name, iir);
				goto out;
			}
			if (msr & TX_RDY) {
				++yp->nb_mdint;
				yam_tx_byte(dev, yp);
			}
			if (lsr & LSR_RXC) {
				++yp->nb_rxint;
				rxb = inb(RBR(dev->base_addr));
				if (msr & RX_FLAG)
					yam_rx_flag(dev, yp);
				else
					yam_rx_byte(dev, yp, rxb);
			}
		}
	}
out:
	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_PROC_FS

static void *yam_seq_start(struct seq_file *seq, loff_t *pos)
{
	return (*pos < NR_PORTS) ? yam_devs[*pos] : NULL;
}

static void *yam_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return (*pos < NR_PORTS) ? yam_devs[*pos] : NULL;
}

static void yam_seq_stop(struct seq_file *seq, void *v)
{
}

static int yam_seq_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = v;
	const struct yam_port *yp = netdev_priv(dev);

	seq_printf(seq, "Device %s\n", dev->name);
	seq_printf(seq, "  Up       %d\n", netif_running(dev));
	seq_printf(seq, "  Speed    %u\n", yp->bitrate);
	seq_printf(seq, "  IoBase   0x%x\n", yp->iobase);
	seq_printf(seq, "  BaudRate %u\n", yp->baudrate);
	seq_printf(seq, "  IRQ      %u\n", yp->irq);
	seq_printf(seq, "  TxState  %u\n", yp->tx_state);
	seq_printf(seq, "  Duplex   %u\n", yp->dupmode);
	seq_printf(seq, "  HoldDly  %u\n", yp->holdd);
	seq_printf(seq, "  TxDelay  %u\n", yp->txd);
	seq_printf(seq, "  TxTail   %u\n", yp->txtail);
	seq_printf(seq, "  SlotTime %u\n", yp->slot);
	seq_printf(seq, "  Persist  %u\n", yp->pers);
	seq_printf(seq, "  TxFrames %lu\n", dev->stats.tx_packets);
	seq_printf(seq, "  RxFrames %lu\n", dev->stats.rx_packets);
	seq_printf(seq, "  TxInt    %u\n", yp->nb_mdint);
	seq_printf(seq, "  RxInt    %u\n", yp->nb_rxint);
	seq_printf(seq, "  RxOver   %lu\n", dev->stats.rx_fifo_errors);
	seq_printf(seq, "\n");
	return 0;
}

static const struct seq_operations yam_seqops = {
	.start = yam_seq_start,
	.next = yam_seq_next,
	.stop = yam_seq_stop,
	.show = yam_seq_show,
};

static int yam_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &yam_seqops);
}

static const struct file_operations yam_info_fops = {
	.owner = THIS_MODULE,
	.open = yam_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#endif


/* --------------------------------------------------------------------- */

static int yam_open(struct net_device *dev)
{
	struct yam_port *yp = netdev_priv(dev);
	enum uart u;
	int i;
	int ret=0;

	printk(KERN_INFO "Trying %s at iobase 0x%lx irq %u\n", dev->name, dev->base_addr, dev->irq);

	if (!yp->bitrate)
		return -ENXIO;
	if (!dev->base_addr || dev->base_addr > 0x1000 - YAM_EXTENT ||
		dev->irq < 2 || dev->irq > 15) {
		return -ENXIO;
	}
	if (!request_region(dev->base_addr, YAM_EXTENT, dev->name))
	{
		printk(KERN_ERR "%s: cannot 0x%lx busy\n", dev->name, dev->base_addr);
		return -EACCES;
	}
	if ((u = yam_check_uart(dev->base_addr)) == c_uart_unknown) {
		printk(KERN_ERR "%s: cannot find uart type\n", dev->name);
		ret = -EIO;
		goto out_release_base;
	}
	if (fpga_download(dev->base_addr, yp->bitrate)) {
		printk(KERN_ERR "%s: cannot init FPGA\n", dev->name);
		ret = -EIO;
		goto out_release_base;
	}
	outb(0, IER(dev->base_addr));
	if (request_irq(dev->irq, yam_interrupt, IRQF_DISABLED | IRQF_SHARED, dev->name, dev)) {
		printk(KERN_ERR "%s: irq %d busy\n", dev->name, dev->irq);
		ret = -EBUSY;
		goto out_release_base;
	}

	yam_set_uart(dev);

	netif_start_queue(dev);
	
	yp->slotcnt = yp->slot / 10;

	/* Reset overruns for all ports - FPGA programming makes overruns */
	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *yam_dev = yam_devs[i];

		inb(LSR(yam_dev->base_addr));
		yam_dev->stats.rx_fifo_errors = 0;
	}

	printk(KERN_INFO "%s at iobase 0x%lx irq %u uart %s\n", dev->name, dev->base_addr, dev->irq,
		   uart_str[u]);
	return 0;

out_release_base:
	release_region(dev->base_addr, YAM_EXTENT);
	return ret;
}

/* --------------------------------------------------------------------- */

static int yam_close(struct net_device *dev)
{
	struct sk_buff *skb;
	struct yam_port *yp = netdev_priv(dev);

	if (!dev)
		return -EINVAL;

	/*
	 * disable interrupts
	 */
	outb(0, IER(dev->base_addr));
	outb(1, MCR(dev->base_addr));
	/* Remove IRQ handler if last */
	free_irq(dev->irq,dev);
	release_region(dev->base_addr, YAM_EXTENT);
	netif_stop_queue(dev);
	while ((skb = skb_dequeue(&yp->send_queue)))
		dev_kfree_skb(skb);

	printk(KERN_INFO "%s: close yam at iobase 0x%lx irq %u\n",
		   yam_drvname, dev->base_addr, dev->irq);
	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct yam_port *yp = netdev_priv(dev);
	struct yamdrv_ioctl_cfg yi;
	struct yamdrv_ioctl_mcs *ym;
	int ioctl_cmd;

	if (copy_from_user(&ioctl_cmd, ifr->ifr_data, sizeof(int)))
		 return -EFAULT;

	if (yp->magic != YAM_MAGIC)
		return -EINVAL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (cmd != SIOCDEVPRIVATE)
		return -EINVAL;

	switch (ioctl_cmd) {

	case SIOCYAMRESERVED:
		return -EINVAL;			/* unused */

	case SIOCYAMSMCS:
		if (netif_running(dev))
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((ym = kmalloc(sizeof(struct yamdrv_ioctl_mcs), GFP_KERNEL)) == NULL)
			return -ENOBUFS;
		if (copy_from_user(ym, ifr->ifr_data, sizeof(struct yamdrv_ioctl_mcs))) {
			kfree(ym);
			return -EFAULT;
		}
		if (ym->bitrate > YAM_MAXBITRATE) {
			kfree(ym);
			return -EINVAL;
		}
		/* setting predef as 0 for loading userdefined mcs data */
		add_mcs(ym->bits, ym->bitrate, 0);
		kfree(ym);
		break;

	case SIOCYAMSCFG:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		if (copy_from_user(&yi, ifr->ifr_data, sizeof(struct yamdrv_ioctl_cfg)))
			 return -EFAULT;

		if ((yi.cfg.mask & YAM_IOBASE) && netif_running(dev))
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_IRQ) && netif_running(dev))
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_BITRATE) && netif_running(dev))
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_BAUDRATE) && netif_running(dev))
			return -EINVAL;		/* Cannot change this parameter when up */

		if (yi.cfg.mask & YAM_IOBASE) {
			yp->iobase = yi.cfg.iobase;
			dev->base_addr = yi.cfg.iobase;
		}
		if (yi.cfg.mask & YAM_IRQ) {
			if (yi.cfg.irq > 15)
				return -EINVAL;
			yp->irq = yi.cfg.irq;
			dev->irq = yi.cfg.irq;
		}
		if (yi.cfg.mask & YAM_BITRATE) {
			if (yi.cfg.bitrate > YAM_MAXBITRATE)
				return -EINVAL;
			yp->bitrate = yi.cfg.bitrate;
		}
		if (yi.cfg.mask & YAM_BAUDRATE) {
			if (yi.cfg.baudrate > YAM_MAXBAUDRATE)
				return -EINVAL;
			yp->baudrate = yi.cfg.baudrate;
		}
		if (yi.cfg.mask & YAM_MODE) {
			if (yi.cfg.mode > YAM_MAXMODE)
				return -EINVAL;
			yp->dupmode = yi.cfg.mode;
		}
		if (yi.cfg.mask & YAM_HOLDDLY) {
			if (yi.cfg.holddly > YAM_MAXHOLDDLY)
				return -EINVAL;
			yp->holdd = yi.cfg.holddly;
		}
		if (yi.cfg.mask & YAM_TXDELAY) {
			if (yi.cfg.txdelay > YAM_MAXTXDELAY)
				return -EINVAL;
			yp->txd = yi.cfg.txdelay;
		}
		if (yi.cfg.mask & YAM_TXTAIL) {
			if (yi.cfg.txtail > YAM_MAXTXTAIL)
				return -EINVAL;
			yp->txtail = yi.cfg.txtail;
		}
		if (yi.cfg.mask & YAM_PERSIST) {
			if (yi.cfg.persist > YAM_MAXPERSIST)
				return -EINVAL;
			yp->pers = yi.cfg.persist;
		}
		if (yi.cfg.mask & YAM_SLOTTIME) {
			if (yi.cfg.slottime > YAM_MAXSLOTTIME)
				return -EINVAL;
			yp->slot = yi.cfg.slottime;
			yp->slotcnt = yp->slot / 10;
		}
		break;

	case SIOCYAMGCFG:
		yi.cfg.mask = 0xffffffff;
		yi.cfg.iobase = yp->iobase;
		yi.cfg.irq = yp->irq;
		yi.cfg.bitrate = yp->bitrate;
		yi.cfg.baudrate = yp->baudrate;
		yi.cfg.mode = yp->dupmode;
		yi.cfg.txdelay = yp->txd;
		yi.cfg.holddly = yp->holdd;
		yi.cfg.txtail = yp->txtail;
		yi.cfg.persist = yp->pers;
		yi.cfg.slottime = yp->slot;
		if (copy_to_user(ifr->ifr_data, &yi, sizeof(struct yamdrv_ioctl_cfg)))
			 return -EFAULT;
		break;

	default:
		return -EINVAL;

	}

	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *) addr;

	/* addr is an AX.25 shifted ASCII mac address */
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;
}

/* --------------------------------------------------------------------- */

static const struct net_device_ops yam_netdev_ops = {
	.ndo_open	     = yam_open,
	.ndo_stop	     = yam_close,
	.ndo_start_xmit      = yam_send_packet,
	.ndo_do_ioctl 	     = yam_ioctl,
	.ndo_set_mac_address = yam_set_mac_address,
};

static void yam_setup(struct net_device *dev)
{
	struct yam_port *yp = netdev_priv(dev);

	yp->magic = YAM_MAGIC;
	yp->bitrate = DEFAULT_BITRATE;
	yp->baudrate = DEFAULT_BITRATE * 2;
	yp->iobase = 0;
	yp->irq = 0;
	yp->dupmode = 0;
	yp->holdd = DEFAULT_HOLDD;
	yp->txd = DEFAULT_TXD;
	yp->txtail = DEFAULT_TXTAIL;
	yp->slot = DEFAULT_SLOT;
	yp->pers = DEFAULT_PERS;
	yp->dev = dev;

	dev->base_addr = yp->iobase;
	dev->irq = yp->irq;

	skb_queue_head_init(&yp->send_queue);

	dev->netdev_ops = &yam_netdev_ops;
	dev->header_ops = &ax25_header_ops;

	dev->type = ARPHRD_AX25;
	dev->hard_header_len = AX25_MAX_HEADER_LEN;
	dev->mtu = AX25_MTU;
	dev->addr_len = AX25_ADDR_LEN;
	memcpy(dev->broadcast, &ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr, &ax25_defaddr, AX25_ADDR_LEN);
}

static int __init yam_init_driver(void)
{
	struct net_device *dev;
	int i, err;
	char name[IFNAMSIZ];

	printk(yam_drvinfo);

	for (i = 0; i < NR_PORTS; i++) {
		sprintf(name, "yam%d", i);
		
		dev = alloc_netdev(sizeof(struct yam_port), name,
				   yam_setup);
		if (!dev) {
			pr_err("yam: cannot allocate net device\n");
			err = -ENOMEM;
			goto error;
		}
		
		err = register_netdev(dev);
		if (err) {
			printk(KERN_WARNING "yam: cannot register net device %s\n", dev->name);
			goto error;
		}
		yam_devs[i] = dev;

	}

	yam_timer.function = yam_dotimer;
	yam_timer.expires = jiffies + HZ / 100;
	add_timer(&yam_timer);

	proc_create("yam", S_IRUGO, init_net.proc_net, &yam_info_fops);
	return 0;
 error:
	while (--i >= 0) {
		unregister_netdev(yam_devs[i]);
		free_netdev(yam_devs[i]);
	}
	return err;
}

/* --------------------------------------------------------------------- */

static void __exit yam_cleanup_driver(void)
{
	struct yam_mcs *p;
	int i;

	del_timer(&yam_timer);
	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = yam_devs[i];
		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}

	while (yam_data) {
		p = yam_data;
		yam_data = yam_data->next;
		kfree(p);
	}

	remove_proc_entry("yam", init_net.proc_net);
}

/* --------------------------------------------------------------------- */

MODULE_AUTHOR("Frederic Rible F1OAT frible@teaser.fr");
MODULE_DESCRIPTION("Yam amateur radio modem driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_1200);
MODULE_FIRMWARE(FIRMWARE_9600);

module_init(yam_init_driver);
module_exit(yam_cleanup_driver);

/* --------------------------------------------------------------------- */

