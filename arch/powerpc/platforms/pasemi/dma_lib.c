/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Common functions for DMA access on PA Semi PWRficient
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/sched.h>

#include <asm/pasemi_dma.h>

#define MAX_TXCH 64
#define MAX_RXCH 64
#define MAX_FLAGS 64
#define MAX_FUN 8

static struct pasdma_status *dma_status;

static void __iomem *iob_regs;
static void __iomem *mac_regs[6];
static void __iomem *dma_regs;

static int base_hw_irq;

static int num_txch, num_rxch;

static struct pci_dev *dma_pdev;

/* Bitmaps to handle allocation of channels */

static DECLARE_BITMAP(txch_free, MAX_TXCH);
static DECLARE_BITMAP(rxch_free, MAX_RXCH);
static DECLARE_BITMAP(flags_free, MAX_FLAGS);
static DECLARE_BITMAP(fun_free, MAX_FUN);

/* pasemi_read_iob_reg - read IOB register
 * @reg: Register to read (offset into PCI CFG space)
 */
unsigned int pasemi_read_iob_reg(unsigned int reg)
{
	return in_le32(iob_regs+reg);
}
EXPORT_SYMBOL(pasemi_read_iob_reg);

/* pasemi_write_iob_reg - write IOB register
 * @reg: Register to write to (offset into PCI CFG space)
 * @val: Value to write
 */
void pasemi_write_iob_reg(unsigned int reg, unsigned int val)
{
	out_le32(iob_regs+reg, val);
}
EXPORT_SYMBOL(pasemi_write_iob_reg);

/* pasemi_read_mac_reg - read MAC register
 * @intf: MAC interface
 * @reg: Register to read (offset into PCI CFG space)
 */
unsigned int pasemi_read_mac_reg(int intf, unsigned int reg)
{
	return in_le32(mac_regs[intf]+reg);
}
EXPORT_SYMBOL(pasemi_read_mac_reg);

/* pasemi_write_mac_reg - write MAC register
 * @intf: MAC interface
 * @reg: Register to write to (offset into PCI CFG space)
 * @val: Value to write
 */
void pasemi_write_mac_reg(int intf, unsigned int reg, unsigned int val)
{
	out_le32(mac_regs[intf]+reg, val);
}
EXPORT_SYMBOL(pasemi_write_mac_reg);

/* pasemi_read_dma_reg - read DMA register
 * @reg: Register to read (offset into PCI CFG space)
 */
unsigned int pasemi_read_dma_reg(unsigned int reg)
{
	return in_le32(dma_regs+reg);
}
EXPORT_SYMBOL(pasemi_read_dma_reg);

/* pasemi_write_dma_reg - write DMA register
 * @reg: Register to write to (offset into PCI CFG space)
 * @val: Value to write
 */
void pasemi_write_dma_reg(unsigned int reg, unsigned int val)
{
	out_le32(dma_regs+reg, val);
}
EXPORT_SYMBOL(pasemi_write_dma_reg);

static int pasemi_alloc_tx_chan(enum pasemi_dmachan_type type)
{
	int bit;
	int start, limit;

	switch (type & (TXCHAN_EVT0|TXCHAN_EVT1)) {
	case TXCHAN_EVT0:
		start = 0;
		limit = 10;
		break;
	case TXCHAN_EVT1:
		start = 10;
		limit = MAX_TXCH;
		break;
	default:
		start = 0;
		limit = MAX_TXCH;
		break;
	}
retry:
	bit = find_next_bit(txch_free, MAX_TXCH, start);
	if (bit >= limit)
		return -ENOSPC;
	if (!test_and_clear_bit(bit, txch_free))
		goto retry;

	return bit;
}

static void pasemi_free_tx_chan(int chan)
{
	BUG_ON(test_bit(chan, txch_free));
	set_bit(chan, txch_free);
}

static int pasemi_alloc_rx_chan(void)
{
	int bit;
retry:
	bit = find_first_bit(rxch_free, MAX_RXCH);
	if (bit >= MAX_TXCH)
		return -ENOSPC;
	if (!test_and_clear_bit(bit, rxch_free))
		goto retry;

	return bit;
}

