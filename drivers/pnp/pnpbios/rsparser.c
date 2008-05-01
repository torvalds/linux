/*
 * rsparser.c - parses and encodes pnpbios resource data streams
 */

#include <linux/ctype.h>
#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/slab.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#else
inline void pcibios_penalize_isa_irq(int irq, int active)
{
}
#endif				/* CONFIG_PCI */

#include "../base.h"
#include "pnpbios.h"

/* standard resource tags */
#define SMALL_TAG_PNPVERNO		0x01
#define SMALL_TAG_LOGDEVID		0x02
#define SMALL_TAG_COMPATDEVID		0x03
#define SMALL_TAG_IRQ			0x04
#define SMALL_TAG_DMA			0x05
#define SMALL_TAG_STARTDEP		0x06
#define SMALL_TAG_ENDDEP		0x07
#define SMALL_TAG_PORT			0x08
#define SMALL_TAG_FIXEDPORT		0x09
#define SMALL_TAG_VENDOR		0x0e
#define SMALL_TAG_END			0x0f
#define LARGE_TAG			0x80
#define LARGE_TAG_MEM			0x81
#define LARGE_TAG_ANSISTR		0x82
#define LARGE_TAG_UNICODESTR		0x83
#define LARGE_TAG_VENDOR		0x84
#define LARGE_TAG_MEM32			0x85
#define LARGE_TAG_FIXEDMEM32		0x86

/*
 * Resource Data Stream Format:
 *
 * Allocated Resources (required)
 * end tag ->
 * Resource Configuration Options (optional)
 * end tag ->
 * Compitable Device IDs (optional)
 * final end tag ->
 */

/*
 * Allocated Resources
 */

static void pnpbios_parse_allocated_ioresource(struct pnp_dev *dev,
					       int start, int len)
{
	int flags = 0;
	int end = start + len - 1;

	if (len <= 0 || end >= 0x10003)
		flags |= IORESOURCE_DISABLED;

	pnp_add_io_resource(dev, start, end, flags);
}

static void pnpbios_parse_allocated_memresource(struct pnp_dev *dev,
						int start, int len)
{
	int flags = 0;
	int end = start + len - 1;

	if (len <= 0)
		flags |= IORESOURCE_DISABLED;

	pnp_add_mem_resource(dev, start, end, flags);
}

static unsigned char *pnpbios_parse_allocated_resource_data(struct pnp_dev *dev,
							    unsigned char *p,
							    unsigned char *end)
{
	unsigned int len, tag;
	int io, size, mask, i, flags;

	if (!p)
		return NULL;

	dev_dbg(&dev->dev, "parse allocated resources\n");

	pnp_init_resources(dev);

	while ((char *)p < (char *)end) {

		/* determine the type of tag */
		if (p[0] & LARGE_TAG) {	/* large tag */
			len = (p[2] << 8) | p[1];
			tag = p[0];
		} else {	/* small tag */
			len = p[0] & 0x07;
			tag = ((p[0] >> 3) & 0x0f);
		}

		switch (tag) {

		case LARGE_TAG_MEM:
			if (len != 9)
				goto len_err;
			io = *(short *)&p[4];
			size = *(short *)&p[10];
			pnpbios_parse_allocated_memresource(dev, io, size);
			break;

		case LARGE_TAG_ANSISTR:
			/* ignore this for now */
			break;

		case LARGE_TAG_VENDOR:
			/* do nothing */
			break;

		case LARGE_TAG_MEM32:
			if (len != 17)
				goto len_err;
			io = *(int *)&p[4];
			size = *(int *)&p[16];
			pnpbios_parse_allocated_memresource(dev, io, size);
			break;

		case LARGE_TAG_FIXEDMEM32:
			if (len != 9)
				goto len_err;
			io = *(int *)&p[4];
			size = *(int *)&p[8];
			pnpbios_parse_allocated_memresource(dev, io, size);
			break;

		case SMALL_TAG_IRQ:
			if (len < 2 || len > 3)
				goto len_err;
			flags = 0;
			io = -1;
			mask = p[1] + p[2] * 256;
			for (i = 0; i < 16; i++, mask = mask >> 1)
				if (mask & 0x01)
					io = i;
			if (io != -1)
				pcibios_penalize_isa_irq(io, 1);
			else
				flags = IORESOURCE_DISABLED;
			pnp_add_irq_resource(dev, io, flags);
			break;

		case SMALL_TAG_DMA:
			if (len != 2)
				goto len_err;
			flags = 0;
			io = -1;
			mask = p[1];
			for (i = 0; i < 8; i++, mask = mask >> 1)
				if (mask & 0x01)
					io = i;
			if (io == -1)
				flags = IORESOURCE_DISABLED;
			pnp_add_dma_resource(dev, io, flags);
			break;

		case SMALL_TAG_PORT:
			if (len != 7)
				goto len_err;
			io = p[2] + p[3] * 256;
			size = p[7];
			pnpbios_parse_allocated_ioresource(dev, io, size);
			break;

		case SMALL_TAG_VENDOR:
			/* do nothing */
			break;

		case SMALL_TAG_FIXEDPORT:
			if (len != 3)
				goto len_err;
			io = p[1] + p[2] * 256;
			size = p[3];
			pnpbios_parse_allocated_ioresource(dev, io, size);
			break;

		case SMALL_TAG_END:
			p = p + 2;
			return (unsigned char *)p;
			break;

		default:	/* an unkown tag */
len_err:
			dev_err(&dev->dev, "unknown tag %#x length %d\n",
				tag, len);
			break;
		}

		/* continue to the next tag */
		if (p[0] & LARGE_TAG)
			p += len + 3;
		else
			p += len + 1;
	}

	dev_err(&dev->dev, "no end tag in resource structure\n");

	return NULL;
}

