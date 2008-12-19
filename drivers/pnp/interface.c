/*
 * interface.c - contains everything related to the user interface
 *
 * Some code, especially possible resource dumping is based on isapnp_proc.c (c) Jaroslav Kysela <perex@perex.cz>
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */

#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>

#include "base.h"

struct pnp_info_buffer {
	char *buffer;		/* pointer to begin of buffer */
	char *curr;		/* current position in buffer */
	unsigned long size;	/* current size */
	unsigned long len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

typedef struct pnp_info_buffer pnp_info_buffer_t;

static int pnp_printf(pnp_info_buffer_t * buffer, char *fmt, ...)
{
	va_list args;
	int res;

	if (buffer->stop || buffer->error)
		return 0;
	va_start(args, fmt);
	res = vsnprintf(buffer->curr, buffer->len - buffer->size, fmt, args);
	va_end(args);
	if (buffer->size + res >= buffer->len) {
		buffer->stop = 1;
		return 0;
	}
	buffer->curr += res;
	buffer->size += res;
	return res;
}

static void pnp_print_port(pnp_info_buffer_t * buffer, char *space,
			   struct pnp_port *port)
{
	pnp_printf(buffer, "%sport %#llx-%#llx, align %#llx, size %#llx, "
		   "%i-bit address decoding\n", space,
		   (unsigned long long) port->min,
		   (unsigned long long) port->max,
		   port->align ? ((unsigned long long) port->align - 1) : 0,
		   (unsigned long long) port->size,
		   port->flags & IORESOURCE_IO_16BIT_ADDR ? 16 : 10);
}

static void pnp_print_irq(pnp_info_buffer_t * buffer, char *space,
			  struct pnp_irq *irq)
{
	int first = 1, i;

	pnp_printf(buffer, "%sirq ", space);
	for (i = 0; i < PNP_IRQ_NR; i++)
		if (test_bit(i, irq->map.bits)) {
			if (!first) {
				pnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			if (i == 2 || i == 9)
				pnp_printf(buffer, "2/9");
			else
				pnp_printf(buffer, "%i", i);
		}
	if (bitmap_empty(irq->map.bits, PNP_IRQ_NR))
		pnp_printf(buffer, "<none>");
	if (irq->flags & IORESOURCE_IRQ_HIGHEDGE)
		pnp_printf(buffer, " High-Edge");
	if (irq->flags & IORESOURCE_IRQ_LOWEDGE)
		pnp_printf(buffer, " Low-Edge");
	if (irq->flags & IORESOURCE_IRQ_HIGHLEVEL)
		pnp_printf(buffer, " High-Level");
	if (irq->flags & IORESOURCE_IRQ_LOWLEVEL)
		pnp_printf(buffer, " Low-Level");
	if (irq->flags & IORESOURCE_IRQ_OPTIONAL)
		pnp_printf(buffer, " (optional)");
	pnp_printf(buffer, "\n");
}

static void pnp_print_dma(pnp_info_buffer_t * buffer, char *space,
			  struct pnp_dma *dma)
{
	int first = 1, i;
	char *s;

	pnp_printf(buffer, "%sdma ", space);
	for (i = 0; i < 8; i++)
		if (dma->map & (1 << i)) {
			if (!first) {
				pnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			pnp_printf(buffer, "%i", i);
		}
	if (!dma->map)
		pnp_printf(buffer, "<none>");
	switch (dma->flags & IORESOURCE_DMA_TYPE_MASK) {
	case IORESOURCE_DMA_8BIT:
		s = "8-bit";
		break;
	case IORESOURCE_DMA_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	default:
		s = "16-bit";
	}
	pnp_printf(buffer, " %s", s);
	if (dma->flags & IORESOURCE_DMA_MASTER)
		pnp_printf(buffer, " master");
	if (dma->flags & IORESOURCE_DMA_BYTE)
		pnp_printf(buffer, " byte-count");
	if (dma->flags & IORESOURCE_DMA_WORD)
		pnp_printf(buffer, " word-count");
	switch (dma->flags & IORESOURCE_DMA_SPEED_MASK) {
	case IORESOURCE_DMA_TYPEA:
		s = "type-A";
		break;
	case IORESOURCE_DMA_TYPEB:
		s = "type-B";
		break;
	case IORESOURCE_DMA_TYPEF:
		s = "type-F";
		break;
	default:
		s = "compatible";
		break;
	}
	pnp_printf(buffer, " %s\n", s);
}

static void pnp_print_mem(pnp_info_buffer_t * buffer, char *space,
			  struct pnp_mem *mem)
{
	char *s;

