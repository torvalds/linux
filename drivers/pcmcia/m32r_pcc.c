/*
 *  drivers/pcmcia/m32r_pcc.c
 *
 *  Device driver for the PCMCIA functionality of M32R.
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
#include <asm/addrspace.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>

/* XXX: should be moved into asm/irq.h */
#define PCC0_IRQ 24
#define PCC1_IRQ 25

#include "m32r_pcc.h"

#define CHAOS_PCC_DEBUG
#ifdef CHAOS_PCC_DEBUG
	static volatile u_short dummy_readbuf;
#endif

#define PCC_DEBUG_DBEX

#ifdef DEBUG
static int m32r_pcc_debug;
module_param(m32r_pcc_debug, int, 0644);
#define debug(lvl, fmt, arg...) do {				\
	if (m32r_pcc_debug > (lvl))				\
		printk(KERN_DEBUG "m32r_pcc: " fmt , ## arg);	\
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
 	kio_addr_t		ioaddr;
	u_long			mapaddr;
	u_long			base;	/* PCC register base */
	u_char			cs_irq, intr;
	pccard_io_map		io_map[MAX_IO_WIN];
	pccard_mem_map		mem_map[MAX_WIN];
	u_char			io_win;
	u_char			mem_win;
	pcc_as_t		current_space;
	u_char			last_iodbex;
#ifdef CHAOS_PCC_DEBUG
	u_char			last_iosize;
#endif
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

void pcc_iorw(int sock, unsigned long port, void *buf, size_t size, size_t nmemb, int wr, int flag)
{
	u_long addr;
	u_long flags;
	int need_ex;
#ifdef PCC_DEBUG_DBEX
	int _dbex;
#endif
	pcc_socket_t *t = &socket[sock];
#ifdef CHAOS_PCC_DEBUG
	int map_changed = 0;
#endif

	/* Need lock ? */
	spin_lock_irqsave(&pcc_lock, flags);

	/*
	 * Check if need dbex
	 */
	need_ex = (size > 1 && flag == 0) ? PCMOD_DBEX : 0;
#ifdef PCC_DEBUG_DBEX
	_dbex = need_ex;
	need_ex = 0;
#endif

	/*
	 * calculate access address
	 */
	addr = t->mapaddr + port - t->ioaddr + KSEG1; /* XXX */

	/*
	 * Check current mapping
	 */
	if (t->current_space != as_io || t->last_iodbex != need_ex) {

		u_long cbsz;

		/*
		 * Disable first
		 */
		pcc_set(sock, PCCR, 0);

		/*
		 * Set mode and io address
		 */
		cbsz = (t->flags & MAP_16BIT) ? 0 : PCMOD_CBSZ;
		pcc_set(sock, PCMOD, PCMOD_AS_IO | cbsz | need_ex);
		pcc_set(sock, PCADR, addr & 0x1ff00000);

		/*
		 * Enable and read it
		 */
		pcc_set(sock, PCCR, 1);

#ifdef CHAOS_PCC_DEBUG
#if 0
		map_changed = (t->current_space == as_attr && size == 2); /* XXX */
#else
		map_changed = 1;
#endif
#endif
		t->current_space = as_io;
	}

	/*
	 * access to IO space
	 */
	if (size == 1) {
		/* Byte */
		unsigned char *bp = (unsigned char *)buf;

#ifdef CHAOS_DEBUG
		if (map_changed) {
			dummy_readbuf = readb(addr);
		}
#endif
		if (wr) {
			/* write Byte */
			while (nmemb--) {
				writeb(*bp++, addr);
			}
		} else {
			/* read Byte */
			while (nmemb--) {
	    		*bp++ = readb(addr);
			}
		}
	} else {
		/* Word */
		unsigned short *bp = (unsigned short *)buf;

#ifdef CHAOS_PCC_DEBUG
		if (map_changed) {
			dummy_readbuf = readw(addr);
		}
#endif
		if (wr) {
			/* write Word */
			while (nmemb--) {
#ifdef PCC_DEBUG_DBEX
				if (_dbex) {
					unsigned char *cp = (unsigned char *)bp;
					unsigned short tmp;
					tmp = cp[1] << 8 | cp[0];
					writew(tmp, addr);
					bp++;
				} else
#endif
				writew(*bp++, addr);
	    	}
	    } else {
	    	/* read Word */
	    	while (nmemb--) {
#ifdef  PCC_DEBUG_DBEX
				if (_dbex) {
					unsigned char *cp = (unsigned char *)bp;
					unsigned short tmp;
					tmp = readw(addr);
					cp[0] = tmp & 0xff;
					cp[1] = (tmp >> 8) & 0xff;
					bp++;
				} else
#endif
				*bp++ = readw(addr);
	    	}
	    }
	}

#if 1
	/* addr is no longer used */
	if ((addr = pcc_get(sock, PCIRC)) & PCIRC_BWERR) {
	  printk("m32r_pcc: BWERR detected : port 0x%04lx : iosize %dbit\n",
			 port, size * 8);
	  pcc_set(sock, PCIRC, addr);
	}
#endif
	/*
	 * save state
	 */
	t->last_iosize = size;
	t->last_iodbex = need_ex;

	/* Need lock ? */

	spin_unlock_irqrestore(&pcc_lock,flags);

	return;
}

void pcc_ioread(int sock, unsigned long port, void *buf, size_t size, size_t nmemb, int flag) {
	pcc_iorw(sock, port, buf, size, nmemb, 0, flag);
}

void pcc_iowrite(int sock, unsigned long port, void *buf, size_t size, size_t nmemb, int flag) {
    pcc_iorw(sock, port, buf, size, nmemb, 1, flag);
}

/*====================================================================*/

#define IS_REGISTERED		0x2000
#define IS_ALIVE		0x8000

typedef struct pcc_t {
	char			*name;
	u_short			flags;
} pcc_t;

static pcc_t pcc[] = {
	{ "xnux2", 0 }, { "xnux2", 0 },
};

static irqreturn_t pcc_interrupt(int, void *);

/*====================================================================*/

static struct timer_list poll_timer;

static unsigned int pcc_get(u_short sock, unsigned int reg)
{
	return inl(socket[sock].base + reg);
}


static void pcc_set(u_short sock, unsigned int reg, unsigned int data)
{
  	outl(data, socket[sock].base + reg);
}

/*======================================================================

	See if a card is present, powered up, in IO mode, and already
	bound to a (non PC Card) Linux driver.  We leave these alone.

	We make an exception for cards that seem to be serial devices.

======================================================================*/

static int __init is_alive(u_short sock)
{
	unsigned int stat;
	unsigned int f;

	stat = pcc_get(sock, PCIRC);
	f = (stat & (PCIRC_CDIN1 | PCIRC_CDIN2)) >> 16;
	if(!f){
		printk("m32r_pcc: No Card is detected at socket %d : stat = 0x%08x\n",stat,sock);
		return 0;
	}
	if(f!=3)
		printk("m32r_pcc: Insertion fail (%.8x) at socket %d\n",stat,sock);
	else
		printk("m32r_pcc: Card is Inserted at socket %d(%.8x)\n",sock,stat);
	return 0;
}

static void add_pcc_socket(ulong base, int irq, ulong mapaddr, kio_addr_t ioaddr)
{
  	pcc_socket_t *t = &socket[pcc_sockets];

	/* add sockets */
	t->ioaddr = ioaddr;
	t->mapaddr = mapaddr;
	t->base = base;
#ifdef CHAOS_PCC_DEBUG
	t->flags = MAP_16BIT;
#else
	t->flags = 0;
#endif
	if (is_alive(pcc_sockets))
		t->flags |= IS_ALIVE;

	/* add pcc */
	if (t->base > 0) {
		request_region(t->base, 0x20, "m32r-pcc");
	}

	printk(KERN_INFO "  %s ", pcc[pcc_sockets].name);
	printk("pcc at 0x%08lx\n", t->base);

	/* Update socket interrupt information, capabilities */
	t->socket.features |= (SS_CAP_PCCARD | SS_CAP_STATIC_MAP);
	t->socket.map_size = M32R_PCC_MAPSIZE;
	t->socket.io_offset = ioaddr;	/* use for io access offset */
	t->socket.irq_mask = 0;
	t->socket.pci_irq = 2 + pcc_sockets; /* XXX */

	request_irq(irq, pcc_interrupt, 0, "m32r-pcc", pcc_interrupt);

	pcc_sockets++;

	return;
}


/*====================================================================*/

static irqreturn_t pcc_interrupt(int irq, void *dev)
{
	int i, j, irc;
	u_int events, active;
	int handled = 0;

	debug(4, "m32r: pcc_interrupt(%d)\n", irq);

	for (j = 0; j < 20; j++) {
		active = 0;
		for (i = 0; i < pcc_sockets; i++) {
			if ((socket[i].cs_irq != irq) &&
				(socket[i].socket.pci_irq != irq))
				continue;
			handled = 1;
			irc = pcc_get(i, PCIRC);
			irc >>=16;
			debug(2, "m32r-pcc:interrput: socket %d pcirc 0x%02x ", i, irc);
			if (!irc)
				continue;

			events = (irc) ? SS_DETECT : 0;
			events |= (pcc_get(i,PCCR) & PCCR_PCEN) ? SS_READY : 0;
			debug(2, " event 0x%02x\n", events);

			if (events)
				pcmcia_parse_events(&socket[i].socket, events);

			active |= events;
			active = 0;
		}
		if (!active) break;
	}
	if (j == 20)
		printk(KERN_NOTICE "m32r-pcc: infinite loop in interrupt handler\n");

	debug(4, "m32r-pcc: interrupt done\n");

	return IRQ_RETVAL(handled);
} /* pcc_interrupt */

static void pcc_interrupt_wrapper(u_long data)
{
	pcc_interrupt(0, NULL);
	init_timer(&poll_timer);
	poll_timer.expires = jiffies + poll_interval;
	add_timer(&poll_timer);
}

/*====================================================================*/

static int _pcc_get_status(u_short sock, u_int *value)
{
	u_int status;

	status = pcc_get(sock,PCIRC);
	*value = ((status & PCIRC_CDIN1) && (status & PCIRC_CDIN2))
		? SS_DETECT : 0;

	status = pcc_get(sock,PCCR);

#if 0
	*value |= (status & PCCR_PCEN) ? SS_READY : 0;
#else
	*value |= SS_READY; /* XXX: always */
#endif

	status = pcc_get(sock,PCCSIGCR);
	*value |= (status & PCCSIGCR_VEN) ? SS_POWERON : 0;

	debug(3, "m32r-pcc: GetStatus(%d) = %#4.4x\n", sock, *value);
	return 0;
} /* _get_status */

/*====================================================================*/

static int _pcc_set_socket(u_short sock, socket_state_t *state)
{
	u_long reg = 0;

	debug(3, "m32r-pcc: SetSocket(%d, flags %#3.3x, Vcc %d, Vpp %d, "
		  "io_irq %d, csc_mask %#2.2x)", sock, state->flags,
		  state->Vcc, state->Vpp, state->io_irq, state->csc_mask);

	if (state->Vcc) {
		/*
		 * 5V only
		 */
		if (state->Vcc == 50) {
			reg |= PCCSIGCR_VEN;
		} else {
			return -EINVAL;
		}
	}

	if (state->flags & SS_RESET) {
		debug(3, ":RESET\n");
		reg |= PCCSIGCR_CRST;
	}
	if (state->flags & SS_OUTPUT_ENA){
		debug(3, ":OUTPUT_ENA\n");
		/* bit clear */
	} else {
		reg |= PCCSIGCR_SEN;
	}

	pcc_set(sock,PCCSIGCR,reg);

#ifdef DEBUG
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

	debug(3, "m32r-pcc: SetIOMap(%d, %d, %#2.2x, %d ns, "
		  "%#lx-%#lx)\n", sock, io->map, io->flags,
		  io->speed, io->start, io->stop);
	map = io->map;

	return 0;
} /* _set_io_map */

/*====================================================================*/

static int _pcc_set_mem_map(u_short sock, struct pccard_mem_map *mem)
{

	u_char map = mem->map;
	u_long mode;
	u_long addr;
	pcc_socket_t *t = &socket[sock];
#ifdef CHAOS_PCC_DEBUG
#if 0
	pcc_as_t last = t->current_space;
#endif
#endif

	debug(3, "m32r-pcc: SetMemMap(%d, %d, %#2.2x, %d ns, "
		 "%#lx,  %#x)\n", sock, map, mem->flags,
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
	 * Disable first
	 */
	pcc_set(sock, PCCR, 0);

	/*
	 * Set mode
	 */
	if (mem->flags & MAP_ATTRIB) {
		mode = PCMOD_AS_ATTRIB | PCMOD_CBSZ;
		t->current_space = as_attr;
	} else {
		mode = 0; /* common memory */
		t->current_space = as_comm;
	}
	pcc_set(sock, PCMOD, mode);

	/*
	 * Set address
	 */
	addr = t->mapaddr + (mem->card_start & M32R_PCC_MAPMASK);
	pcc_set(sock, PCADR, addr);

	mem->static_start = addr + mem->card_start;

	/*
	 * Enable again
	 */
	pcc_set(sock, PCCR, 1);

#ifdef CHAOS_PCC_DEBUG
#if 0
	if (last != as_attr) {
#else
	if (1) {
#endif
		dummy_readbuf = *(u_char *)(addr + KSEG1);
	}
#endif

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
		*value = 0;
		return -EINVAL;
	}
	LOCKED(_pcc_get_status(sock, value));
}

static int pcc_set_socket(struct pcmcia_socket *s, socket_state_t *state)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;

	LOCKED(_pcc_set_socket(sock, state));
}

static int pcc_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;
	LOCKED(_pcc_set_io_map(sock, io));
}