/*
 * Resource Configuration Options
 */

static __init void pnpbios_parse_mem_option(struct pnp_dev *dev,
					    unsigned char *p, int size,
					    struct pnp_option *option)
{
	struct pnp_mem *mem;

	mem = kzalloc(sizeof(struct pnp_mem), GFP_KERNEL);
	if (!mem)
		return;
	mem->min = ((p[5] << 8) | p[4]) << 8;
	mem->max = ((p[7] << 8) | p[6]) << 8;
	mem->align = (p[9] << 8) | p[8];
	mem->size = ((p[11] << 8) | p[10]) << 8;
	mem->flags = p[3];
	pnp_register_mem_resource(dev, option, mem);
}

static __init void pnpbios_parse_mem32_option(struct pnp_dev *dev,
					      unsigned char *p, int size,
					      struct pnp_option *option)
{
	struct pnp_mem *mem;

	mem = kzalloc(sizeof(struct pnp_mem), GFP_KERNEL);
	if (!mem)
		return;
	mem->min = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
	mem->max = (p[11] << 24) | (p[10] << 16) | (p[9] << 8) | p[8];
	mem->align = (p[15] << 24) | (p[14] << 16) | (p[13] << 8) | p[12];
	mem->size = (p[19] << 24) | (p[18] << 16) | (p[17] << 8) | p[16];
	mem->flags = p[3];
	pnp_register_mem_resource(dev, option, mem);
}

static __init void pnpbios_parse_fixed_mem32_option(struct pnp_dev *dev,
						    unsigned char *p, int size,
						    struct pnp_option *option)
{
	struct pnp_mem *mem;

	mem = kzalloc(sizeof(struct pnp_mem), GFP_KERNEL);
	if (!mem)
		return;
	mem->min = mem->max = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
	mem->size = (p[11] << 24) | (p[10] << 16) | (p[9] << 8) | p[8];
	mem->align = 0;
	mem->flags = p[3];
	pnp_register_mem_resource(dev, option, mem);
}

static __init void pnpbios_parse_irq_option(struct pnp_dev *dev,
					    unsigned char *p, int size,
					    struct pnp_option *option)
{
	struct pnp_irq *irq;
	unsigned long bits;

	irq = kzalloc(sizeof(struct pnp_irq), GFP_KERNEL);
	if (!irq)
		return;
	bits = (p[2] << 8) | p[1];
	bitmap_copy(irq->map, &bits, 16);
	if (size > 2)
		irq->flags = p[3];
	else
		irq->flags = IORESOURCE_IRQ_HIGHEDGE;
	pnp_register_irq_resource(dev, option, irq);
}

static __init void pnpbios_parse_dma_option(struct pnp_dev *dev,
					    unsigned char *p, int size,
					    struct pnp_option *option)
{
	struct pnp_dma *dma;

	dma = kzalloc(sizeof(struct pnp_dma), GFP_KERNEL);
	if (!dma)
		return;
	dma->map = p[1];
	dma->flags = p[2];
	pnp_register_dma_resource(dev, option, dma);
}

static __init void pnpbios_parse_port_option(struct pnp_dev *dev,
					     unsigned char *p, int size,
					     struct pnp_option *option)
{
	struct pnp_port *port;

