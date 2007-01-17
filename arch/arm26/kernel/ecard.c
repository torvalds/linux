/*
 *  linux/arch/arm26/kernel/ecard.c
 *
 *  Copyright 1995-2001 Russell King
 *  Copyright 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Find all installed expansion cards, and handle interrupts from them.
 *
 *  Created from information from Acorns RiscOS3 PRMs
 *  15-Jun-2003 IM      Modified from ARM32 (RiscPC capable) version
 *  10-Jan-1999	RMK	Run loaders in a simulated RISC OS environment.
 *  06-May-1997	RMK	Added blacklist for cards whose loader doesn't work.
 *  12-Sep-1997	RMK	Created new handling of interrupt enables/disables
 *			- cards can now register their own routine to control
 *			interrupts (recommended).
 *  29-Sep-1997	RMK	Expansion card interrupt hardware not being re-enabled
 *			on reset from Linux. (Caused cards not to respond
 *			under RiscOS without hard reset).
 *
 */
#define ECARD_C

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/irqchip.h>
#include <asm/tlbflush.h>

enum req {
	req_readbytes,
	req_reset
};

struct ecard_request {
	enum req	req;
	ecard_t		*ec;
	unsigned int	address;
	unsigned int	length;
	unsigned int	use_loader;
	void		*buffer;
};

struct expcard_blacklist {
	unsigned short	 manufacturer;
	unsigned short	 product;
	const char	*type;
};

static ecard_t *cards;
static ecard_t *slot_to_expcard[MAX_ECARDS];
static unsigned int ectcr;

/* List of descriptions of cards which don't have an extended
 * identification, or chunk directories containing a description.
 */
static struct expcard_blacklist __initdata blacklist[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1, "Acorn Ether1" }
};

asmlinkage extern int
ecard_loader_reset(volatile unsigned char *pa, loader_t loader);
asmlinkage extern int
ecard_loader_read(int off, volatile unsigned char *pa, loader_t loader);

static const struct ecard_id *
ecard_match_device(const struct ecard_id *ids, struct expansion_card *ec);

static inline unsigned short
ecard_getu16(unsigned char *v)
{
	return v[0] | v[1] << 8;
}

static inline signed long
ecard_gets24(unsigned char *v)
{
	return v[0] | v[1] << 8 | v[2] << 16 | ((v[2] & 0x80) ? 0xff000000 : 0);
}

static inline ecard_t *
slot_to_ecard(unsigned int slot)
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
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)
#define POD_INT_ADDR(x)	((volatile unsigned char *)\
			 ((BUS_ADDR((x)) - IO_BASE) + IO_START))

static inline void ecard_task_reset(struct ecard_request *req)
{
	struct expansion_card *ec = req->ec;
	if (ec->loader)
		ecard_loader_reset(POD_INT_ADDR(ec->podaddr), ec->loader);
}

static void
ecard_task_readbytes(struct ecard_request *req)
{
	unsigned char *buf = (unsigned char *)req->buffer;
	volatile unsigned char *base_addr =
		(volatile unsigned char *)POD_INT_ADDR(req->ec->podaddr);
	unsigned int len = req->length;
	unsigned int off = req->address;

	if (!req->use_loader || !req->ec->loader) {
		off *= 4;
		while (len--) {
			*buf++ = base_addr[off];
			off += 4;
		}
	} else {
		while(len--) {
			/*
			 * The following is required by some
			 * expansion card loader programs.
			 */
			*(unsigned long *)0x108 = 0;
			*buf++ = ecard_loader_read(off++, base_addr,
						   req->ec->loader);
		}
	}
}

static void ecard_do_request(struct ecard_request *req)
{
	switch (req->req) {
	case req_readbytes:
		ecard_task_readbytes(req);
		break;

	case req_reset:
		ecard_task_reset(req);
		break;
	}
}

/*
 * On 26-bit processors, we don't need the kcardd thread to access the
 * expansion card loaders.  We do it directly.
 */
