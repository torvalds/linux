/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 */

/*
 * bootup setup stuff..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <generated/utsrelease.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/timer.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/sections.h>
#include <asm/time.h>
#include <asm/i8259.h>
#include <asm/mpic.h>
#include <asm/rtas.h>
#include <asm/xmon.h>

#include "chrp.h"
#include "gg2.h"

void rtas_indicator_progress(char *, unsigned short);

int _chrp_type;
EXPORT_SYMBOL(_chrp_type);

static struct mpic *chrp_mpic;

/* Used for doing CHRP event-scans */
DEFINE_PER_CPU(struct timer_list, heartbeat_timer);
unsigned long event_scan_interval;

extern unsigned long loops_per_jiffy;

/* To be replaced by RTAS when available */
static unsigned int __iomem *briq_SPOR;

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

static const char *chrp_names[] = {
	"Unknown",
	"","","",
	"Motorola",
	"IBM or Longtrail",
	"Genesi Pegasos",
	"Total Impact Briq"
};

void chrp_show_cpuinfo(struct seq_file *m)
{
	int i, sdramen;
	unsigned int t;
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);

	/* longtrail (goldengate) stuff */
	if (model && !strncmp(model, "IBM,LongTrail", 13)) {
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
	of_node_put(root);
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
	const char *model;

	root = of_find_node_by_path("/");
	if (!root)
		return;

	model = of_get_property(root, "model", NULL);
	if (model && !strncmp(model, "IBM,LongTrail", 13)) {
		/* logical device 0 (KBC/Keyboard) */
		sio_fixup_irq("keyboard", 0, 1, 2);
		/* select logical device 1 (KBC/Mouse) */
		sio_fixup_irq("mouse", 1, 12, 2);
	}

	of_node_put(root);
}


static void __init pegasos_set_l2cr(void)
{
	struct device_node *np;

	/* On Pegasos, enable the l2 cache if needed, as the OF forgets it */
	if (_chrp_type != _CHRP_Pegasos)
		return;

	/* Enable L2 cache if needed */
	np = of_find_node_by_type(NULL, "cpu");
	if (np != NULL) {
		const unsigned int *l2cr = of_get_property(np, "l2cr", NULL);
		if (l2cr == NULL) {
			printk ("Pegasos l2cr : no cpu l2cr property found\n");
			goto out;
		}
		if (!((*l2cr) & 0x80000000)) {
			printk ("Pegasos l2cr : L2 cache was not active, "
				"activating\n");
			_set_L2CR(0);
			_set_L2CR((*l2cr) | 0x80000000);
		}
	}
out:
	of_node_put(np);
}

static void briq_restart(char *cmd)
{
	local_irq_disable();
	if (briq_SPOR)
		out_be32(briq_SPOR, 0);
	for(;;);
}

/*
 * Per default, input/output-device points to the keyboard/screen
 * If no card is installed, the built-in serial port is used as a fallback.
 * But unfortunately, the firmware does not connect /chosen/{stdin,stdout}
 * the the built-in serial node. Instead, a /failsafe node is created.
 */
static void chrp_init_early(void)
{
	struct device_node *node;
	const char *property;

	if (strstr(boot_command_line, "console="))
		return;
	/* find the boot console from /chosen/stdout */
	if (!of_chosen)
		return;
	node = of_find_node_by_path("/");
	if (!node)
		return;
	property = of_get_property(node, "model", NULL);
	if (!property)
		goto out_put;
	if (strcmp(property, "Pegasos2"))
		goto out_put;
	/* this is a Pegasos2 */
	property = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (!property)
		goto out_put;
	of_node_put(node);
	node = of_find_node_by_path(property);
	if (!node)
		return;
	property = of_get_property(node, "device_type", NULL);
	if (!property)
		goto out_put;
	if (strcmp(property, "serial"))
		goto out_put;
	/*
	 * The 9pin connector is either /failsafe
	 * or /pci@80000000/isa@C/serial@i2F8
	 * The optional graphics card has also type 'serial' in VGA mode.
	 */
	property = of_get_property(node, "name", NULL);
	if (!property)
		goto out_put;
	if (!strcmp(property, "failsafe") || !strcmp(property, "serial"))
		add_preferred_console("ttyS", 0, NULL);
out_put:
	of_node_put(node);
}

