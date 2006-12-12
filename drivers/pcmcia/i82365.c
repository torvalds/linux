/*======================================================================

    Device driver for Intel 82365 and compatible PC Card controllers.

    i82365.c 1.265 1999/11/10 18:36:21

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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/sched.h>
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

#include <linux/isapnp.h>

/* ISA-bus controllers */
#include "i82365.h"
#include "cirrus.h"
#include "vg468.h"
#include "ricoh.h"

#ifdef DEBUG
static const char version[] =
"i82365.c 1.265 1999/11/10 18:36:21 (David Hinds)";

static int pc_debug;

module_param(pc_debug, int, 0644);

#define debug(lvl, fmt, arg...) do {				\
	if (pc_debug > (lvl))					\
		printk(KERN_DEBUG "i82365: " fmt , ## arg);	\
} while (0)
#else
#define debug(lvl, fmt, arg...) do { } while (0)
#endif

static irqreturn_t i365_count_irq(int, void *);
static inline int _check_irq(int irq, int flags)
{
    if (request_irq(irq, i365_count_irq, flags, "x", i365_count_irq) != 0)
	return -1;
    free_irq(irq, i365_count_irq);
    return 0;
}

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* Default base address for i82365sl and other ISA chips */
static unsigned long i365_base = 0x3e0;
/* Should we probe at 0x3e2 for an extra ISA controller? */
static int extra_sockets = 0;
/* Specify a socket number to ignore */
static int ignore = -1;
/* Bit map or list of interrupts to choose from */
static u_int irq_mask = 0xffff;
static int irq_list[16];
static int irq_list_count;
/* The card status change interrupt -- 0 means autoselect */
static int cs_irq = 0;

/* Probe for safe interrupts? */
static int do_scan = 1;
/* Poll status interval -- 0 means default to interrupt */
static int poll_interval = 0;
/* External clock time, in nanoseconds.  120 ns = 8.33 MHz */
static int cycle_time = 120;

/* Cirrus options */
static int has_dma = -1;
static int has_led = -1;
static int has_ring = -1;
static int dynamic_mode = 0;
static int freq_bypass = -1;
static int setup_time = -1;
static int cmd_time = -1;
static int recov_time = -1;

/* Vadem options */
static int async_clock = -1;
static int cable_mode = -1;
static int wakeup = 0;

module_param(i365_base, ulong, 0444);
module_param(ignore, int, 0444);
module_param(extra_sockets, int, 0444);
module_param(irq_mask, int, 0444);
module_param_array(irq_list, int, &irq_list_count, 0444);
module_param(cs_irq, int, 0444);
module_param(async_clock, int, 0444);
module_param(cable_mode, int, 0444);
module_param(wakeup, int, 0444);

module_param(do_scan, int, 0444);
module_param(poll_interval, int, 0444);
module_param(cycle_time, int, 0444);
module_param(has_dma, int, 0444);
module_param(has_led, int, 0444);
module_param(has_ring, int, 0444);
module_param(dynamic_mode, int, 0444);
module_param(freq_bypass, int, 0444);
module_param(setup_time, int, 0444);
module_param(cmd_time, int, 0444);
module_param(recov_time, int, 0444);

/*====================================================================*/

typedef struct cirrus_state_t {
    u_char		misc1, misc2;
    u_char		timer[6];
} cirrus_state_t;

typedef struct vg46x_state_t {
    u_char		ctl, ema;
} vg46x_state_t;

struct i82365_socket {
    u_short		type, flags;
    struct pcmcia_socket	socket;
    unsigned int	number;
    kio_addr_t		ioaddr;
    u_short		psock;
    u_char		cs_irq, intr;
    union {
	cirrus_state_t		cirrus;
	vg46x_state_t		vg46x;
    } state;
};

/* Where we keep track of our sockets... */
static int sockets = 0;
static struct i82365_socket socket[8] = {
    { 0, }, /* ... */
};

/* Default ISA interrupt mask */
#define I365_MASK	0xdeb8	/* irq 15,14,12,11,10,9,7,5,4,3 */

static int grab_irq;
static DEFINE_SPINLOCK(isa_lock);
#define ISA_LOCK(n, f) spin_lock_irqsave(&isa_lock, f)
#define ISA_UNLOCK(n, f) spin_unlock_irqrestore(&isa_lock, f)

static struct timer_list poll_timer;

/*====================================================================*/

/* These definitions must match the pcic table! */
typedef enum pcic_id {
    IS_I82365A, IS_I82365B, IS_I82365DF,
    IS_IBM, IS_RF5Cx96, IS_VLSI, IS_VG468, IS_VG469,
    IS_PD6710, IS_PD672X, IS_VT83C469,
} pcic_id;

/* Flags for classifying groups of controllers */
#define IS_VADEM	0x0001
#define IS_CIRRUS	0x0002
#define IS_VIA		0x0010
#define IS_UNKNOWN	0x0400
#define IS_VG_PWR	0x0800
#define IS_DF_PWR	0x1000
#define IS_REGISTERED	0x2000
#define IS_ALIVE	0x8000

typedef struct pcic_t {
    char		*name;
    u_short		flags;
} pcic_t;

static pcic_t pcic[] = {
    { "Intel i82365sl A step", 0 },
    { "Intel i82365sl B step", 0 },
    { "Intel i82365sl DF", IS_DF_PWR },
    { "IBM Clone", 0 },
    { "Ricoh RF5C296/396", 0 },
    { "VLSI 82C146", 0 },
    { "Vadem VG-468", IS_VADEM },
    { "Vadem VG-469", IS_VADEM|IS_VG_PWR },
    { "Cirrus PD6710", IS_CIRRUS },
    { "Cirrus PD672x", IS_CIRRUS },
    { "VIA VT83C469", IS_CIRRUS|IS_VIA },
};

