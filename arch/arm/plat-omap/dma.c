/*
 * linux/arch/arm/plat-omap/dma.c
 *
 * Copyright (C) 2003 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * OMAP2/3 support Copyright (C) 2004-2007 Texas Instruments, Inc.
 * Merged to support both OMAP1 and OMAP2 by Tony Lindgren <tony@atomide.com>
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Support functions for the OMAP internal DMA channels.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/dma.h>
#include <asm/io.h>

#include <asm/arch/tc.h>

#undef DEBUG

#ifndef CONFIG_ARCH_OMAP1
enum { DMA_CH_ALLOC_DONE, DMA_CH_PARAMS_SET_DONE, DMA_CH_STARTED,
	DMA_CH_QUEUED, DMA_CH_NOTSTARTED, DMA_CH_PAUSED, DMA_CH_LINK_ENABLED
};

enum { DMA_CHAIN_STARTED, DMA_CHAIN_NOTSTARTED };
#endif

#define OMAP_DMA_ACTIVE		0x01
#define OMAP_DMA_CCR_EN		(1 << 7)
#define OMAP2_DMA_CSR_CLEAR_MASK	0xffe

#define OMAP_FUNC_MUX_ARM_BASE	(0xfffe1000 + 0xec)

static int enable_1510_mode = 0;

struct omap_dma_lch {
	int next_lch;
	int dev_id;
	u16 saved_csr;
	u16 enabled_irqs;
	const char *dev_name;
	void (* callback)(int lch, u16 ch_status, void *data);
	void *data;

#ifndef CONFIG_ARCH_OMAP1
	/* required for Dynamic chaining */
	int prev_linked_ch;
	int next_linked_ch;
	int state;
	int chain_id;

	int status;
#endif
	long flags;
};

#ifndef CONFIG_ARCH_OMAP1
struct dma_link_info {
	int *linked_dmach_q;
	int no_of_lchs_linked;

	int q_count;
	int q_tail;
	int q_head;

	int chain_state;
	int chain_mode;

};

static struct dma_link_info dma_linked_lch[OMAP_LOGICAL_DMA_CH_COUNT];

/* Chain handling macros */
#define OMAP_DMA_CHAIN_QINIT(chain_id)					\
	do {								\
		dma_linked_lch[chain_id].q_head =			\
		dma_linked_lch[chain_id].q_tail =			\
		dma_linked_lch[chain_id].q_count = 0;			\
	} while (0)
#define OMAP_DMA_CHAIN_QFULL(chain_id)					\
		(dma_linked_lch[chain_id].no_of_lchs_linked ==		\
		dma_linked_lch[chain_id].q_count)
#define OMAP_DMA_CHAIN_QLAST(chain_id)					\
	do {								\
		((dma_linked_lch[chain_id].no_of_lchs_linked-1) ==	\
		dma_linked_lch[chain_id].q_count)			\
	} while (0)
#define OMAP_DMA_CHAIN_QEMPTY(chain_id)					\
		(0 == dma_linked_lch[chain_id].q_count)
#define __OMAP_DMA_CHAIN_INCQ(end)					\
	((end) = ((end)+1) % dma_linked_lch[chain_id].no_of_lchs_linked)
#define OMAP_DMA_CHAIN_INCQHEAD(chain_id)				\
	do {								\
		__OMAP_DMA_CHAIN_INCQ(dma_linked_lch[chain_id].q_head);	\
		dma_linked_lch[chain_id].q_count--;			\
	} while (0)

#define OMAP_DMA_CHAIN_INCQTAIL(chain_id)				\
	do {								\
		__OMAP_DMA_CHAIN_INCQ(dma_linked_lch[chain_id].q_tail);	\
		dma_linked_lch[chain_id].q_count++; \
	} while (0)
#endif
static int dma_chan_count;

static spinlock_t dma_chan_lock;
static struct omap_dma_lch dma_chan[OMAP_LOGICAL_DMA_CH_COUNT];

static const u8 omap1_dma_irq[OMAP_LOGICAL_DMA_CH_COUNT] = {
	INT_DMA_CH0_6, INT_DMA_CH1_7, INT_DMA_CH2_8, INT_DMA_CH3,
	INT_DMA_CH4, INT_DMA_CH5, INT_1610_DMA_CH6, INT_1610_DMA_CH7,
	INT_1610_DMA_CH8, INT_1610_DMA_CH9, INT_1610_DMA_CH10,
	INT_1610_DMA_CH11, INT_1610_DMA_CH12, INT_1610_DMA_CH13,
	INT_1610_DMA_CH14, INT_1610_DMA_CH15, INT_DMA_LCD
};

static inline void disable_lnk(int lch);
static void omap_disable_channel_irq(int lch);
static inline void omap_enable_channel_irq(int lch);

#define REVISIT_24XX()		printk(KERN_ERR "FIXME: no %s on 24xx\n", \
						__func__);

#ifdef CONFIG_ARCH_OMAP15XX
/* Returns 1 if the DMA module is in OMAP1510-compatible mode, 0 otherwise */
int omap_dma_in_1510_mode(void)
{
	return enable_1510_mode;
}
#else
#define omap_dma_in_1510_mode()		0
#endif

#ifdef CONFIG_ARCH_OMAP1
static inline int get_gdma_dev(int req)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;

	return ((omap_readl(reg) >> shift) & 0x3f) + 1;
}

static inline void set_gdma_dev(int req, int dev)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;
	u32 l;

	l = omap_readl(reg);
	l &= ~(0x3f << shift);
	l |= (dev - 1) << shift;
	omap_writel(l, reg);
}
#else
#define set_gdma_dev(req, dev)	do {} while (0)
#endif

static void clear_lch_regs(int lch)
{
	int i;
	u32 lch_base = OMAP_DMA_BASE + lch * 0x40;

	for (i = 0; i < 0x2c; i += 2)
		omap_writew(0, lch_base + i);
}

void omap_set_dma_priority(int lch, int dst_port, int priority)
{
	unsigned long reg;
	u32 l;

	if (cpu_class_is_omap1()) {
		switch (dst_port) {
		case OMAP_DMA_PORT_OCP_T1:	/* FFFECC00 */
			reg = OMAP_TC_OCPT1_PRIOR;
			break;
		case OMAP_DMA_PORT_OCP_T2:	/* FFFECCD0 */
			reg = OMAP_TC_OCPT2_PRIOR;
			break;
		case OMAP_DMA_PORT_EMIFF:	/* FFFECC08 */
			reg = OMAP_TC_EMIFF_PRIOR;
			break;
		case OMAP_DMA_PORT_EMIFS:	/* FFFECC04 */
			reg = OMAP_TC_EMIFS_PRIOR;
			break;
		default:
			BUG();
			return;
		}
		l = omap_readl(reg);
		l &= ~(0xf << 8);
		l |= (priority & 0xf) << 8;
		omap_writel(l, reg);
	}

	if (cpu_class_is_omap2()) {
		if (priority)
			OMAP_DMA_CCR_REG(lch) |= (1 << 6);
		else
			OMAP_DMA_CCR_REG(lch) &= ~(1 << 6);
	}
}

void omap_set_dma_transfer_params(int lch, int data_type, int elem_count,
				  int frame_count, int sync_mode,
				  int dma_trigger, int src_or_dst_synch)
{
	OMAP_DMA_CSDP_REG(lch) &= ~0x03;
	OMAP_DMA_CSDP_REG(lch) |= data_type;

	if (cpu_class_is_omap1()) {
		OMAP_DMA_CCR_REG(lch) &= ~(1 << 5);
		if (sync_mode == OMAP_DMA_SYNC_FRAME)
			OMAP_DMA_CCR_REG(lch) |= 1 << 5;

		OMAP1_DMA_CCR2_REG(lch) &= ~(1 << 2);
		if (sync_mode == OMAP_DMA_SYNC_BLOCK)
			OMAP1_DMA_CCR2_REG(lch) |= 1 << 2;
	}

	if (cpu_class_is_omap2() && dma_trigger) {
		u32 val = OMAP_DMA_CCR_REG(lch);

		val &= ~(3 << 19);
		if (dma_trigger > 63)
			val |= 1 << 20;
		if (dma_trigger > 31)
			val |= 1 << 19;

		val &= ~(0x1f);
		val |= (dma_trigger & 0x1f);

		if (sync_mode & OMAP_DMA_SYNC_FRAME)
			val |= 1 << 5;
		else
			val &= ~(1 << 5);

		if (sync_mode & OMAP_DMA_SYNC_BLOCK)
			val |= 1 << 18;
		else
			val &= ~(1 << 18);

		if (src_or_dst_synch)
			val |= 1 << 24;		/* source synch */
		else
			val &= ~(1 << 24);	/* dest synch */

		OMAP_DMA_CCR_REG(lch) = val;
	}

	OMAP_DMA_CEN_REG(lch) = elem_count;
	OMAP_DMA_CFN_REG(lch) = frame_count;
}

