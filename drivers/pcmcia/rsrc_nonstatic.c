/*
 * rsrc_nonstatic.c -- Resource management routines for !SS_CAP_STATIC_MAP sockets
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/device.h>

#include <asm/irq.h>
#include <asm/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

MODULE_AUTHOR("David A. Hinds, Dominik Brodowski");
MODULE_LICENSE("GPL");

/* Parameters that can be set with 'insmod' */

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0444)

INT_MODULE_PARM(probe_mem,	1);		/* memory probe? */
#ifdef CONFIG_PCMCIA_PROBE
INT_MODULE_PARM(probe_io,	1);		/* IO port probe? */
INT_MODULE_PARM(mem_limit,	0x10000);
#endif

/* for io_db and mem_db */
struct resource_map {
	u_long			base, num;
	struct resource_map	*next;
};

struct socket_data {
	struct resource_map		mem_db;
	struct resource_map		io_db;
	unsigned int			rsrc_mem_probe;
};

static DEFINE_MUTEX(rsrc_mutex);
#define MEM_PROBE_LOW	(1 << 0)
#define MEM_PROBE_HIGH	(1 << 1)


/*======================================================================

    Linux resource management extensions

======================================================================*/

static struct resource *
make_resource(resource_size_t b, resource_size_t n, int flags, const char *name)
{
	struct resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		res->name = name;
		res->start = b;
		res->end = b + n - 1;
		res->flags = flags;
	}
	return res;
}

static struct resource *
claim_region(struct pcmcia_socket *s, resource_size_t base,
		resource_size_t size, int type, char *name)
{
	struct resource *res, *parent;

	parent = type & IORESOURCE_MEM ? &iomem_resource : &ioport_resource;
	res = make_resource(base, size, type | IORESOURCE_BUSY, name);

	if (res) {
#ifdef CONFIG_PCI
		if (s && s->cb_dev)
			parent = pci_find_parent_resource(s->cb_dev, res);
#endif
		if (!parent || request_resource(parent, res)) {
			kfree(res);
			res = NULL;
		}
	}
	return res;
}

static void free_region(struct resource *res)
{
	if (res) {
		release_resource(res);
		kfree(res);
	}
}

/*======================================================================

    These manage the internal databases of available resources.

======================================================================*/

static int add_interval(struct resource_map *map, u_long base, u_long num)
{
	struct resource_map *p, *q;

	for (p = map; ; p = p->next) {
		if ((p != map) && (p->base+p->num-1 >= base))
			return -1;
		if ((p->next == map) || (p->next->base > base+num-1))
			break;
	}
	q = kmalloc(sizeof(struct resource_map), GFP_KERNEL);
	if (!q) {
		printk(KERN_WARNING "out of memory to update resources\n");
		return -ENOMEM;
	}
	q->base = base; q->num = num;
	q->next = p->next; p->next = q;
	return 0;
}

/*====================================================================*/

static int sub_interval(struct resource_map *map, u_long base, u_long num)
{
    struct resource_map *p, *q;

    for (p = map; ; p = q) {
	q = p->next;
	if (q == map)
	    break;
	if ((q->base+q->num > base) && (base+num > q->base)) {
	    if (q->base >= base) {
		if (q->base+q->num <= base+num) {
		    /* Delete whole block */
		    p->next = q->next;
		    kfree(q);
		    /* don't advance the pointer yet */
		    q = p;
		} else {
		    /* Cut off bit from the front */
		    q->num = q->base + q->num - base - num;
		    q->base = base + num;
		}
	    } else if (q->base+q->num <= base+num) {
		/* Cut off bit from the end */
		q->num = base - q->base;
	    } else {
		/* Split the block into two pieces */
		p = kmalloc(sizeof(struct resource_map), GFP_KERNEL);
		if (!p) {
		    printk(KERN_WARNING "out of memory to update resources\n");
		    return -ENOMEM;
		}
		p->base = base+num;
		p->num = q->base+q->num - p->base;
		q->num = base - q->base;
		p->next = q->next ; q->next = p;
	    }
	}
    }
    return 0;
}

/*======================================================================

    These routines examine a region of IO or memory addresses to
    determine what ranges might be genuinely available.

======================================================================*/

