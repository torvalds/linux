/*
 * Regular cardbus driver ("yenta_socket")
 *
 * (C) Copyright 1999, 2000 Linus Torvalds
 *
 * Changelog:
 * Aug 2002: Manfred Spraul <manfred@colorfullife.com>
 * 	Dynamically adjust the size of the bridge resource
 * 	
 * May 2003: Dominik Brodowski <linux@brodo.de>
 * 	Merge pci_socket.c and yenta.c into one file
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>

#include <asm/io.h>

#include "yenta_socket.h"
#include "i82365.h"

static int disable_clkrun;
module_param(disable_clkrun, bool, 0444);
MODULE_PARM_DESC(disable_clkrun, "If PC card doesn't function properly, please try this option");

static int isa_probe = 1;
module_param(isa_probe, bool, 0444);
MODULE_PARM_DESC(isa_probe, "If set ISA interrupts are probed (default). Set to N to disable probing");

static int pwr_irqs_off;
module_param(pwr_irqs_off, bool, 0644);
MODULE_PARM_DESC(pwr_irqs_off, "Force IRQs off during power-on of slot. Use only when seeing IRQ storms!");

#if 0
#define debug(x,args...) printk(KERN_DEBUG "%s: " x, __func__ , ##args)
#else
#define debug(x,args...)
#endif

/* Don't ask.. */
#define to_cycles(ns)	((ns)/120)
#define to_ns(cycles)	((cycles)*120)

static int yenta_probe_cb_irq(struct yenta_socket *socket);


static unsigned int override_bios;
module_param(override_bios, uint, 0000);
MODULE_PARM_DESC (override_bios, "yenta ignore bios resource allocation");

/*
 * Generate easy-to-use ways of reading a cardbus sockets
 * regular memory space ("cb_xxx"), configuration space
 * ("config_xxx") and compatibility space ("exca_xxxx")
 */
static inline u32 cb_readl(struct yenta_socket *socket, unsigned reg)
{
	u32 val = readl(socket->base + reg);
	debug("%p %04x %08x\n", socket, reg, val);
	return val;
}

static inline void cb_writel(struct yenta_socket *socket, unsigned reg, u32 val)
{
	debug("%p %04x %08x\n", socket, reg, val);
	writel(val, socket->base + reg);
	readl(socket->base + reg); /* avoid problems with PCI write posting */
}

static inline u8 config_readb(struct yenta_socket *socket, unsigned offset)
{
	u8 val;
	pci_read_config_byte(socket->dev, offset, &val);
	debug("%p %04x %02x\n", socket, offset, val);
	return val;
}

static inline u16 config_readw(struct yenta_socket *socket, unsigned offset)
{
	u16 val;
	pci_read_config_word(socket->dev, offset, &val);
	debug("%p %04x %04x\n", socket, offset, val);
	return val;
}

static inline u32 config_readl(struct yenta_socket *socket, unsigned offset)
{
	u32 val;
	pci_read_config_dword(socket->dev, offset, &val);
	debug("%p %04x %08x\n", socket, offset, val);
	return val;
}

static inline void config_writeb(struct yenta_socket *socket, unsigned offset, u8 val)
{
	debug("%p %04x %02x\n", socket, offset, val);
	pci_write_config_byte(socket->dev, offset, val);
}

static inline void config_writew(struct yenta_socket *socket, unsigned offset, u16 val)
{
	debug("%p %04x %04x\n", socket, offset, val);
	pci_write_config_word(socket->dev, offset, val);
}

static inline void config_writel(struct yenta_socket *socket, unsigned offset, u32 val)
{
	debug("%p %04x %08x\n", socket, offset, val);
	pci_write_config_dword(socket->dev, offset, val);
}

static inline u8 exca_readb(struct yenta_socket *socket, unsigned reg)
{
	u8 val = readb(socket->base + 0x800 + reg);
	debug("%p %04x %02x\n", socket, reg, val);
	return val;
}

static inline u8 exca_readw(struct yenta_socket *socket, unsigned reg)
{
	u16 val;
	val = readb(socket->base + 0x800 + reg);
	val |= readb(socket->base + 0x800 + reg + 1) << 8;
	debug("%p %04x %04x\n", socket, reg, val);
	return val;
}

static inline void exca_writeb(struct yenta_socket *socket, unsigned reg, u8 val)
{
	debug("%p %04x %02x\n", socket, reg, val);
	writeb(val, socket->base + 0x800 + reg);
	readb(socket->base + 0x800 + reg); /* PCI write posting... */
}

static void exca_writew(struct yenta_socket *socket, unsigned reg, u16 val)
{
	debug("%p %04x %04x\n", socket, reg, val);
	writeb(val, socket->base + 0x800 + reg);
	writeb(val >> 8, socket->base + 0x800 + reg + 1);

	/* PCI write posting... */
	readb(socket->base + 0x800 + reg);
	readb(socket->base + 0x800 + reg + 1);
}

/*
 * Ugh, mixed-mode cardbus and 16-bit pccard state: things depend
 * on what kind of card is inserted..
 */
