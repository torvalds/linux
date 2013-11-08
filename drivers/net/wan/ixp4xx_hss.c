/*
 * Intel IXP4xx HSS (synchronous serial port) driver for Linux
 *
 * Copyright (C) 2007-2008 Krzysztof Hałasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/fs.h>
#include <linux/hdlc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <mach/npe.h>
#include <mach/qmgr.h>

#define DEBUG_DESC		0
#define DEBUG_RX		0
#define DEBUG_TX		0
#define DEBUG_PKT_BYTES		0
#define DEBUG_CLOSE		0

#define DRV_NAME		"ixp4xx_hss"

#define PKT_EXTRA_FLAGS		0 /* orig 1 */
#define PKT_NUM_PIPES		1 /* 1, 2 or 4 */
#define PKT_PIPE_FIFO_SIZEW	4 /* total 4 dwords per HSS */

#define RX_DESCS		16 /* also length of all RX queues */
#define TX_DESCS		16 /* also length of all TX queues */

#define POOL_ALLOC_SIZE		(sizeof(struct desc) * (RX_DESCS + TX_DESCS))
#define RX_SIZE			(HDLC_MAX_MRU + 4) /* NPE needs more space */
#define MAX_CLOSE_WAIT		1000 /* microseconds */
#define HSS_COUNT		2
#define FRAME_SIZE		256 /* doesn't matter at this point */
#define FRAME_OFFSET		0
#define MAX_CHANNELS		(FRAME_SIZE / 8)

#define NAPI_WEIGHT		16

/* Queue IDs */
#define HSS0_CHL_RXTRIG_QUEUE	12	/* orig size = 32 dwords */
#define HSS0_PKT_RX_QUEUE	13	/* orig size = 32 dwords */
#define HSS0_PKT_TX0_QUEUE	14	/* orig size = 16 dwords */
#define HSS0_PKT_TX1_QUEUE	15
#define HSS0_PKT_TX2_QUEUE	16
#define HSS0_PKT_TX3_QUEUE	17
#define HSS0_PKT_RXFREE0_QUEUE	18	/* orig size = 16 dwords */
#define HSS0_PKT_RXFREE1_QUEUE	19
#define HSS0_PKT_RXFREE2_QUEUE	20
#define HSS0_PKT_RXFREE3_QUEUE	21
#define HSS0_PKT_TXDONE_QUEUE	22	/* orig size = 64 dwords */

#define HSS1_CHL_RXTRIG_QUEUE	10
#define HSS1_PKT_RX_QUEUE	0
#define HSS1_PKT_TX0_QUEUE	5
#define HSS1_PKT_TX1_QUEUE	6
#define HSS1_PKT_TX2_QUEUE	7
#define HSS1_PKT_TX3_QUEUE	8
#define HSS1_PKT_RXFREE0_QUEUE	1
#define HSS1_PKT_RXFREE1_QUEUE	2
#define HSS1_PKT_RXFREE2_QUEUE	3
#define HSS1_PKT_RXFREE3_QUEUE	4
#define HSS1_PKT_TXDONE_QUEUE	9

#define NPE_PKT_MODE_HDLC		0
#define NPE_PKT_MODE_RAW		1
#define NPE_PKT_MODE_56KMODE		2
#define NPE_PKT_MODE_56KENDIAN_MSB	4

/* PKT_PIPE_HDLC_CFG_WRITE flags */
#define PKT_HDLC_IDLE_ONES		0x1 /* default = flags */
#define PKT_HDLC_CRC_32			0x2 /* default = CRC-16 */
#define PKT_HDLC_MSB_ENDIAN		0x4 /* default = LE */


/* hss_config, PCRs */
/* Frame sync sampling, default = active low */
#define PCR_FRM_SYNC_ACTIVE_HIGH	0x40000000
#define PCR_FRM_SYNC_FALLINGEDGE	0x80000000
#define PCR_FRM_SYNC_RISINGEDGE		0xC0000000

/* Frame sync pin: input (default) or output generated off a given clk edge */
#define PCR_FRM_SYNC_OUTPUT_FALLING	0x20000000
#define PCR_FRM_SYNC_OUTPUT_RISING	0x30000000

/* Frame and data clock sampling on edge, default = falling */
#define PCR_FCLK_EDGE_RISING		0x08000000
#define PCR_DCLK_EDGE_RISING		0x04000000

/* Clock direction, default = input */
#define PCR_SYNC_CLK_DIR_OUTPUT		0x02000000

/* Generate/Receive frame pulses, default = enabled */
#define PCR_FRM_PULSE_DISABLED		0x01000000

 /* Data rate is full (default) or half the configured clk speed */
#define PCR_HALF_CLK_RATE		0x00200000

/* Invert data between NPE and HSS FIFOs? (default = no) */
#define PCR_DATA_POLARITY_INVERT	0x00100000

/* TX/RX endianness, default = LSB */
#define PCR_MSB_ENDIAN			0x00080000

/* Normal (default) / open drain mode (TX only) */
#define PCR_TX_PINS_OPEN_DRAIN		0x00040000

/* No framing bit transmitted and expected on RX? (default = framing bit) */
#define PCR_SOF_NO_FBIT			0x00020000

/* Drive data pins? */
#define PCR_TX_DATA_ENABLE		0x00010000

/* Voice 56k type: drive the data pins low (default), high, high Z */
#define PCR_TX_V56K_HIGH		0x00002000
#define PCR_TX_V56K_HIGH_IMP		0x00004000

/* Unassigned type: drive the data pins low (default), high, high Z */
#define PCR_TX_UNASS_HIGH		0x00000800
#define PCR_TX_UNASS_HIGH_IMP		0x00001000

/* T1 @ 1.544MHz only: Fbit dictated in FIFO (default) or high Z */
#define PCR_TX_FB_HIGH_IMP		0x00000400

/* 56k data endiannes - which bit unused: high (default) or low */
#define PCR_TX_56KE_BIT_0_UNUSED	0x00000200

/* 56k data transmission type: 32/8 bit data (default) or 56K data */
#define PCR_TX_56KS_56K_DATA		0x00000100