void omap_set_dma_color_mode(int lch, enum omap_dma_color_mode mode, u32 color)
{
	u16 w;

	BUG_ON(omap_dma_in_1510_mode());

	if (cpu_class_is_omap2()) {
		REVISIT_24XX();
		return;
	}

	w = OMAP1_DMA_CCR2_REG(lch) & ~0x03;
	switch (mode) {
	case OMAP_DMA_CONSTANT_FILL:
		w |= 0x01;
		break;
	case OMAP_DMA_TRANSPARENT_COPY:
		w |= 0x02;
		break;
	case OMAP_DMA_COLOR_DIS:
		break;
	default:
		BUG();
	}
	OMAP1_DMA_CCR2_REG(lch) = w;

	w = OMAP1_DMA_LCH_CTRL_REG(lch) & ~0x0f;
	/* Default is channel type 2D */
	if (mode) {
		OMAP1_DMA_COLOR_L_REG(lch) = (u16)color;
		OMAP1_DMA_COLOR_U_REG(lch) = (u16)(color >> 16);
		w |= 1;		/* Channel type G */
	}
	OMAP1_DMA_LCH_CTRL_REG(lch) = w;
}

void omap_set_dma_write_mode(int lch, enum omap_dma_write_mode mode)
{
	if (cpu_class_is_omap2()) {
		OMAP_DMA_CSDP_REG(lch) &= ~(0x3 << 16);
		OMAP_DMA_CSDP_REG(lch) |= (mode << 16);
	}
}

/* Note that src_port is only for omap1 */
void omap_set_dma_src_params(int lch, int src_port, int src_amode,
			     unsigned long src_start,
			     int src_ei, int src_fi)
{
	if (cpu_class_is_omap1()) {
		OMAP_DMA_CSDP_REG(lch) &= ~(0x1f << 2);
		OMAP_DMA_CSDP_REG(lch) |= src_port << 2;
	}

	OMAP_DMA_CCR_REG(lch) &= ~(0x03 << 12);
	OMAP_DMA_CCR_REG(lch) |= src_amode << 12;

	if (cpu_class_is_omap1()) {
		OMAP1_DMA_CSSA_U_REG(lch) = src_start >> 16;
		OMAP1_DMA_CSSA_L_REG(lch) = src_start;
	}

	if (cpu_class_is_omap2())
		OMAP2_DMA_CSSA_REG(lch) = src_start;

	OMAP_DMA_CSEI_REG(lch) = src_ei;
	OMAP_DMA_CSFI_REG(lch) = src_fi;
}

void omap_set_dma_params(int lch, struct omap_dma_channel_params * params)
{
	omap_set_dma_transfer_params(lch, params->data_type,
				     params->elem_count, params->frame_count,
				     params->sync_mode, params->trigger,
				     params->src_or_dst_synch);
	omap_set_dma_src_params(lch, params->src_port,
				params->src_amode, params->src_start,
				params->src_ei, params->src_fi);

	omap_set_dma_dest_params(lch, params->dst_port,
				 params->dst_amode, params->dst_start,
				 params->dst_ei, params->dst_fi);
	if (params->read_prio || params->write_prio)
		omap_dma_set_prio_lch(lch, params->read_prio,
				      params->write_prio);
}

void omap_set_dma_src_index(int lch, int eidx, int fidx)
{
	if (cpu_class_is_omap2()) {
		REVISIT_24XX();
		return;
	}
	OMAP_DMA_CSEI_REG(lch) = eidx;
	OMAP_DMA_CSFI_REG(lch) = fidx;
}

void omap_set_dma_src_data_pack(int lch, int enable)
{
	OMAP_DMA_CSDP_REG(lch) &= ~(1 << 6);
	if (enable)
		OMAP_DMA_CSDP_REG(lch) |= (1 << 6);
}

void omap_set_dma_src_burst_mode(int lch, enum omap_dma_burst_mode burst_mode)
{
	unsigned int burst = 0;
	OMAP_DMA_CSDP_REG(lch) &= ~(0x03 << 7);

	switch (burst_mode) {
	case OMAP_DMA_DATA_BURST_DIS:
		break;
	case OMAP_DMA_DATA_BURST_4:
		if (cpu_class_is_omap2())
			burst = 0x1;
		else
			burst = 0x2;
		break;
	case OMAP_DMA_DATA_BURST_8:
		if (cpu_class_is_omap2()) {
			burst = 0x2;
			break;
		}
		/* not supported by current hardware on OMAP1
		 * w |= (0x03 << 7);
		 * fall through
		 */
	case OMAP_DMA_DATA_BURST_16:
		if (cpu_class_is_omap2()) {
			burst = 0x3;
			break;
		}
		/* OMAP1 don't support burst 16
		 * fall through
		 */
	default:
		BUG();
	}
	OMAP_DMA_CSDP_REG(lch) |= (burst << 7);
}

/* Note that dest_port is only for OMAP1 */
void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
			      unsigned long dest_start,
			      int dst_ei, int dst_fi)
{
	if (cpu_class_is_omap1()) {
		OMAP_DMA_CSDP_REG(lch) &= ~(0x1f << 9);
		OMAP_DMA_CSDP_REG(lch) |= dest_port << 9;
	}

	OMAP_DMA_CCR_REG(lch) &= ~(0x03 << 14);
	OMAP_DMA_CCR_REG(lch) |= dest_amode << 14;

	if (cpu_class_is_omap1()) {
		OMAP1_DMA_CDSA_U_REG(lch) = dest_start >> 16;
		OMAP1_DMA_CDSA_L_REG(lch) = dest_start;
	}

	if (cpu_class_is_omap2())
		OMAP2_DMA_CDSA_REG(lch) = dest_start;

	OMAP_DMA_CDEI_REG(lch) = dst_ei;
	OMAP_DMA_CDFI_REG(lch) = dst_fi;
}

void omap_set_dma_dest_index(int lch, int eidx, int fidx)
{
	if (cpu_class_is_omap2()) {
		REVISIT_24XX();
		return;
	}
	OMAP_DMA_CDEI_REG(lch) = eidx;
	OMAP_DMA_CDFI_REG(lch) = fidx;
}

void omap_set_dma_dest_data_pack(int lch, int enable)
{
	OMAP_DMA_CSDP_REG(lch) &= ~(1 << 13);
	if (enable)
		OMAP_DMA_CSDP_REG(lch) |= 1 << 13;
}

void omap_set_dma_dest_burst_mode(int lch, enum omap_dma_burst_mode burst_mode)
{
	unsigned int burst = 0;
	OMAP_DMA_CSDP_REG(lch) &= ~(0x03 << 14);

	switch (burst_mode) {
	case OMAP_DMA_DATA_BURST_DIS:
		break;
	case OMAP_DMA_DATA_BURST_4:
		if (cpu_class_is_omap2())
			burst = 0x1;
		else
			burst = 0x2;
		break;
	case OMAP_DMA_DATA_BURST_8:
		if (cpu_class_is_omap2())
			burst = 0x2;
		else
			burst = 0x3;
		break;
	case OMAP_DMA_DATA_BURST_16:
		if (cpu_class_is_omap2()) {
			burst = 0x3;
			break;
		}
		/* OMAP1 don't support burst 16
		 * fall through
		 */
	default:
		printk(KERN_ERR "Invalid DMA burst mode\n");
		BUG();
		return;
	}
	OMAP_DMA_CSDP_REG(lch) |= (burst << 14);
}

static inline void omap_enable_channel_irq(int lch)
{
	u32 status;

	/* Clear CSR */
	if (cpu_class_is_omap1())
		status = OMAP_DMA_CSR_REG(lch);
	else if (cpu_class_is_omap2())
		OMAP_DMA_CSR_REG(lch) = OMAP2_DMA_CSR_CLEAR_MASK;

	/* Enable some nice interrupts. */
	OMAP_DMA_CICR_REG(lch) = dma_chan[lch].enabled_irqs;

	dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
}

static void omap_disable_channel_irq(int lch)
{
	if (cpu_class_is_omap2())
		OMAP_DMA_CICR_REG(lch) = 0;
}

void omap_enable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs |= bits;
}

void omap_disable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs &= ~bits;
}

static inline void enable_lnk(int lch)
{
	if (cpu_class_is_omap1())
		OMAP_DMA_CLNK_CTRL_REG(lch) &= ~(1 << 14);

	/* Set the ENABLE_LNK bits */
	if (dma_chan[lch].next_lch != -1)
		OMAP_DMA_CLNK_CTRL_REG(lch) =
			dma_chan[lch].next_lch | (1 << 15);

#ifndef CONFIG_ARCH_OMAP1
	if (dma_chan[lch].next_linked_ch != -1)
		OMAP_DMA_CLNK_CTRL_REG(lch) =
			dma_chan[lch].next_linked_ch | (1 << 15);
#endif
}

static inline void disable_lnk(int lch)
{
	/* Disable interrupts */
	if (cpu_class_is_omap1()) {
		OMAP_DMA_CICR_REG(lch) = 0;
		/* Set the STOP_LNK bit */
		OMAP_DMA_CLNK_CTRL_REG(lch) |= 1 << 14;
	}

	if (cpu_class_is_omap2()) {
		omap_disable_channel_irq(lch);
		/* Clear the ENABLE_LNK bit */
		OMAP_DMA_CLNK_CTRL_REG(lch) &= ~(1 << 15);
	}

	dma_chan[lch].flags &= ~OMAP_DMA_ACTIVE;
}

static inline void omap2_enable_irq_lch(int lch)
{
	u32 val;

	if (!cpu_class_is_omap2())
		return;

	val = omap_readl(OMAP_DMA4_IRQENABLE_L0);
	val |= 1 << lch;
	omap_writel(val, OMAP_DMA4_IRQENABLE_L0);
}

