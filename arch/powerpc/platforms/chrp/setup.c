/*
 *  arch/ppc/platforms/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/pci-bridge.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/sections.h>
#include <asm/time.h>
#include <asm/btext.h>
#include <asm/i8259.h>
#include <asm/mpic.h>
#include <asm/rtas.h>
#include <asm/xmon.h>

#include "chrp.h"

void rtas_indicator_progress(char *, unsigned short);
void btext_progress(char *, unsigned short);

int _chrp_type;
EXPORT_SYMBOL(_chrp_type);

struct mpic *chrp_mpic;

/*
 * XXX this should be in xmon.h, but putting it there means xmon.h
 * has to include <linux/interrupt.h> (to get irqreturn_t), which
 * causes all sorts of problems.  -- paulus
 */
extern irqreturn_t xmon_irq(int, void *, struct pt_regs *);

extern unsigned long loops_per_jiffy;

#ifdef CONFIG_SMP
extern struct smp_ops_t chrp_smp_ops;
#endif

static const char *gg2_memtypes[4] = {
	"FPM", "SDRAM", "EDO", "BEDO"
};
static const char *gg2_cachesizes[4] = {
	"256 KB", "512 KB", "1 MB", "Reserved"
};
static const char *gg2_cachetypes[4] = {
	"Asynchronous", "Reserved", "Flow-Through Synchronous",
	"Pipelined Synchronous"
};
static const char *gg2_cachemodes[4] = {
	"Disabled", "Write-Through", "Copy-Back", "Transparent Mode"
};