/* hss_config, cCR */
/* Number of packetized clients, default = 1 */
#define CCR_NPE_HFIFO_2_HDLC		0x04000000
#define CCR_NPE_HFIFO_3_OR_4HDLC	0x08000000

/* default = no loopback */
#define CCR_LOOPBACK			0x02000000

/* HSS number, default = 0 (first) */
#define CCR_SECOND_HSS			0x01000000


/* hss_config, clkCR: main:10, num:10, denom:12 */
#define CLK42X_SPEED_EXP	((0x3FF << 22) | (  2 << 12) |   15) /*65 KHz*/

#define CLK42X_SPEED_512KHZ	((  130 << 22) | (  2 << 12) |   15)
#define CLK42X_SPEED_1536KHZ	((   43 << 22) | ( 18 << 12) |   47)
#define CLK42X_SPEED_1544KHZ	((   43 << 22) | ( 33 << 12) |  192)
#define CLK42X_SPEED_2048KHZ	((   32 << 22) | ( 34 << 12) |   63)
#define CLK42X_SPEED_4096KHZ	((   16 << 22) | ( 34 << 12) |  127)
#define CLK42X_SPEED_8192KHZ	((    8 << 22) | ( 34 << 12) |  255)

#define CLK46X_SPEED_512KHZ	((  130 << 22) | ( 24 << 12) |  127)
#define CLK46X_SPEED_1536KHZ	((   43 << 22) | (152 << 12) |  383)
#define CLK46X_SPEED_1544KHZ	((   43 << 22) | ( 66 << 12) |  385)
#define CLK46X_SPEED_2048KHZ	((   32 << 22) | (280 << 12) |  511)
#define CLK46X_SPEED_4096KHZ	((   16 << 22) | (280 << 12) | 1023)
#define CLK46X_SPEED_8192KHZ	((    8 << 22) | (280 << 12) | 2047)

/*
 * HSS_CONFIG_CLOCK_CR register consists of 3 parts:
 *     A (10 bits), B (10 bits) and C (12 bits).
 * IXP42x HSS clock generator operation (verified with an oscilloscope):
 * Each clock bit takes 7.5 ns (1 / 133.xx MHz).
 * The clock sequence consists of (C - B) states of 0s and 1s, each state is
 * A bits wide. It's followed by (B + 1) states of 0s and 1s, each state is
 * (A + 1) bits wide.
 *
 * The resulting average clock frequency (assuming 33.333 MHz oscillator) is:
 * freq = 66.666 MHz / (A + (B + 1) / (C + 1))
 * minimum freq = 66.666 MHz / (A + 1)
 * maximum freq = 66.666 MHz / A
 *
 * Example: A = 2, B = 2, C = 7, CLOCK_CR register = 2 << 22 | 2 << 12 | 7
 * freq = 66.666 MHz / (2 + (2 + 1) / (7 + 1)) = 28.07 MHz (Mb/s).
 * The clock sequence is: 1100110011 (5 doubles) 000111000 (3 triples).
 * The sequence takes (C - B) * A + (B + 1) * (A + 1) = 5 * 2 + 3 * 3 bits
 * = 19 bits (each 7.5 ns long) = 142.5 ns (then the sequence repeats).
 * The sequence consists of 4 complete clock periods, thus the average
 * frequency (= clock rate) is 4 / 142.5 ns = 28.07 MHz (Mb/s).
 * (max specified clock rate for IXP42x HSS is 8.192 Mb/s).
 */

/* hss_config, LUT entries */
#define TDMMAP_UNASSIGNED	0
#define TDMMAP_HDLC		1	/* HDLC - packetized */
#define TDMMAP_VOICE56K		2	/* Voice56K - 7-bit channelized */
#define TDMMAP_VOICE64K		3	/* Voice64K - 8-bit channelized */

/* offsets into HSS config */
#define HSS_CONFIG_TX_PCR	0x00 /* port configuration registers */
#define HSS_CONFIG_RX_PCR	0x04
#define HSS_CONFIG_CORE_CR	0x08 /* loopback control, HSS# */
#define HSS_CONFIG_CLOCK_CR	0x0C /* clock generator control */
#define HSS_CONFIG_TX_FCR	0x10 /* frame configuration registers */
#define HSS_CONFIG_RX_FCR	0x14
#define HSS_CONFIG_TX_LUT	0x18 /* channel look-up tables */
#define HSS_CONFIG_RX_LUT	0x38


/* NPE command codes */
/* writes the ConfigWord value to the location specified by offset */
#define PORT_CONFIG_WRITE		0x40

/* triggers the NPE to load the contents of the configuration table */
#define PORT_CONFIG_LOAD		0x41

/* triggers the NPE to return an HssErrorReadResponse message */
#define PORT_ERROR_READ			0x42

/* triggers the NPE to reset internal status and enable the HssPacketized
   operation for the flow specified by pPipe */
#define PKT_PIPE_FLOW_ENABLE		0x50
#define PKT_PIPE_FLOW_DISABLE		0x51
#define PKT_NUM_PIPES_WRITE		0x52
#define PKT_PIPE_FIFO_SIZEW_WRITE	0x53
#define PKT_PIPE_HDLC_CFG_WRITE		0x54
#define PKT_PIPE_IDLE_PATTERN_WRITE	0x55
#define PKT_PIPE_RX_SIZE_WRITE		0x56
#define PKT_PIPE_MODE_WRITE		0x57

/* HDLC packet status values - desc->status */
#define ERR_SHUTDOWN		1 /* stop or shutdown occurrence */
#define ERR_HDLC_ALIGN		2 /* HDLC alignment error */
#define ERR_HDLC_FCS		3 /* HDLC Frame Check Sum error */
#define ERR_RXFREE_Q_EMPTY	4 /* RX-free queue became empty while receiving
				     this packet (if buf_len < pkt_len) */
#define ERR_HDLC_TOO_LONG	5 /* HDLC frame size too long */
#define ERR_HDLC_ABORT		6 /* abort sequence received */
#define ERR_DISCONNECTING	7 /* disconnect is in progress */


#ifdef __ARMEB__
typedef struct sk_buff buffer_t;
#define free_buffer dev_kfree_skb
#define free_buffer_irq dev_kfree_skb_irq
#else
typedef void buffer_t;
#define free_buffer kfree
#define free_buffer_irq kfree
#endif

