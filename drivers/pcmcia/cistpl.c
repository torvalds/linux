/*
 * cistpl.c -- 16-bit PCMCIA Card Information Structure parser
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
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <pcmcia/ss.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

static const u_char mantissa[] = {
    10, 12, 13, 15, 20, 25, 30, 35,
    40, 45, 50, 55, 60, 70, 80, 90
};

static const u_int exponent[] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000
};

/* Convert an extended speed byte to a time in nanoseconds */
#define SPEED_CVT(v) \
    (mantissa[(((v)>>3)&15)-1] * exponent[(v)&7] / 10)
/* Convert a power byte to a current in 0.1 microamps */
#define POWER_CVT(v) \
    (mantissa[((v)>>3)&15] * exponent[(v)&7] / 10)
#define POWER_SCALE(v)		(exponent[(v)&7])

/* Upper limit on reasonable # of tuples */
#define MAX_TUPLES		200

/* Bits in IRQInfo1 field */
#define IRQ_INFO2_VALID		0x10

/* 16-bit CIS? */
static int cis_width;
module_param(cis_width, int, 0444);

void release_cis_mem(struct pcmcia_socket *s)
{
	mutex_lock(&s->ops_mutex);
	if (s->cis_mem.flags & MAP_ACTIVE) {
		s->cis_mem.flags &= ~MAP_ACTIVE;
		s->ops->set_mem_map(s, &s->cis_mem);
		if (s->cis_mem.res) {
			release_resource(s->cis_mem.res);
			kfree(s->cis_mem.res);
			s->cis_mem.res = NULL;
		}
		iounmap(s->cis_virt);
		s->cis_virt = NULL;
	}
	mutex_unlock(&s->ops_mutex);
}

/**
 * set_cis_map() - map the card memory at "card_offset" into virtual space.
 *
 * If flags & MAP_ATTRIB, map the attribute space, otherwise
 * map the memory space.
 *
 * Must be called with ops_mutex held.
 */
static void __iomem *set_cis_map(struct pcmcia_socket *s,
				unsigned int card_offset, unsigned int flags)
{
	pccard_mem_map *mem = &s->cis_mem;
	int ret;

	if (!(s->features & SS_CAP_STATIC_MAP) && (mem->res == NULL)) {
		mem->res = pcmcia_find_mem_region(0, s->map_size,
						s->map_size, 0, s);
		if (mem->res == NULL) {
			dev_notice(&s->dev, "cs: unable to map card memory!\n");
			return NULL;
		}
		s->cis_virt = NULL;
	}

	if (!(s->features & SS_CAP_STATIC_MAP) && (!s->cis_virt))
		s->cis_virt = ioremap(mem->res->start, s->map_size);

	mem->card_start = card_offset;
	mem->flags = flags;

	ret = s->ops->set_mem_map(s, mem);
	if (ret) {
		iounmap(s->cis_virt);
		s->cis_virt = NULL;
		return NULL;
	}

	if (s->features & SS_CAP_STATIC_MAP) {
		if (s->cis_virt)
			iounmap(s->cis_virt);
		s->cis_virt = ioremap(mem->static_start, s->map_size);
	}

	return s->cis_virt;
}


/* Bits in attr field */
#define IS_ATTR		1
#define IS_INDIRECT	8

/**
 * pcmcia_read_cis_mem() - low-level function to read CIS memory
 *
 * must be called with ops_mutex held
 */
int pcmcia_read_cis_mem(struct pcmcia_socket *s, int attr, u_int addr,
		 u_int len, void *ptr)
{
	void __iomem *sys, *end;
	unsigned char *buf = ptr;

	dev_dbg(&s->dev, "pcmcia_read_cis_mem(%d, %#x, %u)\n", attr, addr, len);

	if (attr & IS_INDIRECT) {
		/* Indirect accesses use a bunch of special registers at fixed
		   locations in common memory */
		u_char flags = ICTRL0_COMMON|ICTRL0_AUTOINC|ICTRL0_BYTEGRAN;
		if (attr & IS_ATTR) {
			addr *= 2;
			flags = ICTRL0_AUTOINC;
		}

		sys = set_cis_map(s, 0, MAP_ACTIVE |
				((cis_width) ? MAP_16BIT : 0));
		if (!sys) {
			dev_dbg(&s->dev, "could not map memory\n");
			memset(ptr, 0xff, len);
			return -1;
		}

		writeb(flags, sys+CISREG_ICTRL0);
		writeb(addr & 0xff, sys+CISREG_IADDR0);
		writeb((addr>>8) & 0xff, sys+CISREG_IADDR1);
		writeb((addr>>16) & 0xff, sys+CISREG_IADDR2);
		writeb((addr>>24) & 0xff, sys+CISREG_IADDR3);
		for ( ; len > 0; len--, buf++)
			*buf = readb(sys+CISREG_IDATA0);
	} else {
		u_int inc = 1, card_offset, flags;

		if (addr > CISTPL_MAX_CIS_SIZE) {
			dev_dbg(&s->dev,
				"attempt to read CIS mem at addr %#x", addr);
			memset(ptr, 0xff, len);
			return -1;
		}

		flags = MAP_ACTIVE | ((cis_width) ? MAP_16BIT : 0);
		if (attr) {
			flags |= MAP_ATTRIB;
			inc++;
			addr *= 2;
		}

		card_offset = addr & ~(s->map_size-1);
		while (len) {
			sys = set_cis_map(s, card_offset, flags);
			if (!sys) {
				dev_dbg(&s->dev, "could not map memory\n");
				memset(ptr, 0xff, len);
				return -1;
			}
			end = sys + s->map_size;
			sys = sys + (addr & (s->map_size-1));
			for ( ; len > 0; len--, buf++, sys += inc) {
				if (sys == end)
					break;
				*buf = readb(sys);
			}
			card_offset += s->map_size;
			addr = 0;
		}
	}
	dev_dbg(&s->dev, "  %#2.2x %#2.2x %#2.2x %#2.2x ...\n",
		*(u_char *)(ptr+0), *(u_char *)(ptr+1),
		*(u_char *)(ptr+2), *(u_char *)(ptr+3));
	return 0;
}