#define PCIC_COUNT	(sizeof(pcic)/sizeof(pcic_t))

/*====================================================================*/

static DEFINE_SPINLOCK(bus_lock);

static u_char i365_get(u_short sock, u_short reg)
{
    unsigned long flags;
    spin_lock_irqsave(&bus_lock,flags);
    {
	kio_addr_t port = socket[sock].ioaddr;
	u_char val;
	reg = I365_REG(socket[sock].psock, reg);
	outb(reg, port); val = inb(port+1);
	spin_unlock_irqrestore(&bus_lock,flags);
	return val;
    }
}

static void i365_set(u_short sock, u_short reg, u_char data)
{
    unsigned long flags;
    spin_lock_irqsave(&bus_lock,flags);
    {
	kio_addr_t port = socket[sock].ioaddr;
	u_char val = I365_REG(socket[sock].psock, reg);
	outb(val, port); outb(data, port+1);
	spin_unlock_irqrestore(&bus_lock,flags);
    }
}

static void i365_bset(u_short sock, u_short reg, u_char mask)
{
    u_char d = i365_get(sock, reg);
    d |= mask;
    i365_set(sock, reg, d);
}

static void i365_bclr(u_short sock, u_short reg, u_char mask)
{
    u_char d = i365_get(sock, reg);
    d &= ~mask;
    i365_set(sock, reg, d);
}

static void i365_bflip(u_short sock, u_short reg, u_char mask, int b)
{
    u_char d = i365_get(sock, reg);
    if (b)
	d |= mask;
    else
	d &= ~mask;
    i365_set(sock, reg, d);
}

static u_short i365_get_pair(u_short sock, u_short reg)
{
    u_short a, b;
    a = i365_get(sock, reg);
    b = i365_get(sock, reg+1);
    return (a + (b<<8));
}

static void i365_set_pair(u_short sock, u_short reg, u_short data)
{
    i365_set(sock, reg, data & 0xff);
    i365_set(sock, reg+1, data >> 8);
}

/*======================================================================

    Code to save and restore global state information for Cirrus
    PD67xx controllers, and to set and report global configuration
    options.

    The VIA controllers also use these routines, as they are mostly
    Cirrus lookalikes, without the timing registers.
    
======================================================================*/

#define flip(v,b,f) (v = ((f)<0) ? v : ((f) ? ((v)|(b)) : ((v)&(~b))))

static void cirrus_get_state(u_short s)
{
    int i;
    cirrus_state_t *p = &socket[s].state.cirrus;
    p->misc1 = i365_get(s, PD67_MISC_CTL_1);
    p->misc1 &= (PD67_MC1_MEDIA_ENA | PD67_MC1_INPACK_ENA);
    p->misc2 = i365_get(s, PD67_MISC_CTL_2);
    for (i = 0; i < 6; i++)
	p->timer[i] = i365_get(s, PD67_TIME_SETUP(0)+i);
}

static void cirrus_set_state(u_short s)
{
    int i;
    u_char misc;
    cirrus_state_t *p = &socket[s].state.cirrus;

    misc = i365_get(s, PD67_MISC_CTL_2);
    i365_set(s, PD67_MISC_CTL_2, p->misc2);
    if (misc & PD67_MC2_SUSPEND) mdelay(50);
    misc = i365_get(s, PD67_MISC_CTL_1);
    misc &= ~(PD67_MC1_MEDIA_ENA | PD67_MC1_INPACK_ENA);
    i365_set(s, PD67_MISC_CTL_1, misc | p->misc1);
    for (i = 0; i < 6; i++)
	i365_set(s, PD67_TIME_SETUP(0)+i, p->timer[i]);
}

static u_int __init cirrus_set_opts(u_short s, char *buf)
{
    struct i82365_socket *t = &socket[s];
    cirrus_state_t *p = &socket[s].state.cirrus;
    u_int mask = 0xffff;

    if (has_ring == -1) has_ring = 1;
    flip(p->misc2, PD67_MC2_IRQ15_RI, has_ring);
    flip(p->misc2, PD67_MC2_DYNAMIC_MODE, dynamic_mode);
    flip(p->misc2, PD67_MC2_FREQ_BYPASS, freq_bypass);
    if (p->misc2 & PD67_MC2_IRQ15_RI)
	strcat(buf, " [ring]");
    if (p->misc2 & PD67_MC2_DYNAMIC_MODE)
	strcat(buf, " [dyn mode]");
    if (p->misc2 & PD67_MC2_FREQ_BYPASS)
	strcat(buf, " [freq bypass]");
    if (p->misc1 & PD67_MC1_INPACK_ENA)
	strcat(buf, " [inpack]");
    if (p->misc2 & PD67_MC2_IRQ15_RI)
	mask &= ~0x8000;
    if (has_led > 0) {
	strcat(buf, " [led]");
	mask &= ~0x1000;
    }
    if (has_dma > 0) {
	strcat(buf, " [dma]");
	mask &= ~0x0600;
    }
    if (!(t->flags & IS_VIA)) {
	if (setup_time >= 0)
	    p->timer[0] = p->timer[3] = setup_time;
	if (cmd_time > 0) {
	    p->timer[1] = cmd_time;
	    p->timer[4] = cmd_time*2+4;
	}
	if (p->timer[1] == 0) {
	    p->timer[1] = 6; p->timer[4] = 16;
	    if (p->timer[0] == 0)
		p->timer[0] = p->timer[3] = 1;
	}
	if (recov_time >= 0)
	    p->timer[2] = p->timer[5] = recov_time;
	buf += strlen(buf);
	sprintf(buf, " [%d/%d/%d] [%d/%d/%d]", p->timer[0], p->timer[1],
		p->timer[2], p->timer[3], p->timer[4], p->timer[5]);
    }
    return mask;
}