int omap_request_dma(int dev_id, const char *dev_name,
		     void (* callback)(int lch, u16 ch_status, void *data),
		     void *data, int *dma_ch_out)
{
	int ch, free_ch = -1;
	unsigned long flags;
	struct omap_dma_lch *chan;

	spin_lock_irqsave(&dma_chan_lock, flags);
	for (ch = 0; ch < dma_chan_count; ch++) {
		if (free_ch == -1 && dma_chan[ch].dev_id == -1) {
			free_ch = ch;
			if (dev_id == 0)
				break;
		}
	}
	if (free_ch == -1) {
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return -EBUSY;
	}
	chan = dma_chan + free_ch;
	chan->dev_id = dev_id;

	if (cpu_class_is_omap1())
		clear_lch_regs(free_ch);

	if (cpu_class_is_omap2())
		omap_clear_dma(free_ch);

	spin_unlock_irqrestore(&dma_chan_lock, flags);

	chan->dev_name = dev_name;
	chan->callback = callback;
	chan->data = data;
#ifndef CONFIG_ARCH_OMAP1
	chan->chain_id = -1;
#endif
	chan->enabled_irqs = OMAP_DMA_DROP_IRQ | OMAP_DMA_BLOCK_IRQ;

	if (cpu_class_is_omap1())
		chan->enabled_irqs |= OMAP1_DMA_TOUT_IRQ;
	else if (cpu_class_is_omap2())
		chan->enabled_irqs |= OMAP2_DMA_MISALIGNED_ERR_IRQ |
			OMAP2_DMA_TRANS_ERR_IRQ;

	if (cpu_is_omap16xx()) {
		/* If the sync device is set, configure it dynamically. */
		if (dev_id != 0) {
			set_gdma_dev(free_ch + 1, dev_id);
			dev_id = free_ch + 1;
		}
		/* Disable the 1510 compatibility mode and set the sync device
		 * id. */
		OMAP_DMA_CCR_REG(free_ch) = dev_id | (1 << 10);
	} else if (cpu_is_omap730() || cpu_is_omap15xx()) {
		OMAP_DMA_CCR_REG(free_ch) = dev_id;
	}

	if (cpu_class_is_omap2()) {
		omap2_enable_irq_lch(free_ch);

		omap_enable_channel_irq(free_ch);
		/* Clear the CSR register and IRQ status register */
		OMAP_DMA_CSR_REG(free_ch) = OMAP2_DMA_CSR_CLEAR_MASK;
		omap_writel(1 << free_ch, OMAP_DMA4_IRQSTATUS_L0);
	}

	*dma_ch_out = free_ch;

	return 0;
}

void omap_free_dma(int lch)
{
	unsigned long flags;

	spin_lock_irqsave(&dma_chan_lock, flags);
	if (dma_chan[lch].dev_id == -1) {
		printk("omap_dma: trying to free nonallocated DMA channel %d\n",
		       lch);
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return;
	}
	dma_chan[lch].dev_id = -1;
	dma_chan[lch].next_lch = -1;
	dma_chan[lch].callback = NULL;
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	if (cpu_class_is_omap1()) {
		/* Disable all DMA interrupts for the channel. */
		OMAP_DMA_CICR_REG(lch) = 0;
		/* Make sure the DMA transfer is stopped. */
		OMAP_DMA_CCR_REG(lch) = 0;
	}

	if (cpu_class_is_omap2()) {
		u32 val;
		/* Disable interrupts */
		val = omap_readl(OMAP_DMA4_IRQENABLE_L0);
		val &= ~(1 << lch);
		omap_writel(val, OMAP_DMA4_IRQENABLE_L0);

		/* Clear the CSR register and IRQ status register */
		OMAP_DMA_CSR_REG(lch) = OMAP2_DMA_CSR_CLEAR_MASK;
		omap_writel(1 << lch, OMAP_DMA4_IRQSTATUS_L0);

		/* Disable all DMA interrupts for the channel. */
		OMAP_DMA_CICR_REG(lch) = 0;

		/* Make sure the DMA transfer is stopped. */
		OMAP_DMA_CCR_REG(lch) = 0;
		omap_clear_dma(lch);
	}
}

/**
 * @brief omap_dma_set_global_params : Set global priority settings for dma
 *
 * @param arb_rate
 * @param max_fifo_depth
 * @param tparams - Number of thereads to reserve : DMA_THREAD_RESERVE_NORM
 * 						    DMA_THREAD_RESERVE_ONET
 * 						    DMA_THREAD_RESERVE_TWOT
 * 						    DMA_THREAD_RESERVE_THREET
 */
void
omap_dma_set_global_params(int arb_rate, int max_fifo_depth, int tparams)
{
	u32 reg;

	if (!cpu_class_is_omap2()) {
		printk(KERN_ERR "FIXME: no %s on 15xx/16xx\n", __func__);
		return;
	}

	if (arb_rate == 0)
		arb_rate = 1;

	reg = (arb_rate & 0xff) << 16;
	reg |= (0xff & max_fifo_depth);

	omap_writel(reg, OMAP_DMA4_GCR_REG);
}
EXPORT_SYMBOL(omap_dma_set_global_params);

/**
 * @brief omap_dma_set_prio_lch : Set channel wise priority settings
 *
 * @param lch
 * @param read_prio - Read priority
 * @param write_prio - Write priority
 * Both of the above can be set with one of the following values :
 * 	DMA_CH_PRIO_HIGH/DMA_CH_PRIO_LOW
 */
