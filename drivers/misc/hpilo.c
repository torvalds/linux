/*
 * Driver for HP iLO/iLO2 management processor.
 *
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	David Altobelli <david.altobelli@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include "hpilo.h"

static struct class *ilo_class;
static unsigned int ilo_major;
static char ilo_hwdev[MAX_ILO_DEV];

static inline int get_entry_id(int entry)
{
	return (entry & ENTRY_MASK_DESCRIPTOR) >> ENTRY_BITPOS_DESCRIPTOR;
}

static inline int get_entry_len(int entry)
{
	return ((entry & ENTRY_MASK_QWORDS) >> ENTRY_BITPOS_QWORDS) << 3;
}

static inline int mk_entry(int id, int len)
{
	int qlen = len & 7 ? (len >> 3) + 1 : len >> 3;
	return id << ENTRY_BITPOS_DESCRIPTOR | qlen << ENTRY_BITPOS_QWORDS;
}

static inline int desc_mem_sz(int nr_entry)
{
	return nr_entry << L2_QENTRY_SZ;
}

/*
 * FIFO queues, shared with hardware.
 *
 * If a queue has empty slots, an entry is added to the queue tail,
 * and that entry is marked as occupied.
 * Entries can be dequeued from the head of the list, when the device
 * has marked the entry as consumed.
 *
 * Returns true on successful queue/dequeue, false on failure.
 */
static int fifo_enqueue(struct ilo_hwinfo *hw, char *fifobar, int entry)
{
	struct fifo *fifo_q = FIFOBARTOHANDLE(fifobar);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hw->fifo_lock, flags);
	if (!(fifo_q->fifobar[(fifo_q->tail + 1) & fifo_q->imask]
	      & ENTRY_MASK_O)) {
		fifo_q->fifobar[fifo_q->tail & fifo_q->imask] |=
				(entry & ENTRY_MASK_NOSTATE) | fifo_q->merge;
		fifo_q->tail += 1;
		ret = 1;
	}
	spin_unlock_irqrestore(&hw->fifo_lock, flags);

	return ret;
}

static int fifo_dequeue(struct ilo_hwinfo *hw, char *fifobar, int *entry)
{
	struct fifo *fifo_q = FIFOBARTOHANDLE(fifobar);
	unsigned long flags;
	int ret = 0;
	u64 c;

	spin_lock_irqsave(&hw->fifo_lock, flags);
	c = fifo_q->fifobar[fifo_q->head & fifo_q->imask];
	if (c & ENTRY_MASK_C) {
		if (entry)
			*entry = c & ENTRY_MASK_NOSTATE;

		fifo_q->fifobar[fifo_q->head & fifo_q->imask] =
							(c | ENTRY_MASK) + 1;
		fifo_q->head += 1;
		ret = 1;
	}
	spin_unlock_irqrestore(&hw->fifo_lock, flags);

	return ret;
}

static int fifo_check_recv(struct ilo_hwinfo *hw, char *fifobar)
{
	struct fifo *fifo_q = FIFOBARTOHANDLE(fifobar);
	unsigned long flags;
	int ret = 0;
	u64 c;

	spin_lock_irqsave(&hw->fifo_lock, flags);
	c = fifo_q->fifobar[fifo_q->head & fifo_q->imask];
	if (c & ENTRY_MASK_C)
		ret = 1;
	spin_unlock_irqrestore(&hw->fifo_lock, flags);

	return ret;
}

static int ilo_pkt_enqueue(struct ilo_hwinfo *hw, struct ccb *ccb,
			   int dir, int id, int len)
{
	char *fifobar;
	int entry;

	if (dir == SENDQ)
		fifobar = ccb->ccb_u1.send_fifobar;
	else
		fifobar = ccb->ccb_u3.recv_fifobar;

	entry = mk_entry(id, len);
	return fifo_enqueue(hw, fifobar, entry);
}

