/*
 * BRIEF MODULE DESCRIPTION
 *	Defines for using and allocating DMA channels on the Alchemy
 *      Au1x00 MIPS processors.
 *
 * Copyright 2000, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __ASM_AU1000_DMA_H
#define __ASM_AU1000_DMA_H

#include <linux/io.h>		/* need byte IO */
#include <linux/spinlock.h>	/* And spinlocks */
#include <linux/delay.h>

#define NUM_AU1000_DMA_CHANNELS 8

/* DMA Channel Register Offsets */
#define DMA_MODE_SET		0x00000000
#define DMA_MODE_READ		DMA_MODE_SET
#define DMA_MODE_CLEAR		0x00000004
/* DMA Mode register bits follow */
#define DMA_DAH_MASK		(0x0f << 20)
#define DMA_DID_BIT		16
#define DMA_DID_MASK		(0x0f << DMA_DID_BIT)
#define DMA_DS			(1 << 15)
#define DMA_BE			(1 << 13)
#define DMA_DR			(1 << 12)
#define DMA_TS8			(1 << 11)
#define DMA_DW_BIT		9
#define DMA_DW_MASK		(0x03 << DMA_DW_BIT)
#define DMA_DW8			(0 << DMA_DW_BIT)
#define DMA_DW16		(1 << DMA_DW_BIT)
#define DMA_DW32		(2 << DMA_DW_BIT)
#define DMA_NC			(1 << 8)
#define DMA_IE			(1 << 7)
#define DMA_HALT		(1 << 6)
#define DMA_GO			(1 << 5)
#define DMA_AB			(1 << 4)
#define DMA_D1			(1 << 3)
#define DMA_BE1			(1 << 2)
#define DMA_D0			(1 << 1)
#define DMA_BE0			(1 << 0)

#define DMA_PERIPHERAL_ADDR	0x00000008
#define DMA_BUFFER0_START	0x0000000C
#define DMA_BUFFER1_START	0x00000014
#define DMA_BUFFER0_COUNT	0x00000010
#define DMA_BUFFER1_COUNT	0x00000018
#define DMA_BAH_BIT	16
#define DMA_BAH_MASK	(0x0f << DMA_BAH_BIT)
#define DMA_COUNT_BIT	0
#define DMA_COUNT_MASK	(0xffff << DMA_COUNT_BIT)

/* DMA Device IDs follow */
enum {
	DMA_ID_UART0_TX = 0,
	DMA_ID_UART0_RX,
	DMA_ID_GP04,
	DMA_ID_GP05,
	DMA_ID_AC97C_TX,
	DMA_ID_AC97C_RX,
	DMA_ID_UART3_TX,
	DMA_ID_UART3_RX,
	DMA_ID_USBDEV_EP0_RX,
	DMA_ID_USBDEV_EP0_TX,
	DMA_ID_USBDEV_EP2_TX,
	DMA_ID_USBDEV_EP3_TX,
	DMA_ID_USBDEV_EP4_RX,
	DMA_ID_USBDEV_EP5_RX,
	DMA_ID_I2S_TX,
	DMA_ID_I2S_RX,
	DMA_NUM_DEV
};

/* DMA Device ID's for 2nd bank (AU1100) follow */
enum {
	DMA_ID_SD0_TX = 0,
	DMA_ID_SD0_RX,
	DMA_ID_SD1_TX,
	DMA_ID_SD1_RX,
	DMA_NUM_DEV_BANK2
};

struct dma_chan {
	int dev_id;		/* this channel is allocated if >= 0, */
				/* free otherwise */
	void __iomem *io;
	const char *dev_str;
	int irq;
	void *irq_dev;
	unsigned int fifo_addr;
	unsigned int mode;
};

/* These are in arch/mips/au1000/common/dma.c */
extern struct dma_chan au1000_dma_table[];
extern int request_au1000_dma(int dev_id,
			      const char *dev_str,
			      irq_handler_t irqhandler,
			      unsigned long irqflags,
			      void *irq_dev_id);
extern void free_au1000_dma(unsigned int dmanr);
extern int au1000_dma_read_proc(char *buf, char **start, off_t fpos,
				int length, int *eof, void *data);
extern void dump_au1000_dma_channel(unsigned int dmanr);
extern spinlock_t au1000_dma_spin_lock;

static inline struct dma_chan *get_dma_chan(unsigned int dmanr)
{
	if (dmanr >= NUM_AU1000_DMA_CHANNELS ||
	    au1000_dma_table[dmanr].dev_id < 0)
		return NULL;
	return &au1000_dma_table[dmanr];
}

static inline unsigned long claim_dma_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&au1000_dma_spin_lock, flags);
	return flags;
}

static inline void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&au1000_dma_spin_lock, flags);
}

/*
 * Set the DMA buffer enable bits in the mode register.
 */
static inline void enable_dma_buffer0(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_BE0, chan->io + DMA_MODE_SET);
}

static inline void enable_dma_buffer1(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_BE1, chan->io + DMA_MODE_SET);
}
static inline void enable_dma_buffers(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_BE0 | DMA_BE1, chan->io + DMA_MODE_SET);
}

static inline void start_dma(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_GO, chan->io + DMA_MODE_SET);
}

#define DMA_HALT_POLL 0x5000

static inline void halt_dma(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);
	int i;

	if (!chan)
		return;
	__raw_writel(DMA_GO, chan->io + DMA_MODE_CLEAR);

	/* Poll the halt bit */
	for (i = 0; i < DMA_HALT_POLL; i++)
		if (__raw_readl(chan->io + DMA_MODE_READ) & DMA_HALT)
			break;
	if (i == DMA_HALT_POLL)
		printk(KERN_INFO "halt_dma: HALT poll expired!\n");
}