#define ecard_call(req)	ecard_do_request(req)

/* ======================= Mid-level card control ===================== */

static void
ecard_readbytes(void *addr, ecard_t *ec, int off, int len, int useld)
{
	struct ecard_request req;

	req.req		= req_readbytes;
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
	return !ec->irqmask || ec->irqaddr[0] & ec->irqmask;
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
	return !ec->fiqmask || ec->fiqaddr[0] & ec->fiqmask;
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
static void ecard_irq_unmask(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->irqenable)
			ec->ops->irqenable(ec, irqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable IRQs for %d\n", irqnr);
	}
}

static void ecard_irq_mask(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops && ec->ops->irqdisable)
			ec->ops->irqdisable(ec, irqnr);
	}
}

static struct irqchip ecard_chip = {
	.ack	= ecard_irq_mask,
	.mask	= ecard_irq_mask,
	.unmask = ecard_irq_unmask,
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

static void
ecard_dump_irq_state(ecard_t *ec)
{
	printk("  %d: %sclaimed, ",
	       ec->slot_no,
	       ec->claimed ? "" : "not ");

	if (ec->ops && ec->ops->irqpending &&
	    ec->ops != &ecard_default_ops)
		printk("irq %spending\n",
		       ec->ops->irqpending(ec) ? "" : "not ");
	else
		printk("irqaddr %p, mask = %02X, status = %02X\n",
		       ec->irqaddr, ec->irqmask, *ec->irqaddr);
}

static void ecard_check_lockup(struct irqdesc *desc)
{
	static int last, lockup;
	ecard_t *ec;

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

			desc->chip->mask(IRQ_EXPANSIONCARD);

			printk("Expansion card IRQ state:\n");

			for (ec = cards; ec; ec = ec->next)
				ecard_dump_irq_state(ec);
		}
	} else
		lockup = 0;

	/*
	 * If we did not recognise the source of this interrupt,
	 * warn the user, but don't flood the user with these messages.
	 */
	if (!last || time_after(jiffies, (unsigned long)(last + 5*HZ))) {
		last = jiffies;
		printk(KERN_WARNING "Unrecognised interrupt from backplane\n");
	}
}

static void
ecard_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	ecard_t *ec;
	int called = 0;

	desc->chip->mask(irq);
	for (ec = cards; ec; ec = ec->next) {
		int pending;

		if (!ec->claimed || ec->irq == NO_IRQ)
			continue;

		if (ec->ops && ec->ops->irqpending)
			pending = ec->ops->irqpending(ec);
		else
			pending = ecard_default_ops.irqpending(ec);

		if (pending) {
			struct irqdesc *d = irq_desc + ec->irq;
			d->handle(ec->irq, d, regs);
			called ++;
		}
	}
	desc->chip->unmask(irq);

	if (called == 0)
		ecard_check_lockup(desc);
}

#define ecard_irqexp_handler NULL
#define ecard_probeirqhw() (0)

unsigned int ecard_address(ecard_t *ec, card_type_t type, card_speed_t speed)
{
	unsigned long address = 0;
	int slot = ec->slot_no;

	ectcr &= ~(1 << slot);

	switch (type) {
	case ECARD_MEMC:
		address = IO_EC_MEMC_BASE + (slot << 12);
		break;

	case ECARD_IOC:
		address = IO_EC_IOC_BASE + (slot << 12) + (speed << 17);
		break;

	default:
		break;
	}

	return address;
}