static int ilo_pkt_dequeue(struct ilo_hwinfo *hw, struct ccb *ccb,
			   int dir, int *id, int *len, void **pkt)
{
	char *fifobar, *desc;
	int entry = 0, pkt_id = 0;
	int ret;

	if (dir == SENDQ) {
		fifobar = ccb->ccb_u1.send_fifobar;
		desc = ccb->ccb_u2.send_desc;
	} else {
		fifobar = ccb->ccb_u3.recv_fifobar;
		desc = ccb->ccb_u4.recv_desc;
	}

	ret = fifo_dequeue(hw, fifobar, &entry);
	if (ret) {
		pkt_id = get_entry_id(entry);
		if (id)
			*id = pkt_id;
		if (len)
			*len = get_entry_len(entry);
		if (pkt)
			*pkt = (void *)(desc + desc_mem_sz(pkt_id));
	}

	return ret;
}

static int ilo_pkt_recv(struct ilo_hwinfo *hw, struct ccb *ccb)
{
	char *fifobar = ccb->ccb_u3.recv_fifobar;

	return fifo_check_recv(hw, fifobar);
}

static inline void doorbell_set(struct ccb *ccb)
{
	iowrite8(1, ccb->ccb_u5.db_base);
}

static inline void doorbell_clr(struct ccb *ccb)
{
	iowrite8(2, ccb->ccb_u5.db_base);
}

static inline int ctrl_set(int l2sz, int idxmask, int desclim)
{
	int active = 0, go = 1;
	return l2sz << CTRL_BITPOS_L2SZ |
	       idxmask << CTRL_BITPOS_FIFOINDEXMASK |
	       desclim << CTRL_BITPOS_DESCLIMIT |
	       active << CTRL_BITPOS_A |
	       go << CTRL_BITPOS_G;
}

static void ctrl_setup(struct ccb *ccb, int nr_desc, int l2desc_sz)
{
	/* for simplicity, use the same parameters for send and recv ctrls */
	ccb->send_ctrl = ctrl_set(l2desc_sz, nr_desc-1, nr_desc-1);
	ccb->recv_ctrl = ctrl_set(l2desc_sz, nr_desc-1, nr_desc-1);
}

static inline int fifo_sz(int nr_entry)
{
	/* size of a fifo is determined by the number of entries it contains */
	return (nr_entry * sizeof(u64)) + FIFOHANDLESIZE;
}

static void fifo_setup(void *base_addr, int nr_entry)
{
	struct fifo *fifo_q = base_addr;
	int i;

	/* set up an empty fifo */
	fifo_q->head = 0;
	fifo_q->tail = 0;
	fifo_q->reset = 0;
	fifo_q->nrents = nr_entry;
	fifo_q->imask = nr_entry - 1;
	fifo_q->merge = ENTRY_MASK_O;

	for (i = 0; i < nr_entry; i++)
		fifo_q->fifobar[i] = 0;
}

static void ilo_ccb_close(struct pci_dev *pdev, struct ccb_data *data)
{
	struct ccb *driver_ccb = &data->driver_ccb;
	struct ccb __iomem *device_ccb = data->mapped_ccb;
	int retries;

	/* complicated dance to tell the hw we are stopping */
	doorbell_clr(driver_ccb);
	iowrite32(ioread32(&device_ccb->send_ctrl) & ~(1 << CTRL_BITPOS_G),
		  &device_ccb->send_ctrl);
	iowrite32(ioread32(&device_ccb->recv_ctrl) & ~(1 << CTRL_BITPOS_G),
		  &device_ccb->recv_ctrl);

	/* give iLO some time to process stop request */
	for (retries = MAX_WAIT; retries > 0; retries--) {
		doorbell_set(driver_ccb);
		udelay(WAIT_TIME);
		if (!(ioread32(&device_ccb->send_ctrl) & (1 << CTRL_BITPOS_A))
		    &&
		    !(ioread32(&device_ccb->recv_ctrl) & (1 << CTRL_BITPOS_A)))
			break;
	}
	if (retries == 0)
		dev_err(&pdev->dev, "Closing, but controller still active\n");

	/* clear the hw ccb */
	memset_io(device_ccb, 0, sizeof(struct ccb));

	/* free resources used to back send/recv queues */
	pci_free_consistent(pdev, data->dma_size, data->dma_va, data->dma_pa);
}

