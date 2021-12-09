// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for the PMU in Apple PowerBooks and PowerMacs.
 *
 * The VIA (versatile interface adapter) interfaces to the PMU,
 * a 6805 microprocessor core whose primary function is to control
 * battery charging and system power on the PowerBook 3400 and 2400.
 * The PMU also controls the ADB (Apple Desktop Bus) which connects
 * to the keyboard and mouse, as well as the non-volatile RAM
 * and the RTC (real time clock) chip.
 *
 * Copyright (C) 1998 Paul Mackerras and Fabio Riccardi.
 * Copyright (C) 2001-2002 Benjamin Herrenschmidt
 * Copyright (C) 2006-2007 Johannes Berg
 *
 * THIS DRIVER IS BECOMING A TOTAL MESS !
 *  - Cleanup atomically disabling reply to PMU events after
 *    a sleep or a freq. switch
 *
 */
#include <linux/stdarg.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/miscdevice.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/freezer.h>
#include <linux/syscalls.h>
#include <linux/suspend.h>
#include <linux/cpu.h>
#include <linux/compat.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/irq.h>
#ifdef CONFIG_PPC_PMAC
#include <asm/pmac_feature.h>
#include <asm/pmac_pfunc.h>
#include <asm/pmac_low_i2c.h>
#include <asm/prom.h>
#include <asm/mmu_context.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/backlight.h>
#else
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>
#endif

#include "via-pmu-event.h"

/* Some compile options */
#undef DEBUG_SLEEP

/* How many iterations between battery polls */
#define BATTERY_POLLING_COUNT	2

static DEFINE_MUTEX(pmu_info_proc_mutex);

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

/* Bits in B data register: both active low */
#ifdef CONFIG_PPC_PMAC
#define TACK		0x08		/* Transfer acknowledge (input) */
#define TREQ		0x10		/* Transfer request (output) */
#else
#define TACK		0x02
#define TREQ		0x04
#endif

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define CB2_INT		0x08
#define CB1_INT		0x10		/* transition on CB1 input */

static volatile enum pmu_state {
	uninitialized = 0,
	idle,
	sending,
	intack,
	reading,
	reading_intr,
	locked,
} pmu_state;

static volatile enum int_data_state {
	int_data_empty,
	int_data_fill,
	int_data_ready,
	int_data_flush
} int_data_state[2] = { int_data_empty, int_data_empty };

static struct adb_request *current_req;
static struct adb_request *last_req;
static struct adb_request *req_awaiting_reply;
static unsigned char interrupt_data[2][32];
static int interrupt_data_len[2];
static int int_data_last;
static unsigned char *reply_ptr;
static int data_index;
static int data_len;
static volatile int adb_int_pending;
static volatile int disable_poll;
static int pmu_kind = PMU_UNKNOWN;
static int pmu_fully_inited;
static int pmu_has_adb;
#ifdef CONFIG_PPC_PMAC
static volatile unsigned char __iomem *via1;
static volatile unsigned char __iomem *via2;
static struct device_node *vias;
static struct device_node *gpio_node;
#endif
static unsigned char __iomem *gpio_reg;
static int gpio_irq = 0;
static int gpio_irq_enabled = -1;
static volatile int pmu_suspended;
static spinlock_t pmu_lock;
static u8 pmu_intr_mask;
static int pmu_version;
static int drop_interrupts;
#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
static int option_lid_wakeup = 1;
#endif /* CONFIG_SUSPEND && CONFIG_PPC32 */
static unsigned long async_req_locks;

#define NUM_IRQ_STATS 13
static unsigned int pmu_irq_stats[NUM_IRQ_STATS];

static struct proc_dir_entry *proc_pmu_root;
static struct proc_dir_entry *proc_pmu_info;
static struct proc_dir_entry *proc_pmu_irqstats;
static struct proc_dir_entry *proc_pmu_options;
static int option_server_mode;

int pmu_battery_count;
static int pmu_cur_battery;
unsigned int pmu_power_flags = PMU_PWR_AC_PRESENT;
struct pmu_battery_info pmu_batteries[PMU_MAX_BATTERIES];
static int query_batt_timer = BATTERY_POLLING_COUNT;
static struct adb_request batt_req;
static struct proc_dir_entry *proc_pmu_batt[PMU_MAX_BATTERIES];

int asleep;

#ifdef CONFIG_ADB
static int adb_dev_map;
static int pmu_adb_flags;

static int pmu_probe(void);
static int pmu_init(void);
static int pmu_send_request(struct adb_request *req, int sync);
static int pmu_adb_autopoll(int devs);
static int pmu_adb_reset_bus(void);
#endif /* CONFIG_ADB */

static int init_pmu(void);
static void pmu_start(void);
static irqreturn_t via_pmu_interrupt(int irq, void *arg);
static irqreturn_t gpio1_interrupt(int irq, void *arg);
static int pmu_info_proc_show(struct seq_file *m, void *v);
static int pmu_irqstats_proc_show(struct seq_file *m, void *v);
static int pmu_battery_proc_show(struct seq_file *m, void *v);
static void pmu_pass_intr(unsigned char *data, int len);
static const struct proc_ops pmu_options_proc_ops;

#ifdef CONFIG_ADB
const struct adb_driver via_pmu_driver = {
	.name         = "PMU",
	.probe        = pmu_probe,
	.init         = pmu_init,
	.send_request = pmu_send_request,
	.autopoll     = pmu_adb_autopoll,
	.poll         = pmu_poll_adb,
	.reset_bus    = pmu_adb_reset_bus,
};
#endif /* CONFIG_ADB */

extern void low_sleep_handler(void);
extern void enable_kernel_altivec(void);
extern void enable_kernel_fp(void);

#ifdef DEBUG_SLEEP
int pmu_polled_request(struct adb_request *req);
void pmu_blink(int n);
#endif

/*
 * This table indicates for each PMU opcode:
 * - the number of data bytes to be sent with the command, or -1
 *   if a length byte should be sent,
 * - the number of response bytes which the PMU will return, or
 *   -1 if it will send a length byte.
 */
