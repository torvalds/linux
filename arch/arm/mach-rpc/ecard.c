/*
 *  linux/arch/arm/kernel/ecard.c
 *
 *  Copyright 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Find all installed expansion cards, and handle interrupts from them.
 *
 *  Created from information from Acorns RiscOS3 PRMs
 *
 *  08-Dec-1996	RMK	Added code for the 9'th expansion card - the ether
 *			podule slot.
 *  06-May-1997	RMK	Added blacklist for cards whose loader doesn't work.
 *  12-Sep-1997	RMK	Created new handling of interrupt enables/disables
 *			- cards can now register their own routine to control
 *			interrupts (recommended).
 *  29-Sep-1997	RMK	Expansion card interrupt hardware not being re-enabled
 *			on reset from Linux. (Caused cards not to respond
 *			under RiscOS without hard reset).
 *  15-Feb-1998	RMK	Added DMA support
 *  12-Sep-1998	RMK	Added EASI support
 *  10-Jan-1999	RMK	Run loaders in a simulated RISC OS environment.
 *  17-Apr-1999	RMK	Support for EASI Type C cycles.
 */
#define ECARD_C

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/mach/irq.h>
#include <asm/tlbflush.h>

#include "ecard.h"

struct ecard_request {
	void		(*fn)(struct ecard_request *);
	ecard_t		*ec;
	unsigned int	address;
	unsigned int	length;
	unsigned int	use_loader;
	void		*buffer;
	struct completion *complete;
};

struct expcard_blacklist {
	unsigned short	 manufacturer;
	unsigned short	 product;
	const char	*type;
	void (*init)(ecard_t *ec);
};

static ecard_t *cards;
static ecard_t *slot_to_expcard[MAX_ECARDS];
static unsigned int ectcr;

static void atomwide_3p_quirk(ecard_t *ec);

/* List of descriptions of cards which don't have an extended
 * identification, or chunk directories containing a description.
 */
static struct expcard_blacklist __initdata blacklist[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1, "Acorn Ether1" },
	{ MANU_ATOMWIDE, PROD_ATOMWIDE_3PSERIAL, NULL, atomwide_3p_quirk },
};

asmlinkage extern int
ecard_loader_reset(unsigned long base, loader_t loader);
asmlinkage extern int
ecard_loader_read(int off, unsigned long base, loader_t loader);

static inline unsigned short ecard_getu16(unsigned char *v)
{
	return v[0] | v[1] << 8;
}

static inline signed long ecard_gets24(unsigned char *v)
{
	return v[0] | v[1] << 8 | v[2] << 16 | ((v[2] & 0x80) ? 0xff000000 : 0);
}

static inline ecard_t *slot_to_ecard(unsigned int slot)
{
	return slot < MAX_ECARDS ? slot_to_expcard[slot] : NULL;
}

/* ===================== Expansion card daemon ======================== */
/*
 * Since the loader programs on the expansion cards need to be run
 * in a specific environment, create a separate task with this
 * environment up, and pass requests to this task as and when we
 * need to.
 *
 * This should allow 99% of loaders to be called from Linux.
 *
 * From a security standpoint, we trust the card vendors.  This
 * may be a misplaced trust.
 */
static void ecard_task_reset(struct ecard_request *req)
{
	struct expansion_card *ec = req->ec;
	struct resource *res;

	res = ec->slot_no == 8
		? &ec->resource[ECARD_RES_MEMC]
		: ec->easi
		  ? &ec->resource[ECARD_RES_EASI]
		  : &ec->resource[ECARD_RES_IOCSYNC];

	ecard_loader_reset(res->start, ec->loader);
}