/*======================================================================

    Code to save and restore global state information for Vadem VG468
    and VG469 controllers, and to set and report global configuration
    options.
    
======================================================================*/

static void vg46x_get_state(u_short s)
{
    vg46x_state_t *p = &socket[s].state.vg46x;
    p->ctl = i365_get(s, VG468_CTL);
    if (socket[s].type == IS_VG469)
	p->ema = i365_get(s, VG469_EXT_MODE);
}

static void vg46x_set_state(u_short s)
{
    vg46x_state_t *p = &socket[s].state.vg46x;
    i365_set(s, VG468_CTL, p->ctl);
    if (socket[s].type == IS_VG469)
	i365_set(s, VG469_EXT_MODE, p->ema);
}

static u_int __init vg46x_set_opts(u_short s, char *buf)
{
    vg46x_state_t *p = &socket[s].state.vg46x;
    
    flip(p->ctl, VG468_CTL_ASYNC, async_clock);
    flip(p->ema, VG469_MODE_CABLE, cable_mode);
    if (p->ctl & VG468_CTL_ASYNC)
	strcat(buf, " [async]");
    if (p->ctl & VG468_CTL_INPACK)
	strcat(buf, " [inpack]");
    if (socket[s].type == IS_VG469) {
	u_char vsel = i365_get(s, VG469_VSELECT);
	if (vsel & VG469_VSEL_EXT_STAT) {
	    strcat(buf, " [ext mode]");
	    if (vsel & VG469_VSEL_EXT_BUS)
		strcat(buf, " [isa buf]");
	}
	if (p->ema & VG469_MODE_CABLE)
	    strcat(buf, " [cable]");
	if (p->ema & VG469_MODE_COMPAT)
	    strcat(buf, " [c step]");
    }
    return 0xffff;
}

/*======================================================================

    Generic routines to get and set controller options
    
======================================================================*/

static void get_bridge_state(u_short s)
{
    struct i82365_socket *t = &socket[s];
    if (t->flags & IS_CIRRUS)
	cirrus_get_state(s);
    else if (t->flags & IS_VADEM)
	vg46x_get_state(s);
}

static void set_bridge_state(u_short s)
{
    struct i82365_socket *t = &socket[s];
    if (t->flags & IS_CIRRUS)
	cirrus_set_state(s);
    else {
	i365_set(s, I365_GBLCTL, 0x00);
	i365_set(s, I365_GENCTL, 0x00);
    }
    i365_bflip(s, I365_INTCTL, I365_INTR_ENA, t->intr);
    if (t->flags & IS_VADEM)
	vg46x_set_state(s);
}

static u_int __init set_bridge_opts(u_short s, u_short ns)
{
    u_short i;
    u_int m = 0xffff;
    char buf[128];

    for (i = s; i < s+ns; i++) {
	if (socket[i].flags & IS_ALIVE) {
	    printk(KERN_INFO "    host opts [%d]: already alive!\n", i);
	    continue;
	}
	buf[0] = '\0';
	get_bridge_state(i);
	if (socket[i].flags & IS_CIRRUS)
	    m = cirrus_set_opts(i, buf);
	else if (socket[i].flags & IS_VADEM)
	    m = vg46x_set_opts(i, buf);
	set_bridge_state(i);
	printk(KERN_INFO "    host opts [%d]:%s\n", i,
	       (*buf) ? buf : " none");
    }
    return m;
}

/*======================================================================

    Interrupt testing code, for ISA and PCI interrupts
    
======================================================================*/

static volatile u_int irq_hits;
static u_short irq_sock;

static irqreturn_t i365_count_irq(int irq, void *dev)
{
    i365_get(irq_sock, I365_CSC);
    irq_hits++;
    debug(2, "-> hit on irq %d\n", irq);
    return IRQ_HANDLED;
}

static u_int __init test_irq(u_short sock, int irq)
{
    debug(2, "  testing ISA irq %d\n", irq);
    if (request_irq(irq, i365_count_irq, IRQF_PROBE_SHARED, "scan",
			i365_count_irq) != 0)
	return 1;
    irq_hits = 0; irq_sock = sock;
    msleep(10);
    if (irq_hits) {
	free_irq(irq, i365_count_irq);
	debug(2, "    spurious hit!\n");
	return 1;
    }

    /* Generate one interrupt */
    i365_set(sock, I365_CSCINT, I365_CSC_DETECT | (irq << 4));
    i365_bset(sock, I365_GENCTL, I365_CTL_SW_IRQ);
    udelay(1000);

    free_irq(irq, i365_count_irq);

    /* mask all interrupts */
    i365_set(sock, I365_CSCINT, 0);
    debug(2, "    hits = %d\n", irq_hits);
    
    return (irq_hits != 1);
}

static u_int __init isa_scan(u_short sock, u_int mask0)
{
    u_int mask1 = 0;
    int i;

#ifdef __alpha__
#define PIC 0x4d0
    /* Don't probe level-triggered interrupts -- reserved for PCI */
    mask0 &= ~(inb(PIC) | (inb(PIC+1) << 8));
#endif
    
    if (do_scan) {
	set_bridge_state(sock);
	i365_set(sock, I365_CSCINT, 0);
	for (i = 0; i < 16; i++)
	    if ((mask0 & (1 << i)) && (test_irq(sock, i) == 0))
		mask1 |= (1 << i);
	for (i = 0; i < 16; i++)
	    if ((mask1 & (1 << i)) && (test_irq(sock, i) != 0))
		mask1 ^= (1 << i);
    }
    
    printk(KERN_INFO "    ISA irqs (");
    if (mask1) {
	printk("scanned");
    } else {
	/* Fallback: just find interrupts that aren't in use */
	for (i = 0; i < 16; i++)
	    if ((mask0 & (1 << i)) && (_check_irq(i, IRQF_PROBE_SHARED) == 0))
		mask1 |= (1 << i);
	printk("default");
	/* If scan failed, default to polled status */
	if (!cs_irq && (poll_interval == 0)) poll_interval = HZ;
    }
    printk(") = ");
    
    for (i = 0; i < 16; i++)
	if (mask1 & (1<<i))
	    printk("%s%d", ((mask1 & ((1<<i)-1)) ? "," : ""), i);
    if (mask1 == 0) printk("none!");
    
    return mask1;
}