static const s8 pmu_data_len[256][2] = {
/*	   0	   1	   2	   3	   4	   5	   6	   7  */
/*00*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*08*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*10*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*18*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0, 0},
/*20*/	{-1, 0},{ 0, 0},{ 2, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},
/*28*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0,-1},
/*30*/	{ 4, 0},{20, 0},{-1, 0},{ 3, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*38*/	{ 0, 4},{ 0,20},{ 2,-1},{ 2, 1},{ 3,-1},{-1,-1},{-1,-1},{ 4, 0},
/*40*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*48*/	{ 0, 1},{ 0, 1},{-1,-1},{ 1, 0},{ 1, 0},{-1,-1},{-1,-1},{-1,-1},
/*50*/	{ 1, 0},{ 0, 0},{ 2, 0},{ 2, 0},{-1, 0},{ 1, 0},{ 3, 0},{ 1, 0},
/*58*/	{ 0, 1},{ 1, 0},{ 0, 2},{ 0, 2},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},
/*60*/	{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*68*/	{ 0, 3},{ 0, 3},{ 0, 2},{ 0, 8},{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},
/*70*/	{ 1, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*78*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{ 5, 1},{ 4, 1},{ 4, 1},
/*80*/	{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*88*/	{ 0, 5},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*90*/	{ 1, 0},{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*98*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*a0*/	{ 2, 0},{ 2, 0},{ 2, 0},{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},
/*a8*/	{ 1, 1},{ 1, 0},{ 3, 0},{ 2, 0},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*b0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*b8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*c0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*c8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*d0*/	{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*d8*/	{ 1, 1},{ 1, 1},{-1,-1},{-1,-1},{ 0, 1},{ 0,-1},{-1,-1},{-1,-1},
/*e0*/	{-1, 0},{ 4, 0},{ 0, 1},{-1, 0},{-1, 0},{ 4, 0},{-1, 0},{-1, 0},
/*e8*/	{ 3,-1},{-1,-1},{ 0, 1},{-1,-1},{ 0,-1},{-1,-1},{-1,-1},{ 0, 0},
/*f0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*f8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
};

static char *pbook_type[] = {
	"Unknown PowerBook",
	"PowerBook 2400/3400/3500(G3)",
	"PowerBook G3 Series",
	"1999 PowerBook G3",
	"Core99"
};

int __init find_via_pmu(void)
{
#ifdef CONFIG_PPC_PMAC
	u64 taddr;
	const u32 *reg;

	if (pmu_state != uninitialized)
		return 1;
	vias = of_find_node_by_name(NULL, "via-pmu");
	if (vias == NULL)
		return 0;

	reg = of_get_property(vias, "reg", NULL);
	if (reg == NULL) {
		printk(KERN_ERR "via-pmu: No \"reg\" property !\n");
		goto fail;
	}
	taddr = of_translate_address(vias, reg);
	if (taddr == OF_BAD_ADDR) {
		printk(KERN_ERR "via-pmu: Can't translate address !\n");
		goto fail;
	}

	spin_lock_init(&pmu_lock);

	pmu_has_adb = 1;

	pmu_intr_mask =	PMU_INT_PCEJECT |
			PMU_INT_SNDBRT |
			PMU_INT_ADB |
			PMU_INT_TICK;
	
	if (of_node_name_eq(vias->parent, "ohare") ||
	    of_device_is_compatible(vias->parent, "ohare"))
		pmu_kind = PMU_OHARE_BASED;
	else if (of_device_is_compatible(vias->parent, "paddington"))
		pmu_kind = PMU_PADDINGTON_BASED;
	else if (of_device_is_compatible(vias->parent, "heathrow"))
		pmu_kind = PMU_HEATHROW_BASED;
	else if (of_device_is_compatible(vias->parent, "Keylargo")
		 || of_device_is_compatible(vias->parent, "K2-Keylargo")) {
		struct device_node *gpiop;
		struct device_node *adbp;
		u64 gaddr = OF_BAD_ADDR;

		pmu_kind = PMU_KEYLARGO_BASED;
		adbp = of_find_node_by_type(NULL, "adb");
		pmu_has_adb = (adbp != NULL);
		of_node_put(adbp);
		pmu_intr_mask =	PMU_INT_PCEJECT |
				PMU_INT_SNDBRT |
				PMU_INT_ADB |
				PMU_INT_TICK |
				PMU_INT_ENVIRONMENT;
		
		gpiop = of_find_node_by_name(NULL, "gpio");
		if (gpiop) {
			reg = of_get_property(gpiop, "reg", NULL);
			if (reg)
				gaddr = of_translate_address(gpiop, reg);
			if (gaddr != OF_BAD_ADDR)
				gpio_reg = ioremap(gaddr, 0x10);
			of_node_put(gpiop);
		}
		if (gpio_reg == NULL) {
			printk(KERN_ERR "via-pmu: Can't find GPIO reg !\n");
			goto fail;
		}
	} else
		pmu_kind = PMU_UNKNOWN;

	via1 = via2 = ioremap(taddr, 0x2000);
	if (via1 == NULL) {
		printk(KERN_ERR "via-pmu: Can't map address !\n");
		goto fail_via_remap;
	}
	
	out_8(&via1[IER], IER_CLR | 0x7f);	/* disable all intrs */
	out_8(&via1[IFR], 0x7f);			/* clear IFR */

	pmu_state = idle;

	if (!init_pmu())
		goto fail_init;

	sys_ctrler = SYS_CTRLER_PMU;
	
	return 1;

 fail_init:
	iounmap(via1);
	via1 = via2 = NULL;
 fail_via_remap:
	iounmap(gpio_reg);
	gpio_reg = NULL;
 fail:
	of_node_put(vias);
	vias = NULL;
	pmu_state = uninitialized;
	return 0;
#else
	if (macintosh_config->adb_type != MAC_ADB_PB2)
		return 0;

	pmu_kind = PMU_UNKNOWN;

	spin_lock_init(&pmu_lock);

	pmu_has_adb = 1;

	pmu_intr_mask =	PMU_INT_PCEJECT |
			PMU_INT_SNDBRT |
			PMU_INT_ADB |
			PMU_INT_TICK;

	pmu_state = idle;

	if (!init_pmu()) {
		pmu_state = uninitialized;
		return 0;
	}

	return 1;
#endif /* !CONFIG_PPC_PMAC */
}

#ifdef CONFIG_ADB
static int pmu_probe(void)
{
	return pmu_state == uninitialized ? -ENODEV : 0;
}

static int pmu_init(void)
{
	return pmu_state == uninitialized ? -ENODEV : 0;
}
#endif /* CONFIG_ADB */

/*
 * We can't wait until pmu_init gets called, that happens too late.
 * It happens after IDE and SCSI initialization, which can take a few
 * seconds, and by that time the PMU could have given up on us and
 * turned us off.
 * Thus this is called with arch_initcall rather than device_initcall.
 */
static int __init via_pmu_start(void)
{
	unsigned int __maybe_unused irq;

	if (pmu_state == uninitialized)
		return -ENODEV;

	batt_req.complete = 1;

#ifdef CONFIG_PPC_PMAC
	irq = irq_of_parse_and_map(vias, 0);
	if (!irq) {
		printk(KERN_ERR "via-pmu: can't map interrupt\n");
		return -ENODEV;
	}
	/* We set IRQF_NO_SUSPEND because we don't want the interrupt
	 * to be disabled between the 2 passes of driver suspend, we
	 * control our own disabling for that one
	 */
	if (request_irq(irq, via_pmu_interrupt, IRQF_NO_SUSPEND,
			"VIA-PMU", (void *)0)) {
		printk(KERN_ERR "via-pmu: can't request irq %d\n", irq);
		return -ENODEV;
	}

	if (pmu_kind == PMU_KEYLARGO_BASED) {
		gpio_node = of_find_node_by_name(NULL, "extint-gpio1");
		if (gpio_node == NULL)
			gpio_node = of_find_node_by_name(NULL,
							 "pmu-interrupt");
		if (gpio_node)
			gpio_irq = irq_of_parse_and_map(gpio_node, 0);

		if (gpio_irq) {
			if (request_irq(gpio_irq, gpio1_interrupt,
					IRQF_NO_SUSPEND, "GPIO1 ADB",
					(void *)0))
				printk(KERN_ERR "pmu: can't get irq %d"
				       " (GPIO1)\n", gpio_irq);
			else
				gpio_irq_enabled = 1;
		}
	}

	/* Enable interrupts */
	out_8(&via1[IER], IER_SET | SR_INT | CB1_INT);
#else
	if (request_irq(IRQ_MAC_ADB_SR, via_pmu_interrupt, IRQF_NO_SUSPEND,
			"VIA-PMU-SR", NULL)) {
		pr_err("%s: couldn't get SR irq\n", __func__);
		return -ENODEV;
	}
	if (request_irq(IRQ_MAC_ADB_CL, via_pmu_interrupt, IRQF_NO_SUSPEND,
			"VIA-PMU-CL", NULL)) {
		pr_err("%s: couldn't get CL irq\n", __func__);
		free_irq(IRQ_MAC_ADB_SR, NULL);
		return -ENODEV;
	}
#endif /* !CONFIG_PPC_PMAC */

	pmu_fully_inited = 1;

	/* Make sure PMU settle down before continuing. This is _very_ important
	 * since the IDE probe may shut interrupts down for quite a bit of time. If
	 * a PMU communication is pending while this happens, the PMU may timeout
	 * Not that on Core99 machines, the PMU keeps sending us environement
	 * messages, we should find a way to either fix IDE or make it call
	 * pmu_suspend() before masking interrupts. This can also happens while
	 * scolling with some fbdevs.
	 */
	do {
		pmu_poll();
	} while (pmu_state != idle);

	return 0;
}

arch_initcall(via_pmu_start);

/*
 * This has to be done after pci_init, which is a subsys_initcall.
 */
static int __init via_pmu_dev_init(void)
{
	if (pmu_state == uninitialized)
		return -ENODEV;

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Initialize backlight */
	pmu_backlight_init();
#endif

#ifdef CONFIG_PPC32
  	if (of_machine_is_compatible("AAPL,3400/2400") ||
  		of_machine_is_compatible("AAPL,3500")) {
		int mb = pmac_call_feature(PMAC_FTR_GET_MB_INFO,
			NULL, PMAC_MB_INFO_MODEL, 0);
		pmu_battery_count = 1;
		if (mb == PMAC_TYPE_COMET)
			pmu_batteries[0].flags |= PMU_BATT_TYPE_COMET;
		else
			pmu_batteries[0].flags |= PMU_BATT_TYPE_HOOPER;
	} else if (of_machine_is_compatible("AAPL,PowerBook1998") ||
		of_machine_is_compatible("PowerBook1,1")) {
		pmu_battery_count = 2;
		pmu_batteries[0].flags |= PMU_BATT_TYPE_SMART;
		pmu_batteries[1].flags |= PMU_BATT_TYPE_SMART;
	} else {
		struct device_node* prim =
			of_find_node_by_name(NULL, "power-mgt");
		const u32 *prim_info = NULL;
		if (prim)
			prim_info = of_get_property(prim, "prim-info", NULL);
		if (prim_info) {
			/* Other stuffs here yet unknown */
			pmu_battery_count = (prim_info[6] >> 16) & 0xff;
			pmu_batteries[0].flags |= PMU_BATT_TYPE_SMART;
			if (pmu_battery_count > 1)
				pmu_batteries[1].flags |= PMU_BATT_TYPE_SMART;
		}
		of_node_put(prim);
	}
#endif /* CONFIG_PPC32 */

	/* Create /proc/pmu */
	proc_pmu_root = proc_mkdir("pmu", NULL);
	if (proc_pmu_root) {
		long i;

		for (i=0; i<pmu_battery_count; i++) {
			char title[16];
			sprintf(title, "battery_%ld", i);
			proc_pmu_batt[i] = proc_create_single_data(title, 0,
					proc_pmu_root, pmu_battery_proc_show,
					(void *)i);
		}

		proc_pmu_info = proc_create_single("info", 0, proc_pmu_root,
				pmu_info_proc_show);
		proc_pmu_irqstats = proc_create_single("interrupts", 0,
				proc_pmu_root, pmu_irqstats_proc_show);
		proc_pmu_options = proc_create("options", 0600, proc_pmu_root,
						&pmu_options_proc_ops);
	}
	return 0;
}

device_initcall(via_pmu_dev_init);

static int
init_pmu(void)
{
	int timeout;
	struct adb_request req;

	/* Negate TREQ. Set TACK to input and TREQ to output. */
	out_8(&via2[B], in_8(&via2[B]) | TREQ);
	out_8(&via2[DIRB], (in_8(&via2[DIRB]) | TREQ) & ~TACK);

	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, pmu_intr_mask);
	timeout =  100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: no response from PMU\n");
			return 0;
		}
		udelay(10);
		pmu_poll();
	}

	/* ack all pending interrupts */
	timeout = 100000;
	interrupt_data[0][0] = 1;
	while (interrupt_data[0][0] || pmu_state != idle) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: timed out acking intrs\n");
			return 0;
		}
		if (pmu_state == idle)
			adb_int_pending = 1;
		via_pmu_interrupt(0, NULL);
		udelay(10);
	}

	/* Tell PMU we are ready.  */
	if (pmu_kind == PMU_KEYLARGO_BASED) {
		pmu_request(&req, NULL, 2, PMU_SYSTEM_READY, 2);
		while (!req.complete)
			pmu_poll();
	}

	/* Read PMU version */
	pmu_request(&req, NULL, 1, PMU_GET_VERSION);
	pmu_wait_complete(&req);
	if (req.reply_len > 0)
		pmu_version = req.reply[0];
	
	/* Read server mode setting */
	if (pmu_kind == PMU_KEYLARGO_BASED) {
		pmu_request(&req, NULL, 2, PMU_POWER_EVENTS,
			    PMU_PWR_GET_POWERUP_EVENTS);
		pmu_wait_complete(&req);
		if (req.reply_len == 2) {
			if (req.reply[1] & PMU_PWR_WAKEUP_AC_INSERT)
				option_server_mode = 1;
			printk(KERN_INFO "via-pmu: Server Mode is %s\n",
			       option_server_mode ? "enabled" : "disabled");
		}
	}

	printk(KERN_INFO "PMU driver v%d initialized for %s, firmware: %02x\n",
	       PMU_DRIVER_VERSION, pbook_type[pmu_kind], pmu_version);

	return 1;
}