/**
 * pcmcia_write_cis_mem() - low-level function to write CIS memory
 *
 * Probably only useful for writing one-byte registers. Must be called
 * with ops_mutex held.
 */
int pcmcia_write_cis_mem(struct pcmcia_socket *s, int attr, u_int addr,
		   u_int len, void *ptr)
{
	void __iomem *sys, *end;
	unsigned char *buf = ptr;

	dev_dbg(&s->dev,
		"pcmcia_write_cis_mem(%d, %#x, %u)\n", attr, addr, len);

	if (attr & IS_INDIRECT) {
		/* Indirect accesses use a bunch of special registers at fixed
		   locations in common memory */
		u_char flags = ICTRL0_COMMON|ICTRL0_AUTOINC|ICTRL0_BYTEGRAN;
		if (attr & IS_ATTR) {
			addr *= 2;
			flags = ICTRL0_AUTOINC;
		}

		sys = set_cis_map(s, 0, MAP_ACTIVE |
				((cis_width) ? MAP_16BIT : 0));
		if (!sys) {
			dev_dbg(&s->dev, "could not map memory\n");
			return -EINVAL;
		}

		writeb(flags, sys+CISREG_ICTRL0);
		writeb(addr & 0xff, sys+CISREG_IADDR0);
		writeb((addr>>8) & 0xff, sys+CISREG_IADDR1);
		writeb((addr>>16) & 0xff, sys+CISREG_IADDR2);
		writeb((addr>>24) & 0xff, sys+CISREG_IADDR3);
		for ( ; len > 0; len--, buf++)
			writeb(*buf, sys+CISREG_IDATA0);
	} else {
		u_int inc = 1, card_offset, flags;

		flags = MAP_ACTIVE | ((cis_width) ? MAP_16BIT : 0);
		if (attr & IS_ATTR) {
			flags |= MAP_ATTRIB;
			inc++;
			addr *= 2;
		}

		card_offset = addr & ~(s->map_size-1);
		while (len) {
			sys = set_cis_map(s, card_offset, flags);
			if (!sys) {
				dev_dbg(&s->dev, "could not map memory\n");
				return -EINVAL;
			}

			end = sys + s->map_size;
			sys = sys + (addr & (s->map_size-1));
			for ( ; len > 0; len--, buf++, sys += inc) {
				if (sys == end)
					break;
				writeb(*buf, sys);
			}
			card_offset += s->map_size;
			addr = 0;
		}
	}
	return 0;
}


/**
 * read_cis_cache() - read CIS memory or its associated cache
 *
 * This is a wrapper around read_cis_mem, with the same interface,
 * but which caches information, for cards whose CIS may not be
 * readable all the time.
 */
static int read_cis_cache(struct pcmcia_socket *s, int attr, u_int addr,
			size_t len, void *ptr)
{
	struct cis_cache_entry *cis;
	int ret = 0;

	if (s->state & SOCKET_CARDBUS)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	if (s->fake_cis) {
		if (s->fake_cis_len >= addr+len)
			memcpy(ptr, s->fake_cis+addr, len);
		else {
			memset(ptr, 0xff, len);
			ret = -EINVAL;
		}
		mutex_unlock(&s->ops_mutex);
		return ret;
	}

	list_for_each_entry(cis, &s->cis_cache, node) {
		if (cis->addr == addr && cis->len == len && cis->attr == attr) {
			memcpy(ptr, cis->cache, len);
			mutex_unlock(&s->ops_mutex);
			return 0;
		}
	}

	ret = pcmcia_read_cis_mem(s, attr, addr, len, ptr);

	if (ret == 0) {
		/* Copy data into the cache */
		cis = kmalloc(sizeof(struct cis_cache_entry) + len, GFP_KERNEL);
		if (cis) {
			cis->addr = addr;
			cis->len = len;
			cis->attr = attr;
			memcpy(cis->cache, ptr, len);
			list_add(&cis->node, &s->cis_cache);
		}
	}
	mutex_unlock(&s->ops_mutex);

	return ret;
}

static void
remove_cis_cache(struct pcmcia_socket *s, int attr, u_int addr, u_int len)
{
	struct cis_cache_entry *cis;

	mutex_lock(&s->ops_mutex);
	list_for_each_entry(cis, &s->cis_cache, node)
		if (cis->addr == addr && cis->len == len && cis->attr == attr) {
			list_del(&cis->node);
			kfree(cis);
			break;
		}
	mutex_unlock(&s->ops_mutex);
}

/**
 * destroy_cis_cache() - destroy the CIS cache
 * @s:		pcmcia_socket for which CIS cache shall be destroyed
 *
 * This destroys the CIS cache but keeps any fake CIS alive. Must be
 * called with ops_mutex held.
 */
void destroy_cis_cache(struct pcmcia_socket *s)
{
	struct list_head *l, *n;
	struct cis_cache_entry *cis;

	list_for_each_safe(l, n, &s->cis_cache) {
		cis = list_entry(l, struct cis_cache_entry, node);
		list_del(&cis->node);
		kfree(cis);
	}
}

/**
 * verify_cis_cache() - does the CIS match what is in the CIS cache?
 */
int verify_cis_cache(struct pcmcia_socket *s)
{
	struct cis_cache_entry *cis;
	char *buf;
	int ret;

	if (s->state & SOCKET_CARDBUS)
		return -EINVAL;

	buf = kmalloc(256, GFP_KERNEL);
	if (buf == NULL) {
		dev_warn(&s->dev, "no memory for verifying CIS\n");
		return -ENOMEM;
	}
	mutex_lock(&s->ops_mutex);
	list_for_each_entry(cis, &s->cis_cache, node) {
		int len = cis->len;

		if (len > 256)
			len = 256;

		ret = pcmcia_read_cis_mem(s, cis->attr, cis->addr, len, buf);
		if (ret || memcmp(buf, cis->cache, len) != 0) {
			kfree(buf);
			mutex_unlock(&s->ops_mutex);
			return -1;
		}
	}
	kfree(buf);
	mutex_unlock(&s->ops_mutex);
	return 0;
}