static void ecard_task_readbytes(struct ecard_request *req)
{
	struct expansion_card *ec = req->ec;
	unsigned char *buf = req->buffer;
	unsigned int len = req->length;
	unsigned int off = req->address;

	if (ec->slot_no == 8) {
		void __iomem *base = (void __iomem *)
				ec->resource[ECARD_RES_MEMC].start;

		/*
		 * The card maintains an index which increments the address
		 * into a 4096-byte page on each access.  We need to keep
		 * track of the counter.
		 */
		static unsigned int index;
		unsigned int page;

		page = (off >> 12) * 4;
		if (page > 256 * 4)
			return;

		off &= 4095;

		/*
		 * If we are reading offset 0, or our current index is
		 * greater than the offset, reset the hardware index counter.
		 */
		if (off == 0 || index > off) {
			writeb(0, base);
			index = 0;
		}

		/*
		 * Increment the hardware index counter until we get to the
		 * required offset.  The read bytes are discarded.
		 */
		while (index < off) {
			readb(base + page);
			index += 1;
		}

		while (len--) {
			*buf++ = readb(base + page);
			index += 1;
		}
	} else {
		unsigned long base = (ec->easi
			 ? &ec->resource[ECARD_RES_EASI]
			 : &ec->resource[ECARD_RES_IOCSYNC])->start;
		void __iomem *pbase = (void __iomem *)base;

		if (!req->use_loader || !ec->loader) {
			off *= 4;
			while (len--) {
				*buf++ = readb(pbase + off);
				off += 4;
			}
		} else {
			while(len--) {
				/*
				 * The following is required by some
				 * expansion card loader programs.
				 */
				*(unsigned long *)0x108 = 0;
				*buf++ = ecard_loader_read(off++, base,
							   ec->loader);
			}
		}
	}

}

static DECLARE_WAIT_QUEUE_HEAD(ecard_wait);
static struct ecard_request *ecard_req;
static DEFINE_MUTEX(ecard_mutex);

/*
 * Set up the expansion card daemon's page tables.
 */
static void ecard_init_pgtables(struct mm_struct *mm)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, VM_EXEC);

	/* We want to set up the page tables for the following mapping:
	 *  Virtual	Physical
	 *  0x03000000	0x03000000
	 *  0x03010000	unmapped
	 *  0x03210000	0x03210000
	 *  0x03400000	unmapped
	 *  0x08000000	0x08000000
	 *  0x10000000	unmapped
	 *
	 * FIXME: we don't follow this 100% yet.
	 */
	pgd_t *src_pgd, *dst_pgd;

	src_pgd = pgd_offset(mm, (unsigned long)IO_BASE);
	dst_pgd = pgd_offset(mm, IO_START);

	memcpy(dst_pgd, src_pgd, sizeof(pgd_t) * (IO_SIZE / PGDIR_SIZE));

	src_pgd = pgd_offset(mm, (unsigned long)EASI_BASE);
	dst_pgd = pgd_offset(mm, EASI_START);

	memcpy(dst_pgd, src_pgd, sizeof(pgd_t) * (EASI_SIZE / PGDIR_SIZE));

	flush_tlb_range(&vma, IO_START, IO_START + IO_SIZE);
	flush_tlb_range(&vma, EASI_START, EASI_START + EASI_SIZE);
}

static int ecard_init_mm(void)
{
	struct mm_struct * mm = mm_alloc();
	struct mm_struct *active_mm = current->active_mm;

	if (!mm)
		return -ENOMEM;

	current->mm = mm;
	current->active_mm = mm;
	activate_mm(active_mm, mm);
	mmdrop(active_mm);
	ecard_init_pgtables(mm);
	return 0;
}

static int
ecard_task(void * unused)
{
	/*
	 * Allocate a mm.  We're not a lazy-TLB kernel task since we need
	 * to set page table entries where the user space would be.  Note
	 * that this also creates the page tables.  Failure is not an
	 * option here.
	 */
	if (ecard_init_mm())
		panic("kecardd: unable to alloc mm\n");

	while (1) {
		struct ecard_request *req;

		wait_event_interruptible(ecard_wait, ecard_req != NULL);

		req = xchg(&ecard_req, NULL);
		if (req != NULL) {
			req->fn(req);
			complete(req->complete);
		}
	}
}

/*
 * Wake the expansion card daemon to action our request.
 *
 * FIXME: The test here is not sufficient to detect if the
 * kcardd is running.
 */
static void ecard_call(struct ecard_request *req)
{
	DECLARE_COMPLETION_ONSTACK(completion);

	req->complete = &completion;

	mutex_lock(&ecard_mutex);
	ecard_req = req;
	wake_up(&ecard_wait);

	/*
	 * Now wait for kecardd to run.
	 */
	wait_for_completion(&completion);
	mutex_unlock(&ecard_mutex);
}

/* ======================= Mid-level card control ===================== */

static void
ecard_readbytes(void *addr, ecard_t *ec, int off, int len, int useld)
{
	struct ecard_request req;

	req.fn		= ecard_task_readbytes;
	req.ec		= ec;
	req.address	= off;
	req.length	= len;
	req.use_loader	= useld;
	req.buffer	= addr;

	ecard_call(&req);
}