/*====================================================================*/

/* Time conversion functions */

static int to_cycles(int ns)
{
    return ns/cycle_time;
}

/*====================================================================*/

static int __init identify(kio_addr_t port, u_short sock)
{
    u_char val;
    int type = -1;

    /* Use the next free entry in the socket table */
    socket[sockets].ioaddr = port;
    socket[sockets].psock = sock;
    
    /* Wake up a sleepy Cirrus controller */
    if (wakeup) {
	i365_bclr(sockets, PD67_MISC_CTL_2, PD67_MC2_SUSPEND);
	/* Pause at least 50 ms */
	mdelay(50);
    }
    
    if ((val = i365_get(sockets, I365_IDENT)) & 0x70)
	return -1;
    switch (val) {
    case 0x82:
	type = IS_I82365A; break;
    case 0x83:
	type = IS_I82365B; break;
    case 0x84:
	type = IS_I82365DF; break;
    case 0x88: case 0x89: case 0x8a:
	type = IS_IBM; break;
    }
    
    /* Check for Vadem VG-468 chips */
    outb(0x0e, port);
    outb(0x37, port);
    i365_bset(sockets, VG468_MISC, VG468_MISC_VADEMREV);
    val = i365_get(sockets, I365_IDENT);
    if (val & I365_IDENT_VADEM) {
	i365_bclr(sockets, VG468_MISC, VG468_MISC_VADEMREV);
	type = ((val & 7) >= 4) ? IS_VG469 : IS_VG468;
    }

    /* Check for Ricoh chips */
    val = i365_get(sockets, RF5C_CHIP_ID);
    if ((val == RF5C_CHIP_RF5C296) || (val == RF5C_CHIP_RF5C396))
	type = IS_RF5Cx96;
    
    /* Check for Cirrus CL-PD67xx chips */
    i365_set(sockets, PD67_CHIP_INFO, 0);
    val = i365_get(sockets, PD67_CHIP_INFO);
    if ((val & PD67_INFO_CHIP_ID) == PD67_INFO_CHIP_ID) {
	val = i365_get(sockets, PD67_CHIP_INFO);
	if ((val & PD67_INFO_CHIP_ID) == 0) {
	    type = (val & PD67_INFO_SLOTS) ? IS_PD672X : IS_PD6710;
	    i365_set(sockets, PD67_EXT_INDEX, 0xe5);
	    if (i365_get(sockets, PD67_EXT_INDEX) != 0xe5)
		type = IS_VT83C469;
	}
    }
    return type;
} /* identify */

/*======================================================================

    See if a card is present, powered up, in IO mode, and already
    bound to a (non PC Card) Linux driver.  We leave these alone.

    We make an exception for cards that seem to be serial devices.
    
======================================================================*/

static int __init is_alive(u_short sock)
{
    u_char stat;
    kio_addr_t start, stop;
    
    stat = i365_get(sock, I365_STATUS);
    start = i365_get_pair(sock, I365_IO(0)+I365_W_START);
    stop = i365_get_pair(sock, I365_IO(0)+I365_W_STOP);
    if ((stat & I365_CS_DETECT) && (stat & I365_CS_POWERON) &&
	(i365_get(sock, I365_INTCTL) & I365_PC_IOCARD) &&
	(i365_get(sock, I365_ADDRWIN) & I365_ENA_IO(0)) &&
	((start & 0xfeef) != 0x02e8)) {
	if (!request_region(start, stop-start+1, "i82365"))
	    return 1;
	release_region(start, stop-start+1);
    }

    return 0;
}

/*====================================================================*/

static void __init add_socket(kio_addr_t port, int psock, int type)
{
    socket[sockets].ioaddr = port;
    socket[sockets].psock = psock;
    socket[sockets].type = type;
    socket[sockets].flags = pcic[type].flags;
    if (is_alive(sockets))
	socket[sockets].flags |= IS_ALIVE;
    sockets++;
}

static void __init add_pcic(int ns, int type)
{
    u_int mask = 0, i, base;
    int isa_irq = 0;
    struct i82365_socket *t = &socket[sockets-ns];

    base = sockets-ns;
    if (base == 0) printk("\n");
    printk(KERN_INFO "  %s", pcic[type].name);
    printk(" ISA-to-PCMCIA at port %#lx ofs 0x%02x",
	       t->ioaddr, t->psock*0x40);
    printk(", %d socket%s\n", ns, ((ns > 1) ? "s" : ""));

    /* Set host options, build basic interrupt mask */
    if (irq_list_count == 0)
	mask = irq_mask;
    else
	for (i = mask = 0; i < irq_list_count; i++)
	    mask |= (1<<irq_list[i]);
    mask &= I365_MASK & set_bridge_opts(base, ns);
    /* Scan for ISA interrupts */
    mask = isa_scan(base, mask);
        
    /* Poll if only two interrupts available */
    if (!poll_interval) {
	u_int tmp = (mask & 0xff20);
	tmp = tmp & (tmp-1);
	if ((tmp & (tmp-1)) == 0)
	    poll_interval = HZ;
    }
    /* Only try an ISA cs_irq if this is the first controller */
    if (!grab_irq && (cs_irq || !poll_interval)) {
	/* Avoid irq 12 unless it is explicitly requested */
	u_int cs_mask = mask & ((cs_irq) ? (1<<cs_irq) : ~(1<<12));
	for (cs_irq = 15; cs_irq > 0; cs_irq--)
	    if ((cs_mask & (1 << cs_irq)) &&
		(_check_irq(cs_irq, IRQF_PROBE_SHARED) == 0))
		break;
	if (cs_irq) {
	    grab_irq = 1;
	    isa_irq = cs_irq;
	    printk(" status change on irq %d\n", cs_irq);
	}
    }
    
    if (!isa_irq) {
	if (poll_interval == 0)
	    poll_interval = HZ;
	printk(" polling interval = %d ms\n",
	       poll_interval * 1000 / HZ);
	
    }
    
    /* Update socket interrupt information, capabilities */
    for (i = 0; i < ns; i++) {
	t[i].socket.features |= SS_CAP_PCCARD;
	t[i].socket.map_size = 0x1000;
	t[i].socket.irq_mask = mask;
	t[i].cs_irq = isa_irq;
    }

} /* add_pcic */

