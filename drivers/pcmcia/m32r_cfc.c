/*
 *  drivers/pcmcia/m32r_cfc.c
 *
 *  Device driver for the CFC functionality of M32R.
 *
 *  Copyright (c) 2001, 2002, 2003, 2004
 *    Hiroyuki Kondo, Naoto Sugai, Hayato Fujiwara
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>

#undef MAX_IO_WIN	/* FIXME */
#define MAX_IO_WIN 1
#undef MAX_WIN		/* FIXME */
#define MAX_WIN 1

#include "m32r_cfc.h"

#ifdef CONFIG_PCMCIA_DEBUG
static int m32r_cfc_debug;
module_param(m32r_cfc_debug, int, 0644);
#define debug(lvl, fmt, arg...) do {				\
	if (m32r_cfc_debug > (lvl))				\
		printk(KERN_DEBUG "m32r_cfc: " fmt , ## arg);	\
} while (0)
#else
#define debug(n, args...) do { } while (0)
#endif

/* Poll status interval -- 0 means default to interrupt */
static int poll_interval = 0;

typedef enum pcc_space { as_none = 0, as_comm, as_attr, as_io } pcc_as_t;

typedef struct pcc_socket {
	u_short			type, flags;
	struct pcmcia_socket	socket;
	unsigned int		number;
	unsigned int		ioaddr;
	u_long			mapaddr;
	u_long			base;	/* PCC register base */
	u_char			cs_irq1, cs_irq2, intr;
	pccard_io_map		io_map[MAX_IO_WIN];
	pccard_mem_map		mem_map[MAX_WIN];
	u_char			io_win;
	u_char			mem_win;
	pcc_as_t		current_space;
	u_char			last_iodbex;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc;
#endif
} pcc_socket_t;

static int pcc_sockets = 0;
static pcc_socket_t socket[M32R_MAX_PCC] = {
	{ 0, }, /* ... */
};

/*====================================================================*/

static unsigned int pcc_get(u_short, unsigned int);
static void pcc_set(u_short, unsigned int , unsigned int );

static DEFINE_SPINLOCK(pcc_lock);

#if !defined(CONFIG_PLAT_USRV)
static inline u_long pcc_port2addr(unsigned long port, int size) {
	u_long addr = 0;
	u_long odd;

	if (size == 1) {	/* byte access */
		odd = (port&1) << 11;
		port -= port & 1;
		addr = CFC_IO_MAPBASE_BYTE - CFC_IOPORT_BASE + odd + port;
	} else if (size == 2)
		addr = CFC_IO_MAPBASE_WORD - CFC_IOPORT_BASE + port;

	return addr;
}
#else	/* CONFIG_PLAT_USRV */
static inline u_long pcc_port2addr(unsigned long port, int size) {
	u_long odd;
	u_long addr = ((port - CFC_IOPORT_BASE) & 0xf000) << 8;

	if (size == 1) {	/* byte access */
		odd = port & 1;
		port -= odd;
		odd <<= 11;
		addr = (addr | CFC_IO_MAPBASE_BYTE) + odd + (port & 0xfff);
	} else if (size == 2)	/* word access */
		addr = (addr | CFC_IO_MAPBASE_WORD) + (port & 0xfff);

	return addr;
}
#endif	/* CONFIG_PLAT_USRV */

void pcc_ioread_byte(int sock, unsigned long port, void *buf, size_t size,
	size_t nmemb, int flag)
{
	u_long addr;
	unsigned char *bp = (unsigned char *)buf;
	unsigned long flags;

	debug(3, "m32r_cfc: pcc_ioread_byte: sock=%d, port=%#lx, buf=%p, "
		 "size=%u, nmemb=%d, flag=%d\n",
		  sock, port, buf, size, nmemb, flag);

	addr = pcc_port2addr(port, 1);
	if (!addr) {
		printk("m32r_cfc:ioread_byte null port :%#lx\n",port);
		return;
	}
	debug(3, "m32r_cfc: pcc_ioread_byte: addr=%#lx\n", addr);

	spin_lock_irqsave(&pcc_lock, flags);
	/* read Byte */
	while (nmemb--)
		*bp++ = readb(addr);
	spin_unlock_irqrestore(&pcc_lock, flags);
}

