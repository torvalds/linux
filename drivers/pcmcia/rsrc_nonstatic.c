// SPDX-License-Identifier: GPL-2.0-only
/*
 * rsrc_nonstatic.c -- Resource management routines for !SS_CAP_STATIC_MAP sockets
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
#include <linux/io.h>

#include <asm/irq.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

/* moved to rsrc_mgr.c
MODULE_AUTHOR("David A. Hinds, Dominik Brodowski");
MODULE_LICENSE("GPL");
*/

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
	struct resource_map		mem_db_valid;
	struct resource_map		io_db;
};

#define MEM_PROBE_LOW	(1 << 0)
#define MEM_PROBE_HIGH	(1 << 1)

/* Action field */
#define REMOVE_MANAGED_RESOURCE		1
#define ADD_MANAGED_RESOURCE		2

/*======================================================================

    Linux resource management extensions

======================================================================*/

static struct resource *
claim_region(struct pcmcia_socket *s, resource_size_t base,
		resource_size_t size, int type, char *name)
{
	struct resource *res, *parent;

	parent = type & IORESOURCE_MEM ? &iomem_resource : &ioport_resource;
	res = pcmcia_make_resource(base, size, type | IORESOURCE_BUSY, name);

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
		if ((p != map) && (p->base+p->num >= base)) {
			p->num = max(num + base - p->base, p->num);
			return 0;
		}
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
				p = kmalloc(sizeof(struct resource_map),
					GFP_KERNEL);
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

	dev_info(&s->dev, "cs: IO port probe %#x-%#x:", base, base+num-1);

	/* First, what does a floating port look like? */
	b = kzalloc(256, GFP_KERNEL);
	if (!b) {
		pr_cont("\n");
		dev_err(&s->dev, "do_io_probe: unable to kmalloc 256 bytes\n");
		return;
	}
	for (i = base, most = 0; i < base+num; i += 8) {
		res = claim_region(s, i, 8, IORESOURCE_IO, "PCMCIA ioprobe");
		if (!res)
			continue;
		hole = inb(i);
		for (j = 1; j < 8; j++)
			if (inb(i+j) != hole)
				break;
		free_region(res);
		if ((j == 8) && (++b[hole] > b[most]))
			most = hole;
		if (b[most] == 127)
			break;
	}
	kfree(b);

	bad = any = 0;
	for (i = base; i < base+num; i += 8) {
		res = claim_region(s, i, 8, IORESOURCE_IO, "PCMCIA ioprobe");
		if (!res) {
			if (!any)
				pr_cont(" excluding");
			if (!bad)
				bad = any = i;
			continue;
		}
		for (j = 0; j < 8; j++)
			if (inb(i+j) != most)
				break;
		free_region(res);
		if (j < 8) {
			if (!any)
				pr_cont(" excluding");
			if (!bad)
				bad = any = i;
		} else {
			if (bad) {
				sub_interval(&s_data->io_db, bad, i-bad);
				pr_cont(" %#x-%#x", bad, i-1);
				bad = 0;
			}
		}
	}
	if (bad) {
		if ((num > 16) && (bad == base) && (i == base+num)) {
			sub_interval(&s_data->io_db, bad, i-bad);
			pr_cont(" nothing: probe failed.\n");
			return;
		} else {
			sub_interval(&s_data->io_db, bad, i-bad);
			pr_cont(" %#x-%#x", bad, i-1);
		}
	}

	pr_cont("%s\n", !any ? " clean" : "");
}
#endif

/*======================================================================*/

/*
 * readable() - iomem validation function for cards with a valid CIS
 */
static int readable(struct pcmcia_socket *s, struct resource *res,
		    unsigned int *count)
{
	int ret = -EINVAL;

	if (s->fake_cis) {
		dev_dbg(&s->dev, "fake CIS is being used: can't validate mem\n");
		return 0;
	}

	s->cis_mem.res = res;
	s->cis_virt = ioremap(res->start, s->map_size);
	if (s->cis_virt) {
		mutex_unlock(&s->ops_mutex);
		/* as we're only called from pcmcia.c, we're safe */
		if (s->callback->validate)
			ret = s->callback->validate(s, count);
		/* invalidate mapping */
		mutex_lock(&s->ops_mutex);
		iounmap(s->cis_virt);
		s->cis_virt = NULL;
	}
	s->cis_mem.res = NULL;
	if ((ret) || (*count == 0))
		return -EINVAL;
	return 0;
}