static int yenta_get_status(struct pcmcia_socket *sock, unsigned int *value)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	unsigned int val;
	u32 state = cb_readl(socket, CB_SOCKET_STATE);

	val  = (state & CB_3VCARD) ? SS_3VCARD : 0;
	val |= (state & CB_XVCARD) ? SS_XVCARD : 0;
	val |= (state & (CB_5VCARD | CB_3VCARD | CB_XVCARD | CB_YVCARD)) ? 0 : SS_PENDING;
	val |= (state & (CB_CDETECT1 | CB_CDETECT2)) ? SS_PENDING : 0;


	if (state & CB_CBCARD) {
		val |= SS_CARDBUS;	
		val |= (state & CB_CARDSTS) ? SS_STSCHG : 0;
		val |= (state & (CB_CDETECT1 | CB_CDETECT2)) ? 0 : SS_DETECT;
		val |= (state & CB_PWRCYCLE) ? SS_POWERON | SS_READY : 0;
	} else if (state & CB_16BITCARD) {
		u8 status = exca_readb(socket, I365_STATUS);
		val |= ((status & I365_CS_DETECT) == I365_CS_DETECT) ? SS_DETECT : 0;
		if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
			val |= (status & I365_CS_STSCHG) ? 0 : SS_STSCHG;
		} else {
			val |= (status & I365_CS_BVD1) ? 0 : SS_BATDEAD;
			val |= (status & I365_CS_BVD2) ? 0 : SS_BATWARN;
		}
		val |= (status & I365_CS_WRPROT) ? SS_WRPROT : 0;
		val |= (status & I365_CS_READY) ? SS_READY : 0;
		val |= (status & I365_CS_POWERON) ? SS_POWERON : 0;
	}

	*value = val;
	return 0;
}

static void yenta_get_power(struct yenta_socket *socket, socket_state_t *state)
{
	if (!(cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) &&
	    (socket->flags & YENTA_16BIT_POWER_EXCA)) {
		u8 reg, vcc, vpp;

		reg = exca_readb(socket, I365_POWER);
		vcc = reg & I365_VCC_MASK;
		vpp = reg & I365_VPP1_MASK;
		state->Vcc = state->Vpp = 0;

		if (socket->flags & YENTA_16BIT_POWER_DF) {
			if (vcc == I365_VCC_3V)
				state->Vcc = 33;
			if (vcc == I365_VCC_5V)
				state->Vcc = 50;
			if (vpp == I365_VPP1_5V)
				state->Vpp = state->Vcc;
			if (vpp == I365_VPP1_12V)
				state->Vpp = 120;
		} else {
			if (reg & I365_VCC_5V) {
				state->Vcc = 50;
				if (vpp == I365_VPP1_5V)
					state->Vpp = 50;
				if (vpp == I365_VPP1_12V)
					state->Vpp = 120;
			}
		}
	} else {
		u32 control;

		control = cb_readl(socket, CB_SOCKET_CONTROL);

		switch (control & CB_SC_VCC_MASK) {
		case CB_SC_VCC_5V: state->Vcc = 50; break;
		case CB_SC_VCC_3V: state->Vcc = 33; break;
		default: state->Vcc = 0;
		}

		switch (control & CB_SC_VPP_MASK) {
		case CB_SC_VPP_12V: state->Vpp = 120; break;
		case CB_SC_VPP_5V: state->Vpp = 50; break;
		case CB_SC_VPP_3V: state->Vpp = 33; break;
		default: state->Vpp = 0;
		}
	}
}