static void pasemi_free_rx_chan(int chan)
{
	BUG_ON(test_bit(chan, rxch_free));
	set_bit(chan, rxch_free);
}

/* pasemi_dma_alloc_chan - Allocate a DMA channel
 * @type: Type of channel to allocate
 * @total_size: Total size of structure to allocate (to allow for more
 *		room behind the structure to be used by the client)
 * @offset: Offset in bytes from start of the total structure to the beginning
 *	    of struct pasemi_dmachan. Needed when struct pasemi_dmachan is
 *	    not the first member of the client structure.
 *
 * pasemi_dma_alloc_chan allocates a DMA channel for use by a client. The
 * type argument specifies whether it's a RX or TX channel, and in the case
 * of TX channels which group it needs to belong to (if any).
 *
 * Returns a pointer to the total structure allocated on success, NULL
 * on failure.
 */
void *pasemi_dma_alloc_chan(enum pasemi_dmachan_type type,
			    int total_size, int offset)
{
	void *buf;
	struct pasemi_dmachan *chan;
	int chno;

	BUG_ON(total_size < sizeof(struct pasemi_dmachan));

	buf = kzalloc(total_size, GFP_KERNEL);

	if (!buf)
		return NULL;
	chan = buf + offset;

	chan->priv = buf;

	switch (type & (TXCHAN|RXCHAN)) {
	case RXCHAN:
		chno = pasemi_alloc_rx_chan();
		chan->chno = chno;
		chan->irq = irq_create_mapping(NULL,
					       base_hw_irq + num_txch + chno);
		chan->status = &dma_status->rx_sta[chno];
		break;
	case TXCHAN:
		chno = pasemi_alloc_tx_chan(type);
		chan->chno = chno;
		chan->irq = irq_create_mapping(NULL, base_hw_irq + chno);
		chan->status = &dma_status->tx_sta[chno];
		break;
	}

	chan->chan_type = type;

	return chan;
}
EXPORT_SYMBOL(pasemi_dma_alloc_chan);

/* pasemi_dma_free_chan - Free a previously allocated channel
 * @chan: Channel to free
 *
 * Frees a previously allocated channel. It will also deallocate any
 * descriptor ring associated with the channel, if allocated.
 */
void pasemi_dma_free_chan(struct pasemi_dmachan *chan)
{
	if (chan->ring_virt)
		pasemi_dma_free_ring(chan);

	switch (chan->chan_type & (RXCHAN|TXCHAN)) {
	case RXCHAN:
		pasemi_free_rx_chan(chan->chno);
		break;
	case TXCHAN:
		pasemi_free_tx_chan(chan->chno);
		break;
	}

	kfree(chan->priv);
}
EXPORT_SYMBOL(pasemi_dma_free_chan);

/* pasemi_dma_alloc_ring - Allocate descriptor ring for a channel
 * @chan: Channel for which to allocate
 * @ring_size: Ring size in 64-bit (8-byte) words
 *
 * Allocate a descriptor ring for a channel. Returns 0 on success, errno
 * on failure. The passed in struct pasemi_dmachan is updated with the
 * virtual and DMA addresses of the ring.
 */
int pasemi_dma_alloc_ring(struct pasemi_dmachan *chan, int ring_size)
{
	BUG_ON(chan->ring_virt);

	chan->ring_size = ring_size;

	chan->ring_virt = dma_alloc_coherent(&dma_pdev->dev,
					     ring_size * sizeof(u64),
					     &chan->ring_dma, GFP_KERNEL);

	if (!chan->ring_virt)
		return -ENOMEM;

	memset(chan->ring_virt, 0, ring_size * sizeof(u64));

	return 0;
}
EXPORT_SYMBOL(pasemi_dma_alloc_ring);

/* pasemi_dma_free_ring - Free an allocated descriptor ring for a channel
 * @chan: Channel for which to free the descriptor ring
 *
 * Frees a previously allocated descriptor ring for a channel.
 */
void pasemi_dma_free_ring(struct pasemi_dmachan *chan)
{
	BUG_ON(!chan->ring_virt);

	dma_free_coherent(&dma_pdev->dev, chan->ring_size * sizeof(u64),
			  chan->ring_virt, chan->ring_dma);
	chan->ring_virt = NULL;
	chan->ring_size = 0;
	chan->ring_dma = 0;
}
EXPORT_SYMBOL(pasemi_dma_free_ring);

