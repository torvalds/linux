/*
 * PCMCIA 16-bit resource management functions
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Copyright (C) 1999	     David A. Hinds
 * Copyright (C) 2004-2005   Dominik Brodowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "cs_internal.h"


/* Access speed for IO windows */
static int io_speed;
module_param(io_speed, int, 0444);


int pcmcia_validate_mem(struct pcmcia_socket *s)
{
	if (s->resource_ops->validate_mem)
		return s->resource_ops->validate_mem(s);
	/* if there is no callback, we can assume that everything is OK */
	return 0;
}

struct resource *pcmcia_find_mem_region(u_long base, u_long num, u_long align,
				 int low, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_mem)
		return s->resource_ops->find_mem(base, num, align, low, s);
	return NULL;
}


static void release_io_space(struct pcmcia_socket *s, struct resource *res)
{
	resource_size_t num = resource_size(res);
	int i;

	dev_dbg(&s->dev, "release_io_space for %pR\n", res);

	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;
		if ((s->io[i].res->start <= res->start) &&
		    (s->io[i].res->end >= res->end)) {
			s->io[i].InUse -= num;
			if (res->parent)
				release_resource(res);
			res->start = res->end = 0;
			res->flags = IORESOURCE_IO;
			/* Free the window if no one else is using it */
			if (s->io[i].InUse == 0) {
				release_resource(s->io[i].res);
				kfree(s->io[i].res);
				s->io[i].res = NULL;
			}
		}
	}
} /* release_io_space */

/** alloc_io_space
 *
 * Special stuff for managing IO windows, because they are scarce
 */
static int alloc_io_space(struct pcmcia_socket *s, struct resource *res,
			unsigned int lines)
{
	unsigned int align;
	unsigned int base = res->start;
	unsigned int num = res->end;
	int ret;

	res->flags |= IORESOURCE_IO;

	dev_dbg(&s->dev, "alloc_io_space request for %pR, %d lines\n",
		res, lines);

	align = base ? (lines ? 1<<lines : 0) : 1;
	if (align && (align < num)) {
		if (base) {
			dev_dbg(&s->dev, "odd IO request\n");
			align = 0;
		} else
			while (align && (align < num))
				align <<= 1;
	}
	if (base & ~(align-1)) {
		dev_dbg(&s->dev, "odd IO request\n");
		align = 0;
	}

	ret = s->resource_ops->find_io(s, res->flags, &base, num, align,
				&res->parent);
	if (ret) {
		dev_dbg(&s->dev, "alloc_io_space request failed (%d)\n", ret);
		return -EINVAL;
	}

	res->start = base;
	res->end = res->start + num - 1;

	if (res->parent) {
		ret = request_resource(res->parent, res);
		if (ret) {
			dev_warn(&s->dev,
				"request_resource %pR failed: %d\n", res, ret);
			res->parent = NULL;
			release_io_space(s, res);
		}
	}
	dev_dbg(&s->dev, "alloc_io_space request result %d: %pR\n", ret, res);
	return ret;
} /* alloc_io_space */


/**
 * pcmcia_access_config() - read or write card configuration registers
 *
 * pcmcia_access_config() reads and writes configuration registers in
 * attribute memory.  Memory window 0 is reserved for this and the tuple
 * reading services. Drivers must use pcmcia_read_config_byte() or
 * pcmcia_write_config_byte().
 */
static int pcmcia_access_config(struct pcmcia_device *p_dev,
				off_t where, u8 *val,
				int (*accessf) (struct pcmcia_socket *s,
						int attr, unsigned int addr,
						unsigned int len, void *ptr))
{
	struct pcmcia_socket *s;
	config_t *c;
	int addr;
	int ret = 0;

	s = p_dev->socket;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;

	if (!(c->state & CONFIG_LOCKED)) {
		dev_dbg(&p_dev->dev, "Configuration isnt't locked\n");
		mutex_unlock(&s->ops_mutex);
		return -EACCES;
	}

	addr = (c->ConfigBase + where) >> 1;

	ret = accessf(s, 1, addr, 1, val);

	mutex_unlock(&s->ops_mutex);

	return ret;
} /* pcmcia_access_config */