#ifdef CONFIG_PCMCIA_PROBE
static void do_io_probe(struct pcmcia_socket *s, unsigned int base,
			unsigned int num)
{
    struct resource *res;
    struct socket_data *s_data = s->resource_data;
    unsigned int i, j, bad;
    int any;
    u_char *b, hole, most;

    dev_printk(KERN_INFO, &s->dev, "cs: IO port probe %#x-%#x:",
	       base, base+num-1);

    /* First, what does a floating port look like? */
    b = kzalloc(256, GFP_KERNEL);
    if (!b) {
	    printk("\n");
	    dev_printk(KERN_ERR, &s->dev,
		   "do_io_probe: unable to kmalloc 256 bytes");
            return;
    }
    for (i = base, most = 0; i < base+num; i += 8) {
	res = claim_region(NULL, i, 8, IORESOURCE_IO, "PCMCIA IO probe");
	if (!res)
	    continue;
	hole = inb(i);
	for (j = 1; j < 8; j++)
	    if (inb(i+j) != hole) break;
	free_region(res);
	if ((j == 8) && (++b[hole] > b[most]))
	    most = hole;
	if (b[most] == 127) break;
    }
    kfree(b);

    bad = any = 0;
    for (i = base; i < base+num; i += 8) {
	res = claim_region(NULL, i, 8, IORESOURCE_IO, "PCMCIA IO probe");
	if (!res)
	    continue;
	for (j = 0; j < 8; j++)
	    if (inb(i+j) != most) break;
	free_region(res);
	if (j < 8) {
	    if (!any)
		printk(" excluding");
	    if (!bad)
		bad = any = i;
	} else {
	    if (bad) {
		sub_interval(&s_data->io_db, bad, i-bad);
		printk(" %#x-%#x", bad, i-1);
		bad = 0;
	    }
	}
    }
    if (bad) {
	if ((num > 16) && (bad == base) && (i == base+num)) {
	    printk(" nothing: probe failed.\n");
	    return;
	} else {
	    sub_interval(&s_data->io_db, bad, i-bad);
	    printk(" %#x-%#x", bad, i-1);
	}
    }

    printk(any ? "\n" : " clean.\n");
}
#endif

/*======================================================================

    This is tricky... when we set up CIS memory, we try to validate
    the memory window space allocations.

======================================================================*/

/* Validation function for cards with a valid CIS */
static int readable(struct pcmcia_socket *s, struct resource *res,
		    unsigned int *count)
{
	int ret = -1;

	s->cis_mem.res = res;
	s->cis_virt = ioremap(res->start, s->map_size);
	if (s->cis_virt) {
		ret = pccard_validate_cis(s, count);
		/* invalidate mapping and CIS cache */
		iounmap(s->cis_virt);
		s->cis_virt = NULL;
		destroy_cis_cache(s);
	}
	s->cis_mem.res = NULL;
	if ((ret != 0) || (*count == 0))
		return 0;
	return 1;
}

/* Validation function for simple memory cards */
static int checksum(struct pcmcia_socket *s, struct resource *res)
{
	pccard_mem_map map;
	int i, a = 0, b = -1, d;
	void __iomem *virt;

	virt = ioremap(res->start, s->map_size);
	if (virt) {
		map.map = 0;
		map.flags = MAP_ACTIVE;
		map.speed = 0;
		map.res = res;
		map.card_start = 0;
		s->ops->set_mem_map(s, &map);

		/* Don't bother checking every word... */
		for (i = 0; i < s->map_size; i += 44) {
			d = readl(virt+i);
			a += d;
			b &= d;
		}

		map.flags = 0;
		s->ops->set_mem_map(s, &map);

		iounmap(virt);
	}

	return (b == -1) ? -1 : (a>>1);
}

static int
cis_readable(struct pcmcia_socket *s, unsigned long base, unsigned long size)
{
	struct resource *res1, *res2;
	unsigned int info1, info2;
	int ret = 0;

	res1 = claim_region(s, base, size/2, IORESOURCE_MEM, "cs memory probe");
	res2 = claim_region(s, base + size/2, size/2, IORESOURCE_MEM, "cs memory probe");

	if (res1 && res2) {
		ret = readable(s, res1, &info1);
		ret += readable(s, res2, &info2);
	}

	free_region(res2);
	free_region(res1);

	return (ret == 2) && (info1 == info2);
}