int
omap_dma_set_prio_lch(int lch, unsigned char read_prio,
		      unsigned char write_prio)
{
	u32 w;

	if (unlikely((lch < 0 || lch >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid channel id\n");
		return -EINVAL;
	}
	w = OMAP_DMA_CCR_REG(lch);
	w &= ~((1 << 6) | (1 << 26));
	if (cpu_is_omap2430() || cpu_is_omap34xx())
		w |= ((read_prio & 0x1) << 6) | ((write_prio & 0x1) << 26);
	else
		w |= ((read_prio & 0x1) << 6);

	OMAP_DMA_CCR_REG(lch) = w;
	return 0;
}
EXPORT_SYMBOL(omap_dma_set_prio_lch);

/*
 * Clears any DMA state so the DMA engine is ready to restart with new buffers
 * through omap_start_dma(). Any buffers in flight are discarded.
 */
void omap_clear_dma(int lch)
{
	unsigned long flags;

	local_irq_save(flags);

	if (cpu_class_is_omap1()) {
		int status;
		OMAP_DMA_CCR_REG(lch) &= ~OMAP_DMA_CCR_EN;

		/* Clear pending interrupts */
		status = OMAP_DMA_CSR_REG(lch);
	}

	if (cpu_class_is_omap2()) {
		int i;
		u32 lch_base = OMAP_DMA4_BASE + lch * 0x60 + 0x80;
		for (i = 0; i < 0x44; i += 4)
			omap_writel(0, lch_base + i);
	}

	local_irq_restore(flags);
}

void omap_start_dma(int lch)
{
	if (!omap_dma_in_1510_mode() && dma_chan[lch].next_lch != -1) {
		int next_lch, cur_lch;
		char dma_chan_link_map[OMAP_LOGICAL_DMA_CH_COUNT];

		dma_chan_link_map[lch] = 1;
		/* Set the link register of the first channel */
		enable_lnk(lch);

		memset(dma_chan_link_map, 0, sizeof(dma_chan_link_map));
		cur_lch = dma_chan[lch].next_lch;
		do {
			next_lch = dma_chan[cur_lch].next_lch;

			/* The loop case: we've been here already */
			if (dma_chan_link_map[cur_lch])
				break;
			/* Mark the current channel */
			dma_chan_link_map[cur_lch] = 1;

			enable_lnk(cur_lch);
			omap_enable_channel_irq(cur_lch);

			cur_lch = next_lch;
		} while (next_lch != -1);
	} else if (cpu_class_is_omap2()) {
		/* Errata: Need to write lch even if not using chaining */
		OMAP_DMA_CLNK_CTRL_REG(lch) = lch;
	}

	omap_enable_channel_irq(lch);

	/* Errata: On ES2.0 BUFFERING disable must be set.
	 * This will always fail on ES1.0 */
	if (cpu_is_omap24xx()) {
		OMAP_DMA_CCR_REG(lch) |= OMAP_DMA_CCR_EN;
	}

	OMAP_DMA_CCR_REG(lch) |= OMAP_DMA_CCR_EN;

	dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
}

void omap_stop_dma(int lch)
{
	if (!omap_dma_in_1510_mode() && dma_chan[lch].next_lch != -1) {
		int next_lch, cur_lch = lch;
		char dma_chan_link_map[OMAP_LOGICAL_DMA_CH_COUNT];

		memset(dma_chan_link_map, 0, sizeof(dma_chan_link_map));
		do {
			/* The loop case: we've been here already */
			if (dma_chan_link_map[cur_lch])
				break;
			/* Mark the current channel */
			dma_chan_link_map[cur_lch] = 1;

			disable_lnk(cur_lch);

			next_lch = dma_chan[cur_lch].next_lch;
			cur_lch = next_lch;
		} while (next_lch != -1);

		return;
	}

	/* Disable all interrupts on the channel */
	if (cpu_class_is_omap1())
		OMAP_DMA_CICR_REG(lch) = 0;

	OMAP_DMA_CCR_REG(lch) &= ~OMAP_DMA_CCR_EN;
	dma_chan[lch].flags &= ~OMAP_DMA_ACTIVE;
}

/*
 * Allows changing the DMA callback function or data. This may be needed if
 * the driver shares a single DMA channel for multiple dma triggers.
 */
int omap_set_dma_callback(int lch,
			  void (* callback)(int lch, u16 ch_status, void *data),
			  void *data)
{
	unsigned long flags;

	if (lch < 0)
		return -ENODEV;

	spin_lock_irqsave(&dma_chan_lock, flags);
	if (dma_chan[lch].dev_id == -1) {
		printk(KERN_ERR "DMA callback for not set for free channel\n");
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return -EINVAL;
	}
	dma_chan[lch].callback = callback;
	dma_chan[lch].data = data;
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	return 0;
}

/*
 * Returns current physical source address for the given DMA channel.
 * If the channel is running the caller must disable interrupts prior calling
 * this function and process the returned value before re-enabling interrupt to
 * prevent races with the interrupt handler. Note that in continuous mode there
 * is a chance for CSSA_L register overflow inbetween the two reads resulting
 * in incorrect return value.
 */
dma_addr_t omap_get_dma_src_pos(int lch)
{
	dma_addr_t offset = 0;

	if (cpu_class_is_omap1())
		offset = (dma_addr_t) (OMAP1_DMA_CSSA_L_REG(lch) |
				       (OMAP1_DMA_CSSA_U_REG(lch) << 16));

	if (cpu_class_is_omap2())
		offset = OMAP_DMA_CSAC_REG(lch);

	return offset;
}

/*
 * Returns current physical destination address for the given DMA channel.
 * If the channel is running the caller must disable interrupts prior calling
 * this function and process the returned value before re-enabling interrupt to
 * prevent races with the interrupt handler. Note that in continuous mode there
 * is a chance for CDSA_L register overflow inbetween the two reads resulting
 * in incorrect return value.
 */
dma_addr_t omap_get_dma_dst_pos(int lch)
{
	dma_addr_t offset = 0;

	if (cpu_class_is_omap1())
		offset = (dma_addr_t) (OMAP1_DMA_CDSA_L_REG(lch) |
				       (OMAP1_DMA_CDSA_U_REG(lch) << 16));

	if (cpu_class_is_omap2())
		offset = OMAP_DMA_CDAC_REG(lch);

	return offset;
}

/*
 * Returns current source transfer counting for the given DMA channel.
 * Can be used to monitor the progress of a transfer inside a block.
 * It must be called with disabled interrupts.
 */
int omap_get_dma_src_addr_counter(int lch)
{
	return (dma_addr_t) OMAP_DMA_CSAC_REG(lch);
}

int omap_dma_running(void)
{
	int lch;

	/* Check if LCD DMA is running */
	if (cpu_is_omap16xx())
		if (omap_readw(OMAP1610_DMA_LCD_CCR) & OMAP_DMA_CCR_EN)
			return 1;

	for (lch = 0; lch < dma_chan_count; lch++)
		if (OMAP_DMA_CCR_REG(lch) & OMAP_DMA_CCR_EN)
			return 1;

	return 0;
}

/*
 * lch_queue DMA will start right after lch_head one is finished.
 * For this DMA link to start, you still need to start (see omap_start_dma)
 * the first one. That will fire up the entire queue.
 */
void omap_dma_link_lch (int lch_head, int lch_queue)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA linking is not supported in 1510 mode\n");
		BUG();
		return;
	}

	if ((dma_chan[lch_head].dev_id == -1) ||
	    (dma_chan[lch_queue].dev_id == -1)) {
		printk(KERN_ERR "omap_dma: trying to link "
		       "non requested channels\n");
		dump_stack();
	}

	dma_chan[lch_head].next_lch = lch_queue;
}

/*
 * Once the DMA queue is stopped, we can destroy it.
 */
void omap_dma_unlink_lch (int lch_head, int lch_queue)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA linking is not supported in 1510 mode\n");
		BUG();
		return;
	}

	if (dma_chan[lch_head].next_lch != lch_queue ||
	    dma_chan[lch_head].next_lch == -1) {
		printk(KERN_ERR "omap_dma: trying to unlink "
		       "non linked channels\n");
		dump_stack();
	}


	if ((dma_chan[lch_head].flags & OMAP_DMA_ACTIVE) ||
	    (dma_chan[lch_head].flags & OMAP_DMA_ACTIVE)) {
		printk(KERN_ERR "omap_dma: You need to stop the DMA channels "
		       "before unlinking\n");
		dump_stack();
	}

	dma_chan[lch_head].next_lch = -1;
}

#ifndef CONFIG_ARCH_OMAP1
/* Create chain of DMA channesls */
static void create_dma_lch_chain(int lch_head, int lch_queue)
{
	u32 w;

	/* Check if this is the first link in chain */
	if (dma_chan[lch_head].next_linked_ch == -1) {
		dma_chan[lch_head].next_linked_ch = lch_queue;
		dma_chan[lch_head].prev_linked_ch = lch_queue;
		dma_chan[lch_queue].next_linked_ch = lch_head;
		dma_chan[lch_queue].prev_linked_ch = lch_head;
	}

	/* a link exists, link the new channel in circular chain */
	else {
		dma_chan[lch_queue].next_linked_ch =
					dma_chan[lch_head].next_linked_ch;
		dma_chan[lch_queue].prev_linked_ch = lch_head;
		dma_chan[lch_head].next_linked_ch = lch_queue;
		dma_chan[dma_chan[lch_queue].next_linked_ch].prev_linked_ch =
					lch_queue;
	}

	w = OMAP_DMA_CLNK_CTRL_REG(lch_head);
	w &= ~(0x1f);
	w |= lch_queue;
	OMAP_DMA_CLNK_CTRL_REG(lch_head) = w;

	w = OMAP_DMA_CLNK_CTRL_REG(lch_queue);
	w &= ~(0x1f);
	w |= (dma_chan[lch_queue].next_linked_ch);
	OMAP_DMA_CLNK_CTRL_REG(lch_queue) = w;
}

/**
 * @brief omap_request_dma_chain : Request a chain of DMA channels
 *
 * @param dev_id - Device id using the dma channel
 * @param dev_name - Device name
 * @param callback - Call back function
 * @chain_id -
 * @no_of_chans - Number of channels requested
 * @chain_mode - Dynamic or static chaining : OMAP_DMA_STATIC_CHAIN
 * 					      OMAP_DMA_DYNAMIC_CHAIN
 * @params - Channel parameters
 *
 * @return - Succes : 0
 * 	     Failure: -EINVAL/-ENOMEM
 */
int omap_request_dma_chain(int dev_id, const char *dev_name,
			   void (*callback) (int chain_id, u16 ch_status,
					     void *data),
			   int *chain_id, int no_of_chans, int chain_mode,
			   struct omap_dma_channel_params params)
{
	int *channels;
	int i, err;

	/* Is the chain mode valid ? */
	if (chain_mode != OMAP_DMA_STATIC_CHAIN
			&& chain_mode != OMAP_DMA_DYNAMIC_CHAIN) {
		printk(KERN_ERR "Invalid chain mode requested\n");
		return -EINVAL;
	}

