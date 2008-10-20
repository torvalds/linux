/*
 *  ISA Plug & Play support
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Changelog:
 *  2000-01-01	Added quirks handling for buggy hardware
 *		Peter Denison <peterd@pnd-pc.demon.co.uk>
 *  2000-06-14	Added isapnp_probe_devs() and isapnp_activate_dev()
 *		Christoph Hellwig <hch@infradead.org>
 *  2001-06-03  Added release_region calls to correspond with
 *		request_region calls when a failure occurs.  Also
 *		added KERN_* constants to printk() calls.
 *  2001-11-07  Added isapnp_{,un}register_driver calls along the lines
 *              of the pci driver interface
 *              Kai Germaschewski <kai.germaschewski@gmx.de>
 *  2002-06-06  Made the use of dma channel 0 configurable
 *              Gerald Teschl <gerald.teschl@univie.ac.at>
 *  2002-10-06  Ported to PnP Layer - Adam Belay <ambx1@neo.rr.com>
 *  2003-08-11	Resource Management Updates - Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/isapnp.h>
#include <linux/mutex.h>
#include <asm/io.h>

#include "../base.h"

#if 0
#define ISAPNP_REGION_OK
#endif

int isapnp_disable;		/* Disable ISA PnP */
static int isapnp_rdp;		/* Read Data Port */
static int isapnp_reset = 1;	/* reset all PnP cards (deactivate) */
static int isapnp_verbose = 1;	/* verbose mode */

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Generic ISA Plug & Play support");
module_param(isapnp_disable, int, 0);
MODULE_PARM_DESC(isapnp_disable, "ISA Plug & Play disable");
module_param(isapnp_rdp, int, 0);
MODULE_PARM_DESC(isapnp_rdp, "ISA Plug & Play read data port");
module_param(isapnp_reset, int, 0);
MODULE_PARM_DESC(isapnp_reset, "ISA Plug & Play reset all cards");
module_param(isapnp_verbose, int, 0);
MODULE_PARM_DESC(isapnp_verbose, "ISA Plug & Play verbose mode");
MODULE_LICENSE("GPL");

#define _PIDXR		0x279
#define _PNPWRP		0xa79

/* short tags */
#define _STAG_PNPVERNO		0x01
#define _STAG_LOGDEVID		0x02
#define _STAG_COMPATDEVID	0x03
#define _STAG_IRQ		0x04
#define _STAG_DMA		0x05
#define _STAG_STARTDEP		0x06
#define _STAG_ENDDEP		0x07
#define _STAG_IOPORT		0x08
#define _STAG_FIXEDIO		0x09
#define _STAG_VENDOR		0x0e
#define _STAG_END		0x0f
/* long tags */
#define _LTAG_MEMRANGE		0x81
#define _LTAG_ANSISTR		0x82
#define _LTAG_UNICODESTR	0x83
#define _LTAG_VENDOR		0x84
#define _LTAG_MEM32RANGE	0x85
#define _LTAG_FIXEDMEM32RANGE	0x86

/* Logical device control and configuration registers */

#define ISAPNP_CFG_ACTIVATE	0x30	/* byte */
#define ISAPNP_CFG_MEM		0x40	/* 4 * dword */
#define ISAPNP_CFG_PORT		0x60	/* 8 * word */
#define ISAPNP_CFG_IRQ		0x70	/* 2 * word */
#define ISAPNP_CFG_DMA		0x74	/* 2 * byte */

/*
 * Sizes of ISAPNP logical device configuration register sets.
 * See PNP-ISA-v1.0a.pdf, Appendix A.
 */
#define ISAPNP_MAX_MEM		4
#define ISAPNP_MAX_PORT		8
#define ISAPNP_MAX_IRQ		2
#define ISAPNP_MAX_DMA		2

static unsigned char isapnp_checksum_value;
static DEFINE_MUTEX(isapnp_cfg_mutex);
static int isapnp_csn_count;

/* some prototypes */

static inline void write_data(unsigned char x)
{
	outb(x, _PNPWRP);
}

static inline void write_address(unsigned char x)
{
	outb(x, _PIDXR);
	udelay(20);
}

static inline unsigned char read_data(void)
{
	unsigned char val = inb(isapnp_rdp);
	return val;
}

unsigned char isapnp_read_byte(unsigned char idx)
{
	write_address(idx);
	return read_data();
}

