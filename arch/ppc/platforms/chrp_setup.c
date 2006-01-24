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
#include <asm/open_pic.h>
#include <asm/xmon.h>
#include "mem_pieces.h"

unsigned long chrp_get_rtc_time(void);
int chrp_set_rtc_time(unsigned long nowtime);
void chrp_calibrate_decr(void);
long chrp_time_init(void);

void chrp_find_bridges(void);
void chrp_event_scan(void);
void rtas_display_progress(char *, unsigned short);
void rtas_indicator_progress(char *, unsigned short);
void btext_progress(char *, unsigned short);

extern int of_show_percpuinfo(struct seq_file *, int);

int _chrp_type;
EXPORT_SYMBOL(_chrp_type);

/*
 * XXX this should be in xmon.h, but putting it there means xmon.h
 * has to include <linux/interrupt.h> (to get irqreturn_t), which
 * causes all sorts of problems.  -- paulus
 */
extern irqreturn_t xmon_irq(int, void *, struct pt_regs *);

extern dev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_jiffy;
static int max_width;

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

int
chrp_show_cpuinfo(struct seq_file *m)
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
	return 0;
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
	struct device_node *device;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

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

#ifndef CONFIG_PPC64BRIDGE
	/*
	 *  Temporary fixes for PCI devices.
	 *  -- Geert
	 */
	hydra_init();		/* Mac I/O */

#endif /* CONFIG_PPC64BRIDGE */

	/*
	 *  Fix the Super I/O configuration
	 */
	sio_init();

	/* Get the event scan rate for the rtas so we know how
	 * often it expects a heartbeat. -- Cort
	 */
	if ( rtas_data ) {
		struct property *p;
		device = find_devices("rtas");
		for ( p = device->properties;
		      p && strncmp(p->name, "rtas-event-scan-rate", 20);
		      p = p->next )
			/* nothing */ ;
		if ( p && *(unsigned long *)p->value ) {
			ppc_md.heartbeat = chrp_event_scan;
			ppc_md.heartbeat_reset = (HZ/(*(unsigned long *)p->value)*30)-1;
			ppc_md.heartbeat_count = 1;
			printk("RTAS Event Scan Rate: %lu (%lu jiffies)\n",
			       *(unsigned long *)p->value, ppc_md.heartbeat_reset );
		}
	}

	pci_create_OF_bus_map();
}

void
chrp_event_scan(void)
{
	unsigned char log[1024];
	unsigned long ret = 0;
	/* XXX: we should loop until the hardware says no more error logs -- Cort */
	call_rtas( "event-scan", 4, 1, &ret, 0xffffffff, 0,
		   __pa(log), 1024 );
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;
}

void
chrp_restart(char *cmd)
{
	printk("RTAS system-reboot returned %d\n",
	       call_rtas("system-reboot", 0, 1, NULL));
	for (;;);
}

void
chrp_power_off(void)
{
	/* allow power on only with power button press */
	printk("RTAS power-off returned %d\n",
	       call_rtas("power-off", 2, 1, NULL,0xffffffff,0xffffffff));
	for (;;);
}

void
chrp_halt(void)
{
	chrp_power_off();
}

/*
 * Finds the open-pic node and sets OpenPIC_Addr based on its reg property.
 * Then checks if it has an interrupt-ranges property.  If it does then
 * we have a distributed open-pic, so call openpic_set_sources to tell
 * the openpic code where to find the interrupt source registers.
 */