/*
 * checksum() - iomem validation function for simple memory cards
 */
static int checksum(struct pcmcia_socket *s, struct resource *res,
		    unsigned int *value)
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

	if (b == -1)
		return -EINVAL;

	*value = a;

	return 0;
}

/**
 * do_validate_mem() - low level validate a memory region for PCMCIA use
 * @s:		PCMCIA socket to validate
 * @base:	start address of resource to check
 * @size:	size of resource to check
 * @validate:	validation function to use
 *
 * do_validate_mem() splits up the memory region which is to be checked
 * into two parts. Both are passed to the @validate() function. If
 * @validate() returns non-zero, or the value parameter to @validate()
 * is zero, or the value parameter is different between both calls,
 * the check fails, and -EINVAL is returned. Else, 0 is returned.
 */
static int do_validate_mem(struct pcmcia_socket *s,
			   unsigned long base, unsigned long size,
			   int (*validate)(struct pcmcia_socket *s,
					   struct resource *res,
					   unsigned int *value))
{
	struct socket_data *s_data = s->resource_data;
	struct resource *res1, *res2;
	unsigned int info1 = 1, info2 = 1;
	int ret = -EINVAL;

	res1 = claim_region(s, base, size/2, IORESOURCE_MEM, "PCMCIA memprobe");
	res2 = claim_region(s, base + size/2, size/2, IORESOURCE_MEM,
			"PCMCIA memprobe");

	if (res1 && res2) {
		ret = 0;
		if (validate) {
			ret = validate(s, res1, &info1);
			ret += validate(s, res2, &info2);
		}
	}

	dev_dbg(&s->dev, "cs: memory probe 0x%06lx-0x%06lx: %pr %pr %u %u %u",
		base, base+size-1, res1, res2, ret, info1, info2);

	free_region(res2);
	free_region(res1);

	if ((ret) || (info1 != info2) || (info1 == 0))
		return -EINVAL;

	if (validate && !s->fake_cis) {
		/* move it to the validated data set */
		add_interval(&s_data->mem_db_valid, base, size);
		sub_interval(&s_data->mem_db, base, size);
	}

	return 0;
}


/**
 * do_mem_probe() - validate a memory region for PCMCIA use
 * @s:		PCMCIA socket to validate
 * @base:	start address of resource to check
 * @num:	size of resource to check
 * @validate:	validation function to use
 * @fallback:	validation function to use if validate fails
 *
 * do_mem_probe() checks a memory region for use by the PCMCIA subsystem.
 * To do so, the area is split up into sensible parts, and then passed
 * into the @validate() function. Only if @validate() and @fallback() fail,
 * the area is marked as unavailable for use by the PCMCIA subsystem. The
 * function returns the size of the usable memory area.
 */
static int do_mem_probe(struct pcmcia_socket *s, u_long base, u_long num,
			int (*validate)(struct pcmcia_socket *s,
					struct resource *res,
					unsigned int *value),
			int (*fallback)(struct pcmcia_socket *s,
					struct resource *res,
					unsigned int *value))
{
	struct socket_data *s_data = s->resource_data;
	u_long i, j, bad, fail, step;

	dev_info(&s->dev, "cs: memory probe 0x%06lx-0x%06lx:",
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
				if (!do_validate_mem(s, j, step, validate))
					break;
			}
			fail = ((i == base) && (j == base+num));
		}
		if ((fail) && (fallback)) {
			for (j = i; j < base+num; j += step)
				if (!do_validate_mem(s, j, step, fallback))
					break;
		}
		if (i != j) {
			if (!bad)
				pr_cont(" excluding");
			pr_cont(" %#05lx-%#05lx", i, j-1);
			sub_interval(&s_data->mem_db, i, j-i);
			bad += j-i;
		}
	}
	pr_cont("%s\n", !bad ? " clean" : "");
	return num - bad;
}


#ifdef CONFIG_PCMCIA_PROBE

/**
 * inv_probe() - top-to-bottom search for one usuable high memory area
 * @s:		PCMCIA socket to validate
 * @m:		resource_map to check
 */
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
	return do_mem_probe(s, m->base, m->num, readable, checksum);
}