void pcc_ioread_word(int sock, unsigned long port, void *buf, size_t size,
	size_t nmemb, int flag)
{
	u_long addr;
	unsigned short *bp = (unsigned short *)buf;
	unsigned long flags;

	debug(3, "m32r_cfc: pcc_ioread_word: sock=%d, port=%#lx, "
		 "buf=%p, size=%u, nmemb=%d, flag=%d\n",
		 sock, port, buf, size, nmemb, flag);

	if (size != 2)
		printk("m32r_cfc: ioread_word :illigal size %u : %#lx\n", size,
			port);
	if (size == 9)
		printk("m32r_cfc: ioread_word :insw \n");

	addr = pcc_port2addr(port, 2);
	if (!addr) {
		printk("m32r_cfc:ioread_word null port :%#lx\n",port);
		return;
	}
	debug(3, "m32r_cfc: pcc_ioread_word: addr=%#lx\n", addr);

	spin_lock_irqsave(&pcc_lock, flags);
	/* read Word */
	while (nmemb--)
		*bp++ = readw(addr);
	spin_unlock_irqrestore(&pcc_lock, flags);
}

void pcc_iowrite_byte(int sock, unsigned long port, void *buf, size_t size,
	size_t nmemb, int flag)
{
	u_long addr;
	unsigned char *bp = (unsigned char *)buf;
	unsigned long flags;

	debug(3, "m32r_cfc: pcc_iowrite_byte: sock=%d, port=%#lx, "
		 "buf=%p, size=%u, nmemb=%d, flag=%d\n",
		 sock, port, buf, size, nmemb, flag);

	/* write Byte */
	addr = pcc_port2addr(port, 1);
	if (!addr) {
		printk("m32r_cfc:iowrite_byte null port:%#lx\n",port);
		return;
	}
	debug(3, "m32r_cfc: pcc_iowrite_byte: addr=%#lx\n", addr);

	spin_lock_irqsave(&pcc_lock, flags);
	while (nmemb--)
		writeb(*bp++, addr);
	spin_unlock_irqrestore(&pcc_lock, flags);
}

void pcc_iowrite_word(int sock, unsigned long port, void *buf, size_t size,
	size_t nmemb, int flag)
{
	u_long addr;
	unsigned short *bp = (unsigned short *)buf;
	unsigned long flags;

	debug(3, "m32r_cfc: pcc_iowrite_word: sock=%d, port=%#lx, "
		 "buf=%p, size=%u, nmemb=%d, flag=%d\n",
		 sock, port, buf, size, nmemb, flag);

	if(size != 2)
		printk("m32r_cfc: iowrite_word :illigal size %u : %#lx\n",
			size, port);
	if(size == 9)
		printk("m32r_cfc: iowrite_word :outsw \n");

	addr = pcc_port2addr(port, 2);
	if (!addr) {
		printk("m32r_cfc:iowrite_word null addr :%#lx\n",port);
		return;
	}
#if 1
	if (addr & 1) {
		printk("m32r_cfc:iowrite_word port addr (%#lx):%#lx\n", port,
			addr);
		return;
	}
#endif
	debug(3, "m32r_cfc: pcc_iowrite_word: addr=%#lx\n", addr);

	spin_lock_irqsave(&pcc_lock, flags);
	while (nmemb--)
		writew(*bp++, addr);
	spin_unlock_irqrestore(&pcc_lock, flags);
}

/*====================================================================*/

#define IS_REGISTERED		0x2000
#define IS_ALIVE		0x8000

typedef struct pcc_t {
	char			*name;
	u_short			flags;
} pcc_t;

