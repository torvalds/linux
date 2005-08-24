/*
 * arch/ppc/kernel/ppc4xx_dma.c
 *
 * IBM PPC4xx DMA engine core library
 *
 * Copyright 2000-2004 MontaVista Software Inc.
 *
 * Cleaned up and converted to new DCR access
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Original code by Armin Kuster <akuster@mvista.com>
 * and Pete Popov <ppopov@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/ppc4xx_dma.h>

ppc_dma_ch_t dma_channels[MAX_PPC4xx_DMA_CHANNELS];

int
ppc4xx_get_dma_status(void)
{
	return (mfdcr(DCRN_DMASR));
}

void
ppc4xx_set_src_addr(int dmanr, phys_addr_t src_addr)
{
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("set_src_addr: bad channel: %d\n", dmanr);
		return;
	}

#ifdef PPC4xx_DMA_64BIT
	mtdcr(DCRN_DMASAH0 + dmanr*2, (u32)(src_addr >> 32));
#else
	mtdcr(DCRN_DMASA0 + dmanr*2, (u32)src_addr);
#endif
}

void
ppc4xx_set_dst_addr(int dmanr, phys_addr_t dst_addr)
{
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("set_dst_addr: bad channel: %d\n", dmanr);
		return;
	}

#ifdef PPC4xx_DMA_64BIT
	mtdcr(DCRN_DMADAH0 + dmanr*2, (u32)(dst_addr >> 32));
#else
	mtdcr(DCRN_DMADA0 + dmanr*2, (u32)dst_addr);
#endif
}

void
ppc4xx_enable_dma(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
	unsigned int status_bits[] = { DMA_CS0 | DMA_TS0 | DMA_CH0_ERR,
				       DMA_CS1 | DMA_TS1 | DMA_CH1_ERR,
				       DMA_CS2 | DMA_TS2 | DMA_CH2_ERR,
				       DMA_CS3 | DMA_TS3 | DMA_CH3_ERR};

	if (p_dma_ch->in_use) {
		printk("enable_dma: channel %d in use\n", dmanr);
		return;
	}

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("enable_dma: bad channel: %d\n", dmanr);
		return;
	}

	if (p_dma_ch->mode == DMA_MODE_READ) {
		/* peripheral to memory */
		ppc4xx_set_src_addr(dmanr, 0);
		ppc4xx_set_dst_addr(dmanr, p_dma_ch->addr);
	} else if (p_dma_ch->mode == DMA_MODE_WRITE) {
		/* memory to peripheral */
		ppc4xx_set_src_addr(dmanr, p_dma_ch->addr);
		ppc4xx_set_dst_addr(dmanr, 0);
	}

	/* for other xfer modes, the addresses are already set */
	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));

	control &= ~(DMA_TM_MASK | DMA_TD);	/* clear all mode bits */
	if (p_dma_ch->mode == DMA_MODE_MM) {
		/* software initiated memory to memory */
		control |= DMA_ETD_OUTPUT | DMA_TCE_ENABLE;
	}

	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	/*
	 * Clear the CS, TS, RI bits for the channel from DMASR.  This
	 * has been observed to happen correctly only after the mode and
	 * ETD/DCE bits in DMACRx are set above.  Must do this before
	 * enabling the channel.
	 */

	mtdcr(DCRN_DMASR, status_bits[dmanr]);

	/*
	 * For device-paced transfers, Terminal Count Enable apparently
	 * must be on, and this must be turned on after the mode, etc.
	 * bits are cleared above (at least on Redwood-6).
	 */

	if ((p_dma_ch->mode == DMA_MODE_MM_DEVATDST) ||
	    (p_dma_ch->mode == DMA_MODE_MM_DEVATSRC))
		control |= DMA_TCE_ENABLE;

	/*
	 * Now enable the channel.
	 */

	control |= (p_dma_ch->mode | DMA_CE_ENABLE);

	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	p_dma_ch->in_use = 1;
}

void
ppc4xx_disable_dma(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (!p_dma_ch->in_use) {
		printk("disable_dma: channel %d not in use\n", dmanr);
		return;
	}

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("disable_dma: bad channel: %d\n", dmanr);
		return;
	}

	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));
	control &= ~DMA_CE_ENABLE;
	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	p_dma_ch->in_use = 0;
}

