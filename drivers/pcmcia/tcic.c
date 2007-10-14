/*======================================================================

    Device driver for Databook TCIC-2 PCMCIA controller

    tcic.c 1.111 2000/02/15 04:13:12

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/ss.h>
#include "tcic.h"

#ifdef DEBUG
static int pc_debug;

module_param(pc_debug, int, 0644);
static const char version[] =
"tcic.c 1.111 2000/02/15 04:13:12 (David Hinds)";

#define debug(lvl, fmt, arg...) do {				\
	if (pc_debug > (lvl))					\
		printk(KERN_DEBUG "tcic: " fmt , ## arg);	\
} while (0)
#else
#define debug(lvl, fmt, arg...) do { } while (0)
#endif

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("Databook TCIC-2 PCMCIA socket driver");
MODULE_LICENSE("Dual MPL/GPL");

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* The base port address of the TCIC-2 chip */
static unsigned long tcic_base = TCIC_BASE;

/* Specify a socket number to ignore */
static int ignore = -1;

/* Probe for safe interrupts? */
static int do_scan = 1;

/* Bit map of interrupts to choose from */
static u_int irq_mask = 0xffff;
static int irq_list[16];
static unsigned int irq_list_count;

/* The card status change interrupt -- 0 means autoselect */
static int cs_irq;

/* Poll status interval -- 0 means default to interrupt */
static int poll_interval;

/* Delay for card status double-checking */
static int poll_quick = HZ/20;

/* CCLK external clock time, in nanoseconds.  70 ns = 14.31818 MHz */
static int cycle_time = 70;

module_param(tcic_base, ulong, 0444);
module_param(ignore, int, 0444);
module_param(do_scan, int, 0444);
module_param(irq_mask, int, 0444);
module_param_array(irq_list, int, &irq_list_count, 0444);
module_param(cs_irq, int, 0444);
module_param(poll_interval, int, 0444);
module_param(poll_quick, int, 0444);
module_param(cycle_time, int, 0444);

/*====================================================================*/

static irqreturn_t tcic_interrupt(int irq, void *dev);
static void tcic_timer(u_long data);
static struct pccard_operations tcic_operations;

struct tcic_socket {
    u_short	psock;
    u_char	last_sstat;
    u_char	id;
    struct pcmcia_socket	socket;
};

static struct timer_list poll_timer;
static int tcic_timer_pending;

static int sockets;
static struct tcic_socket socket_table[2];

/*====================================================================*/

/* Trick when selecting interrupts: the TCIC sktirq pin is supposed
   to map to irq 11, but is coded as 0 or 1 in the irq registers. */
#define TCIC_IRQ(x) ((x) ? (((x) == 11) ? 1 : (x)) : 15)

#ifdef DEBUG_X
static u_char tcic_getb(u_char reg)
{
    u_char val = inb(tcic_base+reg);
    printk(KERN_DEBUG "tcic_getb(%#lx) = %#x\n", tcic_base+reg, val);
    return val;
}

static u_short tcic_getw(u_char reg)
{
    u_short val = inw(tcic_base+reg);
    printk(KERN_DEBUG "tcic_getw(%#lx) = %#x\n", tcic_base+reg, val);
    return val;
}

static void tcic_setb(u_char reg, u_char data)
{
    printk(KERN_DEBUG "tcic_setb(%#lx, %#x)\n", tcic_base+reg, data);
    outb(data, tcic_base+reg);
}

static void tcic_setw(u_char reg, u_short data)
{
    printk(KERN_DEBUG "tcic_setw(%#lx, %#x)\n", tcic_base+reg, data);
    outw(data, tcic_base+reg);
}
#else
#define tcic_getb(reg) inb(tcic_base+reg)
#define tcic_getw(reg) inw(tcic_base+reg)
#define tcic_setb(reg, data) outb(data, tcic_base+reg)
#define tcic_setw(reg, data) outw(data, tcic_base+reg)
#endif

static void tcic_setl(u_char reg, u_int data)
{
#ifdef DEBUG_X
    printk(KERN_DEBUG "tcic_setl(%#x, %#lx)\n", tcic_base+reg, data);
#endif
    outw(data & 0xffff, tcic_base+reg);
    outw(data >> 16, tcic_base+reg+2);
}