static int
checksum_match(struct pcmcia_socket *s, unsigned long base, unsigned long size)
{
	struct resource *res1, *res2;
	int a = -1, b = -1;

	res1 = claim_region(s, base, size/2, IORESOURCE_MEM, "cs memory probe");
	res2 = claim_region(s, base + size/2, size/2, IORESOURCE_MEM, "cs memory probe");

	if (res1 && res2) {
		a = checksum(s, res1);
		b = checksum(s, res2);
	}

	free_region(res2);
	free_region(res1);

	return (a == b) && (a >= 0);
}

/*======================================================================

    The memory probe.  If the memory list includes a 64K-aligned block
    below 1MB, we probe in 64K chunks, and as soon as we accumulate at
    least mem_limit free space, we quit.

======================================================================*/

static int do_mem_probe(u_long base, u_long num, struct pcmcia_socket *s)
{
    struct socket_data *s_data = s->resource_data;
    u_long i, j, bad, fail, step;

    dev_printk(KERN_INFO, &s->dev, "cs: memory probe 0x%06lx-0x%06lx:",
	       base, base+num-1);
    bad = fail = 0;
    step = (num < 0x20000) ? 0x2000 : ((num>>4) & ~0x1fff);
    /* don't allow too large steps */
    if (step > 0x800000)
	step = 0x800000;
    /* cis_readable wants to map 2x map_size */
    if (step < 2 * s->map_size)
	step = 2 * s->map_size;
    for (i = j = base; i < base+num; i = j + step) {
	if (!fail) {
	    for (j = i; j < base+num; j += step) {
		if (cis_readable(s, j, step))
		    break;
	    }
	    fail = ((i == base) && (j == base+num));
	}
	if (fail) {
	    for (j = i; j < base+num; j += 2*step)
		if (checksum_match(s, j, step) &&
		    checksum_match(s, j + step, step))
		    break;
	}
	if (i != j) {
	    if (!bad) printk(" excluding");
	    printk(" %#05lx-%#05lx", i, j-1);
	    sub_interval(&s_data->mem_db, i, j-i);
	    bad += j-i;
	}
    }
    printk(bad ? "\n" : " clean.\n");
    return (num - bad);
}

#ifdef CONFIG_PCMCIA_PROBE

static u_long inv_probe(struct resource_map *m, struct pcmcia_socket *s)
{
	struct socket_data *s_data = s->resource_data;
	u_long ok;
	if (m == &s_data->mem_db)
		return 0;
	ok = inv_probe(m->next, s);
	if (ok) {
		if (m->base >= 0x100000)
			sub_interval(&s_data->mem_db, m->base, m->num);
		return ok;
	}
	if (m->base < 0x100000)
		return 0;
	return do_mem_probe(m->base, m->num, s);
}

static int validate_mem(struct pcmcia_socket *s, unsigned int probe_mask)
{
	struct resource_map *m, mm;
	static unsigned char order[] = { 0xd0, 0xe0, 0xc0, 0xf0 };
	unsigned long b, i, ok = 0;
	struct socket_data *s_data = s->resource_data;

	/* We do up to four passes through the list */
	if (probe_mask & MEM_PROBE_HIGH) {
		if (inv_probe(s_data->mem_db.next, s) > 0)
			return 0;
		dev_printk(KERN_NOTICE, &s->dev,
			   "cs: warning: no high memory space available!\n");
		return -ENODEV;
	}

	for (m = s_data->mem_db.next; m != &s_data->mem_db; m = mm.next) {
		mm = *m;
		/* Only probe < 1 MB */
		if (mm.base >= 0x100000)
			continue;
		if ((mm.base | mm.num) & 0xffff) {
			ok += do_mem_probe(mm.base, mm.num, s);
			continue;
		}
		/* Special probe for 64K-aligned block */
		for (i = 0; i < 4; i++) {
			b = order[i] << 12;
			if ((b >= mm.base) && (b+0x10000 <= mm.base+mm.num)) {
				if (ok >= mem_limit)
					sub_interval(&s_data->mem_db, b, 0x10000);
				else
					ok += do_mem_probe(b, 0x10000, s);
			}
		}
	}

	if (ok > 0)
		return 0;

	return -ENODEV;
}

