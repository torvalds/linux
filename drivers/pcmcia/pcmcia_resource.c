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

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "cs_internal.h"


/* Access speed for IO windows */
static int io_speed = 0;
module_param(io_speed, int, 0444);


#ifdef CONFIG_PCMCIA_PROBE
#include <asm/irq.h>
/* mask of IRQs already reserved by other cards, we should avoid using them */
static u8 pcmcia_used_irq[NR_IRQS];
#endif


#ifdef CONFIG_PCMCIA_DEBUG
extern int ds_pc_debug;

#define ds_dbg(skt, lvl, fmt, arg...) do {			\
	if (ds_pc_debug >= lvl)					\
		dev_printk(KERN_DEBUG, &skt->dev,		\
			   "pcmcia_resource: " fmt,		\
			   ## arg);				\
} while (0)
#else
#define ds_dbg(skt, lvl, fmt, arg...) do { } while (0)
#endif



/** alloc_io_space
 *
 * Special stuff for managing IO windows, because they are scarce
 */

static int alloc_io_space(struct pcmcia_socket *s, u_int attr,
			  unsigned int *base, unsigned int num, u_int lines)
{
	int i;
	unsigned int try, align;

	align = (*base) ? (lines ? 1<<lines : 0) : 1;
	if (align && (align < num)) {
		if (*base) {
			ds_dbg(s, 0, "odd IO request: num %#x align %#x\n",
			       num, align);
			align = 0;
		} else
			while (align && (align < num)) align <<= 1;
	}
	if (*base & ~(align-1)) {
		ds_dbg(s, 0, "odd IO request: base %#x align %#x\n",
		       *base, align);
		align = 0;
	}
	if ((s->features & SS_CAP_STATIC_MAP) && s->io_offset) {
		*base = s->io_offset | (*base & 0x0fff);
		return 0;
	}
	/* Check for an already-allocated window that must conflict with
	 * what was asked for.  It is a hack because it does not catch all
	 * potential conflicts, just the most obvious ones.
	 */
	for (i = 0; i < MAX_IO_WIN; i++)
		if ((s->io[i].res) && *base &&
		    ((s->io[i].res->start & (align-1)) == *base))
			return 1;
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res) {
			s->io[i].res = pcmcia_find_io_region(*base, num, align, s);
			if (s->io[i].res) {
				*base = s->io[i].res->start;
				s->io[i].res->flags = (s->io[i].res->flags & ~IORESOURCE_BITS) | (attr & IORESOURCE_BITS);
				s->io[i].InUse = num;
				break;
			} else
				return 1;
		} else if ((s->io[i].res->flags & IORESOURCE_BITS) != (attr & IORESOURCE_BITS))
			continue;
		/* Try to extend top of window */
		try = s->io[i].res->end + 1;
		if ((*base == 0) || (*base == try))
			if (pcmcia_adjust_io_region(s->io[i].res, s->io[i].res->start,
						    s->io[i].res->end + num, s) == 0) {
				*base = try;
				s->io[i].InUse += num;
				break;
			}
		/* Try to extend bottom of window */
		try = s->io[i].res->start - num;
		if ((*base == 0) || (*base == try))
			if (pcmcia_adjust_io_region(s->io[i].res, s->io[i].res->start - num,
						    s->io[i].res->end, s) == 0) {
				*base = try;
				s->io[i].InUse += num;
				break;
			}
	}
	return (i == MAX_IO_WIN);
} /* alloc_io_space */


static void release_io_space(struct pcmcia_socket *s, unsigned int base,
			     unsigned int num)
{
	int i;

	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;
		if ((s->io[i].res->start <= base) &&
		    (s->io[i].res->end >= base+num-1)) {
			s->io[i].InUse -= num;
			/* Free the window if no one else is using it */
			if (s->io[i].InUse == 0) {
				release_resource(s->io[i].res);
				kfree(s->io[i].res);
				s->io[i].res = NULL;
			}
		}
	}
} /* release_io_space */


/** pccard_access_configuration_register
 *
 * Access_configuration_register() reads and writes configuration
 * registers in attribute memory.  Memory window 0 is reserved for
 * this and the tuple reading services.
 */