static int ilo_ccb_setup(struct ilo_hwinfo *hw, struct ccb_data *data, int slot)
{
	char *dma_va, *dma_pa;
	struct ccb *driver_ccb, *ilo_ccb;

	driver_ccb = &data->driver_ccb;
	ilo_ccb = &data->ilo_ccb;

	data->dma_size = 2 * fifo_sz(NR_QENTRY) +
			 2 * desc_mem_sz(NR_QENTRY) +
			 ILO_START_ALIGN + ILO_CACHE_SZ;

	data->dma_va = pci_alloc_consistent(hw->ilo_dev, data->dma_size,
					    &data->dma_pa);
	if (!data->dma_va)
		return -ENOMEM;

	dma_va = (char *)data->dma_va;
	dma_pa = (char *)data->dma_pa;

	memset(dma_va, 0, data->dma_size);

	dma_va = (char *)roundup((unsigned long)dma_va, ILO_START_ALIGN);
	dma_pa = (char *)roundup((unsigned long)dma_pa, ILO_START_ALIGN);

	/*
	 * Create two ccb's, one with virt addrs, one with phys addrs.
	 * Copy the phys addr ccb to device shared mem.
	 */
	ctrl_setup(driver_ccb, NR_QENTRY, L2_QENTRY_SZ);
	ctrl_setup(ilo_ccb, NR_QENTRY, L2_QENTRY_SZ);

	fifo_setup(dma_va, NR_QENTRY);
	driver_ccb->ccb_u1.send_fifobar = dma_va + FIFOHANDLESIZE;
	ilo_ccb->ccb_u1.send_fifobar = dma_pa + FIFOHANDLESIZE;
	dma_va += fifo_sz(NR_QENTRY);
	dma_pa += fifo_sz(NR_QENTRY);

	dma_va = (char *)roundup((unsigned long)dma_va, ILO_CACHE_SZ);
	dma_pa = (char *)roundup((unsigned long)dma_pa, ILO_CACHE_SZ);

	fifo_setup(dma_va, NR_QENTRY);
	driver_ccb->ccb_u3.recv_fifobar = dma_va + FIFOHANDLESIZE;
	ilo_ccb->ccb_u3.recv_fifobar = dma_pa + FIFOHANDLESIZE;
	dma_va += fifo_sz(NR_QENTRY);
	dma_pa += fifo_sz(NR_QENTRY);

	driver_ccb->ccb_u2.send_desc = dma_va;
	ilo_ccb->ccb_u2.send_desc = dma_pa;
	dma_pa += desc_mem_sz(NR_QENTRY);
	dma_va += desc_mem_sz(NR_QENTRY);

	driver_ccb->ccb_u4.recv_desc = dma_va;
	ilo_ccb->ccb_u4.recv_desc = dma_pa;

	driver_ccb->channel = slot;
	ilo_ccb->channel = slot;

	driver_ccb->ccb_u5.db_base = hw->db_vaddr + (slot << L2_DB_SIZE);
	ilo_ccb->ccb_u5.db_base = NULL; /* hw ccb's doorbell is not used */

	return 0;
}

static void ilo_ccb_open(struct ilo_hwinfo *hw, struct ccb_data *data, int slot)
{
	int pkt_id, pkt_sz;
	struct ccb *driver_ccb = &data->driver_ccb;

	/* copy the ccb with physical addrs to device memory */
	data->mapped_ccb = (struct ccb __iomem *)
				(hw->ram_vaddr + (slot * ILOHW_CCB_SZ));
	memcpy_toio(data->mapped_ccb, &data->ilo_ccb, sizeof(struct ccb));

	/* put packets on the send and receive queues */
	pkt_sz = 0;
	for (pkt_id = 0; pkt_id < NR_QENTRY; pkt_id++) {
		ilo_pkt_enqueue(hw, driver_ccb, SENDQ, pkt_id, pkt_sz);
		doorbell_set(driver_ccb);
	}

	pkt_sz = desc_mem_sz(1);
	for (pkt_id = 0; pkt_id < NR_QENTRY; pkt_id++)
		ilo_pkt_enqueue(hw, driver_ccb, RECVQ, pkt_id, pkt_sz);

	/* the ccb is ready to use */
	doorbell_clr(driver_ccb);
}