void chrp_show_cpuinfo(struct seq_file *m)
{
	int i, sdramen;
	unsigned int t;
	struct device_node *root;
	const char *model = "";

	root = find_path_device("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);

	/* longtrail (goldengate) stuff */
	if (!strncmp(model, "IBM,LongTrail", 13)) {
		/* VLSI VAS96011/12 `Golden Gate 2' */
		/* Memory banks */
		sdramen = (in_le32(gg2_pci_config_base + GG2_PCI_DRAM_CTRL)
			   >>31) & 1;
		for (i = 0; i < (sdramen ? 4 : 6); i++) {
			t = in_le32(gg2_pci_config_base+
						 GG2_PCI_DRAM_BANK0+
						 i*4);
			if (!(t & 1))
				continue;
			switch ((t>>8) & 0x1f) {
			case 0x1f:
				model = "4 MB";
				break;
			case 0x1e:
				model = "8 MB";
				break;
			case 0x1c:
				model = "16 MB";
				break;
			case 0x18:
				model = "32 MB";
				break;
			case 0x10:
				model = "64 MB";
				break;
			case 0x00:
				model = "128 MB";
				break;
			default:
				model = "Reserved";
				break;
			}
			seq_printf(m, "memory bank %d\t: %s %s\n", i, model,
				   gg2_memtypes[sdramen ? 1 : ((t>>1) & 3)]);
		}
		/* L2 cache */
		t = in_le32(gg2_pci_config_base+GG2_PCI_CC_CTRL);
		seq_printf(m, "board l2\t: %s %s (%s)\n",
			   gg2_cachesizes[(t>>7) & 3],
			   gg2_cachetypes[(t>>2) & 3],
			   gg2_cachemodes[t & 3]);
	}
}

/*
 *  Fixes for the National Semiconductor PC78308VUL SuperI/O
 *
 *  Some versions of Open Firmware incorrectly initialize the IRQ settings
 *  for keyboard and mouse
 */
static inline void __init sio_write(u8 val, u8 index)
{
	outb(index, 0x15c);
	outb(val, 0x15d);
}

static inline u8 __init sio_read(u8 index)
{
	outb(index, 0x15c);
	return inb(0x15d);
}

static void __init sio_fixup_irq(const char *name, u8 device, u8 level,
				     u8 type)
{
	u8 level0, type0, active;

	/* select logical device */
	sio_write(device, 0x07);
	active = sio_read(0x30);
	level0 = sio_read(0x70);
	type0 = sio_read(0x71);
	if (level0 != level || type0 != type || !active) {
		printk(KERN_WARNING "sio: %s irq level %d, type %d, %sactive: "
		       "remapping to level %d, type %d, active\n",
		       name, level0, type0, !active ? "in" : "", level, type);
		sio_write(0x01, 0x30);
		sio_write(level, 0x70);
		sio_write(type, 0x71);
	}
}

static void __init sio_init(void)
{
	struct device_node *root;

	if ((root = find_path_device("/")) &&
	    !strncmp(get_property(root, "model", NULL), "IBM,LongTrail", 13)) {
		/* logical device 0 (KBC/Keyboard) */
		sio_fixup_irq("keyboard", 0, 1, 2);
		/* select logical device 1 (KBC/Mouse) */
		sio_fixup_irq("mouse", 1, 12, 2);
	}
}


static void __init pegasos_set_l2cr(void)
{
	struct device_node *np;

	/* On Pegasos, enable the l2 cache if needed, as the OF forgets it */
	if (_chrp_type != _CHRP_Pegasos)
		return;

	/* Enable L2 cache if needed */
	np = find_type_devices("cpu");
	if (np != NULL) {
		unsigned int *l2cr = (unsigned int *)
			get_property (np, "l2cr", NULL);
		if (l2cr == NULL) {
			printk ("Pegasos l2cr : no cpu l2cr property found\n");
			return;
		}
		if (!((*l2cr) & 0x80000000)) {
			printk ("Pegasos l2cr : L2 cache was not active, "
				"activating\n");
			_set_L2CR(0);
			_set_L2CR((*l2cr) | 0x80000000);
		}
	}
}

void __init chrp_setup_arch(void)
{
	struct device_node *root = find_path_device ("/");
	char *machine = NULL;
	struct device_node *device;
	unsigned int *p = NULL;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	if (root)
		machine = get_property(root, "model", NULL);
	if (machine && strncmp(machine, "Pegasos", 7) == 0) {
		_chrp_type = _CHRP_Pegasos;
	} else if (machine && strncmp(machine, "IBM", 3) == 0) {
		_chrp_type = _CHRP_IBM;
	} else if (machine && strncmp(machine, "MOT", 3) == 0) {
		_chrp_type = _CHRP_Motorola;
	} else {
		/* Let's assume it is an IBM chrp if all else fails */
		_chrp_type = _CHRP_IBM;
	}
	printk("chrp type = %x\n", _chrp_type);

	rtas_initialize();
	if (rtas_token("display-character") >= 0)
		ppc_md.progress = rtas_progress;

#ifdef CONFIG_BOOTX_TEXT
	if (ppc_md.progress == NULL && boot_text_mapped)
		ppc_md.progress = btext_progress;
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	/* this is fine for chrp */
	initrd_below_start_ok = 1;

	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
		ROOT_DEV = Root_SDA2; /* sda2 (sda1 is for the kernel) */

	/* On pegasos, enable the L2 cache if not already done by OF */
	pegasos_set_l2cr();

	/* Lookup PCI host bridges */
	chrp_find_bridges();

	/*
	 *  Temporary fixes for PCI devices.
	 *  -- Geert
	 */
	hydra_init();		/* Mac I/O */

	/*
	 *  Fix the Super I/O configuration
	 */
	sio_init();

	/* Get the event scan rate for the rtas so we know how
	 * often it expects a heartbeat. -- Cort
	 */
	device = find_devices("rtas");
	if (device)
		p = (unsigned int *) get_property
			(device, "rtas-event-scan-rate", NULL);
	if (p && *p) {
		ppc_md.heartbeat = chrp_event_scan;
		ppc_md.heartbeat_reset = HZ / (*p * 30) - 1;
		ppc_md.heartbeat_count = 1;
		printk("RTAS Event Scan Rate: %u (%lu jiffies)\n",
		       *p, ppc_md.heartbeat_reset);
	}

	pci_create_OF_bus_map();

	/*
	 * Print the banner, then scroll down so boot progress
	 * can be printed.  -- Cort
	 */
	if (ppc_md.progress) ppc_md.progress("Linux/PPC "UTS_RELEASE"\n", 0x0);
}

void
chrp_event_scan(void)
{
	unsigned char log[1024];
	int ret = 0;

	/* XXX: we should loop until the hardware says no more error logs -- Cort */
	rtas_call(rtas_token("event-scan"), 4, 1, &ret, 0xffffffff, 0,
		  __pa(log), 1024);
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;
}

/*
 * Finds the open-pic node and sets up the mpic driver.
 */
static void __init chrp_find_openpic(void)
{
	struct device_node *np, *root;
	int len, i, j, irq_count;
	int isu_size, idu_size;
	unsigned int *iranges, *opprop = NULL;
	int oplen = 0;
	unsigned long opaddr;
	int na = 1;
	unsigned char init_senses[NR_IRQS - NUM_8259_INTERRUPTS];

	np = find_type_devices("open-pic");
	if (np == NULL)
		return;
	root = find_path_device("/");
	if (root) {
		opprop = (unsigned int *) get_property
			(root, "platform-open-pic", &oplen);
		na = prom_n_addr_cells(root);
	}
	if (opprop && oplen >= na * sizeof(unsigned int)) {
		opaddr = opprop[na-1];	/* assume 32-bit */
		oplen /= na * sizeof(unsigned int);
	} else {
		if (np->n_addrs == 0)
			return;
		opaddr = np->addrs[0].address;
		oplen = 0;
	}

	printk(KERN_INFO "OpenPIC at %lx\n", opaddr);

	irq_count = NR_IRQS - NUM_ISA_INTERRUPTS - 4; /* leave room for IPIs */
	prom_get_irq_senses(init_senses, NUM_ISA_INTERRUPTS, NR_IRQS - 4);
	/* i8259 cascade is always positive level */
	init_senses[0] = IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE;

	iranges = (unsigned int *) get_property(np, "interrupt-ranges", &len);
	if (iranges == NULL)
		len = 0;	/* non-distributed mpic */
	else
		len /= 2 * sizeof(unsigned int);

	/*
	 * The first pair of cells in interrupt-ranges refers to the
	 * IDU; subsequent pairs refer to the ISUs.
	 */
	if (oplen < len) {
		printk(KERN_ERR "Insufficient addresses for distributed"
		       " OpenPIC (%d < %d)\n", np->n_addrs, len);
		len = oplen;
	}

	isu_size = 0;
	idu_size = 0;
	if (len > 0 && iranges[1] != 0) {
		printk(KERN_INFO "OpenPIC irqs %d..%d in IDU\n",
		       iranges[0], iranges[0] + iranges[1] - 1);
		idu_size = iranges[1];
	}
	if (len > 1)
		isu_size = iranges[3];

	chrp_mpic = mpic_alloc(opaddr, MPIC_PRIMARY,
			       isu_size, NUM_ISA_INTERRUPTS, irq_count,
			       NR_IRQS - 4, init_senses, irq_count,
			       " MPIC    ");
	if (chrp_mpic == NULL) {
		printk(KERN_ERR "Failed to allocate MPIC structure\n");
		return;
	}

	j = na - 1;
	for (i = 1; i < len; ++i) {
		iranges += 2;
		j += na;
		printk(KERN_INFO "OpenPIC irqs %d..%d in ISU at %x\n",
		       iranges[0], iranges[0] + iranges[1] - 1,
		       opprop[j]);
		mpic_assign_isu(chrp_mpic, i - 1, opprop[j]);
	}

	mpic_init(chrp_mpic);
	mpic_setup_cascade(NUM_ISA_INTERRUPTS, i8259_irq_cascade, NULL);
}

#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(XMON)
static struct irqaction xmon_irqaction = {
	.handler = xmon_irq,
	.mask = CPU_MASK_NONE,
	.name = "XMON break",
};
#endif

void __init chrp_init_IRQ(void)
{
	struct device_node *np;
	unsigned long chrp_int_ack = 0;
#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(XMON)
	struct device_node *kbd;
#endif

	for (np = find_devices("pci"); np != NULL; np = np->next) {
		unsigned int *addrp = (unsigned int *)
			get_property(np, "8259-interrupt-acknowledge", NULL);

		if (addrp == NULL)
			continue;
		chrp_int_ack = addrp[prom_n_addr_cells(np)-1];
		break;
	}
	if (np == NULL)
		printk(KERN_ERR "Cannot find PCI interrupt acknowledge address\n");

	chrp_find_openpic();

	i8259_init(chrp_int_ack, 0);

	if (_chrp_type == _CHRP_Pegasos)
		ppc_md.get_irq        = i8259_irq;
	else
		ppc_md.get_irq        = mpic_get_irq;

#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(XMON)
	/* see if there is a keyboard in the device tree
	   with a parent of type "adb" */
	for (kbd = find_devices("keyboard"); kbd; kbd = kbd->next)
		if (kbd->parent && kbd->parent->type
		    && strcmp(kbd->parent->type, "adb") == 0)
			break;
	if (kbd)
		setup_irq(HYDRA_INT_ADB_NMI, &xmon_irqaction);
#endif
}

void __init
chrp_init2(void)
{
#ifdef CONFIG_NVRAM
	chrp_nvram_init();
#endif

	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");

	if (ppc_md.progress)
		ppc_md.progress("  Have fun!    ", 0x7777);
}

void __init chrp_init(void)
{
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;
	isa_io_base = CHRP_ISA_IO_BASE;		/* default value */
	ppc_do_canonicalize_irqs = 1;

	/* Assume we have an 8259... */
	__irq_offset_value = NUM_ISA_INTERRUPTS;

	ppc_md.setup_arch     = chrp_setup_arch;
	ppc_md.show_cpuinfo   = chrp_show_cpuinfo;

	ppc_md.init_IRQ       = chrp_init_IRQ;
	ppc_md.init           = chrp_init2;

	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	ppc_md.restart        = rtas_restart;
	ppc_md.power_off      = rtas_power_off;
	ppc_md.halt           = rtas_halt;

	ppc_md.time_init      = chrp_time_init;
	ppc_md.set_rtc_time   = chrp_set_rtc_time;
	ppc_md.get_rtc_time   = chrp_get_rtc_time;
	ppc_md.calibrate_decr = chrp_calibrate_decr;

#ifdef CONFIG_SMP
	smp_ops = &chrp_smp_ops;
#endif /* CONFIG_SMP */
}

#ifdef CONFIG_BOOTX_TEXT
void
btext_progress(char *s, unsigned short hex)
{
	btext_drawstring(s);
	btext_drawstring("\n");
}
#endif /* CONFIG_BOOTX_TEXT */