static unsigned short isapnp_read_word(unsigned char idx)
{
	unsigned short val;

	val = isapnp_read_byte(idx);
	val = (val << 8) + isapnp_read_byte(idx + 1);
	return val;
}

void isapnp_write_byte(unsigned char idx, unsigned char val)
{
	write_address(idx);
	write_data(val);
}

static void isapnp_write_word(unsigned char idx, unsigned short val)
{
	isapnp_write_byte(idx, val >> 8);
	isapnp_write_byte(idx + 1, val);
}

static void isapnp_key(void)
{
	unsigned char code = 0x6a, msb;
	int i;

	mdelay(1);
	write_address(0x00);
	write_address(0x00);

	write_address(code);

	for (i = 1; i < 32; i++) {
		msb = ((code & 0x01) ^ ((code & 0x02) >> 1)) << 7;
		code = (code >> 1) | msb;
		write_address(code);
	}
}

/* place all pnp cards in wait-for-key state */
static void isapnp_wait(void)
{
	isapnp_write_byte(0x02, 0x02);
}

static void isapnp_wake(unsigned char csn)
{
	isapnp_write_byte(0x03, csn);
}

static void isapnp_device(unsigned char logdev)
{
	isapnp_write_byte(0x07, logdev);
}

static void isapnp_activate(unsigned char logdev)
{
	isapnp_device(logdev);
	isapnp_write_byte(ISAPNP_CFG_ACTIVATE, 1);
	udelay(250);
}

static void isapnp_deactivate(unsigned char logdev)
{
	isapnp_device(logdev);
	isapnp_write_byte(ISAPNP_CFG_ACTIVATE, 0);
	udelay(500);
}

static void __init isapnp_peek(unsigned char *data, int bytes)
{
	int i, j;
	unsigned char d = 0;

	for (i = 1; i <= bytes; i++) {
		for (j = 0; j < 20; j++) {
			d = isapnp_read_byte(0x05);
			if (d & 1)
				break;
			udelay(100);
		}
		if (!(d & 1)) {
			if (data != NULL)
				*data++ = 0xff;
			continue;
		}
		d = isapnp_read_byte(0x04);	/* PRESDI */
		isapnp_checksum_value += d;
		if (data != NULL)
			*data++ = d;
	}
}

#define RDP_STEP	32	/* minimum is 4 */

static int isapnp_next_rdp(void)
{
	int rdp = isapnp_rdp;
	static int old_rdp = 0;

	if (old_rdp) {
		release_region(old_rdp, 1);
		old_rdp = 0;
	}
	while (rdp <= 0x3ff) {
		/*
		 *      We cannot use NE2000 probe spaces for ISAPnP or we
		 *      will lock up machines.
		 */
		if ((rdp < 0x280 || rdp > 0x380)
		    && request_region(rdp, 1, "ISAPnP")) {
			isapnp_rdp = rdp;
			old_rdp = rdp;
			return 0;
		}
		rdp += RDP_STEP;
	}
	return -1;
}

/* Set read port address */
static inline void isapnp_set_rdp(void)
{
	isapnp_write_byte(0x00, isapnp_rdp >> 2);
	udelay(100);
}

/*
 *	Perform an isolation. The port selection code now tries to avoid
 *	"dangerous to read" ports.
 */
static int __init isapnp_isolate_rdp_select(void)
{
	isapnp_wait();
	isapnp_key();

	/* Control: reset CSN and conditionally everything else too */
	isapnp_write_byte(0x02, isapnp_reset ? 0x05 : 0x04);
	mdelay(2);

	isapnp_wait();
	isapnp_key();
	isapnp_wake(0x00);

	if (isapnp_next_rdp() < 0) {
		isapnp_wait();
		return -1;
	}

	isapnp_set_rdp();
	udelay(1000);
	write_address(0x01);
	udelay(1000);
	return 0;
}

/*
 *  Isolate (assign uniqued CSN) to all ISA PnP devices.
 */