static int ecard_prints(char *buffer, ecard_t *ec)
{
	char *start = buffer;

	buffer += sprintf(buffer, "  %d: ", ec->slot_no);

	if (ec->cid.id == 0) {
		struct in_chunk_dir incd;

		buffer += sprintf(buffer, "[%04X:%04X] ",
			ec->cid.manufacturer, ec->cid.product);

		if (!ec->card_desc && ec->cid.cd &&
		    ecard_readchunk(&incd, ec, 0xf5, 0)) {
			ec->card_desc = kmalloc(strlen(incd.d.string)+1, GFP_KERNEL);

			if (ec->card_desc)
				strcpy((char *)ec->card_desc, incd.d.string);
		}

		buffer += sprintf(buffer, "%s\n", ec->card_desc ? ec->card_desc : "*unknown*");
	} else
		buffer += sprintf(buffer, "Simple card %d\n", ec->cid.id);

	return buffer - start;
}

static int get_ecard_dev_info(char *buf, char **start, off_t pos, int count)
{
	ecard_t *ec = cards;
	off_t at = 0;
	int len, cnt;

	cnt = 0;
	while (ec && count > cnt) {
		len = ecard_prints(buf, ec);
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		ec = ec->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_ecard_dir = NULL;

static void ecard_proc_init(void)
{
	proc_bus_ecard_dir = proc_mkdir("ecard", proc_bus);
	create_proc_info_entry("devices", 0, proc_bus_ecard_dir,
		get_ecard_dev_info);
}

#define ec_set_resource(ec,nr,st,sz,flg)			\
	do {							\
		(ec)->resource[nr].name = ec->dev.bus_id;	\
		(ec)->resource[nr].start = st;			\
		(ec)->resource[nr].end = (st) + (sz) - 1;	\
		(ec)->resource[nr].flags = flg;			\
	} while (0)

static void __init ecard_init_resources(struct expansion_card *ec)
{
	unsigned long base = PODSLOT_IOC0_BASE;
	unsigned int slot = ec->slot_no;
	int i;

	ec_set_resource(ec, ECARD_RES_MEMC,
			PODSLOT_MEMC_BASE + (slot << 14),
			PODSLOT_MEMC_SIZE, IORESOURCE_MEM);

	for (i = 0; i < ECARD_RES_IOCSYNC - ECARD_RES_IOCSLOW; i++) {
		ec_set_resource(ec, i + ECARD_RES_IOCSLOW,
				base + (slot << 14) + (i << 19),
				PODSLOT_IOC_SIZE, IORESOURCE_MEM);
	}

	for (i = 0; i < ECARD_NUM_RESOURCES; i++) {
		if (ec->resource[i].start &&
		    request_resource(&iomem_resource, &ec->resource[i])) {
			printk(KERN_ERR "%s: resource(s) not available\n",
				ec->dev.bus_id);
			ec->resource[i].end -= ec->resource[i].start;
			ec->resource[i].start = 0;
		}
	}
}

static ssize_t ecard_show_irq(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->irq);
}

static ssize_t ecard_show_vendor(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->cid.manufacturer);
}

static ssize_t ecard_show_device(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->cid.product);
}

static ssize_t ecard_show_dma(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->dma);
}

static ssize_t ecard_show_resources(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	char *str = buf;
	int i;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++)
		str += sprintf(str, "%08lx %08lx %08lx\n",
				ec->resource[i].start,
				ec->resource[i].end,
				ec->resource[i].flags);

	return str - buf;
}

static DEVICE_ATTR(irq, S_IRUGO, ecard_show_irq, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, ecard_show_vendor, NULL);
static DEVICE_ATTR(device, S_IRUGO, ecard_show_device, NULL);
static DEVICE_ATTR(dma, S_IRUGO, ecard_show_dma, NULL);
static DEVICE_ATTR(resource, S_IRUGO, ecard_show_resources, NULL);

/*
 * Probe for an expansion card.
 *
 * If bit 1 of the first byte of the card is set, then the
 * card does not exist.
 */