static int ilo_ccb_verify(struct ilo_hwinfo *hw, struct ccb_data *data)
{
	int pkt_id, i;
	struct ccb *driver_ccb = &data->driver_ccb;

	/* make sure iLO is really handling requests */
	for (i = MAX_WAIT; i > 0; i--) {
		if (ilo_pkt_dequeue(hw, driver_ccb, SENDQ, &pkt_id, NULL, NULL))
			break;
		udelay(WAIT_TIME);
	}

	if (i == 0) {
		dev_err(&hw->ilo_dev->dev, "Open could not dequeue a packet\n");
		return -EBUSY;
	}

	ilo_pkt_enqueue(hw, driver_ccb, SENDQ, pkt_id, 0);
	doorbell_set(driver_ccb);
	return 0;
}

static inline int is_channel_reset(struct ccb *ccb)
{
	/* check for this particular channel needing a reset */
	return FIFOBARTOHANDLE(ccb->ccb_u1.send_fifobar)->reset;
}

static inline void set_channel_reset(struct ccb *ccb)
{
	/* set a flag indicating this channel needs a reset */
	FIFOBARTOHANDLE(ccb->ccb_u1.send_fifobar)->reset = 1;
}

static inline int get_device_outbound(struct ilo_hwinfo *hw)
{
	return ioread32(&hw->mmio_vaddr[DB_OUT]);
}

static inline int is_db_reset(int db_out)
{
	return db_out & (1 << DB_RESET);
}

static inline int is_device_reset(struct ilo_hwinfo *hw)
{
	/* check for global reset condition */
	return is_db_reset(get_device_outbound(hw));
}

static inline void clear_pending_db(struct ilo_hwinfo *hw, int clr)
{
	iowrite32(clr, &hw->mmio_vaddr[DB_OUT]);
}

static inline void clear_device(struct ilo_hwinfo *hw)
{
	/* clear the device (reset bits, pending channel entries) */
	clear_pending_db(hw, -1);
}

static inline void ilo_enable_interrupts(struct ilo_hwinfo *hw)
{
	iowrite8(ioread8(&hw->mmio_vaddr[DB_IRQ]) | 1, &hw->mmio_vaddr[DB_IRQ]);
}

static inline void ilo_disable_interrupts(struct ilo_hwinfo *hw)
{
	iowrite8(ioread8(&hw->mmio_vaddr[DB_IRQ]) & ~1,
		 &hw->mmio_vaddr[DB_IRQ]);
}

static void ilo_set_reset(struct ilo_hwinfo *hw)
{
	int slot;

	/*
	 * Mapped memory is zeroed on ilo reset, so set a per ccb flag
	 * to indicate that this ccb needs to be closed and reopened.
	 */
	for (slot = 0; slot < MAX_CCB; slot++) {
		if (!hw->ccb_alloc[slot])
			continue;
		set_channel_reset(&hw->ccb_alloc[slot]->driver_ccb);
	}
}

static ssize_t ilo_read(struct file *fp, char __user *buf,
			size_t len, loff_t *off)
{
	int err, found, cnt, pkt_id, pkt_len;
	struct ccb_data *data = fp->private_data;
	struct ccb *driver_ccb = &data->driver_ccb;
	struct ilo_hwinfo *hw = data->ilo_hw;
	void *pkt;

	if (is_channel_reset(driver_ccb)) {
		/*
		 * If the device has been reset, applications
		 * need to close and reopen all ccbs.
		 */
		return -ENODEV;
	}

	/*
	 * This function is to be called when data is expected
	 * in the channel, and will return an error if no packet is found
	 * during the loop below.  The sleep/retry logic is to allow
	 * applications to call read() immediately post write(),
	 * and give iLO some time to process the sent packet.
	 */
	cnt = 20;
	do {
		/* look for a received packet */
		found = ilo_pkt_dequeue(hw, driver_ccb, RECVQ, &pkt_id,
					&pkt_len, &pkt);
		if (found)
			break;
		cnt--;
		msleep(100);
	} while (!found && cnt);

	if (!found)
		return -EAGAIN;

	/* only copy the length of the received packet */
	if (pkt_len < len)
		len = pkt_len;

	err = copy_to_user(buf, pkt, len);

	/* return the received packet to the queue */
	ilo_pkt_enqueue(hw, driver_ccb, RECVQ, pkt_id, desc_mem_sz(1));

	return err ? -EFAULT : len;
}