static int __init isapnp_isolate(void)
{
	unsigned char checksum = 0x6a;
	unsigned char chksum = 0x00;
	unsigned char bit = 0x00;
	int data;
	int csn = 0;
	int i;
	int iteration = 1;

	isapnp_rdp = 0x213;
	if (isapnp_isolate_rdp_select() < 0)
		return -1;

	while (1) {
		for (i = 1; i <= 64; i++) {
			data = read_data() << 8;
			udelay(250);
			data = data | read_data();
			udelay(250);
			if (data == 0x55aa)
				bit = 0x01;
			checksum =
			    ((((checksum ^ (checksum >> 1)) & 0x01) ^ bit) << 7)
			    | (checksum >> 1);
			bit = 0x00;
		}
		for (i = 65; i <= 72; i++) {
			data = read_data() << 8;
			udelay(250);
			data = data | read_data();
			udelay(250);
			if (data == 0x55aa)
				chksum |= (1 << (i - 65));
		}
		if (checksum != 0x00 && checksum == chksum) {
			csn++;

			isapnp_write_byte(0x06, csn);
			udelay(250);
			iteration++;
			isapnp_wake(0x00);
			isapnp_set_rdp();
			udelay(1000);
			write_address(0x01);
			udelay(1000);
			goto __next;
		}
		if (iteration == 1) {
			isapnp_rdp += RDP_STEP;
			if (isapnp_isolate_rdp_select() < 0)
				return -1;
		} else if (iteration > 1) {
			break;
		}
__next:
		if (csn == 255)
			break;
		checksum = 0x6a;
		chksum = 0x00;
		bit = 0x00;
	}
	isapnp_wait();
	isapnp_csn_count = csn;
	return csn;
}

/*
 *  Read one tag from stream.
 */
static int __init isapnp_read_tag(unsigned char *type, unsigned short *size)
{
	unsigned char tag, tmp[2];

	isapnp_peek(&tag, 1);
	if (tag == 0)		/* invalid tag */
		return -1;
	if (tag & 0x80) {	/* large item */
		*type = tag;
		isapnp_peek(tmp, 2);
		*size = (tmp[1] << 8) | tmp[0];
	} else {
		*type = (tag >> 3) & 0x0f;
		*size = tag & 0x07;
	}
#if 0
	printk(KERN_DEBUG "tag = 0x%x, type = 0x%x, size = %i\n", tag, *type,
	       *size);
#endif
	if (*type == 0xff && *size == 0xffff)	/* probably invalid data */
		return -1;
	return 0;
}

/*
 *  Skip specified number of bytes from stream.
 */
static void __init isapnp_skip_bytes(int count)
{
	isapnp_peek(NULL, count);
}

/*
 *  Parse logical device tag.
 */
static struct pnp_dev *__init isapnp_parse_device(struct pnp_card *card,
						  int size, int number)
{
	unsigned char tmp[6];
	struct pnp_dev *dev;
	u32 eisa_id;
	char id[8];

	isapnp_peek(tmp, size);
	eisa_id = tmp[0] | tmp[1] << 8 | tmp[2] << 16 | tmp[3] << 24;
	pnp_eisa_id_to_string(eisa_id, id);

	dev = pnp_alloc_dev(&isapnp_protocol, number, id);
	if (!dev)
		return NULL;

	dev->card = card;
	dev->capabilities |= PNP_CONFIGURABLE;
	dev->capabilities |= PNP_READ;
	dev->capabilities |= PNP_WRITE;
	dev->capabilities |= PNP_DISABLE;
	pnp_init_resources(dev);
	return dev;
}

/*
 *  Add IRQ resource to resources list.
 */
static void __init isapnp_parse_irq_resource(struct pnp_dev *dev,
					     unsigned int option_flags,
					     int size)
{
	unsigned char tmp[3];
	unsigned long bits;
	pnp_irq_mask_t map;
	unsigned char flags = IORESOURCE_IRQ_HIGHEDGE;

	isapnp_peek(tmp, size);
	bits = (tmp[1] << 8) | tmp[0];

	bitmap_zero(map.bits, PNP_IRQ_NR);
	bitmap_copy(map.bits, &bits, 16);

	if (size > 2)
		flags = tmp[2];

	pnp_register_irq_resource(dev, option_flags, &map, flags);
}

/*
 *  Add DMA resource to resources list.
 */
static void __init isapnp_parse_dma_resource(struct pnp_dev *dev,
					     unsigned int option_flags,
					     int size)
{
	unsigned char tmp[2];

	isapnp_peek(tmp, size);
	pnp_register_dma_resource(dev, option_flags, tmp[0], tmp[1]);
}

/*
 *  Add port resource to resources list.
 */