/*
 * Sets the dma mode for single DMA transfers only.
 * For scatter/gather transfers, the mode is passed to the
 * alloc_dma_handle() function as one of the parameters.
 *
 * The mode is simply saved and used later.  This allows
 * the driver to call set_dma_mode() and set_dma_addr() in
 * any order.
 *
 * Valid mode values are:
 *
 * DMA_MODE_READ          peripheral to memory
 * DMA_MODE_WRITE         memory to peripheral
 * DMA_MODE_MM            memory to memory
 * DMA_MODE_MM_DEVATSRC   device-paced memory to memory, device at src
 * DMA_MODE_MM_DEVATDST   device-paced memory to memory, device at dst
 */
int
ppc4xx_set_dma_mode(unsigned int dmanr, unsigned int mode)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("set_dma_mode: bad channel 0x%x\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	p_dma_ch->mode = mode;

	return DMA_STATUS_GOOD;
}

/*
 * Sets the DMA Count register. Note that 'count' is in bytes.
 * However, the DMA Count register counts the number of "transfers",
 * where each transfer is equal to the bus width.  Thus, count
 * MUST be a multiple of the bus width.
 */
void
ppc4xx_set_dma_count(unsigned int dmanr, unsigned int count)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

#ifdef DEBUG_4xxDMA
	{
		int error = 0;
		switch (p_dma_ch->pwidth) {
		case PW_8:
			break;
		case PW_16:
			if (count & 0x1)
				error = 1;
			break;
		case PW_32:
			if (count & 0x3)
				error = 1;
			break;
		case PW_64:
			if (count & 0x7)
				error = 1;
			break;
		default:
			printk("set_dma_count: invalid bus width: 0x%x\n",
			       p_dma_ch->pwidth);
			return;
		}
		if (error)
			printk
			    ("Warning: set_dma_count count 0x%x bus width %d\n",
			     count, p_dma_ch->pwidth);
	}
#endif

	count = count >> p_dma_ch->shift;

	mtdcr(DCRN_DMACT0 + (dmanr * 0x8), count);
}

/*
 *   Returns the number of bytes left to be transfered.
 *   After a DMA transfer, this should return zero.
 *   Reading this while a DMA transfer is still in progress will return
 *   unpredictable results.
 */
int
ppc4xx_get_dma_residue(unsigned int dmanr)
{
	unsigned int count;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_get_dma_residue: bad channel 0x%x\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	count = mfdcr(DCRN_DMACT0 + (dmanr * 0x8));

	return (count << p_dma_ch->shift);
}

/*
 * Sets the DMA address for a memory to peripheral or peripheral
 * to memory transfer.  The address is just saved in the channel
 * structure for now and used later in enable_dma().
 */
void
ppc4xx_set_dma_addr(unsigned int dmanr, phys_addr_t addr)
{
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_set_dma_addr: bad channel: %d\n", dmanr);
		return;
	}

#ifdef DEBUG_4xxDMA
	{
		int error = 0;
		switch (p_dma_ch->pwidth) {
		case PW_8:
			break;
		case PW_16:
			if ((unsigned) addr & 0x1)
				error = 1;
			break;
		case PW_32:
			if ((unsigned) addr & 0x3)
				error = 1;
			break;
		case PW_64:
			if ((unsigned) addr & 0x7)
				error = 1;
			break;
		default:
			printk("ppc4xx_set_dma_addr: invalid bus width: 0x%x\n",
			       p_dma_ch->pwidth);
			return;
		}
		if (error)
			printk("Warning: ppc4xx_set_dma_addr addr 0x%x bus width %d\n",
			       addr, p_dma_ch->pwidth);
	}
#endif

	/* save dma address and program it later after we know the xfer mode */
	p_dma_ch->addr = addr;
}

/*
 * Sets both DMA addresses for a memory to memory transfer.
 * For memory to peripheral or peripheral to memory transfers
 * the function set_dma_addr() should be used instead.
 */
void
ppc4xx_set_dma_addr2(unsigned int dmanr, phys_addr_t src_dma_addr,
		     phys_addr_t dst_dma_addr)
{
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_set_dma_addr2: bad channel: %d\n", dmanr);
		return;
	}