static ssize_t ilo_write(struct file *fp, const char __user *buf,
			 size_t len, loff_t *off)
{
	int err, pkt_id, pkt_len;
	struct ccb_data *data = fp->private_data;
	struct ccb *driver_ccb = &data->driver_ccb;
	struct ilo_hwinfo *hw = data->ilo_hw;
	void *pkt;

	if (is_channel_reset(driver_ccb))
		return -ENODEV;

	/* get a packet to send the user command */
	if (!ilo_pkt_dequeue(hw, driver_ccb, SENDQ, &pkt_id, &pkt_len, &pkt))
		return -EBUSY;

	/* limit the length to the length of the packet */
	if (pkt_len < len)
		len = pkt_len;

	/* on failure, set the len to 0 to return empty packet to the device */
	err = copy_from_user(pkt, buf, len);
	if (err)
		len = 0;

	/* send the packet */
	ilo_pkt_enqueue(hw, driver_ccb, SENDQ, pkt_id, len);
	doorbell_set(driver_ccb);

	return err ? -EFAULT : len;
}

static unsigned int ilo_poll(struct file *fp, poll_table *wait)
{
	struct ccb_data *data = fp->private_data;
	struct ccb *driver_ccb = &data->driver_ccb;

	poll_wait(fp, &data->ccb_waitq, wait);

	if (is_channel_reset(driver_ccb))
		return POLLERR;
	else if (ilo_pkt_recv(data->ilo_hw, driver_ccb))
		return POLLIN | POLLRDNORM;

	return 0;
}

static int ilo_close(struct inode *ip, struct file *fp)
{
	int slot;
	struct ccb_data *data;
	struct ilo_hwinfo *hw;
	unsigned long flags;

	slot = iminor(ip) % MAX_CCB;
	hw = container_of(ip->i_cdev, struct ilo_hwinfo, cdev);

	spin_lock(&hw->open_lock);

	if (hw->ccb_alloc[slot]->ccb_cnt == 1) {

		data = fp->private_data;

		spin_lock_irqsave(&hw->alloc_lock, flags);
		hw->ccb_alloc[slot] = NULL;
		spin_unlock_irqrestore(&hw->alloc_lock, flags);

		ilo_ccb_close(hw->ilo_dev, data);

		kfree(data);
	} else
		hw->ccb_alloc[slot]->ccb_cnt--;

	spin_unlock(&hw->open_lock);

	return 0;
}

static int ilo_open(struct inode *ip, struct file *fp)
{
	int slot, error;
	struct ccb_data *data;
	struct ilo_hwinfo *hw;
	unsigned long flags;

	slot = iminor(ip) % MAX_CCB;
	hw = container_of(ip->i_cdev, struct ilo_hwinfo, cdev);

	/* new ccb allocation */
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock(&hw->open_lock);

	/* each fd private_data holds sw/hw view of ccb */
	if (hw->ccb_alloc[slot] == NULL) {
		/* create a channel control block for this minor */
		error = ilo_ccb_setup(hw, data, slot);
		if (error) {
			kfree(data);
			goto out;
		}

		data->ccb_cnt = 1;
		data->ccb_excl = fp->f_flags & O_EXCL;
		data->ilo_hw = hw;
		init_waitqueue_head(&data->ccb_waitq);

		/* write the ccb to hw */
		spin_lock_irqsave(&hw->alloc_lock, flags);
		ilo_ccb_open(hw, data, slot);
		hw->ccb_alloc[slot] = data;
		spin_unlock_irqrestore(&hw->alloc_lock, flags);

		/* make sure the channel is functional */
		error = ilo_ccb_verify(hw, data);
		if (error) {

			spin_lock_irqsave(&hw->alloc_lock, flags);
			hw->ccb_alloc[slot] = NULL;
			spin_unlock_irqrestore(&hw->alloc_lock, flags);

			ilo_ccb_close(hw->ilo_dev, data);

			kfree(data);
			goto out;
		}

	} else {
		kfree(data);
		if (fp->f_flags & O_EXCL || hw->ccb_alloc[slot]->ccb_excl) {
			/*
			 * The channel exists, and either this open
			 * or a previous open of this channel wants
			 * exclusive access.
			 */
			error = -EBUSY;
		} else {
			hw->ccb_alloc[slot]->ccb_cnt++;
			error = 0;
		}
	}
out:
	spin_unlock(&hw->open_lock);

	if (!error)
		fp->private_data = hw->ccb_alloc[slot];

	return error;
}

