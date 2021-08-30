// SPDX-License-Identifier: GPL-2.0
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

#include <linux/uaccess.h>

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

__printf(2, 3)
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

static ssize_t options_show(struct device *dmdev, struct device_attribute *attr,
			    char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	pnp_info_buffer_t *buffer;
	struct pnp_option *option;
	int ret, dep = 0, set = 0;
	char *indent;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
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
static DEVICE_ATTR_RO(options);

static ssize_t resources_show(struct device *dmdev,
			      struct device_attribute *attr, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	pnp_info_buffer_t *buffer;
	struct pnp_resource *pnp_res;
	struct resource *res;
	int ret;

	if (!dev)
		return -EINVAL;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
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
		case IORESOURCE_BUS:
			pnp_printf(buffer, " %#llx-%#llx%s\n",
				   (unsigned long long) res->start,
				   (unsigned long long) res->end,
				   res->flags & IORESOURCE_WINDOW ?
					" window" : "");
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

static char *pnp_get_resource_value(char *buf,
				    unsigned long type,
				    resource_size_t *start,
				    resource_size_t *end,
				    unsigned long *flags)
{
	if (start)
		*start = 0;
	if (end)
		*end = 0;
	if (flags)
		*flags = 0;

	/* TBD: allow for disabled resources */

	buf = skip_spaces(buf);
	if (start) {
		*start = simple_strtoull(buf, &buf, 0);
		if (end) {
			buf = skip_spaces(buf);
			if (*buf == '-') {
				buf = skip_spaces(buf + 1);
				*end = simple_strtoull(buf, &buf, 0);
			} else
				*end = *start;
		}
	}

	/* TBD: allow for additional flags, e.g., IORESOURCE_WINDOW */

	return buf;
}

static ssize_t resources_store(struct device *dmdev,
			       struct device_attribute *attr, const char *ubuf,
			       size_t count)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char *buf = (void *)ubuf;
	int retval = 0;

	if (dev->status & PNP_ATTACHED) {
		retval = -EBUSY;
		dev_info(&dev->dev, "in use; can't configure\n");
		goto done;
	}

	buf = skip_spaces(buf);
	if (!strncasecmp(buf, "disable", 7)) {
		retval = pnp_disable_dev(dev);
		goto done;
	}
	if (!strncasecmp(buf, "activate", 8)) {
		retval = pnp_activate_dev(dev);
		goto done;
	}
	if (!strncasecmp(buf, "fill", 4)) {
		if (dev->active)
			goto done;
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strncasecmp(buf, "auto", 4)) {
		if (dev->active)
			goto done;
		pnp_init_resources(dev);
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strncasecmp(buf, "clear", 5)) {
		if (dev->active)
			goto done;
		pnp_init_resources(dev);
		goto done;
	}
	if (!strncasecmp(buf, "get", 3)) {
		mutex_lock(&pnp_res_mutex);
		if (pnp_can_read(dev))
			dev->protocol->get(dev);
		mutex_unlock(&pnp_res_mutex);
		goto done;
	}
	if (!strncasecmp(buf, "set", 3)) {
		resource_size_t start;
		resource_size_t end;
		unsigned long flags;

		if (dev->active)
			goto done;
		buf += 3;
		pnp_init_resources(dev);
		mutex_lock(&pnp_res_mutex);
		while (1) {
			buf = skip_spaces(buf);
			if (!strncasecmp(buf, "io", 2)) {
				buf = pnp_get_resource_value(buf + 2,
							     IORESOURCE_IO,
							     &start, &end,
							     &flags);
				pnp_add_io_resource(dev, start, end, flags);
			} else if (!strncasecmp(buf, "mem", 3)) {
				buf = pnp_get_resource_value(buf + 3,
							     IORESOURCE_MEM,
							     &start, &end,
							     &flags);
				pnp_add_mem_resource(dev, start, end, flags);
			} else if (!strncasecmp(buf, "irq", 3)) {
				buf = pnp_get_resource_value(buf + 3,
							     IORESOURCE_IRQ,
							     &start, NULL,
							     &flags);
				pnp_add_irq_resource(dev, start, flags);
			} else if (!strncasecmp(buf, "dma", 3)) {
				buf = pnp_get_resource_value(buf + 3,
							     IORESOURCE_DMA,
							     &start, NULL,
							     &flags);
				pnp_add_dma_resource(dev, start, flags);
			} else if (!strncasecmp(buf, "bus", 3)) {
				buf = pnp_get_resource_value(buf + 3,
							     IORESOURCE_BUS,
							     &start, &end,
							     NULL);
				pnp_add_bus_resource(dev, start, end);
			} else
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
static DEVICE_ATTR_RW(resources);

static ssize_t id_show(struct device *dmdev, struct device_attribute *attr,
		       char *buf)
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
static DEVICE_ATTR_RO(id);

static struct attribute *pnp_dev_attrs[] = {
	&dev_attr_resources.attr,
	&dev_attr_options.attr,
	&dev_attr_id.attr,
	NULL,
};

static const struct attribute_group pnp_dev_group = {
	.attrs = pnp_dev_attrs,
};

const struct attribute_group *pnp_dev_groups[] = {
	&pnp_dev_group,
	NULL,
};