static void tcic_aux_setb(u_short reg, u_char data)
{
    u_char mode = (tcic_getb(TCIC_MODE) & TCIC_MODE_PGMMASK) | reg;
    tcic_setb(TCIC_MODE, mode);
    tcic_setb(TCIC_AUX, data);
}

static u_short tcic_aux_getw(u_short reg)
{
    u_char mode = (tcic_getb(TCIC_MODE) & TCIC_MODE_PGMMASK) | reg;
    tcic_setb(TCIC_MODE, mode);
    return tcic_getw(TCIC_AUX);
}

static void tcic_aux_setw(u_short reg, u_short data)
{
    u_char mode = (tcic_getb(TCIC_MODE) & TCIC_MODE_PGMMASK) | reg;
    tcic_setb(TCIC_MODE, mode);
    tcic_setw(TCIC_AUX, data);
}

/*====================================================================*/

/* Time conversion functions */

static int to_cycles(int ns)
{
    if (ns < 14)
	return 0;
    else
	return 2*(ns-14)/cycle_time;
}

/*====================================================================*/

static volatile u_int irq_hits;

static irqreturn_t __init tcic_irq_count(int irq, void *dev)
{
    irq_hits++;
    return IRQ_HANDLED;
}

static u_int __init try_irq(int irq)
{
    u_short cfg;

    irq_hits = 0;
    if (request_irq(irq, tcic_irq_count, 0, "irq scan", tcic_irq_count) != 0)
	return -1;
    mdelay(10);
    if (irq_hits) {
	free_irq(irq, tcic_irq_count);
	return -1;
    }

    /* Generate one interrupt */
    cfg = TCIC_SYSCFG_AUTOBUSY | 0x0a00;
    tcic_aux_setw(TCIC_AUX_SYSCFG, cfg | TCIC_IRQ(irq));
    tcic_setb(TCIC_IENA, TCIC_IENA_ERR | TCIC_IENA_CFG_HIGH);
    tcic_setb(TCIC_ICSR, TCIC_ICSR_ERR | TCIC_ICSR_JAM);

    udelay(1000);
    free_irq(irq, tcic_irq_count);

    /* Turn off interrupts */
    tcic_setb(TCIC_IENA, TCIC_IENA_CFG_OFF);
    while (tcic_getb(TCIC_ICSR))
	tcic_setb(TCIC_ICSR, TCIC_ICSR_JAM);
    tcic_aux_setw(TCIC_AUX_SYSCFG, cfg);
    
    return (irq_hits != 1);
}

static u_int __init irq_scan(u_int mask0)
{
    u_int mask1;
    int i;

#ifdef __alpha__
#define PIC 0x4d0
    /* Don't probe level-triggered interrupts -- reserved for PCI */
    int level_mask = inb_p(PIC) | (inb_p(PIC+1) << 8);
    if (level_mask)
	mask0 &= ~level_mask;
#endif

    mask1 = 0;
    if (do_scan) {
	for (i = 0; i < 16; i++)
	    if ((mask0 & (1 << i)) && (try_irq(i) == 0))
		mask1 |= (1 << i);
	for (i = 0; i < 16; i++)
	    if ((mask1 & (1 << i)) && (try_irq(i) != 0)) {
		mask1 ^= (1 << i);
	    }
    }
    
    if (mask1) {
	printk("scanned");
    } else {
	/* Fallback: just find interrupts that aren't in use */
	for (i = 0; i < 16; i++)
	    if ((mask0 & (1 << i)) &&
		(request_irq(i, tcic_irq_count, 0, "x", tcic_irq_count) == 0)) {
		mask1 |= (1 << i);
		free_irq(i, tcic_irq_count);
	    }
	printk("default");
    }
    
    printk(") = ");
    for (i = 0; i < 16; i++)
	if (mask1 & (1<<i))
	    printk("%s%d", ((mask1 & ((1<<i)-1)) ? "," : ""), i);
    printk(" ");
    
    return mask1;
}

/*======================================================================

    See if a card is present, powered up, in IO mode, and already
    bound to a (non-PCMCIA) Linux driver.

    We make an exception for cards that look like serial devices.
    
======================================================================*/