static int yenta_get_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u8 reg;
	u32 control;

	control = cb_readl(socket, CB_SOCKET_CONTROL);

	yenta_get_power(socket, state);
	state->io_irq = socket->io_irq;

	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		u16 bridge = config_readw(socket, CB_BRIDGE_CONTROL);
		if (bridge & CB_BRIDGE_CRST)
			state->flags |= SS_RESET;
		return 0;
	}

	/* 16-bit card state.. */
	reg = exca_readb(socket, I365_POWER);
	state->flags = (reg & I365_PWR_AUTO) ? SS_PWR_AUTO : 0;
	state->flags |= (reg & I365_PWR_OUT) ? SS_OUTPUT_ENA : 0;

	reg = exca_readb(socket, I365_INTCTL);
	state->flags |= (reg & I365_PC_RESET) ? 0 : SS_RESET;
	state->flags |= (reg & I365_PC_IOCARD) ? SS_IOCARD : 0;

	reg = exca_readb(socket, I365_CSCINT);
	state->csc_mask = (reg & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (state->flags & SS_IOCARD) {
		state->csc_mask |= (reg & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		state->csc_mask |= (reg & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		state->csc_mask |= (reg & I365_CSC_BVD2) ? SS_BATWARN : 0;
		state->csc_mask |= (reg & I365_CSC_READY) ? SS_READY : 0;
	}

	return 0;
}

static void yenta_set_power(struct yenta_socket *socket, socket_state_t *state)
{
	/* some birdges require to use the ExCA registers to power 16bit cards */
	if (!(cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) &&
	    (socket->flags & YENTA_16BIT_POWER_EXCA)) {
		u8 reg, old;
		reg = old = exca_readb(socket, I365_POWER);
		reg &= ~(I365_VCC_MASK | I365_VPP1_MASK | I365_VPP2_MASK);

		/* i82365SL-DF style */
		if (socket->flags & YENTA_16BIT_POWER_DF) {
			switch (state->Vcc) {
			case 33: reg |= I365_VCC_3V; break;
			case 50: reg |= I365_VCC_5V; break;
			default: reg = 0; break;
			}
			switch (state->Vpp) {
			case 33:
			case 50: reg |= I365_VPP1_5V; break;
			case 120: reg |= I365_VPP1_12V; break;
			}
		} else {
			/* i82365SL-B style */
			switch (state->Vcc) {
			case 50: reg |= I365_VCC_5V; break;
			default: reg = 0; break;
			}
			switch (state->Vpp) {
			case 50: reg |= I365_VPP1_5V | I365_VPP2_5V; break;
			case 120: reg |= I365_VPP1_12V | I365_VPP2_12V; break;
			}
		}

		if (reg != old)
			exca_writeb(socket, I365_POWER, reg);
	} else {
		u32 reg = 0;	/* CB_SC_STPCLK? */
		switch (state->Vcc) {
		case 33: reg = CB_SC_VCC_3V; break;
		case 50: reg = CB_SC_VCC_5V; break;
		default: reg = 0; break;
		}
		switch (state->Vpp) {
		case 33:  reg |= CB_SC_VPP_3V; break;
		case 50:  reg |= CB_SC_VPP_5V; break;
		case 120: reg |= CB_SC_VPP_12V; break;
		}
		if (reg != cb_readl(socket, CB_SOCKET_CONTROL))
			cb_writel(socket, CB_SOCKET_CONTROL, reg);
	}
}

static int yenta_set_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u16 bridge;

	yenta_set_power(socket, state);
	socket->io_irq = state->io_irq;
	bridge = config_readw(socket, CB_BRIDGE_CONTROL) & ~(CB_BRIDGE_CRST | CB_BRIDGE_INTR);
	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		u8 intr;
		bridge |= (state->flags & SS_RESET) ? CB_BRIDGE_CRST : 0;

		/* ISA interrupt control? */
		intr = exca_readb(socket, I365_INTCTL);
		intr = (intr & ~0xf);
		if (!socket->cb_irq) {
			intr |= state->io_irq;
			bridge |= CB_BRIDGE_INTR;
		}
		exca_writeb(socket, I365_INTCTL, intr);
	}  else {
		u8 reg;

		reg = exca_readb(socket, I365_INTCTL) & (I365_RING_ENA | I365_INTR_ENA);
		reg |= (state->flags & SS_RESET) ? 0 : I365_PC_RESET;
		reg |= (state->flags & SS_IOCARD) ? I365_PC_IOCARD : 0;
		if (state->io_irq != socket->cb_irq) {
			reg |= state->io_irq;
			bridge |= CB_BRIDGE_INTR;
		}
		exca_writeb(socket, I365_INTCTL, reg);

		reg = exca_readb(socket, I365_POWER) & (I365_VCC_MASK|I365_VPP1_MASK);
		reg |= I365_PWR_NORESET;
		if (state->flags & SS_PWR_AUTO) reg |= I365_PWR_AUTO;
		if (state->flags & SS_OUTPUT_ENA) reg |= I365_PWR_OUT;
		if (exca_readb(socket, I365_POWER) != reg)
			exca_writeb(socket, I365_POWER, reg);

		/* CSC interrupt: no ISA irq for CSC */
		reg = I365_CSC_DETECT;
		if (state->flags & SS_IOCARD) {
			if (state->csc_mask & SS_STSCHG) reg |= I365_CSC_STSCHG;
		} else {
			if (state->csc_mask & SS_BATDEAD) reg |= I365_CSC_BVD1;
			if (state->csc_mask & SS_BATWARN) reg |= I365_CSC_BVD2;
			if (state->csc_mask & SS_READY) reg |= I365_CSC_READY;
		}
		exca_writeb(socket, I365_CSCINT, reg);
		exca_readb(socket, I365_CSC);
		if(sock->zoom_video)
			sock->zoom_video(sock, state->flags & SS_ZVCARD);
	}
	config_writew(socket, CB_BRIDGE_CONTROL, bridge);
	/* Socket event mask: get card insert/remove events.. */
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CDMASK);
	return 0;
}

static int yenta_set_io_map(struct pcmcia_socket *sock, struct pccard_io_map *io)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int map;
	unsigned char ioctl, addr, enable;

	map = io->map;

	if (map > 1)
		return -EINVAL;

	enable = I365_ENA_IO(map);
	addr = exca_readb(socket, I365_ADDRWIN);

	/* Disable the window before changing it.. */
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writew(socket, I365_IO(map)+I365_W_START, io->start);
	exca_writew(socket, I365_IO(map)+I365_W_STOP, io->stop);

	ioctl = exca_readb(socket, I365_IOCTL) & ~I365_IOCTL_MASK(map);
	if (io->flags & MAP_0WS) ioctl |= I365_IOCTL_0WS(map);
	if (io->flags & MAP_16BIT) ioctl |= I365_IOCTL_16BIT(map);
	if (io->flags & MAP_AUTOSZ) ioctl |= I365_IOCTL_IOCS16(map);
	exca_writeb(socket, I365_IOCTL, ioctl);

	if (io->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}

static int yenta_set_mem_map(struct pcmcia_socket *sock, struct pccard_mem_map *mem)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	struct pci_bus_region region;
	int map;
	unsigned char addr, enable;
	unsigned int start, stop, card_start;
	unsigned short word;

	pcibios_resource_to_bus(socket->dev, &region, mem->res);

	map = mem->map;
	start = region.start;
	stop = region.end;
	card_start = mem->card_start;

	if (map > 4 || start > stop || ((start ^ stop) >> 24) ||
	    (card_start >> 26) || mem->speed > 1000)
		return -EINVAL;

	enable = I365_ENA_MEM(map);
	addr = exca_readb(socket, I365_ADDRWIN);
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writeb(socket, CB_MEM_PAGE(map), start >> 24);

	word = (start >> 12) & 0x0fff;
	if (mem->flags & MAP_16BIT)
		word |= I365_MEM_16BIT;
	if (mem->flags & MAP_0WS)
		word |= I365_MEM_0WS;
	exca_writew(socket, I365_MEM(map) + I365_W_START, word);

	word = (stop >> 12) & 0x0fff;
	switch (to_cycles(mem->speed)) {
		case 0: break;
		case 1:  word |= I365_MEM_WS0; break;
		case 2:  word |= I365_MEM_WS1; break;
		default: word |= I365_MEM_WS1 | I365_MEM_WS0; break;
	}
	exca_writew(socket, I365_MEM(map) + I365_W_STOP, word);

	word = ((card_start - start) >> 12) & 0x3fff;
	if (mem->flags & MAP_WRPROT)
		word |= I365_MEM_WRPROT;
	if (mem->flags & MAP_ATTRIB)
		word |= I365_MEM_REG;
	exca_writew(socket, I365_MEM(map) + I365_W_OFF, word);

	if (mem->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}