/**
 * pcmcia_read_config_byte() - read a byte from a card configuration register
 *
 * pcmcia_read_config_byte() reads a byte from a configuration register in
 * attribute memory.
 */
int pcmcia_read_config_byte(struct pcmcia_device *p_dev, off_t where, u8 *val)
{
	return pcmcia_access_config(p_dev, where, val, pcmcia_read_cis_mem);
}
EXPORT_SYMBOL(pcmcia_read_config_byte);


/**
 * pcmcia_write_config_byte() - write a byte to a card configuration register
 *
 * pcmcia_write_config_byte() writes a byte to a configuration register in
 * attribute memory.
 */
int pcmcia_write_config_byte(struct pcmcia_device *p_dev, off_t where, u8 val)
{
	return pcmcia_access_config(p_dev, where, &val, pcmcia_write_cis_mem);
}
EXPORT_SYMBOL(pcmcia_write_config_byte);


int pcmcia_map_mem_page(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int offset)
{
	struct pcmcia_socket *s = p_dev->socket;
	unsigned int w;
	int ret;

	w = ((res->flags & IORESOURCE_BITS & WIN_FLAGS_REQ) >> 2) - 1;
	if (w >= MAX_WIN)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	s->win[w].card_start = offset;
	ret = s->ops->set_mem_map(s, &s->win[w]);
	if (ret)
		dev_warn(&p_dev->dev, "failed to set_mem_map\n");
	mutex_unlock(&s->ops_mutex);
	return ret;
} /* pcmcia_map_mem_page */
EXPORT_SYMBOL(pcmcia_map_mem_page);


/** pcmcia_modify_configuration
 *
 * Modify a locked socket configuration
 */
int pcmcia_modify_configuration(struct pcmcia_device *p_dev,
				modconf_t *mod)
{
	struct pcmcia_socket *s;
	config_t *c;
	int ret;

	s = p_dev->socket;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;

	if (!(s->state & SOCKET_PRESENT)) {
		dev_dbg(&p_dev->dev, "No card present\n");
		ret = -ENODEV;
		goto unlock;
	}
	if (!(c->state & CONFIG_LOCKED)) {
		dev_dbg(&p_dev->dev, "Configuration isnt't locked\n");
		ret = -EACCES;
		goto unlock;
	}

	if (mod->Attributes & (CONF_IRQ_CHANGE_VALID | CONF_VCC_CHANGE_VALID)) {
		dev_dbg(&p_dev->dev,
			"changing Vcc or IRQ is not allowed at this time\n");
		ret = -EINVAL;
		goto unlock;
	}

	/* We only allow changing Vpp1 and Vpp2 to the same value */
	if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) &&
	    (mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
		if (mod->Vpp1 != mod->Vpp2) {
			dev_dbg(&p_dev->dev,
				"Vpp1 and Vpp2 must be the same\n");
			ret = -EINVAL;
			goto unlock;
		}
		s->socket.Vpp = mod->Vpp1;
		if (s->ops->set_socket(s, &s->socket)) {
			dev_printk(KERN_WARNING, &p_dev->dev,
				   "Unable to set VPP\n");
			ret = -EIO;
			goto unlock;
		}
	} else if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) ||
		   (mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
		dev_dbg(&p_dev->dev,
			"changing Vcc is not allowed at this time\n");
		ret = -EINVAL;
		goto unlock;
	}

	if (mod->Attributes & CONF_IO_CHANGE_WIDTH) {
		pccard_io_map io_off = { 0, 0, 0, 0, 1 };
		pccard_io_map io_on;
		int i;

		io_on.speed = io_speed;
		for (i = 0; i < MAX_IO_WIN; i++) {
			if (!s->io[i].res)
				continue;
			io_off.map = i;
			io_on.map = i;

			io_on.flags = MAP_ACTIVE | IO_DATA_PATH_WIDTH_8;
			io_on.start = s->io[i].res->start;
			io_on.stop = s->io[i].res->end;

			s->ops->set_io_map(s, &io_off);
			mdelay(40);
			s->ops->set_io_map(s, &io_on);
		}
	}
	ret = 0;