int pcmcia_access_configuration_register(struct pcmcia_device *p_dev,
					 conf_reg_t *reg)
{
	struct pcmcia_socket *s;
	config_t *c;
	int addr;
	u_char val;

	if (!p_dev || !p_dev->function_config)
		return -EINVAL;

	s = p_dev->socket;
	c = p_dev->function_config;

	if (!(c->state & CONFIG_LOCKED))
		return -EACCES;

	addr = (c->ConfigBase + reg->Offset) >> 1;

	switch (reg->Action) {
	case CS_READ:
		pcmcia_read_cis_mem(s, 1, addr, 1, &val);
		reg->Value = val;
		break;
	case CS_WRITE:
		val = reg->Value;
		pcmcia_write_cis_mem(s, 1, addr, 1, &val);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
} /* pcmcia_access_configuration_register */
EXPORT_SYMBOL(pcmcia_access_configuration_register);


/** pcmcia_get_window
 */
int pcmcia_get_window(struct pcmcia_socket *s, window_handle_t *handle,
		      int idx, win_req_t *req)
{
	window_t *win;
	int w;

	if (!s || !(s->state & SOCKET_PRESENT))
		return -ENODEV;
	for (w = idx; w < MAX_WIN; w++)
		if (s->state & SOCKET_WIN_REQ(w))
			break;
	if (w == MAX_WIN)
		return -EINVAL;
	win = &s->win[w];
	req->Base = win->ctl.res->start;
	req->Size = win->ctl.res->end - win->ctl.res->start + 1;
	req->AccessSpeed = win->ctl.speed;
	req->Attributes = 0;
	if (win->ctl.flags & MAP_ATTRIB)
		req->Attributes |= WIN_MEMORY_TYPE_AM;
	if (win->ctl.flags & MAP_ACTIVE)
		req->Attributes |= WIN_ENABLE;
	if (win->ctl.flags & MAP_16BIT)
		req->Attributes |= WIN_DATA_WIDTH_16;
	if (win->ctl.flags & MAP_USE_WAIT)
		req->Attributes |= WIN_USE_WAIT;
	*handle = win;
	return 0;
} /* pcmcia_get_window */
EXPORT_SYMBOL(pcmcia_get_window);


/** pcmcia_get_mem_page
 *
 * Change the card address of an already open memory window.
 */
int pcmcia_get_mem_page(window_handle_t win, memreq_t *req)
{
	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return -EINVAL;
	req->Page = 0;
	req->CardOffset = win->ctl.card_start;
	return 0;
} /* pcmcia_get_mem_page */
EXPORT_SYMBOL(pcmcia_get_mem_page);


int pcmcia_map_mem_page(window_handle_t win, memreq_t *req)
{
	struct pcmcia_socket *s;
	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return -EINVAL;
	s = win->sock;
	if (req->Page != 0) {
		ds_dbg(s, 0, "failure: requested page is zero\n");
		return -EINVAL;
	}
	win->ctl.card_start = req->CardOffset;
	if (s->ops->set_mem_map(s, &win->ctl) != 0) {
		ds_dbg(s, 0, "failed to set_mem_map\n");
		return -EIO;
	}
	return 0;
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

	s = p_dev->socket;
	c = p_dev->function_config;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	if (!(c->state & CONFIG_LOCKED))
		return -EACCES;

	if (mod->Attributes & CONF_IRQ_CHANGE_VALID) {
		if (mod->Attributes & CONF_ENABLE_IRQ) {
			c->Attributes |= CONF_ENABLE_IRQ;
			s->socket.io_irq = s->irq.AssignedIRQ;
		} else {
			c->Attributes &= ~CONF_ENABLE_IRQ;
			s->socket.io_irq = 0;
		}
		s->ops->set_socket(s, &s->socket);
	}

	if (mod->Attributes & CONF_VCC_CHANGE_VALID) {
		ds_dbg(s, 0, "changing Vcc is not allowed at this time\n");
		return -EINVAL;
	}

	/* We only allow changing Vpp1 and Vpp2 to the same value */
	if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) &&
	    (mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
		if (mod->Vpp1 != mod->Vpp2) {
			ds_dbg(s, 0, "Vpp1 and Vpp2 must be the same\n");
			return -EINVAL;
		}
		s->socket.Vpp = mod->Vpp1;
		if (s->ops->set_socket(s, &s->socket)) {
			dev_printk(KERN_WARNING, &s->dev,
				   "Unable to set VPP\n");
			return -EIO;
		}
	} else if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) ||
		   (mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
		ds_dbg(s, 0, "changing Vcc is not allowed at this time\n");
		return -EINVAL;
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

	return 0;
} /* modify_configuration */
EXPORT_SYMBOL(pcmcia_modify_configuration);


