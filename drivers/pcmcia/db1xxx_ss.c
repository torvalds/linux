// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCMCIA socket code for the Alchemy Db1xxx/Pb1xxx boards.
 *
 * Copyright (c) 2009 Manuel Lauss <manuel.lauss@gmail.com>
 *
 */

/* This is a fairly generic PCMCIA socket driver suitable for the
 * following Alchemy Development boards:
 *  Db1000, Db/Pb1500, Db/Pb1100, Db/Pb1550, Db/Pb1200, Db1300
 *
 * The Db1000 is used as a reference:  Per-socket card-, carddetect- and
 *  statuschange IRQs connected to SoC GPIOs, control and status register
 *  bits arranged in per-socket groups in an external PLD.  All boards
 *  listed here use this layout, including bit positions and meanings.
 *  Of course there are exceptions in later boards:
 *
 *	- Pb1100/Pb1500:  single socket only; voltage key bits VS are
 *			  at STATUS[5:4] (instead of STATUS[1:0]).
 *	- Au1200-based:	  additional card-eject irqs, irqs not gpios!
 *	- Db1300:	  Db1200-like, no pwr ctrl, single socket (#1).
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <pcmcia/ss.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

#define MEM_MAP_SIZE	0x400000
#define IO_MAP_SIZE	0x1000

struct db1x_pcmcia_sock {
	struct pcmcia_socket	socket;
	int		nr;		/* socket number */
	void		*virt_io;

	phys_addr_t	phys_io;
	phys_addr_t	phys_attr;
	phys_addr_t	phys_mem;

	/* previous flags for set_socket() */
	unsigned int old_flags;

	/* interrupt sources: linux irq numbers! */
	int	insert_irq;	/* default carddetect irq */
	int	stschg_irq;	/* card-status-change irq */
	int	card_irq;	/* card irq */
	int	eject_irq;	/* db1200/pb1200 have these */
	int	insert_gpio;	/* db1000 carddetect gpio */

#define BOARD_TYPE_DEFAULT	0	/* most boards */
#define BOARD_TYPE_DB1200	1	/* IRQs aren't gpios */
#define BOARD_TYPE_PB1100	2	/* VS bits slightly different */
#define BOARD_TYPE_DB1300	3	/* no power control */
	int	board_type;
};

#define to_db1x_socket(x) container_of(x, struct db1x_pcmcia_sock, socket)

static int db1300_card_inserted(struct db1x_pcmcia_sock *sock)
{
	return bcsr_read(BCSR_SIGSTAT) & (1 << 8);
}

/* DB/PB1200: check CPLD SIGSTATUS register bit 10/12 */
static int db1200_card_inserted(struct db1x_pcmcia_sock *sock)
{
	unsigned short sigstat;

	sigstat = bcsr_read(BCSR_SIGSTAT);
	return sigstat & 1 << (8 + 2 * sock->nr);
}

/* carddetect gpio: low-active */
static int db1000_card_inserted(struct db1x_pcmcia_sock *sock)
{
	return !gpio_get_value(sock->insert_gpio);
}

static int db1x_card_inserted(struct db1x_pcmcia_sock *sock)
{
	switch (sock->board_type) {
	case BOARD_TYPE_DB1200:
		return db1200_card_inserted(sock);
	case BOARD_TYPE_DB1300:
		return db1300_card_inserted(sock);
	default:
		return db1000_card_inserted(sock);
	}
}

/* STSCHG tends to bounce heavily when cards are inserted/ejected.
 * To avoid this, the interrupt is normally disabled and only enabled
 * after reset to a card has been de-asserted.
 */
static inline void set_stschg(struct db1x_pcmcia_sock *sock, int en)
{
	if (sock->stschg_irq != -1) {
		if (en)
			enable_irq(sock->stschg_irq);
		else
			disable_irq(sock->stschg_irq);
	}
}

static irqreturn_t db1000_pcmcia_cdirq(int irq, void *data)
{
	struct db1x_pcmcia_sock *sock = data;

	pcmcia_parse_events(&sock->socket, SS_DETECT);

	return IRQ_HANDLED;
}