	pnp_printf(buffer, "%sMemory %#llx-%#llx, align %#llx, size %#llx",
		   space, (unsigned long long) mem->min,
		   (unsigned long long) mem->max,
		   (unsigned long long) mem->align,
		   (unsigned long long) mem->size);
	if (mem->flags & IORESOURCE_MEM_WRITEABLE)
		pnp_printf(buffer, ", writeable");
	if (mem->flags & IORESOURCE_MEM_CACHEABLE)
		pnp_printf(buffer, ", cacheable");
	if (mem->flags & IORESOURCE_MEM_RANGELENGTH)
		pnp_printf(buffer, ", range-length");
	if (mem->flags & IORESOURCE_MEM_SHADOWABLE)
		pnp_printf(buffer, ", shadowable");
	if (mem->flags & IORESOURCE_MEM_EXPANSIONROM)
		pnp_printf(buffer, ", expansion ROM");
	switch (mem->flags & IORESOURCE_MEM_TYPE_MASK) {
	case IORESOURCE_MEM_8BIT:
		s = "8-bit";
		break;
	case IORESOURCE_MEM_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	case IORESOURCE_MEM_32BIT:
		s = "32-bit";
		break;
	default:
		s = "16-bit";
	}
	pnp_printf(buffer, ", %s\n", s);
}

static void pnp_print_option(pnp_info_buffer_t * buffer, char *space,
			     struct pnp_option *option)
{
	switch (option->type) {
	case IORESOURCE_IO:
		pnp_print_port(buffer, space, &option->u.port);
		break;
	case IORESOURCE_MEM:
		pnp_print_mem(buffer, space, &option->u.mem);
		break;
	case IORESOURCE_IRQ:
		pnp_print_irq(buffer, space, &option->u.irq);
		break;
	case IORESOURCE_DMA:
		pnp_print_dma(buffer, space, &option->u.dma);
		break;
	}
}

static ssize_t pnp_show_options(struct device *dmdev,
				struct device_attribute *attr, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	pnp_info_buffer_t *buffer;
	struct pnp_option *option;
	int ret, dep = 0, set = 0;
	char *indent;

	buffer = pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;

	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;

	list_for_each_entry(option, &dev->options, list) {
		if (pnp_option_is_dependent(option)) {
			indent = "  ";
			if (!dep || pnp_option_set(option) != set) {
				set = pnp_option_set(option);
				dep = 1;
				pnp_printf(buffer, "Dependent: %02i - "
					   "Priority %s\n", set,
					   pnp_option_priority_name(option));
			}
		} else {
			dep = 0;
			indent = "";
		}
		pnp_print_option(buffer, indent, option);
	}

	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

static ssize_t pnp_show_current_resources(struct device *dmdev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	pnp_info_buffer_t *buffer;
	struct pnp_resource *pnp_res;
	struct resource *res;
	int ret;

	if (!dev)
		return -EINVAL;

	buffer = pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;

	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;

	pnp_printf(buffer, "state = %s\n", dev->active ? "active" : "disabled");

	list_for_each_entry(pnp_res, &dev->resources, list) {
		res = &pnp_res->res;

		pnp_printf(buffer, pnp_resource_type_name(res));

		if (res->flags & IORESOURCE_DISABLED) {
			pnp_printf(buffer, " disabled\n");
			continue;
		}

		switch (pnp_resource_type(res)) {
		case IORESOURCE_IO:
		case IORESOURCE_MEM:
			pnp_printf(buffer, " %#llx-%#llx\n",
				   (unsigned long long) res->start,
				   (unsigned long long) res->end);
			break;
		case IORESOURCE_IRQ:
		case IORESOURCE_DMA:
			pnp_printf(buffer, " %lld\n",
				   (unsigned long long) res->start);
			break;
		}
	}

	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

static ssize_t pnp_set_current_resources(struct device *dmdev,
					 struct device_attribute *attr,
					 const char *ubuf, size_t count)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char *buf = (void *)ubuf;
	int retval = 0;
	resource_size_t start, end;

	if (dev->status & PNP_ATTACHED) {
		retval = -EBUSY;
		dev_info(&dev->dev, "in use; can't configure\n");
		goto done;
	}

	while (isspace(*buf))
		++buf;
	if (!strnicmp(buf, "disable", 7)) {
		retval = pnp_disable_dev(dev);
		goto done;
	}
	if (!strnicmp(buf, "activate", 8)) {
		retval = pnp_activate_dev(dev);
		goto done;
	}
	if (!strnicmp(buf, "fill", 4)) {
		if (dev->active)
			goto done;
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strnicmp(buf, "auto", 4)) {
		if (dev->active)
			goto done;
		pnp_init_resources(dev);
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strnicmp(buf, "clear", 5)) {
		if (dev->active)
			goto done;
		pnp_init_resources(dev);
		goto done;
	}
	if (!strnicmp(buf, "get", 3)) {
		mutex_lock(&pnp_res_mutex);
		if (pnp_can_read(dev))
			dev->protocol->get(dev);
		mutex_unlock(&pnp_res_mutex);
		goto done;
	}
	if (!strnicmp(buf, "set", 3)) {
		if (dev->active)
			goto done;
		buf += 3;
		pnp_init_resources(dev);
		mutex_lock(&pnp_res_mutex);
		while (1) {
			while (isspace(*buf))
				++buf;
			if (!strnicmp(buf, "io", 2)) {
				buf += 2;
				while (isspace(*buf))
					++buf;
				start = simple_strtoul(buf, &buf, 0);
				while (isspace(*buf))
					++buf;
				if (*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					end = simple_strtoul(buf, &buf, 0);
				} else
					end = start;
				pnp_add_io_resource(dev, start, end, 0);
				continue;
			}
			if (!strnicmp(buf, "mem", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				start = simple_strtoul(buf, &buf, 0);
				while (isspace(*buf))
					++buf;
				if (*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					end = simple_strtoul(buf, &buf, 0);
				} else
					end = start;
				pnp_add_mem_resource(dev, start, end, 0);
				continue;
			}
			if (!strnicmp(buf, "irq", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				start = simple_strtoul(buf, &buf, 0);
				pnp_add_irq_resource(dev, start, 0);
				continue;
			}
			if (!strnicmp(buf, "dma", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				start = simple_strtoul(buf, &buf, 0);
				pnp_add_dma_resource(dev, start, 0);
				continue;
			}
			break;
		}
		mutex_unlock(&pnp_res_mutex);
		goto done;
	}

done:
	if (retval < 0)
		return retval;
	return count;
}

static ssize_t pnp_show_current_ids(struct device *dmdev,
				    struct device_attribute *attr, char *buf)
{
	char *str = buf;
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	struct pnp_id *pos = dev->id;

	while (pos) {
		str += sprintf(str, "%s\n", pos->id);
		pos = pos->next;
	}
	return (str - buf);
}

struct device_attribute pnp_interface_attrs[] = {
	__ATTR(resources, S_IRUGO | S_IWUSR,
		   pnp_show_current_resources,
		   pnp_set_current_resources),
	__ATTR(options, S_IRUGO, pnp_show_options, NULL),
	__ATTR(id, S_IRUGO, pnp_show_current_ids, NULL),
	__ATTR_NULL,
};