/**
 * pcmcia_replace_cis() - use a replacement CIS instead of the card's CIS
 *
 * For really bad cards, we provide a facility for uploading a
 * replacement CIS.
 */
int pcmcia_replace_cis(struct pcmcia_socket *s,
		       const u8 *data, const size_t len)
{
	if (len > CISTPL_MAX_CIS_SIZE) {
		dev_warn(&s->dev, "replacement CIS too big\n");
		return -EINVAL;
	}
	mutex_lock(&s->ops_mutex);
	kfree(s->fake_cis);
	s->fake_cis = kmalloc(len, GFP_KERNEL);
	if (s->fake_cis == NULL) {
		dev_warn(&s->dev, "no memory to replace CIS\n");
		mutex_unlock(&s->ops_mutex);
		return -ENOMEM;
	}
	s->fake_cis_len = len;
	memcpy(s->fake_cis, data, len);
	dev_info(&s->dev, "Using replacement CIS\n");
	mutex_unlock(&s->ops_mutex);
	return 0;
}

/* The high-level CIS tuple services */

struct tuple_flags {
	u_int		link_space:4;
	u_int		has_link:1;
	u_int		mfc_fn:3;
	u_int		space:4;
};

#define LINK_SPACE(f)	(((struct tuple_flags *)(&(f)))->link_space)
#define HAS_LINK(f)	(((struct tuple_flags *)(&(f)))->has_link)
#define MFC_FN(f)	(((struct tuple_flags *)(&(f)))->mfc_fn)
#define SPACE(f)	(((struct tuple_flags *)(&(f)))->space)

int pccard_get_first_tuple(struct pcmcia_socket *s, unsigned int function,
			tuple_t *tuple)
{
	if (!s)
		return -EINVAL;

	if (!(s->state & SOCKET_PRESENT) || (s->state & SOCKET_CARDBUS))
		return -ENODEV;
	tuple->TupleLink = tuple->Flags = 0;

	/* Assume presence of a LONGLINK_C to address 0 */
	tuple->CISOffset = tuple->LinkOffset = 0;
	SPACE(tuple->Flags) = HAS_LINK(tuple->Flags) = 1;

	if ((s->functions > 1) && !(tuple->Attributes & TUPLE_RETURN_COMMON)) {
		cisdata_t req = tuple->DesiredTuple;
		tuple->DesiredTuple = CISTPL_LONGLINK_MFC;
		if (pccard_get_next_tuple(s, function, tuple) == 0) {
			tuple->DesiredTuple = CISTPL_LINKTARGET;
			if (pccard_get_next_tuple(s, function, tuple) != 0)
				return -ENOSPC;
		} else
			tuple->CISOffset = tuple->TupleLink = 0;
		tuple->DesiredTuple = req;
	}
	return pccard_get_next_tuple(s, function, tuple);
}

static int follow_link(struct pcmcia_socket *s, tuple_t *tuple)
{
	u_char link[5];
	u_int ofs;
	int ret;

	if (MFC_FN(tuple->Flags)) {
		/* Get indirect link from the MFC tuple */
		ret = read_cis_cache(s, LINK_SPACE(tuple->Flags),
				tuple->LinkOffset, 5, link);
		if (ret)
			return -1;
		ofs = get_unaligned_le32(link + 1);
		SPACE(tuple->Flags) = (link[0] == CISTPL_MFC_ATTR);
		/* Move to the next indirect link */
		tuple->LinkOffset += 5;
		MFC_FN(tuple->Flags)--;
	} else if (HAS_LINK(tuple->Flags)) {
		ofs = tuple->LinkOffset;
		SPACE(tuple->Flags) = LINK_SPACE(tuple->Flags);
		HAS_LINK(tuple->Flags) = 0;
	} else
		return -1;

	if (SPACE(tuple->Flags)) {
		/* This is ugly, but a common CIS error is to code the long
		   link offset incorrectly, so we check the right spot... */
		ret = read_cis_cache(s, SPACE(tuple->Flags), ofs, 5, link);
		if (ret)
			return -1;
		if ((link[0] == CISTPL_LINKTARGET) && (link[1] >= 3) &&
			(strncmp(link+2, "CIS", 3) == 0))
			return ofs;
		remove_cis_cache(s, SPACE(tuple->Flags), ofs, 5);
		/* Then, we try the wrong spot... */
		ofs = ofs >> 1;
	}
	ret = read_cis_cache(s, SPACE(tuple->Flags), ofs, 5, link);
	if (ret)
		return -1;
	if ((link[0] == CISTPL_LINKTARGET) && (link[1] >= 3) &&
		(strncmp(link+2, "CIS", 3) == 0))
		return ofs;
	remove_cis_cache(s, SPACE(tuple->Flags), ofs, 5);
	return -1;
}

