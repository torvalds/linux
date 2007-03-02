/*
 * linux/arch/arm/plat-omap/mcbsp.c
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/dma.h>
#include <asm/arch/mux.h>
#include <asm/arch/irqs.h>
#include <asm/arch/dsp_common.h>
#include <asm/arch/mcbsp.h>

#ifdef CONFIG_MCBSP_DEBUG
#define DBG(x...)	printk(x)
#else
#define DBG(x...)			do { } while (0)
#endif

struct omap_mcbsp {
	u32                          io_base;
	u8                           id;
	u8                           free;
	omap_mcbsp_word_length       rx_word_length;
	omap_mcbsp_word_length       tx_word_length;

	omap_mcbsp_io_type_t         io_type; /* IRQ or poll */
	/* IRQ based TX/RX */
	int                          rx_irq;
	int                          tx_irq;

	/* DMA stuff */
	u8                           dma_rx_sync;
	short                        dma_rx_lch;
	u8                           dma_tx_sync;
	short                        dma_tx_lch;

	/* Completion queues */
	struct completion            tx_irq_completion;
	struct completion            rx_irq_completion;
	struct completion            tx_dma_completion;
	struct completion            rx_dma_completion;

	spinlock_t                   lock;
};

static struct omap_mcbsp mcbsp[OMAP_MAX_MCBSP_COUNT];
#ifdef CONFIG_ARCH_OMAP1
static struct clk *mcbsp_dsp_ck = 0;
static struct clk *mcbsp_api_ck = 0;
static struct clk *mcbsp_dspxor_ck = 0;
#endif
#ifdef CONFIG_ARCH_OMAP2
static struct clk *mcbsp1_ick = 0;
static struct clk *mcbsp1_fck = 0;
static struct clk *mcbsp2_ick = 0;
static struct clk *mcbsp2_fck = 0;
#endif