struct port {
	struct device *dev;
	struct npe *npe;
	struct net_device *netdev;
	struct napi_struct napi;
	struct hss_plat_info *plat;
	buffer_t *rx_buff_tab[RX_DESCS], *tx_buff_tab[TX_DESCS];
	struct desc *desc_tab;	/* coherent */
	u32 desc_tab_phys;
	unsigned int id;
	unsigned int clock_type, clock_rate, loopback;
	unsigned int initialized, carrier;
	u8 hdlc_cfg;
	u32 clock_reg;
};

/* NPE message structure */
struct msg {
#ifdef __ARMEB__
	u8 cmd, unused, hss_port, index;
	union {
		struct { u8 data8a, data8b, data8c, data8d; };
		struct { u16 data16a, data16b; };
		struct { u32 data32; };
	};
#else
	u8 index, hss_port, unused, cmd;
	union {
		struct { u8 data8d, data8c, data8b, data8a; };
		struct { u16 data16b, data16a; };
		struct { u32 data32; };
	};
#endif
};

/* HDLC packet descriptor */
struct desc {
	u32 next;		/* pointer to next buffer, unused */

#ifdef __ARMEB__
	u16 buf_len;		/* buffer length */
	u16 pkt_len;		/* packet length */
	u32 data;		/* pointer to data buffer in RAM */
	u8 status;
	u8 error_count;
	u16 __reserved;
#else
	u16 pkt_len;		/* packet length */
	u16 buf_len;		/* buffer length */
	u32 data;		/* pointer to data buffer in RAM */
	u16 __reserved;
	u8 error_count;
	u8 status;
#endif
	u32 __reserved1[4];
};


#define rx_desc_phys(port, n)	((port)->desc_tab_phys +		\
				 (n) * sizeof(struct desc))
#define rx_desc_ptr(port, n)	(&(port)->desc_tab[n])

#define tx_desc_phys(port, n)	((port)->desc_tab_phys +		\
				 ((n) + RX_DESCS) * sizeof(struct desc))
#define tx_desc_ptr(port, n)	(&(port)->desc_tab[(n) + RX_DESCS])

/*****************************************************************************
 * global variables
 ****************************************************************************/

static int ports_open;
static struct dma_pool *dma_pool;
static spinlock_t npe_lock;

static const struct {
	int tx, txdone, rx, rxfree;
}queue_ids[2] = {{HSS0_PKT_TX0_QUEUE, HSS0_PKT_TXDONE_QUEUE, HSS0_PKT_RX_QUEUE,
		  HSS0_PKT_RXFREE0_QUEUE},
		 {HSS1_PKT_TX0_QUEUE, HSS1_PKT_TXDONE_QUEUE, HSS1_PKT_RX_QUEUE,
		  HSS1_PKT_RXFREE0_QUEUE},
};

/*****************************************************************************
 * utility functions
 ****************************************************************************/

static inline struct port* dev_to_port(struct net_device *dev)
{
	return dev_to_hdlc(dev)->priv;
}

#ifndef __ARMEB__
static inline void memcpy_swab32(u32 *dest, u32 *src, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++)
		dest[i] = swab32(src[i]);
}
#endif

/*****************************************************************************
 * HSS access
 ****************************************************************************/

static void hss_npe_send(struct port *port, struct msg *msg, const char* what)
{
	u32 *val = (u32*)msg;
	if (npe_send_message(port->npe, msg, what)) {
		printk(KERN_CRIT "HSS-%i: unable to send command [%08X:%08X]"
		       " to %s\n", port->id, val[0], val[1],
		       npe_name(port->npe));
		BUG();
	}
}

static void hss_config_set_lut(struct port *port)
{
	struct msg msg;
	int ch;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;

	for (ch = 0; ch < MAX_CHANNELS; ch++) {
		msg.data32 >>= 2;
		msg.data32 |= TDMMAP_HDLC << 30;

		if (ch % 16 == 15) {
			msg.index = HSS_CONFIG_TX_LUT + ((ch / 4) & ~3);
			hss_npe_send(port, &msg, "HSS_SET_TX_LUT");

			msg.index += HSS_CONFIG_RX_LUT - HSS_CONFIG_TX_LUT;
			hss_npe_send(port, &msg, "HSS_SET_RX_LUT");
		}
	}
}

static void hss_config(struct port *port)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;
	msg.index = HSS_CONFIG_TX_PCR;
	msg.data32 = PCR_FRM_PULSE_DISABLED | PCR_MSB_ENDIAN |
		PCR_TX_DATA_ENABLE | PCR_SOF_NO_FBIT;
	if (port->clock_type == CLOCK_INT)
		msg.data32 |= PCR_SYNC_CLK_DIR_OUTPUT;
	hss_npe_send(port, &msg, "HSS_SET_TX_PCR");

	msg.index = HSS_CONFIG_RX_PCR;
	msg.data32 ^= PCR_TX_DATA_ENABLE | PCR_DCLK_EDGE_RISING;
	hss_npe_send(port, &msg, "HSS_SET_RX_PCR");

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;
	msg.index = HSS_CONFIG_CORE_CR;
	msg.data32 = (port->loopback ? CCR_LOOPBACK : 0) |
		(port->id ? CCR_SECOND_HSS : 0);
	hss_npe_send(port, &msg, "HSS_SET_CORE_CR");

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;
	msg.index = HSS_CONFIG_CLOCK_CR;
	msg.data32 = port->clock_reg;
	hss_npe_send(port, &msg, "HSS_SET_CLOCK_CR");

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;
	msg.index = HSS_CONFIG_TX_FCR;
	msg.data16a = FRAME_OFFSET;
	msg.data16b = FRAME_SIZE - 1;
	hss_npe_send(port, &msg, "HSS_SET_TX_FCR");

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_WRITE;
	msg.hss_port = port->id;
	msg.index = HSS_CONFIG_RX_FCR;
	msg.data16a = FRAME_OFFSET;
	msg.data16b = FRAME_SIZE - 1;
	hss_npe_send(port, &msg, "HSS_SET_RX_FCR");

	hss_config_set_lut(port);

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_CONFIG_LOAD;
	msg.hss_port = port->id;
	hss_npe_send(port, &msg, "HSS_LOAD_CONFIG");

	if (npe_recv_message(port->npe, &msg, "HSS_LOAD_CONFIG") ||
	    /* HSS_LOAD_CONFIG for port #1 returns port_id = #4 */
	    msg.cmd != PORT_CONFIG_LOAD || msg.data32) {
		printk(KERN_CRIT "HSS-%i: HSS_LOAD_CONFIG failed\n",
		       port->id);
		BUG();
	}

	/* HDLC may stop working without this - check FIXME */
	npe_recv_message(port->npe, &msg, "FLUSH_IT");
}