int pccard_get_next_tuple(struct pcmcia_socket *s, unsigned int function,
			tuple_t *tuple)
{
	u_char link[2], tmp;
	int ofs, i, attr;
	int ret;

	if (!s)
		return -EINVAL;
	if (!(s->state & SOCKET_PRESENT) || (s->state & SOCKET_CARDBUS))
		return -ENODEV;

	link[1] = tuple->TupleLink;
	ofs = tuple->CISOffset + tuple->TupleLink;
	attr = SPACE(tuple->Flags);

	for (i = 0; i < MAX_TUPLES; i++) {
		if (link[1] == 0xff)
			link[0] = CISTPL_END;
		else {
			ret = read_cis_cache(s, attr, ofs, 2, link);
			if (ret)
				return -1;
			if (link[0] == CISTPL_NULL) {
				ofs++;
				continue;
			}
		}

		/* End of chain?  Follow long link if possible */
		if (link[0] == CISTPL_END) {
			ofs = follow_link(s, tuple);
			if (ofs < 0)
				return -ENOSPC;
			attr = SPACE(tuple->Flags);
			ret = read_cis_cache(s, attr, ofs, 2, link);
			if (ret)
				return -1;
		}

		/* Is this a link tuple?  Make a note of it */
		if ((link[0] == CISTPL_LONGLINK_A) ||
			(link[0] == CISTPL_LONGLINK_C) ||
			(link[0] == CISTPL_LONGLINK_MFC) ||
			(link[0] == CISTPL_LINKTARGET) ||
			(link[0] == CISTPL_INDIRECT) ||
			(link[0] == CISTPL_NO_LINK)) {
			switch (link[0]) {
			case CISTPL_LONGLINK_A:
				HAS_LINK(tuple->Flags) = 1;
				LINK_SPACE(tuple->Flags) = attr | IS_ATTR;
				ret = read_cis_cache(s, attr, ofs+2, 4,
						&tuple->LinkOffset);
				if (ret)
					return -1;
				break;
			case CISTPL_LONGLINK_C:
				HAS_LINK(tuple->Flags) = 1;
				LINK_SPACE(tuple->Flags) = attr & ~IS_ATTR;
				ret = read_cis_cache(s, attr, ofs+2, 4,
						&tuple->LinkOffset);
				if (ret)
					return -1;
				break;
			case CISTPL_INDIRECT:
				HAS_LINK(tuple->Flags) = 1;
				LINK_SPACE(tuple->Flags) = IS_ATTR |
					IS_INDIRECT;
				tuple->LinkOffset = 0;
				break;
			case CISTPL_LONGLINK_MFC:
				tuple->LinkOffset = ofs + 3;
				LINK_SPACE(tuple->Flags) = attr;
				if (function == BIND_FN_ALL) {
					/* Follow all the MFC links */
					ret = read_cis_cache(s, attr, ofs+2,
							1, &tmp);
					if (ret)
						return -1;
					MFC_FN(tuple->Flags) = tmp;
				} else {
					/* Follow exactly one of the links */
					MFC_FN(tuple->Flags) = 1;
					tuple->LinkOffset += function * 5;
				}
				break;
			case CISTPL_NO_LINK:
				HAS_LINK(tuple->Flags) = 0;
				break;
			}
			if ((tuple->Attributes & TUPLE_RETURN_LINK) &&
				(tuple->DesiredTuple == RETURN_FIRST_TUPLE))
				break;
		} else
			if (tuple->DesiredTuple == RETURN_FIRST_TUPLE)
				break;

		if (link[0] == tuple->DesiredTuple)
			break;
		ofs += link[1] + 2;
	}
	if (i == MAX_TUPLES) {
		dev_dbg(&s->dev, "cs: overrun in pcmcia_get_next_tuple\n");
		return -ENOSPC;
	}

	tuple->TupleCode = link[0];
	tuple->TupleLink = link[1];
	tuple->CISOffset = ofs + 2;
	return 0;
}

int pccard_get_tuple_data(struct pcmcia_socket *s, tuple_t *tuple)
{
	u_int len;
	int ret;

	if (!s)
		return -EINVAL;

	if (tuple->TupleLink < tuple->TupleOffset)
		return -ENOSPC;
	len = tuple->TupleLink - tuple->TupleOffset;
	tuple->TupleDataLen = tuple->TupleLink;
	if (len == 0)
		return 0;
	ret = read_cis_cache(s, SPACE(tuple->Flags),
			tuple->CISOffset + tuple->TupleOffset,
			min(len, (u_int) tuple->TupleDataMax),
			tuple->TupleData);
	if (ret)
		return -1;
	return 0;
}


/* Parsing routines for individual tuples */

static int parse_device(tuple_t *tuple, cistpl_device_t *device)
{
	int i;
	u_char scale;
	u_char *p, *q;

	p = (u_char *)tuple->TupleData;
	q = p + tuple->TupleDataLen;

	device->ndev = 0;
	for (i = 0; i < CISTPL_MAX_DEVICES; i++) {

		if (*p == 0xff)
			break;
		device->dev[i].type = (*p >> 4);
		device->dev[i].wp = (*p & 0x08) ? 1 : 0;
		switch (*p & 0x07) {
		case 0:
			device->dev[i].speed = 0;
			break;
		case 1:
			device->dev[i].speed = 250;
			break;
		case 2:
			device->dev[i].speed = 200;
			break;
		case 3:
			device->dev[i].speed = 150;
			break;
		case 4:
			device->dev[i].speed = 100;
			break;
		case 7:
			if (++p == q)
				return -EINVAL;
			device->dev[i].speed = SPEED_CVT(*p);
			while (*p & 0x80)
				if (++p == q)
					return -EINVAL;
			break;
		default:
			return -EINVAL;
		}

		if (++p == q)
			return -EINVAL;
		if (*p == 0xff)
			break;
		scale = *p & 7;
		if (scale == 7)
			return -EINVAL;
		device->dev[i].size = ((*p >> 3) + 1) * (512 << (scale*2));
		device->ndev++;
		if (++p == q)
			break;
	}

	return 0;
}


static int parse_checksum(tuple_t *tuple, cistpl_checksum_t *csum)
{
	u_char *p;
	if (tuple->TupleDataLen < 5)
		return -EINVAL;
	p = (u_char *) tuple->TupleData;
	csum->addr = tuple->CISOffset + get_unaligned_le16(p) - 2;
	csum->len = get_unaligned_le16(p + 2);
	csum->sum = *(p + 4);
	return 0;
}


static int parse_longlink(tuple_t *tuple, cistpl_longlink_t *link)
{
	if (tuple->TupleDataLen < 4)
		return -EINVAL;
	link->addr = get_unaligned_le32(tuple->TupleData);
	return 0;
}


static int parse_longlink_mfc(tuple_t *tuple, cistpl_longlink_mfc_t *link)
{
	u_char *p;
	int i;

	p = (u_char *)tuple->TupleData;

	link->nfn = *p; p++;
	if (tuple->TupleDataLen <= link->nfn*5)
		return -EINVAL;
	for (i = 0; i < link->nfn; i++) {
		link->fn[i].space = *p; p++;
		link->fn[i].addr = get_unaligned_le32(p);
		p += 4;
	}
	return 0;
}