	port = kzalloc(sizeof(struct pnp_port), GFP_KERNEL);
	if (!port)
		return;
	port->min = (p[3] << 8) | p[2];
	port->max = (p[5] << 8) | p[4];
	port->align = p[6];
	port->size = p[7];
	port->flags = p[1] ? PNP_PORT_FLAG_16BITADDR : 0;
	pnp_register_port_resource(dev, option, port);
}

static __init void pnpbios_parse_fixed_port_option(struct pnp_dev *dev,
						   unsigned char *p, int size,
						   struct pnp_option *option)
{
	struct pnp_port *port;

	port = kzalloc(sizeof(struct pnp_port), GFP_KERNEL);
	if (!port)
		return;
	port->min = port->max = (p[2] << 8) | p[1];
	port->size = p[3];
	port->align = 0;
	port->flags = PNP_PORT_FLAG_FIXED;
	pnp_register_port_resource(dev, option, port);
}

static __init unsigned char *
pnpbios_parse_resource_option_data(unsigned char *p, unsigned char *end,
					struct pnp_dev *dev)
{
	unsigned int len, tag;
	int priority = 0;
	struct pnp_option *option, *option_independent;

	if (!p)
		return NULL;

	dev_dbg(&dev->dev, "parse resource options\n");

	option_independent = option = pnp_register_independent_option(dev);
	if (!option)
		return NULL;

	while ((char *)p < (char *)end) {

		/* determine the type of tag */
		if (p[0] & LARGE_TAG) {	/* large tag */
			len = (p[2] << 8) | p[1];
			tag = p[0];
		} else {	/* small tag */
			len = p[0] & 0x07;
			tag = ((p[0] >> 3) & 0x0f);
		}

		switch (tag) {

		case LARGE_TAG_MEM:
			if (len != 9)
				goto len_err;
			pnpbios_parse_mem_option(dev, p, len, option);
			break;

		case LARGE_TAG_MEM32:
			if (len != 17)
				goto len_err;
			pnpbios_parse_mem32_option(dev, p, len, option);
			break;

		case LARGE_TAG_FIXEDMEM32:
			if (len != 9)
				goto len_err;
			pnpbios_parse_fixed_mem32_option(dev, p, len, option);
			break;

		case SMALL_TAG_IRQ:
			if (len < 2 || len > 3)
				goto len_err;
			pnpbios_parse_irq_option(dev, p, len, option);
			break;

		case SMALL_TAG_DMA:
			if (len != 2)
				goto len_err;
			pnpbios_parse_dma_option(dev, p, len, option);
			break;

		case SMALL_TAG_PORT:
			if (len != 7)
				goto len_err;
			pnpbios_parse_port_option(dev, p, len, option);
			break;

		case SMALL_TAG_VENDOR:
			/* do nothing */
			break;

		case SMALL_TAG_FIXEDPORT:
			if (len != 3)
				goto len_err;
			pnpbios_parse_fixed_port_option(dev, p, len, option);
			break;

		case SMALL_TAG_STARTDEP:
			if (len > 1)
				goto len_err;
			priority = 0x100 | PNP_RES_PRIORITY_ACCEPTABLE;
			if (len > 0)
				priority = 0x100 | p[1];
			option = pnp_register_dependent_option(dev, priority);
			if (!option)
				return NULL;
			break;

		case SMALL_TAG_ENDDEP:
			if (len != 0)
				goto len_err;
			if (option_independent == option)
				dev_warn(&dev->dev, "missing "
					 "SMALL_TAG_STARTDEP tag\n");
			option = option_independent;
			dev_dbg(&dev->dev, "end dependent options\n");
			break;

		case SMALL_TAG_END:
			return p + 2;

		default:	/* an unkown tag */
len_err:
			dev_err(&dev->dev, "unknown tag %#x length %d\n",
				tag, len);
			break;
		}

		/* continue to the next tag */
		if (p[0] & LARGE_TAG)
			p += len + 3;
		else
			p += len + 1;
	}

	dev_err(&dev->dev, "no end tag in resource structure\n");

	return NULL;
}

/*
 * Compatible Device IDs
 */

static unsigned char *pnpbios_parse_compatible_ids(unsigned char *p,
						   unsigned char *end,
						   struct pnp_dev *dev)
{
	int len, tag;
	u32 eisa_id;
	char id[8];
	struct pnp_id *dev_id;

	if (!p)
		return NULL;