/*====================================================================*/

#ifdef CONFIG_PNP
static struct isapnp_device_id id_table[] __initdata = {
	{ 	ISAPNP_ANY_ID, ISAPNP_ANY_ID, ISAPNP_VENDOR('P', 'N', 'P'),
		ISAPNP_FUNCTION(0x0e00), (unsigned long) "Intel 82365-Compatible" },
	{ 	ISAPNP_ANY_ID, ISAPNP_ANY_ID, ISAPNP_VENDOR('P', 'N', 'P'),
		ISAPNP_FUNCTION(0x0e01), (unsigned long) "Cirrus Logic CL-PD6720" },
	{ 	ISAPNP_ANY_ID, ISAPNP_ANY_ID, ISAPNP_VENDOR('P', 'N', 'P'),
		ISAPNP_FUNCTION(0x0e02), (unsigned long) "VLSI VL82C146" },
	{	0 }
};
MODULE_DEVICE_TABLE(isapnp, id_table);

static struct pnp_dev *i82365_pnpdev;
#endif

static void __init isa_probe(void)
{
    int i, j, sock, k, ns, id;
    kio_addr_t port;
#ifdef CONFIG_PNP
    struct isapnp_device_id *devid;
    struct pnp_dev *dev;

    for (devid = id_table; devid->vendor; devid++) {
	if ((dev = pnp_find_dev(NULL, devid->vendor, devid->function, NULL))) {
	
	    if (pnp_device_attach(dev) < 0)
	    	continue;

	    if (pnp_activate_dev(dev) < 0) {
		printk("activate failed\n");
		pnp_device_detach(dev);
		break;
	    }

	    if (!pnp_port_valid(dev, 0)) {
		printk("invalid resources ?\n");
		pnp_device_detach(dev);
		break;
	    }
	    i365_base = pnp_port_start(dev, 0);
	    i82365_pnpdev = dev;
	    break;
	}
    }
#endif

    if (!request_region(i365_base, 2, "i82365")) {
	if (sockets == 0)
	    printk("port conflict at %#lx\n", i365_base);
	return;
    }

    id = identify(i365_base, 0);
    if ((id == IS_I82365DF) && (identify(i365_base, 1) != id)) {
	for (i = 0; i < 4; i++) {
	    if (i == ignore) continue;
	    port = i365_base + ((i & 1) << 2) + ((i & 2) << 1);
	    sock = (i & 1) << 1;
	    if (identify(port, sock) == IS_I82365DF) {
		add_socket(port, sock, IS_VLSI);
		add_pcic(1, IS_VLSI);
	    }
	}
    } else {
	for (i = 0; i < 8; i += 2) {
	    if (sockets && !extra_sockets && (i == 4))
		break;
	    port = i365_base + 2*(i>>2);
	    sock = (i & 3);
	    id = identify(port, sock);
	    if (id < 0) continue;

	    for (j = ns = 0; j < 2; j++) {
		/* Does the socket exist? */
		if ((ignore == i+j) || (identify(port, sock+j) < 0))
		    continue;
		/* Check for bad socket decode */
		for (k = 0; k <= sockets; k++)
		    i365_set(k, I365_MEM(0)+I365_W_OFF, k);
		for (k = 0; k <= sockets; k++)
		    if (i365_get(k, I365_MEM(0)+I365_W_OFF) != k)
			break;
		if (k <= sockets) break;
		add_socket(port, sock+j, id); ns++;
	    }
	    if (ns != 0) add_pcic(ns, id);
	}
    }
}

/*====================================================================*/