int
pmu_get_model(void)
{
	return pmu_kind;
}

static void pmu_set_server_mode(int server_mode)
{
	struct adb_request req;

	if (pmu_kind != PMU_KEYLARGO_BASED)
		return;

	option_server_mode = server_mode;
	pmu_request(&req, NULL, 2, PMU_POWER_EVENTS, PMU_PWR_GET_POWERUP_EVENTS);
	pmu_wait_complete(&req);
	if (req.reply_len < 2)
		return;
	if (server_mode)
		pmu_request(&req, NULL, 4, PMU_POWER_EVENTS,
			    PMU_PWR_SET_POWERUP_EVENTS,
			    req.reply[0], PMU_PWR_WAKEUP_AC_INSERT); 
	else
		pmu_request(&req, NULL, 4, PMU_POWER_EVENTS,
			    PMU_PWR_CLR_POWERUP_EVENTS,
			    req.reply[0], PMU_PWR_WAKEUP_AC_INSERT); 
	pmu_wait_complete(&req);
}

/* This new version of the code for 2400/3400/3500 powerbooks
 * is inspired from the implementation in gkrellm-pmu
 */
static void
done_battery_state_ohare(struct adb_request* req)
{
#ifdef CONFIG_PPC_PMAC
	/* format:
	 *  [0]    :  flags
	 *    0x01 :  AC indicator
	 *    0x02 :  charging
	 *    0x04 :  battery exist
	 *    0x08 :  
	 *    0x10 :  
	 *    0x20 :  full charged
	 *    0x40 :  pcharge reset
	 *    0x80 :  battery exist
	 *
	 *  [1][2] :  battery voltage
	 *  [3]    :  CPU temperature
	 *  [4]    :  battery temperature
	 *  [5]    :  current
	 *  [6][7] :  pcharge
	 *              --tkoba
	 */
	unsigned int bat_flags = PMU_BATT_TYPE_HOOPER;
	long pcharge, charge, vb, vmax, lmax;
	long vmax_charging, vmax_charged;
	long amperage, voltage, time, max;
	int mb = pmac_call_feature(PMAC_FTR_GET_MB_INFO,
			NULL, PMAC_MB_INFO_MODEL, 0);

	if (req->reply[0] & 0x01)
		pmu_power_flags |= PMU_PWR_AC_PRESENT;
	else
		pmu_power_flags &= ~PMU_PWR_AC_PRESENT;
	
	if (mb == PMAC_TYPE_COMET) {
		vmax_charged = 189;
		vmax_charging = 213;
		lmax = 6500;
	} else {
		vmax_charged = 330;
		vmax_charging = 330;
		lmax = 6500;
	}
	vmax = vmax_charged;

	/* If battery installed */
	if (req->reply[0] & 0x04) {
		bat_flags |= PMU_BATT_PRESENT;
		if (req->reply[0] & 0x02)
			bat_flags |= PMU_BATT_CHARGING;
		vb = (req->reply[1] << 8) | req->reply[2];
		voltage = (vb * 265 + 72665) / 10;
		amperage = req->reply[5];
		if ((req->reply[0] & 0x01) == 0) {
			if (amperage > 200)
				vb += ((amperage - 200) * 15)/100;
		} else if (req->reply[0] & 0x02) {
			vb = (vb * 97) / 100;
			vmax = vmax_charging;
		}
		charge = (100 * vb) / vmax;
		if (req->reply[0] & 0x40) {
			pcharge = (req->reply[6] << 8) + req->reply[7];
			if (pcharge > lmax)
				pcharge = lmax;
			pcharge *= 100;
			pcharge = 100 - pcharge / lmax;
			if (pcharge < charge)
				charge = pcharge;
		}
		if (amperage > 0)
			time = (charge * 16440) / amperage;
		else
			time = 0;
		max = 100;
		amperage = -amperage;
	} else
		charge = max = amperage = voltage = time = 0;

	pmu_batteries[pmu_cur_battery].flags = bat_flags;
	pmu_batteries[pmu_cur_battery].charge = charge;
	pmu_batteries[pmu_cur_battery].max_charge = max;
	pmu_batteries[pmu_cur_battery].amperage = amperage;
	pmu_batteries[pmu_cur_battery].voltage = voltage;
	pmu_batteries[pmu_cur_battery].time_remaining = time;
#endif /* CONFIG_PPC_PMAC */

	clear_bit(0, &async_req_locks);
}

static void
done_battery_state_smart(struct adb_request* req)
{
	/* format:
	 *  [0] : format of this structure (known: 3,4,5)
	 *  [1] : flags
	 *  
	 *  format 3 & 4:
	 *  
	 *  [2] : charge
	 *  [3] : max charge
	 *  [4] : current
	 *  [5] : voltage
	 *  
	 *  format 5:
	 *  
	 *  [2][3] : charge
	 *  [4][5] : max charge
	 *  [6][7] : current
	 *  [8][9] : voltage
	 */
	 
	unsigned int bat_flags = PMU_BATT_TYPE_SMART;
	int amperage;
	unsigned int capa, max, voltage;
	
	if (req->reply[1] & 0x01)
		pmu_power_flags |= PMU_PWR_AC_PRESENT;
	else
		pmu_power_flags &= ~PMU_PWR_AC_PRESENT;


	capa = max = amperage = voltage = 0;
	
	if (req->reply[1] & 0x04) {
		bat_flags |= PMU_BATT_PRESENT;
		switch(req->reply[0]) {
			case 3:
			case 4: capa = req->reply[2];
				max = req->reply[3];
				amperage = *((signed char *)&req->reply[4]);
				voltage = req->reply[5];
				break;
			case 5: capa = (req->reply[2] << 8) | req->reply[3];
				max = (req->reply[4] << 8) | req->reply[5];
				amperage = *((signed short *)&req->reply[6]);
				voltage = (req->reply[8] << 8) | req->reply[9];
				break;
			default:
				pr_warn("pmu.c: unrecognized battery info, "
					"len: %d, %4ph\n", req->reply_len,
							   req->reply);
				break;
		}
	}

	if ((req->reply[1] & 0x01) && (amperage > 0))
		bat_flags |= PMU_BATT_CHARGING;

	pmu_batteries[pmu_cur_battery].flags = bat_flags;
	pmu_batteries[pmu_cur_battery].charge = capa;
	pmu_batteries[pmu_cur_battery].max_charge = max;
	pmu_batteries[pmu_cur_battery].amperage = amperage;
	pmu_batteries[pmu_cur_battery].voltage = voltage;
	if (amperage) {
		if ((req->reply[1] & 0x01) && (amperage > 0))
			pmu_batteries[pmu_cur_battery].time_remaining
				= ((max-capa) * 3600) / amperage;
		else
			pmu_batteries[pmu_cur_battery].time_remaining
				= (capa * 3600) / (-amperage);
	} else
		pmu_batteries[pmu_cur_battery].time_remaining = 0;

	pmu_cur_battery = (pmu_cur_battery + 1) % pmu_battery_count;

	clear_bit(0, &async_req_locks);
}

static void
query_battery_state(void)
{
	if (test_and_set_bit(0, &async_req_locks))
		return;
	if (pmu_kind == PMU_OHARE_BASED)
		pmu_request(&batt_req, done_battery_state_ohare,
			1, PMU_BATTERY_STATE);
	else
		pmu_request(&batt_req, done_battery_state_smart,
			2, PMU_SMART_BATTERY_STATE, pmu_cur_battery+1);
}

static int pmu_info_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "PMU driver version     : %d\n", PMU_DRIVER_VERSION);
	seq_printf(m, "PMU firmware version   : %02x\n", pmu_version);
	seq_printf(m, "AC Power               : %d\n",
		((pmu_power_flags & PMU_PWR_AC_PRESENT) != 0) || pmu_battery_count == 0);
	seq_printf(m, "Battery count          : %d\n", pmu_battery_count);

	return 0;
}

static int pmu_irqstats_proc_show(struct seq_file *m, void *v)
{
	int i;
	static const char *irq_names[NUM_IRQ_STATS] = {
		"Unknown interrupt (type 0)",
		"Unknown interrupt (type 1)",
		"PC-Card eject button",
		"Sound/Brightness button",
		"ADB message",
		"Battery state change",
		"Environment interrupt",
		"Tick timer",
		"Ghost interrupt (zero len)",
		"Empty interrupt (empty mask)",
		"Max irqs in a row",
		"Total CB1 triggered events",
		"Total GPIO1 triggered events",
        };

	for (i = 0; i < NUM_IRQ_STATS; i++) {
		seq_printf(m, " %2u: %10u (%s)\n",
			     i, pmu_irq_stats[i], irq_names[i]);
	}
	return 0;
}

static int pmu_battery_proc_show(struct seq_file *m, void *v)
{
	long batnum = (long)m->private;
	
	seq_putc(m, '\n');
	seq_printf(m, "flags      : %08x\n", pmu_batteries[batnum].flags);
	seq_printf(m, "charge     : %d\n", pmu_batteries[batnum].charge);
	seq_printf(m, "max_charge : %d\n", pmu_batteries[batnum].max_charge);
	seq_printf(m, "current    : %d\n", pmu_batteries[batnum].amperage);
	seq_printf(m, "voltage    : %d\n", pmu_batteries[batnum].voltage);
	seq_printf(m, "time rem.  : %d\n", pmu_batteries[batnum].time_remaining);
	return 0;
}