int ecard_readchunk(struct in_chunk_dir *cd, ecard_t *ec, int id, int num)
{
	struct ex_chunk_dir excd;
	int index = 16;
	int useld = 0;

	if (!ec->cid.cd)
		return 0;

	while(1) {
		ecard_readbytes(&excd, ec, index, 8, useld);
		index += 8;
		if (c_id(&excd) == 0) {
			if (!useld && ec->loader) {
				useld = 1;
				index = 0;
				continue;
			}
			return 0;
		}
		if (c_id(&excd) == 0xf0) { /* link */
			index = c_start(&excd);
			continue;
		}
		if (c_id(&excd) == 0x80) { /* loader */
			if (!ec->loader) {
				ec->loader = kmalloc(c_len(&excd),
							       GFP_KERNEL);
				if (ec->loader)
					ecard_readbytes(ec->loader, ec,
							(int)c_start(&excd),
							c_len(&excd), useld);
				else
					return 0;
			}
			continue;
		}
		if (c_id(&excd) == id && num-- == 0)
			break;
	}

	if (c_id(&excd) & 0x80) {
		switch (c_id(&excd) & 0x70) {
		case 0x70:
			ecard_readbytes((unsigned char *)excd.d.string, ec,
					(int)c_start(&excd), c_len(&excd),
					useld);
			break;
		case 0x00:
			break;
		}
	}
	cd->start_offset = c_start(&excd);
	memcpy(cd->d.string, excd.d.string, 256);
	return 1;
}

/* ======================= Interrupt control ============================ */

static void ecard_def_irq_enable(ecard_t *ec, int irqnr)
{
}

static void ecard_def_irq_disable(ecard_t *ec, int irqnr)
{
}

static int ecard_def_irq_pending(ecard_t *ec)
{
	return !ec->irqmask || readb(ec->irqaddr) & ec->irqmask;
}

static void ecard_def_fiq_enable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_enable called - impossible");
}

static void ecard_def_fiq_disable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_disable called - impossible");
}

static int ecard_def_fiq_pending(ecard_t *ec)
{
	return !ec->fiqmask || readb(ec->fiqaddr) & ec->fiqmask;
}

static expansioncard_ops_t ecard_default_ops = {
	ecard_def_irq_enable,
	ecard_def_irq_disable,
	ecard_def_irq_pending,
	ecard_def_fiq_enable,
	ecard_def_fiq_disable,
	ecard_def_fiq_pending
};

/*
 * Enable and disable interrupts from expansion cards.
 * (interrupts are disabled for these functions).
 *
 * They are not meant to be called directly, but via enable/disable_irq.
 */
static void ecard_irq_unmask(struct irq_data *d)
{
	ecard_t *ec = irq_data_get_irq_chip_data(d);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->irqenable)
			ec->ops->irqenable(ec, d->irq);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable IRQs for %d\n", d->irq);
	}
}

static void ecard_irq_mask(struct irq_data *d)
{
	ecard_t *ec = irq_data_get_irq_chip_data(d);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops && ec->ops->irqdisable)
			ec->ops->irqdisable(ec, d->irq);
	}
}

static struct irq_chip ecard_chip = {
	.name		= "ECARD",
	.irq_ack	= ecard_irq_mask,
	.irq_mask	= ecard_irq_mask,
	.irq_unmask	= ecard_irq_unmask,
};

void ecard_enablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->fiqenable)
			ec->ops->fiqenable(ec, fiqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable FIQs for %d\n", fiqnr);
	}
}

void ecard_disablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops->fiqdisable)
			ec->ops->fiqdisable(ec, fiqnr);
	}
}

static void ecard_dump_irq_state(void)
{
	ecard_t *ec;

	printk("Expansion card IRQ state:\n");

	for (ec = cards; ec; ec = ec->next) {
		const char *claimed;

		if (ec->slot_no == 8)
			continue;

		claimed = ec->claimed ? "" : "not ";

		if (ec->ops && ec->ops->irqpending &&
		    ec->ops != &ecard_default_ops)
			printk("  %d: %sclaimed irq %spending\n",
			       ec->slot_no, claimed,
			       ec->ops->irqpending(ec) ? "" : "not ");
		else
			printk("  %d: %sclaimed irqaddr %p, mask = %02X, status = %02X\n",
			       ec->slot_no, claimed,
			       ec->irqaddr, ec->irqmask, readb(ec->irqaddr));
	}
}