static irqreturn_t yenta_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int events;
	struct yenta_socket *socket = (struct yenta_socket *) dev_id;
	u8 csc;
	u32 cb_event;

	/* Clear interrupt status for the event */
	cb_event = cb_readl(socket, CB_SOCKET_EVENT);
	cb_writel(socket, CB_SOCKET_EVENT, cb_event);

	csc = exca_readb(socket, I365_CSC);

	events = (cb_event & (CB_CD1EVENT | CB_CD2EVENT)) ? SS_DETECT : 0 ;
	events |= (csc & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
		events |= (csc & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		events |= (csc & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		events |= (csc & I365_CSC_BVD2) ? SS_BATWARN : 0;
		events |= (csc & I365_CSC_READY) ? SS_READY : 0;
	}

	if (events)
		pcmcia_parse_events(&socket->socket, events);

	if (cb_event || csc)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static void yenta_interrupt_wrapper(unsigned long data)
{
	struct yenta_socket *socket = (struct yenta_socket *) data;

	yenta_interrupt(0, (void *)socket, NULL);
	socket->poll_timer.expires = jiffies + HZ;
	add_timer(&socket->poll_timer);
}

static void yenta_clear_maps(struct yenta_socket *socket)
{
	int i;
	struct resource res = { .start = 0, .end = 0x0fff };
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { .res = &res, };

	yenta_set_socket(&socket->socket, &dead_socket);
	for (i = 0; i < 2; i++) {
		io.map = i;
		yenta_set_io_map(&socket->socket, &io);
	}
	for (i = 0; i < 5; i++) {
		mem.map = i;
		yenta_set_mem_map(&socket->socket, &mem);
	}
}

/* redoes voltage interrogation if required */
static void yenta_interrogate(struct yenta_socket *socket)
{
	u32 state;

	state = cb_readl(socket, CB_SOCKET_STATE);
	if (!(state & (CB_5VCARD | CB_3VCARD | CB_XVCARD | CB_YVCARD)) ||
	    (state & (CB_CDETECT1 | CB_CDETECT2 | CB_NOTACARD | CB_BADVCCREQ)) ||
	    ((state & (CB_16BITCARD | CB_CBCARD)) == (CB_16BITCARD | CB_CBCARD)))
		cb_writel(socket, CB_SOCKET_FORCE, CB_CVSTEST);
}

/* Called at resume and initialization events */
static int yenta_sock_init(struct pcmcia_socket *sock)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	exca_writeb(socket, I365_GBLCTL, 0x00);
	exca_writeb(socket, I365_GENCTL, 0x00);

	/* Redo card voltage interrogation */
	yenta_interrogate(socket);

	yenta_clear_maps(socket);

	if (socket->type && socket->type->sock_init)
		socket->type->sock_init(socket);

	/* Re-enable CSC interrupts */
	cb_writel(socket, CB_SOCKET_MASK, CB_CDMASK);

	return 0;
}

static int yenta_sock_suspend(struct pcmcia_socket *sock)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	/* Disable CSC interrupts */
	cb_writel(socket, CB_SOCKET_MASK, 0x0);

	return 0;
}

/*
 * Use an adaptive allocation for the memory resource,
 * sometimes the memory behind pci bridges is limited:
 * 1/8 of the size of the io window of the parent.
 * max 4 MB, min 16 kB. We try very hard to not get below
 * the "ACC" values, though.
 */
#define BRIDGE_MEM_MAX 4*1024*1024
#define BRIDGE_MEM_ACC 128*1024
#define BRIDGE_MEM_MIN 16*1024

#define BRIDGE_IO_MAX 512
#define BRIDGE_IO_ACC 256
#define BRIDGE_IO_MIN 32

#ifndef PCIBIOS_MIN_CARDBUS_IO
#define PCIBIOS_MIN_CARDBUS_IO PCIBIOS_MIN_IO
#endif

static int yenta_search_one_res(struct resource *root, struct resource *res,
				u32 min)
{
	u32 align, size, start, end;

	if (res->flags & IORESOURCE_IO) {
		align = 1024;
		size = BRIDGE_IO_MAX;
		start = PCIBIOS_MIN_CARDBUS_IO;
		end = ~0U;
	} else {
		unsigned long avail = root->end - root->start;
		int i;
		size = BRIDGE_MEM_MAX;
		if (size > avail/8) {
			size=(avail+1)/8;
			/* round size down to next power of 2 */
			i = 0;
			while ((size /= 2) != 0)
				i++;
			size = 1 << i;
		}
		if (size < min)
			size = min;
		align = size;
		start = PCIBIOS_MIN_MEM;
		end = ~0U;
	}

	do {
		if (allocate_resource(root, res, size, start, end, align,
				      NULL, NULL)==0) {
			return 1;
		}
		size = size/2;
		align = size;
	} while (size >= min);

	return 0;
}