static int __init
ecard_probe(int slot, card_type_t type)
{
	ecard_t **ecp;
	ecard_t *ec;
	struct ex_ecid cid;
	int i, rc = -ENOMEM;

	ec = kzalloc(sizeof(ecard_t), GFP_KERNEL);
	if (!ec)
		goto nomem;

	ec->slot_no	= slot;
	ec->type        = type;
	ec->irq		= NO_IRQ;
	ec->fiq		= NO_IRQ;
	ec->dma		= NO_DMA;
	ec->card_desc	= NULL;
	ec->ops		= &ecard_default_ops;

	rc = -ENODEV;
	if ((ec->podaddr = ecard_address(ec, type, ECARD_SYNC)) == 0)
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
	ec->irqaddr	= (unsigned char *)ioaddr(ec->podaddr);

	if (ec->cid.is) {
		ec->irqmask = ec->cid.irqmask;
		ec->irqaddr += ec->cid.irqoff;
		ec->fiqmask = ec->cid.fiqmask;
		ec->fiqaddr += ec->cid.fiqoff;
	} else {
		ec->irqmask = 1;
		ec->fiqmask = 4;
	}

	for (i = 0; i < sizeof(blacklist) / sizeof(*blacklist); i++)
		if (blacklist[i].manufacturer == ec->cid.manufacturer &&
		    blacklist[i].product == ec->cid.product) {
			ec->card_desc = blacklist[i].type;
			break;
		}

	snprintf(ec->dev.bus_id, sizeof(ec->dev.bus_id), "ecard%d", slot);
	ec->dev.parent = NULL;
	ec->dev.bus    = &ecard_bus_type;
	ec->dev.dma_mask = &ec->dma_mask;
	ec->dma_mask = (u64)0xffffffff;

	ecard_init_resources(ec);

	/*
	 * hook the interrupt handlers
	 */
	ec->irq = 32 + slot;
	set_irq_chip(ec->irq, &ecard_chip);
	set_irq_handler(ec->irq, do_level_IRQ);
	set_irq_flags(ec->irq, IRQF_VALID);

	for (ecp = &cards; *ecp; ecp = &(*ecp)->next);

	*ecp = ec;
	slot_to_expcard[slot] = ec;

	device_register(&ec->dev);
	device_create_file(&ec->dev, &dev_attr_dma);
	device_create_file(&ec->dev, &dev_attr_irq);
	device_create_file(&ec->dev, &dev_attr_resource);
	device_create_file(&ec->dev, &dev_attr_vendor);
	device_create_file(&ec->dev, &dev_attr_device); 

	return 0;

nodev:
	kfree(ec);
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
	int slot, irqhw;

	printk("Probing expansion cards\n");

	for (slot = 0; slot < MAX_ECARDS; slot ++) {
		ecard_probe(slot, ECARD_IOC);
	}

	irqhw = ecard_probeirqhw();

	set_irq_chained_handler(IRQ_EXPANSIONCARD,
				irqhw ? ecard_irqexp_handler : ecard_irq_handler);

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

	ecard_claim(ec);
	ret = drv->probe(ec, id);
	if (ret)
		ecard_release(ec);
	return ret;
}

static int ecard_drv_remove(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);

	drv->remove(ec);
	ecard_release(ec);

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

	if (drv->shutdown)
		drv->shutdown(ec);
	ecard_release(ec);
	req.req = req_reset;
	req.ec = ec;
	ecard_call(&req);
}

int ecard_register_driver(struct ecard_driver *drv)
{
	drv->drv.bus = &ecard_bus_type;
	drv->drv.probe = ecard_drv_probe;
	drv->drv.remove = ecard_drv_remove;
	drv->drv.shutdown = ecard_drv_shutdown;

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
	.name	= "ecard",
	.match	= ecard_match,
};

static int ecard_bus_init(void)
{
	return bus_register(&ecard_bus_type);
}

postcore_initcall(ecard_bus_init);

EXPORT_SYMBOL(ecard_readchunk);
EXPORT_SYMBOL(ecard_address);
EXPORT_SYMBOL(ecard_register_driver);
EXPORT_SYMBOL(ecard_remove_driver);
EXPORT_SYMBOL(ecard_bus_type);