static void omap_mcbsp_dump_reg(u8 id)
{
	DBG("**** MCBSP%d regs ****\n", mcbsp[id].id);
	DBG("DRR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DRR2));
	DBG("DRR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DRR1));
	DBG("DXR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DXR2));
	DBG("DXR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, DXR1));
	DBG("SPCR2: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SPCR2));
	DBG("SPCR1: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SPCR1));
	DBG("RCR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, RCR2));
	DBG("RCR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, RCR1));
	DBG("XCR2:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, XCR2));
	DBG("XCR1:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, XCR1));
	DBG("SRGR2: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SRGR2));
	DBG("SRGR1: 0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, SRGR1));
	DBG("PCR0:  0x%04x\n", OMAP_MCBSP_READ(mcbsp[id].io_base, PCR0));
	DBG("***********************\n");
}

static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp * mcbsp_tx = (struct omap_mcbsp *)(dev_id);

	DBG("TX IRQ callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_tx->io_base, SPCR2));

	complete(&mcbsp_tx->tx_irq_completion);
	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp * mcbsp_rx = (struct omap_mcbsp *)(dev_id);

	DBG("RX IRQ callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_rx->io_base, SPCR2));

	complete(&mcbsp_rx->rx_irq_completion);
	return IRQ_HANDLED;
}

static void omap_mcbsp_tx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp * mcbsp_dma_tx = (struct omap_mcbsp *)(data);

	DBG("TX DMA callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_dma_tx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_tx->dma_tx_lch);
	mcbsp_dma_tx->dma_tx_lch = -1;

	complete(&mcbsp_dma_tx->tx_dma_completion);
}

static void omap_mcbsp_rx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp * mcbsp_dma_rx = (struct omap_mcbsp *)(data);

	DBG("RX DMA callback : 0x%x\n", OMAP_MCBSP_READ(mcbsp_dma_rx->io_base, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_rx->dma_rx_lch);
	mcbsp_dma_rx->dma_rx_lch = -1;

	complete(&mcbsp_dma_rx->rx_dma_completion);
}


/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */

void omap_mcbsp_config(unsigned int id, const struct omap_mcbsp_reg_cfg * config)
{
	u32 io_base = mcbsp[id].io_base;

	DBG("OMAP-McBSP: McBSP%d  io_base: 0x%8x\n", id+1, io_base);

	/* We write the given config */
	OMAP_MCBSP_WRITE(io_base, SPCR2, config->spcr2);
	OMAP_MCBSP_WRITE(io_base, SPCR1, config->spcr1);
	OMAP_MCBSP_WRITE(io_base, RCR2, config->rcr2);
	OMAP_MCBSP_WRITE(io_base, RCR1, config->rcr1);
	OMAP_MCBSP_WRITE(io_base, XCR2, config->xcr2);
	OMAP_MCBSP_WRITE(io_base, XCR1, config->xcr1);
	OMAP_MCBSP_WRITE(io_base, SRGR2, config->srgr2);
	OMAP_MCBSP_WRITE(io_base, SRGR1, config->srgr1);
	OMAP_MCBSP_WRITE(io_base, MCR2, config->mcr2);
	OMAP_MCBSP_WRITE(io_base, MCR1, config->mcr1);
	OMAP_MCBSP_WRITE(io_base, PCR0, config->pcr0);
}



static int omap_mcbsp_check(unsigned int id)
{
	if (cpu_is_omap730()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 1) {
		       printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n", id + 1);
		       return -1;
		}
		return 0;
	}

	if (cpu_is_omap15xx() || cpu_is_omap16xx() || cpu_is_omap24xx()) {
		if (id > OMAP_MAX_MCBSP_COUNT) {
			printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n", id + 1);
			return -1;
		}
		return 0;
	}

	return -1;
}

#ifdef CONFIG_ARCH_OMAP1
static void omap_mcbsp_dsp_request(void)
{
	if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
		clk_enable(mcbsp_dsp_ck);
		clk_enable(mcbsp_api_ck);

		/* enable 12MHz clock to mcbsp 1 & 3 */
		clk_enable(mcbsp_dspxor_ck);

		/*
		 * DSP external peripheral reset
		 * FIXME: This should be moved to dsp code
		 */
		__raw_writew(__raw_readw(DSP_RSTCT2) | 1 | 1 << 1,
			     DSP_RSTCT2);
	}
}

static void omap_mcbsp_dsp_free(void)
{
	if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
		clk_disable(mcbsp_dspxor_ck);
		clk_disable(mcbsp_dsp_ck);
		clk_disable(mcbsp_api_ck);
	}
}
#endif

#ifdef CONFIG_ARCH_OMAP2
static void omap2_mcbsp2_mux_setup(void)
{
	omap_cfg_reg(Y15_24XX_MCBSP2_CLKX);
	omap_cfg_reg(R14_24XX_MCBSP2_FSX);
	omap_cfg_reg(W15_24XX_MCBSP2_DR);
	omap_cfg_reg(V15_24XX_MCBSP2_DX);
	omap_cfg_reg(V14_24XX_GPIO117);
}
#endif

/*
 * We can choose between IRQ based or polled IO.
 * This needs to be called before omap_mcbsp_request().
 */
int omap_mcbsp_set_io_type(unsigned int id, omap_mcbsp_io_type_t io_type)
{
	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	spin_lock(&mcbsp[id].lock);

	if (!mcbsp[id].free) {
		printk (KERN_ERR "OMAP-McBSP: McBSP%d is currently in use\n", id + 1);
		spin_unlock(&mcbsp[id].lock);
		return -EINVAL;
	}

	mcbsp[id].io_type = io_type;

	spin_unlock(&mcbsp[id].lock);

	return 0;
}

int omap_mcbsp_request(unsigned int id)
{
	int err;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

#ifdef CONFIG_ARCH_OMAP1
	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
		omap_mcbsp_dsp_request();
#endif

#ifdef CONFIG_ARCH_OMAP2
	if (cpu_is_omap24xx()) {
		if (id == OMAP_MCBSP1) {
			clk_enable(mcbsp1_ick);
			clk_enable(mcbsp1_fck);
		} else {
			clk_enable(mcbsp2_ick);
			clk_enable(mcbsp2_fck);
		}
	}
#endif

	spin_lock(&mcbsp[id].lock);
	if (!mcbsp[id].free) {
		printk (KERN_ERR "OMAP-McBSP: McBSP%d is currently in use\n", id + 1);
		spin_unlock(&mcbsp[id].lock);
		return -1;
	}

	mcbsp[id].free = 0;
	spin_unlock(&mcbsp[id].lock);

	if (mcbsp[id].io_type == OMAP_MCBSP_IRQ_IO) {
		/* We need to get IRQs here */
		err = request_irq(mcbsp[id].tx_irq, omap_mcbsp_tx_irq_handler, 0,
				  "McBSP",
				  (void *) (&mcbsp[id]));
		if (err != 0) {
			printk(KERN_ERR "OMAP-McBSP: Unable to request TX IRQ %d for McBSP%d\n",
			       mcbsp[id].tx_irq, mcbsp[id].id);
			return err;
		}

		init_completion(&(mcbsp[id].tx_irq_completion));


		err = request_irq(mcbsp[id].rx_irq, omap_mcbsp_rx_irq_handler, 0,
				  "McBSP",
				  (void *) (&mcbsp[id]));
		if (err != 0) {
			printk(KERN_ERR "OMAP-McBSP: Unable to request RX IRQ %d for McBSP%d\n",
			       mcbsp[id].rx_irq, mcbsp[id].id);
			free_irq(mcbsp[id].tx_irq, (void *) (&mcbsp[id]));
			return err;
		}

		init_completion(&(mcbsp[id].rx_irq_completion));
	}

	return 0;

}

void omap_mcbsp_free(unsigned int id)
{
	if (omap_mcbsp_check(id) < 0)
		return;

#ifdef CONFIG_ARCH_OMAP1
	if (cpu_class_is_omap1()) {
		if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
			omap_mcbsp_dsp_free();
	}
#endif

#ifdef CONFIG_ARCH_OMAP2
	if (cpu_is_omap24xx()) {
		if (id == OMAP_MCBSP1) {
			clk_disable(mcbsp1_ick);
			clk_disable(mcbsp1_fck);
		} else {
			clk_disable(mcbsp2_ick);
			clk_disable(mcbsp2_fck);
		}
	}
#endif

	spin_lock(&mcbsp[id].lock);
	if (mcbsp[id].free) {
		printk (KERN_ERR "OMAP-McBSP: McBSP%d was not reserved\n", id + 1);
		spin_unlock(&mcbsp[id].lock);
		return;
	}

	mcbsp[id].free = 1;
	spin_unlock(&mcbsp[id].lock);

	if (mcbsp[id].io_type == OMAP_MCBSP_IRQ_IO) {
		/* Free IRQs */
		free_irq(mcbsp[id].rx_irq, (void *) (&mcbsp[id]));
		free_irq(mcbsp[id].tx_irq, (void *) (&mcbsp[id]));
	}
}

/*
 * Here we start the McBSP, by enabling the sample
 * generator, both transmitter and receivers,
 * and the frame sync.
 */
void omap_mcbsp_start(unsigned int id)
{
	u32 io_base;
	u16 w;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

	mcbsp[id].rx_word_length = ((OMAP_MCBSP_READ(io_base, RCR1) >> 5) & 0x7);
	mcbsp[id].tx_word_length = ((OMAP_MCBSP_READ(io_base, XCR1) >> 5) & 0x7);

	/* Start the sample generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 6));

	/* Enable transmitter and receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | 1);

	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w | 1);

	udelay(100);

	/* Start frame sync */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w | (1 << 7));

	/* Dump McBSP Regs */
	omap_mcbsp_dump_reg(id);

}

void omap_mcbsp_stop(unsigned int id)
{
	u32 io_base;
	u16 w;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

        /* Reset transmitter */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1));

	/* Reset receiver */
	w = OMAP_MCBSP_READ(io_base, SPCR1);
	OMAP_MCBSP_WRITE(io_base, SPCR1, w & ~(1));

	/* Reset the sample rate generator */
	w = OMAP_MCBSP_READ(io_base, SPCR2);
	OMAP_MCBSP_WRITE(io_base, SPCR2, w & ~(1 << 6));
}


/* polled mcbsp i/o operations */
int omap_mcbsp_pollwrite(unsigned int id, u16 buf)
{
	u32 base = mcbsp[id].io_base;
	writew(buf, base + OMAP_MCBSP_REG_DXR1);
	/* if frame sync error - clear the error */
	if (readw(base + OMAP_MCBSP_REG_SPCR2) & XSYNC_ERR) {
		/* clear error */
		writew(readw(base + OMAP_MCBSP_REG_SPCR2) & (~XSYNC_ERR),
		       base + OMAP_MCBSP_REG_SPCR2);
		/* resend */
		return -1;
	} else {
		/* wait for transmit confirmation */
		int attemps = 0;
		while (!(readw(base + OMAP_MCBSP_REG_SPCR2) & XRDY)) {
			if (attemps++ > 1000) {
				writew(readw(base + OMAP_MCBSP_REG_SPCR2) &
				       (~XRST),
				       base + OMAP_MCBSP_REG_SPCR2);
				udelay(10);
				writew(readw(base + OMAP_MCBSP_REG_SPCR2) |
				       (XRST),
				       base + OMAP_MCBSP_REG_SPCR2);
				udelay(10);
				printk(KERN_ERR
				       " Could not write to McBSP Register\n");
				return -2;
			}
		}
	}
	return 0;
}

int omap_mcbsp_pollread(unsigned int id, u16 * buf)
{
	u32 base = mcbsp[id].io_base;
	/* if frame sync error - clear the error */
	if (readw(base + OMAP_MCBSP_REG_SPCR1) & RSYNC_ERR) {
		/* clear error */
		writew(readw(base + OMAP_MCBSP_REG_SPCR1) & (~RSYNC_ERR),
		       base + OMAP_MCBSP_REG_SPCR1);
		/* resend */
		return -1;
	} else {
		/* wait for recieve confirmation */
		int attemps = 0;
		while (!(readw(base + OMAP_MCBSP_REG_SPCR1) & RRDY)) {
			if (attemps++ > 1000) {
				writew(readw(base + OMAP_MCBSP_REG_SPCR1) &
				       (~RRST),
				       base + OMAP_MCBSP_REG_SPCR1);
				udelay(10);
				writew(readw(base + OMAP_MCBSP_REG_SPCR1) |
				       (RRST),
				       base + OMAP_MCBSP_REG_SPCR1);
				udelay(10);
				printk(KERN_ERR
				       " Could not read from McBSP Register\n");
				return -2;
			}
		}
	}
	*buf = readw(base + OMAP_MCBSP_REG_DRR1);
	return 0;
}

/*
 * IRQ based word transmission.
 */
void omap_mcbsp_xmit_word(unsigned int id, u32 word)
{
	u32 io_base;
	omap_mcbsp_word_length word_length = mcbsp[id].tx_word_length;

	if (omap_mcbsp_check(id) < 0)
		return;

	io_base = mcbsp[id].io_base;

	wait_for_completion(&(mcbsp[id].tx_irq_completion));

	if (word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, word & 0xffff);
}

u32 omap_mcbsp_recv_word(unsigned int id)
{
	u32 io_base;
	u16 word_lsb, word_msb = 0;
	omap_mcbsp_word_length word_length = mcbsp[id].rx_word_length;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	io_base = mcbsp[id].io_base;

	wait_for_completion(&(mcbsp[id].rx_irq_completion));

	if (word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	return (word_lsb | (word_msb << 16));
}


int omap_mcbsp_spi_master_xmit_word_poll(unsigned int id, u32 word)
{
	u32 io_base = mcbsp[id].io_base;
	omap_mcbsp_word_length tx_word_length = mcbsp[id].tx_word_length;
	omap_mcbsp_word_length rx_word_length = mcbsp[id].rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 & (~XRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 | XRST);
			udelay(10);
			printk("McBSP transmitter not ready\n");
			return -EAGAIN;
		}
	}

	/* Now we can push the data */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 & (~RRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 | RRST);
			udelay(10);
			printk("McBSP receiver not ready\n");
			return -EAGAIN;
		}
	}

	/* Receiver is ready, let's read the dummy data */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	return 0;
}

int omap_mcbsp_spi_master_recv_word_poll(unsigned int id, u32 * word)
{
	u32 io_base = mcbsp[id].io_base, clock_word = 0;
	omap_mcbsp_word_length tx_word_length = mcbsp[id].tx_word_length;
	omap_mcbsp_word_length rx_word_length = mcbsp[id].rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = OMAP_MCBSP_READ(io_base, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 & (~XRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR2, spcr2 | XRST);
			udelay(10);
			printk("McBSP transmitter not ready\n");
			return -EAGAIN;
		}
	}

	/* We first need to enable the bus clock */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		OMAP_MCBSP_WRITE(io_base, DXR2, clock_word >> 16);
	OMAP_MCBSP_WRITE(io_base, DXR1, clock_word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = OMAP_MCBSP_READ(io_base, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 & (~RRST));
			udelay(10);
			OMAP_MCBSP_WRITE(io_base, SPCR1, spcr1 | RRST);
			udelay(10);
			printk("McBSP receiver not ready\n");
			return -EAGAIN;
		}
	}

	/* Receiver is ready, there is something for us */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = OMAP_MCBSP_READ(io_base, DRR2);
	word_lsb = OMAP_MCBSP_READ(io_base, DRR1);

	word[0] = (word_lsb | (word_msb << 16));

	return 0;
}


/*
 * Simple DMA based buffer rx/tx routines.
 * Nothing fancy, just a single buffer tx/rx through DMA.
 * The DMA resources are released once the transfer is done.
 * For anything fancier, you should use your own customized DMA
 * routines and callbacks.
 */
int omap_mcbsp_xmit_buffer(unsigned int id, dma_addr_t buffer, unsigned int length)
{
	int dma_tx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	if (omap_request_dma(mcbsp[id].dma_tx_sync, "McBSP TX", omap_mcbsp_tx_dma_callback,
			     &mcbsp[id],
			     &dma_tx_ch)) {
		printk("OMAP-McBSP: Unable to request DMA channel for McBSP%d TX. Trying IRQ based TX\n", id+1);
		return -EAGAIN;
	}
	mcbsp[id].dma_tx_lch = dma_tx_ch;

	DBG("TX DMA on channel %d\n", dma_tx_ch);

	init_completion(&(mcbsp[id].tx_dma_completion));

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_is_omap24xx())
		sync_dev = mcbsp[id].dma_tx_sync;

	omap_set_dma_transfer_params(mcbsp[id].dma_tx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT,
	 sync_dev, 0);

	omap_set_dma_dest_params(mcbsp[id].dma_tx_lch,
				 src_port,
				 OMAP_DMA_AMODE_CONSTANT,
				 mcbsp[id].io_base + OMAP_MCBSP_REG_DXR1,
				 0, 0);

	omap_set_dma_src_params(mcbsp[id].dma_tx_lch,
				dest_port,
				OMAP_DMA_AMODE_POST_INC,
				buffer,
				0, 0);

	omap_start_dma(mcbsp[id].dma_tx_lch);
	wait_for_completion(&(mcbsp[id].tx_dma_completion));
	return 0;
}


int omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer, unsigned int length)
{
	int dma_rx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (omap_mcbsp_check(id) < 0)
		return -EINVAL;

	if (omap_request_dma(mcbsp[id].dma_rx_sync, "McBSP RX", omap_mcbsp_rx_dma_callback,
			     &mcbsp[id],
			     &dma_rx_ch)) {
		printk("Unable to request DMA channel for McBSP%d RX. Trying IRQ based RX\n", id+1);
		return -EAGAIN;
	}
	mcbsp[id].dma_rx_lch = dma_rx_ch;

	DBG("RX DMA on channel %d\n", dma_rx_ch);

	init_completion(&(mcbsp[id].rx_dma_completion));

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_is_omap24xx())
		sync_dev = mcbsp[id].dma_rx_sync;

	omap_set_dma_transfer_params(mcbsp[id].dma_rx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT,
	 sync_dev, 0);

	omap_set_dma_src_params(mcbsp[id].dma_rx_lch,
				src_port,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp[id].io_base + OMAP_MCBSP_REG_DRR1,
				0, 0);

	omap_set_dma_dest_params(mcbsp[id].dma_rx_lch,
				 dest_port,
				 OMAP_DMA_AMODE_POST_INC,
				 buffer,
				 0, 0);

	omap_start_dma(mcbsp[id].dma_rx_lch);
	wait_for_completion(&(mcbsp[id].rx_dma_completion));
	return 0;
}