unlock:
	mutex_unlock(&s->ops_mutex);

	return ret;
} /* modify_configuration */
EXPORT_SYMBOL(pcmcia_modify_configuration);


int pcmcia_release_configuration(struct pcmcia_device *p_dev)
{
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	int i;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;
	if (p_dev->_locked) {
		p_dev->_locked = 0;
		if (--(s->lock_count) == 0) {
			s->socket.flags = SS_OUTPUT_ENA;   /* Is this correct? */
			s->socket.Vpp = 0;
			s->socket.io_irq = 0;
			s->ops->set_socket(s, &s->socket);
		}
	}
	if (c->state & CONFIG_LOCKED) {
		c->state &= ~CONFIG_LOCKED;
		if (c->state & CONFIG_IO_REQ)
			for (i = 0; i < MAX_IO_WIN; i++) {
				if (!s->io[i].res)
					continue;
				s->io[i].Config--;
				if (s->io[i].Config != 0)
					continue;
				io.map = i;
				s->ops->set_io_map(s, &io);
			}
	}
	mutex_unlock(&s->ops_mutex);

	return 0;
} /* pcmcia_release_configuration */


/** pcmcia_release_io
 *
 * Release_io() releases the I/O ranges allocated by a client.  This
 * may be invoked some time after a card ejection has already dumped
 * the actual socket configuration, so if the client is "stale", we
 * don't bother checking the port ranges against the current socket
 * values.
 */
static int pcmcia_release_io(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;
	int ret = -EINVAL;
	config_t *c;

	mutex_lock(&s->ops_mutex);
	if (!p_dev->_io)
		goto out;

	c = p_dev->function_config;

	release_io_space(s, &c->io[0]);

	if (c->io[1].end)
		release_io_space(s, &c->io[1]);

	p_dev->_io = 0;
	c->state &= ~CONFIG_IO_REQ;

out:
	mutex_unlock(&s->ops_mutex);

	return ret;
} /* pcmcia_release_io */

/**
 * pcmcia_release_window() - release reserved iomem for PCMCIA devices
 *
 * pcmcia_release_window() releases struct resource *res which was
 * previously reserved by calling pcmcia_request_window().
 */
int pcmcia_release_window(struct pcmcia_device *p_dev, struct resource *res)
{
	struct pcmcia_socket *s = p_dev->socket;
	pccard_mem_map *win;
	unsigned int w;

	dev_dbg(&p_dev->dev, "releasing window %pR\n", res);

	w = ((res->flags & IORESOURCE_BITS & WIN_FLAGS_REQ) >> 2) - 1;
	if (w >= MAX_WIN)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	win = &s->win[w];

	if (!(p_dev->_win & CLIENT_WIN_REQ(w))) {
		dev_dbg(&p_dev->dev, "not releasing unknown window\n");
		mutex_unlock(&s->ops_mutex);
		return -EINVAL;
	}

	/* Shut down memory window */
	win->flags &= ~MAP_ACTIVE;
	s->ops->set_mem_map(s, win);
	s->state &= ~SOCKET_WIN_REQ(w);

	/* Release system memory */
	if (win->res) {
		release_resource(res);
		release_resource(win->res);
		kfree(win->res);
		win->res = NULL;
	}
	res->start = res->end = 0;
	res->flags = IORESOURCE_MEM;
	p_dev->_win &= ~CLIENT_WIN_REQ(w);
	mutex_unlock(&s->ops_mutex);

	return 0;
} /* pcmcia_release_window */
EXPORT_SYMBOL(pcmcia_release_window);