#ifdef DEBUG_4xxDMA
	{
		ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
		int error = 0;
		switch (p_dma_ch->pwidth) {
			case PW_8:
				break;
			case PW_16:
				if (((unsigned) src_dma_addr & 0x1) ||
						((unsigned) dst_dma_addr & 0x1)
				   )
					error = 1;
				break;
			case PW_32:
				if (((unsigned) src_dma_addr & 0x3) ||
						((unsigned) dst_dma_addr & 0x3)
				   )
					error = 1;
				break;
			case PW_64:
				if (((unsigned) src_dma_addr & 0x7) ||
						((unsigned) dst_dma_addr & 0x7)
				   )
					error = 1;
				break;
			default:
				printk("ppc4xx_set_dma_addr2: invalid bus width: 0x%x\n",
						p_dma_ch->pwidth);
				return;
		}
		if (error)
			printk
				("Warning: ppc4xx_set_dma_addr2 src 0x%x dst 0x%x bus width %d\n",
				 src_dma_addr, dst_dma_addr, p_dma_ch->pwidth);
	}
#endif

	ppc4xx_set_src_addr(dmanr, src_dma_addr);
	ppc4xx_set_dst_addr(dmanr, dst_dma_addr);
}

/*
 * Enables the channel interrupt.
 *
 * If performing a scatter/gatter transfer, this function
 * MUST be called before calling alloc_dma_handle() and building
 * the sgl list.  Otherwise, interrupts will not be enabled, if
 * they were previously disabled.
 */
int
ppc4xx_enable_dma_interrupt(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_enable_dma_interrupt: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	p_dma_ch->int_enable = 1;

	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));
	control |= DMA_CIE_ENABLE;	/* Channel Interrupt Enable */
	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	return DMA_STATUS_GOOD;
}

/*
 * Disables the channel interrupt.
 *
 * If performing a scatter/gatter transfer, this function
 * MUST be called before calling alloc_dma_handle() and building
 * the sgl list.  Otherwise, interrupts will not be disabled, if
 * they were previously enabled.
 */
int
ppc4xx_disable_dma_interrupt(unsigned int dmanr)
{
	unsigned int control;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_disable_dma_interrupt: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	p_dma_ch->int_enable = 0;

	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));
	control &= ~DMA_CIE_ENABLE;	/* Channel Interrupt Enable */
	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	return DMA_STATUS_GOOD;
}

/*
 * Configures a DMA channel, including the peripheral bus width, if a
 * peripheral is attached to the channel, the polarity of the DMAReq and
 * DMAAck signals, etc.  This information should really be setup by the boot
 * code, since most likely the configuration won't change dynamically.
 * If the kernel has to call this function, it's recommended that it's
 * called from platform specific init code.  The driver should not need to
 * call this function.
 */
int
ppc4xx_init_dma_channel(unsigned int dmanr, ppc_dma_ch_t * p_init)
{
	unsigned int polarity;
	uint32_t control = 0;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

	DMA_MODE_READ = (unsigned long) DMA_TD;	/* Peripheral to Memory */
	DMA_MODE_WRITE = 0;	/* Memory to Peripheral */

	if (!p_init) {
		printk("ppc4xx_init_dma_channel: NULL p_init\n");
		return DMA_STATUS_NULL_POINTER;
	}

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_init_dma_channel: bad channel %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

#if DCRN_POL > 0
	polarity = mfdcr(DCRN_POL);
#else
	polarity = 0;
#endif

	/* Setup the control register based on the values passed to
	 * us in p_init.  Then, over-write the control register with this
	 * new value.
	 */
	control |= SET_DMA_CONTROL;

	/* clear all polarity signals and then "or" in new signal levels */
	polarity &= ~GET_DMA_POLARITY(dmanr);
	polarity |= p_init->polarity;
#if DCRN_POL > 0
	mtdcr(DCRN_POL, polarity);
#endif
	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	/* save these values in our dma channel structure */
	memcpy(p_dma_ch, p_init, sizeof (ppc_dma_ch_t));

	/*
	 * The peripheral width values written in the control register are:
	 *   PW_8                 0
	 *   PW_16                1
	 *   PW_32                2
	 *   PW_64                3
	 *
	 *   Since the DMA count register takes the number of "transfers",
	 *   we need to divide the count sent to us in certain
	 *   functions by the appropriate number.  It so happens that our
	 *   right shift value is equal to the peripheral width value.
	 */
	p_dma_ch->shift = p_init->pwidth;

	/*
	 * Save the control word for easy access.
	 */
	p_dma_ch->control = control;

	mtdcr(DCRN_DMASR, 0xffffffff);	/* clear status register */
	return DMA_STATUS_GOOD;
}