/**
 * validate_mem() - memory probe function
 * @s:		PCMCIA socket to validate
 * @probe_mask: MEM_PROBE_LOW | MEM_PROBE_HIGH
 *
 * The memory probe.  If the memory list includes a 64K-aligned block
 * below 1MB, we probe in 64K chunks, and as soon as we accumulate at
 * least mem_limit free space, we quit. Returns 0 on usuable ports.
 */
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
		if (s_data->mem_db_valid.next != &s_data->mem_db_valid)
			return 0;
		dev_notice(&s->dev,
			   "cs: warning: no high memory space available!\n");
		return -ENODEV;
	}

	for (m = s_data->mem_db.next; m != &s_data->mem_db; m = mm.next) {
		mm = *m;
		/* Only probe < 1 MB */
		if (mm.base >= 0x100000)
			continue;
		if ((mm.base | mm.num) & 0xffff) {
			ok += do_mem_probe(s, mm.base, mm.num, readable,
					   checksum);
			continue;
		}
		/* Special probe for 64K-aligned block */
		for (i = 0; i < 4; i++) {
			b = order[i] << 12;
			if ((b >= mm.base) && (b+0x10000 <= mm.base+mm.num)) {
				if (ok >= mem_limit)
					sub_interval(&s_data->mem_db, b, 0x10000);
				else
					ok += do_mem_probe(s, b, 0x10000,
							   readable, checksum);
			}
		}
	}

	if (ok > 0)
		return 0;

	return -ENODEV;
}

#else /* CONFIG_PCMCIA_PROBE */

/**
 * validate_mem() - memory probe function
 * @s:		PCMCIA socket to validate
 * @probe_mask: ignored
 *
 * Returns 0 on usuable ports.
 */
static int validate_mem(struct pcmcia_socket *s, unsigned int probe_mask)
{
	struct resource_map *m, mm;
	struct socket_data *s_data = s->resource_data;
	unsigned long ok = 0;

	for (m = s_data->mem_db.next; m != &s_data->mem_db; m = mm.next) {
		mm = *m;
		ok += do_mem_probe(s, mm.base, mm.num, readable, checksum);
	}
	if (ok > 0)
		return 0;
	return -ENODEV;
}

#endif /* CONFIG_PCMCIA_PROBE */


/**
 * pcmcia_nonstatic_validate_mem() - try to validate iomem for PCMCIA use
 * @s:		PCMCIA socket to validate
 *
 * This is tricky... when we set up CIS memory, we try to validate
 * the memory window space allocations.
 *
 * Locking note: Must be called with skt_mutex held!
 */
static int pcmcia_nonstatic_validate_mem(struct pcmcia_socket *s)
{
	struct socket_data *s_data = s->resource_data;
	unsigned int probe_mask = MEM_PROBE_LOW;
	int ret;

	if (!probe_mem || !(s->state & SOCKET_PRESENT))
		return 0;

	if (s->features & SS_CAP_PAGE_REGS)
		probe_mask = MEM_PROBE_HIGH;

	ret = validate_mem(s, probe_mask);

	if (s_data->mem_db_valid.next != &s_data->mem_db_valid)
		return 0;

	return ret;
}

struct pcmcia_align_data {
	unsigned long	mask;
	unsigned long	offset;
	struct resource_map	*map;
};

static resource_size_t pcmcia_common_align(struct pcmcia_align_data *align_data,
					resource_size_t start)
{
	resource_size_t ret;
	/*
	 * Ensure that we have the correct start address
	 */
	ret = (start & ~align_data->mask) + align_data->offset;
	if (ret < start)
		ret += align_data->mask + 1;
	return ret;
}