int pcmcia_request_configuration(struct pcmcia_device *p_dev,
				 config_req_t *req)
{
	int i;
	u_int base;
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	pccard_io_map iomap;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	if (req->IntType & INT_CARDBUS) {
		dev_dbg(&p_dev->dev, "IntType may not be INT_CARDBUS\n");
		return -EINVAL;
	}

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED) {
		mutex_unlock(&s->ops_mutex);
		dev_dbg(&p_dev->dev, "Configuration is locked\n");
		return -EACCES;
	}

	/* Do power control.  We don't allow changes in Vcc. */
	s->socket.Vpp = req->Vpp;
	if (s->ops->set_socket(s, &s->socket)) {
		mutex_unlock(&s->ops_mutex);
		dev_printk(KERN_WARNING, &p_dev->dev,
			   "Unable to set socket state\n");
		return -EINVAL;
	}

	/* Pick memory or I/O card, DMA mode, interrupt */
	c->IntType = req->IntType;
	c->Attributes = req->Attributes;
	if (req->IntType & INT_MEMORY_AND_IO)
		s->socket.flags |= SS_IOCARD;
	if (req->IntType & INT_ZOOMED_VIDEO)
		s->socket.flags |= SS_ZVCARD | SS_IOCARD;
	if (req->Attributes & CONF_ENABLE_DMA)
		s->socket.flags |= SS_DMA_MODE;
	if (req->Attributes & CONF_ENABLE_SPKR)
		s->socket.flags |= SS_SPKR_ENA;
	if (req->Attributes & CONF_ENABLE_IRQ)
		s->socket.io_irq = s->pcmcia_irq;
	else
		s->socket.io_irq = 0;
	s->ops->set_socket(s, &s->socket);
	s->lock_count++;

	/* Set up CIS configuration registers */
	base = c->ConfigBase = req->ConfigBase;
	c->CardValues = req->Present;
	if (req->Present & PRESENT_COPY) {
		c->Copy = req->Copy;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_SCR)>>1, 1, &c->Copy);
	}
	if (req->Present & PRESENT_OPTION) {
		if (s->functions == 1) {
			c->Option = req->ConfigIndex & COR_CONFIG_MASK;
		} else {
			c->Option = req->ConfigIndex & COR_MFC_CONFIG_MASK;
			c->Option |= COR_FUNC_ENA|COR_IREQ_ENA;
			if (req->Present & PRESENT_IOBASE_0)
				c->Option |= COR_ADDR_DECODE;
		}
		if ((req->Attributes & CONF_ENABLE_IRQ) &&
			!(req->Attributes & CONF_ENABLE_PULSE_IRQ))
			c->Option |= COR_LEVEL_REQ;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_COR)>>1, 1, &c->Option);
		mdelay(40);
	}
	if (req->Present & PRESENT_STATUS) {
		c->Status = req->Status;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_CCSR)>>1, 1, &c->Status);
	}
	if (req->Present & PRESENT_PIN_REPLACE) {
		c->Pin = req->Pin;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_PRR)>>1, 1, &c->Pin);
	}
	if (req->Present & PRESENT_EXT_STATUS) {
		c->ExtStatus = req->ExtStatus;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_ESR)>>1, 1, &c->ExtStatus);
	}
	if (req->Present & PRESENT_IOBASE_0) {
		u8 b = c->io[0].start & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_0)>>1, 1, &b);
		b = (c->io[0].start >> 8) & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_1)>>1, 1, &b);
	}
	if (req->Present & PRESENT_IOSIZE) {
		u8 b = resource_size(&c->io[0]) + resource_size(&c->io[1]) - 1;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOSIZE)>>1, 1, &b);
	}

	/* Configure I/O windows */
	if (c->state & CONFIG_IO_REQ) {
		iomap.speed = io_speed;
		for (i = 0; i < MAX_IO_WIN; i++)
			if (s->io[i].res) {
				iomap.map = i;
				iomap.flags = MAP_ACTIVE;
				switch (s->io[i].res->flags & IO_DATA_PATH_WIDTH) {
				case IO_DATA_PATH_WIDTH_16:
					iomap.flags |= MAP_16BIT; break;
				case IO_DATA_PATH_WIDTH_AUTO:
					iomap.flags |= MAP_AUTOSZ; break;
				default:
					break;
				}
				iomap.start = s->io[i].res->start;
				iomap.stop = s->io[i].res->end;
				s->ops->set_io_map(s, &iomap);
				s->io[i].Config++;
			}
	}

	c->state |= CONFIG_LOCKED;
	p_dev->_locked = 1;
	mutex_unlock(&s->ops_mutex);
	return 0;
} /* pcmcia_request_configuration */
EXPORT_SYMBOL(pcmcia_request_configuration);