int pcmcia_release_configuration(struct pcmcia_device *p_dev)
{
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;
	int i;

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
static int pcmcia_release_io(struct pcmcia_device *p_dev, io_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;

	if (!p_dev->_io )
		return -EINVAL;

	p_dev->_io = 0;

	if ((c->io.BasePort1 != req->BasePort1) ||
	    (c->io.NumPorts1 != req->NumPorts1) ||
	    (c->io.BasePort2 != req->BasePort2) ||
	    (c->io.NumPorts2 != req->NumPorts2))
		return -EINVAL;

	c->state &= ~CONFIG_IO_REQ;

	release_io_space(s, req->BasePort1, req->NumPorts1);
	if (req->NumPorts2)
		release_io_space(s, req->BasePort2, req->NumPorts2);

	return 0;
} /* pcmcia_release_io */


static int pcmcia_release_irq(struct pcmcia_device *p_dev, irq_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c= p_dev->function_config;

	if (!p_dev->_irq)
		return -EINVAL;
	p_dev->_irq = 0;

	if (c->state & CONFIG_LOCKED)
		return -EACCES;
	if (c->irq.Attributes != req->Attributes) {
		ds_dbg(s, 0, "IRQ attributes must match assigned ones\n");
		return -EINVAL;
	}
	if (s->irq.AssignedIRQ != req->AssignedIRQ) {
		ds_dbg(s, 0, "IRQ must match assigned one\n");
		return -EINVAL;
	}
	if (--s->irq.Config == 0) {
		c->state &= ~CONFIG_IRQ_REQ;
		s->irq.AssignedIRQ = 0;
	}

	if (req->Attributes & IRQ_HANDLE_PRESENT) {
		free_irq(req->AssignedIRQ, req->Instance);
	}

#ifdef CONFIG_PCMCIA_PROBE
	pcmcia_used_irq[req->AssignedIRQ]--;
#endif

	return 0;
} /* pcmcia_release_irq */


int pcmcia_release_window(window_handle_t win)
{
	struct pcmcia_socket *s;

	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return -EINVAL;
	s = win->sock;
	if (!(win->handle->_win & CLIENT_WIN_REQ(win->index)))
		return -EINVAL;

	/* Shut down memory window */
	win->ctl.flags &= ~MAP_ACTIVE;
	s->ops->set_mem_map(s, &win->ctl);
	s->state &= ~SOCKET_WIN_REQ(win->index);

	/* Release system memory */
	if (win->ctl.res) {
		release_resource(win->ctl.res);
		kfree(win->ctl.res);
		win->ctl.res = NULL;
	}
	win->handle->_win &= ~CLIENT_WIN_REQ(win->index);

	win->magic = 0;

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
		ds_dbg(p_dev->socket, 0, "IntType may not be INT_CARDBUS\n");
		return -EINVAL;
	}
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return -EACCES;

	/* Do power control.  We don't allow changes in Vcc. */
	s->socket.Vpp = req->Vpp;
	if (s->ops->set_socket(s, &s->socket)) {
		dev_printk(KERN_WARNING, &s->dev,
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
		s->socket.io_irq = s->irq.AssignedIRQ;
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
		if (c->state & CONFIG_IRQ_REQ)
			if (!(c->irq.Attributes & IRQ_FORCED_PULSE))
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
		u_char b = c->io.BasePort1 & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_0)>>1, 1, &b);
		b = (c->io.BasePort1 >> 8) & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_1)>>1, 1, &b);
	}
	if (req->Present & PRESENT_IOSIZE) {
		u_char b = c->io.NumPorts1 + c->io.NumPorts2 - 1;
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
	return 0;
} /* pcmcia_request_configuration */
EXPORT_SYMBOL(pcmcia_request_configuration);