static pcc_t pcc[] = {
#if !defined(CONFIG_PLAT_USRV)
	{ "m32r_cfc", 0 }, { "", 0 },
#else	/* CONFIG_PLAT_USRV */
	{ "m32r_cfc", 0 }, { "m32r_cfc", 0 }, { "m32r_cfc", 0 },
	{ "m32r_cfc", 0 }, { "m32r_cfc", 0 }, { "", 0 },
#endif	/* CONFIG_PLAT_USRV */
};

static irqreturn_t pcc_interrupt(int, void *);

/*====================================================================*/

static struct timer_list poll_timer;

static unsigned int pcc_get(u_short sock, unsigned int reg)
{
	unsigned int val = inw(reg);
	debug(3, "m32r_cfc: pcc_get: reg(0x%08x)=0x%04x\n", reg, val);
	return val;
}


static void pcc_set(u_short sock, unsigned int reg, unsigned int data)
{
	outw(data, reg);
	debug(3, "m32r_cfc: pcc_set: reg(0x%08x)=0x%04x\n", reg, data);
}

/*======================================================================

	See if a card is present, powered up, in IO mode, and already
	bound to a (non PC Card) Linux driver.  We leave these alone.

	We make an exception for cards that seem to be serial devices.

======================================================================*/

static int __init is_alive(u_short sock)
{
	unsigned int stat;

	debug(3, "m32r_cfc: is_alive:\n");

	printk("CF: ");
	stat = pcc_get(sock, (unsigned int)PLD_CFSTS);
	if (!stat)
		printk("No ");
	printk("Card is detected at socket %d : stat = 0x%08x\n", sock, stat);
	debug(3, "m32r_cfc: is_alive: sock stat is 0x%04x\n", stat);

	return 0;
}

static void add_pcc_socket(ulong base, int irq, ulong mapaddr,
			   unsigned int ioaddr)
{
	pcc_socket_t *t = &socket[pcc_sockets];

	debug(3, "m32r_cfc: add_pcc_socket: base=%#lx, irq=%d, "
		 "mapaddr=%#lx, ioaddr=%08x\n",
		 base, irq, mapaddr, ioaddr);

	/* add sockets */
	t->ioaddr = ioaddr;
	t->mapaddr = mapaddr;
#if !defined(CONFIG_PLAT_USRV)
	t->base = 0;
	t->flags = 0;
	t->cs_irq1 = irq;		// insert irq
	t->cs_irq2 = irq + 1;		// eject irq
#else	/* CONFIG_PLAT_USRV */
	t->base = base;
	t->flags = 0;
	t->cs_irq1 = 0;			// insert irq
	t->cs_irq2 = 0;			// eject irq
#endif	/* CONFIG_PLAT_USRV */

	if (is_alive(pcc_sockets))
		t->flags |= IS_ALIVE;

	/* add pcc */
#if !defined(CONFIG_PLAT_USRV)
	request_region((unsigned int)PLD_CFRSTCR, 0x20, "m32r_cfc");
#else	/* CONFIG_PLAT_USRV */
	{
		unsigned int reg_base;

		reg_base = (unsigned int)PLD_CFRSTCR;
		reg_base |= pcc_sockets << 8;
		request_region(reg_base, 0x20, "m32r_cfc");
	}
#endif	/* CONFIG_PLAT_USRV */
	printk(KERN_INFO "  %s ", pcc[pcc_sockets].name);
	printk("pcc at 0x%08lx\n", t->base);

	/* Update socket interrupt information, capabilities */
	t->socket.features |= (SS_CAP_PCCARD | SS_CAP_STATIC_MAP);
	t->socket.map_size = M32R_PCC_MAPSIZE;
	t->socket.io_offset = ioaddr;	/* use for io access offset */
	t->socket.irq_mask = 0;
#if !defined(CONFIG_PLAT_USRV)
	t->socket.pci_irq = PLD_IRQ_CFIREQ ;	/* card interrupt */
#else	/* CONFIG_PLAT_USRV */
	t->socket.pci_irq = PLD_IRQ_CF0 + pcc_sockets;
#endif	/* CONFIG_PLAT_USRV */

#ifndef CONFIG_PLAT_USRV
	/* insert interrupt */
	request_irq(irq, pcc_interrupt, 0, "m32r_cfc", pcc_interrupt);
#ifndef CONFIG_PLAT_MAPPI3
	/* eject interrupt */
	request_irq(irq+1, pcc_interrupt, 0, "m32r_cfc", pcc_interrupt);
#endif
	debug(3, "m32r_cfc: enable CFMSK, RDYSEL\n");
	pcc_set(pcc_sockets, (unsigned int)PLD_CFIMASK, 0x01);
#endif	/* CONFIG_PLAT_USRV */
#if defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_USRV) || defined(CONFIG_PLAT_OPSPUT)
	pcc_set(pcc_sockets, (unsigned int)PLD_CFCR1, 0x0200);
#endif
	pcc_sockets++;

	return;
}