	while ((char *)p < (char *)end) {

		/* determine the type of tag */
		if (p[0] & LARGE_TAG) {	/* large tag */
			len = (p[2] << 8) | p[1];
			tag = p[0];
		} else {	/* small tag */
			len = p[0] & 0x07;
			tag = ((p[0] >> 3) & 0x0f);
		}

		switch (tag) {

		case LARGE_TAG_ANSISTR:
			strncpy(dev->name, p + 3,
				len >= PNP_NAME_LEN ? PNP_NAME_LEN - 2 : len);
			dev->name[len >=
				  PNP_NAME_LEN ? PNP_NAME_LEN - 1 : len] = '\0';
			break;

		case SMALL_TAG_COMPATDEVID:	/* compatible ID */
			if (len != 4)
				goto len_err;
			eisa_id = p[1] | p[2] << 8 | p[3] << 16 | p[4] << 24;
			pnp_eisa_id_to_string(eisa_id & PNP_EISA_ID_MASK, id);
			dev_id = pnp_add_id(dev, id);
			if (!dev_id)
				return NULL;
			break;

		case SMALL_TAG_END:
			p = p + 2;
			return (unsigned char *)p;
			break;

		default:	/* an unkown tag */
len_err:
			dev_err(&dev->dev, "unknown tag %#x length %d\n",
				tag, len);
			break;
		}

		/* continue to the next tag */
		if (p[0] & LARGE_TAG)
			p += len + 3;
		else
			p += len + 1;
	}

	dev_err(&dev->dev, "no end tag in resource structure\n");

	return NULL;
}

/*
 * Allocated Resource Encoding
 */

static void pnpbios_encode_mem(struct pnp_dev *dev, unsigned char *p,
			       struct resource *res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;

	p[4] = (base >> 8) & 0xff;
	p[5] = ((base >> 8) >> 8) & 0xff;
	p[6] = (base >> 8) & 0xff;
	p[7] = ((base >> 8) >> 8) & 0xff;
	p[10] = (len >> 8) & 0xff;
	p[11] = ((len >> 8) >> 8) & 0xff;

	dev_dbg(&dev->dev, "  encode mem %#llx-%#llx\n",
		(unsigned long long) res->start, (unsigned long long) res->end);
}

static void pnpbios_encode_mem32(struct pnp_dev *dev, unsigned char *p,
				 struct resource *res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;

	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[6] = (base >> 16) & 0xff;
	p[7] = (base >> 24) & 0xff;
	p[8] = base & 0xff;
	p[9] = (base >> 8) & 0xff;
	p[10] = (base >> 16) & 0xff;
	p[11] = (base >> 24) & 0xff;
	p[16] = len & 0xff;
	p[17] = (len >> 8) & 0xff;
	p[18] = (len >> 16) & 0xff;
	p[19] = (len >> 24) & 0xff;

	dev_dbg(&dev->dev, "  encode mem32 %#llx-%#llx\n",
		(unsigned long long) res->start, (unsigned long long) res->end);
}

static void pnpbios_encode_fixed_mem32(struct pnp_dev *dev, unsigned char *p,
				       struct resource *res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;

	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[6] = (base >> 16) & 0xff;
	p[7] = (base >> 24) & 0xff;
	p[8] = len & 0xff;
	p[9] = (len >> 8) & 0xff;
	p[10] = (len >> 16) & 0xff;
	p[11] = (len >> 24) & 0xff;

	dev_dbg(&dev->dev, "  encode fixed_mem32 %#llx-%#llx\n",
		(unsigned long long) res->start, (unsigned long long) res->end);
}

static void pnpbios_encode_irq(struct pnp_dev *dev, unsigned char *p,
			       struct resource *res)
{
	unsigned long map = 0;

	map = 1 << res->start;
	p[1] = map & 0xff;
	p[2] = (map >> 8) & 0xff;

	dev_dbg(&dev->dev, "  encode irq %llu\n",
		(unsigned long long)res->start);
}

static void pnpbios_encode_dma(struct pnp_dev *dev, unsigned char *p,
			       struct resource *res)
{
	unsigned long map = 0;

	map = 1 << res->start;
	p[1] = map & 0xff;

	dev_dbg(&dev->dev, "  encode dma %llu\n",
		(unsigned long long)res->start);
}

static void pnpbios_encode_port(struct pnp_dev *dev, unsigned char *p,
				struct resource *res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;

	p[2] = base & 0xff;
	p[3] = (base >> 8) & 0xff;
	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[7] = len & 0xff;

	dev_dbg(&dev->dev, "  encode io %#llx-%#llx\n",
		(unsigned long long) res->start, (unsigned long long) res->end);
}