	if (unlikely((no_of_chans < 1
			|| no_of_chans > OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid Number of channels requested\n");
		return -EINVAL;
	}

	/* Allocate a queue to maintain the status of the channels
	 * in the chain */
	channels = kmalloc(sizeof(*channels) * no_of_chans, GFP_KERNEL);
	if (channels == NULL) {
		printk(KERN_ERR "omap_dma: No memory for channel queue\n");
		return -ENOMEM;
	}

	/* request and reserve DMA channels for the chain */
	for (i = 0; i < no_of_chans; i++) {
		err = omap_request_dma(dev_id, dev_name,
					callback, 0, &channels[i]);
		if (err < 0) {
			int j;
			for (j = 0; j < i; j++)
				omap_free_dma(channels[j]);
			kfree(channels);
			printk(KERN_ERR "omap_dma: Request failed %d\n", err);
			return err;
		}
		dma_chan[channels[i]].next_linked_ch = -1;
		dma_chan[channels[i]].prev_linked_ch = -1;
		dma_chan[channels[i]].state = DMA_CH_NOTSTARTED;

		/*
		 * Allowing client drivers to set common parameters now,
		 * so that later only relevant (src_start, dest_start
		 * and element count) can be set
		 */
		omap_set_dma_params(channels[i], &params);
	}

	*chain_id = channels[0];
	dma_linked_lch[*chain_id].linked_dmach_q = channels;
	dma_linked_lch[*chain_id].chain_mode = chain_mode;
	dma_linked_lch[*chain_id].chain_state = DMA_CHAIN_NOTSTARTED;
	dma_linked_lch[*chain_id].no_of_lchs_linked = no_of_chans;

	for (i = 0; i < no_of_chans; i++)
		dma_chan[channels[i]].chain_id = *chain_id;

	/* Reset the Queue pointers */
	OMAP_DMA_CHAIN_QINIT(*chain_id);

	/* Set up the chain */
	if (no_of_chans == 1)
		create_dma_lch_chain(channels[0], channels[0]);
	else {
		for (i = 0; i < (no_of_chans - 1); i++)
			create_dma_lch_chain(channels[i], channels[i + 1]);
	}
	return 0;
}
EXPORT_SYMBOL(omap_request_dma_chain);

/**
 * @brief omap_modify_dma_chain_param : Modify the chain's params - Modify the
 * params after setting it. Dont do this while dma is running!!
 *
 * @param chain_id - Chained logical channel id.
 * @param params
 *
 * @return - Success : 0
 * 	     Failure : -EINVAL
 */
int omap_modify_dma_chain_params(int chain_id,
				struct omap_dma_channel_params params)
{
	int *channels;
	u32 i;

	/* Check for input params */
	if (unlikely((chain_id < 0
			|| chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}
	channels = dma_linked_lch[chain_id].linked_dmach_q;

	for (i = 0; i < dma_linked_lch[chain_id].no_of_lchs_linked; i++) {
		/*
		 * Allowing client drivers to set common parameters now,
		 * so that later only relevant (src_start, dest_start
		 * and element count) can be set
		 */
		omap_set_dma_params(channels[i], &params);
	}
	return 0;
}
EXPORT_SYMBOL(omap_modify_dma_chain_params);

/**
 * @brief omap_free_dma_chain - Free all the logical channels in a chain.
 *
 * @param chain_id
 *
 * @return - Success : 0
 * 	     Failure : -EINVAL
 */
int omap_free_dma_chain(int chain_id)
{
	int *channels;
	u32 i;

	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}

	channels = dma_linked_lch[chain_id].linked_dmach_q;
	for (i = 0; i < dma_linked_lch[chain_id].no_of_lchs_linked; i++) {
		dma_chan[channels[i]].next_linked_ch = -1;
		dma_chan[channels[i]].prev_linked_ch = -1;
		dma_chan[channels[i]].chain_id = -1;
		dma_chan[channels[i]].state = DMA_CH_NOTSTARTED;
		omap_free_dma(channels[i]);
	}

	kfree(channels);

	dma_linked_lch[chain_id].linked_dmach_q = NULL;
	dma_linked_lch[chain_id].chain_mode = -1;
	dma_linked_lch[chain_id].chain_state = -1;
	return (0);
}
EXPORT_SYMBOL(omap_free_dma_chain);

/**
 * @brief omap_dma_chain_status - Check if the chain is in
 * active / inactive state.
 * @param chain_id
 *
 * @return - Success : OMAP_DMA_CHAIN_ACTIVE/OMAP_DMA_CHAIN_INACTIVE
 * 	     Failure : -EINVAL
 */
int omap_dma_chain_status(int chain_id)
{
	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}
	pr_debug("CHAINID=%d, qcnt=%d\n", chain_id,
			dma_linked_lch[chain_id].q_count);

	if (OMAP_DMA_CHAIN_QEMPTY(chain_id))
		return OMAP_DMA_CHAIN_INACTIVE;
	return OMAP_DMA_CHAIN_ACTIVE;
}
EXPORT_SYMBOL(omap_dma_chain_status);

/**
 * @brief omap_dma_chain_a_transfer - Get a free channel from a chain,
 * set the params and start the transfer.
 *
 * @param chain_id
 * @param src_start - buffer start address
 * @param dest_start - Dest address
 * @param elem_count
 * @param frame_count
 * @param callbk_data - channel callback parameter data.
 *
 * @return  - Success : 0
 * 	      Failure: -EINVAL/-EBUSY
 */
int omap_dma_chain_a_transfer(int chain_id, int src_start, int dest_start,
			int elem_count, int frame_count, void *callbk_data)
{
	int *channels;
	u32 w, lch;
	int start_dma = 0;

	/* if buffer size is less than 1 then there is
	 * no use of starting the chain */
	if (elem_count < 1) {
		printk(KERN_ERR "Invalid buffer size\n");
		return -EINVAL;
	}

	/* Check for input params */
	if (unlikely((chain_id < 0
			|| chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exist\n");
		return -EINVAL;
	}

	/* Check if all the channels in chain are in use */
	if (OMAP_DMA_CHAIN_QFULL(chain_id))
		return -EBUSY;

	/* Frame count may be negative in case of indexed transfers */
	channels = dma_linked_lch[chain_id].linked_dmach_q;

	/* Get a free channel */
	lch = channels[dma_linked_lch[chain_id].q_tail];

	/* Store the callback data */
	dma_chan[lch].data = callbk_data;

	/* Increment the q_tail */
	OMAP_DMA_CHAIN_INCQTAIL(chain_id);

	/* Set the params to the free channel */
	if (src_start != 0)
		OMAP2_DMA_CSSA_REG(lch) = src_start;
	if (dest_start != 0)
		OMAP2_DMA_CDSA_REG(lch) = dest_start;

	/* Write the buffer size */
	OMAP_DMA_CEN_REG(lch) = elem_count;
	OMAP_DMA_CFN_REG(lch) = frame_count;

	/* If the chain is dynamically linked,
	 * then we may have to start the chain if its not active */
	if (dma_linked_lch[chain_id].chain_mode == OMAP_DMA_DYNAMIC_CHAIN) {

		/* In Dynamic chain, if the chain is not started,
		 * queue the channel */
		if (dma_linked_lch[chain_id].chain_state ==
						DMA_CHAIN_NOTSTARTED) {
			/* Enable the link in previous channel */
			if (dma_chan[dma_chan[lch].prev_linked_ch].state ==
								DMA_CH_QUEUED)
				enable_lnk(dma_chan[lch].prev_linked_ch);
			dma_chan[lch].state = DMA_CH_QUEUED;
		}

		/* Chain is already started, make sure its active,
		 * if not then start the chain */
		else {
			start_dma = 1;

			if (dma_chan[dma_chan[lch].prev_linked_ch].state ==
							DMA_CH_STARTED) {
				enable_lnk(dma_chan[lch].prev_linked_ch);
				dma_chan[lch].state = DMA_CH_QUEUED;
				start_dma = 0;
				if (0 == ((1 << 7) & (OMAP_DMA_CCR_REG
					(dma_chan[lch].prev_linked_ch)))) {
					disable_lnk(dma_chan[lch].
						    prev_linked_ch);
					pr_debug("\n prev ch is stopped\n");
					start_dma = 1;
				}
			}

			else if (dma_chan[dma_chan[lch].prev_linked_ch].state
							== DMA_CH_QUEUED) {
				enable_lnk(dma_chan[lch].prev_linked_ch);
				dma_chan[lch].state = DMA_CH_QUEUED;
				start_dma = 0;
			}
			omap_enable_channel_irq(lch);

			w = OMAP_DMA_CCR_REG(lch);

			if ((0 == (w & (1 << 24))))
				w &= ~(1 << 25);
			else
				w |= (1 << 25);
			if (start_dma == 1) {
				if (0 == (w & (1 << 7))) {
					w |= (1 << 7);
					dma_chan[lch].state = DMA_CH_STARTED;
					pr_debug("starting %d\n", lch);
					OMAP_DMA_CCR_REG(lch) = w;
				} else
					start_dma = 0;
			} else {
				if (0 == (w & (1 << 7)))
					OMAP_DMA_CCR_REG(lch) = w;
			}
			dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
		}
	}
	return 0;
}
EXPORT_SYMBOL(omap_dma_chain_a_transfer);

/**
 * @brief omap_start_dma_chain_transfers - Start the chain
 *
 * @param chain_id
 *
 * @return - Success : 0
 * 	     Failure : -EINVAL/-EBUSY
 */
int omap_start_dma_chain_transfers(int chain_id)
{
	int *channels;
	u32 w, i;

	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	channels = dma_linked_lch[chain_id].linked_dmach_q;

	if (dma_linked_lch[channels[0]].chain_state == DMA_CHAIN_STARTED) {
		printk(KERN_ERR "Chain is already started\n");
		return -EBUSY;
	}

	if (dma_linked_lch[chain_id].chain_mode == OMAP_DMA_STATIC_CHAIN) {
		for (i = 0; i < dma_linked_lch[chain_id].no_of_lchs_linked;
									i++) {
			enable_lnk(channels[i]);
			omap_enable_channel_irq(channels[i]);
		}
	} else {
		omap_enable_channel_irq(channels[0]);
	}

	w = OMAP_DMA_CCR_REG(channels[0]);
	w |= (1 << 7);
	dma_linked_lch[chain_id].chain_state = DMA_CHAIN_STARTED;
	dma_chan[channels[0]].state = DMA_CH_STARTED;

	if ((0 == (w & (1 << 24))))
		w &= ~(1 << 25);
	else
		w |= (1 << 25);
	OMAP_DMA_CCR_REG(channels[0]) = w;

	dma_chan[channels[0]].flags |= OMAP_DMA_ACTIVE;
	return 0;
}
EXPORT_SYMBOL(omap_start_dma_chain_transfers);

/**
 * @brief omap_stop_dma_chain_transfers - Stop the dma transfer of a chain.
 *
 * @param chain_id
 *
 * @return - Success : 0
 * 	     Failure : EINVAL
 */
int omap_stop_dma_chain_transfers(int chain_id)
{
	int *channels;
	u32 w, i;
	u32 sys_cf;

	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}
	channels = dma_linked_lch[chain_id].linked_dmach_q;

	/* DMA Errata:
	 * Special programming model needed to disable DMA before end of block
	 */
	sys_cf = omap_readl(OMAP_DMA4_OCP_SYSCONFIG);
	w = sys_cf;
	/* Middle mode reg set no Standby */
	w &= ~((1 << 12)|(1 << 13));
	omap_writel(w, OMAP_DMA4_OCP_SYSCONFIG);

	for (i = 0; i < dma_linked_lch[chain_id].no_of_lchs_linked; i++) {

		/* Stop the Channel transmission */
		w = OMAP_DMA_CCR_REG(channels[i]);
		w &= ~(1 << 7);
		OMAP_DMA_CCR_REG(channels[i]) = w;

		/* Disable the link in all the channels */
		disable_lnk(channels[i]);
		dma_chan[channels[i]].state = DMA_CH_NOTSTARTED;

	}
	dma_linked_lch[chain_id].chain_state = DMA_CHAIN_NOTSTARTED;

	/* Reset the Queue pointers */
	OMAP_DMA_CHAIN_QINIT(chain_id);

	/* Errata - put in the old value */
	omap_writel(sys_cf, OMAP_DMA4_OCP_SYSCONFIG);
	return 0;
}
EXPORT_SYMBOL(omap_stop_dma_chain_transfers);

/* Get the index of the ongoing DMA in chain */
/**
 * @brief omap_get_dma_chain_index - Get the element and frame index
 * of the ongoing DMA in chain
 *
 * @param chain_id
 * @param ei - Element index
 * @param fi - Frame index
 *
 * @return - Success : 0
 * 	     Failure : -EINVAL
 */
int omap_get_dma_chain_index(int chain_id, int *ei, int *fi)
{
	int lch;
	int *channels;

	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}
	if ((!ei) || (!fi))
		return -EINVAL;

	channels = dma_linked_lch[chain_id].linked_dmach_q;

	/* Get the current channel */
	lch = channels[dma_linked_lch[chain_id].q_head];

	*ei = OMAP2_DMA_CCEN_REG(lch);
	*fi = OMAP2_DMA_CCFN_REG(lch);

	return 0;
}
EXPORT_SYMBOL(omap_get_dma_chain_index);

/**
 * @brief omap_get_dma_chain_dst_pos - Get the destination position of the
 * ongoing DMA in chain
 *
 * @param chain_id
 *
 * @return - Success : Destination position
 * 	     Failure : -EINVAL
 */
int omap_get_dma_chain_dst_pos(int chain_id)
{
	int lch;
	int *channels;

	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}

	channels = dma_linked_lch[chain_id].linked_dmach_q;

	/* Get the current channel */
	lch = channels[dma_linked_lch[chain_id].q_head];

	return (OMAP_DMA_CDAC_REG(lch));
}
EXPORT_SYMBOL(omap_get_dma_chain_dst_pos);

/**
 * @brief omap_get_dma_chain_src_pos - Get the source position
 * of the ongoing DMA in chain
 * @param chain_id
 *
 * @return - Success : Destination position
 * 	     Failure : -EINVAL
 */
int omap_get_dma_chain_src_pos(int chain_id)
{
	int lch;
	int *channels;

	/* Check for input params */
	if (unlikely((chain_id < 0 || chain_id >= OMAP_LOGICAL_DMA_CH_COUNT))) {
		printk(KERN_ERR "Invalid chain id\n");
		return -EINVAL;
	}

	/* Check if the chain exists */
	if (dma_linked_lch[chain_id].linked_dmach_q == NULL) {
		printk(KERN_ERR "Chain doesn't exists\n");
		return -EINVAL;
	}

	channels = dma_linked_lch[chain_id].linked_dmach_q;

	/* Get the current channel */
	lch = channels[dma_linked_lch[chain_id].q_head];

	return (OMAP_DMA_CSAC_REG(lch));
}
EXPORT_SYMBOL(omap_get_dma_chain_src_pos);
#endif

/*----------------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_OMAP1

static int omap1_dma_handle_ch(int ch)
{
	u16 csr;

	if (enable_1510_mode && ch >= 6) {
		csr = dma_chan[ch].saved_csr;
		dma_chan[ch].saved_csr = 0;
	} else
		csr = OMAP_DMA_CSR_REG(ch);
	if (enable_1510_mode && ch <= 2 && (csr >> 7) != 0) {
		dma_chan[ch + 6].saved_csr = csr >> 7;
		csr &= 0x7f;
	}
	if ((csr & 0x3f) == 0)
		return 0;
	if (unlikely(dma_chan[ch].dev_id == -1)) {
		printk(KERN_WARNING "Spurious interrupt from DMA channel "
		       "%d (CSR %04x)\n", ch, csr);
		return 0;
	}
	if (unlikely(csr & OMAP1_DMA_TOUT_IRQ))
		printk(KERN_WARNING "DMA timeout with device %d\n",
		       dma_chan[ch].dev_id);
	if (unlikely(csr & OMAP_DMA_DROP_IRQ))
		printk(KERN_WARNING "DMA synchronization event drop occurred "
		       "with device %d\n", dma_chan[ch].dev_id);
	if (likely(csr & OMAP_DMA_BLOCK_IRQ))
		dma_chan[ch].flags &= ~OMAP_DMA_ACTIVE;
	if (likely(dma_chan[ch].callback != NULL))
		dma_chan[ch].callback(ch, csr, dma_chan[ch].data);
	return 1;
}

static irqreturn_t omap1_dma_irq_handler(int irq, void *dev_id)
{
	int ch = ((int) dev_id) - 1;
	int handled = 0;

	for (;;) {
		int handled_now = 0;

		handled_now += omap1_dma_handle_ch(ch);
		if (enable_1510_mode && dma_chan[ch + 6].saved_csr)
			handled_now += omap1_dma_handle_ch(ch + 6);
		if (!handled_now)
			break;
		handled += handled_now;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

#else
#define omap1_dma_irq_handler	NULL
#endif

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)

static int omap2_dma_handle_ch(int ch)
{
	u32 status = OMAP_DMA_CSR_REG(ch);

	if (!status) {
		if (printk_ratelimit())
			printk(KERN_WARNING "Spurious DMA IRQ for lch %d\n", ch);
		omap_writel(1 << ch, OMAP_DMA4_IRQSTATUS_L0);
		return 0;
	}
	if (unlikely(dma_chan[ch].dev_id == -1)) {
		if (printk_ratelimit())
			printk(KERN_WARNING "IRQ %04x for non-allocated DMA"
					"channel %d\n", status, ch);
		return 0;
	}
	if (unlikely(status & OMAP_DMA_DROP_IRQ))
		printk(KERN_INFO
		       "DMA synchronization event drop occurred with device "
		       "%d\n", dma_chan[ch].dev_id);
	if (unlikely(status & OMAP2_DMA_TRANS_ERR_IRQ))
		printk(KERN_INFO "DMA transaction error with device %d\n",
		       dma_chan[ch].dev_id);
	if (unlikely(status & OMAP2_DMA_SECURE_ERR_IRQ))
		printk(KERN_INFO "DMA secure error with device %d\n",
		       dma_chan[ch].dev_id);
	if (unlikely(status & OMAP2_DMA_MISALIGNED_ERR_IRQ))
		printk(KERN_INFO "DMA misaligned error with device %d\n",
		       dma_chan[ch].dev_id);

	OMAP_DMA_CSR_REG(ch) = OMAP2_DMA_CSR_CLEAR_MASK;
	omap_writel(1 << ch, OMAP_DMA4_IRQSTATUS_L0);

	/* If the ch is not chained then chain_id will be -1 */
	if (dma_chan[ch].chain_id != -1) {
		int chain_id = dma_chan[ch].chain_id;
		dma_chan[ch].state = DMA_CH_NOTSTARTED;
		if (OMAP_DMA_CLNK_CTRL_REG(ch) & (1 << 15))
			dma_chan[dma_chan[ch].next_linked_ch].state =
							DMA_CH_STARTED;
		if (dma_linked_lch[chain_id].chain_mode ==
						OMAP_DMA_DYNAMIC_CHAIN)
			disable_lnk(ch);

		if (!OMAP_DMA_CHAIN_QEMPTY(chain_id))
			OMAP_DMA_CHAIN_INCQHEAD(chain_id);

		status = OMAP_DMA_CSR_REG(ch);
	}

