/*
 * interface.c - contains everything related to the user interface
 *
 * Some code, especially possible resource dumping is based on isapnp_proc.c (c) Jaroslav Kysela <perex@perex.cz>
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/slab.h>
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
	pnp_printf(buffer,
		   "%sport 0x%x-0x%x, align 0x%x, size 0x%x, %i-bit address decoding\n",
		   space, port->min, port->max,
		   port->align ? (port->align - 1) : 0, port->size,
		   port->flags & PNP_PORT_FLAG_16BITADDR ? 16 : 10);
}

static void pnp_print_irq(pnp_info_buffer_t * buffer, char *space,
			  struct pnp_irq *irq)
{
	int first = 1, i;

	pnp_printf(buffer, "%sirq ", space);
	for (i = 0; i < PNP_IRQ_NR; i++)
		if (test_bit(i, irq->map)) {
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
	if (bitmap_empty(irq->map, PNP_IRQ_NR))
		pnp_printf(buffer, "<none>");
	if (irq->flags & IORESOURCE_IRQ_HIGHEDGE)
		pnp_printf(buffer, " High-Edge");
	if (irq->flags & IORESOURCE_IRQ_LOWEDGE)
		pnp_printf(buffer, " Low-Edge");
	if (irq->flags & IORESOURCE_IRQ_HIGHLEVEL)
		pnp_printf(buffer, " High-Level");
	if (irq->flags & IORESOURCE_IRQ_LOWLEVEL)
		pnp_printf(buffer, " Low-Level");
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

	pnp_printf(buffer, "%sMemory 0x%x-0x%x, align 0x%x, size 0x%x",
		   space, mem->min, mem->max, mem->align, mem->size);
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
			     struct pnp_option *option, int dep)
{
	char *s;
	struct pnp_port *port;
	struct pnp_irq *irq;
	struct pnp_dma *dma;
	struct pnp_mem *mem;

	if (dep) {
		switch (option->priority) {
		case PNP_RES_PRIORITY_PREFERRED:
			s = "preferred";
			break;
		case PNP_RES_PRIORITY_ACCEPTABLE:
			s = "acceptable";
			break;
		case PNP_RES_PRIORITY_FUNCTIONAL:
			s = "functional";
			break;
		default:
			s = "invalid";
		}
		pnp_printf(buffer, "Dependent: %02i - Priority %s\n", dep, s);
	}

	for (port = option->port; port; port = port->next)
		pnp_print_port(buffer, space, port);
	for (irq = option->irq; irq; irq = irq->next)
		pnp_print_irq(buffer, space, irq);
	for (dma = option->dma; dma; dma = dma->next)
		pnp_print_dma(buffer, space, dma);
	for (mem = option->mem; mem; mem = mem->next)
		pnp_print_mem(buffer, space, mem);
}

static ssize_t pnp_show_options(struct device *dmdev,
				struct device_attribute *attr, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	struct pnp_option *independent = dev->independent;
	struct pnp_option *dependent = dev->dependent;
	int ret, dep = 1;

	pnp_info_buffer_t *buffer = (pnp_info_buffer_t *)
	    pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;

	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;
	if (independent)
		pnp_print_option(buffer, "", independent, 0);

	while (dependent) {
		pnp_print_option(buffer, "   ", dependent, dep);
		dependent = dependent->next;
		dep++;
	}
	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

static DEVICE_ATTR(options, S_IRUGO, pnp_show_options, NULL);

static ssize_t pnp_show_current_resources(struct device *dmdev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	int i, ret;
	pnp_info_buffer_t *buffer;

	if (!dev)
		return -EINVAL;

	buffer = (pnp_info_buffer_t *) pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;
	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;

	pnp_printf(buffer, "state = ");
	if (dev->active)
		pnp_printf(buffer, "active\n");
	else
		pnp_printf(buffer, "disabled\n");

	for (i = 0; i < PNP_MAX_PORT; i++) {
		if (pnp_port_valid(dev, i)) {
			pnp_printf(buffer, "io");
			if (pnp_port_flags(dev, i) & IORESOURCE_DISABLED)
				pnp_printf(buffer, " disabled\n");
			else
				pnp_printf(buffer, " 0x%llx-0x%llx\n",
					   (unsigned long long)
					   pnp_port_start(dev, i),
					   (unsigned long long)pnp_port_end(dev,
									    i));
		}
	}
	for (i = 0; i < PNP_MAX_MEM; i++) {
		if (pnp_mem_valid(dev, i)) {
			pnp_printf(buffer, "mem");
			if (pnp_mem_flags(dev, i) & IORESOURCE_DISABLED)
				pnp_printf(buffer, " disabled\n");
			else
				pnp_printf(buffer, " 0x%llx-0x%llx\n",
					   (unsigned long long)
					   pnp_mem_start(dev, i),
					   (unsigned long long)pnp_mem_end(dev,
									   i));
		}
	}
	for (i = 0; i < PNP_MAX_IRQ; i++) {
		if (pnp_irq_valid(dev, i)) {
			pnp_printf(buffer, "irq");
			if (pnp_irq_flags(dev, i) & IORESOURCE_DISABLED)
				pnp_printf(buffer, " disabled\n");
			else
				pnp_printf(buffer, " %lld\n",
					   (unsigned long long)pnp_irq(dev, i));
		}
	}
	for (i = 0; i < PNP_MAX_DMA; i++) {
		if (pnp_dma_valid(dev, i)) {
			pnp_printf(buffer, "dma");
			if (pnp_dma_flags(dev, i) & IORESOURCE_DISABLED)
				pnp_printf(buffer, " disabled\n");
			else
				pnp_printf(buffer, " %lld\n",
					   (unsigned long long)pnp_dma(dev, i));
		}
	}
	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

extern struct semaphore pnp_res_mutex;

static ssize_t
pnp_set_current_resources(struct device *dmdev, struct device_attribute *attr,
			  const char *ubuf, size_t count)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char *buf = (void *)ubuf;
	int retval = 0;

	if (dev->status & PNP_ATTACHED) {
		retval = -EBUSY;
		pnp_info("Device %s cannot be configured because it is in use.",
			 dev->dev.bus_id);
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
		pnp_init_resource_table(&dev->res);
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strnicmp(buf, "clear", 5)) {
		if (dev->active)
			goto done;
		pnp_init_resource_table(&dev->res);
		goto done;
	}
	if (!strnicmp(buf, "get", 3)) {
		down(&pnp_res_mutex);
		if (pnp_can_read(dev))
			dev->protocol->get(dev, &dev->res);
		up(&pnp_res_mutex);
		goto done;
	}
	if (!strnicmp(buf, "set", 3)) {
		int nport = 0, nmem = 0, nirq = 0, ndma = 0;
		if (dev->active)
			goto done;
		buf += 3;
		pnp_init_resource_table(&dev->res);
		down(&pnp_res_mutex);
		while (1) {
			while (isspace(*buf))
				++buf;
			if (!strnicmp(buf, "io", 2)) {
				buf += 2;
				while (isspace(*buf))
					++buf;
				dev->res.port_resource[nport].start =
				    simple_strtoul(buf, &buf, 0);
				while (isspace(*buf))
					++buf;
				if (*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					dev->res.port_resource[nport].end =
					    simple_strtoul(buf, &buf, 0);
				} else
					dev->res.port_resource[nport].end =
					    dev->res.port_resource[nport].start;
				dev->res.port_resource[nport].flags =
				    IORESOURCE_IO;
				nport++;
				if (nport >= PNP_MAX_PORT)
					break;
				continue;
			}
			if (!strnicmp(buf, "mem", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.mem_resource[nmem].start =
				    simple_strtoul(buf, &buf, 0);
				while (isspace(*buf))
					++buf;
				if (*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					dev->res.mem_resource[nmem].end =
					    simple_strtoul(buf, &buf, 0);
				} else
					dev->res.mem_resource[nmem].end =
					    dev->res.mem_resource[nmem].start;
				dev->res.mem_resource[nmem].flags =
				    IORESOURCE_MEM;
				nmem++;
				if (nmem >= PNP_MAX_MEM)
					break;
				continue;
			}
			if (!strnicmp(buf, "irq", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.irq_resource[nirq].start =
				    dev->res.irq_resource[nirq].end =
				    simple_strtoul(buf, &buf, 0);
				dev->res.irq_resource[nirq].flags =
				    IORESOURCE_IRQ;
				nirq++;
				if (nirq >= PNP_MAX_IRQ)
					break;
				continue;
			}
			if (!strnicmp(buf, "dma", 3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.dma_resource[ndma].start =
				    dev->res.dma_resource[ndma].end =
				    simple_strtoul(buf, &buf, 0);
				dev->res.dma_resource[ndma].flags =
				    IORESOURCE_DMA;
				ndma++;
				if (ndma >= PNP_MAX_DMA)
					break;
				continue;
			}
			break;
		}
		up(&pnp_res_mutex);
		goto done;
	}

done:
	if (retval < 0)
		return retval;
	return count;
}

static DEVICE_ATTR(resources, S_IRUGO | S_IWUSR,
		   pnp_show_current_resources, pnp_set_current_resources);

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

static DEVICE_ATTR(id, S_IRUGO, pnp_show_current_ids, NULL);

int pnp_interface_attach_device(struct pnp_dev *dev)
{
	int rc = device_create_file(&dev->dev, &dev_attr_options);

	if (rc)
		goto err;
	rc = device_create_file(&dev->dev, &dev_attr_resources);
	if (rc)
		goto err_opt;
	rc = device_create_file(&dev->dev, &dev_attr_id);
	if (rc)
		goto err_res;

	return 0;

err_res:
	device_remove_file(&dev->dev, &dev_attr_resources);
err_opt:
	device_remove_file(&dev->dev, &dev_attr_options);
err:
	return rc;
}