static int pmu_options_proc_show(struct seq_file *m, void *v)
{
#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
	if (pmu_kind == PMU_KEYLARGO_BASED &&
	    pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,0,-1) >= 0)
		seq_printf(m, "lid_wakeup=%d\n", option_lid_wakeup);
#endif
	if (pmu_kind == PMU_KEYLARGO_BASED)
		seq_printf(m, "server_mode=%d\n", option_server_mode);

	return 0;
}

static int pmu_options_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmu_options_proc_show, NULL);
}

static ssize_t pmu_options_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	char tmp[33];
	char *label, *val;
	size_t fcount = count;
	
	if (!count)
		return -EINVAL;
	if (count > 32)
		count = 32;
	if (copy_from_user(tmp, buffer, count))
		return -EFAULT;
	tmp[count] = 0;

	label = tmp;
	while(*label == ' ')
		label++;
	val = label;
	while(*val && (*val != '=')) {
		if (*val == ' ')
			*val = 0;
		val++;
	}
	if ((*val) == 0)
		return -EINVAL;
	*(val++) = 0;
	while(*val == ' ')
		val++;
#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
	if (pmu_kind == PMU_KEYLARGO_BASED &&
	    pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,0,-1) >= 0)
		if (!strcmp(label, "lid_wakeup"))
			option_lid_wakeup = ((*val) == '1');
#endif
	if (pmu_kind == PMU_KEYLARGO_BASED && !strcmp(label, "server_mode")) {
		int new_value;
		new_value = ((*val) == '1');
		if (new_value != option_server_mode)
			pmu_set_server_mode(new_value);
	}
	return fcount;
}

static const struct proc_ops pmu_options_proc_ops = {
	.proc_open	= pmu_options_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= pmu_options_proc_write,
};

#ifdef CONFIG_ADB
/* Send an ADB command */
static int pmu_send_request(struct adb_request *req, int sync)
{
	int i, ret;

	if (pmu_state == uninitialized || !pmu_fully_inited) {
		req->complete = 1;
		return -ENXIO;
	}

	ret = -EINVAL;

	switch (req->data[0]) {
	case PMU_PACKET:
		for (i = 0; i < req->nbytes - 1; ++i)
			req->data[i] = req->data[i+1];
		--req->nbytes;
		if (pmu_data_len[req->data[0]][1] != 0) {
			req->reply[0] = ADB_RET_OK;
			req->reply_len = 1;
		} else
			req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
	case CUDA_PACKET:
		switch (req->data[1]) {
		case CUDA_GET_TIME:
			if (req->nbytes != 2)
				break;
			req->data[0] = PMU_READ_RTC;
			req->nbytes = 1;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_GET_TIME;
			ret = pmu_queue_request(req);
			break;
		case CUDA_SET_TIME:
			if (req->nbytes != 6)
				break;
			req->data[0] = PMU_SET_RTC;
			req->nbytes = 5;
			for (i = 1; i <= 4; ++i)
				req->data[i] = req->data[i+1];
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_SET_TIME;
			ret = pmu_queue_request(req);
			break;
		}
		break;
	case ADB_PACKET:
	    	if (!pmu_has_adb)
    			return -ENXIO;
		for (i = req->nbytes - 1; i > 1; --i)
			req->data[i+2] = req->data[i];
		req->data[3] = req->nbytes - 2;
		req->data[2] = pmu_adb_flags;
		/*req->data[1] = req->data[1];*/
		req->data[0] = PMU_ADB_CMD;
		req->nbytes += 2;
		req->reply_expected = 1;
		req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
	}
	if (ret) {
		req->complete = 1;
		return ret;
	}

	if (sync)
		while (!req->complete)
			pmu_poll();

	return 0;
}

/* Enable/disable autopolling */
static int __pmu_adb_autopoll(int devs)
{
	struct adb_request req;

	if (devs) {
		pmu_request(&req, NULL, 5, PMU_ADB_CMD, 0, 0x86,
			    adb_dev_map >> 8, adb_dev_map);
		pmu_adb_flags = 2;
	} else {
		pmu_request(&req, NULL, 1, PMU_ADB_POLL_OFF);
		pmu_adb_flags = 0;
	}
	while (!req.complete)
		pmu_poll();
	return 0;
}

static int pmu_adb_autopoll(int devs)
{
	if (pmu_state == uninitialized || !pmu_fully_inited || !pmu_has_adb)
		return -ENXIO;

	adb_dev_map = devs;
	return __pmu_adb_autopoll(devs);
}

/* Reset the ADB bus */
static int pmu_adb_reset_bus(void)
{
	struct adb_request req;
	int save_autopoll = adb_dev_map;

	if (pmu_state == uninitialized || !pmu_fully_inited || !pmu_has_adb)
		return -ENXIO;

	/* anyone got a better idea?? */
	__pmu_adb_autopoll(0);

	req.nbytes = 4;
	req.done = NULL;
	req.data[0] = PMU_ADB_CMD;
	req.data[1] = ADB_BUSRESET;
	req.data[2] = 0;
	req.data[3] = 0;
	req.data[4] = 0;
	req.reply_len = 0;
	req.reply_expected = 1;
	if (pmu_queue_request(&req) != 0) {
		printk(KERN_ERR "pmu_adb_reset_bus: pmu_queue_request failed\n");
		return -EIO;
	}
	pmu_wait_complete(&req);

	if (save_autopoll != 0)
		__pmu_adb_autopoll(save_autopoll);

	return 0;
}
#endif /* CONFIG_ADB */

/* Construct and send a pmu request */
int
pmu_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int nbytes, ...)
{
	va_list list;
	int i;

	if (pmu_state == uninitialized)
		return -ENXIO;

	if (nbytes < 0 || nbytes > 32) {
		printk(KERN_ERR "pmu_request: bad nbytes (%d)\n", nbytes);
		req->complete = 1;
		return -EINVAL;
	}
	req->nbytes = nbytes;
	req->done = done;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; ++i)
		req->data[i] = va_arg(list, int);
	va_end(list);
	req->reply_len = 0;
	req->reply_expected = 0;
	return pmu_queue_request(req);
}

int
pmu_queue_request(struct adb_request *req)
{
	unsigned long flags;
	int nsend;

	if (pmu_state == uninitialized) {
		req->complete = 1;
		return -ENXIO;
	}
	if (req->nbytes <= 0) {
		req->complete = 1;
		return 0;
	}
	nsend = pmu_data_len[req->data[0]][0];
	if (nsend >= 0 && req->nbytes != nsend + 1) {
		req->complete = 1;
		return -EINVAL;
	}

	req->next = NULL;
	req->sent = 0;
	req->complete = 0;

	spin_lock_irqsave(&pmu_lock, flags);
	if (current_req) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
		if (pmu_state == idle)
			pmu_start();
	}
	spin_unlock_irqrestore(&pmu_lock, flags);

	return 0;
}

static inline void
wait_for_ack(void)
{
	/* Sightly increased the delay, I had one occurrence of the message
	 * reported
	 */
	int timeout = 4000;
	while ((in_8(&via2[B]) & TACK) == 0) {
		if (--timeout < 0) {
			printk(KERN_ERR "PMU not responding (!ack)\n");
			return;
		}
		udelay(10);
	}
}

/* New PMU seems to be very sensitive to those timings, so we make sure
 * PCI is flushed immediately */
static inline void
send_byte(int x)
{
	out_8(&via1[ACR], in_8(&via1[ACR]) | SR_OUT | SR_EXT);
	out_8(&via1[SR], x);
	out_8(&via2[B], in_8(&via2[B]) & ~TREQ);	/* assert TREQ */
	(void)in_8(&via2[B]);
}

static inline void
recv_byte(void)
{
	out_8(&via1[ACR], (in_8(&via1[ACR]) & ~SR_OUT) | SR_EXT);
	in_8(&via1[SR]);		/* resets SR */
	out_8(&via2[B], in_8(&via2[B]) & ~TREQ);
	(void)in_8(&via2[B]);
}

static inline void
pmu_done(struct adb_request *req)
{
	void (*done)(struct adb_request *) = req->done;
	mb();
	req->complete = 1;
    	/* Here, we assume that if the request has a done member, the
    	 * struct request will survive to setting req->complete to 1
    	 */
	if (done)
		(*done)(req);
}

static void
pmu_start(void)
{
	struct adb_request *req;

	/* assert pmu_state == idle */
	/* get the packet to send */
	req = current_req;
	if (!req || pmu_state != idle
	    || (/*req->reply_expected && */req_awaiting_reply))
		return;

	pmu_state = sending;
	data_index = 1;
	data_len = pmu_data_len[req->data[0]][0];

	/* Sounds safer to make sure ACK is high before writing. This helped
	 * kill a problem with ADB and some iBooks
	 */
	wait_for_ack();
	/* set the shift register to shift out and send a byte */
	send_byte(req->data[0]);
}

void
pmu_poll(void)
{
	if (pmu_state == uninitialized)
		return;
	if (disable_poll)
		return;
	via_pmu_interrupt(0, NULL);
}

void
pmu_poll_adb(void)
{
	if (pmu_state == uninitialized)
		return;
	if (disable_poll)
		return;
	/* Kicks ADB read when PMU is suspended */
	adb_int_pending = 1;
	do {
		via_pmu_interrupt(0, NULL);
	} while (pmu_suspended && (adb_int_pending || pmu_state != idle
		|| req_awaiting_reply));
}

void
pmu_wait_complete(struct adb_request *req)
{
	if (pmu_state == uninitialized)
		return;
	while((pmu_state != idle && pmu_state != locked) || !req->complete)
		via_pmu_interrupt(0, NULL);
}

/* This function loops until the PMU is idle and prevents it from
 * anwsering to ADB interrupts. pmu_request can still be called.
 * This is done to avoid spurrious shutdowns when we know we'll have
 * interrupts switched off for a long time
 */