/** pcmcia_request_io
 *
 * Request_io() reserves ranges of port addresses for a socket.
 * I have not implemented range sharing or alias addressing.
 */
int pcmcia_request_io(struct pcmcia_device *p_dev, io_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	if (!req)
		return -EINVAL;
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return -EACCES;
	if (c->state & CONFIG_IO_REQ) {
		ds_dbg(s, 0, "IO already configured\n");
		return -EBUSY;
	}
	if (req->Attributes1 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS)) {
		ds_dbg(s, 0, "bad attribute setting for IO region 1\n");
		return -EINVAL;
	}
	if ((req->NumPorts2 > 0) &&
	    (req->Attributes2 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS))) {
		ds_dbg(s, 0, "bad attribute setting for IO region 2\n");
		return -EINVAL;
	}

	ds_dbg(s, 1, "trying to allocate resource 1\n");
	if (alloc_io_space(s, req->Attributes1, &req->BasePort1,
			   req->NumPorts1, req->IOAddrLines)) {
		ds_dbg(s, 0, "allocation of resource 1 failed\n");
		return -EBUSY;
	}

	if (req->NumPorts2) {
		ds_dbg(s, 1, "trying to allocate resource 2\n");
		if (alloc_io_space(s, req->Attributes2, &req->BasePort2,
				   req->NumPorts2, req->IOAddrLines)) {
			ds_dbg(s, 0, "allocation of resource 2 failed\n");
			release_io_space(s, req->BasePort1, req->NumPorts1);
			return -EBUSY;
		}
	}

	c->io = *req;
	c->state |= CONFIG_IO_REQ;
	p_dev->_io = 1;
	return 0;
} /* pcmcia_request_io */
EXPORT_SYMBOL(pcmcia_request_io);


/** pcmcia_request_irq
 *
 * Request_irq() reserves an irq for this client.
 *
 * Also, since Linux only reserves irq's when they are actually
 * hooked, we don't guarantee that an irq will still be available
 * when the configuration is locked.  Now that I think about it,
 * there might be a way to fix this using a dummy handler.
 */

#ifdef CONFIG_PCMCIA_PROBE
static irqreturn_t test_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}
#endif