/**
 * pcmcia_request_io() - attempt to reserve port ranges for PCMCIA devices
 *
 * pcmcia_request_io() attepts to reserve the IO port ranges specified in
 * &struct pcmcia_device @p_dev->resource[0] and @p_dev->resource[1]. The
 * "start" value is the requested start of the IO port resource; "end"
 * reflects the number of ports requested. The number of IO lines requested
 * is specified in &struct pcmcia_device @p_dev->io_lines.
 */
int pcmcia_request_io(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;
	int ret = -EINVAL;

	mutex_lock(&s->ops_mutex);
	dev_dbg(&p_dev->dev, "pcmcia_request_io: %pR , %pR",
		&c->io[0], &c->io[1]);

	if (!(s->state & SOCKET_PRESENT)) {
		dev_dbg(&p_dev->dev, "pcmcia_request_io: No card present\n");
		goto out;
	}

	if (c->state & CONFIG_LOCKED) {
		dev_dbg(&p_dev->dev, "Configuration is locked\n");
		goto out;
	}
	if (c->state & CONFIG_IO_REQ) {
		dev_dbg(&p_dev->dev, "IO already configured\n");
		goto out;
	}

	ret = alloc_io_space(s, &c->io[0], p_dev->io_lines);
	if (ret)
		goto out;

	if (c->io[1].end) {
		ret = alloc_io_space(s, &c->io[1], p_dev->io_lines);
		if (ret) {
			struct resource tmp = c->io[0];
			/* release the previously allocated resource */
			release_io_space(s, &c->io[0]);
			/* but preserve the settings, for they worked... */
			c->io[0].end = resource_size(&tmp);
			c->io[0].start = tmp.start;
			c->io[0].flags = tmp.flags;
			goto out;
		}
	} else
		c->io[1].start = 0;

	c->state |= CONFIG_IO_REQ;
	p_dev->_io = 1;

	dev_dbg(&p_dev->dev, "pcmcia_request_io succeeded: %pR , %pR",
		&c->io[0], &c->io[1]);
out:
	mutex_unlock(&s->ops_mutex);

	return ret;
} /* pcmcia_request_io */
EXPORT_SYMBOL(pcmcia_request_io);


/**
 * pcmcia_request_irq() - attempt to request a IRQ for a PCMCIA device
 *
 * pcmcia_request_irq() is a wrapper around request_irq which will allow
 * the PCMCIA core to clean up the registration in pcmcia_disable_device().
 * Drivers are free to use request_irq() directly, but then they need to
 * call free_irq themselfves, too. Also, only IRQF_SHARED capable IRQ
 * handlers are allowed.
 */
int __must_check pcmcia_request_irq(struct pcmcia_device *p_dev,
				    irq_handler_t handler)
{
	int ret;

	if (!p_dev->irq)
		return -EINVAL;

	ret = request_irq(p_dev->irq, handler, IRQF_SHARED,
			p_dev->devname, p_dev->priv);
	if (!ret)
		p_dev->_irq = 1;

	return ret;
}
EXPORT_SYMBOL(pcmcia_request_irq);


/**
 * pcmcia_request_exclusive_irq() - attempt to request an exclusive IRQ first
 *
 * pcmcia_request_exclusive_irq() is a wrapper around request_irq which
 * attempts first to request an exclusive IRQ. If it fails, it also accepts
 * a shared IRQ, but prints out a warning. PCMCIA drivers should allow for
 * IRQ sharing and either use request_irq directly (then they need to call
 * free_irq themselves, too), or the pcmcia_request_irq() function.
 */