static int parse_strings(u_char *p, u_char *q, int max,
			 char *s, u_char *ofs, u_char *found)
{
	int i, j, ns;

	if (p == q)
		return -EINVAL;
	ns = 0; j = 0;
	for (i = 0; i < max; i++) {
		if (*p == 0xff)
			break;
		ofs[i] = j;
		ns++;
		for (;;) {
			s[j++] = (*p == 0xff) ? '\0' : *p;
			if ((*p == '\0') || (*p == 0xff))
				break;
			if (++p == q)
				return -EINVAL;
		}
		if ((*p == 0xff) || (++p == q))
			break;
	}
	if (found) {
		*found = ns;
		return 0;
	}

	return (ns == max) ? 0 : -EINVAL;
}


static int parse_vers_1(tuple_t *tuple, cistpl_vers_1_t *vers_1)
{
	u_char *p, *q;

	p = (u_char *)tuple->TupleData;
	q = p + tuple->TupleDataLen;

	vers_1->major = *p; p++;
	vers_1->minor = *p; p++;
	if (p >= q)
		return -EINVAL;

	return parse_strings(p, q, CISTPL_VERS_1_MAX_PROD_STRINGS,
			vers_1->str, vers_1->ofs, &vers_1->ns);
}


static int parse_altstr(tuple_t *tuple, cistpl_altstr_t *altstr)
{
	u_char *p, *q;

	p = (u_char *)tuple->TupleData;
	q = p + tuple->TupleDataLen;

	return parse_strings(p, q, CISTPL_MAX_ALTSTR_STRINGS,
			altstr->str, altstr->ofs, &altstr->ns);
}


static int parse_jedec(tuple_t *tuple, cistpl_jedec_t *jedec)
{
	u_char *p, *q;
	int nid;

	p = (u_char *)tuple->TupleData;
	q = p + tuple->TupleDataLen;

	for (nid = 0; nid < CISTPL_MAX_DEVICES; nid++) {
		if (p > q-2)
			break;
		jedec->id[nid].mfr = p[0];
		jedec->id[nid].info = p[1];
		p += 2;
	}
	jedec->nid = nid;
	return 0;
}


static int parse_manfid(tuple_t *tuple, cistpl_manfid_t *m)
{
	if (tuple->TupleDataLen < 4)
		return -EINVAL;
	m->manf = get_unaligned_le16(tuple->TupleData);
	m->card = get_unaligned_le16(tuple->TupleData + 2);
	return 0;
}


static int parse_funcid(tuple_t *tuple, cistpl_funcid_t *f)
{
	u_char *p;
	if (tuple->TupleDataLen < 2)
		return -EINVAL;
	p = (u_char *)tuple->TupleData;
	f->func = p[0];
	f->sysinit = p[1];
	return 0;
}


static int parse_funce(tuple_t *tuple, cistpl_funce_t *f)
{
	u_char *p;
	int i;
	if (tuple->TupleDataLen < 1)
		return -EINVAL;
	p = (u_char *)tuple->TupleData;
	f->type = p[0];
	for (i = 1; i < tuple->TupleDataLen; i++)
		f->data[i-1] = p[i];
	return 0;
}


static int parse_config(tuple_t *tuple, cistpl_config_t *config)
{
	int rasz, rmsz, i;
	u_char *p;

	p = (u_char *)tuple->TupleData;
	rasz = *p & 0x03;
	rmsz = (*p & 0x3c) >> 2;
	if (tuple->TupleDataLen < rasz+rmsz+4)
		return -EINVAL;
	config->last_idx = *(++p);
	p++;
	config->base = 0;
	for (i = 0; i <= rasz; i++)
		config->base += p[i] << (8*i);
	p += rasz+1;
	for (i = 0; i < 4; i++)
		config->rmask[i] = 0;
	for (i = 0; i <= rmsz; i++)
		config->rmask[i>>2] += p[i] << (8*(i%4));
	config->subtuples = tuple->TupleDataLen - (rasz+rmsz+4);
	return 0;
}

/* The following routines are all used to parse the nightmarish
 * config table entries.
 */

static u_char *parse_power(u_char *p, u_char *q, cistpl_power_t *pwr)
{
	int i;
	u_int scale;

	if (p == q)
		return NULL;
	pwr->present = *p;
	pwr->flags = 0;
	p++;
	for (i = 0; i < 7; i++)
		if (pwr->present & (1<<i)) {
			if (p == q)
				return NULL;
			pwr->param[i] = POWER_CVT(*p);
			scale = POWER_SCALE(*p);
			while (*p & 0x80) {
				if (++p == q)
					return NULL;
				if ((*p & 0x7f) < 100)
					pwr->param[i] +=
						(*p & 0x7f) * scale / 100;
				else if (*p == 0x7d)
					pwr->flags |= CISTPL_POWER_HIGHZ_OK;
				else if (*p == 0x7e)
					pwr->param[i] = 0;
				else if (*p == 0x7f)
					pwr->flags |= CISTPL_POWER_HIGHZ_REQ;
				else
					return NULL;
			}
			p++;
		}
	return p;
}


static u_char *parse_timing(u_char *p, u_char *q, cistpl_timing_t *timing)
{
	u_char scale;

	if (p == q)
		return NULL;
	scale = *p;
	if ((scale & 3) != 3) {
		if (++p == q)
			return NULL;
		timing->wait = SPEED_CVT(*p);
		timing->waitscale = exponent[scale & 3];
	} else
		timing->wait = 0;
	scale >>= 2;
	if ((scale & 7) != 7) {
		if (++p == q)
			return NULL;
		timing->ready = SPEED_CVT(*p);
		timing->rdyscale = exponent[scale & 7];
	} else
		timing->ready = 0;
	scale >>= 3;
	if (scale != 7) {
		if (++p == q)
			return NULL;
		timing->reserved = SPEED_CVT(*p);
		timing->rsvscale = exponent[scale];
	} else
		timing->reserved = 0;
	p++;
	return p;
}