/* pasemi_dma_start_chan - Start a DMA channel
 * @chan: Channel to start
 * @cmdsta: Additional CCMDSTA/TCMDSTA bits to write
 *
 * Enables (starts) a DMA channel with optional additional arguments.
 */
void pasemi_dma_start_chan(const struct pasemi_dmachan *chan, const u32 cmdsta)
{
	if (chan->chan_type == RXCHAN)
		pasemi_write_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(chan->chno),
				     cmdsta | PAS_DMA_RXCHAN_CCMDSTA_EN);
	else
		pasemi_write_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(chan->chno),
				     cmdsta | PAS_DMA_TXCHAN_TCMDSTA_EN);
}
EXPORT_SYMBOL(pasemi_dma_start_chan);

/* pasemi_dma_stop_chan - Stop a DMA channel
 * @chan: Channel to stop
 *
 * Stops (disables) a DMA channel. This is done by setting the ST bit in the
 * CMDSTA register and waiting on the ACT (active) bit to clear, then
 * finally disabling the whole channel.
 *
 * This function will only try for a short while for the channel to stop, if
 * it doesn't it will return failure.
 *
 * Returns 1 on success, 0 on failure.
 */
#define MAX_RETRIES 5000
int pasemi_dma_stop_chan(const struct pasemi_dmachan *chan)
{
	int reg, retries;
	u32 sta;

	if (chan->chan_type == RXCHAN) {
		reg = PAS_DMA_RXCHAN_CCMDSTA(chan->chno);
		pasemi_write_dma_reg(reg, PAS_DMA_RXCHAN_CCMDSTA_ST);
		for (retries = 0; retries < MAX_RETRIES; retries++) {
			sta = pasemi_read_dma_reg(reg);
			if (!(sta & PAS_DMA_RXCHAN_CCMDSTA_ACT)) {
				pasemi_write_dma_reg(reg, 0);
				return 1;
			}
			cond_resched();
		}
	} else {
		reg = PAS_DMA_TXCHAN_TCMDSTA(chan->chno);
		pasemi_write_dma_reg(reg, PAS_DMA_TXCHAN_TCMDSTA_ST);
		for (retries = 0; retries < MAX_RETRIES; retries++) {
			sta = pasemi_read_dma_reg(reg);
			if (!(sta & PAS_DMA_TXCHAN_TCMDSTA_ACT)) {
				pasemi_write_dma_reg(reg, 0);
				return 1;
			}
			cond_resched();
		}
	}

	return 0;
}
EXPORT_SYMBOL(pasemi_dma_stop_chan);

/* pasemi_dma_alloc_buf - Allocate a buffer to use for DMA
 * @chan: Channel to allocate for
 * @size: Size of buffer in bytes
 * @handle: DMA handle
 *
 * Allocate a buffer to be used by the DMA engine for read/write,
 * similar to dma_alloc_coherent().
 *
 * Returns the virtual address of the buffer, or NULL in case of failure.
 */
void *pasemi_dma_alloc_buf(struct pasemi_dmachan *chan, int size,
			   dma_addr_t *handle)
{
	return dma_alloc_coherent(&dma_pdev->dev, size, handle, GFP_KERNEL);
}
EXPORT_SYMBOL(pasemi_dma_alloc_buf);

/* pasemi_dma_free_buf - Free a buffer used for DMA
 * @chan: Channel the buffer was allocated for
 * @size: Size of buffer in bytes
 * @handle: DMA handle
 *
 * Frees a previously allocated buffer.
 */
void pasemi_dma_free_buf(struct pasemi_dmachan *chan, int size,
			 dma_addr_t *handle)
{
	dma_free_coherent(&dma_pdev->dev, size, handle, GFP_KERNEL);
}
EXPORT_SYMBOL(pasemi_dma_free_buf);

/* pasemi_dma_alloc_flag - Allocate a flag (event) for channel synchronization
 *
 * Allocates a flag for use with channel synchronization (event descriptors).
 * Returns allocated flag (0-63), < 0 on error.
 */