static irqreturn_t pcic_interrupt(int irq, void *dev)
{
    int i, j, csc;
    u_int events, active;
    u_long flags = 0;
    int handled = 0;

    debug(4, "pcic_interrupt(%d)\n", irq);

    for (j = 0; j < 20; j++) {
	active = 0;
	for (i = 0; i < sockets; i++) {
	    if (socket[i].cs_irq != irq)
		continue;
	    handled = 1;
	    ISA_LOCK(i, flags);
	    csc = i365_get(i, I365_CSC);
	    if ((csc == 0) || (i365_get(i, I365_IDENT) & 0x70)) {
		ISA_UNLOCK(i, flags);
		continue;
	    }
	    events = (csc & I365_CSC_DETECT) ? SS_DETECT : 0;

	    if (i365_get(i, I365_INTCTL) & I365_PC_IOCARD)
		events |= (csc & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	    else {
		events |= (csc & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		events |= (csc & I365_CSC_BVD2) ? SS_BATWARN : 0;
		events |= (csc & I365_CSC_READY) ? SS_READY : 0;
	    }
	    ISA_UNLOCK(i, flags);
	    debug(2, "socket %d event 0x%02x\n", i, events);

	    if (events)
		pcmcia_parse_events(&socket[i].socket, events);

	    active |= events;
	}
	if (!active) break;
    }
    if (j == 20)
	printk(KERN_NOTICE "i82365: infinite loop in interrupt handler\n");

    debug(4, "interrupt done\n");
    return IRQ_RETVAL(handled);
} /* pcic_interrupt */

static void pcic_interrupt_wrapper(u_long data)
{
    pcic_interrupt(0, NULL);
    poll_timer.expires = jiffies + poll_interval;
    add_timer(&poll_timer);
}

/*====================================================================*/

static int i365_get_status(u_short sock, u_int *value)
{
    u_int status;
    
    status = i365_get(sock, I365_STATUS);
    *value = ((status & I365_CS_DETECT) == I365_CS_DETECT)
	? SS_DETECT : 0;
	
    if (i365_get(sock, I365_INTCTL) & I365_PC_IOCARD)
	*value |= (status & I365_CS_STSCHG) ? 0 : SS_STSCHG;
    else {
	*value |= (status & I365_CS_BVD1) ? 0 : SS_BATDEAD;
	*value |= (status & I365_CS_BVD2) ? 0 : SS_BATWARN;
    }
    *value |= (status & I365_CS_WRPROT) ? SS_WRPROT : 0;
    *value |= (status & I365_CS_READY) ? SS_READY : 0;
    *value |= (status & I365_CS_POWERON) ? SS_POWERON : 0;

    if (socket[sock].type == IS_VG469) {
	status = i365_get(sock, VG469_VSENSE);
	if (socket[sock].psock & 1) {
	    *value |= (status & VG469_VSENSE_B_VS1) ? 0 : SS_3VCARD;
	    *value |= (status & VG469_VSENSE_B_VS2) ? 0 : SS_XVCARD;
	} else {
	    *value |= (status & VG469_VSENSE_A_VS1) ? 0 : SS_3VCARD;
	    *value |= (status & VG469_VSENSE_A_VS2) ? 0 : SS_XVCARD;
	}
    }
    
    debug(1, "GetStatus(%d) = %#4.4x\n", sock, *value);
    return 0;
} /* i365_get_status */

/*====================================================================*/

static int i365_set_socket(u_short sock, socket_state_t *state)
{
    struct i82365_socket *t = &socket[sock];
    u_char reg;
    
    debug(1, "SetSocket(%d, flags %#3.3x, Vcc %d, Vpp %d, "
	  "io_irq %d, csc_mask %#2.2x)\n", sock, state->flags,
	  state->Vcc, state->Vpp, state->io_irq, state->csc_mask);
    
    /* First set global controller options */
    set_bridge_state(sock);
    
    /* IO card, RESET flag, IO interrupt */
    reg = t->intr;
    reg |= state->io_irq;
    reg |= (state->flags & SS_RESET) ? 0 : I365_PC_RESET;
    reg |= (state->flags & SS_IOCARD) ? I365_PC_IOCARD : 0;
    i365_set(sock, I365_INTCTL, reg);
    
    reg = I365_PWR_NORESET;
    if (state->flags & SS_PWR_AUTO) reg |= I365_PWR_AUTO;
    if (state->flags & SS_OUTPUT_ENA) reg |= I365_PWR_OUT;

    if (t->flags & IS_CIRRUS) {
	if (state->Vpp != 0) {
	    if (state->Vpp == 120)
		reg |= I365_VPP1_12V;
	    else if (state->Vpp == state->Vcc)
		reg |= I365_VPP1_5V;
	    else return -EINVAL;
	}
	if (state->Vcc != 0) {
	    reg |= I365_VCC_5V;
	    if (state->Vcc == 33)
		i365_bset(sock, PD67_MISC_CTL_1, PD67_MC1_VCC_3V);
	    else if (state->Vcc == 50)
		i365_bclr(sock, PD67_MISC_CTL_1, PD67_MC1_VCC_3V);
	    else return -EINVAL;
	}
    } else if (t->flags & IS_VG_PWR) {
	if (state->Vpp != 0) {
	    if (state->Vpp == 120)
		reg |= I365_VPP1_12V;
	    else if (state->Vpp == state->Vcc)
		reg |= I365_VPP1_5V;
	    else return -EINVAL;
	}
	if (state->Vcc != 0) {
	    reg |= I365_VCC_5V;
	    if (state->Vcc == 33)
		i365_bset(sock, VG469_VSELECT, VG469_VSEL_VCC);
	    else if (state->Vcc == 50)
		i365_bclr(sock, VG469_VSELECT, VG469_VSEL_VCC);
	    else return -EINVAL;
	}
    } else if (t->flags & IS_DF_PWR) {
	switch (state->Vcc) {
	case 0:		break;
	case 33:   	reg |= I365_VCC_3V; break;
	case 50:	reg |= I365_VCC_5V; break;
	default:	return -EINVAL;
	}
	switch (state->Vpp) {
	case 0:		break;
	case 50:   	reg |= I365_VPP1_5V; break;
	case 120:	reg |= I365_VPP1_12V; break;
	default:	return -EINVAL;
	}
    } else {
	switch (state->Vcc) {
	case 0:		break;
	case 50:	reg |= I365_VCC_5V; break;
	default:	return -EINVAL;
	}
	switch (state->Vpp) {
	case 0:		break;
	case 50:	reg |= I365_VPP1_5V | I365_VPP2_5V; break;
	case 120:	reg |= I365_VPP1_12V | I365_VPP2_12V; break;
	default:	return -EINVAL;
	}
    }
    
    if (reg != i365_get(sock, I365_POWER))
	i365_set(sock, I365_POWER, reg);

    /* Chipset-specific functions */
    if (t->flags & IS_CIRRUS) {
	/* Speaker control */
	i365_bflip(sock, PD67_MISC_CTL_1, PD67_MC1_SPKR_ENA,
		   state->flags & SS_SPKR_ENA);
    }
    
    /* Card status change interrupt mask */
    reg = t->cs_irq << 4;
    if (state->csc_mask & SS_DETECT) reg |= I365_CSC_DETECT;
    if (state->flags & SS_IOCARD) {
	if (state->csc_mask & SS_STSCHG) reg |= I365_CSC_STSCHG;
    } else {
	if (state->csc_mask & SS_BATDEAD) reg |= I365_CSC_BVD1;
	if (state->csc_mask & SS_BATWARN) reg |= I365_CSC_BVD2;
	if (state->csc_mask & SS_READY) reg |= I365_CSC_READY;
    }
    i365_set(sock, I365_CSCINT, reg);
    i365_get(sock, I365_CSC);
    
    return 0;
} /* i365_set_socket */

/*====================================================================*/

static int i365_set_io_map(u_short sock, struct pccard_io_map *io)
{
    u_char map, ioctl;
    
    debug(1, "SetIOMap(%d, %d, %#2.2x, %d ns, "
	  "%#lx-%#lx)\n", sock, io->map, io->flags,
	  io->speed, io->start, io->stop);
    map = io->map;
    if ((map > 1) || (io->start > 0xffff) || (io->stop > 0xffff) ||
	(io->stop < io->start)) return -EINVAL;
    /* Turn off the window before changing anything */
    if (i365_get(sock, I365_ADDRWIN) & I365_ENA_IO(map))
	i365_bclr(sock, I365_ADDRWIN, I365_ENA_IO(map));
    i365_set_pair(sock, I365_IO(map)+I365_W_START, io->start);
    i365_set_pair(sock, I365_IO(map)+I365_W_STOP, io->stop);
    ioctl = i365_get(sock, I365_IOCTL) & ~I365_IOCTL_MASK(map);
    if (io->speed) ioctl |= I365_IOCTL_WAIT(map);
    if (io->flags & MAP_0WS) ioctl |= I365_IOCTL_0WS(map);
    if (io->flags & MAP_16BIT) ioctl |= I365_IOCTL_16BIT(map);
    if (io->flags & MAP_AUTOSZ) ioctl |= I365_IOCTL_IOCS16(map);
    i365_set(sock, I365_IOCTL, ioctl);
    /* Turn on the window if necessary */
    if (io->flags & MAP_ACTIVE)
	i365_bset(sock, I365_ADDRWIN, I365_ENA_IO(map));
    return 0;
} /* i365_set_io_map */

/*====================================================================*/

static int i365_set_mem_map(u_short sock, struct pccard_mem_map *mem)
{
    u_short base, i;
    u_char map;
    
    debug(1, "SetMemMap(%d, %d, %#2.2x, %d ns, %#llx-%#llx, "
	  "%#x)\n", sock, mem->map, mem->flags, mem->speed,
	  (unsigned long long)mem->res->start,
	  (unsigned long long)mem->res->end, mem->card_start);

    map = mem->map;
    if ((map > 4) || (mem->card_start > 0x3ffffff) ||
	(mem->res->start > mem->res->end) || (mem->speed > 1000))
	return -EINVAL;
    if ((mem->res->start > 0xffffff) || (mem->res->end > 0xffffff))
	return -EINVAL;
	
    /* Turn off the window before changing anything */
    if (i365_get(sock, I365_ADDRWIN) & I365_ENA_MEM(map))
	i365_bclr(sock, I365_ADDRWIN, I365_ENA_MEM(map));
    
    base = I365_MEM(map);
    i = (mem->res->start >> 12) & 0x0fff;
    if (mem->flags & MAP_16BIT) i |= I365_MEM_16BIT;
    if (mem->flags & MAP_0WS) i |= I365_MEM_0WS;
    i365_set_pair(sock, base+I365_W_START, i);
    
    i = (mem->res->end >> 12) & 0x0fff;
    switch (to_cycles(mem->speed)) {
    case 0:	break;
    case 1:	i |= I365_MEM_WS0; break;
    case 2:	i |= I365_MEM_WS1; break;
    default:	i |= I365_MEM_WS1 | I365_MEM_WS0; break;
    }
    i365_set_pair(sock, base+I365_W_STOP, i);
    
    i = ((mem->card_start - mem->res->start) >> 12) & 0x3fff;
    if (mem->flags & MAP_WRPROT) i |= I365_MEM_WRPROT;
    if (mem->flags & MAP_ATTRIB) i |= I365_MEM_REG;
    i365_set_pair(sock, base+I365_W_OFF, i);
    
    /* Turn on the window if necessary */
    if (mem->flags & MAP_ACTIVE)
	i365_bset(sock, I365_ADDRWIN, I365_ENA_MEM(map));
    return 0;
} /* i365_set_mem_map */

#if 0 /* driver model ordering issue */
/*======================================================================

    Routines for accessing socket information and register dumps via
    /sys/class/pcmcia_socket/...
    
======================================================================*/

static ssize_t show_info(struct class_device *class_dev, char *buf)
{
	struct i82365_socket *s = container_of(class_dev, struct i82365_socket, socket.dev);
	return sprintf(buf, "type:     %s\npsock:    %d\n",
		       pcic[s->type].name, s->psock);
}

static ssize_t show_exca(struct class_device *class_dev, char *buf)
{
	struct i82365_socket *s = container_of(class_dev, struct i82365_socket, socket.dev);
	unsigned short sock;
	int i;
	ssize_t ret = 0;
	unsigned long flags = 0;

	sock = s->number;

	ISA_LOCK(sock, flags);
	for (i = 0; i < 0x40; i += 4) {
		ret += sprintf(buf, "%02x %02x %02x %02x%s",
			       i365_get(sock,i), i365_get(sock,i+1),
			       i365_get(sock,i+2), i365_get(sock,i+3),
			       ((i % 16) == 12) ? "\n" : " ");
		buf += ret;
	}
	ISA_UNLOCK(sock, flags);

	return ret;
}

static CLASS_DEVICE_ATTR(exca, S_IRUGO, show_exca, NULL);
static CLASS_DEVICE_ATTR(info, S_IRUGO, show_info, NULL);
#endif

/*====================================================================*/

/* this is horribly ugly... proper locking needs to be done here at 
 * some time... */
#define LOCKED(x) do { \
	int retval; \
	unsigned long flags; \
	spin_lock_irqsave(&isa_lock, flags); \
	retval = x; \
	spin_unlock_irqrestore(&isa_lock, flags); \
	return retval; \
} while (0)
	

static int pcic_get_status(struct pcmcia_socket *s, u_int *value)
{
	unsigned int sock = container_of(s, struct i82365_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE) {
		*value = 0;
		return -EINVAL;
	}

	LOCKED(i365_get_status(sock, value));
}

static int pcic_set_socket(struct pcmcia_socket *s, socket_state_t *state)
{
	unsigned int sock = container_of(s, struct i82365_socket, socket)->number;

	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;

	LOCKED(i365_set_socket(sock, state));
}

static int pcic_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
	unsigned int sock = container_of(s, struct i82365_socket, socket)->number;
	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;

	LOCKED(i365_set_io_map(sock, io));
}