static int __init is_active(int s)
{
    u_short scf1, ioctl, base, num;
    u_char pwr, sstat;
    u_int addr;
    
    tcic_setl(TCIC_ADDR, (s << TCIC_ADDR_SS_SHFT)
	      | TCIC_ADDR_INDREG | TCIC_SCF1(s));
    scf1 = tcic_getw(TCIC_DATA);
    pwr = tcic_getb(TCIC_PWR);
    sstat = tcic_getb(TCIC_SSTAT);
    addr = TCIC_IWIN(s, 0);
    tcic_setw(TCIC_ADDR, addr + TCIC_IBASE_X);
    base = tcic_getw(TCIC_DATA);
    tcic_setw(TCIC_ADDR, addr + TCIC_ICTL_X);
    ioctl = tcic_getw(TCIC_DATA);

    if (ioctl & TCIC_ICTL_TINY)
	num = 1;
    else {
	num = (base ^ (base-1));
	base = base & (base-1);
    }

    if ((sstat & TCIC_SSTAT_CD) && (pwr & TCIC_PWR_VCC(s)) &&
	(scf1 & TCIC_SCF1_IOSTS) && (ioctl & TCIC_ICTL_ENA) &&
	((base & 0xfeef) != 0x02e8)) {
	struct resource *res = request_region(base, num, "tcic-2");
	if (!res) /* region is busy */
	    return 1;
	release_region(base, num);
    }

    return 0;
}

/*======================================================================

    This returns the revision code for the specified socket.
    
======================================================================*/

static int __init get_tcic_id(void)
{
    u_short id;
    
    tcic_aux_setw(TCIC_AUX_TEST, TCIC_TEST_DIAG);
    id = tcic_aux_getw(TCIC_AUX_ILOCK);
    id = (id & TCIC_ILOCKTEST_ID_MASK) >> TCIC_ILOCKTEST_ID_SH;
    tcic_aux_setw(TCIC_AUX_TEST, 0);
    return id;
}

/*====================================================================*/

static struct device_driver tcic_driver = {
	.name = "tcic-pcmcia",
	.bus = &platform_bus_type,
	.suspend = pcmcia_socket_dev_suspend,
	.resume = pcmcia_socket_dev_resume,
};

static struct platform_device tcic_device = {
	.name = "tcic-pcmcia",
	.id = 0,
};