int pcmcia_request_irq(struct pcmcia_device *p_dev, irq_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	int ret = -EINVAL, irq = 0;
	int type;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return -EACCES;
	if (c->state & CONFIG_IRQ_REQ) {
		ds_dbg(s, 0, "IRQ already configured\n");
		return -EBUSY;
	}

	/* Decide what type of interrupt we are registering */
	type = 0;
	if (s->functions > 1)		/* All of this ought to be handled higher up */
		type = IRQF_SHARED;
	else if (req->Attributes & IRQ_TYPE_DYNAMIC_SHARING)
		type = IRQF_SHARED;
	else printk(KERN_WARNING "pcmcia: Driver needs updating to support IRQ sharing.\n");

#ifdef CONFIG_PCMCIA_PROBE

#ifdef IRQ_NOAUTOEN
	/* if the underlying IRQ infrastructure allows for it, only allocate
	 * the IRQ, but do not enable it
	 */
	if (!(req->Attributes & IRQ_HANDLE_PRESENT))
		type |= IRQ_NOAUTOEN;
#endif /* IRQ_NOAUTOEN */

	if (s->irq.AssignedIRQ != 0) {
		/* If the interrupt is already assigned, it must be the same */
		irq = s->irq.AssignedIRQ;
	} else {
		int try;
		u32 mask = s->irq_mask;
		void *data = &p_dev->dev.driver; /* something unique to this device */

		for (try = 0; try < 64; try++) {
			irq = try % 32;

			/* marked as available by driver, and not blocked by userspace? */
			if (!((mask >> irq) & 1))
				continue;

			/* avoid an IRQ which is already used by a PCMCIA card */
			if ((try < 32) && pcmcia_used_irq[irq])
				continue;

			/* register the correct driver, if possible, of check whether
			 * registering a dummy handle works, i.e. if the IRQ isn't
			 * marked as used by the kernel resource management core */
			ret = request_irq(irq,
					  (req->Attributes & IRQ_HANDLE_PRESENT) ? req->Handler : test_action,
					  type,
					  p_dev->devname,
					  (req->Attributes & IRQ_HANDLE_PRESENT) ? req->Instance : data);
			if (!ret) {
				if (!(req->Attributes & IRQ_HANDLE_PRESENT))
					free_irq(irq, data);
				break;
			}
		}
	}
#endif
	/* only assign PCI irq if no IRQ already assigned */
	if (ret && !s->irq.AssignedIRQ) {
		if (!s->pci_irq)
			return ret;
		type = IRQF_SHARED;
		irq = s->pci_irq;
	}

	if (ret && (req->Attributes & IRQ_HANDLE_PRESENT)) {
		ret = request_irq(irq, req->Handler, type,
				  p_dev->devname, req->Instance);
		if (ret)
			return ret;
	}

	/* Make sure the fact the request type was overridden is passed back */
	if (type == IRQF_SHARED && !(req->Attributes & IRQ_TYPE_DYNAMIC_SHARING)) {
		req->Attributes |= IRQ_TYPE_DYNAMIC_SHARING;
		dev_printk(KERN_WARNING, &p_dev->dev, "pcmcia: "
			"request for exclusive IRQ could not be fulfilled.\n");
		dev_printk(KERN_WARNING, &p_dev->dev, "pcmcia: the driver "
			"needs updating to supported shared IRQ lines.\n");
	}
	c->irq.Attributes = req->Attributes;
	s->irq.AssignedIRQ = req->AssignedIRQ = irq;
	s->irq.Config++;

	c->state |= CONFIG_IRQ_REQ;
	p_dev->_irq = 1;

#ifdef CONFIG_PCMCIA_PROBE
	pcmcia_used_irq[irq]++;
#endif

	return 0;
} /* pcmcia_request_irq */
EXPORT_SYMBOL(pcmcia_request_irq);


/** pcmcia_request_window
 *
 * Request_window() establishes a mapping between card memory space
 * and system memory space.
 */
int pcmcia_request_window(struct pcmcia_device **p_dev, win_req_t *req, window_handle_t *wh)
{
	struct pcmcia_socket *s = (*p_dev)->socket;
	window_t *win;
	u_long align;
	int w;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	if (req->Attributes & (WIN_PAGED | WIN_SHARED)) {
		ds_dbg(s, 0, "bad attribute setting for iomem region\n");
		return -EINVAL;
	}

	/* Window size defaults to smallest available */
	if (req->Size == 0)
		req->Size = s->map_size;
	align = (((s->features & SS_CAP_MEM_ALIGN) ||
		  (req->Attributes & WIN_STRICT_ALIGN)) ?
		 req->Size : s->map_size);
	if (req->Size & (s->map_size-1)) {
		ds_dbg(s, 0, "invalid map size\n");
		return -EINVAL;
	}
	if ((req->Base && (s->features & SS_CAP_STATIC_MAP)) ||
	    (req->Base & (align-1))) {
		ds_dbg(s, 0, "invalid base address\n");
		return -EINVAL;
	}
	if (req->Base)
		align = 0;

	/* Allocate system memory window */
	for (w = 0; w < MAX_WIN; w++)
		if (!(s->state & SOCKET_WIN_REQ(w))) break;
	if (w == MAX_WIN) {
		ds_dbg(s, 0, "all windows are used already\n");
		return -EINVAL;
	}

	win = &s->win[w];
	win->magic = WINDOW_MAGIC;
	win->index = w;
	win->handle = *p_dev;
	win->sock = s;

	if (!(s->features & SS_CAP_STATIC_MAP)) {
		win->ctl.res = pcmcia_find_mem_region(req->Base, req->Size, align,
						      (req->Attributes & WIN_MAP_BELOW_1MB), s);
		if (!win->ctl.res) {
			ds_dbg(s, 0, "allocating mem region failed\n");
			return -EINVAL;
		}
	}
	(*p_dev)->_win |= CLIENT_WIN_REQ(w);

	/* Configure the socket controller */
	win->ctl.map = w+1;
	win->ctl.flags = 0;
	win->ctl.speed = req->AccessSpeed;
	if (req->Attributes & WIN_MEMORY_TYPE)
		win->ctl.flags |= MAP_ATTRIB;
	if (req->Attributes & WIN_ENABLE)
		win->ctl.flags |= MAP_ACTIVE;
	if (req->Attributes & WIN_DATA_WIDTH_16)
		win->ctl.flags |= MAP_16BIT;
	if (req->Attributes & WIN_USE_WAIT)
		win->ctl.flags |= MAP_USE_WAIT;
	win->ctl.card_start = 0;
	if (s->ops->set_mem_map(s, &win->ctl) != 0) {
		ds_dbg(s, 0, "failed to set memory mapping\n");
		return -EIO;
	}
	s->state |= SOCKET_WIN_REQ(w);

	/* Return window handle */
	if (s->features & SS_CAP_STATIC_MAP) {
		req->Base = win->ctl.static_start;
	} else {
		req->Base = win->ctl.res->start;
	}
	*wh = win;

	return 0;
} /* pcmcia_request_window */
EXPORT_SYMBOL(pcmcia_request_window);