static const struct file_operations ilo_fops = {
	.owner		= THIS_MODULE,
	.read		= ilo_read,
	.write		= ilo_write,
	.poll		= ilo_poll,
	.open 		= ilo_open,
	.release 	= ilo_close,
};

static irqreturn_t ilo_isr(int irq, void *data)
{
	struct ilo_hwinfo *hw = data;
	int pending, i;

	spin_lock(&hw->alloc_lock);

	/* check for ccbs which have data */
	pending = get_device_outbound(hw);
	if (!pending) {
		spin_unlock(&hw->alloc_lock);
		return IRQ_NONE;
	}

	if (is_db_reset(pending)) {
		/* wake up all ccbs if the device was reset */
		pending = -1;
		ilo_set_reset(hw);
	}

	for (i = 0; i < MAX_CCB; i++) {
		if (!hw->ccb_alloc[i])
			continue;
		if (pending & (1 << i))
			wake_up_interruptible(&hw->ccb_alloc[i]->ccb_waitq);
	}

	/* clear the device of the channels that have been handled */
	clear_pending_db(hw, pending);

	spin_unlock(&hw->alloc_lock);

	return IRQ_HANDLED;
}

static void ilo_unmap_device(struct pci_dev *pdev, struct ilo_hwinfo *hw)
{
	pci_iounmap(pdev, hw->db_vaddr);
	pci_iounmap(pdev, hw->ram_vaddr);
	pci_iounmap(pdev, hw->mmio_vaddr);
}

static int __devinit ilo_map_device(struct pci_dev *pdev, struct ilo_hwinfo *hw)
{
	int error = -ENOMEM;

	/* map the memory mapped i/o registers */
	hw->mmio_vaddr = pci_iomap(pdev, 1, 0);
	if (hw->mmio_vaddr == NULL) {
		dev_err(&pdev->dev, "Error mapping mmio\n");
		goto out;
	}

	/* map the adapter shared memory region */
	hw->ram_vaddr = pci_iomap(pdev, 2, MAX_CCB * ILOHW_CCB_SZ);
	if (hw->ram_vaddr == NULL) {
		dev_err(&pdev->dev, "Error mapping shared mem\n");
		goto mmio_free;
	}

	/* map the doorbell aperture */
	hw->db_vaddr = pci_iomap(pdev, 3, MAX_CCB * ONE_DB_SIZE);
	if (hw->db_vaddr == NULL) {
		dev_err(&pdev->dev, "Error mapping doorbell\n");
		goto ram_free;
	}

	return 0;
ram_free:
	pci_iounmap(pdev, hw->ram_vaddr);
mmio_free:
	pci_iounmap(pdev, hw->mmio_vaddr);
out:
	return error;
}

static void ilo_remove(struct pci_dev *pdev)
{
	int i, minor;
	struct ilo_hwinfo *ilo_hw = pci_get_drvdata(pdev);

	clear_device(ilo_hw);

	minor = MINOR(ilo_hw->cdev.dev);
	for (i = minor; i < minor + MAX_CCB; i++)
		device_destroy(ilo_class, MKDEV(ilo_major, i));

	cdev_del(&ilo_hw->cdev);
	ilo_disable_interrupts(ilo_hw);
	free_irq(pdev->irq, ilo_hw);
	ilo_unmap_device(pdev, ilo_hw);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(ilo_hw);
	ilo_hwdev[(minor / MAX_CCB)] = 0;
}