static u_char *parse_io(u_char *p, u_char *q, cistpl_io_t *io)
{
	int i, j, bsz, lsz;

	if (p == q)
		return NULL;
	io->flags = *p;

	if (!(*p & 0x80)) {
		io->nwin = 1;
		io->win[0].base = 0;
		io->win[0].len = (1 << (io->flags & CISTPL_IO_LINES_MASK));
		return p+1;
	}

	if (++p == q)
		return NULL;
	io->nwin = (*p & 0x0f) + 1;
	bsz = (*p & 0x30) >> 4;
	if (bsz == 3)
		bsz++;
	lsz = (*p & 0xc0) >> 6;
	if (lsz == 3)
		lsz++;
	p++;

	for (i = 0; i < io->nwin; i++) {
		io->win[i].base = 0;
		io->win[i].len = 1;
		for (j = 0; j < bsz; j++, p++) {
			if (p == q)
				return NULL;
			io->win[i].base += *p << (j*8);
		}
		for (j = 0; j < lsz; j++, p++) {
			if (p == q)
				return NULL;
			io->win[i].len += *p << (j*8);
		}
	}
	return p;
}


static u_char *parse_mem(u_char *p, u_char *q, cistpl_mem_t *mem)
{
	int i, j, asz, lsz, has_ha;
	u_int len, ca, ha;

	if (p == q)
		return NULL;

	mem->nwin = (*p & 0x07) + 1;
	lsz = (*p & 0x18) >> 3;
	asz = (*p & 0x60) >> 5;
	has_ha = (*p & 0x80);
	if (++p == q)
		return NULL;

	for (i = 0; i < mem->nwin; i++) {
		len = ca = ha = 0;
		for (j = 0; j < lsz; j++, p++) {
			if (p == q)
				return NULL;
			len += *p << (j*8);
		}
		for (j = 0; j < asz; j++, p++) {
			if (p == q)
				return NULL;
			ca += *p << (j*8);
		}
		if (has_ha)
			for (j = 0; j < asz; j++, p++) {
				if (p == q)
					return NULL;
				ha += *p << (j*8);
			}
		mem->win[i].len = len << 8;
		mem->win[i].card_addr = ca << 8;
		mem->win[i].host_addr = ha << 8;
	}
	return p;
}


static u_char *parse_irq(u_char *p, u_char *q, cistpl_irq_t *irq)
{
	if (p == q)
		return NULL;
	irq->IRQInfo1 = *p; p++;
	if (irq->IRQInfo1 & IRQ_INFO2_VALID) {
		if (p+2 > q)
			return NULL;
		irq->IRQInfo2 = (p[1]<<8) + p[0];
		p += 2;
	}
	return p;
}


static int parse_cftable_entry(tuple_t *tuple,
			       cistpl_cftable_entry_t *entry)
{
	u_char *p, *q, features;

	p = tuple->TupleData;
	q = p + tuple->TupleDataLen;
	entry->index = *p & 0x3f;
	entry->flags = 0;
	if (*p & 0x40)
		entry->flags |= CISTPL_CFTABLE_DEFAULT;
	if (*p & 0x80) {
		if (++p == q)
			return -EINVAL;
		if (*p & 0x10)
			entry->flags |= CISTPL_CFTABLE_BVDS;
		if (*p & 0x20)
			entry->flags |= CISTPL_CFTABLE_WP;
		if (*p & 0x40)
			entry->flags |= CISTPL_CFTABLE_RDYBSY;
		if (*p & 0x80)
			entry->flags |= CISTPL_CFTABLE_MWAIT;
		entry->interface = *p & 0x0f;
	} else
		entry->interface = 0;

	/* Process optional features */
	if (++p == q)
		return -EINVAL;
	features = *p; p++;

	/* Power options */
	if ((features & 3) > 0) {
		p = parse_power(p, q, &entry->vcc);
		if (p == NULL)
			return -EINVAL;
	} else
		entry->vcc.present = 0;
	if ((features & 3) > 1) {
		p = parse_power(p, q, &entry->vpp1);
		if (p == NULL)
			return -EINVAL;
	} else
		entry->vpp1.present = 0;
	if ((features & 3) > 2) {
		p = parse_power(p, q, &entry->vpp2);
		if (p == NULL)
			return -EINVAL;
	} else
		entry->vpp2.present = 0;

	/* Timing options */
	if (features & 0x04) {
		p = parse_timing(p, q, &entry->timing);
		if (p == NULL)
			return -EINVAL;
	} else {
		entry->timing.wait = 0;
		entry->timing.ready = 0;
		entry->timing.reserved = 0;
	}

	/* I/O window options */
	if (features & 0x08) {
		p = parse_io(p, q, &entry->io);
		if (p == NULL)
			return -EINVAL;
	} else
		entry->io.nwin = 0;

	/* Interrupt options */
	if (features & 0x10) {
		p = parse_irq(p, q, &entry->irq);
		if (p == NULL)
			return -EINVAL;
	} else
		entry->irq.IRQInfo1 = 0;

	switch (features & 0x60) {
	case 0x00:
		entry->mem.nwin = 0;
		break;
	case 0x20:
		entry->mem.nwin = 1;
		entry->mem.win[0].len = get_unaligned_le16(p) << 8;
		entry->mem.win[0].card_addr = 0;
		entry->mem.win[0].host_addr = 0;
		p += 2;
		if (p > q)
			return -EINVAL;
		break;
	case 0x40:
		entry->mem.nwin = 1;
		entry->mem.win[0].len = get_unaligned_le16(p) << 8;
		entry->mem.win[0].card_addr = get_unaligned_le16(p + 2) << 8;
		entry->mem.win[0].host_addr = 0;
		p += 4;
		if (p > q)
			return -EINVAL;
		break;
	case 0x60:
		p = parse_mem(p, q, &entry->mem);
		if (p == NULL)
			return -EINVAL;
		break;
	}

	/* Misc features */
	if (features & 0x80) {
		if (p == q)
			return -EINVAL;
		entry->flags |= (*p << 8);
		while (*p & 0x80)
			if (++p == q)
				return -EINVAL;
		p++;
	}

	entry->subtuples = q-p;

	return 0;
}