static inline void disable_dma(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;

	halt_dma(dmanr);

	/* Now we can disable the buffers */
	__raw_writel(~DMA_GO, chan->io + DMA_MODE_CLEAR);
}

static inline int dma_halted(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return 1;
	return (__raw_readl(chan->io + DMA_MODE_READ) & DMA_HALT) ? 1 : 0;
}

/* Initialize a DMA channel. */
static inline void init_dma(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);
	u32 mode;

	if (!chan)
		return;

	disable_dma(dmanr);

	/* Set device FIFO address */
	__raw_writel(CPHYSADDR(chan->fifo_addr), chan->io + DMA_PERIPHERAL_ADDR);

	mode = chan->mode | (chan->dev_id << DMA_DID_BIT);
	if (chan->irq)
		mode |= DMA_IE;

	__raw_writel(~mode, chan->io + DMA_MODE_CLEAR);
	__raw_writel(mode,	 chan->io + DMA_MODE_SET);
}

/*
 * Set mode for a specific DMA channel
 */
static inline void set_dma_mode(unsigned int dmanr, unsigned int mode)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	/*
	 * set_dma_mode is only allowed to change endianness, direction,
	 * transfer size, device FIFO width, and coherency settings.
	 * Make sure anything else is masked off.
	 */
	mode &= (DMA_BE | DMA_DR | DMA_TS8 | DMA_DW_MASK | DMA_NC);
	chan->mode &= ~(DMA_BE | DMA_DR | DMA_TS8 | DMA_DW_MASK | DMA_NC);
	chan->mode |= mode;
}

static inline unsigned int get_dma_mode(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return 0;
	return chan->mode;
}

static inline int get_dma_active_buffer(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return -1;
	return (__raw_readl(chan->io + DMA_MODE_READ) & DMA_AB) ? 1 : 0;
}

/*
 * Set the device FIFO address for a specific DMA channel - only
 * applicable to GPO4 and GPO5. All the other devices have fixed
 * FIFO addresses.
 */
static inline void set_dma_fifo_addr(unsigned int dmanr, unsigned int a)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;

	if (chan->mode & DMA_DS)	/* second bank of device IDs */
		return;

	if (chan->dev_id != DMA_ID_GP04 && chan->dev_id != DMA_ID_GP05)
		return;

	__raw_writel(CPHYSADDR(a), chan->io + DMA_PERIPHERAL_ADDR);
}

/*
 * Clear the DMA buffer done bits in the mode register.
 */
static inline void clear_dma_done0(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_D0, chan->io + DMA_MODE_CLEAR);
}

static inline void clear_dma_done1(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(DMA_D1, chan->io + DMA_MODE_CLEAR);
}

/*
 * This does nothing - not applicable to Au1000 DMA.
 */
static inline void set_dma_page(unsigned int dmanr, char pagenr)
{
}

/*
 * Set Buffer 0 transfer address for specific DMA channel.
 */
static inline void set_dma_addr0(unsigned int dmanr, unsigned int a)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(a, chan->io + DMA_BUFFER0_START);
}

/*
 * Set Buffer 1 transfer address for specific DMA channel.
 */
static inline void set_dma_addr1(unsigned int dmanr, unsigned int a)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	__raw_writel(a, chan->io + DMA_BUFFER1_START);
}


/*
 * Set Buffer 0 transfer size (max 64k) for a specific DMA channel.
 */
static inline void set_dma_count0(unsigned int dmanr, unsigned int count)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	count &= DMA_COUNT_MASK;
	__raw_writel(count, chan->io + DMA_BUFFER0_COUNT);
}

/*
 * Set Buffer 1 transfer size (max 64k) for a specific DMA channel.
 */
static inline void set_dma_count1(unsigned int dmanr, unsigned int count)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	count &= DMA_COUNT_MASK;
	__raw_writel(count, chan->io + DMA_BUFFER1_COUNT);
}

/*
 * Set both buffer transfer sizes (max 64k) for a specific DMA channel.
 */
static inline void set_dma_count(unsigned int dmanr, unsigned int count)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return;
	count &= DMA_COUNT_MASK;
	__raw_writel(count, chan->io + DMA_BUFFER0_COUNT);
	__raw_writel(count, chan->io + DMA_BUFFER1_COUNT);
}

/*
 * Returns which buffer has its done bit set in the mode register.
 * Returns -1 if neither or both done bits set.
 */
static inline unsigned int get_dma_buffer_done(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return 0;
	return __raw_readl(chan->io + DMA_MODE_READ) & (DMA_D0 | DMA_D1);
}


/*
 * Returns the DMA channel's Buffer Done IRQ number.
 */
static inline int get_dma_done_irq(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return -1;
	return chan->irq;
}

/*
 * Get DMA residue count. Returns the number of _bytes_ left to transfer.
 */
static inline int get_dma_residue(unsigned int dmanr)
{
	int curBufCntReg, count;
	struct dma_chan *chan = get_dma_chan(dmanr);

	if (!chan)
		return 0;

	curBufCntReg = (__raw_readl(chan->io + DMA_MODE_READ) & DMA_AB) ?
	    DMA_BUFFER1_COUNT : DMA_BUFFER0_COUNT;

	count = __raw_readl(chan->io + curBufCntReg) & DMA_COUNT_MASK;

	if ((chan->mode & DMA_DW_MASK) == DMA_DW16)
		count <<= 1;
	else if ((chan->mode & DMA_DW_MASK) == DMA_DW32)
		count <<= 2;

	return count;
}

#endif /* __ASM_AU1000_DMA_H */