int __must_check
__pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
			irq_handler_t handler)
{
	int ret;

	if (!p_dev->irq)
		return -EINVAL;

	ret = request_irq(p_dev->irq, handler, 0, p_dev->devname, p_dev->priv);
	if (ret) {
		ret = pcmcia_request_irq(p_dev, handler);
		dev_printk(KERN_WARNING, &p_dev->dev, "pcmcia: "
			"request for exclusive IRQ could not be fulfilled.\n");
		dev_printk(KERN_WARNING, &p_dev->dev, "pcmcia: the driver "
			"needs updating to supported shared IRQ lines.\n");
	}
	if (ret)
		dev_printk(KERN_INFO, &p_dev->dev, "request_irq() failed\n");
	else
		p_dev->_irq = 1;

	return ret;
} /* pcmcia_request_exclusive_irq */
EXPORT_SYMBOL(__pcmcia_request_exclusive_irq);


#ifdef CONFIG_PCMCIA_PROBE

/* mask of IRQs already reserved by other cards, we should avoid using them */
static u8 pcmcia_used_irq[32];

static irqreturn_t test_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}

/**
 * pcmcia_setup_isa_irq() - determine whether an ISA IRQ can be used
 * @p_dev - the associated PCMCIA device
 *
 * locking note: must be called with ops_mutex locked.
 */
static int pcmcia_setup_isa_irq(struct pcmcia_device *p_dev, int type)
{
	struct pcmcia_socket *s = p_dev->socket;
	unsigned int try, irq;
	u32 mask = s->irq_mask;
	int ret = -ENODEV;

	for (try = 0; try < 64; try++) {
		irq = try % 32;

		if (irq > NR_IRQS)
			continue;

		/* marked as available by driver, not blocked by userspace? */
		if (!((mask >> irq) & 1))
			continue;

		/* avoid an IRQ which is already used by another PCMCIA card */
		if ((try < 32) && pcmcia_used_irq[irq])
			continue;

		/* register the correct driver, if possible, to check whether
		 * registering a dummy handle works, i.e. if the IRQ isn't
		 * marked as used by the kernel resource management core */
		ret = request_irq(irq, test_action, type, p_dev->devname,
				  p_dev);
		if (!ret) {
			free_irq(irq, p_dev);
			p_dev->irq = s->pcmcia_irq = irq;
			pcmcia_used_irq[irq]++;
			break;
		}
	}

	return ret;
}

void pcmcia_cleanup_irq(struct pcmcia_socket *s)
{
	pcmcia_used_irq[s->pcmcia_irq]--;
	s->pcmcia_irq = 0;
}

#else /* CONFIG_PCMCIA_PROBE */

static int pcmcia_setup_isa_irq(struct pcmcia_device *p_dev, int type)
{
	return -EINVAL;
}

void pcmcia_cleanup_irq(struct pcmcia_socket *s)
{
	s->pcmcia_irq = 0;
	return;
}

#endif  /* CONFIG_PCMCIA_PROBE */


/**
 * pcmcia_setup_irq() - determine IRQ to be used for device
 * @p_dev - the associated PCMCIA device
 *
 * locking note: must be called with ops_mutex locked.
 */
int pcmcia_setup_irq(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;

	if (p_dev->irq)
		return 0;

	/* already assigned? */
	if (s->pcmcia_irq) {
		p_dev->irq = s->pcmcia_irq;
		return 0;
	}

	/* prefer an exclusive ISA irq */
	if (!pcmcia_setup_isa_irq(p_dev, 0))
		return 0;

	/* but accept a shared ISA irq */
	if (!pcmcia_setup_isa_irq(p_dev, IRQF_SHARED))
		return 0;

	/* but use the PCI irq otherwise */
	if (s->pci_irq) {
		p_dev->irq = s->pcmcia_irq = s->pci_irq;
		return 0;
	}

	return -EINVAL;
}


