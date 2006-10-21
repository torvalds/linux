/*
 *
 * BRIEF MODULE DESCRIPTION
 *      A DMA channel allocator for Au1000. API is modeled loosely off of
 *      linux/kernel/dma.c.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>

#if defined(CONFIG_SOC_AU1000) || defined(CONFIG_SOC_AU1500) || defined(CONFIG_SOC_AU1100)
/*
 * A note on resource allocation:
 *
 * All drivers needing DMA channels, should allocate and release them
 * through the public routines `request_dma()' and `free_dma()'.
 *
 * In order to avoid problems, all processes should allocate resources in
 * the same sequence and release them in the reverse order.
 *
 * So, when allocating DMAs and IRQs, first allocate the DMA, then the IRQ.
 * When releasing them, first release the IRQ, then release the DMA. The
 * main reason for this order is that, if you are requesting the DMA buffer
 * done interrupt, you won't know the irq number until the DMA channel is
 * returned from request_dma.
 */


DEFINE_SPINLOCK(au1000_dma_spin_lock);

struct dma_chan au1000_dma_table[NUM_AU1000_DMA_CHANNELS] = {
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,},
      {.dev_id = -1,}
};
EXPORT_SYMBOL(au1000_dma_table);

// Device FIFO addresses and default DMA modes
static const struct dma_dev {
	unsigned int fifo_addr;
	unsigned int dma_mode;
} dma_dev_table[DMA_NUM_DEV] = {
	{UART0_ADDR + UART_TX, 0},
	{UART0_ADDR + UART_RX, 0},
	{0, 0},
	{0, 0},
	{AC97C_DATA, DMA_DW16 },          // coherent
	{AC97C_DATA, DMA_DR | DMA_DW16 }, // coherent
	{UART3_ADDR + UART_TX, DMA_DW8 | DMA_NC},
	{UART3_ADDR + UART_RX, DMA_DR | DMA_DW8 | DMA_NC},
	{USBD_EP0RD, DMA_DR | DMA_DW8 | DMA_NC},
	{USBD_EP0WR, DMA_DW8 | DMA_NC},
	{USBD_EP2WR, DMA_DW8 | DMA_NC},
	{USBD_EP3WR, DMA_DW8 | DMA_NC},
	{USBD_EP4RD, DMA_DR | DMA_DW8 | DMA_NC},
	{USBD_EP5RD, DMA_DR | DMA_DW8 | DMA_NC},
	{I2S_DATA, DMA_DW32 | DMA_NC},
	{I2S_DATA, DMA_DR | DMA_DW32 | DMA_NC}
};

int au1000_dma_read_proc(char *buf, char **start, off_t fpos,
			 int length, int *eof, void *data)
{
	int i, len = 0;
	struct dma_chan *chan;

	for (i = 0; i < NUM_AU1000_DMA_CHANNELS; i++) {
		if ((chan = get_dma_chan(i)) != NULL) {
			len += sprintf(buf + len, "%2d: %s\n",
				       i, chan->dev_str);
		}
	}

	if (fpos >= len) {
		*start = buf;
		*eof = 1;
		return 0;
	}
	*start = buf + fpos;
	if ((len -= fpos) > length)
		return length;
	*eof = 1;
	return len;
}

// Device FIFO addresses and default DMA modes - 2nd bank
static const struct dma_dev dma_dev_table_bank2[DMA_NUM_DEV_BANK2] = {
	{SD0_XMIT_FIFO, DMA_DS | DMA_DW8},		// coherent
	{SD0_RECV_FIFO, DMA_DS | DMA_DR | DMA_DW8},	// coherent
	{SD1_XMIT_FIFO, DMA_DS | DMA_DW8},		// coherent
	{SD1_RECV_FIFO, DMA_DS | DMA_DR | DMA_DW8}	// coherent
};

void dump_au1000_dma_channel(unsigned int dmanr)
{
	struct dma_chan *chan;

	if (dmanr >= NUM_AU1000_DMA_CHANNELS)
		return;
	chan = &au1000_dma_table[dmanr];

	printk(KERN_INFO "Au1000 DMA%d Register Dump:\n", dmanr);
	printk(KERN_INFO "  mode = 0x%08x\n",
	       au_readl(chan->io + DMA_MODE_SET));
	printk(KERN_INFO "  addr = 0x%08x\n",
	       au_readl(chan->io + DMA_PERIPHERAL_ADDR));
	printk(KERN_INFO "  start0 = 0x%08x\n",
	       au_readl(chan->io + DMA_BUFFER0_START));
	printk(KERN_INFO "  start1 = 0x%08x\n",
	       au_readl(chan->io + DMA_BUFFER1_START));
	printk(KERN_INFO "  count0 = 0x%08x\n",
	       au_readl(chan->io + DMA_BUFFER0_COUNT));
	printk(KERN_INFO "  count1 = 0x%08x\n",
	       au_readl(chan->io + DMA_BUFFER1_COUNT));
}


/*
 * Finds a free channel, and binds the requested device to it.
 * Returns the allocated channel number, or negative on error.
 * Requests the DMA done IRQ if irqhandler != NULL.
 */
int request_au1000_dma(int dev_id, const char *dev_str,
		       irq_handler_t irqhandler,
		       unsigned long irqflags,
		       void *irq_dev_id)
{
	struct dma_chan *chan;
	const struct dma_dev *dev;
	int i, ret;

#if defined(CONFIG_SOC_AU1100)
	if (dev_id < 0 || dev_id >= (DMA_NUM_DEV + DMA_NUM_DEV_BANK2))
		return -EINVAL;
#else
 	if (dev_id < 0 || dev_id >= DMA_NUM_DEV)
		return -EINVAL;
#endif

	for (i = 0; i < NUM_AU1000_DMA_CHANNELS; i++) {
		if (au1000_dma_table[i].dev_id < 0)
			break;
	}
	if (i == NUM_AU1000_DMA_CHANNELS)
		return -ENODEV;

	chan = &au1000_dma_table[i];

	if (dev_id >= DMA_NUM_DEV) {
		dev_id -= DMA_NUM_DEV;
		dev = &dma_dev_table_bank2[dev_id];
	} else {
		dev = &dma_dev_table[dev_id];
	}

	if (irqhandler) {
		chan->irq = AU1000_DMA_INT_BASE + i;
		chan->irq_dev = irq_dev_id;
		if ((ret = request_irq(chan->irq, irqhandler, irqflags,
				       dev_str, chan->irq_dev))) {
			chan->irq = 0;
			chan->irq_dev = NULL;
			return ret;
		}
	} else {
		chan->irq = 0;
		chan->irq_dev = NULL;
	}

	// fill it in
	chan->io = DMA_CHANNEL_BASE + i * DMA_CHANNEL_LEN;
	chan->dev_id = dev_id;
	chan->dev_str = dev_str;
	chan->fifo_addr = dev->fifo_addr;
	chan->mode = dev->dma_mode;

	/* initialize the channel before returning */
	init_dma(i);

	return i;
}
EXPORT_SYMBOL(request_au1000_dma);

void free_au1000_dma(unsigned int dmanr)
{
	struct dma_chan *chan = get_dma_chan(dmanr);
	if (!chan) {
		printk("Trying to free DMA%d\n", dmanr);
		return;
	}

	disable_dma(dmanr);
	if (chan->irq)
		free_irq(chan->irq, chan->irq_dev);

	chan->irq = 0;
	chan->irq_dev = NULL;
	chan->dev_id = -1;
}
EXPORT_SYMBOL(free_au1000_dma);

#endif // AU1000 AU1500 AU1100