static void hss_set_hdlc_cfg(struct port *port)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PKT_PIPE_HDLC_CFG_WRITE;
	msg.hss_port = port->id;
	msg.data8a = port->hdlc_cfg; /* rx_cfg */
	msg.data8b = port->hdlc_cfg | (PKT_EXTRA_FLAGS << 3); /* tx_cfg */
	hss_npe_send(port, &msg, "HSS_SET_HDLC_CFG");
}

static u32 hss_get_status(struct port *port)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PORT_ERROR_READ;
	msg.hss_port = port->id;
	hss_npe_send(port, &msg, "PORT_ERROR_READ");
	if (npe_recv_message(port->npe, &msg, "PORT_ERROR_READ")) {
		printk(KERN_CRIT "HSS-%i: unable to read HSS status\n",
		       port->id);
		BUG();
	}

	return msg.data32;
}

static void hss_start_hdlc(struct port *port)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PKT_PIPE_FLOW_ENABLE;
	msg.hss_port = port->id;
	msg.data32 = 0;
	hss_npe_send(port, &msg, "HSS_ENABLE_PKT_PIPE");
}

static void hss_stop_hdlc(struct port *port)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = PKT_PIPE_FLOW_DISABLE;
	msg.hss_port = port->id;
	hss_npe_send(port, &msg, "HSS_DISABLE_PKT_PIPE");
	hss_get_status(port); /* make sure it's halted */
}

static int hss_load_firmware(struct port *port)
{
	struct msg msg;
	int err;

	if (port->initialized)
		return 0;

	if (!npe_running(port->npe) &&
	    (err = npe_load_firmware(port->npe, npe_name(port->npe),
				     port->dev)))
		return err;

	/* HDLC mode configuration */
	memset(&msg, 0, sizeof(msg));
	msg.cmd = PKT_NUM_PIPES_WRITE;
	msg.hss_port = port->id;
	msg.data8a = PKT_NUM_PIPES;
	hss_npe_send(port, &msg, "HSS_SET_PKT_PIPES");

	msg.cmd = PKT_PIPE_FIFO_SIZEW_WRITE;
	msg.data8a = PKT_PIPE_FIFO_SIZEW;
	hss_npe_send(port, &msg, "HSS_SET_PKT_FIFO");

	msg.cmd = PKT_PIPE_MODE_WRITE;
	msg.data8a = NPE_PKT_MODE_HDLC;
	/* msg.data8b = inv_mask */
	/* msg.data8c = or_mask */
	hss_npe_send(port, &msg, "HSS_SET_PKT_MODE");

	msg.cmd = PKT_PIPE_RX_SIZE_WRITE;
	msg.data16a = HDLC_MAX_MRU; /* including CRC */
	hss_npe_send(port, &msg, "HSS_SET_PKT_RX_SIZE");

	msg.cmd = PKT_PIPE_IDLE_PATTERN_WRITE;
	msg.data32 = 0x7F7F7F7F; /* ??? FIXME */
	hss_npe_send(port, &msg, "HSS_SET_PKT_IDLE");

	port->initialized = 1;
	return 0;
}

/*****************************************************************************
 * packetized (HDLC) operation
 ****************************************************************************/

static inline void debug_pkt(struct net_device *dev, const char *func,
			     u8 *data, int len)
{
#if DEBUG_PKT_BYTES
	int i;

	printk(KERN_DEBUG "%s: %s(%i)", dev->name, func, len);
	for (i = 0; i < len; i++) {
		if (i >= DEBUG_PKT_BYTES)
			break;
		printk("%s%02X", !(i % 4) ? " " : "", data[i]);
	}
	printk("\n");
#endif
}


static inline void debug_desc(u32 phys, struct desc *desc)
{
#if DEBUG_DESC
	printk(KERN_DEBUG "%X: %X %3X %3X %08X %X %X\n",
	       phys, desc->next, desc->buf_len, desc->pkt_len,
	       desc->data, desc->status, desc->error_count);
#endif
}

static inline int queue_get_desc(unsigned int queue, struct port *port,
				 int is_tx)
{
	u32 phys, tab_phys, n_desc;
	struct desc *tab;

	if (!(phys = qmgr_get_entry(queue)))
		return -1;

	BUG_ON(phys & 0x1F);
	tab_phys = is_tx ? tx_desc_phys(port, 0) : rx_desc_phys(port, 0);
	tab = is_tx ? tx_desc_ptr(port, 0) : rx_desc_ptr(port, 0);
	n_desc = (phys - tab_phys) / sizeof(struct desc);
	BUG_ON(n_desc >= (is_tx ? TX_DESCS : RX_DESCS));
	debug_desc(phys, &tab[n_desc]);
	BUG_ON(tab[n_desc].next);
	return n_desc;
}

static inline void queue_put_desc(unsigned int queue, u32 phys,
				  struct desc *desc)
{
	debug_desc(phys, desc);
	BUG_ON(phys & 0x1F);
	qmgr_put_entry(queue, phys);
	/* Don't check for queue overflow here, we've allocated sufficient
	   length and queues >= 32 don't support this check anyway. */
}


static inline void dma_unmap_tx(struct port *port, struct desc *desc)
{
#ifdef __ARMEB__
	dma_unmap_single(&port->netdev->dev, desc->data,
			 desc->buf_len, DMA_TO_DEVICE);
#else
	dma_unmap_single(&port->netdev->dev, desc->data & ~3,
			 ALIGN((desc->data & 3) + desc->buf_len, 4),
			 DMA_TO_DEVICE);
#endif
}


