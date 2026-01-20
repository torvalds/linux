/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *   Author: Frank Mori Hess <fmh6jj@gmail.com>
 *   copyright: (C) 2006, 2010, 2015 Fluke Corporation
 ***************************************************************************/

#include <linux/compiler.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "nec7210.h"

struct fluke_priv {
	struct nec7210_priv nec7210_priv;
	struct resource *gpib_iomem_res;
	struct resource *write_transfer_counter_res;
	struct resource *dma_port_res;
	int irq;
	struct dma_chan *dma_channel;
	u8 *dma_buffer;
	int dma_buffer_size;
	void __iomem *write_transfer_counter;
};

// cb7210 specific registers and bits
enum cb7210_regs {
	STATE1_REG = 0x4,
	ISR0_IMR0 = 0x6,
	BUS_STATUS = 0x7
};

enum cb7210_page_in {
	ISR0_IMR0_PAGE = 1,
	BUS_STATUS_PAGE = 1,
	STATE1_PAGE = 1
};

/* IMR0 -- Interrupt Mode Register 0 */
enum imr0_bits {
	FLUKE_IFCIE_BIT = 0x8,	/* interface clear interrupt */
};

/* ISR0 -- Interrupt Status Register 0 */
enum isr0_bits {
	FLUKE_IFCI_BIT = 0x8,	/* interface clear interrupt */
};

enum state1_bits {
	SOURCE_HANDSHAKE_SIDS_BITS = 0x0, /* source idle state */
	SOURCE_HANDSHAKE_SGNS_BITS = 0x1, /* source generate state */
	SOURCE_HANDSHAKE_SDYS_BITS = 0x2, /* source delay state */
	SOURCE_HANDSHAKE_STRS_BITS = 0x5, /* source transfer state */
	SOURCE_HANDSHAKE_MASK = 0x7
};

/*
 * we customized the cb7210 vhdl to give the "data in" status
 * on the unused bit 7 of the address0 register.
 */
enum cb7210_address0 {
	DATA_IN_STATUS = 0x80
};

static inline int cb7210_page_in_bits(unsigned int page)
{
	return 0x50 | (page & 0xf);
}

// don't use without locking nec_priv->register_page_lock
static inline u8 fluke_read_byte_nolock(struct nec7210_priv *nec_priv,
					int register_num)
{
	u8 retval;

	retval = readl(nec_priv->mmiobase + register_num * nec_priv->offset);
	return retval;
}

// don't use without locking nec_priv->register_page_lock
static inline void fluke_write_byte_nolock(struct nec7210_priv *nec_priv, u8 data,
					   int register_num)
{
	writel(data, nec_priv->mmiobase + register_num * nec_priv->offset);
}

static inline u8 fluke_paged_read_byte(struct fluke_priv *e_priv,
				       unsigned int register_num, unsigned int page)
{
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	u8 retval;
	unsigned long flags;

	spin_lock_irqsave(&nec_priv->register_page_lock, flags);
	fluke_write_byte_nolock(nec_priv, cb7210_page_in_bits(page), AUXMR);
	udelay(1);
	/* chip auto clears the page after a read */
	retval = fluke_read_byte_nolock(nec_priv, register_num);
	spin_unlock_irqrestore(&nec_priv->register_page_lock, flags);
	return retval;
}

static inline void fluke_paged_write_byte(struct fluke_priv *e_priv, u8 data,
					  unsigned int register_num, unsigned int page)
{
	struct nec7210_priv *nec_priv = &e_priv->nec7210_priv;
	unsigned long flags;

	spin_lock_irqsave(&nec_priv->register_page_lock, flags);
	fluke_write_byte_nolock(nec_priv, cb7210_page_in_bits(page), AUXMR);
	udelay(1);
	fluke_write_byte_nolock(nec_priv, data, register_num);
	spin_unlock_irqrestore(&nec_priv->register_page_lock, flags);
}

enum bus_status_bits {
	BSR_ATN_BIT = 0x1,
	BSR_EOI_BIT = 0x2,
	BSR_SRQ_BIT = 0x4,
	BSR_IFC_BIT = 0x8,
	BSR_REN_BIT = 0x10,
	BSR_DAV_BIT = 0x20,
	BSR_NRFD_BIT = 0x40,
	BSR_NDAC_BIT = 0x80,
};

enum cb7210_aux_cmds {
/*
 * AUX_RTL2 is an undocumented aux command which causes cb7210 to assert
 * (and keep asserted) local rtl message.  This is used in conjunction
 * with the (stupid) cb7210 implementation
 * of the normal nec7210 AUX_RTL aux command, which
 * causes the rtl message to toggle between on and off.
 */
	AUX_RTL2 = 0xd,
	AUX_NBAF = 0xe,	// new byte available false (also clears seoi)
	AUX_LO_SPEED = 0x40,
	AUX_HI_SPEED = 0x41,
};

enum {
	fluke_reg_offset = 4,
	fluke_num_regs = 8,
	write_transfer_counter_mask = 0x7ff,
};