	if (likely(dma_chan[ch].callback != NULL))
		dma_chan[ch].callback(ch, status, dma_chan[ch].data);

	OMAP_DMA_CSR_REG(ch) = status;

	return 0;
}

/* STATUS register count is from 1-32 while our is 0-31 */
static irqreturn_t omap2_dma_irq_handler(int irq, void *dev_id)
{
	u32 val;
	int i;

	val = omap_readl(OMAP_DMA4_IRQSTATUS_L0);
	if (val == 0) {
		if (printk_ratelimit())
			printk(KERN_WARNING "Spurious DMA IRQ\n");
		return IRQ_HANDLED;
	}
	for (i = 0; i < OMAP_LOGICAL_DMA_CH_COUNT && val != 0; i++) {
		if (val & 1)
			omap2_dma_handle_ch(i);
		val >>= 1;
	}

	return IRQ_HANDLED;
}

static struct irqaction omap24xx_dma_irq = {
	.name = "DMA",
	.handler = omap2_dma_irq_handler,
	.flags = IRQF_DISABLED
};

#else
static struct irqaction omap24xx_dma_irq;
#endif

/*----------------------------------------------------------------------------*/

static struct lcd_dma_info {
	spinlock_t lock;
	int reserved;
	void (* callback)(u16 status, void *data);
	void *cb_data;