/*
 * SPI wrapper.
 * Since SPI setup is much simpler than the generic McBSP one,
 * this wrapper just need an omap_mcbsp_spi_cfg structure as an input.
 * Once this is done, you can call omap_mcbsp_start().
 */
void omap_mcbsp_set_spi_mode(unsigned int id, const struct omap_mcbsp_spi_cfg * spi_cfg)
{
	struct omap_mcbsp_reg_cfg mcbsp_cfg;

	if (omap_mcbsp_check(id) < 0)
		return;

	memset(&mcbsp_cfg, 0, sizeof(struct omap_mcbsp_reg_cfg));

	/* SPI has only one frame */
	mcbsp_cfg.rcr1 |= (RWDLEN1(spi_cfg->word_length) | RFRLEN1(0));
	mcbsp_cfg.xcr1 |= (XWDLEN1(spi_cfg->word_length) | XFRLEN1(0));

        /* Clock stop mode */
	if (spi_cfg->clk_stp_mode == OMAP_MCBSP_CLK_STP_MODE_NO_DELAY)
		mcbsp_cfg.spcr1 |= (1 << 12);
	else
		mcbsp_cfg.spcr1 |= (3 << 11);

	/* Set clock parities */
	if (spi_cfg->rx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 |= CLKRP;
	else
		mcbsp_cfg.pcr0 &= ~CLKRP;

	if (spi_cfg->tx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 &= ~CLKXP;
	else
		mcbsp_cfg.pcr0 |= CLKXP;

	/* Set SCLKME to 0 and CLKSM to 1 */
	mcbsp_cfg.pcr0 &= ~SCLKME;
	mcbsp_cfg.srgr2 |= CLKSM;

	/* Set FSXP */
	if (spi_cfg->fsx_polarity == OMAP_MCBSP_FS_ACTIVE_HIGH)
		mcbsp_cfg.pcr0 &= ~FSXP;
	else
		mcbsp_cfg.pcr0 |= FSXP;

	if (spi_cfg->spi_mode == OMAP_MCBSP_SPI_MASTER) {
		mcbsp_cfg.pcr0 |= CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(spi_cfg->clk_div -1);
		mcbsp_cfg.pcr0 |= FSXM;
		mcbsp_cfg.srgr2 &= ~FSGM;
		mcbsp_cfg.xcr2 |= XDATDLY(1);
		mcbsp_cfg.rcr2 |= RDATDLY(1);
	}
	else {
		mcbsp_cfg.pcr0 &= ~CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(1);
		mcbsp_cfg.pcr0 &= ~FSXM;
		mcbsp_cfg.xcr2 &= ~XDATDLY(3);
		mcbsp_cfg.rcr2 &= ~RDATDLY(3);
	}

	mcbsp_cfg.xcr2 &= ~XPHASE;
	mcbsp_cfg.rcr2 &= ~RPHASE;

	omap_mcbsp_config(id, &mcbsp_cfg);
}


/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
struct omap_mcbsp_info {
	u32 virt_base;
	u8 dma_rx_sync, dma_tx_sync;
	u16 rx_irq, tx_irq;
};

#ifdef CONFIG_ARCH_OMAP730
static const struct omap_mcbsp_info mcbsp_730[] = {
	[0] = { .virt_base = io_p2v(OMAP730_MCBSP1_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_730_McBSP1RX,
		.tx_irq = INT_730_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP730_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_730_McBSP2RX,
		.tx_irq = INT_730_McBSP2TX },
};
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static const struct omap_mcbsp_info mcbsp_1510[] = {
	[0] = { .virt_base = OMAP1510_MCBSP1_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_McBSP1RX,
		.tx_irq = INT_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP1510_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP2_TX,
		.rx_irq = INT_1510_SPI_RX,
		.tx_irq = INT_1510_SPI_TX },
	[2] = { .virt_base = OMAP1510_MCBSP3_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_McBSP3RX,
		.tx_irq = INT_McBSP3TX },
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
static const struct omap_mcbsp_info mcbsp_1610[] = {
	[0] = { .virt_base = OMAP1610_MCBSP1_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP1_TX,
		.rx_irq = INT_McBSP1RX,
		.tx_irq = INT_McBSP1TX },
	[1] = { .virt_base = io_p2v(OMAP1610_MCBSP2_BASE),
		.dma_rx_sync = OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP2_TX,
		.rx_irq = INT_1610_McBSP2_RX,
		.tx_irq = INT_1610_McBSP2_TX },
	[2] = { .virt_base = OMAP1610_MCBSP3_BASE,
		.dma_rx_sync = OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync = OMAP_DMA_MCBSP3_TX,
		.rx_irq = INT_McBSP3RX,
		.tx_irq = INT_McBSP3TX },
};
#endif

#if defined(CONFIG_ARCH_OMAP24XX)
static const struct omap_mcbsp_info mcbsp_24xx[] = {
	[0] = { .virt_base = IO_ADDRESS(OMAP24XX_MCBSP1_BASE),
		.dma_rx_sync = OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync = OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq = INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq = INT_24XX_MCBSP1_IRQ_TX,
		},
	[1] = { .virt_base = IO_ADDRESS(OMAP24XX_MCBSP2_BASE),
		.dma_rx_sync = OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync = OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq = INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq = INT_24XX_MCBSP2_IRQ_TX,
		},
};
#endif

static int __init omap_mcbsp_init(void)
{
	int mcbsp_count = 0, i;
	static const struct omap_mcbsp_info *mcbsp_info;

	printk("Initializing OMAP McBSP system\n");

#ifdef CONFIG_ARCH_OMAP1
	mcbsp_dsp_ck = clk_get(0, "dsp_ck");
	if (IS_ERR(mcbsp_dsp_ck)) {
		printk(KERN_ERR "mcbsp: could not acquire dsp_ck handle.\n");
		return PTR_ERR(mcbsp_dsp_ck);
	}
	mcbsp_api_ck = clk_get(0, "api_ck");
	if (IS_ERR(mcbsp_api_ck)) {
		printk(KERN_ERR "mcbsp: could not acquire api_ck handle.\n");
		return PTR_ERR(mcbsp_api_ck);
	}
	mcbsp_dspxor_ck = clk_get(0, "dspxor_ck");
	if (IS_ERR(mcbsp_dspxor_ck)) {
		printk(KERN_ERR "mcbsp: could not acquire dspxor_ck handle.\n");
		return PTR_ERR(mcbsp_dspxor_ck);
	}
#endif
#ifdef CONFIG_ARCH_OMAP2
	mcbsp1_ick = clk_get(0, "mcbsp1_ick");
	if (IS_ERR(mcbsp1_ick)) {
		printk(KERN_ERR "mcbsp: could not acquire mcbsp1_ick handle.\n");
		return PTR_ERR(mcbsp1_ick);
	}
	mcbsp1_fck = clk_get(0, "mcbsp1_fck");
	if (IS_ERR(mcbsp1_fck)) {
		printk(KERN_ERR "mcbsp: could not acquire mcbsp1_fck handle.\n");
		return PTR_ERR(mcbsp1_fck);
	}
	mcbsp2_ick = clk_get(0, "mcbsp2_ick");
	if (IS_ERR(mcbsp2_ick)) {
		printk(KERN_ERR "mcbsp: could not acquire mcbsp2_ick handle.\n");
		return PTR_ERR(mcbsp2_ick);
	}
	mcbsp2_fck = clk_get(0, "mcbsp2_fck");
	if (IS_ERR(mcbsp2_fck)) {
		printk(KERN_ERR "mcbsp: could not acquire mcbsp2_fck handle.\n");
		return PTR_ERR(mcbsp2_fck);
	}
#endif

#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		mcbsp_info = mcbsp_730;
		mcbsp_count = ARRAY_SIZE(mcbsp_730);
	}
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap15xx()) {
		mcbsp_info = mcbsp_1510;
		mcbsp_count = ARRAY_SIZE(mcbsp_1510);
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap16xx()) {
		mcbsp_info = mcbsp_1610;
		mcbsp_count = ARRAY_SIZE(mcbsp_1610);
	}
#endif
#if defined(CONFIG_ARCH_OMAP24XX)
	if (cpu_is_omap24xx()) {
		mcbsp_info = mcbsp_24xx;
		mcbsp_count = ARRAY_SIZE(mcbsp_24xx);
		omap2_mcbsp2_mux_setup();
	}
#endif
	for (i = 0; i < OMAP_MAX_MCBSP_COUNT ; i++) {
		if (i >= mcbsp_count) {
			mcbsp[i].io_base = 0;
			mcbsp[i].free = 0;
                        continue;
		}
		mcbsp[i].id = i + 1;
		mcbsp[i].free = 1;
		mcbsp[i].dma_tx_lch = -1;
		mcbsp[i].dma_rx_lch = -1;

		mcbsp[i].io_base = mcbsp_info[i].virt_base;
		mcbsp[i].io_type = OMAP_MCBSP_IRQ_IO; /* Default I/O is IRQ based */
		mcbsp[i].tx_irq = mcbsp_info[i].tx_irq;
		mcbsp[i].rx_irq = mcbsp_info[i].rx_irq;
		mcbsp[i].dma_rx_sync = mcbsp_info[i].dma_rx_sync;
		mcbsp[i].dma_tx_sync = mcbsp_info[i].dma_tx_sync;
		spin_lock_init(&mcbsp[i].lock);
	}

	return 0;
}

arch_initcall(omap_mcbsp_init);

EXPORT_SYMBOL(omap_mcbsp_config);
EXPORT_SYMBOL(omap_mcbsp_request);
EXPORT_SYMBOL(omap_mcbsp_set_io_type);
EXPORT_SYMBOL(omap_mcbsp_free);
EXPORT_SYMBOL(omap_mcbsp_start);
EXPORT_SYMBOL(omap_mcbsp_stop);
EXPORT_SYMBOL(omap_mcbsp_xmit_word);
EXPORT_SYMBOL(omap_mcbsp_recv_word);
EXPORT_SYMBOL(omap_mcbsp_xmit_buffer);
EXPORT_SYMBOL(omap_mcbsp_recv_buffer);
EXPORT_SYMBOL(omap_mcbsp_spi_master_xmit_word_poll);
EXPORT_SYMBOL(omap_mcbsp_spi_master_recv_word_poll);
EXPORT_SYMBOL(omap_mcbsp_set_spi_mode);