/*====================================================================*/

static irqreturn_t pcc_interrupt(int irq, void *dev)
{
	int i;
	u_int events = 0;
	int handled = 0;

	debug(3, "m32r_cfc: pcc_interrupt: irq=%d, dev=%p\n", irq, dev);
	for (i = 0; i < pcc_sockets; i++) {
		if (socket[i].cs_irq1 != irq && socket[i].cs_irq2 != irq)
			continue;

		handled = 1;
		debug(3, "m32r_cfc: pcc_interrupt: socket %d irq 0x%02x ",
			i, irq);
		events |= SS_DETECT;	/* insert or eject */
		if (events)
			pcmcia_parse_events(&socket[i].socket, events);
	}
	debug(3, "m32r_cfc: pcc_interrupt: done\n");

	return IRQ_RETVAL(handled);
} /* pcc_interrupt */

static void pcc_interrupt_wrapper(u_long data)
{
	debug(3, "m32r_cfc: pcc_interrupt_wrapper:\n");
	pcc_interrupt(0, NULL);
	init_timer(&poll_timer);
	poll_timer.expires = jiffies + poll_interval;
	add_timer(&poll_timer);
}

/*====================================================================*/

static int _pcc_get_status(u_short sock, u_int *value)
{
	u_int status;

	debug(3, "m32r_cfc: _pcc_get_status:\n");
	status = pcc_get(sock, (unsigned int)PLD_CFSTS);
	*value = (status) ? SS_DETECT : 0;
 	debug(3, "m32r_cfc: _pcc_get_status: status=0x%08x\n", status);

#if defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_USRV) || defined(CONFIG_PLAT_OPSPUT)
	if ( status ) {
		/* enable CF power */
		status = inw((unsigned int)PLD_CPCR);
		if (!(status & PLD_CPCR_CF)) {
			debug(3, "m32r_cfc: _pcc_get_status: "
				 "power on (CPCR=0x%08x)\n", status);
			status |= PLD_CPCR_CF;
			outw(status, (unsigned int)PLD_CPCR);
			udelay(100);
		}
		*value |= SS_POWERON;

		pcc_set(sock, (unsigned int)PLD_CFBUFCR,0);/* enable buffer */
		udelay(100);

		*value |= SS_READY; 		/* always ready */
		*value |= SS_3VCARD;
	} else {
		/* disable CF power */
		status = inw((unsigned int)PLD_CPCR);
		status &= ~PLD_CPCR_CF;
		outw(status, (unsigned int)PLD_CPCR);
		udelay(100);
		debug(3, "m32r_cfc: _pcc_get_status: "
			 "power off (CPCR=0x%08x)\n", status);
	}
#elif defined(CONFIG_PLAT_MAPPI2) || defined(CONFIG_PLAT_MAPPI3)
	if ( status ) {
		status = pcc_get(sock, (unsigned int)PLD_CPCR);
		if (status == 0) { /* power off */
			pcc_set(sock, (unsigned int)PLD_CPCR, 1);
			pcc_set(sock, (unsigned int)PLD_CFBUFCR,0); /* force buffer off for ZA-36 */
			udelay(50);
		}
		*value |= SS_POWERON;

		pcc_set(sock, (unsigned int)PLD_CFBUFCR,0);
		udelay(50);
		pcc_set(sock, (unsigned int)PLD_CFRSTCR, 0x0101);
		udelay(25); /* for IDE reset */
		pcc_set(sock, (unsigned int)PLD_CFRSTCR, 0x0100);
		mdelay(2);  /* for IDE reset */

		*value |= SS_READY;
		*value |= SS_3VCARD;
	} else {
		/* disable CF power */
	        pcc_set(sock, (unsigned int)PLD_CPCR, 0);
		udelay(100);
		debug(3, "m32r_cfc: _pcc_get_status: "
			 "power off (CPCR=0x%08x)\n", status);
	}
#else
#error no platform configuration
#endif
	debug(3, "m32r_cfc: _pcc_get_status: GetStatus(%d) = %#4.4x\n",
		 sock, *value);
	return 0;
} /* _get_status */