static int yenta_search_res(struct yenta_socket *socket, struct resource *res,
			    u32 min)
{
	int i;
	for (i=0; i<PCI_BUS_NUM_RESOURCES; i++) {
		struct resource * root = socket->dev->bus->resource[i];
		if (!root)
			continue;

		if ((res->flags ^ root->flags) &
		    (IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_PREFETCH))
			continue; /* Wrong type */

		if (yenta_search_one_res(root, res, min))
			return 1;
	}
	return 0;
}

static int yenta_allocate_res(struct yenta_socket *socket, int nr, unsigned type, int addr_start, int addr_end)
{
	struct resource *root, *res;
	struct pci_bus_region region;
	unsigned mask;

	res = socket->dev->resource + PCI_BRIDGE_RESOURCES + nr;
	/* Already allocated? */
	if (res->parent)
		return 0;

	/* The granularity of the memory limit is 4kB, on IO it's 4 bytes */
	mask = ~0xfff;
	if (type & IORESOURCE_IO)
		mask = ~3;

	res->name = socket->dev->subordinate->name;
	res->flags = type;

	region.start = config_readl(socket, addr_start) & mask;
	region.end = config_readl(socket, addr_end) | ~mask;
	if (region.start && region.end > region.start && !override_bios) {
		pcibios_bus_to_resource(socket->dev, res, &region);
		root = pci_find_parent_resource(socket->dev, res);
		if (root && (request_resource(root, res) == 0))
			return 0;
		printk(KERN_INFO "yenta %s: Preassigned resource %d busy or not available, reconfiguring...\n",
				pci_name(socket->dev), nr);
	}

	if (type & IORESOURCE_IO) {
		if ((yenta_search_res(socket, res, BRIDGE_IO_MAX)) ||
		    (yenta_search_res(socket, res, BRIDGE_IO_ACC)) ||
		    (yenta_search_res(socket, res, BRIDGE_IO_MIN)))
			return 1;
	} else {
		if (type & IORESOURCE_PREFETCH) {
			if ((yenta_search_res(socket, res, BRIDGE_MEM_MAX)) ||
			    (yenta_search_res(socket, res, BRIDGE_MEM_ACC)) ||
			    (yenta_search_res(socket, res, BRIDGE_MEM_MIN)))
				return 1;
			/* Approximating prefetchable by non-prefetchable */
			res->flags = IORESOURCE_MEM;
		}
		if ((yenta_search_res(socket, res, BRIDGE_MEM_MAX)) ||
		    (yenta_search_res(socket, res, BRIDGE_MEM_ACC)) ||
		    (yenta_search_res(socket, res, BRIDGE_MEM_MIN)))
			return 1;
	}

	printk(KERN_INFO "yenta %s: no resource of type %x available, trying to continue...\n",
	       pci_name(socket->dev), type);
	res->start = res->end = res->flags = 0;
	return 0;
}

/*
 * Allocate the bridge mappings for the device..
 */
static void yenta_allocate_resources(struct yenta_socket *socket)
{
	int program = 0;
	program += yenta_allocate_res(socket, 0, IORESOURCE_IO,
			   PCI_CB_IO_BASE_0, PCI_CB_IO_LIMIT_0);
	program += yenta_allocate_res(socket, 1, IORESOURCE_IO,
			   PCI_CB_IO_BASE_1, PCI_CB_IO_LIMIT_1);
	program += yenta_allocate_res(socket, 2, IORESOURCE_MEM|IORESOURCE_PREFETCH,
			   PCI_CB_MEMORY_BASE_0, PCI_CB_MEMORY_LIMIT_0);
	program += yenta_allocate_res(socket, 3, IORESOURCE_MEM,
			   PCI_CB_MEMORY_BASE_1, PCI_CB_MEMORY_LIMIT_1);
	if (program)
		pci_setup_cardbus(socket->dev->subordinate);
}


/*
 * Free the bridge mappings for the device..
 */
static void yenta_free_resources(struct yenta_socket *socket)
{
	int i;
	for (i=0;i<4;i++) {
		struct resource *res;
		res = socket->dev->resource + PCI_BRIDGE_RESOURCES + i;
		if (res->start != 0 && res->end != 0)
			release_resource(res);
		res->start = res->end = res->flags = 0;
	}
}


/*
 * Close it down - release our resources and go home..
 */
static void yenta_close(struct pci_dev *dev)
{
	struct yenta_socket *sock = pci_get_drvdata(dev);

	/* we don't want a dying socket registered */
	pcmcia_unregister_socket(&sock->socket);
	
	/* Disable all events so we don't die in an IRQ storm */
	cb_writel(sock, CB_SOCKET_MASK, 0x0);
	exca_writeb(sock, I365_CSCINT, 0);

	if (sock->cb_irq)
		free_irq(sock->cb_irq, sock);
	else
		del_timer_sync(&sock->poll_timer);

	if (sock->base)
		iounmap(sock->base);
	yenta_free_resources(sock);

	pci_release_regions(dev);
	pci_disable_device(dev);
	pci_set_drvdata(dev, NULL);
}


static struct pccard_operations yenta_socket_operations = {
	.init			= yenta_sock_init,
	.suspend		= yenta_sock_suspend,
	.get_status		= yenta_get_status,
	.get_socket		= yenta_get_socket,
	.set_socket		= yenta_set_socket,
	.set_io_map		= yenta_set_io_map,
	.set_mem_map		= yenta_set_mem_map,
};