void __init chrp_setup_arch(void)
{
	struct device_node *root = of_find_node_by_path("/");
	const char *machine = NULL;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	if (root)
		machine = of_get_property(root, "model", NULL);
	if (machine && strncmp(machine, "Pegasos", 7) == 0) {
		_chrp_type = _CHRP_Pegasos;
	} else if (machine && strncmp(machine, "IBM", 3) == 0) {
		_chrp_type = _CHRP_IBM;
	} else if (machine && strncmp(machine, "MOT", 3) == 0) {
		_chrp_type = _CHRP_Motorola;
	} else if (machine && strncmp(machine, "TotalImpact,BRIQ-1", 18) == 0) {
		_chrp_type = _CHRP_briq;
		/* Map the SPOR register on briq and change the restart hook */
		briq_SPOR = ioremap(0xff0000e8, 4);
		ppc_md.restart = briq_restart;
	} else {
		/* Let's assume it is an IBM chrp if all else fails */
		_chrp_type = _CHRP_IBM;
	}
	of_node_put(root);
	printk("chrp type = %x [%s]\n", _chrp_type, chrp_names[_chrp_type]);

	rtas_initialize();
	if (rtas_token("display-character") >= 0)
		ppc_md.progress = rtas_progress;

	/* use RTAS time-of-day routines if available */
	if (rtas_token("get-time-of-day") != RTAS_UNKNOWN_SERVICE) {
		ppc_md.get_boot_time	= rtas_get_boot_time;
		ppc_md.get_rtc_time	= rtas_get_rtc_time;
		ppc_md.set_rtc_time	= rtas_set_rtc_time;
	}

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

	pci_create_OF_bus_map();

	/*
	 * Print the banner, then scroll down so boot progress
	 * can be printed.  -- Cort
	 */
	if (ppc_md.progress) ppc_md.progress("Linux/PPC "UTS_RELEASE"\n", 0x0);
}

static void chrp_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

/*
 * Finds the open-pic node and sets up the mpic driver.
 */
static void __init chrp_find_openpic(void)
{
	struct device_node *np, *root;
	int len, i, j;
	int isu_size, idu_size;
	const unsigned int *iranges, *opprop = NULL;
	int oplen = 0;
	unsigned long opaddr;
	int na = 1;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL)
		return;
	root = of_find_node_by_path("/");
	if (root) {
		opprop = of_get_property(root, "platform-open-pic", &oplen);
		na = of_n_addr_cells(root);
	}
	if (opprop && oplen >= na * sizeof(unsigned int)) {
		opaddr = opprop[na-1];	/* assume 32-bit */
		oplen /= na * sizeof(unsigned int);
	} else {
		struct resource r;
		if (of_address_to_resource(np, 0, &r)) {
			goto bail;
		}
		opaddr = r.start;
		oplen = 0;
	}

	printk(KERN_INFO "OpenPIC at %lx\n", opaddr);

	iranges = of_get_property(np, "interrupt-ranges", &len);
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
		       " OpenPIC (%d < %d)\n", oplen, len);
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

	chrp_mpic = mpic_alloc(np, opaddr, MPIC_NO_RESET,
			isu_size, 0, " MPIC    ");
	if (chrp_mpic == NULL) {
		printk(KERN_ERR "Failed to allocate MPIC structure\n");
		goto bail;
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
	ppc_md.get_irq = mpic_get_irq;
 bail:
	of_node_put(root);
	of_node_put(np);
}