/*====================================================================*/

static int _pcc_set_socket(u_short sock, socket_state_t *state)
{
	debug(3, "m32r_cfc: SetSocket(%d, flags %#3.3x, Vcc %d, Vpp %d, "
		  "io_irq %d, csc_mask %#2.2x)\n", sock, state->flags,
		  state->Vcc, state->Vpp, state->io_irq, state->csc_mask);

#if defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_USRV) || defined(CONFIG_PLAT_OPSPUT) || defined(CONFIG_PLAT_MAPPI2) || defined(CONFIG_PLAT_MAPPI3)
	if (state->Vcc) {
		if ((state->Vcc != 50) && (state->Vcc != 33))
			return -EINVAL;
		/* accept 5V and 3.3V */
	}
#endif
	if (state->flags & SS_RESET) {
		debug(3, ":RESET\n");
		pcc_set(sock,(unsigned int)PLD_CFRSTCR,0x101);
	}else{
		pcc_set(sock,(unsigned int)PLD_CFRSTCR,0x100);
	}
	if (state->flags & SS_OUTPUT_ENA){
		debug(3, ":OUTPUT_ENA\n");
		/* bit clear */
		pcc_set(sock,(unsigned int)PLD_CFBUFCR,0);
	} else {
		pcc_set(sock,(unsigned int)PLD_CFBUFCR,1);
	}

#ifdef CONFIG_PCMCIA_DEBUG
	if(state->flags & SS_IOCARD){
		debug(3, ":IOCARD");
	}
	if (state->flags & SS_PWR_AUTO) {
		debug(3, ":PWR_AUTO");
	}
	if (state->csc_mask & SS_DETECT)
		debug(3, ":csc-SS_DETECT");
	if (state->flags & SS_IOCARD) {
		if (state->csc_mask & SS_STSCHG)
			debug(3, ":STSCHG");
	} else {
		if (state->csc_mask & SS_BATDEAD)
			debug(3, ":BATDEAD");
		if (state->csc_mask & SS_BATWARN)
			debug(3, ":BATWARN");
		if (state->csc_mask & SS_READY)
			debug(3, ":READY");
	}
	debug(3, "\n");
#endif
	return 0;
} /* _set_socket */

/*====================================================================*/

static int _pcc_set_io_map(u_short sock, struct pccard_io_map *io)
{
	u_char map;

	debug(3, "m32r_cfc: SetIOMap(%d, %d, %#2.2x, %d ns, "
		  "%#lx-%#lx)\n", sock, io->map, io->flags,
		  io->speed, io->start, io->stop);
	map = io->map;

	return 0;
} /* _set_io_map */

/*====================================================================*/