#include "ti113x.h"
#include "ricoh.h"
#include "topic.h"
#include "o2micro.h"

enum {
	CARDBUS_TYPE_DEFAULT = -1,
	CARDBUS_TYPE_TI,
	CARDBUS_TYPE_TI113X,
	CARDBUS_TYPE_TI12XX,
	CARDBUS_TYPE_TI1250,
	CARDBUS_TYPE_RICOH,
	CARDBUS_TYPE_TOPIC95,
	CARDBUS_TYPE_TOPIC97,
	CARDBUS_TYPE_O2MICRO,
	CARDBUS_TYPE_ENE,
};

/*
 * Different cardbus controllers have slightly different
 * initialization sequences etc details. List them here..
 */
static struct cardbus_type cardbus_type[] = {
	[CARDBUS_TYPE_TI]	= {
		.override	= ti_override,
		.save_state	= ti_save_state,
		.restore_state	= ti_restore_state,
		.sock_init	= ti_init,
	},
	[CARDBUS_TYPE_TI113X]	= {
		.override	= ti113x_override,
		.save_state	= ti_save_state,
		.restore_state	= ti_restore_state,
		.sock_init	= ti_init,
	},
	[CARDBUS_TYPE_TI12XX]	= {
		.override	= ti12xx_override,
		.save_state	= ti_save_state,
		.restore_state	= ti_restore_state,
		.sock_init	= ti_init,
	},
	[CARDBUS_TYPE_TI1250]	= {
		.override	= ti1250_override,
		.save_state	= ti_save_state,
		.restore_state	= ti_restore_state,
		.sock_init	= ti_init,
	},
	[CARDBUS_TYPE_RICOH]	= {
		.override	= ricoh_override,
		.save_state	= ricoh_save_state,
		.restore_state	= ricoh_restore_state,
	},
	[CARDBUS_TYPE_TOPIC95]	= {
		.override	= topic95_override,
	},
	[CARDBUS_TYPE_TOPIC97]	= {
		.override	= topic97_override,
	},
	[CARDBUS_TYPE_O2MICRO]	= {
		.override	= o2micro_override,
		.restore_state	= o2micro_restore_state,
	},
	[CARDBUS_TYPE_ENE]	= {
		.override	= ene_override,
		.save_state	= ti_save_state,
		.restore_state	= ti_restore_state,
		.sock_init	= ti_init,
	},
};


/*
 * Only probe "regular" interrupts, don't
 * touch dangerous spots like the mouse irq,
 * because there are mice that apparently
 * get really confused if they get fondled
 * too intimately.
 *
 * Default to 11, 10, 9, 7, 6, 5, 4, 3.
 */
static u32 isa_interrupts = 0x0ef8;

static unsigned int yenta_probe_irq(struct yenta_socket *socket, u32 isa_irq_mask)
{
	int i;
	unsigned long val;
	u32 mask;

	/*
	 * Probe for usable interrupts using the force
	 * register to generate bogus card status events.
	 */
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CSTSMASK);
	exca_writeb(socket, I365_CSCINT, 0);
	val = probe_irq_on() & isa_irq_mask;
	for (i = 1; i < 16; i++) {
		if (!((val >> i) & 1))
			continue;
		exca_writeb(socket, I365_CSCINT, I365_CSC_STSCHG | (i << 4));
		cb_writel(socket, CB_SOCKET_FORCE, CB_FCARDSTS);
		udelay(100);
		cb_writel(socket, CB_SOCKET_EVENT, -1);
	}
	cb_writel(socket, CB_SOCKET_MASK, 0);
	exca_writeb(socket, I365_CSCINT, 0);

	mask = probe_irq_mask(val) & 0xffff;

	return mask;
}


/* interrupt handler, only used during probing */
static irqreturn_t yenta_probe_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct yenta_socket *socket = (struct yenta_socket *) dev_id;
	u8 csc;
        u32 cb_event;

	/* Clear interrupt status for the event */
	cb_event = cb_readl(socket, CB_SOCKET_EVENT);
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	csc = exca_readb(socket, I365_CSC);

	if (cb_event || csc) {
		socket->probe_status = 1;
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/* probes the PCI interrupt, use only on override functions */
static int yenta_probe_cb_irq(struct yenta_socket *socket)
{
	if (!socket->cb_irq)
		return -1;

	socket->probe_status = 0;

	if (request_irq(socket->cb_irq, yenta_probe_handler, SA_SHIRQ, "yenta", socket)) {
		printk(KERN_WARNING "Yenta: request_irq() in yenta_probe_cb_irq() failed!\n");
		return -1;
	}

	/* generate interrupt, wait */
	exca_writeb(socket, I365_CSCINT, I365_CSC_STSCHG);
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CSTSMASK);
	cb_writel(socket, CB_SOCKET_FORCE, CB_FCARDSTS);

	msleep(100);

	/* disable interrupts */
	cb_writel(socket, CB_SOCKET_MASK, 0);
	exca_writeb(socket, I365_CSCINT, 0);
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	exca_readb(socket, I365_CSC);

	free_irq(socket->cb_irq, socket);

	return (int) socket->probe_status;
}



/*
 * Set static data that doesn't need re-initializing..
 */
static void yenta_get_socket_capabilities(struct yenta_socket *socket, u32 isa_irq_mask)
{
	socket->socket.pci_irq = socket->cb_irq;
	if (isa_probe)
		socket->socket.irq_mask = yenta_probe_irq(socket, isa_irq_mask);
	else
		socket->socket.irq_mask = 0;

	printk(KERN_INFO "Yenta: ISA IRQ mask 0x%04x, PCI irq %d\n",
	       socket->socket.irq_mask, socket->cb_irq);
}