static int pcic_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *mem)
{
	unsigned int sock = container_of(s, struct i82365_socket, socket)->number;
	if (socket[sock].flags & IS_ALIVE)
		return -EINVAL;

	LOCKED(i365_set_mem_map(sock, mem));
}

static int pcic_init(struct pcmcia_socket *s)
{
	int i;
	struct resource res = { .start = 0, .end = 0x1000 };
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { .res = &res, };

	for (i = 0; i < 2; i++) {
		io.map = i;
		pcic_set_io_map(s, &io);
	}
	for (i = 0; i < 5; i++) {
		mem.map = i;
		pcic_set_mem_map(s, &mem);
	}
	return 0;
}

static struct pccard_operations pcic_operations = {
	.init			= pcic_init,
	.get_status		= pcic_get_status,
	.set_socket		= pcic_set_socket,
	.set_io_map		= pcic_set_io_map,
	.set_mem_map		= pcic_set_mem_map,
};

/*====================================================================*/

static struct device_driver i82365_driver = {
	.name = "i82365",
	.bus = &platform_bus_type,
	.suspend = pcmcia_socket_dev_suspend,
	.resume = pcmcia_socket_dev_resume,
};

static struct platform_device *i82365_device;

static int __init init_i82365(void)
{
    int i, ret;

    ret = driver_register(&i82365_driver);
    if (ret)
	return ret;

    i82365_device = platform_device_alloc("i82365", 0);
    if (i82365_device) {
	    ret = platform_device_add(i82365_device);
	    if (ret)
		    platform_device_put(i82365_device);
    } else
	    ret = -ENOMEM;

    if (ret) {
	driver_unregister(&i82365_driver);
	return ret;
    }

    printk(KERN_INFO "Intel ISA PCIC probe: ");
    sockets = 0;

    isa_probe();

    if (sockets == 0) {
	printk("not found.\n");
	platform_device_unregister(i82365_device);
	release_region(i365_base, 2);
	driver_unregister(&i82365_driver);
	return -ENODEV;
    }

    /* Set up interrupt handler(s) */
    if (grab_irq != 0)
	request_irq(cs_irq, pcic_interrupt, 0, "i82365", pcic_interrupt);
    
    /* register sockets with the pcmcia core */
    for (i = 0; i < sockets; i++) {
	    socket[i].socket.dev.dev = &i82365_device->dev;
	    socket[i].socket.ops = &pcic_operations;
	    socket[i].socket.resource_ops = &pccard_nonstatic_ops;
	    socket[i].socket.owner = THIS_MODULE;
	    socket[i].number = i;
	    ret = pcmcia_register_socket(&socket[i].socket);
	    if (!ret)
		    socket[i].flags |= IS_REGISTERED;

#if 0 /* driver model ordering issue */
	   class_device_create_file(&socket[i].socket.dev,
			   	    &class_device_attr_info);
	   class_device_create_file(&socket[i].socket.dev,
			   	    &class_device_attr_exca);
#endif
    }

    /* Finally, schedule a polling interrupt */
    if (poll_interval != 0) {
	poll_timer.function = pcic_interrupt_wrapper;
	poll_timer.data = 0;
	init_timer(&poll_timer);
    	poll_timer.expires = jiffies + poll_interval;
	add_timer(&poll_timer);
    }
    
    return 0;
    
} /* init_i82365 */

static void __exit exit_i82365(void)
{
    int i;

    for (i = 0; i < sockets; i++) {
	    if (socket[i].flags & IS_REGISTERED)
		    pcmcia_unregister_socket(&socket[i].socket);
    }
    platform_device_unregister(i82365_device);
    if (poll_interval != 0)
	del_timer_sync(&poll_timer);
    if (grab_irq != 0)
	free_irq(cs_irq, pcic_interrupt);
    for (i = 0; i < sockets; i++) {
	/* Turn off all interrupt sources! */
	i365_set(i, I365_CSCINT, 0);
	release_region(socket[i].ioaddr, 2);
    }
    release_region(i365_base, 2);
#ifdef CONFIG_PNP
    if (i82365_pnpdev)
    		pnp_disable_dev(i82365_pnpdev);
#endif
    driver_unregister(&i82365_driver);
} /* exit_i82365 */

module_init(init_i82365);
module_exit(exit_i82365);
MODULE_LICENSE("Dual MPL/GPL");
/*====================================================================*/