void
pmu_suspend(void)
{
	unsigned long flags;

	if (pmu_state == uninitialized)
		return;
	
	spin_lock_irqsave(&pmu_lock, flags);
	pmu_suspended++;
	if (pmu_suspended > 1) {
		spin_unlock_irqrestore(&pmu_lock, flags);
		return;
	}

	do {
		spin_unlock_irqrestore(&pmu_lock, flags);
		if (req_awaiting_reply)
			adb_int_pending = 1;
		via_pmu_interrupt(0, NULL);
		spin_lock_irqsave(&pmu_lock, flags);
		if (!adb_int_pending && pmu_state == idle && !req_awaiting_reply) {
			if (gpio_irq >= 0)
				disable_irq_nosync(gpio_irq);
			out_8(&via1[IER], CB1_INT | IER_CLR);
			spin_unlock_irqrestore(&pmu_lock, flags);
			break;
		}
	} while (1);
}

void
pmu_resume(void)
{
	unsigned long flags;

	if (pmu_state == uninitialized || pmu_suspended < 1)
		return;

	spin_lock_irqsave(&pmu_lock, flags);
	pmu_suspended--;
	if (pmu_suspended > 0) {
		spin_unlock_irqrestore(&pmu_lock, flags);
		return;
	}
	adb_int_pending = 1;
	if (gpio_irq >= 0)
		enable_irq(gpio_irq);
	out_8(&via1[IER], CB1_INT | IER_SET);
	spin_unlock_irqrestore(&pmu_lock, flags);
	pmu_poll();
}

/* Interrupt data could be the result data from an ADB cmd */
static void
pmu_handle_data(unsigned char *data, int len)
{
	unsigned char ints;
	int idx;
	int i = 0;

	asleep = 0;
	if (drop_interrupts || len < 1) {
		adb_int_pending = 0;
		pmu_irq_stats[8]++;
		return;
	}

	/* Get PMU interrupt mask */
	ints = data[0];

	/* Record zero interrupts for stats */
	if (ints == 0)
		pmu_irq_stats[9]++;

	/* Hack to deal with ADB autopoll flag */
	if (ints & PMU_INT_ADB)
		ints &= ~(PMU_INT_ADB_AUTO | PMU_INT_AUTO_SRQ_POLL);

next:
	if (ints == 0) {
		if (i > pmu_irq_stats[10])
			pmu_irq_stats[10] = i;
		return;
	}
	i++;

	idx = ffs(ints) - 1;
	ints &= ~BIT(idx);

	pmu_irq_stats[idx]++;

	/* Note: for some reason, we get an interrupt with len=1,
	 * data[0]==0 after each normal ADB interrupt, at least
	 * on the Pismo. Still investigating...  --BenH
	 */
	switch (BIT(idx)) {
	case PMU_INT_ADB:
		if ((data[0] & PMU_INT_ADB_AUTO) == 0) {
			struct adb_request *req = req_awaiting_reply;
			if (!req) {
				printk(KERN_ERR "PMU: extra ADB reply\n");
				return;
			}
			req_awaiting_reply = NULL;
			if (len <= 2)
				req->reply_len = 0;
			else {
				memcpy(req->reply, data + 1, len - 1);
				req->reply_len = len - 1;
			}
			pmu_done(req);
		} else {
#ifdef CONFIG_XMON
			if (len == 4 && data[1] == 0x2c) {
				extern int xmon_wants_key, xmon_adb_keycode;
				if (xmon_wants_key) {
					xmon_adb_keycode = data[2];
					return;
				}
			}
#endif /* CONFIG_XMON */
#ifdef CONFIG_ADB
			/*
			 * XXX On the [23]400 the PMU gives us an up
			 * event for keycodes 0x74 or 0x75 when the PC
			 * card eject buttons are released, so we
			 * ignore those events.
			 */
			if (!(pmu_kind == PMU_OHARE_BASED && len == 4
			      && data[1] == 0x2c && data[3] == 0xff
			      && (data[2] & ~1) == 0xf4))
				adb_input(data+1, len-1, 1);
#endif /* CONFIG_ADB */		
		}
		break;

	/* Sound/brightness button pressed */
	case PMU_INT_SNDBRT:
#ifdef CONFIG_PMAC_BACKLIGHT
		if (len == 3)
			pmac_backlight_set_legacy_brightness_pmu(data[1] >> 4);
#endif
		break;

	/* Tick interrupt */
	case PMU_INT_TICK:
		/* Environment or tick interrupt, query batteries */
		if (pmu_battery_count) {
			if ((--query_batt_timer) == 0) {
				query_battery_state();
				query_batt_timer = BATTERY_POLLING_COUNT;
			}
		}
		break;

	case PMU_INT_ENVIRONMENT:
		if (pmu_battery_count)
			query_battery_state();
		pmu_pass_intr(data, len);
		/* len == 6 is probably a bad check. But how do I
		 * know what PMU versions send what events here? */
		if (len == 6) {
			via_pmu_event(PMU_EVT_POWER, !!(data[1]&8));
			via_pmu_event(PMU_EVT_LID, data[1]&1);
		}
		break;

	default:
	       pmu_pass_intr(data, len);
	}
	goto next;
}

static struct adb_request*
pmu_sr_intr(void)
{
	struct adb_request *req;
	int bite = 0;

	if (in_8(&via2[B]) & TREQ) {
		printk(KERN_ERR "PMU: spurious SR intr (%x)\n", in_8(&via2[B]));
		return NULL;
	}
	/* The ack may not yet be low when we get the interrupt */
	while ((in_8(&via2[B]) & TACK) != 0)
			;

	/* if reading grab the byte, and reset the interrupt */
	if (pmu_state == reading || pmu_state == reading_intr)
		bite = in_8(&via1[SR]);

	/* reset TREQ and wait for TACK to go high */
	out_8(&via2[B], in_8(&via2[B]) | TREQ);
	wait_for_ack();

	switch (pmu_state) {
	case sending:
		req = current_req;
		if (data_len < 0) {
			data_len = req->nbytes - 1;
			send_byte(data_len);
			break;
		}
		if (data_index <= data_len) {
			send_byte(req->data[data_index++]);
			break;
		}
		req->sent = 1;
		data_len = pmu_data_len[req->data[0]][1];
		if (data_len == 0) {
			pmu_state = idle;
			current_req = req->next;
			if (req->reply_expected)
				req_awaiting_reply = req;
			else
				return req;
		} else {
			pmu_state = reading;
			data_index = 0;
			reply_ptr = req->reply + req->reply_len;
			recv_byte();
		}
		break;

	case intack:
		data_index = 0;
		data_len = -1;
		pmu_state = reading_intr;
		reply_ptr = interrupt_data[int_data_last];
		recv_byte();
		if (gpio_irq >= 0 && !gpio_irq_enabled) {
			enable_irq(gpio_irq);
			gpio_irq_enabled = 1;
		}
		break;

	case reading:
	case reading_intr:
		if (data_len == -1) {
			data_len = bite;
			if (bite > 32)
				printk(KERN_ERR "PMU: bad reply len %d\n", bite);
		} else if (data_index < 32) {
			reply_ptr[data_index++] = bite;
		}
		if (data_index < data_len) {
			recv_byte();
			break;
		}

		if (pmu_state == reading_intr) {
			pmu_state = idle;
			int_data_state[int_data_last] = int_data_ready;
			interrupt_data_len[int_data_last] = data_len;
		} else {
			req = current_req;
			/* 
			 * For PMU sleep and freq change requests, we lock the
			 * PMU until it's explicitly unlocked. This avoids any
			 * spurrious event polling getting in
			 */
			current_req = req->next;
			req->reply_len += data_index;
			if (req->data[0] == PMU_SLEEP || req->data[0] == PMU_CPU_SPEED)
				pmu_state = locked;
			else
				pmu_state = idle;
			return req;
		}
		break;

	default:
		printk(KERN_ERR "via_pmu_interrupt: unknown state %d?\n",
		       pmu_state);
	}
	return NULL;
}

static irqreturn_t
via_pmu_interrupt(int irq, void *arg)
{
	unsigned long flags;
	int intr;
	int nloop = 0;
	int int_data = -1;
	struct adb_request *req = NULL;
	int handled = 0;

	/* This is a bit brutal, we can probably do better */
	spin_lock_irqsave(&pmu_lock, flags);
	++disable_poll;
	
	for (;;) {
		/* On 68k Macs, VIA interrupts are dispatched individually.
		 * Unless we are polling, the relevant IRQ flag has already
		 * been cleared.
		 */
		intr = 0;
		if (IS_ENABLED(CONFIG_PPC_PMAC) || !irq) {
			intr = in_8(&via1[IFR]) & (SR_INT | CB1_INT);
			out_8(&via1[IFR], intr);
		}
#ifndef CONFIG_PPC_PMAC
		switch (irq) {
		case IRQ_MAC_ADB_CL:
			intr = CB1_INT;
			break;
		case IRQ_MAC_ADB_SR:
			intr = SR_INT;
			break;
		}
#endif
		if (intr == 0)
			break;
		handled = 1;
		if (++nloop > 1000) {
			printk(KERN_DEBUG "PMU: stuck in intr loop, "
			       "intr=%x, ier=%x pmu_state=%d\n",
			       intr, in_8(&via1[IER]), pmu_state);
			break;
		}
		if (intr & CB1_INT) {
			adb_int_pending = 1;
			pmu_irq_stats[11]++;
		}
		if (intr & SR_INT) {
			req = pmu_sr_intr();
			if (req)
				break;
		}
#ifndef CONFIG_PPC_PMAC
		break;
#endif
	}

recheck:
	if (pmu_state == idle) {
		if (adb_int_pending) {
			if (int_data_state[0] == int_data_empty)
				int_data_last = 0;
			else if (int_data_state[1] == int_data_empty)
				int_data_last = 1;
			else
				goto no_free_slot;
			pmu_state = intack;
			int_data_state[int_data_last] = int_data_fill;
			/* Sounds safer to make sure ACK is high before writing.
			 * This helped kill a problem with ADB and some iBooks
			 */
			wait_for_ack();
			send_byte(PMU_INT_ACK);
			adb_int_pending = 0;
		} else if (current_req)
			pmu_start();
	}
no_free_slot:			
	/* Mark the oldest buffer for flushing */
	if (int_data_state[!int_data_last] == int_data_ready) {
		int_data_state[!int_data_last] = int_data_flush;
		int_data = !int_data_last;
	} else if (int_data_state[int_data_last] == int_data_ready) {
		int_data_state[int_data_last] = int_data_flush;
		int_data = int_data_last;
	}
	--disable_poll;
	spin_unlock_irqrestore(&pmu_lock, flags);

	/* Deal with completed PMU requests outside of the lock */
	if (req) {
		pmu_done(req);
		req = NULL;
	}
		
	/* Deal with interrupt datas outside of the lock */
	if (int_data >= 0) {
		pmu_handle_data(interrupt_data[int_data], interrupt_data_len[int_data]);
		spin_lock_irqsave(&pmu_lock, flags);
		++disable_poll;
		int_data_state[int_data] = int_data_empty;
		int_data = -1;
		goto recheck;
	}

	return IRQ_RETVAL(handled);
}