static void __init isapnp_parse_port_resource(struct pnp_dev *dev,
					      unsigned int option_flags,
					      int size)
{
	unsigned char tmp[7];
	resource_size_t min, max, align, len;
	unsigned char flags;

	isapnp_peek(tmp, size);
	min = (tmp[2] << 8) | tmp[1];
	max = (tmp[4] << 8) | tmp[3];
	align = tmp[5];
	len = tmp[6];
	flags = tmp[0] ? IORESOURCE_IO_16BIT_ADDR : 0;
	pnp_register_port_resource(dev, option_flags,
				   min, max, align, len, flags);
}

/*
 *  Add fixed port resource to resources list.
 */
static void __init isapnp_parse_fixed_port_resource(struct pnp_dev *dev,
						    unsigned int option_flags,
						    int size)
{
	unsigned char tmp[3];
	resource_size_t base, len;

	isapnp_peek(tmp, size);
	base = (tmp[1] << 8) | tmp[0];
	len = tmp[2];
	pnp_register_port_resource(dev, option_flags, base, base, 0, len,
				   IORESOURCE_IO_FIXED);
}

/*
 *  Add memory resource to resources list.
 */
static void __init isapnp_parse_mem_resource(struct pnp_dev *dev,
					     unsigned int option_flags,
					     int size)
{
	unsigned char tmp[9];
	resource_size_t min, max, align, len;
	unsigned char flags;

	isapnp_peek(tmp, size);
	min = ((tmp[2] << 8) | tmp[1]) << 8;
	max = ((tmp[4] << 8) | tmp[3]) << 8;
	align = (tmp[6] << 8) | tmp[5];
	len = ((tmp[8] << 8) | tmp[7]) << 8;
	flags = tmp[0];
	pnp_register_mem_resource(dev, option_flags,
				  min, max, align, len, flags);
}

/*
 *  Add 32-bit memory resource to resources list.
 */
static void __init isapnp_parse_mem32_resource(struct pnp_dev *dev,
					       unsigned int option_flags,
					       int size)
{
	unsigned char tmp[17];
	resource_size_t min, max, align, len;
	unsigned char flags;

	isapnp_peek(tmp, size);
	min = (tmp[4] << 24) | (tmp[3] << 16) | (tmp[2] << 8) | tmp[1];
	max = (tmp[8] << 24) | (tmp[7] << 16) | (tmp[6] << 8) | tmp[5];
	align = (tmp[12] << 24) | (tmp[11] << 16) | (tmp[10] << 8) | tmp[9];
	len = (tmp[16] << 24) | (tmp[15] << 16) | (tmp[14] << 8) | tmp[13];
	flags = tmp[0];
	pnp_register_mem_resource(dev, option_flags,
				  min, max, align, len, flags);
}

/*
 *  Add 32-bit fixed memory resource to resources list.
 */
static void __init isapnp_parse_fixed_mem32_resource(struct pnp_dev *dev,
						     unsigned int option_flags,
						     int size)
{
	unsigned char tmp[9];
	resource_size_t base, len;
	unsigned char flags;

	isapnp_peek(tmp, size);
	base = (tmp[4] << 24) | (tmp[3] << 16) | (tmp[2] << 8) | tmp[1];
	len = (tmp[8] << 24) | (tmp[7] << 16) | (tmp[6] << 8) | tmp[5];
	flags = tmp[0];
	pnp_register_mem_resource(dev, option_flags, base, base, 0, len, flags);
}

/*
 *  Parse card name for ISA PnP device.
 */
static void __init
isapnp_parse_name(char *name, unsigned int name_max, unsigned short *size)
{
	if (name[0] == '\0') {
		unsigned short size1 =
		    *size >= name_max ? (name_max - 1) : *size;
		isapnp_peek(name, size1);
		name[size1] = '\0';
		*size -= size1;

		/* clean whitespace from end of string */
		while (size1 > 0 && name[--size1] == ' ')
			name[size1] = '\0';
	}
}

/*
 *  Parse resource map for logical device.
 */