static int __init init_tcic(void)
{
    int i, sock, ret = 0;
    u_int mask, scan;

    if (driver_register(&tcic_driver))
	return -1;
    
    printk(KERN_INFO "Databook TCIC-2 PCMCIA probe: ");
    sock = 0;

    if (!request_region(tcic_base, 16, "tcic-2")) {
	printk("could not allocate ports,\n ");
	driver_unregister(&tcic_driver);
	return -ENODEV;
    }
    else {
	tcic_setw(TCIC_ADDR, 0);
	if (tcic_getw(TCIC_ADDR) == 0) {
	    tcic_setw(TCIC_ADDR, 0xc3a5);
	    if (tcic_getw(TCIC_ADDR) == 0xc3a5) sock = 2;
	}
	if (sock == 0) {
	    /* See if resetting the controller does any good */
	    tcic_setb(TCIC_SCTRL, TCIC_SCTRL_RESET);
	    tcic_setb(TCIC_SCTRL, 0);
	    tcic_setw(TCIC_ADDR, 0);
	    if (tcic_getw(TCIC_ADDR) == 0) {
		tcic_setw(TCIC_ADDR, 0xc3a5);
		if (tcic_getw(TCIC_ADDR) == 0xc3a5) sock = 2;
	    }
	}
    }
    if (sock == 0) {
	printk("not found.\n");
	release_region(tcic_base, 16);
	driver_unregister(&tcic_driver);
	return -ENODEV;
    }

    sockets = 0;
    for (i = 0; i < sock; i++) {
	if ((i == ignore) || is_active(i)) continue;
	socket_table[sockets].psock = i;
	socket_table[sockets].id = get_tcic_id();

	socket_table[sockets].socket.owner = THIS_MODULE;
	/* only 16-bit cards, memory windows must be size-aligned */
	/* No PCI or CardBus support */
	socket_table[sockets].socket.features = SS_CAP_PCCARD | SS_CAP_MEM_ALIGN;
	/* irq 14, 11, 10, 7, 6, 5, 4, 3 */
	socket_table[sockets].socket.irq_mask = 0x4cf8;
	/* 4K minimum window size */
	socket_table[sockets].socket.map_size = 0x1000;		
	sockets++;
    }

    switch (socket_table[0].id) {
    case TCIC_ID_DB86082:
	printk("DB86082"); break;
    case TCIC_ID_DB86082A:
	printk("DB86082A"); break;
    case TCIC_ID_DB86084:
	printk("DB86084"); break;
    case TCIC_ID_DB86084A:
	printk("DB86084A"); break;
    case TCIC_ID_DB86072:
	printk("DB86072"); break;
    case TCIC_ID_DB86184:
	printk("DB86184"); break;
    case TCIC_ID_DB86082B:
	printk("DB86082B"); break;
    default:
	printk("Unknown ID 0x%02x", socket_table[0].id);
    }
    
    /* Set up polling */
    poll_timer.function = &tcic_timer;
    poll_timer.data = 0;
    init_timer(&poll_timer);

    /* Build interrupt mask */
    printk(", %d sockets\n" KERN_INFO "  irq list (", sockets);
    if (irq_list_count == 0)
	mask = irq_mask;
    else
	for (i = mask = 0; i < irq_list_count; i++)
	    mask |= (1<<irq_list[i]);

    /* irq 14, 11, 10, 7, 6, 5, 4, 3 */
    mask &= 0x4cf8;
    /* Scan interrupts */
    mask = irq_scan(mask);
    for (i=0;i<sockets;i++)
	    socket_table[i].socket.irq_mask = mask;
    
    /* Check for only two interrupts available */
    scan = (mask & (mask-1));
    if (((scan & (scan-1)) == 0) && (poll_interval == 0))
	poll_interval = HZ;
    
    if (poll_interval == 0) {
	/* Avoid irq 12 unless it is explicitly requested */
	u_int cs_mask = mask & ((cs_irq) ? (1<<cs_irq) : ~(1<<12));
	for (i = 15; i > 0; i--)
	    if ((cs_mask & (1 << i)) &&
		(request_irq(i, tcic_interrupt, 0, "tcic",
			     tcic_interrupt) == 0))
		break;
	cs_irq = i;
	if (cs_irq == 0) poll_interval = HZ;
    }
    
    if (socket_table[0].socket.irq_mask & (1 << 11))
	printk("sktirq is irq 11, ");
    if (cs_irq != 0)
	printk("status change on irq %d\n", cs_irq);
    else
	printk("polled status, interval = %d ms\n",
	       poll_interval * 1000 / HZ);
    
    for (i = 0; i < sockets; i++) {
	tcic_setw(TCIC_ADDR+2, socket_table[i].psock << TCIC_SS_SHFT);
	socket_table[i].last_sstat = tcic_getb(TCIC_SSTAT);
    }
    
    /* jump start interrupt handler, if needed */
    tcic_interrupt(0, NULL);

    platform_device_register(&tcic_device);

    for (i = 0; i < sockets; i++) {
	    socket_table[i].socket.ops = &tcic_operations;
	    socket_table[i].socket.resource_ops = &pccard_nonstatic_ops;
	    socket_table[i].socket.dev.parent = &tcic_device.dev;
	    ret = pcmcia_register_socket(&socket_table[i].socket);
	    if (ret && i)
		    pcmcia_unregister_socket(&socket_table[0].socket);
    }
    
    return ret;

    return 0;
    
} /* init_tcic */

/*====================================================================*/

static void __exit exit_tcic(void)
{
    int i;

    del_timer_sync(&poll_timer);
    if (cs_irq != 0) {
	tcic_aux_setw(TCIC_AUX_SYSCFG, TCIC_SYSCFG_AUTOBUSY|0x0a00);
	free_irq(cs_irq, tcic_interrupt);
    }
    release_region(tcic_base, 16);

    for (i = 0; i < sockets; i++) {
	    pcmcia_unregister_socket(&socket_table[i].socket);	    
    }

    platform_device_unregister(&tcic_device);
    driver_unregister(&tcic_driver);
} /* exit_tcic */

/*====================================================================*/