static void hss_hdlc_set_carrier(void *pdev, int carrier)
{
	struct net_device *netdev = pdev;
	struct port *port = dev_to_port(netdev);
	unsigned long flags;

	spin_lock_irqsave(&npe_lock, flags);
	port->carrier = carrier;
	if (!port->loopback) {
		if (carrier)
			netif_carrier_on(netdev);
		else
			netif_carrier_off(netdev);
	}
	spin_unlock_irqrestore(&npe_lock, flags);
}

static void hss_hdlc_rx_irq(void *pdev)
{
	struct net_device *dev = pdev;
	struct port *port = dev_to_port(dev);

#if DEBUG_RX
	printk(KERN_DEBUG "%s: hss_hdlc_rx_irq\n", dev->name);
#endif
	qmgr_disable_irq(queue_ids[port->id].rx);
	napi_schedule(&port->napi);
}

static int hss_hdlc_poll(struct napi_struct *napi, int budget)
{
	struct port *port = container_of(napi, struct port, napi);
	struct net_device *dev = port->netdev;
	unsigned int rxq = queue_ids[port->id].rx;
	unsigned int rxfreeq = queue_ids[port->id].rxfree;
	int received = 0;

#if DEBUG_RX
	printk(KERN_DEBUG "%s: hss_hdlc_poll\n", dev->name);
#endif

	while (received < budget) {
		struct sk_buff *skb;
		struct desc *desc;
		int n;
#ifdef __ARMEB__
		struct sk_buff *temp;
		u32 phys;
#endif

		if ((n = queue_get_desc(rxq, port, 0)) < 0) {
#if DEBUG_RX
			printk(KERN_DEBUG "%s: hss_hdlc_poll"
			       " napi_complete\n", dev->name);
#endif
			napi_complete(napi);
			qmgr_enable_irq(rxq);
			if (!qmgr_stat_empty(rxq) &&
			    napi_reschedule(napi)) {
#if DEBUG_RX
				printk(KERN_DEBUG "%s: hss_hdlc_poll"
				       " napi_reschedule succeeded\n",
				       dev->name);
#endif
				qmgr_disable_irq(rxq);
				continue;
			}
#if DEBUG_RX
			printk(KERN_DEBUG "%s: hss_hdlc_poll all done\n",
			       dev->name);
#endif
			return received; /* all work done */
		}

		desc = rx_desc_ptr(port, n);
#if 0 /* FIXME - error_count counts modulo 256, perhaps we should use it */
		if (desc->error_count)
			printk(KERN_DEBUG "%s: hss_hdlc_poll status 0x%02X"
			       " errors %u\n", dev->name, desc->status,
			       desc->error_count);
#endif
		skb = NULL;
		switch (desc->status) {
		case 0:
#ifdef __ARMEB__
			if ((skb = netdev_alloc_skb(dev, RX_SIZE)) != NULL) {
				phys = dma_map_single(&dev->dev, skb->data,
						      RX_SIZE,
						      DMA_FROM_DEVICE);
				if (dma_mapping_error(&dev->dev, phys)) {
					dev_kfree_skb(skb);
					skb = NULL;
				}
			}
#else
			skb = netdev_alloc_skb(dev, desc->pkt_len);
#endif
			if (!skb)
				dev->stats.rx_dropped++;
			break;
		case ERR_HDLC_ALIGN:
		case ERR_HDLC_ABORT:
			dev->stats.rx_frame_errors++;
			dev->stats.rx_errors++;
			break;
		case ERR_HDLC_FCS:
			dev->stats.rx_crc_errors++;
			dev->stats.rx_errors++;
			break;
		case ERR_HDLC_TOO_LONG:
			dev->stats.rx_length_errors++;
			dev->stats.rx_errors++;
			break;
		default:	/* FIXME - remove printk */
			printk(KERN_ERR "%s: hss_hdlc_poll: status 0x%02X"
			       " errors %u\n", dev->name, desc->status,
			       desc->error_count);
			dev->stats.rx_errors++;
		}

		if (!skb) {
			/* put the desc back on RX-ready queue */
			desc->buf_len = RX_SIZE;
			desc->pkt_len = desc->status = 0;
			queue_put_desc(rxfreeq, rx_desc_phys(port, n), desc);
			continue;
		}

		/* process received frame */
#ifdef __ARMEB__
		temp = skb;
		skb = port->rx_buff_tab[n];
		dma_unmap_single(&dev->dev, desc->data,
				 RX_SIZE, DMA_FROM_DEVICE);
#else
		dma_sync_single_for_cpu(&dev->dev, desc->data,
					RX_SIZE, DMA_FROM_DEVICE);
		memcpy_swab32((u32 *)skb->data, (u32 *)port->rx_buff_tab[n],
			      ALIGN(desc->pkt_len, 4) / 4);
#endif
		skb_put(skb, desc->pkt_len);

		debug_pkt(dev, "hss_hdlc_poll", skb->data, skb->len);

		skb->protocol = hdlc_type_trans(skb, dev);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		netif_receive_skb(skb);

		/* put the new buffer on RX-free queue */
#ifdef __ARMEB__
		port->rx_buff_tab[n] = temp;
		desc->data = phys;
#endif
		desc->buf_len = RX_SIZE;
		desc->pkt_len = 0;
		queue_put_desc(rxfreeq, rx_desc_phys(port, n), desc);
		received++;
	}
#if DEBUG_RX
	printk(KERN_DEBUG "hss_hdlc_poll: end, not all work done\n");
#endif
	return received;	/* not all work done */
}


static void hss_hdlc_txdone_irq(void *pdev)
{
	struct net_device *dev = pdev;
	struct port *port = dev_to_port(dev);
	int n_desc;

#if DEBUG_TX
	printk(KERN_DEBUG DRV_NAME ": hss_hdlc_txdone_irq\n");
#endif
	while ((n_desc = queue_get_desc(queue_ids[port->id].txdone,
					port, 1)) >= 0) {
		struct desc *desc;
		int start;

		desc = tx_desc_ptr(port, n_desc);

		dev->stats.tx_packets++;
		dev->stats.tx_bytes += desc->pkt_len;

		dma_unmap_tx(port, desc);
#if DEBUG_TX
		printk(KERN_DEBUG "%s: hss_hdlc_txdone_irq free %p\n",
		       dev->name, port->tx_buff_tab[n_desc]);
#endif
		free_buffer_irq(port->tx_buff_tab[n_desc]);
		port->tx_buff_tab[n_desc] = NULL;

		start = qmgr_stat_below_low_watermark(port->plat->txreadyq);
		queue_put_desc(port->plat->txreadyq,
			       tx_desc_phys(port, n_desc), desc);
		if (start) { /* TX-ready queue was empty */
#if DEBUG_TX
			printk(KERN_DEBUG "%s: hss_hdlc_txdone_irq xmit"
			       " ready\n", dev->name);
#endif
			netif_wake_queue(dev);
		}
	}
}