static int __init isapnp_create_device(struct pnp_card *card,
				       unsigned short size)
{
	int number = 0, skip = 0, priority, compat = 0;
	unsigned char type, tmp[17];
	unsigned int option_flags;
	struct pnp_dev *dev;
	u32 eisa_id;
	char id[8];

	if ((dev = isapnp_parse_device(card, size, number++)) == NULL)
		return 1;
	option_flags = 0;
	pnp_add_card_device(card, dev);

	while (1) {
		if (isapnp_read_tag(&type, &size) < 0)
			return 1;
		if (skip && type != _STAG_LOGDEVID && type != _STAG_END)
			goto __skip;
		switch (type) {
		case _STAG_LOGDEVID:
			if (size >= 5 && size <= 6) {
				if ((dev =
				     isapnp_parse_device(card, size,
							 number++)) == NULL)
					return 1;
				size = 0;
				skip = 0;
				option_flags = 0;
				pnp_add_card_device(card, dev);
			} else {
				skip = 1;
			}
			compat = 0;
			break;
		case _STAG_COMPATDEVID:
			if (size == 4 && compat < DEVICE_COUNT_COMPATIBLE) {
				isapnp_peek(tmp, 4);
				eisa_id = tmp[0] | tmp[1] << 8 |
					  tmp[2] << 16 | tmp[3] << 24;
				pnp_eisa_id_to_string(eisa_id, id);
				pnp_add_id(dev, id);
				compat++;
				size = 0;
			}
			break;
		case _STAG_IRQ:
			if (size < 2 || size > 3)
				goto __skip;
			isapnp_parse_irq_resource(dev, option_flags, size);
			size = 0;
			break;
		case _STAG_DMA:
			if (size != 2)
				goto __skip;
			isapnp_parse_dma_resource(dev, option_flags, size);
			size = 0;
			break;
		case _STAG_STARTDEP:
			if (size > 1)
				goto __skip;
			priority = PNP_RES_PRIORITY_ACCEPTABLE;
			if (size > 0) {
				isapnp_peek(tmp, size);
				priority = tmp[0];
				size = 0;
			}
			option_flags = pnp_new_dependent_set(dev, priority);
			break;
		case _STAG_ENDDEP:
			if (size != 0)
				goto __skip;
			option_flags = 0;
			break;
		case _STAG_IOPORT:
			if (size != 7)
				goto __skip;
			isapnp_parse_port_resource(dev, option_flags, size);
			size = 0;
			break;
		case _STAG_FIXEDIO:
			if (size != 3)
				goto __skip;
			isapnp_parse_fixed_port_resource(dev, option_flags,
							 size);
			size = 0;
			break;
		case _STAG_VENDOR:
			break;
		case _LTAG_MEMRANGE:
			if (size != 9)
				goto __skip;
			isapnp_parse_mem_resource(dev, option_flags, size);
			size = 0;
			break;
		case _LTAG_ANSISTR:
			isapnp_parse_name(dev->name, sizeof(dev->name), &size);
			break;
		case _LTAG_UNICODESTR:
			/* silently ignore */
			/* who use unicode for hardware identification? */
			break;
		case _LTAG_VENDOR:
			break;
		case _LTAG_MEM32RANGE:
			if (size != 17)
				goto __skip;
			isapnp_parse_mem32_resource(dev, option_flags, size);
			size = 0;
			break;
		case _LTAG_FIXEDMEM32RANGE:
			if (size != 9)
				goto __skip;
			isapnp_parse_fixed_mem32_resource(dev, option_flags,
							  size);
			size = 0;
			break;
		case _STAG_END:
			if (size > 0)
				isapnp_skip_bytes(size);
			return 1;
		default:
			dev_err(&dev->dev, "unknown tag %#x (card %i), "
				"ignored\n", type, card->number);
		}
__skip:
		if (size > 0)
			isapnp_skip_bytes(size);
	}
	return 0;
}

/*
 *  Parse resource map for ISA PnP card.
 */