/**
 * pcmcia_request_window() - attempt to reserve iomem for PCMCIA devices
 *
 * pcmcia_request_window() attepts to reserve an iomem ranges specified in
 * struct resource *res pointing to one of the entries in
 * struct pcmcia_device *p_dev->resource[2..5]. The "start" value is the
 * requested start of the IO mem resource; "end" reflects the size
 * requested.
 */
int pcmcia_request_window(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int speed)
{
	struct pcmcia_socket *s = p_dev->socket;
	pccard_mem_map *win;
	u_long align;
	int w;

	if (!(s->state & SOCKET_PRESENT)) {
		dev_dbg(&p_dev->dev, "No card present\n");
		return -ENODEV;
	}

	/* Window size defaults to smallest available */
	if (res->end == 0)
		res->end = s->map_size;
	align = (s->features & SS_CAP_MEM_ALIGN) ? res->end : s->map_size;
	if (res->end & (s->map_size-1)) {
		dev_dbg(&p_dev->dev, "invalid map size\n");
		return -EINVAL;
	}
	if ((res->start && (s->features & SS_CAP_STATIC_MAP)) ||
	    (res->start & (align-1))) {
		dev_dbg(&p_dev->dev, "invalid base address\n");
		return -EINVAL;
	}
	if (res->start)
		align = 0;

	/* Allocate system memory window */
	mutex_lock(&s->ops_mutex);
	for (w = 0; w < MAX_WIN; w++)
		if (!(s->state & SOCKET_WIN_REQ(w)))
			break;
	if (w == MAX_WIN) {
		dev_dbg(&p_dev->dev, "all windows are used already\n");
		mutex_unlock(&s->ops_mutex);
		return -EINVAL;
	}

	win = &s->win[w];

	if (!(s->features & SS_CAP_STATIC_MAP)) {
		win->res = pcmcia_find_mem_region(res->start, res->end, align,
						0, s);
		if (!win->res) {
			dev_dbg(&p_dev->dev, "allocating mem region failed\n");
			mutex_unlock(&s->ops_mutex);
			return -EINVAL;
		}
	}
	p_dev->_win |= CLIENT_WIN_REQ(w);

	/* Configure the socket controller */
	win->map = w+1;
	win->flags = res->flags & WIN_FLAGS_MAP;
	win->speed = speed;
	win->card_start = 0;

	if (s->ops->set_mem_map(s, win) != 0) {
		dev_dbg(&p_dev->dev, "failed to set memory mapping\n");
		mutex_unlock(&s->ops_mutex);
		return -EIO;
	}
	s->state |= SOCKET_WIN_REQ(w);

	/* Return window handle */
	if (s->features & SS_CAP_STATIC_MAP)
		res->start = win->static_start;
	else
		res->start = win->res->start;

	/* convert to new-style resources */
	res->end += res->start - 1;
	res->flags &= ~WIN_FLAGS_REQ;
	res->flags |= (win->map << 2) | IORESOURCE_MEM;
	res->parent = win->res;
	if (win->res)
		request_resource(&iomem_resource, res);

	dev_dbg(&p_dev->dev, "request_window results in %pR\n", res);

	mutex_unlock(&s->ops_mutex);

	return 0;
} /* pcmcia_request_window */
EXPORT_SYMBOL(pcmcia_request_window);

void pcmcia_disable_device(struct pcmcia_device *p_dev)
{
	int i;
	for (i = 0; i < MAX_WIN; i++) {
		struct resource *res = p_dev->resource[MAX_IO_WIN + i];
		if (res->flags & WIN_FLAGS_REQ)
			pcmcia_release_window(p_dev, res);
	}

	pcmcia_release_configuration(p_dev);
	pcmcia_release_io(p_dev);
	if (p_dev->_irq) {
		free_irq(p_dev->irq, p_dev->priv);
		p_dev->_irq = 0;
	}
}
EXPORT_SYMBOL(pcmcia_disable_device);