static void pnpbios_encode_fixed_port(struct pnp_dev *dev, unsigned char *p,
				      struct resource *res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;

	p[1] = base & 0xff;
	p[2] = (base >> 8) & 0xff;
	p[3] = len & 0xff;

	dev_dbg(&dev->dev, "  encode fixed_io %#llx-%#llx\n",
		(unsigned long long) res->start, (unsigned long long) res->end);
}

static unsigned char *pnpbios_encode_allocated_resource_data(struct pnp_dev
								*dev,
							     unsigned char *p,
							     unsigned char *end)
{
	unsigned int len, tag;
	int port = 0, irq = 0, dma = 0, mem = 0;

	if (!p)
		return NULL;

	while ((char *)p < (char *)end) {

		/* determine the type of tag */
		if (p[0] & LARGE_TAG) {	/* large tag */
			len = (p[2] << 8) | p[1];
			tag = p[0];
		} else {	/* small tag */
			len = p[0] & 0x07;
			tag = ((p[0] >> 3) & 0x0f);
		}

		switch (tag) {

		case LARGE_TAG_MEM:
			if (len != 9)
				goto len_err;
			pnpbios_encode_mem(dev, p,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;

		case LARGE_TAG_MEM32:
			if (len != 17)
				goto len_err;
			pnpbios_encode_mem32(dev, p,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;

		case LARGE_TAG_FIXEDMEM32:
			if (len != 9)
				goto len_err;
			pnpbios_encode_fixed_mem32(dev, p,
				pnp_get_resource(dev, IORESOURCE_MEM, mem));
			mem++;
			break;

		case SMALL_TAG_IRQ:
			if (len < 2 || len > 3)
				goto len_err;
			pnpbios_encode_irq(dev, p,
				pnp_get_resource(dev, IORESOURCE_IRQ, irq));
			irq++;
			break;

		case SMALL_TAG_DMA:
			if (len != 2)
				goto len_err;
			pnpbios_encode_dma(dev, p,
				pnp_get_resource(dev, IORESOURCE_DMA, dma));
			dma++;
			break;

		case SMALL_TAG_PORT:
			if (len != 7)
				goto len_err;
			pnpbios_encode_port(dev, p,
				pnp_get_resource(dev, IORESOURCE_IO, port));
			port++;
			break;

		case SMALL_TAG_VENDOR:
			/* do nothing */
			break;

		case SMALL_TAG_FIXEDPORT:
			if (len != 3)
				goto len_err;
			pnpbios_encode_fixed_port(dev, p,
				pnp_get_resource(dev, IORESOURCE_IO, port));
			port++;
			break;

		case SMALL_TAG_END:
			p = p + 2;
			return (unsigned char *)p;
			break;

		default:	/* an unkown tag */
len_err:
			dev_err(&dev->dev, "unknown tag %#x length %d\n",
				tag, len);
			break;
		}

		/* continue to the next tag */
		if (p[0] & LARGE_TAG)
			p += len + 3;
		else
			p += len + 1;
	}

	dev_err(&dev->dev, "no end tag in resource structure\n");

	return NULL;
}

/*
 * Core Parsing Functions
 */

int __init pnpbios_parse_data_stream(struct pnp_dev *dev,
					struct pnp_bios_node *node)
{
	unsigned char *p = (char *)node->data;
	unsigned char *end = (char *)(node->data + node->size);

	p = pnpbios_parse_allocated_resource_data(dev, p, end);
	if (!p)
		return -EIO;
	p = pnpbios_parse_resource_option_data(p, end, dev);
	if (!p)
		return -EIO;
	p = pnpbios_parse_compatible_ids(p, end, dev);
	if (!p)
		return -EIO;
	return 0;
}

int pnpbios_read_resources_from_node(struct pnp_dev *dev,
				     struct pnp_bios_node *node)
{
	unsigned char *p = (char *)node->data;
	unsigned char *end = (char *)(node->data + node->size);

	p = pnpbios_parse_allocated_resource_data(dev, p, end);
	if (!p)
		return -EIO;
	return 0;
}

int pnpbios_write_resources_to_node(struct pnp_dev *dev,
				    struct pnp_bios_node *node)
{
	unsigned char *p = (char *)node->data;
	unsigned char *end = (char *)(node->data + node->size);

	p = pnpbios_encode_allocated_resource_data(dev, p, end);
	if (!p)
		return -EIO;
	return 0;
}