static void __init isapnp_parse_resource_map(struct pnp_card *card)
{
	unsigned char type, tmp[17];
	unsigned short size;

	while (1) {
		if (isapnp_read_tag(&type, &size) < 0)
			return;
		switch (type) {
		case _STAG_PNPVERNO:
			if (size != 2)
				goto __skip;
			isapnp_peek(tmp, 2);
			card->pnpver = tmp[0];
			card->productver = tmp[1];
			size = 0;
			break;
		case _STAG_LOGDEVID:
			if (size >= 5 && size <= 6) {
				if (isapnp_create_device(card, size) == 1)
					return;
				size = 0;
			}
			break;
		case _STAG_VENDOR:
			break;
		case _LTAG_ANSISTR:
			isapnp_parse_name(card->name, sizeof(card->name),
					  &size);
			break;
		case _LTAG_UNICODESTR:
			/* silently ignore */
			/* who use unicode for hardware identification? */
			break;
		case _LTAG_VENDOR:
			break;
		case _STAG_END:
			if (size > 0)
				isapnp_skip_bytes(size);
			return;
		default:
			dev_err(&card->dev, "unknown tag %#x, ignored\n",
			       type);
		}
__skip:
		if (size > 0)
			isapnp_skip_bytes(size);
	}
}

/*
 *  Compute ISA PnP checksum for first eight bytes.
 */
static unsigned char __init isapnp_checksum(unsigned char *data)
{
	int i, j;
	unsigned char checksum = 0x6a, bit, b;

	for (i = 0; i < 8; i++) {
		b = data[i];
		for (j = 0; j < 8; j++) {
			bit = 0;
			if (b & (1 << j))
				bit = 1;
			checksum =
			    ((((checksum ^ (checksum >> 1)) & 0x01) ^ bit) << 7)
			    | (checksum >> 1);
		}
	}
	return checksum;
}

/*
 *  Build device list for all present ISA PnP devices.
 */
static int __init isapnp_build_device_list(void)
{
	int csn;
	unsigned char header[9], checksum;
	struct pnp_card *card;
	u32 eisa_id;
	char id[8];

	isapnp_wait();
	isapnp_key();
	for (csn = 1; csn <= isapnp_csn_count; csn++) {
		isapnp_wake(csn);
		isapnp_peek(header, 9);
		checksum = isapnp_checksum(header);
		eisa_id = header[0] | header[1] << 8 |
			  header[2] << 16 | header[3] << 24;
		pnp_eisa_id_to_string(eisa_id, id);
		card = pnp_alloc_card(&isapnp_protocol, csn, id);
		if (!card)
			continue;

#if 0
		dev_info(&card->dev,
		       "vendor: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       header[0], header[1], header[2], header[3], header[4],
		       header[5], header[6], header[7], header[8]);
		dev_info(&card->dev, "checksum = %#x\n", checksum);
#endif
		INIT_LIST_HEAD(&card->devices);
		card->serial =
		    (header[7] << 24) | (header[6] << 16) | (header[5] << 8) |
		    header[4];
		isapnp_checksum_value = 0x00;
		isapnp_parse_resource_map(card);
		if (isapnp_checksum_value != 0x00)
			dev_err(&card->dev, "invalid checksum %#x\n",
				isapnp_checksum_value);
		card->checksum = isapnp_checksum_value;

		pnp_add_card(card);
	}
	isapnp_wait();
	return 0;
}

/*
 *  Basic configuration routines.
 */

int isapnp_present(void)
{
	struct pnp_card *card;

	pnp_for_each_card(card) {
		if (card->protocol == &isapnp_protocol)
			return 1;
	}
	return 0;
}

int isapnp_cfg_begin(int csn, int logdev)
{
	if (csn < 1 || csn > isapnp_csn_count || logdev > 10)
		return -EINVAL;
	mutex_lock(&isapnp_cfg_mutex);
	isapnp_wait();
	isapnp_key();
	isapnp_wake(csn);
#if 0
	/* to avoid malfunction when the isapnptools package is used */
	/* we must set RDP to our value again */
	/* it is possible to set RDP only in the isolation phase */
	/*   Jens Thoms Toerring <Jens.Toerring@physik.fu-berlin.de> */
	isapnp_write_byte(0x02, 0x04);	/* clear CSN of card */
	mdelay(2);		/* is this necessary? */
	isapnp_wake(csn);	/* bring card into sleep state */
	isapnp_wake(0);		/* bring card into isolation state */
	isapnp_set_rdp();	/* reset the RDP port */
	udelay(1000);		/* delay 1000us */
	isapnp_write_byte(0x06, csn);	/* reset CSN to previous value */
	udelay(250);		/* is this necessary? */
#endif
	if (logdev >= 0)
		isapnp_device(logdev);
	return 0;
}

int isapnp_cfg_end(void)
{
	isapnp_wait();
	mutex_unlock(&isapnp_cfg_mutex);
	return 0;
}

/*
 *  Initialization.
 */