int pasemi_dma_alloc_flag(void)
{
	int bit;

retry:
	bit = find_next_bit(flags_free, MAX_FLAGS, 0);
	if (bit >= MAX_FLAGS)
		return -ENOSPC;
	if (!test_and_clear_bit(bit, flags_free))
		goto retry;

	return bit;
}
EXPORT_SYMBOL(pasemi_dma_alloc_flag);


/* pasemi_dma_free_flag - Deallocates a flag (event)
 * @flag: Flag number to deallocate
 *
 * Frees up a flag so it can be reused for other purposes.
 */
void pasemi_dma_free_flag(int flag)
{
	BUG_ON(test_bit(flag, flags_free));
	BUG_ON(flag >= MAX_FLAGS);
	set_bit(flag, flags_free);
}
EXPORT_SYMBOL(pasemi_dma_free_flag);


/* pasemi_dma_set_flag - Sets a flag (event) to 1
 * @flag: Flag number to set active
 *
 * Sets the flag provided to 1.
 */
void pasemi_dma_set_flag(int flag)
{
	BUG_ON(flag >= MAX_FLAGS);
	if (flag < 32)
		pasemi_write_dma_reg(PAS_DMA_TXF_SFLG0, 1 << flag);
	else
		pasemi_write_dma_reg(PAS_DMA_TXF_SFLG1, 1 << flag);
}
EXPORT_SYMBOL(pasemi_dma_set_flag);

/* pasemi_dma_clear_flag - Sets a flag (event) to 0
 * @flag: Flag number to set inactive
 *
 * Sets the flag provided to 0.
 */
void pasemi_dma_clear_flag(int flag)
{
	BUG_ON(flag >= MAX_FLAGS);
	if (flag < 32)
		pasemi_write_dma_reg(PAS_DMA_TXF_CFLG0, 1 << flag);
	else
		pasemi_write_dma_reg(PAS_DMA_TXF_CFLG1, 1 << flag);
}
EXPORT_SYMBOL(pasemi_dma_clear_flag);

/* pasemi_dma_alloc_fun - Allocate a function engine
 *
 * Allocates a function engine to use for crypto/checksum offload
 * Returns allocated engine (0-8), < 0 on error.
 */
int pasemi_dma_alloc_fun(void)
{
	int bit;

retry:
	bit = find_next_bit(fun_free, MAX_FLAGS, 0);
	if (bit >= MAX_FLAGS)
		return -ENOSPC;
	if (!test_and_clear_bit(bit, fun_free))
		goto retry;

	return bit;
}
EXPORT_SYMBOL(pasemi_dma_alloc_fun);


/* pasemi_dma_free_fun - Deallocates a function engine
 * @flag: Engine number to deallocate
 *
 * Frees up a function engine so it can be used for other purposes.
 */
void pasemi_dma_free_fun(int fun)
{
	BUG_ON(test_bit(fun, fun_free));
	BUG_ON(fun >= MAX_FLAGS);
	set_bit(fun, fun_free);
}
EXPORT_SYMBOL(pasemi_dma_free_fun);


static void *map_onedev(struct pci_dev *p, int index)
{
	struct device_node *dn;
	void __iomem *ret;

	dn = pci_device_to_OF_node(p);
	if (!dn)
		goto fallback;

	ret = of_iomap(dn, index);
	if (!ret)
		goto fallback;

	return ret;
fallback:
	/* This is hardcoded and ugly, but we have some firmware versions
	 * that don't provide the register space in the device tree. Luckily
	 * they are at well-known locations so we can just do the math here.
	 */
	return ioremap(0xe0000000 + (p->devfn << 12), 0x2000);
}

/* pasemi_dma_init - Initialize the PA Semi DMA library
 *
 * This function initializes the DMA library. It must be called before
 * any other function in the library.
 *
 * Returns 0 on success, errno on failure.
 */