/*
 * This function returns the channel configuration.
 */
int
ppc4xx_get_channel_config(unsigned int dmanr, ppc_dma_ch_t * p_dma_ch)
{
	unsigned int polarity;
	unsigned int control;

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_get_channel_config: bad channel %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	memcpy(p_dma_ch, &dma_channels[dmanr], sizeof (ppc_dma_ch_t));

#if DCRN_POL > 0
	polarity = mfdcr(DCRN_POL);
#else
	polarity = 0;
#endif

	p_dma_ch->polarity = polarity & GET_DMA_POLARITY(dmanr);
	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));

	p_dma_ch->cp = GET_DMA_PRIORITY(control);
	p_dma_ch->pwidth = GET_DMA_PW(control);
	p_dma_ch->psc = GET_DMA_PSC(control);
	p_dma_ch->pwc = GET_DMA_PWC(control);
	p_dma_ch->phc = GET_DMA_PHC(control);
	p_dma_ch->ce = GET_DMA_CE_ENABLE(control);
	p_dma_ch->int_enable = GET_DMA_CIE_ENABLE(control);
	p_dma_ch->shift = GET_DMA_PW(control);

#ifdef CONFIG_PPC4xx_EDMA
	p_dma_ch->pf = GET_DMA_PREFETCH(control);
#else
	p_dma_ch->ch_enable = GET_DMA_CH(control);
	p_dma_ch->ece_enable = GET_DMA_ECE(control);
	p_dma_ch->tcd_disable = GET_DMA_TCD(control);
#endif
	return DMA_STATUS_GOOD;
}

/*
 * Sets the priority for the DMA channel dmanr.
 * Since this is setup by the hardware init function, this function
 * can be used to dynamically change the priority of a channel.
 *
 * Acceptable priorities:
 *
 * PRIORITY_LOW
 * PRIORITY_MID_LOW
 * PRIORITY_MID_HIGH
 * PRIORITY_HIGH
 *
 */
int
ppc4xx_set_channel_priority(unsigned int dmanr, unsigned int priority)
{
	unsigned int control;

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_set_channel_priority: bad channel %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	if ((priority != PRIORITY_LOW) &&
	    (priority != PRIORITY_MID_LOW) &&
	    (priority != PRIORITY_MID_HIGH) && (priority != PRIORITY_HIGH)) {
		printk("ppc4xx_set_channel_priority: bad priority: 0x%x\n", priority);
	}

	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));
	control |= SET_DMA_PRIORITY(priority);
	mtdcr(DCRN_DMACR0 + (dmanr * 0x8), control);

	return DMA_STATUS_GOOD;
}

/*
 * Returns the width of the peripheral attached to this channel. This assumes
 * that someone who knows the hardware configuration, boot code or some other
 * init code, already set the width.
 *
 * The return value is one of:
 *   PW_8
 *   PW_16
 *   PW_32
 *   PW_64
 *
 *   The function returns 0 on error.
 */
unsigned int
ppc4xx_get_peripheral_width(unsigned int dmanr)
{
	unsigned int control;

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_get_peripheral_width: bad channel %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	control = mfdcr(DCRN_DMACR0 + (dmanr * 0x8));

	return (GET_DMA_PW(control));
}

/*
 * Clears the channel status bits
 */