static int _pcc_set_mem_map(u_short sock, struct pccard_mem_map *mem)
{

	u_char map = mem->map;
	u_long addr;
	pcc_socket_t *t = &socket[sock];

	debug(3, "m32r_cfc: SetMemMap(%d, %d, %#2.2x, %d ns, "
		 "%#lx, %#x)\n", sock, map, mem->flags,
		 mem->speed, mem->static_start, mem->card_start);

	/*
	 * sanity check
	 */
	if ((map > MAX_WIN) || (mem->card_start > 0x3ffffff)){
		return -EINVAL;
	}

	/*
	 * de-activate
	 */
	if ((mem->flags & MAP_ACTIVE) == 0) {
		t->current_space = as_none;
		return 0;
	}

	/*
	 * Set mode
	 */
	if (mem->flags & MAP_ATTRIB) {
		t->current_space = as_attr;
	} else {
		t->current_space = as_comm;
	}

	/*
	 * Set address
	 */
	addr = t->mapaddr + (mem->card_start & M32R_PCC_MAPMASK);
	mem->static_start = addr + mem->card_start;

	return 0;

} /* _set_mem_map */

#if 0 /* driver model ordering issue */
/*======================================================================

	Routines for accessing socket information and register dumps via
	/proc/bus/pccard/...

======================================================================*/

static ssize_t show_info(struct class_device *class_dev, char *buf)
{
	pcc_socket_t *s = container_of(class_dev, struct pcc_socket,
		socket.dev);

	return sprintf(buf, "type:     %s\nbase addr:    0x%08lx\n",
		pcc[s->type].name, s->base);
}

static ssize_t show_exca(struct class_device *class_dev, char *buf)
{
	/* FIXME */

	return 0;
}

static CLASS_DEVICE_ATTR(info, S_IRUGO, show_info, NULL);
static CLASS_DEVICE_ATTR(exca, S_IRUGO, show_exca, NULL);
#endif

/*====================================================================*/

/* this is horribly ugly... proper locking needs to be done here at
 * some time... */
#define LOCKED(x) do {					\
	int retval;					\
	unsigned long flags;				\
	spin_lock_irqsave(&pcc_lock, flags);		\
	retval = x;					\
	spin_unlock_irqrestore(&pcc_lock, flags);	\
	return retval;					\
} while (0)


static int pcc_get_status(struct pcmcia_socket *s, u_int *value)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE) {
		debug(3, "m32r_cfc: pcc_get_status: sock(%d) -EINVAL\n", sock);
		*value = 0;
		return -EINVAL;
	}
	debug(3, "m32r_cfc: pcc_get_status: sock(%d)\n", sock);
	LOCKED(_pcc_get_status(sock, value));
}

static int pcc_set_socket(struct pcmcia_socket *s, socket_state_t *state)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE) {
		debug(3, "m32r_cfc: pcc_set_socket: sock(%d) -EINVAL\n", sock);
		return -EINVAL;
	}
	debug(3, "m32r_cfc: pcc_set_socket: sock(%d)\n", sock);
	LOCKED(_pcc_set_socket(sock, state));
}

static int pcc_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE) {
		debug(3, "m32r_cfc: pcc_set_io_map: sock(%d) -EINVAL\n", sock);
		return -EINVAL;
	}
	debug(3, "m32r_cfc: pcc_set_io_map: sock(%d)\n", sock);
	LOCKED(_pcc_set_io_map(sock, io));
}

static int pcc_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *mem)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE) {
		debug(3, "m32r_cfc: pcc_set_mem_map: sock(%d) -EINVAL\n", sock);
		return -EINVAL;
	}
	debug(3, "m32r_cfc: pcc_set_mem_map: sock(%d)\n", sock);
	LOCKED(_pcc_set_mem_map(sock, mem));
}

static int pcc_init(struct pcmcia_socket *s)
{
	debug(3, "m32r_cfc: pcc_init()\n");
	return 0;
}

static struct pccard_operations pcc_operations = {
	.init			= pcc_init,
	.get_status		= pcc_get_status,
	.set_socket		= pcc_set_socket,
	.set_io_map		= pcc_set_io_map,
	.set_mem_map		= pcc_set_mem_map,
};

static int cfc_drv_pcmcia_suspend(struct platform_device *dev,
				     pm_message_t state)
{
	return pcmcia_socket_dev_suspend(&dev->dev, state);
}