static int __devinit ilo_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	int devnum, minor, start, error;
	struct ilo_hwinfo *ilo_hw;

	/* find a free range for device files */
	for (devnum = 0; devnum < MAX_ILO_DEV; devnum++) {
		if (ilo_hwdev[devnum] == 0) {
			ilo_hwdev[devnum] = 1;
			break;
		}
	}

	if (devnum == MAX_ILO_DEV) {
		dev_err(&pdev->dev, "Error finding free device\n");
		return -ENODEV;
	}

	/* track global allocations for this device */
	error = -ENOMEM;
	ilo_hw = kzalloc(sizeof(*ilo_hw), GFP_KERNEL);
	if (!ilo_hw)
		goto out;

	ilo_hw->ilo_dev = pdev;
	spin_lock_init(&ilo_hw->alloc_lock);
	spin_lock_init(&ilo_hw->fifo_lock);
	spin_lock_init(&ilo_hw->open_lock);

	error = pci_enable_device(pdev);
	if (error)
		goto free;

	pci_set_master(pdev);

	error = pci_request_regions(pdev, ILO_NAME);
	if (error)
		goto disable;

	error = ilo_map_device(pdev, ilo_hw);
	if (error)
		goto free_regions;

	pci_set_drvdata(pdev, ilo_hw);
	clear_device(ilo_hw);

	error = request_irq(pdev->irq, ilo_isr, IRQF_SHARED, "hpilo", ilo_hw);
	if (error)
		goto unmap;

	ilo_enable_interrupts(ilo_hw);

	cdev_init(&ilo_hw->cdev, &ilo_fops);
	ilo_hw->cdev.owner = THIS_MODULE;
	start = devnum * MAX_CCB;
	error = cdev_add(&ilo_hw->cdev, MKDEV(ilo_major, start), MAX_CCB);
	if (error) {
		dev_err(&pdev->dev, "Could not add cdev\n");
		goto remove_isr;
	}

	for (minor = 0 ; minor < MAX_CCB; minor++) {
		struct device *dev;
		dev = device_create(ilo_class, &pdev->dev,
				    MKDEV(ilo_major, minor), NULL,
				    "hpilo!d%dccb%d", devnum, minor);
		if (IS_ERR(dev))
			dev_err(&pdev->dev, "Could not create files\n");
	}

	return 0;
remove_isr:
	ilo_disable_interrupts(ilo_hw);
	free_irq(pdev->irq, ilo_hw);
unmap:
	ilo_unmap_device(pdev, ilo_hw);
free_regions:
	pci_release_regions(pdev);
disable:
	pci_disable_device(pdev);
free:
	kfree(ilo_hw);
out:
	ilo_hwdev[devnum] = 0;
	return error;
}

static struct pci_device_id ilo_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPAQ, 0xB204) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, 0x3307) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ilo_devices);

static struct pci_driver ilo_driver = {
	.name 	  = ILO_NAME,
	.id_table = ilo_devices,
	.probe 	  = ilo_probe,
	.remove   = __devexit_p(ilo_remove),
};

static int __init ilo_init(void)
{
	int error;
	dev_t dev;

	ilo_class = class_create(THIS_MODULE, "iLO");
	if (IS_ERR(ilo_class)) {
		error = PTR_ERR(ilo_class);
		goto out;
	}

	error = alloc_chrdev_region(&dev, 0, MAX_OPEN, ILO_NAME);
	if (error)
		goto class_destroy;

	ilo_major = MAJOR(dev);

	error =	pci_register_driver(&ilo_driver);
	if (error)
		goto chr_remove;

	return 0;
chr_remove:
	unregister_chrdev_region(dev, MAX_OPEN);
class_destroy:
	class_destroy(ilo_class);
out:
	return error;
}

static void __exit ilo_exit(void)
{
	pci_unregister_driver(&ilo_driver);
	unregister_chrdev_region(MKDEV(ilo_major, 0), MAX_OPEN);
	class_destroy(ilo_class);
}

MODULE_VERSION("1.2");
MODULE_ALIAS(ILO_NAME);
MODULE_DESCRIPTION(ILO_NAME);
MODULE_AUTHOR("David Altobelli <david.altobelli@hp.com>");
MODULE_LICENSE("GPL v2");

module_init(ilo_init);
module_exit(ilo_exit);