void pcmcia_disable_device(struct pcmcia_device *p_dev) {
	pcmcia_release_configuration(p_dev);
	pcmcia_release_io(p_dev, &p_dev->io);
	pcmcia_release_irq(p_dev, &p_dev->irq);
	if (p_dev->win)
		pcmcia_release_window(p_dev->win);
}
EXPORT_SYMBOL(pcmcia_disable_device);


struct pcmcia_cfg_mem {
	tuple_t tuple;
	cisparse_t parse;
	u8 buf[256];
	cistpl_cftable_entry_t dflt;
};

/**
 * pcmcia_loop_config() - loop over configuration options
 * @p_dev:	the struct pcmcia_device which we need to loop for.
 * @conf_check:	function to call for each configuration option.
 *		It gets passed the struct pcmcia_device, the CIS data
 *		describing the configuration option, and private data
 *		being passed to pcmcia_loop_config()
 * @priv_data:	private data to be passed to the conf_check function.
 *
 * pcmcia_loop_config() loops over all configuration options, and calls
 * the driver-specific conf_check() for each one, checking whether
 * it is a valid one. Returns 0 on success or errorcode otherwise.
 */
int pcmcia_loop_config(struct pcmcia_device *p_dev,
		       int	(*conf_check)	(struct pcmcia_device *p_dev,
						 cistpl_cftable_entry_t *cfg,
						 cistpl_cftable_entry_t *dflt,
						 unsigned int vcc,
						 void *priv_data),
		       void *priv_data)
{
	struct pcmcia_cfg_mem *cfg_mem;

	tuple_t *tuple;
	int ret;
	unsigned int vcc;

	cfg_mem = kzalloc(sizeof(struct pcmcia_cfg_mem), GFP_KERNEL);
	if (cfg_mem == NULL)
		return -ENOMEM;

	/* get the current Vcc setting */
	vcc = p_dev->socket->socket.Vcc;

	tuple = &cfg_mem->tuple;
	tuple->TupleData = cfg_mem->buf;
	tuple->TupleDataMax = 255;
	tuple->TupleOffset = 0;
	tuple->DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple->Attributes = 0;

	ret = pcmcia_get_first_tuple(p_dev, tuple);
	while (!ret) {
		cistpl_cftable_entry_t *cfg = &cfg_mem->parse.cftable_entry;

		if (pcmcia_get_tuple_data(p_dev, tuple))
			goto next_entry;

		if (pcmcia_parse_tuple(tuple, &cfg_mem->parse))
			goto next_entry;

		/* default values */
		p_dev->conf.ConfigIndex = cfg->index;
		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			cfg_mem->dflt = *cfg;

		ret = conf_check(p_dev, cfg, &cfg_mem->dflt, vcc, priv_data);
		if (!ret)
			break;

next_entry:
		ret = pcmcia_get_next_tuple(p_dev, tuple);
	}

	return ret;
}
EXPORT_SYMBOL(pcmcia_loop_config);