#else /* CONFIG_PCMCIA_PROBE */

static int validate_mem(struct pcmcia_socket *s, unsigned int probe_mask)
{
	struct resource_map *m, mm;
	struct socket_data *s_data = s->resource_data;
	unsigned long ok = 0;

	for (m = s_data->mem_db.next; m != &s_data->mem_db; m = mm.next) {
		mm = *m;
		ok += do_mem_probe(mm.base, mm.num, s);
	}
	if (ok > 0)
		return 0;
	return -ENODEV;
}

#endif /* CONFIG_PCMCIA_PROBE */


/*
 * Locking note: Must be called with skt_mutex held!
 */
static int pcmcia_nonstatic_validate_mem(struct pcmcia_socket *s)
{
	struct socket_data *s_data = s->resource_data;
	unsigned int probe_mask = MEM_PROBE_LOW;
	int ret = 0;

	if (!probe_mem)
		return 0;

	mutex_lock(&rsrc_mutex);

	if (s->features & SS_CAP_PAGE_REGS)
		probe_mask = MEM_PROBE_HIGH;

	if (probe_mask & ~s_data->rsrc_mem_probe) {
		if (s->state & SOCKET_PRESENT)
			ret = validate_mem(s, probe_mask);
		if (!ret)
			s_data->rsrc_mem_probe |= probe_mask;
	}

	mutex_unlock(&rsrc_mutex);

	return ret;
}

struct pcmcia_align_data {
	unsigned long	mask;
	unsigned long	offset;
	struct resource_map	*map;
};

static void
pcmcia_common_align(void *align_data, struct resource *res,
			resource_size_t size, resource_size_t align)
{
	struct pcmcia_align_data *data = align_data;
	resource_size_t start;
	/*
	 * Ensure that we have the correct start address
	 */
	start = (res->start & ~data->mask) + data->offset;
	if (start < res->start)
		start += data->mask + 1;
	res->start = start;
}

static void
pcmcia_align(void *align_data, struct resource *res, resource_size_t size,
		resource_size_t align)
{
	struct pcmcia_align_data *data = align_data;
	struct resource_map *m;

	pcmcia_common_align(data, res, size, align);

	for (m = data->map->next; m != data->map; m = m->next) {
		unsigned long start = m->base;
		unsigned long end = m->base + m->num - 1;

		/*
		 * If the lower resources are not available, try aligning
		 * to this entry of the resource database to see if it'll
		 * fit here.
		 */
		if (res->start < start) {
			res->start = start;
			pcmcia_common_align(data, res, size, align);
		}

		/*
		 * If we're above the area which was passed in, there's
		 * no point proceeding.
		 */
		if (res->start >= res->end)
			break;

		if ((res->start + size - 1) <= end)
			break;
	}

	/*
	 * If we failed to find something suitable, ensure we fail.
	 */
	if (m == data->map)
		res->start = res->end;
}

/*
 * Adjust an existing IO region allocation, but making sure that we don't
 * encroach outside the resources which the user supplied.
 */
static int nonstatic_adjust_io_region(struct resource *res, unsigned long r_start,
				      unsigned long r_end, struct pcmcia_socket *s)
{
	struct resource_map *m;
	struct socket_data *s_data = s->resource_data;
	int ret = -ENOMEM;

	mutex_lock(&rsrc_mutex);
	for (m = s_data->io_db.next; m != &s_data->io_db; m = m->next) {
		unsigned long start = m->base;
		unsigned long end = m->base + m->num - 1;

		if (start > r_start || r_end > end)
			continue;

		ret = adjust_resource(res, r_start, r_end - r_start + 1);
		break;
	}
	mutex_unlock(&rsrc_mutex);

	return ret;
}