static int cfc_drv_pcmcia_resume(struct platform_device *dev)
{
	return pcmcia_socket_dev_resume(&dev->dev);
}
/*====================================================================*/

static struct platform_driver pcc_driver = {
	.driver = {
		.name		= "cfc",
		.owner		= THIS_MODULE,
	},
	.suspend 	= cfc_drv_pcmcia_suspend,
	.resume 	= cfc_drv_pcmcia_resume,
};

static struct platform_device pcc_device = {
	.name = "cfc",
	.id = 0,
};

/*====================================================================*/

static int __init init_m32r_pcc(void)
{
	int i, ret;

	ret = platform_driver_register(&pcc_driver);
	if (ret)
		return ret;

	ret = platform_device_register(&pcc_device);
	if (ret){
		platform_driver_unregister(&pcc_driver);
		return ret;
	}

#if defined(CONFIG_PLAT_MAPPI2) || defined(CONFIG_PLAT_MAPPI3)
	pcc_set(0, (unsigned int)PLD_CFCR0, 0x0f0f);
	pcc_set(0, (unsigned int)PLD_CFCR1, 0x0200);
#endif

	pcc_sockets = 0;

#if !defined(CONFIG_PLAT_USRV)
	add_pcc_socket(M32R_PCC0_BASE, PLD_IRQ_CFC_INSERT, CFC_ATTR_MAPBASE,
		       CFC_IOPORT_BASE);
#else	/* CONFIG_PLAT_USRV */
	{
		ulong base, mapaddr;
		unsigned int ioaddr;

		for (i = 0 ; i < M32R_MAX_PCC ; i++) {
			base = (ulong)PLD_CFRSTCR;
			base = base | (i << 8);
			ioaddr = (i + 1) << 12;
			mapaddr = CFC_ATTR_MAPBASE | (i << 20);
			add_pcc_socket(base, 0, mapaddr, ioaddr);
		}
	}
#endif	/* CONFIG_PLAT_USRV */

	if (pcc_sockets == 0) {
		printk("socket is not found.\n");
		platform_device_unregister(&pcc_device);
		platform_driver_unregister(&pcc_driver);
		return -ENODEV;
	}

	/* Set up interrupt handler(s) */

	for (i = 0 ; i < pcc_sockets ; i++) {
		socket[i].socket.dev.parent = &pcc_device.dev;
		socket[i].socket.ops = &pcc_operations;
		socket[i].socket.resource_ops = &pccard_nonstatic_ops;
		socket[i].socket.owner = THIS_MODULE;
		socket[i].number = i;
		ret = pcmcia_register_socket(&socket[i].socket);
		if (!ret)
			socket[i].flags |= IS_REGISTERED;

#if 0	/* driver model ordering issue */
		class_device_create_file(&socket[i].socket.dev,
					 &class_device_attr_info);
		class_device_create_file(&socket[i].socket.dev,
					 &class_device_attr_exca);
#endif
	}

	/* Finally, schedule a polling interrupt */
	if (poll_interval != 0) {
		poll_timer.function = pcc_interrupt_wrapper;
		poll_timer.data = 0;
		init_timer(&poll_timer);
		poll_timer.expires = jiffies + poll_interval;
		add_timer(&poll_timer);
	}

	return 0;
} /* init_m32r_pcc */

static void __exit exit_m32r_pcc(void)
{
	int i;

	for (i = 0; i < pcc_sockets; i++)
		if (socket[i].flags & IS_REGISTERED)
			pcmcia_unregister_socket(&socket[i].socket);

	platform_device_unregister(&pcc_device);
	if (poll_interval != 0)
		del_timer_sync(&poll_timer);

	platform_driver_unregister(&pcc_driver);
} /* exit_m32r_pcc */

module_init(init_m32r_pcc);
module_exit(exit_m32r_pcc);
MODULE_LICENSE("Dual MPL/GPL");
/*====================================================================*/