static resource_size_t
pcmcia_align(void *align_data, const struct resource *res,
	resource_size_t size, resource_size_t align)
{
	struct pcmcia_align_data *data = align_data;
	struct resource_map *m;
	resource_size_t start;

	start = pcmcia_common_align(data, res->start);

	for (m = data->map->next; m != data->map; m = m->next) {
		unsigned long map_start = m->base;
		unsigned long map_end = m->base + m->num - 1;

		/*
		 * If the lower resources are not available, try aligning
		 * to this entry of the resource database to see if it'll
		 * fit here.
		 */
		if (start < map_start)
			start = pcmcia_common_align(data, map_start);

		/*
		 * If we're above the area which was passed in, there's
		 * no point proceeding.
		 */
		if (start >= res->end)
			break;

		if ((start + size - 1) <= map_end)
			break;
	}

	/*
	 * If we failed to find something suitable, ensure we fail.
	 */
	if (m == data->map)
		start = res->end;

	return start;
}

/*
 * Adjust an existing IO region allocation, but making sure that we don't
 * encroach outside the resources which the user supplied.
 */
static int __nonstatic_adjust_io_region(struct pcmcia_socket *s,
					unsigned long r_start,
					unsigned long r_end)
{
	struct resource_map *m;
	struct socket_data *s_data = s->resource_data;
	int ret = -ENOMEM;

	for (m = s_data->io_db.next; m != &s_data->io_db; m = m->next) {
		unsigned long start = m->base;
		unsigned long end = m->base + m->num - 1;

		if (start > r_start || r_end > end)
			continue;

		ret = 0;
	}

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

static struct resource *__nonstatic_find_io_region(struct pcmcia_socket *s,
						unsigned long base, int num,
						unsigned long align)
{
	struct resource *res = pcmcia_make_resource(0, num, IORESOURCE_IO,
						dev_name(&s->dev));
	struct socket_data *s_data = s->resource_data;
	struct pcmcia_align_data data;
	unsigned long min = base;
	int ret;

	if (!res)
		return NULL;

	data.mask = align - 1;
	data.offset = base & data.mask;
	data.map = &s_data->io_db;

#ifdef CONFIG_PCI
	if (s->cb_dev) {
		ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num, 1,
					     min, 0, pcmcia_align, &data);
	} else
#endif
		ret = allocate_resource(&ioport_resource, res, num, min, ~0UL,
					1, pcmcia_align, &data);

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}

static int nonstatic_find_io(struct pcmcia_socket *s, unsigned int attr,
			unsigned int *base, unsigned int num,
			unsigned int align, struct resource **parent)
{
	int i, ret = 0;

	/* Check for an already-allocated window that must conflict with
	 * what was asked for.  It is a hack because it does not catch all
	 * potential conflicts, just the most obvious ones.
	 */
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;

		if (!*base)
			continue;

		if ((s->io[i].res->start & (align-1)) == *base)
			return -EBUSY;
	}

	for (i = 0; i < MAX_IO_WIN; i++) {
		struct resource *res = s->io[i].res;
		unsigned int try;

		if (res && (res->flags & IORESOURCE_BITS) !=
			(attr & IORESOURCE_BITS))
			continue;

		if (!res) {
			if (align == 0)
				align = 0x10000;

			res = s->io[i].res = __nonstatic_find_io_region(s,
								*base, num,
								align);
			if (!res)
				return -EINVAL;

			*base = res->start;
			s->io[i].res->flags =
				((res->flags & ~IORESOURCE_BITS) |
					(attr & IORESOURCE_BITS));
			s->io[i].InUse = num;
			*parent = res;
			return 0;
		}

		/* Try to extend top of window */
		try = res->end + 1;
		if ((*base == 0) || (*base == try)) {
			ret =  __nonstatic_adjust_io_region(s, res->start,
							res->end + num);
			if (!ret) {
				ret = adjust_resource(s->io[i].res, res->start,
						      resource_size(res) + num);
				if (ret)
					continue;
				*base = try;
				s->io[i].InUse += num;
				*parent = res;
				return 0;
			}
		}

		/* Try to extend bottom of window */
		try = res->start - num;
		if ((*base == 0) || (*base == try)) {
			ret =  __nonstatic_adjust_io_region(s,
							res->start - num,
							res->end);
			if (!ret) {
				ret = adjust_resource(s->io[i].res,
						      res->start - num,
						      resource_size(res) + num);
				if (ret)
					continue;
				*base = try;
				s->io[i].InUse += num;
				*parent = res;
				return 0;
			}
		}
	}

	return -EINVAL;
}