/*======================================================================

    These find ranges of I/O ports or memory addresses that are not
    currently allocated by other devices.

    The 'align' field should reflect the number of bits of address
    that need to be preserved from the initial value of *base.  It
    should be a power of two, greater than or equal to 'num'.  A value
    of 0 means that all bits of *base are significant.  *base should
    also be strictly less than 'align'.

======================================================================*/

static struct resource *nonstatic_find_io_region(unsigned long base, int num,
		   unsigned long align, struct pcmcia_socket *s)
{
	struct resource *res = make_resource(0, num, IORESOURCE_IO, dev_name(&s->dev));
	struct socket_data *s_data = s->resource_data;
	struct pcmcia_align_data data;
	unsigned long min = base;
	int ret;

	if (align == 0)
		align = 0x10000;

	data.mask = align - 1;
	data.offset = base & data.mask;
	data.map = &s_data->io_db;

	mutex_lock(&rsrc_mutex);
#ifdef CONFIG_PCI
	if (s->cb_dev) {
		ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num, 1,
					     min, 0, pcmcia_align, &data);
	} else
#endif
		ret = allocate_resource(&ioport_resource, res, num, min, ~0UL,
					1, pcmcia_align, &data);
	mutex_unlock(&rsrc_mutex);

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}

static struct resource * nonstatic_find_mem_region(u_long base, u_long num,
		u_long align, int low, struct pcmcia_socket *s)
{
	struct resource *res = make_resource(0, num, IORESOURCE_MEM, dev_name(&s->dev));
	struct socket_data *s_data = s->resource_data;
	struct pcmcia_align_data data;
	unsigned long min, max;
	int ret, i;

	low = low || !(s->features & SS_CAP_PAGE_REGS);

	data.mask = align - 1;
	data.offset = base & data.mask;
	data.map = &s_data->mem_db;

	for (i = 0; i < 2; i++) {
		if (low) {
			max = 0x100000UL;
			min = base < max ? base : 0;
		} else {
			max = ~0UL;
			min = 0x100000UL + base;
		}

		mutex_lock(&rsrc_mutex);
#ifdef CONFIG_PCI
		if (s->cb_dev) {
			ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num,
						     1, min, 0,
						     pcmcia_align, &data);
		} else
#endif
			ret = allocate_resource(&iomem_resource, res, num, min,
						max, 1, pcmcia_align, &data);
		mutex_unlock(&rsrc_mutex);
		if (ret == 0 || low)
			break;
		low = 1;
	}

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}


static int adjust_memory(struct pcmcia_socket *s, unsigned int action, unsigned long start, unsigned long end)
{
	struct socket_data *data = s->resource_data;
	unsigned long size = end - start + 1;
	int ret = 0;

	if (end < start)
		return -EINVAL;

	mutex_lock(&rsrc_mutex);
	switch (action) {
	case ADD_MANAGED_RESOURCE:
		ret = add_interval(&data->mem_db, start, size);
		break;
	case REMOVE_MANAGED_RESOURCE:
		ret = sub_interval(&data->mem_db, start, size);
		if (!ret) {
			struct pcmcia_socket *socket;
			down_read(&pcmcia_socket_list_rwsem);
			list_for_each_entry(socket, &pcmcia_socket_list, socket_list)
				release_cis_mem(socket);
			up_read(&pcmcia_socket_list_rwsem);
		}
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&rsrc_mutex);

	return ret;
}


static int adjust_io(struct pcmcia_socket *s, unsigned int action, unsigned long start, unsigned long end)
{
	struct socket_data *data = s->resource_data;
	unsigned long size = end - start + 1;
	int ret = 0;

	if (end < start)
		return -EINVAL;

	if (end > IO_SPACE_LIMIT)
		return -EINVAL;

	mutex_lock(&rsrc_mutex);
	switch (action) {
	case ADD_MANAGED_RESOURCE:
		if (add_interval(&data->io_db, start, size) != 0) {
			ret = -EBUSY;
			break;
		}
#ifdef CONFIG_PCMCIA_PROBE
		if (probe_io)
			do_io_probe(s, start, size);
#endif
		break;
	case REMOVE_MANAGED_RESOURCE:
		sub_interval(&data->io_db, start, size);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&rsrc_mutex);

	return ret;
}