#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_XMON)
static struct irqaction xmon_irqaction = {
	.handler = xmon_irq,
	.name = "XMON break",
};
#endif

static void __init chrp_find_8259(void)
{
	struct device_node *np, *pic = NULL;
	unsigned long chrp_int_ack = 0;
	unsigned int cascade_irq;

	/* Look for cascade */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			pic = np;
			break;
		}
	/* Ok, 8259 wasn't found. We need to handle the case where
	 * we have a pegasos that claims to be chrp but doesn't have
	 * a proper interrupt tree
	 */
	if (pic == NULL && chrp_mpic != NULL) {
		printk(KERN_ERR "i8259: Not found in device-tree"
		       " assuming no legacy interrupts\n");
		return;
	}

	/* Look for intack. In a perfect world, we would look for it on
	 * the ISA bus that holds the 8259 but heh... Works that way. If
	 * we ever see a problem, we can try to re-use the pSeries code here.
	 * Also, Pegasos-type platforms don't have a proper node to start
	 * from anyway
	 */
	for_each_node_by_name(np, "pci") {
		const unsigned int *addrp = of_get_property(np,
				"8259-interrupt-acknowledge", NULL);

		if (addrp == NULL)
			continue;
		chrp_int_ack = addrp[of_n_addr_cells(np)-1];
		break;
	}
	of_node_put(np);
	if (np == NULL)
		printk(KERN_WARNING "Cannot find PCI interrupt acknowledge"
		       " address, polling\n");

	i8259_init(pic, chrp_int_ack);
	if (ppc_md.get_irq == NULL) {
		ppc_md.get_irq = i8259_irq;
		irq_set_default_host(i8259_get_host());
	}
	if (chrp_mpic != NULL) {
		cascade_irq = irq_of_parse_and_map(pic, 0);
		if (cascade_irq == NO_IRQ)
			printk(KERN_ERR "i8259: failed to map cascade irq\n");
		else
			irq_set_chained_handler(cascade_irq,
						chrp_8259_cascade);
	}
}

void __init chrp_init_IRQ(void)
{
#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_XMON)
	struct device_node *kbd;
#endif
	chrp_find_openpic();
	chrp_find_8259();

#ifdef CONFIG_SMP
	/* Pegasos has no MPIC, those ops would make it crash. It might be an
	 * option to move setting them to after we probe the PIC though
	 */
	if (chrp_mpic != NULL)
		smp_ops = &chrp_smp_ops;
#endif /* CONFIG_SMP */

	if (_chrp_type == _CHRP_Pegasos)
		ppc_md.get_irq        = i8259_irq;

#if defined(CONFIG_VT) && defined(CONFIG_INPUT_ADBHID) && defined(CONFIG_XMON)
	/* see if there is a keyboard in the device tree
	   with a parent of type "adb" */
	for_each_node_by_name(kbd, "keyboard")
		if (kbd->parent && kbd->parent->type
		    && strcmp(kbd->parent->type, "adb") == 0)
			break;
	of_node_put(kbd);
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

static int __init chrp_probe(void)
{
	const char *dtype = of_get_flat_dt_prop(of_get_flat_dt_root(),
						"device_type", NULL);
 	if (dtype == NULL)
 		return 0;
 	if (strcmp(dtype, "chrp"))
		return 0;

	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	return 1;
}

define_machine(chrp) {
	.name			= "CHRP",
	.probe			= chrp_probe,
	.setup_arch		= chrp_setup_arch,
	.init			= chrp_init2,
	.init_early		= chrp_init_early,
	.show_cpuinfo		= chrp_show_cpuinfo,
	.init_IRQ		= chrp_init_IRQ,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.time_init		= chrp_time_init,
	.set_rtc_time		= chrp_set_rtc_time,
	.get_rtc_time		= chrp_get_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.phys_mem_access_prot	= pci_phys_mem_access_prot,
};