/*
 * Initialize the standard cardbus registers
 */
static void yenta_config_init(struct yenta_socket *socket)
{
	u16 bridge;
	struct pci_dev *dev = socket->dev;
	struct pci_bus_region region;

	pcibios_resource_to_bus(socket->dev, &region, &dev->resource[0]);

	config_writel(socket, CB_LEGACY_MODE_BASE, 0);
	config_writel(socket, PCI_BASE_ADDRESS_0, region.start);
	config_writew(socket, PCI_COMMAND,
			PCI_COMMAND_IO |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER |
			PCI_COMMAND_WAIT);

	/* MAGIC NUMBERS! Fixme */
	config_writeb(socket, PCI_CACHE_LINE_SIZE, L1_CACHE_BYTES / 4);
	config_writeb(socket, PCI_LATENCY_TIMER, 168);
	config_writel(socket, PCI_PRIMARY_BUS,
		(176 << 24) |			   /* sec. latency timer */
		(dev->subordinate->subordinate << 16) | /* subordinate bus */
		(dev->subordinate->secondary << 8) |  /* secondary bus */
		dev->subordinate->primary);		   /* primary bus */

	/*
	 * Set up the bridging state:
	 *  - enable write posting.
	 *  - memory window 0 prefetchable, window 1 non-prefetchable
	 *  - PCI interrupts enabled if a PCI interrupt exists..
	 */
	bridge = config_readw(socket, CB_BRIDGE_CONTROL);
	bridge &= ~(CB_BRIDGE_CRST | CB_BRIDGE_PREFETCH1 | CB_BRIDGE_ISAEN | CB_BRIDGE_VGAEN);
	bridge |= CB_BRIDGE_PREFETCH0 | CB_BRIDGE_POSTEN;
	config_writew(socket, CB_BRIDGE_CONTROL, bridge);
}

/*
 * Initialize a cardbus controller. Make sure we have a usable
 * interrupt, and that we can map the cardbus area. Fill in the
 * socket information structure..
 */
static int __devinit yenta_probe (struct pci_dev *dev, const struct pci_device_id *id)
{
	struct yenta_socket *socket;
	int ret;

	/*
	 * If we failed to assign proper bus numbers for this cardbus
	 * controller during PCI probe, its subordinate pci_bus is NULL.
	 * Bail out if so.
	 */
	if (!dev->subordinate) {
		printk(KERN_ERR "Yenta: no bus associated with %s! "
			"(try 'pci=assign-busses')\n", pci_name(dev));
		return -ENODEV;
	}

	socket = kmalloc(sizeof(struct yenta_socket), GFP_KERNEL);
	if (!socket)
		return -ENOMEM;
	memset(socket, 0, sizeof(*socket));

	/* prepare pcmcia_socket */
	socket->socket.ops = &yenta_socket_operations;
	socket->socket.resource_ops = &pccard_nonstatic_ops;
	socket->socket.dev.dev = &dev->dev;
	socket->socket.driver_data = socket;
	socket->socket.owner = THIS_MODULE;
	socket->socket.features = SS_CAP_PAGE_REGS | SS_CAP_PCCARD;
	socket->socket.map_size = 0x1000;
	socket->socket.cb_dev = dev;

	/* prepare struct yenta_socket */
	socket->dev = dev;
	pci_set_drvdata(dev, socket);

	/*
	 * Do some basic sanity checking..
	 */
	if (pci_enable_device(dev)) {
		ret = -EBUSY;
		goto free;
	}

	ret = pci_request_regions(dev, "yenta_socket");
	if (ret)
		goto disable;

	if (!pci_resource_start(dev, 0)) {
		printk(KERN_ERR "No cardbus resource!\n");
		ret = -ENODEV;
		goto release;
	}

	/*
	 * Ok, start setup.. Map the cardbus registers,
	 * and request the IRQ.
	 */
	socket->base = ioremap(pci_resource_start(dev, 0), 0x1000);
	if (!socket->base) {
		ret = -ENOMEM;
		goto release;
	}

	/*
	 * report the subsystem vendor and device for help debugging
	 * the irq stuff...
	 */
	printk(KERN_INFO "Yenta: CardBus bridge found at %s [%04x:%04x]\n",
		pci_name(dev), dev->subsystem_vendor, dev->subsystem_device);

	yenta_config_init(socket);

	/* Disable all events */
	cb_writel(socket, CB_SOCKET_MASK, 0x0);

	/* Set up the bridge regions.. */
	yenta_allocate_resources(socket);

	socket->cb_irq = dev->irq;

	/* Do we have special options for the device? */
	if (id->driver_data != CARDBUS_TYPE_DEFAULT &&
	    id->driver_data < ARRAY_SIZE(cardbus_type)) {
		socket->type = &cardbus_type[id->driver_data];

		ret = socket->type->override(socket);
		if (ret < 0)
			goto unmap;
	}

	/* We must finish initialization here */

	if (!socket->cb_irq || request_irq(socket->cb_irq, yenta_interrupt, SA_SHIRQ, "yenta", socket)) {
		/* No IRQ or request_irq failed. Poll */
		socket->cb_irq = 0; /* But zero is a valid IRQ number. */
		init_timer(&socket->poll_timer);
		socket->poll_timer.function = yenta_interrupt_wrapper;
		socket->poll_timer.data = (unsigned long)socket;
		socket->poll_timer.expires = jiffies + HZ;
		add_timer(&socket->poll_timer);
		printk(KERN_INFO "Yenta: no PCI IRQ, CardBus support disabled for this socket.\n"
		       KERN_INFO "Yenta: check your BIOS CardBus, BIOS IRQ or ACPI settings.\n");
	} else {
		socket->socket.features |= SS_CAP_CARDBUS;
	}

	/* Figure out what the dang thing can do for the PCMCIA layer... */
	yenta_interrogate(socket);
	yenta_get_socket_capabilities(socket, isa_interrupts);
	printk(KERN_INFO "Socket status: %08x\n", cb_readl(socket, CB_SOCKET_STATE));

	/* Register it with the pcmcia layer.. */
	ret = pcmcia_register_socket(&socket->socket);
	if (ret == 0)
		goto out;

 unmap:
	iounmap(socket->base);
 release:
	pci_release_regions(dev);
 disable:
	pci_disable_device(dev);
 free:
	kfree(socket);
 out:
	return ret;
}