int
ppc4xx_clr_dma_status(unsigned int dmanr)
{
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk(KERN_ERR "ppc4xx_clr_dma_status: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
	mtdcr(DCRN_DMASR, ((u32)DMA_CH0_ERR | (u32)DMA_CS0 | (u32)DMA_TS0) >> dmanr);
	return DMA_STATUS_GOOD;
}

#ifdef CONFIG_PPC4xx_EDMA
/*
 * Enables the burst on the channel (BTEN bit in the control/count register)
 * Note:
 * For scatter/gather dma, this function MUST be called before the
 * ppc4xx_alloc_dma_handle() func as the chan count register is copied into the
 * sgl list and used as each sgl element is added.
 */
int
ppc4xx_enable_burst(unsigned int dmanr)
{
	unsigned int ctc;
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk(KERN_ERR "ppc4xx_enable_burst: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
        ctc = mfdcr(DCRN_DMACT0 + (dmanr * 0x8)) | DMA_CTC_BTEN;
	mtdcr(DCRN_DMACT0 + (dmanr * 0x8), ctc);
	return DMA_STATUS_GOOD;
}
/*
 * Disables the burst on the channel (BTEN bit in the control/count register)
 * Note:
 * For scatter/gather dma, this function MUST be called before the
 * ppc4xx_alloc_dma_handle() func as the chan count register is copied into the
 * sgl list and used as each sgl element is added.
 */
int
ppc4xx_disable_burst(unsigned int dmanr)
{
	unsigned int ctc;
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk(KERN_ERR "ppc4xx_disable_burst: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
	ctc = mfdcr(DCRN_DMACT0 + (dmanr * 0x8)) &~ DMA_CTC_BTEN;
	mtdcr(DCRN_DMACT0 + (dmanr * 0x8), ctc);
	return DMA_STATUS_GOOD;
}
/*
 * Sets the burst size (number of peripheral widths) for the channel
 * (BSIZ bits in the control/count register))
 * must be one of:
 *    DMA_CTC_BSIZ_2
 *    DMA_CTC_BSIZ_4
 *    DMA_CTC_BSIZ_8
 *    DMA_CTC_BSIZ_16
 * Note:
 * For scatter/gather dma, this function MUST be called before the
 * ppc4xx_alloc_dma_handle() func as the chan count register is copied into the
 * sgl list and used as each sgl element is added.
 */
int
ppc4xx_set_burst_size(unsigned int dmanr, unsigned int bsize)
{
	unsigned int ctc;
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk(KERN_ERR "ppc4xx_set_burst_size: bad channel: %d\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}
	ctc = mfdcr(DCRN_DMACT0 + (dmanr * 0x8)) &~ DMA_CTC_BSIZ_MSK;
	ctc |= (bsize & DMA_CTC_BSIZ_MSK);
	mtdcr(DCRN_DMACT0 + (dmanr * 0x8), ctc);
	return DMA_STATUS_GOOD;
}

EXPORT_SYMBOL(ppc4xx_enable_burst);
EXPORT_SYMBOL(ppc4xx_disable_burst);
EXPORT_SYMBOL(ppc4xx_set_burst_size);
#endif /* CONFIG_PPC4xx_EDMA */

EXPORT_SYMBOL(ppc4xx_init_dma_channel);
EXPORT_SYMBOL(ppc4xx_get_channel_config);
EXPORT_SYMBOL(ppc4xx_set_channel_priority);
EXPORT_SYMBOL(ppc4xx_get_peripheral_width);
EXPORT_SYMBOL(dma_channels);
EXPORT_SYMBOL(ppc4xx_set_src_addr);
EXPORT_SYMBOL(ppc4xx_set_dst_addr);
EXPORT_SYMBOL(ppc4xx_set_dma_addr);
EXPORT_SYMBOL(ppc4xx_set_dma_addr2);
EXPORT_SYMBOL(ppc4xx_enable_dma);
EXPORT_SYMBOL(ppc4xx_disable_dma);
EXPORT_SYMBOL(ppc4xx_set_dma_mode);
EXPORT_SYMBOL(ppc4xx_set_dma_count);
EXPORT_SYMBOL(ppc4xx_get_dma_residue);
EXPORT_SYMBOL(ppc4xx_enable_dma_interrupt);
EXPORT_SYMBOL(ppc4xx_disable_dma_interrupt);
EXPORT_SYMBOL(ppc4xx_get_dma_status);
EXPORT_SYMBOL(ppc4xx_clr_dma_status);