static irqreturn_t tcic_interrupt(int irq, void *dev)
{
    int i, quick = 0;
    u_char latch, sstat;
    u_short psock;
    u_int events;
    static volatile int active = 0;

    if (active) {
	printk(KERN_NOTICE "tcic: reentered interrupt handler!\n");
	return IRQ_NONE;
    } else
	active = 1;

    debug(2, "tcic_interrupt()\n");
    
    for (i = 0; i < sockets; i++) {
	psock = socket_table[i].psock;
	tcic_setl(TCIC_ADDR, (psock << TCIC_ADDR_SS_SHFT)
		  | TCIC_ADDR_INDREG | TCIC_SCF1(psock));
	sstat = tcic_getb(TCIC_SSTAT);
	latch = sstat ^ socket_table[psock].last_sstat;
	socket_table[i].last_sstat = sstat;
	if (tcic_getb(TCIC_ICSR) & TCIC_ICSR_CDCHG) {
	    tcic_setb(TCIC_ICSR, TCIC_ICSR_CLEAR);
	    quick = 1;
	}
	if (latch == 0)
	    continue;
	events = (latch & TCIC_SSTAT_CD) ? SS_DETECT : 0;
	events |= (latch & TCIC_SSTAT_WP) ? SS_WRPROT : 0;
	if (tcic_getw(TCIC_DATA) & TCIC_SCF1_IOSTS) {
	    events |= (latch & TCIC_SSTAT_LBAT1) ? SS_STSCHG : 0;
	} else {
	    events |= (latch & TCIC_SSTAT_RDY) ? SS_READY : 0;
	    events |= (latch & TCIC_SSTAT_LBAT1) ? SS_BATDEAD : 0;
	    events |= (latch & TCIC_SSTAT_LBAT2) ? SS_BATWARN : 0;
	}
	if (events) {
		pcmcia_parse_events(&socket_table[i].socket, events);
	}
    }

    /* Schedule next poll, if needed */
    if (((cs_irq == 0) || quick) && (!tcic_timer_pending)) {
	poll_timer.expires = jiffies + (quick ? poll_quick : poll_interval);
	add_timer(&poll_timer);
	tcic_timer_pending = 1;
    }
    active = 0;
    
    debug(2, "interrupt done\n");
    return IRQ_HANDLED;
} /* tcic_interrupt */

static void tcic_timer(u_long data)
{
    debug(2, "tcic_timer()\n");
    tcic_timer_pending = 0;
    tcic_interrupt(0, NULL);
} /* tcic_timer */

/*====================================================================*/

static int tcic_get_status(struct pcmcia_socket *sock, u_int *value)
{
    u_short psock = container_of(sock, struct tcic_socket, socket)->psock;
    u_char reg;

    tcic_setl(TCIC_ADDR, (psock << TCIC_ADDR_SS_SHFT)
	      | TCIC_ADDR_INDREG | TCIC_SCF1(psock));
    reg = tcic_getb(TCIC_SSTAT);
    *value  = (reg & TCIC_SSTAT_CD) ? SS_DETECT : 0;
    *value |= (reg & TCIC_SSTAT_WP) ? SS_WRPROT : 0;
    if (tcic_getw(TCIC_DATA) & TCIC_SCF1_IOSTS) {
	*value |= (reg & TCIC_SSTAT_LBAT1) ? SS_STSCHG : 0;
    } else {
	*value |= (reg & TCIC_SSTAT_RDY) ? SS_READY : 0;
	*value |= (reg & TCIC_SSTAT_LBAT1) ? SS_BATDEAD : 0;
	*value |= (reg & TCIC_SSTAT_LBAT2) ? SS_BATWARN : 0;
    }
    reg = tcic_getb(TCIC_PWR);
    if (reg & (TCIC_PWR_VCC(psock)|TCIC_PWR_VPP(psock)))
	*value |= SS_POWERON;
    debug(1, "GetStatus(%d) = %#2.2x\n", psock, *value);
    return 0;
} /* tcic_get_status */

/*====================================================================*/