static void ecard_check_lockup(struct irq_desc *desc)
{
	static unsigned long last;
	static int lockup;

	/*
	 * If the timer interrupt has not run since the last million
	 * unrecognised expansion card interrupts, then there is
	 * something seriously wrong.  Disable the expansion card
	 * interrupts so at least we can continue.
	 *
	 * Maybe we ought to start a timer to re-enable them some time
	 * later?
	 */
	if (last == jiffies) {
		lockup += 1;
		if (lockup > 1000000) {
			printk(KERN_ERR "\nInterrupt lockup detected - "
			       "disabling all expansion card interrupts\n");

			desc->irq_data.chip->irq_mask(&desc->irq_data);
			ecard_dump_irq_state();
		}
	} else
		lockup = 0;

	/*
	 * If we did not recognise the source of this interrupt,
	 * warn the user, but don't flood the user with these messages.
	 */
	if (!last || time_after(jiffies, last + 5*HZ)) {
		last = jiffies;
		printk(KERN_WARNING "Unrecognised interrupt from backplane\n");
		ecard_dump_irq_state();
	}
}

static void ecard_irq_handler(struct irq_desc *desc)
{
	ecard_t *ec;
	int called = 0;

	desc->irq_data.chip->irq_mask(&desc->irq_data);
	for (ec = cards; ec; ec = ec->next) {
		int pending;

		if (!ec->claimed || !ec->irq || ec->slot_no == 8)
			continue;

		if (ec->ops && ec->ops->irqpending)
			pending = ec->ops->irqpending(ec);
		else
			pending = ecard_default_ops.irqpending(ec);

		if (pending) {
			generic_handle_irq(ec->irq);
			called ++;
		}
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);

	if (called == 0)
		ecard_check_lockup(desc);
}

static void __iomem *__ecard_address(ecard_t *ec, card_type_t type, card_speed_t speed)
{
	void __iomem *address = NULL;
	int slot = ec->slot_no;

	if (ec->slot_no == 8)
		return ECARD_MEMC8_BASE;

	ectcr &= ~(1 << slot);

	switch (type) {
	case ECARD_MEMC:
		if (slot < 4)
			address = ECARD_MEMC_BASE + (slot << 14);
		break;

	case ECARD_IOC:
		if (slot < 4)
			address = ECARD_IOC_BASE + (slot << 14);
		else
			address = ECARD_IOC4_BASE + ((slot - 4) << 14);
		if (address)
			address += speed << 19;
		break;

	case ECARD_EASI:
		address = ECARD_EASI_BASE + (slot << 24);
		if (speed == ECARD_FAST)
			ectcr |= 1 << slot;
		break;

	default:
		break;
	}

#ifdef IOMD_ECTCR
	iomd_writeb(ectcr, IOMD_ECTCR);
#endif
	return address;
}

static int ecard_prints(struct seq_file *m, ecard_t *ec)
{
	seq_printf(m, "  %d: %s ", ec->slot_no, ec->easi ? "EASI" : "    ");

	if (ec->cid.id == 0) {
		struct in_chunk_dir incd;

		seq_printf(m, "[%04X:%04X] ",
			ec->cid.manufacturer, ec->cid.product);

		if (!ec->card_desc && ec->cid.cd &&
		    ecard_readchunk(&incd, ec, 0xf5, 0)) {
			ec->card_desc = kmalloc(strlen(incd.d.string)+1, GFP_KERNEL);

			if (ec->card_desc)
				strcpy((char *)ec->card_desc, incd.d.string);
		}

		seq_printf(m, "%s\n", ec->card_desc ? ec->card_desc : "*unknown*");
	} else
		seq_printf(m, "Simple card %d\n", ec->cid.id);

	return 0;
}

static int ecard_devices_proc_show(struct seq_file *m, void *v)
{
	ecard_t *ec = cards;

	while (ec) {
		ecard_prints(m, ec);
		ec = ec->next;
	}
	return 0;
}

static struct proc_dir_entry *proc_bus_ecard_dir = NULL;

static void ecard_proc_init(void)
{
	proc_bus_ecard_dir = proc_mkdir("bus/ecard", NULL);
	proc_create_single("devices", 0, proc_bus_ecard_dir,
			ecard_devices_proc_show);
}