static irqreturn_t db1000_pcmcia_stschgirq(int irq, void *data)
{
	struct db1x_pcmcia_sock *sock = data;

	pcmcia_parse_events(&sock->socket, SS_STSCHG);

	return IRQ_HANDLED;
}

/* Db/Pb1200 have separate per-socket insertion and ejection
 * interrupts which stay asserted as long as the card is
 * inserted/missing.  The one which caused us to be called
 * needs to be disabled and the other one enabled.
 */
static irqreturn_t db1200_pcmcia_cdirq(int irq, void *data)
{
	disable_irq_nosync(irq);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t db1200_pcmcia_cdirq_fn(int irq, void *data)
{
	struct db1x_pcmcia_sock *sock = data;

	/* Wait a bit for the signals to stop bouncing. */
	msleep(100);
	if (irq == sock->insert_irq)
		enable_irq(sock->eject_irq);
	else
		enable_irq(sock->insert_irq);

	pcmcia_parse_events(&sock->socket, SS_DETECT);

	return IRQ_HANDLED;
}

static int db1x_pcmcia_setup_irqs(struct db1x_pcmcia_sock *sock)
{
	int ret;

	if (sock->stschg_irq != -1) {
		ret = request_irq(sock->stschg_irq, db1000_pcmcia_stschgirq,
				  0, "pcmcia_stschg", sock);
		if (ret)
			return ret;
	}

	/* Db/Pb1200 have separate per-socket insertion and ejection
	 * interrupts, which should show edge behaviour but don't.
	 * So interrupts are disabled until both insertion and
	 * ejection handler have been registered and the currently
	 * active one disabled.
	 */
	if ((sock->board_type == BOARD_TYPE_DB1200) ||
	    (sock->board_type == BOARD_TYPE_DB1300)) {
		ret = request_threaded_irq(sock->insert_irq, db1200_pcmcia_cdirq,
			db1200_pcmcia_cdirq_fn, 0, "pcmcia_insert", sock);
		if (ret)
			goto out1;

		ret = request_threaded_irq(sock->eject_irq, db1200_pcmcia_cdirq,
			db1200_pcmcia_cdirq_fn, 0, "pcmcia_eject", sock);
		if (ret) {
			free_irq(sock->insert_irq, sock);
			goto out1;
		}

		/* enable the currently silent one */
		if (db1x_card_inserted(sock))
			enable_irq(sock->eject_irq);
		else
			enable_irq(sock->insert_irq);
	} else {
		/* all other (older) Db1x00 boards use a GPIO to show
		 * card detection status:  use both-edge triggers.
		 */
		irq_set_irq_type(sock->insert_irq, IRQ_TYPE_EDGE_BOTH);
		ret = request_irq(sock->insert_irq, db1000_pcmcia_cdirq,
				  0, "pcmcia_carddetect", sock);

		if (ret)
			goto out1;
	}

	return 0;	/* all done */

out1:
	if (sock->stschg_irq != -1)
		free_irq(sock->stschg_irq, sock);

	return ret;
}

static void db1x_pcmcia_free_irqs(struct db1x_pcmcia_sock *sock)
{
	if (sock->stschg_irq != -1)
		free_irq(sock->stschg_irq, sock);

	free_irq(sock->insert_irq, sock);
	if (sock->eject_irq != -1)
		free_irq(sock->eject_irq, sock);
}

/*
 * configure a PCMCIA socket on the Db1x00 series of boards (and
 * compatibles).
 *
 * 2 external registers are involved:
 *   pcmcia_status (offset 0x04): bits [0:1/2:3]: read card voltage id
 *   pcmcia_control(offset 0x10):
 *	bits[0:1] set vcc for card
 *	bits[2:3] set vpp for card
 *	bit 4:	enable data buffers
 *	bit 7:	reset# for card
 *	add 8 for second socket.
 */
static int db1x_pcmcia_configure(struct pcmcia_socket *skt,
				 struct socket_state_t *state)
{
	struct db1x_pcmcia_sock *sock = to_db1x_socket(skt);
	unsigned short cr_clr, cr_set;
	unsigned int changed;
	int v, p, ret;

	/* card voltage setup */
	cr_clr = (0xf << (sock->nr * 8)); /* clear voltage settings */
	cr_set = 0;
	v = p = ret = 0;

	switch (state->Vcc) {
	case 50:
		++v;
		/* fall through */
	case 33:
		++v;
		/* fall through */
	case 0:
		break;
	default:
		printk(KERN_INFO "pcmcia%d unsupported Vcc %d\n",
			sock->nr, state->Vcc);
	}

	switch (state->Vpp) {
	case 12:
		++p;
		/* fall through */
	case 33:
	case 50:
		++p;
		/* fall through */
	case 0:
		break;
	default:
		printk(KERN_INFO "pcmcia%d unsupported Vpp %d\n",
			sock->nr, state->Vpp);
	}

	/* sanity check: Vpp must be 0, 12, or Vcc */
	if (((state->Vcc == 33) && (state->Vpp == 50)) ||
	    ((state->Vcc == 50) && (state->Vpp == 33))) {
		printk(KERN_INFO "pcmcia%d bad Vcc/Vpp combo (%d %d)\n",
			sock->nr, state->Vcc, state->Vpp);
		v = p = 0;
		ret = -EINVAL;
	}

	/* create new voltage code */
	if (sock->board_type != BOARD_TYPE_DB1300)
		cr_set |= ((v << 2) | p) << (sock->nr * 8);

	changed = state->flags ^ sock->old_flags;

	if (changed & SS_RESET) {
		if (state->flags & SS_RESET) {
			set_stschg(sock, 0);
			/* assert reset, disable io buffers */
			cr_clr |= (1 << (7 + (sock->nr * 8)));
			cr_clr |= (1 << (4 + (sock->nr * 8)));
		} else {
			/* de-assert reset, enable io buffers */
			cr_set |= 1 << (7 + (sock->nr * 8));
			cr_set |= 1 << (4 + (sock->nr * 8));
		}
	}

	/* update PCMCIA configuration */
	bcsr_mod(BCSR_PCMCIA, cr_clr, cr_set);

	sock->old_flags = state->flags;

	/* reset was taken away: give card time to initialize properly */
	if ((changed & SS_RESET) && !(state->flags & SS_RESET)) {
		msleep(500);
		set_stschg(sock, 1);
	}

	return ret;
}

/* VCC bits at [3:2]/[11:10] */
#define GET_VCC(cr, socknr)		\
	((((cr) >> 2) >> ((socknr) * 8)) & 3)

/* VS bits at [0:1]/[3:2] */
#define GET_VS(sr, socknr)		\
	(((sr) >> (2 * (socknr))) & 3)

/* reset bits at [7]/[15] */
#define GET_RESET(cr, socknr)		\
	((cr) & (1 << (7 + (8 * (socknr)))))

static int db1x_pcmcia_get_status(struct pcmcia_socket *skt,
				  unsigned int *value)
{
	struct db1x_pcmcia_sock *sock = to_db1x_socket(skt);
	unsigned short cr, sr;
	unsigned int status;

	status = db1x_card_inserted(sock) ? SS_DETECT : 0;

	cr = bcsr_read(BCSR_PCMCIA);
	sr = bcsr_read(BCSR_STATUS);

	/* PB1100/PB1500: voltage key bits are at [5:4] */
	if (sock->board_type == BOARD_TYPE_PB1100)
		sr >>= 4;

	/* determine card type */
	switch (GET_VS(sr, sock->nr)) {
	case 0:
	case 2:
		status |= SS_3VCARD;	/* 3V card */
	case 3:
		break;			/* 5V card: set nothing */
	default:
		status |= SS_XVCARD;	/* treated as unsupported in core */
	}

	/* if Vcc is not zero, we have applied power to a card */
	status |= GET_VCC(cr, sock->nr) ? SS_POWERON : 0;

	/* DB1300: power always on, but don't tell when no card present */
	if ((sock->board_type == BOARD_TYPE_DB1300) && (status & SS_DETECT))
		status = SS_POWERON | SS_3VCARD | SS_DETECT;

	/* reset de-asserted? then we're ready */
	status |= (GET_RESET(cr, sock->nr)) ? SS_READY : SS_RESET;

	*value = status;

	return 0;
}

static int db1x_pcmcia_sock_init(struct pcmcia_socket *skt)
{
	return 0;
}

static int db1x_pcmcia_sock_suspend(struct pcmcia_socket *skt)
{
	return 0;
}

static int au1x00_pcmcia_set_io_map(struct pcmcia_socket *skt,
				    struct pccard_io_map *map)
{
	struct db1x_pcmcia_sock *sock = to_db1x_socket(skt);

	map->start = (u32)sock->virt_io;
	map->stop = map->start + IO_MAP_SIZE;

	return 0;
}

static int au1x00_pcmcia_set_mem_map(struct pcmcia_socket *skt,
				     struct pccard_mem_map *map)
{
	struct db1x_pcmcia_sock *sock = to_db1x_socket(skt);

	if (map->flags & MAP_ATTRIB)
		map->static_start = sock->phys_attr + map->card_start;
	else
		map->static_start = sock->phys_mem + map->card_start;

	return 0;
}

static struct pccard_operations db1x_pcmcia_operations = {
	.init			= db1x_pcmcia_sock_init,
	.suspend		= db1x_pcmcia_sock_suspend,
	.get_status		= db1x_pcmcia_get_status,
	.set_socket		= db1x_pcmcia_configure,
	.set_io_map		= au1x00_pcmcia_set_io_map,
	.set_mem_map		= au1x00_pcmcia_set_mem_map,
};

static int db1x_pcmcia_socket_probe(struct platform_device *pdev)
{
	struct db1x_pcmcia_sock *sock;
	struct resource *r;
	int ret, bid;

	sock = kzalloc(sizeof(struct db1x_pcmcia_sock), GFP_KERNEL);
	if (!sock)
		return -ENOMEM;

	sock->nr = pdev->id;

	bid = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));
	switch (bid) {
	case BCSR_WHOAMI_PB1500:
	case BCSR_WHOAMI_PB1500R2:
	case BCSR_WHOAMI_PB1100:
		sock->board_type = BOARD_TYPE_PB1100;
		break;
	case BCSR_WHOAMI_DB1000 ... BCSR_WHOAMI_PB1550_SDR:
		sock->board_type = BOARD_TYPE_DEFAULT;
		break;
	case BCSR_WHOAMI_PB1200 ... BCSR_WHOAMI_DB1200:
		sock->board_type = BOARD_TYPE_DB1200;
		break;
	case BCSR_WHOAMI_DB1300:
		sock->board_type = BOARD_TYPE_DB1300;
		break;
	default:
		printk(KERN_INFO "db1xxx-ss: unknown board %d!\n", bid);
		ret = -ENODEV;
		goto out0;
	};

	/*
	 * gather resources necessary and optional nice-to-haves to
	 * operate a socket:
	 * This includes IRQs for Carddetection/ejection, the card
	 *  itself and optional status change detection.
	 * Also, the memory areas covered by a socket.  For these
	 *  we require the real 36bit addresses (see the au1000.h
	 *  header for more information).
	 */

	/* card: irq assigned to the card itself. */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "card");
	sock->card_irq = r ? r->start : 0;

	/* insert: irq which triggers on card insertion/ejection
	 * BIG FAT NOTE: on DB1000/1100/1500/1550 we pass a GPIO here!
	 */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "insert");
	sock->insert_irq = r ? r->start : -1;
	if (sock->board_type == BOARD_TYPE_DEFAULT) {
		sock->insert_gpio = r ? r->start : -1;
		sock->insert_irq = r ? gpio_to_irq(r->start) : -1;
	}

	/* stschg: irq which trigger on card status change (optional) */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "stschg");
	sock->stschg_irq = r ? r->start : -1;

	/* eject: irq which triggers on ejection (DB1200/PB1200 only) */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "eject");
	sock->eject_irq = r ? r->start : -1;

	ret = -ENODEV;

	/* 36bit PCMCIA Attribute area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-attr");
	if (!r) {
		printk(KERN_ERR "pcmcia%d has no 'pseudo-attr' resource!\n",
			sock->nr);
		goto out0;
	}
	sock->phys_attr = r->start;

	/* 36bit PCMCIA Memory area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-mem");
	if (!r) {
		printk(KERN_ERR "pcmcia%d has no 'pseudo-mem' resource!\n",
			sock->nr);
		goto out0;
	}
	sock->phys_mem = r->start;

	/* 36bit PCMCIA IO area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-io");
	if (!r) {
		printk(KERN_ERR "pcmcia%d has no 'pseudo-io' resource!\n",
			sock->nr);
		goto out0;
	}
	sock->phys_io = r->start;

	/*
	 * PCMCIA client drivers use the inb/outb macros to access
	 * the IO registers.  Since mips_io_port_base is added
	 * to the access address of the mips implementation of
	 * inb/outb, we need to subtract it here because we want
	 * to access the I/O or MEM address directly, without
	 * going through this "mips_io_port_base" mechanism.
	 */
	sock->virt_io = (void *)(ioremap(sock->phys_io, IO_MAP_SIZE) -
				 mips_io_port_base);

	if (!sock->virt_io) {
		printk(KERN_ERR "pcmcia%d: cannot remap IO area\n",
			sock->nr);
		ret = -ENOMEM;
		goto out0;
	}

	sock->socket.ops	= &db1x_pcmcia_operations;
	sock->socket.owner	= THIS_MODULE;
	sock->socket.pci_irq	= sock->card_irq;
	sock->socket.features	= SS_CAP_STATIC_MAP | SS_CAP_PCCARD;
	sock->socket.map_size	= MEM_MAP_SIZE;
	sock->socket.io_offset	= (unsigned long)sock->virt_io;
	sock->socket.dev.parent	= &pdev->dev;
	sock->socket.resource_ops = &pccard_static_ops;

	platform_set_drvdata(pdev, sock);

	ret = db1x_pcmcia_setup_irqs(sock);
	if (ret) {
		printk(KERN_ERR "pcmcia%d cannot setup interrupts\n",
			sock->nr);
		goto out1;
	}

	set_stschg(sock, 0);

	ret = pcmcia_register_socket(&sock->socket);
	if (ret) {
		printk(KERN_ERR "pcmcia%d failed to register\n", sock->nr);
		goto out2;
	}

	printk(KERN_INFO "Alchemy Db/Pb1xxx pcmcia%d @ io/attr/mem %09llx"
		"(%p) %09llx %09llx  card/insert/stschg/eject irqs @ %d "
		"%d %d %d\n", sock->nr, sock->phys_io, sock->virt_io,
		sock->phys_attr, sock->phys_mem, sock->card_irq,
		sock->insert_irq, sock->stschg_irq, sock->eject_irq);

	return 0;

out2:
	db1x_pcmcia_free_irqs(sock);
out1:
	iounmap((void *)(sock->virt_io + (u32)mips_io_port_base));
out0:
	kfree(sock);
	return ret;
}

static int db1x_pcmcia_socket_remove(struct platform_device *pdev)
{
	struct db1x_pcmcia_sock *sock = platform_get_drvdata(pdev);

	db1x_pcmcia_free_irqs(sock);
	pcmcia_unregister_socket(&sock->socket);
	iounmap((void *)(sock->virt_io + (u32)mips_io_port_base));
	kfree(sock);

	return 0;
}

static struct platform_driver db1x_pcmcia_socket_driver = {
	.driver	= {
		.name	= "db1xxx_pcmcia",
	},
	.probe		= db1x_pcmcia_socket_probe,
	.remove		= db1x_pcmcia_socket_remove,
};

module_platform_driver(db1x_pcmcia_socket_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCMCIA Socket Services for Alchemy Db/Pb1x00 boards");
MODULE_AUTHOR("Manuel Lauss");