static int hss_hdlc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	unsigned int txreadyq = port->plat->txreadyq;
	int len, offset, bytes, n;
	void *mem;
	u32 phys;
	struct desc *desc;

#if DEBUG_TX
	printk(KERN_DEBUG "%s: hss_hdlc_xmit\n", dev->name);
#endif

	if (unlikely(skb->len > HDLC_MAX_MRU)) {
		dev_kfree_skb(skb);
		dev->stats.tx_errors++;
		return NETDEV_TX_OK;
	}

	debug_pkt(dev, "hss_hdlc_xmit", skb->data, skb->len);

	len = skb->len;
#ifdef __ARMEB__
	offset = 0; /* no need to keep alignment */
	bytes = len;
	mem = skb->data;
#else
	offset = (int)skb->data & 3; /* keep 32-bit alignment */
	bytes = ALIGN(offset + len, 4);
	if (!(mem = kmalloc(bytes, GFP_ATOMIC))) {
		dev_kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	memcpy_swab32(mem, (u32 *)((int)skb->data & ~3), bytes / 4);
	dev_kfree_skb(skb);
#endif

	phys = dma_map_single(&dev->dev, mem, bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->dev, phys)) {
#ifdef __ARMEB__
		dev_kfree_skb(skb);
#else
		kfree(mem);
#endif
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	n = queue_get_desc(txreadyq, port, 1);
	BUG_ON(n < 0);
	desc = tx_desc_ptr(port, n);

#ifdef __ARMEB__
	port->tx_buff_tab[n] = skb;
#else
	port->tx_buff_tab[n] = mem;
#endif
	desc->data = phys + offset;
	desc->buf_len = desc->pkt_len = len;

	wmb();
	queue_put_desc(queue_ids[port->id].tx, tx_desc_phys(port, n), desc);

	if (qmgr_stat_below_low_watermark(txreadyq)) { /* empty */
#if DEBUG_TX
		printk(KERN_DEBUG "%s: hss_hdlc_xmit queue full\n", dev->name);
#endif
		netif_stop_queue(dev);
		/* we could miss TX ready interrupt */
		if (!qmgr_stat_below_low_watermark(txreadyq)) {
#if DEBUG_TX
			printk(KERN_DEBUG "%s: hss_hdlc_xmit ready again\n",
			       dev->name);
#endif
			netif_wake_queue(dev);
		}
	}

#if DEBUG_TX
	printk(KERN_DEBUG "%s: hss_hdlc_xmit end\n", dev->name);
#endif
	return NETDEV_TX_OK;
}


static int request_hdlc_queues(struct port *port)
{
	int err;

	err = qmgr_request_queue(queue_ids[port->id].rxfree, RX_DESCS, 0, 0,
				 "%s:RX-free", port->netdev->name);
	if (err)
		return err;

	err = qmgr_request_queue(queue_ids[port->id].rx, RX_DESCS, 0, 0,
				 "%s:RX", port->netdev->name);
	if (err)
		goto rel_rxfree;

	err = qmgr_request_queue(queue_ids[port->id].tx, TX_DESCS, 0, 0,
				 "%s:TX", port->netdev->name);
	if (err)
		goto rel_rx;

	err = qmgr_request_queue(port->plat->txreadyq, TX_DESCS, 0, 0,
				 "%s:TX-ready", port->netdev->name);
	if (err)
		goto rel_tx;

	err = qmgr_request_queue(queue_ids[port->id].txdone, TX_DESCS, 0, 0,
				 "%s:TX-done", port->netdev->name);
	if (err)
		goto rel_txready;
	return 0;

rel_txready:
	qmgr_release_queue(port->plat->txreadyq);
rel_tx:
	qmgr_release_queue(queue_ids[port->id].tx);
rel_rx:
	qmgr_release_queue(queue_ids[port->id].rx);
rel_rxfree:
	qmgr_release_queue(queue_ids[port->id].rxfree);
	printk(KERN_DEBUG "%s: unable to request hardware queues\n",
	       port->netdev->name);
	return err;
}

static void release_hdlc_queues(struct port *port)
{
	qmgr_release_queue(queue_ids[port->id].rxfree);
	qmgr_release_queue(queue_ids[port->id].rx);
	qmgr_release_queue(queue_ids[port->id].txdone);
	qmgr_release_queue(queue_ids[port->id].tx);
	qmgr_release_queue(port->plat->txreadyq);
}

static int init_hdlc_queues(struct port *port)
{
	int i;

	if (!ports_open)
		if (!(dma_pool = dma_pool_create(DRV_NAME, NULL,
						 POOL_ALLOC_SIZE, 32, 0)))
			return -ENOMEM;

	if (!(port->desc_tab = dma_pool_alloc(dma_pool, GFP_KERNEL,
					      &port->desc_tab_phys)))
		return -ENOMEM;
	memset(port->desc_tab, 0, POOL_ALLOC_SIZE);
	memset(port->rx_buff_tab, 0, sizeof(port->rx_buff_tab)); /* tables */
	memset(port->tx_buff_tab, 0, sizeof(port->tx_buff_tab));

	/* Setup RX buffers */
	for (i = 0; i < RX_DESCS; i++) {
		struct desc *desc = rx_desc_ptr(port, i);
		buffer_t *buff;
		void *data;
#ifdef __ARMEB__
		if (!(buff = netdev_alloc_skb(port->netdev, RX_SIZE)))
			return -ENOMEM;
		data = buff->data;
#else
		if (!(buff = kmalloc(RX_SIZE, GFP_KERNEL)))
			return -ENOMEM;
		data = buff;
#endif
		desc->buf_len = RX_SIZE;
		desc->data = dma_map_single(&port->netdev->dev, data,
					    RX_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(&port->netdev->dev, desc->data)) {
			free_buffer(buff);
			return -EIO;
		}
		port->rx_buff_tab[i] = buff;
	}

	return 0;
}