#define ec_set_resource(ec,nr,st,sz)				\
	do {							\
		(ec)->resource[nr].name = dev_name(&ec->dev);	\
		(ec)->resource[nr].start = st;			\
		(ec)->resource[nr].end = (st) + (sz) - 1;	\
		(ec)->resource[nr].flags = IORESOURCE_MEM;	\
	} while (0)

static void __init ecard_free_card(struct expansion_card *ec)
{
	int i;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++)
		if (ec->resource[i].flags)
			release_resource(&ec->resource[i]);

	kfree(ec);
}

static struct expansion_card *__init ecard_alloc_card(int type, int slot)
{
	struct expansion_card *ec;
	unsigned long base;
	int i;

	ec = kzalloc(sizeof(ecard_t), GFP_KERNEL);
	if (!ec) {
		ec = ERR_PTR(-ENOMEM);
		goto nomem;
	}

	ec->slot_no = slot;
	ec->easi = type == ECARD_EASI;
	ec->irq = 0;
	ec->fiq = 0;
	ec->dma = NO_DMA;
	ec->ops = &ecard_default_ops;

	dev_set_name(&ec->dev, "ecard%d", slot);
	ec->dev.parent = NULL;
	ec->dev.bus = &ecard_bus_type;
	ec->dev.dma_mask = &ec->dma_mask;
	ec->dma_mask = (u64)0xffffffff;
	ec->dev.coherent_dma_mask = ec->dma_mask;

	if (slot < 4) {
		ec_set_resource(ec, ECARD_RES_MEMC,
				PODSLOT_MEMC_BASE + (slot << 14),
				PODSLOT_MEMC_SIZE);
		base = PODSLOT_IOC0_BASE + (slot << 14);
	} else
		base = PODSLOT_IOC4_BASE + ((slot - 4) << 14);

#ifdef CONFIG_ARCH_RPC
	if (slot < 8) {
		ec_set_resource(ec, ECARD_RES_EASI,
				PODSLOT_EASI_BASE + (slot << 24),
				PODSLOT_EASI_SIZE);
	}

	if (slot == 8) {
		ec_set_resource(ec, ECARD_RES_MEMC, NETSLOT_BASE, NETSLOT_SIZE);
	} else
#endif

	for (i = 0; i <= ECARD_RES_IOCSYNC - ECARD_RES_IOCSLOW; i++)
		ec_set_resource(ec, i + ECARD_RES_IOCSLOW,
				base + (i << 19), PODSLOT_IOC_SIZE);

	for (i = 0; i < ECARD_NUM_RESOURCES; i++) {
		if (ec->resource[i].flags &&
		    request_resource(&iomem_resource, &ec->resource[i])) {
			dev_err(&ec->dev, "resource(s) not available\n");
			ec->resource[i].end -= ec->resource[i].start;
			ec->resource[i].start = 0;
			ec->resource[i].flags = 0;
		}
	}

 nomem:
	return ec;
}

static ssize_t irq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->irq);
}
static DEVICE_ATTR_RO(irq);

static ssize_t dma_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->dma);
}
static DEVICE_ATTR_RO(dma);

static ssize_t resource_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	char *str = buf;
	int i;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++)
		str += sprintf(str, "%08x %08x %08lx\n",
				ec->resource[i].start,
				ec->resource[i].end,
				ec->resource[i].flags);

	return str - buf;
}
static DEVICE_ATTR_RO(resource);

static ssize_t vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->cid.manufacturer);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t device_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->cid.product);
}
static DEVICE_ATTR_RO(device);

static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%s\n", ec->easi ? "EASI" : "IOC");
}
static DEVICE_ATTR_RO(type);

static struct attribute *ecard_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_dma.attr,
	&dev_attr_irq.attr,
	&dev_attr_resource.attr,
	&dev_attr_type.attr,
	&dev_attr_vendor.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ecard_dev);

int ecard_request_resources(struct expansion_card *ec)
{
	int i, err = 0;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++) {
		if (ecard_resource_end(ec, i) &&
		    !request_mem_region(ecard_resource_start(ec, i),
					ecard_resource_len(ec, i),
					ec->dev.driver->name)) {
			err = -EBUSY;
			break;
		}
	}

	if (err) {
		while (i--)
			if (ecard_resource_end(ec, i))
				release_mem_region(ecard_resource_start(ec, i),
						   ecard_resource_len(ec, i));
	}
	return err;
}
EXPORT_SYMBOL(ecard_request_resources);