static int pcc_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *mem)
{
	unsigned int sock = container_of(s, struct pcc_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;
	LOCKED(_pcc_set_mem_map(sock, mem));
}

static int pcc_init(struct pcmcia_socket *s)
{
	debug(4, "m32r-pcc: init call\n");
	return 0;
}

static struct pccard_operations pcc_operations = {
	.init			= pcc_init,
	.get_status		= pcc_get_status,
	.set_socket		= pcc_set_socket,
	.set_io_map		= pcc_set_io_map,
	.set_mem_map		= pcc_set_mem_map,
};

/*====================================================================*/

static struct device_driver pcc_driver = {
	.name = "pcc",
	.bus = &platform_bus_type,
	.suspend = pcmcia_socket_dev_suspend,
	.resume = pcmcia_socket_dev_resume,
};

static struct platform_device pcc_device = {
	.name = "pcc",
	.id = 0,
};

/*====================================================================*/

static int __init init_m32r_pcc(void)
{
	int i, ret;

	ret = driver_register(&pcc_driver);
	if (ret)
		return ret;

	ret = platform_device_register(&pcc_device);
	if (ret){
		driver_unregister(&pcc_driver);
		return ret;
	}

	printk(KERN_INFO "m32r PCC probe:\n");

	pcc_sockets = 0;

	add_pcc_socket(M32R_PCC0_BASE, PCC0_IRQ, M32R_PCC0_MAPBASE, 0x1000);

#ifdef CONFIG_M32RPCC_SLOT2
	add_pcc_socket(M32R_PCC1_BASE, PCC1_IRQ, M32R_PCC1_MAPBASE, 0x2000);
#endif

	if (pcc_sockets == 0) {
		printk("socket is not found.\n");
		platform_device_unregister(&pcc_device);
		driver_unregister(&pcc_driver);
		return -ENODEV;
	}

	/* Set up interrupt handler(s) */

	for (i = 0 ; i < pcc_sockets ; i++) {
		socket[i].socket.dev.parent = &pcc_device.dev;
		socket[i].socket.ops = &pcc_operations;
		socket[i].socket.resource_ops = &pccard_static_ops;
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

	driver_unregister(&pcc_driver);
} /* exit_m32r_pcc */

module_init(init_m32r_pcc);
module_exit(exit_m32r_pcc);
MODULE_LICENSE("Dual MPL/GPL");
/*====================================================================*/