void
pmu_unlock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_lock, flags);
	if (pmu_state == locked)
		pmu_state = idle;
	adb_int_pending = 1;
	spin_unlock_irqrestore(&pmu_lock, flags);
}


static __maybe_unused irqreturn_t
gpio1_interrupt(int irq, void *arg)
{
	unsigned long flags;

	if ((in_8(gpio_reg + 0x9) & 0x02) == 0) {
		spin_lock_irqsave(&pmu_lock, flags);
		if (gpio_irq_enabled > 0) {
			disable_irq_nosync(gpio_irq);
			gpio_irq_enabled = 0;
		}
		pmu_irq_stats[12]++;
		adb_int_pending = 1;
		spin_unlock_irqrestore(&pmu_lock, flags);
		via_pmu_interrupt(0, NULL);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

void
pmu_enable_irled(int on)
{
	struct adb_request req;

	if (pmu_state == uninitialized)
		return ;
	if (pmu_kind == PMU_KEYLARGO_BASED)
		return ;

	pmu_request(&req, NULL, 2, PMU_POWER_CTRL, PMU_POW_IRLED |
	    (on ? PMU_POW_ON : PMU_POW_OFF));
	pmu_wait_complete(&req);
}

/* Offset between Unix time (1970-based) and Mac time (1904-based) */
#define RTC_OFFSET	2082844800

time64_t pmu_get_time(void)
{
	struct adb_request req;
	u32 now;

	if (pmu_request(&req, NULL, 1, PMU_READ_RTC) < 0)
		return 0;
	pmu_wait_complete(&req);
	if (req.reply_len != 4)
		pr_err("%s: got %d byte reply\n", __func__, req.reply_len);
	now = (req.reply[0] << 24) + (req.reply[1] << 16) +
	      (req.reply[2] << 8) + req.reply[3];
	return (time64_t)now - RTC_OFFSET;
}

int pmu_set_rtc_time(struct rtc_time *tm)
{
	u32 now;
	struct adb_request req;

	now = lower_32_bits(rtc_tm_to_time64(tm) + RTC_OFFSET);
	if (pmu_request(&req, NULL, 5, PMU_SET_RTC,
	                now >> 24, now >> 16, now >> 8, now) < 0)
		return -ENXIO;
	pmu_wait_complete(&req);
	if (req.reply_len != 0)
		pr_err("%s: got %d byte reply\n", __func__, req.reply_len);
	return 0;
}

void
pmu_restart(void)
{
	struct adb_request req;

	if (pmu_state == uninitialized)
		return;

	local_irq_disable();

	drop_interrupts = 1;
	
	if (pmu_kind != PMU_KEYLARGO_BASED) {
		pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, PMU_INT_ADB |
						PMU_INT_TICK );
		while(!req.complete)
			pmu_poll();
	}

	pmu_request(&req, NULL, 1, PMU_RESET);
	pmu_wait_complete(&req);
	for (;;)
		;
}

void
pmu_shutdown(void)
{
	struct adb_request req;

	if (pmu_state == uninitialized)
		return;

	local_irq_disable();

	drop_interrupts = 1;

	if (pmu_kind != PMU_KEYLARGO_BASED) {
		pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, PMU_INT_ADB |
						PMU_INT_TICK );
		pmu_wait_complete(&req);
	} else {
		/* Disable server mode on shutdown or we'll just
		 * wake up again
		 */
		pmu_set_server_mode(0);
	}

	pmu_request(&req, NULL, 5, PMU_SHUTDOWN,
		    'M', 'A', 'T', 'T');
	pmu_wait_complete(&req);
	for (;;)
		;
}

int
pmu_present(void)
{
	return pmu_state != uninitialized;
}

#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
/*
 * Put the powerbook to sleep.
 */
 
static u32 save_via[8];
static int __fake_sleep;

static void
save_via_state(void)
{
	save_via[0] = in_8(&via1[ANH]);
	save_via[1] = in_8(&via1[DIRA]);
	save_via[2] = in_8(&via1[B]);
	save_via[3] = in_8(&via1[DIRB]);
	save_via[4] = in_8(&via1[PCR]);
	save_via[5] = in_8(&via1[ACR]);
	save_via[6] = in_8(&via1[T1CL]);
	save_via[7] = in_8(&via1[T1CH]);
}
static void
restore_via_state(void)
{
	out_8(&via1[ANH],  save_via[0]);
	out_8(&via1[DIRA], save_via[1]);
	out_8(&via1[B],    save_via[2]);
	out_8(&via1[DIRB], save_via[3]);
	out_8(&via1[PCR],  save_via[4]);
	out_8(&via1[ACR],  save_via[5]);
	out_8(&via1[T1CL], save_via[6]);
	out_8(&via1[T1CH], save_via[7]);
	out_8(&via1[IER], IER_CLR | 0x7f);	/* disable all intrs */
	out_8(&via1[IFR], 0x7f);			/* clear IFR */
	out_8(&via1[IER], IER_SET | SR_INT | CB1_INT);
}

#define	GRACKLE_PM	(1<<7)
#define GRACKLE_DOZE	(1<<5)
#define	GRACKLE_NAP	(1<<4)
#define	GRACKLE_SLEEP	(1<<3)

static int powerbook_sleep_grackle(void)
{
	unsigned long save_l2cr;
	unsigned short pmcr1;
	struct adb_request req;
	struct pci_dev *grackle;

	grackle = pci_get_domain_bus_and_slot(0, 0, 0);
	if (!grackle)
		return -ENODEV;

	/* Turn off various things. Darwin does some retry tests here... */
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL0, PMU_POW0_OFF|PMU_POW0_HARD_DRIVE);
	pmu_wait_complete(&req);
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
		PMU_POW_OFF|PMU_POW_BACKLIGHT|PMU_POW_IRLED|PMU_POW_MEDIABAY);
	pmu_wait_complete(&req);

	/* For 750, save backside cache setting and disable it */
	save_l2cr = _get_L2CR();	/* (returns -1 if not available) */

	if (!__fake_sleep) {
		/* Ask the PMU to put us to sleep */
		pmu_request(&req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
		pmu_wait_complete(&req);
	}

	/* The VIA is supposed not to be restored correctly*/
	save_via_state();
	/* We shut down some HW */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,0,1);

	pci_read_config_word(grackle, 0x70, &pmcr1);
	/* Apparently, MacOS uses NAP mode for Grackle ??? */
	pmcr1 &= ~(GRACKLE_DOZE|GRACKLE_SLEEP); 
	pmcr1 |= GRACKLE_PM|GRACKLE_NAP;
	pci_write_config_word(grackle, 0x70, pmcr1);

	/* Call low-level ASM sleep handler */
	if (__fake_sleep)
		mdelay(5000);
	else
		low_sleep_handler();

	/* We're awake again, stop grackle PM */
	pci_read_config_word(grackle, 0x70, &pmcr1);
	pmcr1 &= ~(GRACKLE_PM|GRACKLE_DOZE|GRACKLE_SLEEP|GRACKLE_NAP); 
	pci_write_config_word(grackle, 0x70, pmcr1);

	pci_dev_put(grackle);

	/* Make sure the PMU is idle */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,0,0);
	restore_via_state();
	
	/* Restore L2 cache */
	if (save_l2cr != 0xffffffff && (save_l2cr & L2CR_L2E) != 0)
 		_set_L2CR(save_l2cr);
	
	/* Restore userland MMU context */
	switch_mmu_context(NULL, current->active_mm, NULL);

	/* Power things up */
	pmu_unlock();
	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, pmu_intr_mask);
	pmu_wait_complete(&req);
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL0,
			PMU_POW0_ON|PMU_POW0_HARD_DRIVE);
	pmu_wait_complete(&req);
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
			PMU_POW_ON|PMU_POW_BACKLIGHT|PMU_POW_CHARGER|PMU_POW_IRLED|PMU_POW_MEDIABAY);
	pmu_wait_complete(&req);

	return 0;
}