static int tcic_set_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
    u_short psock = container_of(sock, struct tcic_socket, socket)->psock;
    u_char reg;
    u_short scf1, scf2;

    debug(1, "SetSocket(%d, flags %#3.3x, Vcc %d, Vpp %d, "
	  "io_irq %d, csc_mask %#2.2x)\n", psock, state->flags,
	  state->Vcc, state->Vpp, state->io_irq, state->csc_mask);
    tcic_setw(TCIC_ADDR+2, (psock << TCIC_SS_SHFT) | TCIC_ADR2_INDREG);

    reg = tcic_getb(TCIC_PWR);
    reg &= ~(TCIC_PWR_VCC(psock) | TCIC_PWR_VPP(psock));

    if (state->Vcc == 50) {
	switch (state->Vpp) {
	case 0:   reg |= TCIC_PWR_VCC(psock) | TCIC_PWR_VPP(psock); break;
	case 50:  reg |= TCIC_PWR_VCC(psock); break;
	case 120: reg |= TCIC_PWR_VPP(psock); break;
	default:  return -EINVAL;
	}
    } else if (state->Vcc != 0)
	return -EINVAL;

    if (reg != tcic_getb(TCIC_PWR))
	tcic_setb(TCIC_PWR, reg);

    reg = TCIC_ILOCK_HOLD_CCLK | TCIC_ILOCK_CWAIT;
    if (state->flags & SS_OUTPUT_ENA) {
	tcic_setb(TCIC_SCTRL, TCIC_SCTRL_ENA);
	reg |= TCIC_ILOCK_CRESENA;
    } else
	tcic_setb(TCIC_SCTRL, 0);
    if (state->flags & SS_RESET)
	reg |= TCIC_ILOCK_CRESET;
    tcic_aux_setb(TCIC_AUX_ILOCK, reg);
    
    tcic_setw(TCIC_ADDR, TCIC_SCF1(psock));
    scf1 = TCIC_SCF1_FINPACK;
    scf1 |= TCIC_IRQ(state->io_irq);
    if (state->flags & SS_IOCARD) {
	scf1 |= TCIC_SCF1_IOSTS;
	if (state->flags & SS_SPKR_ENA)
	    scf1 |= TCIC_SCF1_SPKR;
	if (state->flags & SS_DMA_MODE)
	    scf1 |= TCIC_SCF1_DREQ2 << TCIC_SCF1_DMA_SHIFT;
    }
    tcic_setw(TCIC_DATA, scf1);

    /* Some general setup stuff, and configure status interrupt */
    reg = TCIC_WAIT_ASYNC | TCIC_WAIT_SENSE | to_cycles(250);
    tcic_aux_setb(TCIC_AUX_WCTL, reg);
    tcic_aux_setw(TCIC_AUX_SYSCFG, TCIC_SYSCFG_AUTOBUSY|0x0a00|
		  TCIC_IRQ(cs_irq));
    
    /* Card status change interrupt mask */
    tcic_setw(TCIC_ADDR, TCIC_SCF2(psock));
    scf2 = TCIC_SCF2_MALL;
    if (state->csc_mask & SS_DETECT) scf2 &= ~TCIC_SCF2_MCD;
    if (state->flags & SS_IOCARD) {
	if (state->csc_mask & SS_STSCHG) reg &= ~TCIC_SCF2_MLBAT1;
    } else {
	if (state->csc_mask & SS_BATDEAD) reg &= ~TCIC_SCF2_MLBAT1;
	if (state->csc_mask & SS_BATWARN) reg &= ~TCIC_SCF2_MLBAT2;
	if (state->csc_mask & SS_READY) reg &= ~TCIC_SCF2_MRDY;
    }
    tcic_setw(TCIC_DATA, scf2);
    /* For the ISA bus, the irq should be active-high totem-pole */
    tcic_setb(TCIC_IENA, TCIC_IENA_CDCHG | TCIC_IENA_CFG_HIGH);

    return 0;
} /* tcic_set_socket */
  
/*====================================================================*/

static int tcic_set_io_map(struct pcmcia_socket *sock, struct pccard_io_map *io)
{
    u_short psock = container_of(sock, struct tcic_socket, socket)->psock;
    u_int addr;
    u_short base, len, ioctl;
    
    debug(1, "SetIOMap(%d, %d, %#2.2x, %d ns, "
	  "%#lx-%#lx)\n", psock, io->map, io->flags,
	  io->speed, io->start, io->stop);
    if ((io->map > 1) || (io->start > 0xffff) || (io->stop > 0xffff) ||
	(io->stop < io->start)) return -EINVAL;
    tcic_setw(TCIC_ADDR+2, TCIC_ADR2_INDREG | (psock << TCIC_SS_SHFT));
    addr = TCIC_IWIN(psock, io->map);

    base = io->start; len = io->stop - io->start;
    /* Check to see that len+1 is power of two, etc */
    if ((len & (len+1)) || (base & len)) return -EINVAL;
    base |= (len+1)>>1;
    tcic_setw(TCIC_ADDR, addr + TCIC_IBASE_X);
    tcic_setw(TCIC_DATA, base);
    
    ioctl  = (psock << TCIC_ICTL_SS_SHFT);
    ioctl |= (len == 0) ? TCIC_ICTL_TINY : 0;
    ioctl |= (io->flags & MAP_ACTIVE) ? TCIC_ICTL_ENA : 0;
    ioctl |= to_cycles(io->speed) & TCIC_ICTL_WSCNT_MASK;
    if (!(io->flags & MAP_AUTOSZ)) {
	ioctl |= TCIC_ICTL_QUIET;
	ioctl |= (io->flags & MAP_16BIT) ? TCIC_ICTL_BW_16 : TCIC_ICTL_BW_8;
    }
    tcic_setw(TCIC_ADDR, addr + TCIC_ICTL_X);
    tcic_setw(TCIC_DATA, ioctl);
    
    return 0;
} /* tcic_set_io_map */