EXPORT_SYMBOL(isapnp_protocol);
EXPORT_SYMBOL(isapnp_present);
EXPORT_SYMBOL(isapnp_cfg_begin);
EXPORT_SYMBOL(isapnp_cfg_end);
EXPORT_SYMBOL(isapnp_write_byte);

static int isapnp_get_resources(struct pnp_dev *dev)
{
	int i, ret;

	dev_dbg(&dev->dev, "get resources\n");
	pnp_init_resources(dev);
	isapnp_cfg_begin(dev->card->number, dev->number);
	dev->active = isapnp_read_byte(ISAPNP_CFG_ACTIVATE);
	if (!dev->active)
		goto __end;

	for (i = 0; i < ISAPNP_MAX_PORT; i++) {
		ret = isapnp_read_word(ISAPNP_CFG_PORT + (i << 1));
		pnp_add_io_resource(dev, ret, ret,
				    ret == 0 ? IORESOURCE_DISABLED : 0);
	}
	for (i = 0; i < ISAPNP_MAX_MEM; i++) {
		ret = isapnp_read_word(ISAPNP_CFG_MEM + (i << 3)) << 8;
		pnp_add_mem_resource(dev, ret, ret,
				     ret == 0 ? IORESOURCE_DISABLED : 0);
	}
	for (i = 0; i < ISAPNP_MAX_IRQ; i++) {
		ret = isapnp_read_word(ISAPNP_CFG_IRQ + (i << 1)) >> 8;
		pnp_add_irq_resource(dev, ret,
				     ret == 0 ? IORESOURCE_DISABLED : 0);
	}
	for (i = 0; i < ISAPNP_MAX_DMA; i++) {
		ret = isapnp_read_byte(ISAPNP_CFG_DMA + i);
		pnp_add_dma_resource(dev, ret,
				     ret == 4 ? IORESOURCE_DISABLED : 0);
	}

__end:
	isapnp_cfg_end();
	return 0;
}

static int isapnp_set_resources(struct pnp_dev *dev)
{
	struct resource *res;
	int tmp;

	dev_dbg(&dev->dev, "set resources\n");
	isapnp_cfg_begin(dev->card->number, dev->number);
	dev->active = 1;
	for (tmp = 0; tmp < ISAPNP_MAX_PORT; tmp++) {
		res = pnp_get_resource(dev, IORESOURCE_IO, tmp);
		if (pnp_resource_enabled(res)) {
			dev_dbg(&dev->dev, "  set io  %d to %#llx\n",
				tmp, (unsigned long long) res->start);
			isapnp_write_word(ISAPNP_CFG_PORT + (tmp << 1),
					  res->start);
		}
	}
	for (tmp = 0; tmp < ISAPNP_MAX_IRQ; tmp++) {
		res = pnp_get_resource(dev, IORESOURCE_IRQ, tmp);
		if (pnp_resource_enabled(res)) {
			int irq = res->start;
			if (irq == 2)
				irq = 9;
			dev_dbg(&dev->dev, "  set irq %d to %d\n", tmp, irq);
			isapnp_write_byte(ISAPNP_CFG_IRQ + (tmp << 1), irq);
		}
	}
	for (tmp = 0; tmp < ISAPNP_MAX_DMA; tmp++) {
		res = pnp_get_resource(dev, IORESOURCE_DMA, tmp);
		if (pnp_resource_enabled(res)) {
			dev_dbg(&dev->dev, "  set dma %d to %lld\n",
				tmp, (unsigned long long) res->start);
			isapnp_write_byte(ISAPNP_CFG_DMA + tmp, res->start);
		}
	}
	for (tmp = 0; tmp < ISAPNP_MAX_MEM; tmp++) {
		res = pnp_get_resource(dev, IORESOURCE_MEM, tmp);
		if (pnp_resource_enabled(res)) {
			dev_dbg(&dev->dev, "  set mem %d to %#llx\n",
				tmp, (unsigned long long) res->start);
			isapnp_write_word(ISAPNP_CFG_MEM + (tmp << 3),
					  (res->start >> 8) & 0xffff);
		}
	}
	/* FIXME: We aren't handling 32bit mems properly here */
	isapnp_activate(dev->number);
	isapnp_cfg_end();
	return 0;
}