static int
powerbook_sleep_Core99(void)
{
	unsigned long save_l2cr;
	unsigned long save_l3cr;
	struct adb_request req;
	
	if (pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,0,-1) < 0) {
		printk(KERN_ERR "Sleep mode not supported on this machine\n");
		return -ENOSYS;
	}

	if (num_online_cpus() > 1 || cpu_is_offline(0))
		return -EAGAIN;

	/* Stop environment and ADB interrupts */
	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0);
	pmu_wait_complete(&req);

	/* Tell PMU what events will wake us up */
	pmu_request(&req, NULL, 4, PMU_POWER_EVENTS, PMU_PWR_CLR_WAKEUP_EVENTS,
		0xff, 0xff);
	pmu_wait_complete(&req);
	pmu_request(&req, NULL, 4, PMU_POWER_EVENTS, PMU_PWR_SET_WAKEUP_EVENTS,
		0, PMU_PWR_WAKEUP_KEY |
		(option_lid_wakeup ? PMU_PWR_WAKEUP_LID_OPEN : 0));
	pmu_wait_complete(&req);

	/* Save the state of the L2 and L3 caches */
	save_l3cr = _get_L3CR();	/* (returns -1 if not available) */
	save_l2cr = _get_L2CR();	/* (returns -1 if not available) */

	if (!__fake_sleep) {
		/* Ask the PMU to put us to sleep */
		pmu_request(&req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
		pmu_wait_complete(&req);
	}

	/* The VIA is supposed not to be restored correctly*/
	save_via_state();

	/* Shut down various ASICs. There's a chance that we can no longer
	 * talk to the PMU after this, so I moved it to _after_ sending the
	 * sleep command to it. Still need to be checked.
	 */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, 1);

	/* Call low-level ASM sleep handler */
	if (__fake_sleep)
		mdelay(5000);
	else
		low_sleep_handler();

	/* Restore Apple core ASICs state */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, 0);

	/* Restore VIA */
	restore_via_state();

	/* tweak LPJ before cpufreq is there */
	loops_per_jiffy *= 2;

	/* Restore video */
	pmac_call_early_video_resume();

	/* Restore L2 cache */
	if (save_l2cr != 0xffffffff && (save_l2cr & L2CR_L2E) != 0)
 		_set_L2CR(save_l2cr);
	/* Restore L3 cache */
	if (save_l3cr != 0xffffffff && (save_l3cr & L3CR_L3E) != 0)
 		_set_L3CR(save_l3cr);
	
	/* Restore userland MMU context */
	switch_mmu_context(NULL, current->active_mm, NULL);

	/* Tell PMU we are ready */
	pmu_unlock();
	pmu_request(&req, NULL, 2, PMU_SYSTEM_READY, 2);
	pmu_wait_complete(&req);
	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, pmu_intr_mask);
	pmu_wait_complete(&req);

	/* Restore LPJ, cpufreq will adjust the cpu frequency */
	loops_per_jiffy /= 2;

	return 0;
}

#define PB3400_MEM_CTRL		0xf8000000
#define PB3400_MEM_CTRL_SLEEP	0x70

static void __iomem *pb3400_mem_ctrl;

static void powerbook_sleep_init_3400(void)
{
	/* map in the memory controller registers */
	pb3400_mem_ctrl = ioremap(PB3400_MEM_CTRL, 0x100);
	if (pb3400_mem_ctrl == NULL)
		printk(KERN_WARNING "ioremap failed: sleep won't be possible");
}

static int powerbook_sleep_3400(void)
{
	int i, x;
	unsigned int hid0;
	unsigned long msr;
	struct adb_request sleep_req;
	unsigned int __iomem *mem_ctrl_sleep;

	if (pb3400_mem_ctrl == NULL)
		return -ENOMEM;
	mem_ctrl_sleep = pb3400_mem_ctrl + PB3400_MEM_CTRL_SLEEP;

	/* Set the memory controller to keep the memory refreshed
	   while we're asleep */
	for (i = 0x403f; i >= 0x4000; --i) {
		out_be32(mem_ctrl_sleep, i);
		do {
			x = (in_be32(mem_ctrl_sleep) >> 16) & 0x3ff;
		} while (x == 0);
		if (x >= 0x100)
			break;
	}

	/* Ask the PMU to put us to sleep */
	pmu_request(&sleep_req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
	pmu_wait_complete(&sleep_req);
	pmu_unlock();

	pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, 1);

	asleep = 1;

	/* Put the CPU into sleep mode */
	hid0 = mfspr(SPRN_HID0);
	hid0 = (hid0 & ~(HID0_NAP | HID0_DOZE)) | HID0_SLEEP;
	mtspr(SPRN_HID0, hid0);
	local_irq_enable();
	msr = mfmsr() | MSR_POW;
	while (asleep) {
		mb();
		mtmsr(msr);
		isync();
	}
	local_irq_disable();

	/* OK, we're awake again, start restoring things */
	out_be32(mem_ctrl_sleep, 0x3f);
	pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, 0);

	return 0;
}

#endif /* CONFIG_SUSPEND && CONFIG_PPC32 */

/*
 * Support for /dev/pmu device
 */
#define RB_SIZE		0x10
struct pmu_private {
	struct list_head list;
	int	rb_get;
	int	rb_put;
	struct rb_entry {
		unsigned short len;
		unsigned char data[16];
	}	rb_buf[RB_SIZE];
	wait_queue_head_t wait;
	spinlock_t lock;
#if defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_PMAC_BACKLIGHT)
	int	backlight_locker;
#endif
};

static LIST_HEAD(all_pmu_pvt);
static DEFINE_SPINLOCK(all_pvt_lock);

static void
pmu_pass_intr(unsigned char *data, int len)
{
	struct pmu_private *pp;
	struct list_head *list;
	int i;
	unsigned long flags;

	if (len > sizeof(pp->rb_buf[0].data))
		len = sizeof(pp->rb_buf[0].data);
	spin_lock_irqsave(&all_pvt_lock, flags);
	for (list = &all_pmu_pvt; (list = list->next) != &all_pmu_pvt; ) {
		pp = list_entry(list, struct pmu_private, list);
		spin_lock(&pp->lock);
		i = pp->rb_put + 1;
		if (i >= RB_SIZE)
			i = 0;
		if (i != pp->rb_get) {
			struct rb_entry *rp = &pp->rb_buf[pp->rb_put];
			rp->len = len;
			memcpy(rp->data, data, len);
			pp->rb_put = i;
			wake_up_interruptible(&pp->wait);
		}
		spin_unlock(&pp->lock);
	}
	spin_unlock_irqrestore(&all_pvt_lock, flags);
}

static int
pmu_open(struct inode *inode, struct file *file)
{
	struct pmu_private *pp;
	unsigned long flags;

	pp = kmalloc(sizeof(struct pmu_private), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	pp->rb_get = pp->rb_put = 0;
	spin_lock_init(&pp->lock);
	init_waitqueue_head(&pp->wait);
	mutex_lock(&pmu_info_proc_mutex);
	spin_lock_irqsave(&all_pvt_lock, flags);
#if defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_PMAC_BACKLIGHT)
	pp->backlight_locker = 0;
#endif
	list_add(&pp->list, &all_pmu_pvt);
	spin_unlock_irqrestore(&all_pvt_lock, flags);
	file->private_data = pp;
	mutex_unlock(&pmu_info_proc_mutex);
	return 0;
}

static ssize_t 
pmu_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct pmu_private *pp = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int ret = 0;

	if (count < 1 || !pp)
		return -EINVAL;

	spin_lock_irqsave(&pp->lock, flags);
	add_wait_queue(&pp->wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	for (;;) {
		ret = -EAGAIN;
		if (pp->rb_get != pp->rb_put) {
			int i = pp->rb_get;
			struct rb_entry *rp = &pp->rb_buf[i];
			ret = rp->len;
			spin_unlock_irqrestore(&pp->lock, flags);
			if (ret > count)
				ret = count;
			if (ret > 0 && copy_to_user(buf, rp->data, ret))
				ret = -EFAULT;
			if (++i >= RB_SIZE)
				i = 0;
			spin_lock_irqsave(&pp->lock, flags);
			pp->rb_get = i;
		}
		if (ret >= 0)
			break;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		spin_unlock_irqrestore(&pp->lock, flags);
		schedule();
		spin_lock_irqsave(&pp->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&pp->wait, &wait);
	spin_unlock_irqrestore(&pp->lock, flags);
	
	return ret;
}

static ssize_t
pmu_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	return 0;
}

static __poll_t
pmu_fpoll(struct file *filp, poll_table *wait)
{
	struct pmu_private *pp = filp->private_data;
	__poll_t mask = 0;
	unsigned long flags;
	
	if (!pp)
		return 0;
	poll_wait(filp, &pp->wait, wait);
	spin_lock_irqsave(&pp->lock, flags);
	if (pp->rb_get != pp->rb_put)
		mask |= EPOLLIN;
	spin_unlock_irqrestore(&pp->lock, flags);
	return mask;
}

static int
pmu_release(struct inode *inode, struct file *file)
{
	struct pmu_private *pp = file->private_data;
	unsigned long flags;

	if (pp) {
		file->private_data = NULL;
		spin_lock_irqsave(&all_pvt_lock, flags);
		list_del(&pp->list);
		spin_unlock_irqrestore(&all_pvt_lock, flags);

#if defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_PMAC_BACKLIGHT)
		if (pp->backlight_locker)
			pmac_backlight_enable();
#endif

		kfree(pp);
	}
	return 0;
}

#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
static void pmac_suspend_disable_irqs(void)
{
	/* Call platform functions marked "on sleep" */
	pmac_pfunc_i2c_suspend();
	pmac_pfunc_base_suspend();
}

static int powerbook_sleep(suspend_state_t state)
{
	int error = 0;

	/* Wait for completion of async requests */
	while (!batt_req.complete)
		pmu_poll();

	/* Giveup the lazy FPU & vec so we don't have to back them
	 * up from the low level code
	 */
	enable_kernel_fp();

#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		enable_kernel_altivec();
#endif /* CONFIG_ALTIVEC */

	switch (pmu_kind) {
	case PMU_OHARE_BASED:
		error = powerbook_sleep_3400();
		break;
	case PMU_HEATHROW_BASED:
	case PMU_PADDINGTON_BASED:
		error = powerbook_sleep_grackle();
		break;
	case PMU_KEYLARGO_BASED:
		error = powerbook_sleep_Core99();
		break;
	default:
		return -ENOSYS;
	}

	if (error)
		return error;

	mdelay(100);

	return 0;
}

static void pmac_suspend_enable_irqs(void)
{
	/* Force a poll of ADB interrupts */
	adb_int_pending = 1;
	via_pmu_interrupt(0, NULL);

	mdelay(10);

	/* Call platform functions marked "on wake" */
	pmac_pfunc_base_resume();
	pmac_pfunc_i2c_resume();
}

static int pmu_sleep_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM
		&& (pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, -1) >= 0);
}