/*====================================================================*/

static int tcic_set_mem_map(struct pcmcia_socket *sock, struct pccard_mem_map *mem)
{
    u_short psock = container_of(sock, struct tcic_socket, socket)->psock;
    u_short addr, ctl;
    u_long base, len, mmap;

    debug(1, "SetMemMap(%d, %d, %#2.2x, %d ns, "
	  "%#llx-%#llx, %#x)\n", psock, mem->map, mem->flags,
	  mem->speed, (unsigned long long)mem->res->start,
	  (unsigned long long)mem->res->end, mem->card_start);
    if ((mem->map > 3) || (mem->card_start > 0x3ffffff) ||
	(mem->res->start > 0xffffff) || (mem->res->end > 0xffffff) ||
	(mem->res->start > mem->res->end) || (mem->speed > 1000))
	return -EINVAL;
    tcic_setw(TCIC_ADDR+2, TCIC_ADR2_INDREG | (psock << TCIC_SS_SHFT));
    addr = TCIC_MWIN(psock, mem->map);

    base = mem->res->start; len = mem->res->end - mem->res->start;
    if ((len & (len+1)) || (base & len)) return -EINVAL;
    if (len == 0x0fff)
	base = (base >> TCIC_MBASE_HA_SHFT) | TCIC_MBASE_4K_BIT;
    else
	base = (base | (len+1)>>1) >> TCIC_MBASE_HA_SHFT;
    tcic_setw(TCIC_ADDR, addr + TCIC_MBASE_X);
    tcic_setw(TCIC_DATA, base);
    
    mmap = mem->card_start - mem->res->start;
    mmap = (mmap >> TCIC_MMAP_CA_SHFT) & TCIC_MMAP_CA_MASK;
    if (mem->flags & MAP_ATTRIB) mmap |= TCIC_MMAP_REG;
    tcic_setw(TCIC_ADDR, addr + TCIC_MMAP_X);
    tcic_setw(TCIC_DATA, mmap);

    ctl  = TCIC_MCTL_QUIET | (psock << TCIC_MCTL_SS_SHFT);
    ctl |= to_cycles(mem->speed) & TCIC_MCTL_WSCNT_MASK;
    ctl |= (mem->flags & MAP_16BIT) ? 0 : TCIC_MCTL_B8;
    ctl |= (mem->flags & MAP_WRPROT) ? TCIC_MCTL_WP : 0;
    ctl |= (mem->flags & MAP_ACTIVE) ? TCIC_MCTL_ENA : 0;
    tcic_setw(TCIC_ADDR, addr + TCIC_MCTL_X);
    tcic_setw(TCIC_DATA, ctl);
    
    return 0;
} /* tcic_set_mem_map */

/*====================================================================*/

static int tcic_init(struct pcmcia_socket *s)
{
	int i;
	struct resource res = { .start = 0, .end = 0x1000 };
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { .res = &res, };

	for (i = 0; i < 2; i++) {
		io.map = i;
		tcic_set_io_map(s, &io);
	}
	for (i = 0; i < 5; i++) {
		mem.map = i;
		tcic_set_mem_map(s, &mem);
	}
	return 0;
}

static struct pccard_operations tcic_operations = {
	.init		   = tcic_init,
	.get_status	   = tcic_get_status,
	.set_socket	   = tcic_set_socket,
	.set_io_map	   = tcic_set_io_map,
	.set_mem_map	   = tcic_set_mem_map,
};

/*====================================================================*/

module_init(init_tcic);
module_exit(exit_tcic);
