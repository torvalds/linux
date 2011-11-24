/*
 * cc770.h - Bosch CC770 and Intel AN82527 network device driver
 *
 * Copyright (C) 2009, 2011 Wolfgang Grandegger <wg@grandegger.com>
 *
 * Derived from the old Socket-CAN i82527 driver:
 *
 * Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Send feedback to <socketcan-users@lists.berlios.de>
 */

#ifndef CC770_DEV_H
#define CC770_DEV_H

#include <linux/can/dev.h>

struct cc770_msgobj {
	u8 ctrl0;
	u8 ctrl1;
	u8 id[4];
	u8 config;
	u8 data[8];
	u8 dontuse;		/* padding */
} __attribute__ ((packed));

struct cc770_regs {
	union {
		struct cc770_msgobj msgobj[16]; /* Message object 1..15 */
		struct {
			u8 control;		/* Control Register */
			u8 status;		/* Status Register */
			u8 cpu_interface;	/* CPU Interface Register */
			u8 dontuse1;
			u8 high_speed_read[2];	/* High Speed Read */
			u8 global_mask_std[2];	/* Standard Global Mask */
			u8 global_mask_ext[4];	/* Extended Global Mask */
			u8 msg15_mask[4];	/* Message 15 Mask */
			u8 dontuse2[15];
			u8 clkout;		/* Clock Out Register */
			u8 dontuse3[15];
			u8 bus_config;		/* Bus Configuration Register */
			u8 dontuse4[15];
			u8 bit_timing_0;	/* Bit Timing Register byte 0 */
			u8 dontuse5[15];
			u8 bit_timing_1;	/* Bit Timing Register byte 1 */
			u8 dontuse6[15];
			u8 interrupt;		/* Interrupt Register */
			u8 dontuse7[15];
			u8 rx_error_counter;	/* Receive Error Counter */
			u8 dontuse8[15];
			u8 tx_error_counter;	/* Transmit Error Counter */
			u8 dontuse9[31];
			u8 p1_conf;
			u8 dontuse10[15];
			u8 p2_conf;
			u8 dontuse11[15];
			u8 p1_in;
			u8 dontuse12[15];
			u8 p2_in;
			u8 dontuse13[15];
			u8 p1_out;
			u8 dontuse14[15];
			u8 p2_out;
			u8 dontuse15[15];
			u8 serial_reset_addr;
		};
	};
} __attribute__ ((packed));

/* Control Register (0x00) */
#define CTRL_INI	0x01	/* Initialization */
#define CTRL_IE		0x02	/* Interrupt Enable */
#define CTRL_SIE	0x04	/* Status Interrupt Enable */
#define CTRL_EIE	0x08	/* Error Interrupt Enable */
#define CTRL_EAF	0x20	/* Enable additional functions */
#define CTRL_CCE	0x40	/* Change Configuration Enable */

/* Status Register (0x01) */
#define STAT_LEC_STUFF	0x01	/* Stuff error */
#define STAT_LEC_FORM	0x02	/* Form error */
#define STAT_LEC_ACK	0x03	/* Acknowledgement error */
#define STAT_LEC_BIT1	0x04	/* Bit1 error */
#define STAT_LEC_BIT0	0x05	/* Bit0 error */
#define STAT_LEC_CRC	0x06	/* CRC error */
#define STAT_LEC_MASK	0x07	/* Last Error Code mask */
#define STAT_TXOK	0x08	/* Transmit Message Successfully */
#define STAT_RXOK	0x10	/* Receive Message Successfully */
#define STAT_WAKE	0x20	/* Wake Up Status */
#define STAT_WARN	0x40	/* Warning Status */
#define STAT_BOFF	0x80	/* Bus Off Status */

/* CPU Interface Register (0x02) */
#define CPUIF_CEN	0x01	/* Clock Out Enable */
#define CPUIF_MUX	0x04	/* Multiplex */
#define CPUIF_SLP	0x08	/* Sleep */
#define CPUIF_PWD	0x10	/* Power Down Mode */
#define CPUIF_DMC	0x20	/* Divide Memory Clock */
#define CPUIF_DSC	0x40	/* Divide System Clock */
#define CPUIF_RST	0x80	/* Hardware Reset Status */

/* Clock Out Register (0x1f) */
#define CLKOUT_CD_MASK  0x0f	/* Clock Divider mask */
#define CLKOUT_SL_MASK	0x30	/* Slew Rate mask */
#define CLKOUT_SL_SHIFT	4