#ifdef CONFIG_PCI
static int nonstatic_autoadd_resources(struct pcmcia_socket *s)
{
	struct resource *res;
	int i, done = 0;

	if (!s->cb_dev || !s->cb_dev->bus)
		return -ENODEV;

#if defined(CONFIG_X86)
	/* If this is the root bus, the risk of hitting
	 * some strange system devices which aren't protected
	 * by either ACPI resource tables or properly requested
	 * resources is too big. Therefore, don't do auto-adding
	 * of resources at the moment.
	 */
	if (s->cb_dev->bus->number == 0)
		return -EINVAL;
#endif

	for (i=0; i < PCI_BUS_NUM_RESOURCES; i++) {
		res = s->cb_dev->bus->resource[i];
		if (!res)
			continue;

		if (res->flags & IORESOURCE_IO) {
			if (res == &ioport_resource)
				continue;
			dev_printk(KERN_INFO, &s->cb_dev->dev,
				   "pcmcia: parent PCI bridge I/O "
				   "window: 0x%llx - 0x%llx\n",
				   (unsigned long long)res->start,
				   (unsigned long long)res->end);
			if (!adjust_io(s, ADD_MANAGED_RESOURCE, res->start, res->end))
				done |= IORESOURCE_IO;

		}

		if (res->flags & IORESOURCE_MEM) {
			if (res == &iomem_resource)
				continue;
			dev_printk(KERN_INFO, &s->cb_dev->dev,
				   "pcmcia: parent PCI bridge Memory "
				   "window: 0x%llx - 0x%llx\n",
				   (unsigned long long)res->start,
				   (unsigned long long)res->end);
			if (!adjust_memory(s, ADD_MANAGED_RESOURCE, res->start, res->end))
				done |= IORESOURCE_MEM;
		}
	}

	/* if we got at least one of IO, and one of MEM, we can be glad and
	 * activate the PCMCIA subsystem */
	if (done == (IORESOURCE_MEM | IORESOURCE_IO))
		s->resource_setup_done = 1;

	return 0;
}

#else

static inline int nonstatic_autoadd_resources(struct pcmcia_socket *s)
{
	return -ENODEV;
}

#endif