	int active;
	unsigned long addr, size;
	int rotate, data_type, xres, yres;
	int vxres;
	int mirror;
	int xscale, yscale;
	int ext_ctrl;
	int src_port;
	int single_transfer;
} lcd_dma;

void omap_set_lcd_dma_b1(unsigned long addr, u16 fb_xres, u16 fb_yres,
			 int data_type)
{
	lcd_dma.addr = addr;
	lcd_dma.data_type = data_type;
	lcd_dma.xres = fb_xres;
	lcd_dma.yres = fb_yres;
}

void omap_set_lcd_dma_src_port(int port)
{
	lcd_dma.src_port = port;
}

void omap_set_lcd_dma_ext_controller(int external)
{
	lcd_dma.ext_ctrl = external;
}

void omap_set_lcd_dma_single_transfer(int single)
{
	lcd_dma.single_transfer = single;
}


void omap_set_lcd_dma_b1_rotation(int rotate)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA rotation is not supported in 1510 mode\n");
		BUG();
		return;
	}
	lcd_dma.rotate = rotate;
}

void omap_set_lcd_dma_b1_mirror(int mirror)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA mirror is not supported in 1510 mode\n");
		BUG();
	}
	lcd_dma.mirror = mirror;
}

void omap_set_lcd_dma_b1_vxres(unsigned long vxres)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA virtual resulotion is not supported "
				"in 1510 mode\n");
		BUG();
	}
	lcd_dma.vxres = vxres;
}

void omap_set_lcd_dma_b1_scale(unsigned int xscale, unsigned int yscale)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA scale is not supported in 1510 mode\n");
		BUG();
	}
	lcd_dma.xscale = xscale;
	lcd_dma.yscale = yscale;
}

static void set_b1_regs(void)
{
	unsigned long top, bottom;
	int es;
	u16 w;
	unsigned long en, fn;
	long ei, fi;
	unsigned long vxres;
	unsigned int xscale, yscale;

	switch (lcd_dma.data_type) {
	case OMAP_DMA_DATA_TYPE_S8:
		es = 1;
		break;
	case OMAP_DMA_DATA_TYPE_S16:
		es = 2;
		break;
	case OMAP_DMA_DATA_TYPE_S32:
		es = 4;
		break;
	default:
		BUG();
		return;
	}

	vxres = lcd_dma.vxres ? lcd_dma.vxres : lcd_dma.xres;
	xscale = lcd_dma.xscale ? lcd_dma.xscale : 1;
	yscale = lcd_dma.yscale ? lcd_dma.yscale : 1;
	BUG_ON(vxres < lcd_dma.xres);
#define PIXADDR(x,y) (lcd_dma.addr + ((y) * vxres * yscale + (x) * xscale) * es)
#define PIXSTEP(sx, sy, dx, dy) (PIXADDR(dx, dy) - PIXADDR(sx, sy) - es + 1)
	switch (lcd_dma.rotate) {
	case 0:
		if (!lcd_dma.mirror) {
			top = PIXADDR(0, 0);
			bottom = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			/* 1510 DMA requires the bottom address to be 2 more
			 * than the actual last memory access location. */
			if (omap_dma_in_1510_mode() &&
			    lcd_dma.data_type == OMAP_DMA_DATA_TYPE_S32)
				bottom += 2;
			ei = PIXSTEP(0, 0, 1, 0);
			fi = PIXSTEP(lcd_dma.xres - 1, 0, 0, 1);
		} else {
			top = PIXADDR(lcd_dma.xres - 1, 0);
			bottom = PIXADDR(0, lcd_dma.yres - 1);
			ei = PIXSTEP(1, 0, 0, 0);
			fi = PIXSTEP(0, 0, lcd_dma.xres - 1, 1);
		}
		en = lcd_dma.xres;
		fn = lcd_dma.yres;
		break;
	case 90:
		if (!lcd_dma.mirror) {
			top = PIXADDR(0, lcd_dma.yres - 1);
			bottom = PIXADDR(lcd_dma.xres - 1, 0);
			ei = PIXSTEP(0, 1, 0, 0);
			fi = PIXSTEP(0, 0, 1, lcd_dma.yres - 1);
		} else {
			top = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			bottom = PIXADDR(0, 0);
			ei = PIXSTEP(0, 1, 0, 0);
			fi = PIXSTEP(1, 0, 0, lcd_dma.yres - 1);
		}
		en = lcd_dma.yres;
		fn = lcd_dma.xres;
		break;
	case 180:
		if (!lcd_dma.mirror) {
			top = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			bottom = PIXADDR(0, 0);
			ei = PIXSTEP(1, 0, 0, 0);
			fi = PIXSTEP(0, 1, lcd_dma.xres - 1, 0);
		} else {
			top = PIXADDR(0, lcd_dma.yres - 1);
			bottom = PIXADDR(lcd_dma.xres - 1, 0);
			ei = PIXSTEP(0, 0, 1, 0);
			fi = PIXSTEP(lcd_dma.xres - 1, 1, 0, 0);
		}
		en = lcd_dma.xres;
		fn = lcd_dma.yres;
		break;
	case 270:
		if (!lcd_dma.mirror) {
			top = PIXADDR(lcd_dma.xres - 1, 0);
			bottom = PIXADDR(0, lcd_dma.yres - 1);
			ei = PIXSTEP(0, 0, 0, 1);
			fi = PIXSTEP(1, lcd_dma.yres - 1, 0, 0);
		} else {
			top = PIXADDR(0, 0);
			bottom = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			ei = PIXSTEP(0, 0, 0, 1);
			fi = PIXSTEP(0, lcd_dma.yres - 1, 1, 0);
		}
		en = lcd_dma.yres;
		fn = lcd_dma.xres;
		break;
	default:
		BUG();
		return;	/* Suppress warning about uninitialized vars */
	}

	if (omap_dma_in_1510_mode()) {
		omap_writew(top >> 16, OMAP1510_DMA_LCD_TOP_F1_U);
		omap_writew(top, OMAP1510_DMA_LCD_TOP_F1_L);
		omap_writew(bottom >> 16, OMAP1510_DMA_LCD_BOT_F1_U);
		omap_writew(bottom, OMAP1510_DMA_LCD_BOT_F1_L);

		return;
	}

	/* 1610 regs */
	omap_writew(top >> 16, OMAP1610_DMA_LCD_TOP_B1_U);
	omap_writew(top, OMAP1610_DMA_LCD_TOP_B1_L);
	omap_writew(bottom >> 16, OMAP1610_DMA_LCD_BOT_B1_U);
	omap_writew(bottom, OMAP1610_DMA_LCD_BOT_B1_L);

	omap_writew(en, OMAP1610_DMA_LCD_SRC_EN_B1);
	omap_writew(fn, OMAP1610_DMA_LCD_SRC_FN_B1);

	w = omap_readw(OMAP1610_DMA_LCD_CSDP);
	w &= ~0x03;
	w |= lcd_dma.data_type;
	omap_writew(w, OMAP1610_DMA_LCD_CSDP);

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	/* Always set the source port as SDRAM for now*/
	w &= ~(0x03 << 6);
	if (lcd_dma.callback != NULL)
		w |= 1 << 1;		/* Block interrupt enable */
	else
		w &= ~(1 << 1);
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);

	if (!(lcd_dma.rotate || lcd_dma.mirror ||
	      lcd_dma.vxres || lcd_dma.xscale || lcd_dma.yscale))
		return;

	w = omap_readw(OMAP1610_DMA_LCD_CCR);
	/* Set the double-indexed addressing mode */
	w |= (0x03 << 12);
	omap_writew(w, OMAP1610_DMA_LCD_CCR);

	omap_writew(ei, OMAP1610_DMA_LCD_SRC_EI_B1);
	omap_writew(fi >> 16, OMAP1610_DMA_LCD_SRC_FI_B1_U);
	omap_writew(fi, OMAP1610_DMA_LCD_SRC_FI_B1_L);
}

static irqreturn_t lcd_dma_irq_handler(int irq, void *dev_id)
{
	u16 w;

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	if (unlikely(!(w & (1 << 3)))) {
		printk(KERN_WARNING "Spurious LCD DMA IRQ\n");
		return IRQ_NONE;
	}
	/* Ack the IRQ */
	w |= (1 << 3);
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);
	lcd_dma.active = 0;
	if (lcd_dma.callback != NULL)
		lcd_dma.callback(w, lcd_dma.cb_data);

	return IRQ_HANDLED;
}