static int parse_device_geo(tuple_t *tuple, cistpl_device_geo_t *geo)
{
	u_char *p, *q;
	int n;

	p = (u_char *)tuple->TupleData;
	q = p + tuple->TupleDataLen;

	for (n = 0; n < CISTPL_MAX_DEVICES; n++) {
		if (p > q-6)
			break;
		geo->geo[n].buswidth = p[0];
		geo->geo[n].erase_block = 1 << (p[1]-1);
		geo->geo[n].read_block  = 1 << (p[2]-1);
		geo->geo[n].write_block = 1 << (p[3]-1);
		geo->geo[n].partition   = 1 << (p[4]-1);
		geo->geo[n].interleave  = 1 << (p[5]-1);
		p += 6;
	}
	geo->ngeo = n;
	return 0;
}


static int parse_vers_2(tuple_t *tuple, cistpl_vers_2_t *v2)
{
	u_char *p, *q;

	if (tuple->TupleDataLen < 10)
		return -EINVAL;

	p = tuple->TupleData;
	q = p + tuple->TupleDataLen;

	v2->vers = p[0];
	v2->comply = p[1];
	v2->dindex = get_unaligned_le16(p + 2);
	v2->vspec8 = p[6];
	v2->vspec9 = p[7];
	v2->nhdr = p[8];
	p += 9;
	return parse_strings(p, q, 2, v2->str, &v2->vendor, NULL);
}


static int parse_org(tuple_t *tuple, cistpl_org_t *org)
{
	u_char *p, *q;
	int i;

	p = tuple->TupleData;
	q = p + tuple->TupleDataLen;
	if (p == q)
		return -EINVAL;
	org->data_org = *p;
	if (++p == q)
		return -EINVAL;
	for (i = 0; i < 30; i++) {
		org->desc[i] = *p;
		if (*p == '\0')
			break;
		if (++p == q)
			return -EINVAL;
	}
	return 0;
}


static int parse_format(tuple_t *tuple, cistpl_format_t *fmt)
{
	u_char *p;

	if (tuple->TupleDataLen < 10)
		return -EINVAL;

	p = tuple->TupleData;

	fmt->type = p[0];
	fmt->edc = p[1];
	fmt->offset = get_unaligned_le32(p + 2);
	fmt->length = get_unaligned_le32(p + 6);

	return 0;
}


int pcmcia_parse_tuple(tuple_t *tuple, cisparse_t *parse)
{
	int ret = 0;

	if (tuple->TupleDataLen > tuple->TupleDataMax)
		return -EINVAL;
	switch (tuple->TupleCode) {
	case CISTPL_DEVICE:
	case CISTPL_DEVICE_A:
		ret = parse_device(tuple, &parse->device);
		break;
	case CISTPL_CHECKSUM:
		ret = parse_checksum(tuple, &parse->checksum);
		break;
	case CISTPL_LONGLINK_A:
	case CISTPL_LONGLINK_C:
		ret = parse_longlink(tuple, &parse->longlink);
		break;
	case CISTPL_LONGLINK_MFC:
		ret = parse_longlink_mfc(tuple, &parse->longlink_mfc);
		break;
	case CISTPL_VERS_1:
		ret = parse_vers_1(tuple, &parse->version_1);
		break;
	case CISTPL_ALTSTR:
		ret = parse_altstr(tuple, &parse->altstr);
		break;
	case CISTPL_JEDEC_A:
	case CISTPL_JEDEC_C:
		ret = parse_jedec(tuple, &parse->jedec);
		break;
	case CISTPL_MANFID:
		ret = parse_manfid(tuple, &parse->manfid);
		break;
	case CISTPL_FUNCID:
		ret = parse_funcid(tuple, &parse->funcid);
		break;
	case CISTPL_FUNCE:
		ret = parse_funce(tuple, &parse->funce);
		break;
	case CISTPL_CONFIG:
		ret = parse_config(tuple, &parse->config);
		break;
	case CISTPL_CFTABLE_ENTRY:
		ret = parse_cftable_entry(tuple, &parse->cftable_entry);
		break;
	case CISTPL_DEVICE_GEO:
	case CISTPL_DEVICE_GEO_A:
		ret = parse_device_geo(tuple, &parse->device_geo);
		break;
	case CISTPL_VERS_2:
		ret = parse_vers_2(tuple, &parse->vers_2);
		break;
	case CISTPL_ORG:
		ret = parse_org(tuple, &parse->org);
		break;
	case CISTPL_FORMAT:
	case CISTPL_FORMAT_A:
		ret = parse_format(tuple, &parse->format);
		break;
	case CISTPL_NO_LINK:
	case CISTPL_LINKTARGET:
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret)
		pr_debug("parse_tuple failed %d\n", ret);
	return ret;
}
EXPORT_SYMBOL(pcmcia_parse_tuple);


/**
 * pccard_validate_cis() - check whether card has a sensible CIS
 * @s:		the struct pcmcia_socket we are to check
 * @info:	returns the number of tuples in the (valid) CIS, or 0
 *
 * This tries to determine if a card has a sensible CIS.  In @info, it
 * returns the number of tuples in the CIS, or 0 if the CIS looks bad. The
 * checks include making sure several critical tuples are present and
 * valid; seeing if the total number of tuples is reasonable; and
 * looking for tuples that use reserved codes.
 *
 * The function returns 0 on success.
 */