static void __init chrp_find_openpic(void)
{
	struct device_node *np;
	int len, i;
	unsigned int *iranges;
	void __iomem *isu;

	np = find_type_devices("open-pic");
	if (np == NULL || np->n_addrs == 0)
		return;
	printk(KERN_INFO "OpenPIC at %x (size %x)\n",
	       np->addrs[0].address, np->addrs[0].size);
	OpenPIC_Addr = ioremap(np->addrs[0].address, 0x40000);
	if (OpenPIC_Addr == NULL) {
		printk(KERN_ERR "Failed to map OpenPIC!\n");
		return;
	}

	iranges = (unsigned int *) get_property(np, "interrupt-ranges", &len);
	if (iranges == NULL || len < 2 * sizeof(unsigned int))
		return;		/* not distributed */

	/*
	 * The first pair of cells in interrupt-ranges refers to the
	 * IDU; subsequent pairs refer to the ISUs.
	 */
	len /= 2 * sizeof(unsigned int);
	if (np->n_addrs < len) {
		printk(KERN_ERR "Insufficient addresses for distributed"
		       " OpenPIC (%d < %d)\n", np->n_addrs, len);
		return;
	}
	if (iranges[1] != 0) {
		printk(KERN_INFO "OpenPIC irqs %d..%d in IDU\n",
		       iranges[0], iranges[0] + iranges[1] - 1);
		openpic_set_sources(iranges[0], iranges[1], NULL);
	}
	for (i = 1; i < len; ++i) {
		iranges += 2;
		printk(KERN_INFO "OpenPIC irqs %d..%d in ISU at %x (%x)\n",
		       iranges[0], iranges[0] + iranges[1] - 1,
		       np->addrs[i].address, np->addrs[i].size);
		isu = ioremap(np->addrs[i].address, np->addrs[i].size);
		if (isu != NULL)
			openpic_set_sources(iranges[0], iranges[1], isu);
		else
			printk(KERN_ERR "Failed to map OpenPIC ISU at %x!\n",
			       np->addrs[i].address);
	}
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
	unsigned char init_senses[NR_IRQS - NUM_8259_INTERRUPTS];
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

	if (OpenPIC_Addr) {
		prom_get_irq_senses(init_senses, NUM_8259_INTERRUPTS, NR_IRQS);
		OpenPIC_InitSenses = init_senses;
		OpenPIC_NumInitSenses = NR_IRQS - NUM_8259_INTERRUPTS;

		openpic_init(NUM_8259_INTERRUPTS);
		/* We have a cascade on OpenPIC IRQ 0, Linux IRQ 16 */
		openpic_hookup_cascade(NUM_8259_INTERRUPTS, "82c59 cascade",
				       i8259_irq);

	}
	i8259_init(chrp_int_ack, 0);

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

static struct device_node *memory_node;

static int __init get_mem_prop(char *name, struct mem_pieces *mp)
{
	struct reg_property *rp;
	int i, s;
	unsigned int *ip;
	int nac = prom_n_addr_cells(memory_node);
	int nsc = prom_n_size_cells(memory_node);

	ip = (unsigned int *) get_property(memory_node, name, &s);
	if (ip == NULL) {
		printk(KERN_ERR "error: couldn't get %s property on /memory\n",
		       name);
		return 0;
	}
	s /= (nsc + nac) * 4;
	rp = mp->regions;
	for (i = 0; i < s; ++i, ip += nac+nsc) {
		if (nac >= 2 && ip[nac-2] != 0)
			continue;
		rp->address = ip[nac-1];
		if (nsc >= 2 && ip[nac+nsc-2] != 0)
			rp->size = ~0U;
		else
			rp->size = ip[nac+nsc-1];
		++rp;
	}
	mp->n_regions = rp - mp->regions;

	/* Make sure the pieces are sorted. */
	mem_pieces_sort(mp);
	mem_pieces_coalesce(mp);
	return 1;
}

static unsigned long __init chrp_find_end_of_memory(void)
{
	unsigned long a, total;
	struct mem_pieces phys_mem;

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes.
	 *
	 * Supporting discontiguous physical memory isn't hard,
	 * it just makes the virtual <-> physical mapping functions
	 * more complicated (or else you end up wasting space
	 * in mem_map).
	 */
	memory_node = find_devices("memory");
	if (memory_node == NULL || !get_mem_prop("reg", &phys_mem)
	    || phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;

	if (phys_mem.n_regions > 1) {
		printk("RAM starting at 0x%x is not contiguous\n",
		       phys_mem.regions[1].address);
		printk("Using RAM from 0 to 0x%lx\n", total-1);
	}

	return total;
}

void __init
chrp_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
	struct device_node *root = find_path_device ("/");
	char *machine = NULL;

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r6 )
	{
		initrd_start = r6 + KERNELBASE;
		initrd_end = r6 + r7 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;
	isa_io_base = CHRP_ISA_IO_BASE;		/* default value */
	ppc_do_canonicalize_irqs = 1;

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

	ppc_md.setup_arch     = chrp_setup_arch;
	ppc_md.show_percpuinfo = of_show_percpuinfo;
	ppc_md.show_cpuinfo   = chrp_show_cpuinfo;

	ppc_md.init_IRQ       = chrp_init_IRQ;
	if (_chrp_type == _CHRP_Pegasos)
		ppc_md.get_irq        = i8259_irq;
	else
		ppc_md.get_irq        = openpic_get_irq;

	ppc_md.init           = chrp_init2;

	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	ppc_md.restart        = chrp_restart;
	ppc_md.power_off      = chrp_power_off;
	ppc_md.halt           = chrp_halt;

	ppc_md.time_init      = chrp_time_init;
	ppc_md.set_rtc_time   = chrp_set_rtc_time;
	ppc_md.get_rtc_time   = chrp_get_rtc_time;
	ppc_md.calibrate_decr = chrp_calibrate_decr;

	ppc_md.find_end_of_memory = chrp_find_end_of_memory;

	if (rtas_data) {
		struct device_node *rtas;
		unsigned int *p;

		rtas = find_devices("rtas");
		if (rtas != NULL) {
			if (get_property(rtas, "display-character", NULL)) {
				ppc_md.progress = rtas_display_progress;
				p = (unsigned int *) get_property
				       (rtas, "ibm,display-line-length", NULL);
				if (p)
					max_width = *p;
			} else if (get_property(rtas, "set-indicator", NULL))
				ppc_md.progress = rtas_indicator_progress;
		}
	}
#ifdef CONFIG_BOOTX_TEXT
	if (ppc_md.progress == NULL && boot_text_mapped)
		ppc_md.progress = btext_progress;
#endif

#ifdef CONFIG_SMP
	smp_ops = &chrp_smp_ops;
#endif /* CONFIG_SMP */

	/*
	 * Print the banner, then scroll down so boot progress
	 * can be printed.  -- Cort
	 */
	if (ppc_md.progress) ppc_md.progress("Linux/PPC "UTS_RELEASE"\n", 0x0);
}

void
rtas_display_progress(char *s, unsigned short hex)
{
	int width;
	char *os = s;

	if ( call_rtas( "display-character", 1, 1, NULL, '\r' ) )
		return;

	width = max_width;
	while ( *os )
	{
		if ( (*os == '\n') || (*os == '\r') )
			width = max_width;
		else
			width--;
		call_rtas( "display-character", 1, 1, NULL, *os++ );
		/* if we overwrite the screen length */
		if ( width == 0 )
			while ( (*os != 0) && (*os != '\n') && (*os != '\r') )
				os++;
	}

	/*while ( width-- > 0 )*/
	call_rtas( "display-character", 1, 1, NULL, ' ' );
}

void
rtas_indicator_progress(char *s, unsigned short hex)
{
	call_rtas("set-indicator", 3, 1, NULL, 6, 0, hex);
}

#ifdef CONFIG_BOOTX_TEXT
void
btext_progress(char *s, unsigned short hex)
{
	prom_print(s);
	prom_print("\n");
}
#endif /* CONFIG_BOOTX_TEXT */