static const struct platform_suspend_ops pmu_pm_ops = {
	.enter = powerbook_sleep,
	.valid = pmu_sleep_valid,
};

static int register_pmu_pm_ops(void)
{
	if (pmu_kind == PMU_OHARE_BASED)
		powerbook_sleep_init_3400();
	ppc_md.suspend_disable_irqs = pmac_suspend_disable_irqs;
	ppc_md.suspend_enable_irqs = pmac_suspend_enable_irqs;
	suspend_set_ops(&pmu_pm_ops);

	return 0;
}

device_initcall(register_pmu_pm_ops);
#endif

static int pmu_ioctl(struct file *filp,
		     u_int cmd, u_long arg)
{
	__u32 __user *argp = (__u32 __user *)arg;
	int error = -EINVAL;

	switch (cmd) {
#ifdef CONFIG_PPC_PMAC
	case PMU_IOC_SLEEP:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return pm_suspend(PM_SUSPEND_MEM);
	case PMU_IOC_CAN_SLEEP:
		if (pmac_call_feature(PMAC_FTR_SLEEP_STATE, NULL, 0, -1) < 0)
			return put_user(0, argp);
		else
			return put_user(1, argp);
#endif

#ifdef CONFIG_PMAC_BACKLIGHT_LEGACY
	/* Compatibility ioctl's for backlight */
	case PMU_IOC_GET_BACKLIGHT:
	{
		int brightness;

		brightness = pmac_backlight_get_legacy_brightness();
		if (brightness < 0)
			return brightness;
		else
			return put_user(brightness, argp);

	}
	case PMU_IOC_SET_BACKLIGHT:
	{
		int brightness;

		error = get_user(brightness, argp);
		if (error)
			return error;

		return pmac_backlight_set_legacy_brightness(brightness);
	}
#ifdef CONFIG_INPUT_ADBHID
	case PMU_IOC_GRAB_BACKLIGHT: {
		struct pmu_private *pp = filp->private_data;

		if (pp->backlight_locker)
			return 0;

		pp->backlight_locker = 1;
		pmac_backlight_disable();

		return 0;
	}
#endif /* CONFIG_INPUT_ADBHID */
#endif /* CONFIG_PMAC_BACKLIGHT_LEGACY */

	case PMU_IOC_GET_MODEL:
	    	return put_user(pmu_kind, argp);
	case PMU_IOC_HAS_ADB:
		return put_user(pmu_has_adb, argp);
	}
	return error;
}

static long pmu_unlocked_ioctl(struct file *filp,
			       u_int cmd, u_long arg)
{
	int ret;

	mutex_lock(&pmu_info_proc_mutex);
	ret = pmu_ioctl(filp, cmd, arg);
	mutex_unlock(&pmu_info_proc_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
#define PMU_IOC_GET_BACKLIGHT32	_IOR('B', 1, compat_size_t)
#define PMU_IOC_SET_BACKLIGHT32	_IOW('B', 2, compat_size_t)
#define PMU_IOC_GET_MODEL32	_IOR('B', 3, compat_size_t)
#define PMU_IOC_HAS_ADB32	_IOR('B', 4, compat_size_t)
#define PMU_IOC_CAN_SLEEP32	_IOR('B', 5, compat_size_t)
#define PMU_IOC_GRAB_BACKLIGHT32 _IOR('B', 6, compat_size_t)

static long compat_pmu_ioctl (struct file *filp, u_int cmd, u_long arg)
{
	switch (cmd) {
	case PMU_IOC_SLEEP:
		break;
	case PMU_IOC_GET_BACKLIGHT32:
		cmd = PMU_IOC_GET_BACKLIGHT;
		break;
	case PMU_IOC_SET_BACKLIGHT32:
		cmd = PMU_IOC_SET_BACKLIGHT;
		break;
	case PMU_IOC_GET_MODEL32:
		cmd = PMU_IOC_GET_MODEL;
		break;
	case PMU_IOC_HAS_ADB32:
		cmd = PMU_IOC_HAS_ADB;
		break;
	case PMU_IOC_CAN_SLEEP32:
		cmd = PMU_IOC_CAN_SLEEP;
		break;
	case PMU_IOC_GRAB_BACKLIGHT32:
		cmd = PMU_IOC_GRAB_BACKLIGHT;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return pmu_unlocked_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations pmu_device_fops = {
	.read		= pmu_read,
	.write		= pmu_write,
	.poll		= pmu_fpoll,
	.unlocked_ioctl	= pmu_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_pmu_ioctl,
#endif
	.open		= pmu_open,
	.release	= pmu_release,
	.llseek		= noop_llseek,
};

static struct miscdevice pmu_device = {
	PMU_MINOR, "pmu", &pmu_device_fops
};

static int pmu_device_init(void)
{
	if (pmu_state == uninitialized)
		return 0;
	if (misc_register(&pmu_device) < 0)
		printk(KERN_ERR "via-pmu: cannot register misc device.\n");
	return 0;
}
device_initcall(pmu_device_init);


#ifdef DEBUG_SLEEP
static inline void 
polled_handshake(void)
{
	via2[B] &= ~TREQ; eieio();
	while ((via2[B] & TACK) != 0)
		;
	via2[B] |= TREQ; eieio();
	while ((via2[B] & TACK) == 0)
		;
}

static inline void 
polled_send_byte(int x)
{
	via1[ACR] |= SR_OUT | SR_EXT; eieio();
	via1[SR] = x; eieio();
	polled_handshake();
}

static inline int
polled_recv_byte(void)
{
	int x;

	via1[ACR] = (via1[ACR] & ~SR_OUT) | SR_EXT; eieio();
	x = via1[SR]; eieio();
	polled_handshake();
	x = via1[SR]; eieio();
	return x;
}

int
pmu_polled_request(struct adb_request *req)
{
	unsigned long flags;
	int i, l, c;

	req->complete = 1;
	c = req->data[0];
	l = pmu_data_len[c][0];
	if (l >= 0 && req->nbytes != l + 1)
		return -EINVAL;

	local_irq_save(flags);
	while (pmu_state != idle)
		pmu_poll();

	while ((via2[B] & TACK) == 0)
		;
	polled_send_byte(c);
	if (l < 0) {
		l = req->nbytes - 1;
		polled_send_byte(l);
	}
	for (i = 1; i <= l; ++i)
		polled_send_byte(req->data[i]);

	l = pmu_data_len[c][1];
	if (l < 0)
		l = polled_recv_byte();
	for (i = 0; i < l; ++i)
		req->reply[i + req->reply_len] = polled_recv_byte();

	if (req->done)
		(*req->done)(req);

	local_irq_restore(flags);
	return 0;
}

/* N.B. This doesn't work on the 3400 */
void pmu_blink(int n)
{
	struct adb_request req;

	memset(&req, 0, sizeof(req));

	for (; n > 0; --n) {
		req.nbytes = 4;
		req.done = NULL;
		req.data[0] = 0xee;
		req.data[1] = 4;
		req.data[2] = 0;
		req.data[3] = 1;
		req.reply[0] = ADB_RET_OK;
		req.reply_len = 1;
		req.reply_expected = 0;
		pmu_polled_request(&req);
		mdelay(50);
		req.nbytes = 4;
		req.done = NULL;
		req.data[0] = 0xee;
		req.data[1] = 4;
		req.data[2] = 0;
		req.data[3] = 0;
		req.reply[0] = ADB_RET_OK;
		req.reply_len = 1;
		req.reply_expected = 0;
		pmu_polled_request(&req);
		mdelay(50);
	}
	mdelay(50);
}
#endif /* DEBUG_SLEEP */

#if defined(CONFIG_SUSPEND) && defined(CONFIG_PPC32)
int pmu_sys_suspended;

static int pmu_syscore_suspend(void)
{
	/* Suspend PMU event interrupts */
	pmu_suspend();
	pmu_sys_suspended = 1;

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Tell backlight code not to muck around with the chip anymore */
	pmu_backlight_set_sleep(1);
#endif

	return 0;
}

static void pmu_syscore_resume(void)
{
	struct adb_request req;

	if (!pmu_sys_suspended)
		return;

	/* Tell PMU we are ready */
	pmu_request(&req, NULL, 2, PMU_SYSTEM_READY, 2);
	pmu_wait_complete(&req);

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Tell backlight code it can use the chip again */
	pmu_backlight_set_sleep(0);
#endif
	/* Resume PMU event interrupts */
	pmu_resume();
	pmu_sys_suspended = 0;
}

static struct syscore_ops pmu_syscore_ops = {
	.suspend = pmu_syscore_suspend,
	.resume = pmu_syscore_resume,
};

static int pmu_syscore_register(void)
{
	register_syscore_ops(&pmu_syscore_ops);

	return 0;
}
subsys_initcall(pmu_syscore_register);
#endif /* CONFIG_SUSPEND && CONFIG_PPC32 */

EXPORT_SYMBOL(pmu_request);
EXPORT_SYMBOL(pmu_queue_request);
EXPORT_SYMBOL(pmu_poll);
EXPORT_SYMBOL(pmu_poll_adb);
EXPORT_SYMBOL(pmu_wait_complete);
EXPORT_SYMBOL(pmu_suspend);
EXPORT_SYMBOL(pmu_resume);
EXPORT_SYMBOL(pmu_unlock);
#if defined(CONFIG_PPC32)
EXPORT_SYMBOL(pmu_enable_irled);
EXPORT_SYMBOL(pmu_battery_count);
EXPORT_SYMBOL(pmu_batteries);
EXPORT_SYMBOL(pmu_power_flags);
#endif /* CONFIG_SUSPEND && CONFIG_PPC32 */