int omap_request_lcd_dma(void (* callback)(u16 status, void *data),
			 void *data)
{
	spin_lock_irq(&lcd_dma.lock);
	if (lcd_dma.reserved) {
		spin_unlock_irq(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA channel already reserved\n");
		BUG();
		return -EBUSY;
	}
	lcd_dma.reserved = 1;
	spin_unlock_irq(&lcd_dma.lock);
	lcd_dma.callback = callback;
	lcd_dma.cb_data = data;
	lcd_dma.active = 0;
	lcd_dma.single_transfer = 0;
	lcd_dma.rotate = 0;
	lcd_dma.vxres = 0;
	lcd_dma.mirror = 0;
	lcd_dma.xscale = 0;
	lcd_dma.yscale = 0;
	lcd_dma.ext_ctrl = 0;
	lcd_dma.src_port = 0;

	return 0;
}

void omap_free_lcd_dma(void)
{
	spin_lock(&lcd_dma.lock);
	if (!lcd_dma.reserved) {
		spin_unlock(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA is not reserved\n");
		BUG();
		return;
	}
	if (!enable_1510_mode)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) & ~1,
			    OMAP1610_DMA_LCD_CCR);
	lcd_dma.reserved = 0;
	spin_unlock(&lcd_dma.lock);
}

void omap_enable_lcd_dma(void)
{
	u16 w;

	/* Set the Enable bit only if an external controller is
	 * connected. Otherwise the OMAP internal controller will
	 * start the transfer when it gets enabled.
	 */
	if (enable_1510_mode || !lcd_dma.ext_ctrl)
		return;

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	w |= 1 << 8;
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);

	lcd_dma.active = 1;

	w = omap_readw(OMAP1610_DMA_LCD_CCR);
	w |= 1 << 7;
	omap_writew(w, OMAP1610_DMA_LCD_CCR);
}

void omap_setup_lcd_dma(void)
{
	BUG_ON(lcd_dma.active);
	if (!enable_1510_mode) {
		/* Set some reasonable defaults */
		omap_writew(0x5440, OMAP1610_DMA_LCD_CCR);
		omap_writew(0x9102, OMAP1610_DMA_LCD_CSDP);
		omap_writew(0x0004, OMAP1610_DMA_LCD_LCH_CTRL);
	}
	set_b1_regs();
	if (!enable_1510_mode) {
		u16 w;

		w = omap_readw(OMAP1610_DMA_LCD_CCR);
		/* If DMA was already active set the end_prog bit to have
		 * the programmed register set loaded into the active
		 * register set.
		 */
		w |= 1 << 11;		/* End_prog */
		if (!lcd_dma.single_transfer)
	        	w |= (3 << 8);	/* Auto_init, repeat */
		omap_writew(w, OMAP1610_DMA_LCD_CCR);
	}
}

void omap_stop_lcd_dma(void)
{
	u16 w;

	lcd_dma.active = 0;
	if (enable_1510_mode || !lcd_dma.ext_ctrl)
		return;

	w = omap_readw(OMAP1610_DMA_LCD_CCR);
	w &= ~(1 << 7);
	omap_writew(w, OMAP1610_DMA_LCD_CCR);

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	w &= ~(1 << 8);
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);
}

/*----------------------------------------------------------------------------*/

static int __init omap_init_dma(void)
{
	int ch, r;

	if (cpu_is_omap15xx()) {
		printk(KERN_INFO "DMA support for OMAP15xx initialized\n");
		dma_chan_count = 9;
		enable_1510_mode = 1;
	} else if (cpu_is_omap16xx() || cpu_is_omap730()) {
		printk(KERN_INFO "OMAP DMA hardware version %d\n",
		       omap_readw(OMAP_DMA_HW_ID));
		printk(KERN_INFO "DMA capabilities: %08x:%08x:%04x:%04x:%04x\n",
		       (omap_readw(OMAP_DMA_CAPS_0_U) << 16) |
		       omap_readw(OMAP_DMA_CAPS_0_L),
		       (omap_readw(OMAP_DMA_CAPS_1_U) << 16) |
		       omap_readw(OMAP_DMA_CAPS_1_L),
		       omap_readw(OMAP_DMA_CAPS_2), omap_readw(OMAP_DMA_CAPS_3),
		       omap_readw(OMAP_DMA_CAPS_4));
		if (!enable_1510_mode) {
			u16 w;

			/* Disable OMAP 3.0/3.1 compatibility mode. */
			w = omap_readw(OMAP_DMA_GSCR);
			w |= 1 << 3;
			omap_writew(w, OMAP_DMA_GSCR);
			dma_chan_count = 16;
		} else
			dma_chan_count = 9;
		if (cpu_is_omap16xx()) {
			u16 w;

			/* this would prevent OMAP sleep */
			w = omap_readw(OMAP1610_DMA_LCD_CTRL);
			w &= ~(1 << 8);
			omap_writew(w, OMAP1610_DMA_LCD_CTRL);
		}
	} else if (cpu_class_is_omap2()) {
		u8 revision = omap_readb(OMAP_DMA4_REVISION);
		printk(KERN_INFO "OMAP DMA hardware revision %d.%d\n",
		       revision >> 4, revision & 0xf);
		dma_chan_count = OMAP_LOGICAL_DMA_CH_COUNT;
	} else {
		dma_chan_count = 0;
		return 0;
	}

	memset(&lcd_dma, 0, sizeof(lcd_dma));
	spin_lock_init(&lcd_dma.lock);
	spin_lock_init(&dma_chan_lock);
	memset(&dma_chan, 0, sizeof(dma_chan));

	for (ch = 0; ch < dma_chan_count; ch++) {
		omap_clear_dma(ch);
		dma_chan[ch].dev_id = -1;
		dma_chan[ch].next_lch = -1;

		if (ch >= 6 && enable_1510_mode)
			continue;

		if (cpu_class_is_omap1()) {
			/* request_irq() doesn't like dev_id (ie. ch) being
			 * zero, so we have to kludge around this. */
			r = request_irq(omap1_dma_irq[ch],
					omap1_dma_irq_handler, 0, "DMA",
					(void *) (ch + 1));
			if (r != 0) {
				int i;

				printk(KERN_ERR "unable to request IRQ %d "
				       "for DMA (error %d)\n",
				       omap1_dma_irq[ch], r);
				for (i = 0; i < ch; i++)
					free_irq(omap1_dma_irq[i],
						 (void *) (i + 1));
				return r;
			}
		}
	}

	if (cpu_is_omap2430() || cpu_is_omap34xx())
		omap_dma_set_global_params(DMA_DEFAULT_ARB_RATE,
				DMA_DEFAULT_FIFO_DEPTH, 0);

	if (cpu_class_is_omap2())
		setup_irq(INT_24XX_SDMA_IRQ0, &omap24xx_dma_irq);

	/* FIXME: Update LCD DMA to work on 24xx */
	if (cpu_class_is_omap1()) {
		r = request_irq(INT_DMA_LCD, lcd_dma_irq_handler, 0,
				"LCD DMA", NULL);
		if (r != 0) {
			int i;

			printk(KERN_ERR "unable to request IRQ for LCD DMA "
			       "(error %d)\n", r);
			for (i = 0; i < dma_chan_count; i++)
				free_irq(omap1_dma_irq[i], (void *) (i + 1));
			return r;
		}
	}

	return 0;
}

arch_initcall(omap_init_dma);

EXPORT_SYMBOL(omap_get_dma_src_pos);
EXPORT_SYMBOL(omap_get_dma_dst_pos);
EXPORT_SYMBOL(omap_get_dma_src_addr_counter);
EXPORT_SYMBOL(omap_clear_dma);
EXPORT_SYMBOL(omap_set_dma_priority);
EXPORT_SYMBOL(omap_request_dma);
EXPORT_SYMBOL(omap_free_dma);
EXPORT_SYMBOL(omap_start_dma);
EXPORT_SYMBOL(omap_stop_dma);
EXPORT_SYMBOL(omap_set_dma_callback);
EXPORT_SYMBOL(omap_enable_dma_irq);
EXPORT_SYMBOL(omap_disable_dma_irq);

EXPORT_SYMBOL(omap_set_dma_transfer_params);
EXPORT_SYMBOL(omap_set_dma_color_mode);
EXPORT_SYMBOL(omap_set_dma_write_mode);

EXPORT_SYMBOL(omap_set_dma_src_params);
EXPORT_SYMBOL(omap_set_dma_src_index);
EXPORT_SYMBOL(omap_set_dma_src_data_pack);
EXPORT_SYMBOL(omap_set_dma_src_burst_mode);

EXPORT_SYMBOL(omap_set_dma_dest_params);
EXPORT_SYMBOL(omap_set_dma_dest_index);
EXPORT_SYMBOL(omap_set_dma_dest_data_pack);
EXPORT_SYMBOL(omap_set_dma_dest_burst_mode);

EXPORT_SYMBOL(omap_set_dma_params);

EXPORT_SYMBOL(omap_dma_link_lch);
EXPORT_SYMBOL(omap_dma_unlink_lch);

EXPORT_SYMBOL(omap_request_lcd_dma);
EXPORT_SYMBOL(omap_free_lcd_dma);
EXPORT_SYMBOL(omap_enable_lcd_dma);
EXPORT_SYMBOL(omap_setup_lcd_dma);
EXPORT_SYMBOL(omap_stop_lcd_dma);
EXPORT_SYMBOL(omap_set_lcd_dma_b1);
EXPORT_SYMBOL(omap_set_lcd_dma_single_transfer);
EXPORT_SYMBOL(omap_set_lcd_dma_ext_controller);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_rotation);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_vxres);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_scale);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_mirror);