static void destroy_hdlc_queues(struct port *port)
{
	int i;

	if (port->desc_tab) {
		for (i = 0; i < RX_DESCS; i++) {
			struct desc *desc = rx_desc_ptr(port, i);
			buffer_t *buff = port->rx_buff_tab[i];
			if (buff) {
				dma_unmap_single(&port->netdev->dev,
						 desc->data, RX_SIZE,
						 DMA_FROM_DEVICE);
				free_buffer(buff);
			}
		}
		for (i = 0; i < TX_DESCS; i++) {
			struct desc *desc = tx_desc_ptr(port, i);
			buffer_t *buff = port->tx_buff_tab[i];
			if (buff) {
				dma_unmap_tx(port, desc);
				free_buffer(buff);
			}
		}
		dma_pool_free(dma_pool, port->desc_tab, port->desc_tab_phys);
		port->desc_tab = NULL;
	}

	if (!ports_open && dma_pool) {
		dma_pool_destroy(dma_pool);
		dma_pool = NULL;
	}
}

static int hss_hdlc_open(struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	unsigned long flags;
	int i, err = 0;

	if ((err = hdlc_open(dev)))
		return err;

	if ((err = hss_load_firmware(port)))
		goto err_hdlc_close;

	if ((err = request_hdlc_queues(port)))
		goto err_hdlc_close;

	if ((err = init_hdlc_queues(port)))
		goto err_destroy_queues;

	spin_lock_irqsave(&npe_lock, flags);
	if (port->plat->open)
		if ((err = port->plat->open(port->id, dev,
					    hss_hdlc_set_carrier)))
			goto err_unlock;
	spin_unlock_irqrestore(&npe_lock, flags);

	/* Populate queues with buffers, no failure after this point */
	for (i = 0; i < TX_DESCS; i++)
		queue_put_desc(port->plat->txreadyq,
			       tx_desc_phys(port, i), tx_desc_ptr(port, i));

	for (i = 0; i < RX_DESCS; i++)
		queue_put_desc(queue_ids[port->id].rxfree,
			       rx_desc_phys(port, i), rx_desc_ptr(port, i));

	napi_enable(&port->napi);
	netif_start_queue(dev);

	qmgr_set_irq(queue_ids[port->id].rx, QUEUE_IRQ_SRC_NOT_EMPTY,
		     hss_hdlc_rx_irq, dev);

	qmgr_set_irq(queue_ids[port->id].txdone, QUEUE_IRQ_SRC_NOT_EMPTY,
		     hss_hdlc_txdone_irq, dev);
	qmgr_enable_irq(queue_ids[port->id].txdone);

	ports_open++;

	hss_set_hdlc_cfg(port);
	hss_config(port);

	hss_start_hdlc(port);

	/* we may already have RX data, enables IRQ */
	napi_schedule(&port->napi);
	return 0;

err_unlock:
	spin_unlock_irqrestore(&npe_lock, flags);
err_destroy_queues:
	destroy_hdlc_queues(port);
	release_hdlc_queues(port);
err_hdlc_close:
	hdlc_close(dev);
	return err;
}

static int hss_hdlc_close(struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	unsigned long flags;
	int i, buffs = RX_DESCS; /* allocated RX buffers */

	spin_lock_irqsave(&npe_lock, flags);
	ports_open--;
	qmgr_disable_irq(queue_ids[port->id].rx);
	netif_stop_queue(dev);
	napi_disable(&port->napi);

	hss_stop_hdlc(port);

	while (queue_get_desc(queue_ids[port->id].rxfree, port, 0) >= 0)
		buffs--;
	while (queue_get_desc(queue_ids[port->id].rx, port, 0) >= 0)
		buffs--;

	if (buffs)
		printk(KERN_CRIT "%s: unable to drain RX queue, %i buffer(s)"
		       " left in NPE\n", dev->name, buffs);

	buffs = TX_DESCS;
	while (queue_get_desc(queue_ids[port->id].tx, port, 1) >= 0)
		buffs--; /* cancel TX */

	i = 0;
	do {
		while (queue_get_desc(port->plat->txreadyq, port, 1) >= 0)
			buffs--;
		if (!buffs)
			break;
	} while (++i < MAX_CLOSE_WAIT);

	if (buffs)
		printk(KERN_CRIT "%s: unable to drain TX queue, %i buffer(s) "
		       "left in NPE\n", dev->name, buffs);
#if DEBUG_CLOSE
	if (!buffs)
		printk(KERN_DEBUG "Draining TX queues took %i cycles\n", i);
#endif
	qmgr_disable_irq(queue_ids[port->id].txdone);

	if (port->plat->close)
		port->plat->close(port->id, dev);
	spin_unlock_irqrestore(&npe_lock, flags);

	destroy_hdlc_queues(port);
	release_hdlc_queues(port);
	hdlc_close(dev);
	return 0;
}


static int hss_hdlc_attach(struct net_device *dev, unsigned short encoding,
			   unsigned short parity)
{
	struct port *port = dev_to_port(dev);

	if (encoding != ENCODING_NRZ)
		return -EINVAL;

	switch(parity) {
	case PARITY_CRC16_PR1_CCITT:
		port->hdlc_cfg = 0;
		return 0;

	case PARITY_CRC32_PR1_CCITT:
		port->hdlc_cfg = PKT_HDLC_CRC_32;
		return 0;

	default:
		return -EINVAL;
	}
}

static u32 check_clock(u32 rate, u32 a, u32 b, u32 c,
		       u32 *best, u32 *best_diff, u32 *reg)
{
	/* a is 10-bit, b is 10-bit, c is 12-bit */
	u64 new_rate;
	u32 new_diff;

	new_rate = ixp4xx_timer_freq * (u64)(c + 1);
	do_div(new_rate, a * (c + 1) + b + 1);
	new_diff = abs((u32)new_rate - rate);

	if (new_diff < *best_diff) {
		*best = new_rate;
		*best_diff = new_diff;
		*reg = (a << 22) | (b << 12) | c;
	}
	return new_diff;
}