int pasemi_dma_init(void)
{
	static DEFINE_SPINLOCK(init_lock);
	struct pci_dev *iob_pdev;
	struct pci_dev *pdev;
	struct resource res;
	struct device_node *dn;
	int i, intf, err = 0;
	unsigned long timeout;
	u32 tmp;

	if (!machine_is(pasemi))
		return -ENODEV;

	spin_lock(&init_lock);

	/* Make sure we haven't already initialized */
	if (dma_pdev)
		goto out;

	iob_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa001, NULL);
	if (!iob_pdev) {
		BUG();
		printk(KERN_WARNING "Can't find I/O Bridge\n");
		err = -ENODEV;
		goto out;
	}
	iob_regs = map_onedev(iob_pdev, 0);

	dma_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa007, NULL);
	if (!dma_pdev) {
		BUG();
		printk(KERN_WARNING "Can't find DMA controller\n");
		err = -ENODEV;
		goto out;
	}
	dma_regs = map_onedev(dma_pdev, 0);
	base_hw_irq = virq_to_hw(dma_pdev->irq);

	pci_read_config_dword(dma_pdev, PAS_DMA_CAP_TXCH, &tmp);
	num_txch = (tmp & PAS_DMA_CAP_TXCH_TCHN_M) >> PAS_DMA_CAP_TXCH_TCHN_S;

	pci_read_config_dword(dma_pdev, PAS_DMA_CAP_RXCH, &tmp);
	num_rxch = (tmp & PAS_DMA_CAP_RXCH_RCHN_M) >> PAS_DMA_CAP_RXCH_RCHN_S;

	intf = 0;
	for (pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa006, NULL);
	     pdev;
	     pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa006, pdev))
		mac_regs[intf++] = map_onedev(pdev, 0);

	pci_dev_put(pdev);

	for (pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa005, NULL);
	     pdev;
	     pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa005, pdev))
		mac_regs[intf++] = map_onedev(pdev, 0);

	pci_dev_put(pdev);

	dn = pci_device_to_OF_node(iob_pdev);
	if (dn)
		err = of_address_to_resource(dn, 1, &res);
	if (!dn || err) {
		/* Fallback for old firmware */
		res.start = 0xfd800000;
		res.end = res.start + 0x1000;
	}
	dma_status = __ioremap(res.start, resource_size(&res), 0);
	pci_dev_put(iob_pdev);

	for (i = 0; i < MAX_TXCH; i++)
		__set_bit(i, txch_free);

	for (i = 0; i < MAX_RXCH; i++)
		__set_bit(i, rxch_free);

	timeout = jiffies + HZ;
	pasemi_write_dma_reg(PAS_DMA_COM_RXCMD, 0);
	while (pasemi_read_dma_reg(PAS_DMA_COM_RXSTA) & 1) {
		if (time_after(jiffies, timeout)) {
			pr_warning("Warning: Could not disable RX section\n");
			break;
		}
	}

	timeout = jiffies + HZ;
	pasemi_write_dma_reg(PAS_DMA_COM_TXCMD, 0);
	while (pasemi_read_dma_reg(PAS_DMA_COM_TXSTA) & 1) {
		if (time_after(jiffies, timeout)) {
			pr_warning("Warning: Could not disable TX section\n");
			break;
		}
	}

	/* setup resource allocations for the different DMA sections */
	tmp = pasemi_read_dma_reg(PAS_DMA_COM_CFG);
	pasemi_write_dma_reg(PAS_DMA_COM_CFG, tmp | 0x18000000);

	/* enable tx section */
	pasemi_write_dma_reg(PAS_DMA_COM_TXCMD, PAS_DMA_COM_TXCMD_EN);

	/* enable rx section */
	pasemi_write_dma_reg(PAS_DMA_COM_RXCMD, PAS_DMA_COM_RXCMD_EN);

	for (i = 0; i < MAX_FLAGS; i++)
		__set_bit(i, flags_free);

	for (i = 0; i < MAX_FUN; i++)
		__set_bit(i, fun_free);

	/* clear all status flags */
	pasemi_write_dma_reg(PAS_DMA_TXF_CFLG0, 0xffffffff);
	pasemi_write_dma_reg(PAS_DMA_TXF_CFLG1, 0xffffffff);

	printk(KERN_INFO "PA Semi PWRficient DMA library initialized "
		"(%d tx, %d rx channels)\n", num_txch, num_rxch);

out:
	spin_unlock(&init_lock);
	return err;
}
EXPORT_SYMBOL(pasemi_dma_init);