static int isapnp_disable_resources(struct pnp_dev *dev)
{
	if (!dev->active)
		return -EINVAL;
	isapnp_cfg_begin(dev->card->number, dev->number);
	isapnp_deactivate(dev->number);
	dev->active = 0;
	isapnp_cfg_end();
	return 0;
}

struct pnp_protocol isapnp_protocol = {
	.name = "ISA Plug and Play",
	.get = isapnp_get_resources,
	.set = isapnp_set_resources,
	.disable = isapnp_disable_resources,
};

static int __init isapnp_init(void)
{
	int cards;
	struct pnp_card *card;
	struct pnp_dev *dev;

	if (isapnp_disable) {
		printk(KERN_INFO "isapnp: ISA Plug & Play support disabled\n");
		return 0;
	}
#ifdef CONFIG_PPC
	if (check_legacy_ioport(_PIDXR) || check_legacy_ioport(_PNPWRP))
		return -EINVAL;
#endif
#ifdef ISAPNP_REGION_OK
	if (!request_region(_PIDXR, 1, "isapnp index")) {
		printk(KERN_ERR "isapnp: Index Register 0x%x already used\n",
		       _PIDXR);
		return -EBUSY;
	}
#endif
	if (!request_region(_PNPWRP, 1, "isapnp write")) {
		printk(KERN_ERR
		       "isapnp: Write Data Register 0x%x already used\n",
		       _PNPWRP);
#ifdef ISAPNP_REGION_OK
		release_region(_PIDXR, 1);
#endif
		return -EBUSY;
	}

	if (pnp_register_protocol(&isapnp_protocol) < 0)
		return -EBUSY;

	/*
	 *      Print a message. The existing ISAPnP code is hanging machines
	 *      so let the user know where.
	 */

	printk(KERN_INFO "isapnp: Scanning for PnP cards...\n");
	if (isapnp_rdp >= 0x203 && isapnp_rdp <= 0x3ff) {
		isapnp_rdp |= 3;
		if (!request_region(isapnp_rdp, 1, "isapnp read")) {
			printk(KERN_ERR
			       "isapnp: Read Data Register 0x%x already used\n",
			       isapnp_rdp);
#ifdef ISAPNP_REGION_OK
			release_region(_PIDXR, 1);
#endif
			release_region(_PNPWRP, 1);
			return -EBUSY;
		}
		isapnp_set_rdp();
	}
	if (isapnp_rdp < 0x203 || isapnp_rdp > 0x3ff) {
		cards = isapnp_isolate();
		if (cards < 0 || (isapnp_rdp < 0x203 || isapnp_rdp > 0x3ff)) {
#ifdef ISAPNP_REGION_OK
			release_region(_PIDXR, 1);
#endif
			release_region(_PNPWRP, 1);
			printk(KERN_INFO
			       "isapnp: No Plug & Play device found\n");
			return 0;
		}
		request_region(isapnp_rdp, 1, "isapnp read");
	}
	isapnp_build_device_list();
	cards = 0;

	protocol_for_each_card(&isapnp_protocol, card) {
		cards++;
		if (isapnp_verbose) {
			dev_info(&card->dev, "card '%s'\n",
			       card->name[0] ? card->name : "unknown");
			if (isapnp_verbose < 2)
				continue;
			card_for_each_dev(card, dev) {
				dev_info(&card->dev, "device '%s'\n",
				       dev->name[0] ? dev->name : "unknown");
			}
		}
	}
	if (cards)
		printk(KERN_INFO
		       "isapnp: %i Plug & Play card%s detected total\n", cards,
		       cards > 1 ? "s" : "");
	else
		printk(KERN_INFO "isapnp: No Plug & Play card found\n");

	isapnp_proc_init();
	return 0;
}

device_initcall(isapnp_init);

/* format is: noisapnp */

static int __init isapnp_setup_disable(char *str)
{
	isapnp_disable = 1;
	return 1;
}

__setup("noisapnp", isapnp_setup_disable);

/* format is: isapnp=rdp,reset,skip_pci_scan,verbose */

static int __init isapnp_setup_isapnp(char *str)
{
	(void)((get_option(&str, &isapnp_rdp) == 2) &&
	       (get_option(&str, &isapnp_reset) == 2) &&
	       (get_option(&str, &isapnp_verbose) == 2));
	return 1;
}

__setup("isapnp=", isapnp_setup_isapnp);