static int nonstatic_init(struct pcmcia_socket *s)
{
	struct socket_data *data;

	data = kzalloc(sizeof(struct socket_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->mem_db.next = &data->mem_db;
	data->io_db.next = &data->io_db;

	s->resource_data = (void *) data;

	nonstatic_autoadd_resources(s);

	return 0;
}

static void nonstatic_release_resource_db(struct pcmcia_socket *s)
{
	struct socket_data *data = s->resource_data;
	struct resource_map *p, *q;

	mutex_lock(&rsrc_mutex);
	for (p = data->mem_db.next; p != &data->mem_db; p = q) {
		q = p->next;
		kfree(p);
	}
	for (p = data->io_db.next; p != &data->io_db; p = q) {
		q = p->next;
		kfree(p);
	}
	mutex_unlock(&rsrc_mutex);
}


struct pccard_resource_ops pccard_nonstatic_ops = {
	.validate_mem = pcmcia_nonstatic_validate_mem,
	.adjust_io_region = nonstatic_adjust_io_region,
	.find_io = nonstatic_find_io_region,
	.find_mem = nonstatic_find_mem_region,
	.add_io = adjust_io,
	.add_mem = adjust_memory,
	.init = nonstatic_init,
	.exit = nonstatic_release_resource_db,
};
EXPORT_SYMBOL(pccard_nonstatic_ops);


/* sysfs interface to the resource database */

static ssize_t show_io_db(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	struct socket_data *data;
	struct resource_map *p;
	ssize_t ret = 0;

	mutex_lock(&rsrc_mutex);
	data = s->resource_data;

	for (p = data->io_db.next; p != &data->io_db; p = p->next) {
		if (ret > (PAGE_SIZE - 10))
			continue;
		ret += snprintf (&buf[ret], (PAGE_SIZE - ret - 1),
				 "0x%08lx - 0x%08lx\n",
				 ((unsigned long) p->base),
				 ((unsigned long) p->base + p->num - 1));
	}

	mutex_unlock(&rsrc_mutex);
	return (ret);
}

static ssize_t store_io_db(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	unsigned long start_addr, end_addr;
	unsigned int add = ADD_MANAGED_RESOURCE;
	ssize_t ret = 0;

	ret = sscanf (buf, "+ 0x%lx - 0x%lx", &start_addr, &end_addr);
	if (ret != 2) {
		ret = sscanf (buf, "- 0x%lx - 0x%lx", &start_addr, &end_addr);
		add = REMOVE_MANAGED_RESOURCE;
		if (ret != 2) {
			ret = sscanf (buf, "0x%lx - 0x%lx", &start_addr, &end_addr);
			add = ADD_MANAGED_RESOURCE;
			if (ret != 2)
				return -EINVAL;
		}
	}
	if (end_addr < start_addr)
		return -EINVAL;

	ret = adjust_io(s, add, start_addr, end_addr);
	if (!ret)
		s->resource_setup_new = 1;

	return ret ? ret : count;
}
static DEVICE_ATTR(available_resources_io, 0600, show_io_db, store_io_db);

static ssize_t show_mem_db(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	struct socket_data *data;
	struct resource_map *p;
	ssize_t ret = 0;

	mutex_lock(&rsrc_mutex);
	data = s->resource_data;

	for (p = data->mem_db.next; p != &data->mem_db; p = p->next) {
		if (ret > (PAGE_SIZE - 10))
			continue;
		ret += snprintf (&buf[ret], (PAGE_SIZE - ret - 1),
				 "0x%08lx - 0x%08lx\n",
				 ((unsigned long) p->base),
				 ((unsigned long) p->base + p->num - 1));
	}

	mutex_unlock(&rsrc_mutex);
	return (ret);
}

static ssize_t store_mem_db(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	unsigned long start_addr, end_addr;
	unsigned int add = ADD_MANAGED_RESOURCE;
	ssize_t ret = 0;

	ret = sscanf (buf, "+ 0x%lx - 0x%lx", &start_addr, &end_addr);
	if (ret != 2) {
		ret = sscanf (buf, "- 0x%lx - 0x%lx", &start_addr, &end_addr);
		add = REMOVE_MANAGED_RESOURCE;
		if (ret != 2) {
			ret = sscanf (buf, "0x%lx - 0x%lx", &start_addr, &end_addr);
			add = ADD_MANAGED_RESOURCE;
			if (ret != 2)
				return -EINVAL;
		}
	}
	if (end_addr < start_addr)
		return -EINVAL;

	ret = adjust_memory(s, add, start_addr, end_addr);
	if (!ret)
		s->resource_setup_new = 1;

	return ret ? ret : count;
}
static DEVICE_ATTR(available_resources_mem, 0600, show_mem_db, store_mem_db);

static struct attribute *pccard_rsrc_attributes[] = {
	&dev_attr_available_resources_io.attr,
	&dev_attr_available_resources_mem.attr,
	NULL,
};

static const struct attribute_group rsrc_attributes = {
	.attrs = pccard_rsrc_attributes,
};

static int __devinit pccard_sysfs_add_rsrc(struct device *dev,
					   struct class_interface *class_intf)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);

	if (s->resource_ops != &pccard_nonstatic_ops)
		return 0;
	return sysfs_create_group(&dev->kobj, &rsrc_attributes);
}

static void __devexit pccard_sysfs_remove_rsrc(struct device *dev,
					       struct class_interface *class_intf)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);

	if (s->resource_ops != &pccard_nonstatic_ops)
		return;
	sysfs_remove_group(&dev->kobj, &rsrc_attributes);
}

static struct class_interface pccard_rsrc_interface __refdata = {
	.class = &pcmcia_socket_class,
	.add_dev = &pccard_sysfs_add_rsrc,
	.remove_dev = __devexit_p(&pccard_sysfs_remove_rsrc),
};

static int __init nonstatic_sysfs_init(void)
{
	return class_interface_register(&pccard_rsrc_interface);
}

static void __exit nonstatic_sysfs_exit(void)
{
	class_interface_unregister(&pccard_rsrc_interface);
}

module_init(nonstatic_sysfs_init);
module_exit(nonstatic_sysfs_exit);