/* Bus Configuration Register (0x2f) */
#define BUSCFG_DR0	0x01	/* Disconnect RX0 Input / Select RX input */
#define BUSCFG_DR1	0x02	/* Disconnect RX1 Input / Silent mode */
#define BUSCFG_DT1	0x08	/* Disconnect TX1 Output */
#define BUSCFG_POL	0x20	/* Polarity dominant or recessive */
#define BUSCFG_CBY	0x40	/* Input Comparator Bypass */

/* Message Control Register 0 (Base Address + 0x0) */
#define INTPND_RES	0x01	/* No Interrupt pending */
#define INTPND_SET	0x02	/* Interrupt pending */
#define INTPND_UNC	0x03
#define RXIE_RES	0x04	/* Receive Interrupt Disable */
#define RXIE_SET	0x08	/* Receive Interrupt Enable */
#define RXIE_UNC	0x0c
#define TXIE_RES	0x10	/* Transmit Interrupt Disable */
#define TXIE_SET	0x20	/* Transmit Interrupt Enable */
#define TXIE_UNC	0x30
#define MSGVAL_RES	0x40	/* Message Invalid */
#define MSGVAL_SET	0x80	/* Message Valid */
#define MSGVAL_UNC	0xc0

/* Message Control Register 1 (Base Address + 0x01) */
#define NEWDAT_RES	0x01	/* No New Data */
#define NEWDAT_SET	0x02	/* New Data */
#define NEWDAT_UNC	0x03
#define MSGLST_RES	0x04	/* No Message Lost */
#define MSGLST_SET	0x08	/* Message Lost */
#define MSGLST_UNC	0x0c
#define CPUUPD_RES	0x04	/* No CPU Updating */
#define CPUUPD_SET	0x08	/* CPU Updating */
#define CPUUPD_UNC	0x0c
#define TXRQST_RES	0x10	/* No Transmission Request */
#define TXRQST_SET	0x20	/* Transmission Request */
#define TXRQST_UNC	0x30
#define RMTPND_RES	0x40	/* No Remote Request Pending */
#define RMTPND_SET	0x80	/* Remote Request Pending */
#define RMTPND_UNC	0xc0

/* Message Configuration Register (Base Address + 0x06) */
#define MSGCFG_XTD	0x04	/* Extended Identifier */
#define MSGCFG_DIR	0x08	/* Direction is Transmit */

#define MSGOBJ_FIRST	1
#define MSGOBJ_LAST	15

#define CC770_IO_SIZE	0x100
#define CC770_MAX_IRQ	20	/* max. number of interrupts handled in ISR */

#define CC770_ECHO_SKB_MAX	1

#define cc770_read_reg(priv, member)					\
	priv->read_reg(priv, offsetof(struct cc770_regs, member))

#define cc770_write_reg(priv, member, value)				\
	priv->write_reg(priv, offsetof(struct cc770_regs, member), value)

/*
 * Message objects and flags used by this driver
 */
#define CC770_OBJ_FLAG_RX 	0x01
#define CC770_OBJ_FLAG_RTR	0x02
#define CC770_OBJ_FLAG_EFF	0x04

enum {
	CC770_OBJ_RX0 = 0,	/* for receiving normal messages */
	CC770_OBJ_RX1,		/* for receiving normal messages */
	CC770_OBJ_RX_RTR0,	/* for receiving remote transmission requests */
	CC770_OBJ_RX_RTR1,	/* for receiving remote transmission requests */
	CC770_OBJ_TX,		/* for sending messages */
	CC770_OBJ_MAX
};

#define obj2msgobj(o)	(MSGOBJ_LAST - (o)) /* message object 11..15 */

/*
 * CC770 private data structure
 */
struct cc770_priv {
	struct can_priv can;	/* must be the first member */
	int open_time;
	struct sk_buff *echo_skb;

	/* the lower-layer is responsible for appropriate locking */
	u8 (*read_reg)(const struct cc770_priv *priv, int reg);
	void (*write_reg)(const struct cc770_priv *priv, int reg, u8 val);
	void (*pre_irq)(const struct cc770_priv *priv);
	void (*post_irq)(const struct cc770_priv *priv);

	void *priv;		/* for board-specific data */
	struct net_device *dev;

	void __iomem *reg_base;	/* ioremap'ed address to registers */
	unsigned long irq_flags;	/* for request_irq() */

	unsigned char obj_flags[CC770_OBJ_MAX];
	u8 control_normal_mode;	/* Control register for normal mode */
	u8 cpu_interface;	/* CPU interface register */
	u8 clkout;		/* Clock out register */
	u8 bus_config;		/* Bus conffiguration register */
};

struct net_device *alloc_cc770dev(int sizeof_priv);
void free_cc770dev(struct net_device *dev);
int register_cc770dev(struct net_device *dev);
void unregister_cc770dev(struct net_device *dev);

#endif /* CC770_DEV_H */