static int yenta_dev_suspend (struct pci_dev *dev, pm_message_t state)
{
	struct yenta_socket *socket = pci_get_drvdata(dev);
	int ret;

	ret = pcmcia_socket_dev_suspend(&dev->dev, state);

	if (socket) {
		if (socket->type && socket->type->save_state)
			socket->type->save_state(socket);

		/* FIXME: pci_save_state needs to have a better interface */
		pci_save_state(dev);
		pci_read_config_dword(dev, 16*4, &socket->saved_state[0]);
		pci_read_config_dword(dev, 17*4, &socket->saved_state[1]);
		pci_disable_device(dev);

		/*
		 * Some laptops (IBM T22) do not like us putting the Cardbus
		 * bridge into D3.  At a guess, some other laptop will
		 * probably require this, so leave it commented out for now.
		 */
		/* pci_set_power_state(dev, 3); */
	}

	return ret;
}


static int yenta_dev_resume (struct pci_dev *dev)
{
	struct yenta_socket *socket = pci_get_drvdata(dev);

	if (socket) {
		pci_set_power_state(dev, 0);
		/* FIXME: pci_restore_state needs to have a better interface */
		pci_restore_state(dev);
		pci_write_config_dword(dev, 16*4, socket->saved_state[0]);
		pci_write_config_dword(dev, 17*4, socket->saved_state[1]);
		pci_enable_device(dev);
		pci_set_master(dev);

		if (socket->type && socket->type->restore_state)
			socket->type->restore_state(socket);
	}

	return pcmcia_socket_dev_resume(&dev->dev);
}


#define CB_ID(vend,dev,type)				\
	{						\
		.vendor		= vend,			\
		.device		= dev,			\
		.subvendor	= PCI_ANY_ID,		\
		.subdevice	= PCI_ANY_ID,		\
		.class		= PCI_CLASS_BRIDGE_CARDBUS << 8, \
		.class_mask	= ~0,			\
		.driver_data	= CARDBUS_TYPE_##type,	\
	}

static struct pci_device_id yenta_table [] = {
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1031, TI),

	/*
	 * TBD: Check if these TI variants can use more
	 * advanced overrides instead.  (I can't get the
	 * data sheets for these devices. --rmk)
	 */
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1210, TI),

	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1130, TI113X),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1131, TI113X),

	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1211, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1220, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1221, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1225, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1251A, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1251B, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1420, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1450, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1451A, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1510, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1520, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1620, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_4410, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_4450, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_4451, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_4510, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_4520, TI12XX),

	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1250, TI1250),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1410, TI1250),

	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_XX21_XX11, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_X515, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_X420, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_X620, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_7410, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_7510, TI12XX),
	CB_ID(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_7610, TI12XX),

	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_710, TI12XX),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_712, TI12XX),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_720, TI12XX),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_722, TI12XX),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_1211, ENE),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_1225, ENE),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_1410, ENE),
	CB_ID(PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_1420, ENE),

	CB_ID(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C465, RICOH),
	CB_ID(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C466, RICOH),
	CB_ID(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C475, RICOH),
	CB_ID(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C476, RICOH),
	CB_ID(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C478, RICOH),

	CB_ID(PCI_VENDOR_ID_TOSHIBA, PCI_DEVICE_ID_TOSHIBA_TOPIC95, TOPIC95),
	CB_ID(PCI_VENDOR_ID_TOSHIBA, PCI_DEVICE_ID_TOSHIBA_TOPIC97, TOPIC97),
	CB_ID(PCI_VENDOR_ID_TOSHIBA, PCI_DEVICE_ID_TOSHIBA_TOPIC100, TOPIC97),

	CB_ID(PCI_VENDOR_ID_O2, PCI_ANY_ID, O2MICRO),

	/* match any cardbus bridge */
	CB_ID(PCI_ANY_ID, PCI_ANY_ID, DEFAULT),
	{ /* all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, yenta_table);


static struct pci_driver yenta_cardbus_driver = {
	.name		= "yenta_cardbus",
	.id_table	= yenta_table,
	.probe		= yenta_probe,
	.remove		= __devexit_p(yenta_close),
	.suspend	= yenta_dev_suspend,
	.resume		= yenta_dev_resume,
};


static int __init yenta_socket_init(void)
{
	return pci_register_driver (&yenta_cardbus_driver);
}


static void __exit yenta_socket_exit (void)
{
	pci_unregister_driver (&yenta_cardbus_driver);
}


module_init(yenta_socket_init);
module_exit(yenta_socket_exit);

MODULE_LICENSE("GPL");