void ecard_release_resources(struct expansion_card *ec)
{
	int i;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++)
		if (ecard_resource_end(ec, i))
			release_mem_region(ecard_resource_start(ec, i),
					   ecard_resource_len(ec, i));
}
EXPORT_SYMBOL(ecard_release_resources);

void ecard_setirq(struct expansion_card *ec, const struct expansion_card_ops *ops, void *irq_data)
{
	ec->irq_data = irq_data;
	barrier();
	ec->ops = ops;
}
EXPORT_SYMBOL(ecard_setirq);

void __iomem *ecardm_iomap(struct expansion_card *ec, unsigned int res,
			   unsigned long offset, unsigned long maxsize)
{
	unsigned long start = ecard_resource_start(ec, res);
	unsigned long end = ecard_resource_end(ec, res);

	if (offset > (end - start))
		return NULL;

	start += offset;
	if (maxsize && end - start > maxsize)
		end = start + maxsize;
	
	return devm_ioremap(&ec->dev, start, end - start);
}
EXPORT_SYMBOL(ecardm_iomap);

static void atomwide_3p_quirk(ecard_t *ec)
{
	void __iomem *addr = __ecard_address(ec, ECARD_IOC, ECARD_SYNC);
	unsigned int i;

	/* Disable interrupts on each port */
	for (i = 0x2000; i <= 0x2800; i += 0x0400)
		writeb(0, addr + i + 4);	
}

/*
 * Probe for an expansion card.
 *
 * If bit 1 of the first byte of the card is set, then the
 * card does not exist.
 */
static int __init ecard_probe(int slot, unsigned irq, card_type_t type)
{
	ecard_t **ecp;
	ecard_t *ec;
	struct ex_ecid cid;
	void __iomem *addr;
	int i, rc;

	ec = ecard_alloc_card(type, slot);
	if (IS_ERR(ec)) {
		rc = PTR_ERR(ec);
		goto nomem;
	}

	rc = -ENODEV;
	if ((addr = __ecard_address(ec, type, ECARD_SYNC)) == NULL)
		goto nodev;

	cid.r_zero = 1;
	ecard_readbytes(&cid, ec, 0, 16, 0);
	if (cid.r_zero)
		goto nodev;

	ec->cid.id	= cid.r_id;
	ec->cid.cd	= cid.r_cd;
	ec->cid.is	= cid.r_is;
	ec->cid.w	= cid.r_w;
	ec->cid.manufacturer = ecard_getu16(cid.r_manu);
	ec->cid.product = ecard_getu16(cid.r_prod);
	ec->cid.country = cid.r_country;
	ec->cid.irqmask = cid.r_irqmask;
	ec->cid.irqoff  = ecard_gets24(cid.r_irqoff);
	ec->cid.fiqmask = cid.r_fiqmask;
	ec->cid.fiqoff  = ecard_gets24(cid.r_fiqoff);
	ec->fiqaddr	=
	ec->irqaddr	= addr;

	if (ec->cid.is) {
		ec->irqmask = ec->cid.irqmask;
		ec->irqaddr += ec->cid.irqoff;
		ec->fiqmask = ec->cid.fiqmask;
		ec->fiqaddr += ec->cid.fiqoff;
	} else {
		ec->irqmask = 1;
		ec->fiqmask = 4;
	}

	for (i = 0; i < ARRAY_SIZE(blacklist); i++)
		if (blacklist[i].manufacturer == ec->cid.manufacturer &&
		    blacklist[i].product == ec->cid.product) {
		    	if (blacklist[i].type)
				ec->card_desc = blacklist[i].type;
			if (blacklist[i].init)
				blacklist[i].init(ec);
			break;
		}

	ec->irq = irq;

	/*
	 * hook the interrupt handlers
	 */
	if (slot < 8) {
		irq_set_chip_and_handler(ec->irq, &ecard_chip,
					 handle_level_irq);
		irq_set_chip_data(ec->irq, ec);
		irq_clear_status_flags(ec->irq, IRQ_NOREQUEST);
	}

#ifdef CONFIG_ARCH_RPC
	/* On RiscPC, only first two slots have DMA capability */
	if (slot < 2)
		ec->dma = 2 + slot;
#endif

	for (ecp = &cards; *ecp; ecp = &(*ecp)->next);

	*ecp = ec;
	slot_to_expcard[slot] = ec;

	rc = device_register(&ec->dev);
	if (rc)
		goto nodev;

	return 0;

 nodev:
	ecard_free_card(ec);
 nomem:
	return rc;
}