int pccard_validate_cis(struct pcmcia_socket *s, unsigned int *info)
{
	tuple_t *tuple;
	cisparse_t *p;
	unsigned int count = 0;
	int ret, reserved, dev_ok = 0, ident_ok = 0;

	if (!s)
		return -EINVAL;

	if (s->functions || !(s->state & SOCKET_PRESENT)) {
		WARN_ON(1);
		return -EINVAL;
	}

	/* We do not want to validate the CIS cache... */
	mutex_lock(&s->ops_mutex);
	destroy_cis_cache(s);
	mutex_unlock(&s->ops_mutex);

	tuple = kmalloc(sizeof(*tuple), GFP_KERNEL);
	if (tuple == NULL) {
		dev_warn(&s->dev, "no memory to validate CIS\n");
		return -ENOMEM;
	}
	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) {
		kfree(tuple);
		dev_warn(&s->dev, "no memory to validate CIS\n");
		return -ENOMEM;
	}

	count = reserved = 0;
	tuple->DesiredTuple = RETURN_FIRST_TUPLE;
	tuple->Attributes = TUPLE_RETURN_COMMON;
	ret = pccard_get_first_tuple(s, BIND_FN_ALL, tuple);
	if (ret != 0)
		goto done;

	/* First tuple should be DEVICE; we should really have either that
	   or a CFTABLE_ENTRY of some sort */
	if ((tuple->TupleCode == CISTPL_DEVICE) ||
	    (!pccard_read_tuple(s, BIND_FN_ALL, CISTPL_CFTABLE_ENTRY, p)) ||
	    (!pccard_read_tuple(s, BIND_FN_ALL, CISTPL_CFTABLE_ENTRY_CB, p)))
		dev_ok++;

	/* All cards should have a MANFID tuple, and/or a VERS_1 or VERS_2
	   tuple, for card identification.  Certain old D-Link and Linksys
	   cards have only a broken VERS_2 tuple; hence the bogus test. */
	if ((pccard_read_tuple(s, BIND_FN_ALL, CISTPL_MANFID, p) == 0) ||
	    (pccard_read_tuple(s, BIND_FN_ALL, CISTPL_VERS_1, p) == 0) ||
	    (pccard_read_tuple(s, BIND_FN_ALL, CISTPL_VERS_2, p) != -ENOSPC))
		ident_ok++;

	if (!dev_ok && !ident_ok)
		goto done;

	for (count = 1; count < MAX_TUPLES; count++) {
		ret = pccard_get_next_tuple(s, BIND_FN_ALL, tuple);
		if (ret != 0)
			break;
		if (((tuple->TupleCode > 0x23) && (tuple->TupleCode < 0x40)) ||
		    ((tuple->TupleCode > 0x47) && (tuple->TupleCode < 0x80)) ||
		    ((tuple->TupleCode > 0x90) && (tuple->TupleCode < 0xff)))
			reserved++;
	}
	if ((count == MAX_TUPLES) || (reserved > 5) ||
		((!dev_ok || !ident_ok) && (count > 10)))
		count = 0;

	ret = 0;

done:
	/* invalidate CIS cache on failure */
	if (!dev_ok || !ident_ok || !count) {
		mutex_lock(&s->ops_mutex);
		destroy_cis_cache(s);
		mutex_unlock(&s->ops_mutex);
		/* We differentiate between dev_ok, ident_ok and count
		   failures to allow for an override for anonymous cards
		   in ds.c */
		if (!dev_ok || !ident_ok)
			ret = -EIO;
		else
			ret = -EFAULT;
	}

	if (info)
		*info = count;
	kfree(tuple);
	kfree(p);
	return ret;
}


#define to_socket(_dev) container_of(_dev, struct pcmcia_socket, dev)

static ssize_t pccard_extract_cis(struct pcmcia_socket *s, char *buf,
				  loff_t off, size_t count)
{
	tuple_t tuple;
	int status, i;
	loff_t pointer = 0;
	ssize_t ret = 0;
	u_char *tuplebuffer;
	u_char *tempbuffer;

	tuplebuffer = kmalloc(sizeof(u_char) * 256, GFP_KERNEL);
	if (!tuplebuffer)
		return -ENOMEM;

	tempbuffer = kmalloc(sizeof(u_char) * 258, GFP_KERNEL);
	if (!tempbuffer) {
		ret = -ENOMEM;
		goto free_tuple;
	}

	memset(&tuple, 0, sizeof(tuple_t));

	tuple.Attributes = TUPLE_RETURN_LINK | TUPLE_RETURN_COMMON;
	tuple.DesiredTuple = RETURN_FIRST_TUPLE;
	tuple.TupleOffset = 0;

	status = pccard_get_first_tuple(s, BIND_FN_ALL, &tuple);
	while (!status) {
		tuple.TupleData = tuplebuffer;
		tuple.TupleDataMax = 255;
		memset(tuplebuffer, 0, sizeof(u_char) * 255);

		status = pccard_get_tuple_data(s, &tuple);
		if (status)
			break;

		if (off < (pointer + 2 + tuple.TupleDataLen)) {
			tempbuffer[0] = tuple.TupleCode & 0xff;
			tempbuffer[1] = tuple.TupleLink & 0xff;
			for (i = 0; i < tuple.TupleDataLen; i++)
				tempbuffer[i + 2] = tuplebuffer[i] & 0xff;

			for (i = 0; i < (2 + tuple.TupleDataLen); i++) {
				if (((i + pointer) >= off) &&
				    (i + pointer) < (off + count)) {
					buf[ret] = tempbuffer[i];
					ret++;
				}
			}
		}

		pointer += 2 + tuple.TupleDataLen;

		if (pointer >= (off + count))
			break;

		if (tuple.TupleCode == CISTPL_END)
			break;
		status = pccard_get_next_tuple(s, BIND_FN_ALL, &tuple);
	}

	kfree(tempbuffer);
 free_tuple:
	kfree(tuplebuffer);

	return ret;
}


static ssize_t pccard_show_cis(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	unsigned int size = 0x200;

	if (off >= size)
		count = 0;
	else {
		struct pcmcia_socket *s;
		unsigned int chains = 1;

		if (off + count > size)
			count = size - off;

		s = to_socket(container_of(kobj, struct device, kobj));

		if (!(s->state & SOCKET_PRESENT))
			return -ENODEV;
		if (!s->functions && pccard_validate_cis(s, &chains))
			return -EIO;
		if (!chains)
			return -ENODATA;

		count = pccard_extract_cis(s, buf, off, count);
	}

	return count;
}


static ssize_t pccard_store_cis(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct pcmcia_socket *s;
	int error;

	s = to_socket(container_of(kobj, struct device, kobj));

	if (off)
		return -EINVAL;

	if (count >= CISTPL_MAX_CIS_SIZE)
		return -EINVAL;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	error = pcmcia_replace_cis(s, buf, count);
	if (error)
		return -EIO;

	pcmcia_parse_uevents(s, PCMCIA_UEVENT_REQUERY);

	return count;
}


const struct bin_attribute pccard_cis_attr = {
	.attr = { .name = "cis", .mode = S_IRUGO | S_IWUSR },
	.size = 0x200,
	.read = pccard_show_cis,
	.write = pccard_store_cis,
};