static struct resource *nonstatic_find_mem_region(u_long base, u_long num,
		u_long align, int low, struct pcmcia_socket *s)
{
	struct resource *res = pcmcia_make_resource(0, num, IORESOURCE_MEM,
						dev_name(&s->dev));
	struct socket_data *s_data = s->resource_data;
	struct pcmcia_align_data data;
	unsigned long min, max;
	int ret, i, j;

	if (!res)
		return NULL;

	low = low || !(s->features & SS_CAP_PAGE_REGS);

	data.mask = align - 1;
	data.offset = base & data.mask;

	for (i = 0; i < 2; i++) {
		data.map = &s_data->mem_db_valid;
		if (low) {
			max = 0x100000UL;
			min = base < max ? base : 0;
		} else {
			max = ~0UL;
			min = 0x100000UL + base;
		}

		for (j = 0; j < 2; j++) {
#ifdef CONFIG_PCI
			if (s->cb_dev) {
				ret = pci_bus_alloc_resource(s->cb_dev->bus,
							res, num, 1, min, 0,
							pcmcia_align, &data);
			} else
#endif
			{
				ret = allocate_resource(&iomem_resource,
							res, num, min, max, 1,
							pcmcia_align, &data);
			}
			if (ret == 0)
				break;
			data.map = &s_data->mem_db;
		}
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

	switch (action) {
	case ADD_MANAGED_RESOURCE:
		ret = add_interval(&data->mem_db, start, size);
		if (!ret)
			do_mem_probe(s, start, size, NULL, NULL);
		break;
	case REMOVE_MANAGED_RESOURCE:
		ret = sub_interval(&data->mem_db, start, size);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}


static int adjust_io(struct pcmcia_socket *s, unsigned int action, unsigned long start, unsigned long end)
{
	struct socket_data *data = s->resource_data;
	unsigned long size;
	int ret = 0;

#if defined(CONFIG_X86)
	/* on x86, avoid anything < 0x100 for it is often used for
	 * legacy platform devices */
	if (start < 0x100)
		start = 0x100;
#endif

	size = end - start + 1;

	if (end < start)
		return -EINVAL;

	if (end > IO_SPACE_LIMIT)
		return -EINVAL;

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
	/* If this is the root bus, the risk of hitting some strange
	 * system devices is too high: If a driver isn't loaded, the
	 * resources are not claimed; even if a driver is loaded, it
	 * may not request all resources or even the wrong one. We
	 * can neither trust the rest of the kernel nor ACPI/PNP and
	 * CRS parsing to get it right. Therefore, use several
	 * safeguards:
	 *
	 * - Do not auto-add resources if the CardBus bridge is on
	 *   the PCI root bus
	 *
	 * - Avoid any I/O ports < 0x100.
	 *
	 * - On PCI-PCI bridges, only use resources which are set up
	 *   exclusively for the secondary PCI bus: the risk of hitting
	 *   system devices is quite low, as they usually aren't
	 *   connected to the secondary PCI bus.
	 */
	if (s->cb_dev->bus->number == 0)
		return -EINVAL;

	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++) {
		res = s->cb_dev->bus->resource[i];
#else
	pci_bus_for_each_resource(s->cb_dev->bus, res, i) {
#endif
		if (!res)
			continue;

		if (res->flags & IORESOURCE_IO) {
			/* safeguard against the root resource, where the
			 * risk of hitting any other device would be too
			 * high */
			if (res == &ioport_resource)
				continue;

			dev_info(&s->cb_dev->dev,
				 "pcmcia: parent PCI bridge window: %pR\n",
				 res);
			if (!adjust_io(s, ADD_MANAGED_RESOURCE, res->start, res->end))
				done |= IORESOURCE_IO;

		}

		if (res->flags & IORESOURCE_MEM) {
			/* safeguard against the root resource, where the
			 * risk of hitting any other device would be too
			 * high */
			if (res == &iomem_resource)
				continue;

			dev_info(&s->cb_dev->dev,
				 "pcmcia: parent PCI bridge window: %pR\n",
				 res);
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
	data->mem_db_valid.next = &data->mem_db_valid;
	data->io_db.next = &data->io_db;

	s->resource_data = (void *) data;

	nonstatic_autoadd_resources(s);

	return 0;
}

static void nonstatic_release_resource_db(struct pcmcia_socket *s)
{
	struct socket_data *data = s->resource_data;
	struct resource_map *p, *q;

	for (p = data->mem_db_valid.next; p != &data->mem_db_valid; p = q) {
		q = p->next;
		kfree(p);
	}
	for (p = data->mem_db.next; p != &data->mem_db; p = q) {
		q = p->next;
		kfree(p);
	}
	for (p = data->io_db.next; p != &data->io_db; p = q) {
		q = p->next;
		kfree(p);
	}
}


struct pccard_resource_ops pccard_nonstatic_ops = {
	.validate_mem = pcmcia_nonstatic_validate_mem,
	.find_io = nonstatic_find_io,
	.find_mem = nonstatic_find_mem_region,
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

	mutex_lock(&s->ops_mutex);
	data = s->resource_data;

	for (p = data->io_db.next; p != &data->io_db; p = p->next) {
		if (ret > (PAGE_SIZE - 10))
			continue;
		ret += sysfs_emit_at(buf, ret,
				"0x%08lx - 0x%08lx\n",
				((unsigned long) p->base),
				((unsigned long) p->base + p->num - 1));
	}

	mutex_unlock(&s->ops_mutex);
	return ret;
}

static ssize_t store_io_db(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	unsigned long start_addr, end_addr;
	unsigned int add = ADD_MANAGED_RESOURCE;
	ssize_t ret = 0;

	ret = sscanf(buf, "+ 0x%lx - 0x%lx", &start_addr, &end_addr);
	if (ret != 2) {
		ret = sscanf(buf, "- 0x%lx - 0x%lx", &start_addr, &end_addr);
		add = REMOVE_MANAGED_RESOURCE;
		if (ret != 2) {
			ret = sscanf(buf, "0x%lx - 0x%lx", &start_addr,
				&end_addr);
			add = ADD_MANAGED_RESOURCE;
			if (ret != 2)
				return -EINVAL;
		}
	}
	if (end_addr < start_addr)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	ret = adjust_io(s, add, start_addr, end_addr);
	mutex_unlock(&s->ops_mutex);

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

	mutex_lock(&s->ops_mutex);
	data = s->resource_data;

	for (p = data->mem_db_valid.next; p != &data->mem_db_valid;
	     p = p->next) {
		if (ret > (PAGE_SIZE - 10))
			continue;
		ret += sysfs_emit_at(buf, ret,
				"0x%08lx - 0x%08lx\n",
				((unsigned long) p->base),
				((unsigned long) p->base + p->num - 1));
	}

	for (p = data->mem_db.next; p != &data->mem_db; p = p->next) {
		if (ret > (PAGE_SIZE - 10))
			continue;
		ret += sysfs_emit_at(buf, ret,
				"0x%08lx - 0x%08lx\n",
				((unsigned long) p->base),
				((unsigned long) p->base + p->num - 1));
	}

	mutex_unlock(&s->ops_mutex);
	return ret;
}

static ssize_t store_mem_db(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);
	unsigned long start_addr, end_addr;
	unsigned int add = ADD_MANAGED_RESOURCE;
	ssize_t ret = 0;

	ret = sscanf(buf, "+ 0x%lx - 0x%lx", &start_addr, &end_addr);
	if (ret != 2) {
		ret = sscanf(buf, "- 0x%lx - 0x%lx", &start_addr, &end_addr);
		add = REMOVE_MANAGED_RESOURCE;
		if (ret != 2) {
			ret = sscanf(buf, "0x%lx - 0x%lx", &start_addr,
				&end_addr);
			add = ADD_MANAGED_RESOURCE;
			if (ret != 2)
				return -EINVAL;
		}
	}
	if (end_addr < start_addr)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	ret = adjust_memory(s, add, start_addr, end_addr);
	mutex_unlock(&s->ops_mutex);

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

static int pccard_sysfs_add_rsrc(struct device *dev,
					   struct class_interface *class_intf)
{
	struct pcmcia_socket *s = dev_get_drvdata(dev);

	if (s->resource_ops != &pccard_nonstatic_ops)
		return 0;
	return sysfs_create_group(&dev->kobj, &rsrc_attributes);
}

static void pccard_sysfs_remove_rsrc(struct device *dev,
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
	.remove_dev = &pccard_sysfs_remove_rsrc,
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