/*
 * Initialise the expansion card system.
 * Locate all hardware - interrupt management and
 * actual cards.
 */
static int __init ecard_init(void)
{
	struct task_struct *task;
	int slot, irqbase;

	irqbase = irq_alloc_descs(-1, 0, 8, -1);
	if (irqbase < 0)
		return irqbase;

	task = kthread_run(ecard_task, NULL, "kecardd");
	if (IS_ERR(task)) {
		printk(KERN_ERR "Ecard: unable to create kernel thread: %ld\n",
		       PTR_ERR(task));
		irq_free_descs(irqbase, 8);
		return PTR_ERR(task);
	}

	printk("Probing expansion cards\n");

	for (slot = 0; slot < 8; slot ++) {
		if (ecard_probe(slot, irqbase + slot, ECARD_EASI) == -ENODEV)
			ecard_probe(slot, irqbase + slot, ECARD_IOC);
	}

	ecard_probe(8, 11, ECARD_IOC);

	irq_set_chained_handler(IRQ_EXPANSIONCARD, ecard_irq_handler);

	ecard_proc_init();

	return 0;
}

subsys_initcall(ecard_init);

/*
 *	ECARD "bus"
 */
static const struct ecard_id *
ecard_match_device(const struct ecard_id *ids, struct expansion_card *ec)
{
	int i;

	for (i = 0; ids[i].manufacturer != 65535; i++)
		if (ec->cid.manufacturer == ids[i].manufacturer &&
		    ec->cid.product == ids[i].product)
			return ids + i;

	return NULL;
}

static int ecard_drv_probe(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);
	const struct ecard_id *id;
	int ret;

	id = ecard_match_device(drv->id_table, ec);

	ec->claimed = 1;
	ret = drv->probe(ec, id);
	if (ret)
		ec->claimed = 0;
	return ret;
}

static int ecard_drv_remove(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);

	drv->remove(ec);
	ec->claimed = 0;

	/*
	 * Restore the default operations.  We ensure that the
	 * ops are set before we change the data.
	 */
	ec->ops = &ecard_default_ops;
	barrier();
	ec->irq_data = NULL;

	return 0;
}

/*
 * Before rebooting, we must make sure that the expansion card is in a
 * sensible state, so it can be re-detected.  This means that the first
 * page of the ROM must be visible.  We call the expansion cards reset
 * handler, if any.
 */
static void ecard_drv_shutdown(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);
	struct ecard_request req;

	if (dev->driver) {
		if (drv->shutdown)
			drv->shutdown(ec);
		ec->claimed = 0;
	}

	/*
	 * If this card has a loader, call the reset handler.
	 */
	if (ec->loader) {
		req.fn = ecard_task_reset;
		req.ec = ec;
		ecard_call(&req);
	}
}

int ecard_register_driver(struct ecard_driver *drv)
{
	drv->drv.bus = &ecard_bus_type;

	return driver_register(&drv->drv);
}

void ecard_remove_driver(struct ecard_driver *drv)
{
	driver_unregister(&drv->drv);
}

static int ecard_match(struct device *_dev, struct device_driver *_drv)
{
	struct expansion_card *ec = ECARD_DEV(_dev);
	struct ecard_driver *drv = ECARD_DRV(_drv);
	int ret;

	if (drv->id_table) {
		ret = ecard_match_device(drv->id_table, ec) != NULL;
	} else {
		ret = ec->cid.id == drv->id;
	}

	return ret;
}

struct bus_type ecard_bus_type = {
	.name		= "ecard",
	.dev_groups	= ecard_dev_groups,
	.match		= ecard_match,
	.probe		= ecard_drv_probe,
	.remove		= ecard_drv_remove,
	.shutdown	= ecard_drv_shutdown,
};

static int ecard_bus_init(void)
{
	return bus_register(&ecard_bus_type);
}

postcore_initcall(ecard_bus_init);

EXPORT_SYMBOL(ecard_readchunk);
EXPORT_SYMBOL(ecard_register_driver);
EXPORT_SYMBOL(ecard_remove_driver);
EXPORT_SYMBOL(ecard_bus_type);