static void find_best_clock(u32 rate, u32 *best, u32 *reg)
{
	u32 a, b, diff = 0xFFFFFFFF;

	a = ixp4xx_timer_freq / rate;

	if (a > 0x3FF) { /* 10-bit value - we can go as slow as ca. 65 kb/s */
		check_clock(rate, 0x3FF, 1, 1, best, &diff, reg);
		return;
	}
	if (a == 0) { /* > 66.666 MHz */
		a = 1; /* minimum divider is 1 (a = 0, b = 1, c = 1) */
		rate = ixp4xx_timer_freq;
	}

	if (rate * a == ixp4xx_timer_freq) { /* don't divide by 0 later */
		check_clock(rate, a - 1, 1, 1, best, &diff, reg);
		return;
	}

	for (b = 0; b < 0x400; b++) {
		u64 c = (b + 1) * (u64)rate;
		do_div(c, ixp4xx_timer_freq - rate * a);
		c--;
		if (c >= 0xFFF) { /* 12-bit - no need to check more 'b's */
			if (b == 0 && /* also try a bit higher rate */
			    !check_clock(rate, a - 1, 1, 1, best, &diff, reg))
				return;
			check_clock(rate, a, b, 0xFFF, best, &diff, reg);
			return;
		}
		if (!check_clock(rate, a, b, c, best, &diff, reg))
			return;
		if (!check_clock(rate, a, b, c + 1, best, &diff, reg))
			return;
	}
}

static int hss_hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line;
	sync_serial_settings __user *line = ifr->ifr_settings.ifs_ifsu.sync;
	struct port *port = dev_to_port(dev);
	unsigned long flags;
	int clk;

	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE:
		ifr->ifr_settings.type = IF_IFACE_V35;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		memset(&new_line, 0, sizeof(new_line));
		new_line.clock_type = port->clock_type;
		new_line.clock_rate = port->clock_rate;
		new_line.loopback = port->loopback;
		if (copy_to_user(line, &new_line, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL:
	case IF_IFACE_V35:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&new_line, line, size))
			return -EFAULT;

		clk = new_line.clock_type;
		if (port->plat->set_clock)
			clk = port->plat->set_clock(port->id, clk);

		if (clk != CLOCK_EXT && clk != CLOCK_INT)
			return -EINVAL;	/* No such clock setting */

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		port->clock_type = clk; /* Update settings */
		if (clk == CLOCK_INT)
			find_best_clock(new_line.clock_rate, &port->clock_rate,
					&port->clock_reg);
		else {
			port->clock_rate = 0;
			port->clock_reg = CLK42X_SPEED_2048KHZ;
		}
		port->loopback = new_line.loopback;

		spin_lock_irqsave(&npe_lock, flags);

		if (dev->flags & IFF_UP)
			hss_config(port);

		if (port->loopback || port->carrier)
			netif_carrier_on(port->netdev);
		else
			netif_carrier_off(port->netdev);
		spin_unlock_irqrestore(&npe_lock, flags);

		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}

/*****************************************************************************
 * initialization
 ****************************************************************************/

static const struct net_device_ops hss_hdlc_ops = {
	.ndo_open       = hss_hdlc_open,
	.ndo_stop       = hss_hdlc_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = hss_hdlc_ioctl,
};

static int __devinit hss_init_one(struct platform_device *pdev)
{
	struct port *port;
	struct net_device *dev;
	hdlc_device *hdlc;
	int err;

	if ((port = kzalloc(sizeof(*port), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	if ((port->npe = npe_request(0)) == NULL) {
		err = -ENODEV;
		goto err_free;
	}

	if ((port->netdev = dev = alloc_hdlcdev(port)) == NULL) {
		err = -ENOMEM;
		goto err_plat;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	hdlc = dev_to_hdlc(dev);
	hdlc->attach = hss_hdlc_attach;
	hdlc->xmit = hss_hdlc_xmit;
	dev->netdev_ops = &hss_hdlc_ops;
	dev->tx_queue_len = 100;
	port->clock_type = CLOCK_EXT;
	port->clock_rate = 0;
	port->clock_reg = CLK42X_SPEED_2048KHZ;
	port->id = pdev->id;
	port->dev = &pdev->dev;
	port->plat = pdev->dev.platform_data;
	netif_napi_add(dev, &port->napi, hss_hdlc_poll, NAPI_WEIGHT);

	if ((err = register_hdlc_device(dev)))
		goto err_free_netdev;

	platform_set_drvdata(pdev, port);

	printk(KERN_INFO "%s: HSS-%i\n", dev->name, port->id);
	return 0;

err_free_netdev:
	free_netdev(dev);
err_plat:
	npe_release(port->npe);
err_free:
	kfree(port);
	return err;
}

static int __devexit hss_remove_one(struct platform_device *pdev)
{
	struct port *port = platform_get_drvdata(pdev);

	unregister_hdlc_device(port->netdev);
	free_netdev(port->netdev);
	npe_release(port->npe);
	platform_set_drvdata(pdev, NULL);
	kfree(port);
	return 0;
}

static struct platform_driver ixp4xx_hss_driver = {
	.driver.name	= DRV_NAME,
	.probe		= hss_init_one,
	.remove		= hss_remove_one,
};

static int __init hss_init_module(void)
{
	if ((ixp4xx_read_feature_bits() &
	     (IXP4XX_FEATURE_HDLC | IXP4XX_FEATURE_HSS)) !=
	    (IXP4XX_FEATURE_HDLC | IXP4XX_FEATURE_HSS))
		return -ENODEV;

	spin_lock_init(&npe_lock);

	return platform_driver_register(&ixp4xx_hss_driver);
}

static void __exit hss_cleanup_module(void)
{
	platform_driver_unregister(&ixp4xx_hss_driver);
}

MODULE_AUTHOR("Krzysztof Halasa");
MODULE_DESCRIPTION("Intel IXP4xx HSS driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ixp4xx_hss");
module_init(hss_init_module);
module_exit(hss_cleanup_module);
