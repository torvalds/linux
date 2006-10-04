/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/screen_info.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/root_dev.h>
#include <linux/nodemask.h>
#include <linux/pm.h>
#include <linux/efi.h>

#include <asm/io.h>
#include <asm/sal.h>
#include <asm/machvec.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/vga.h>
#include <asm/sn/arch.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pda.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/simulator.h>
#include <asm/sn/leds.h>
#include <asm/sn/bte.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/geo.h>
#include <asm/sn/sn_feature_sets.h>
#include "xtalk/xwidgetdev.h"
#include "xtalk/hubdev.h"
#include <asm/sn/klconfig.h>


DEFINE_PER_CPU(struct pda_s, pda_percpu);

#define MAX_PHYS_MEMORY		(1UL << IA64_MAX_PHYS_BITS)	/* Max physical address supported */

extern void bte_init_node(nodepda_t *, cnodeid_t);

extern void sn_timer_init(void);
extern unsigned long last_time_offset;
extern void (*ia64_mark_idle) (int);
extern void snidle(int);
extern unsigned long long (*ia64_printk_clock)(void);

unsigned long sn_rtc_cycles_per_second;
EXPORT_SYMBOL(sn_rtc_cycles_per_second);

DEFINE_PER_CPU(struct sn_hub_info_s, __sn_hub_info);
EXPORT_PER_CPU_SYMBOL(__sn_hub_info);

DEFINE_PER_CPU(short, __sn_cnodeid_to_nasid[MAX_COMPACT_NODES]);
EXPORT_PER_CPU_SYMBOL(__sn_cnodeid_to_nasid);

DEFINE_PER_CPU(struct nodepda_s *, __sn_nodepda);
EXPORT_PER_CPU_SYMBOL(__sn_nodepda);

char sn_system_serial_number_string[128];
EXPORT_SYMBOL(sn_system_serial_number_string);
u64 sn_partition_serial_number;
EXPORT_SYMBOL(sn_partition_serial_number);
u8 sn_partition_id;
EXPORT_SYMBOL(sn_partition_id);
u8 sn_system_size;
EXPORT_SYMBOL(sn_system_size);
u8 sn_sharing_domain_size;
EXPORT_SYMBOL(sn_sharing_domain_size);
u8 sn_coherency_id;
EXPORT_SYMBOL(sn_coherency_id);
u8 sn_region_size;
EXPORT_SYMBOL(sn_region_size);
int sn_prom_type;	/* 0=hardware, 1=medusa/realprom, 2=medusa/fakeprom */

short physical_node_map[MAX_NUMALINK_NODES];
static unsigned long sn_prom_features[MAX_PROM_FEATURE_SETS];

EXPORT_SYMBOL(physical_node_map);

int num_cnodes;

static void sn_init_pdas(char **);
static void build_cnode_tables(void);

static nodepda_t *nodepdaindr[MAX_COMPACT_NODES];

/*
 * The format of "screen_info" is strange, and due to early i386-setup
 * code. This is just enough to make the console code think we're on a
 * VGA color display.
 */
struct screen_info sn_screen_info = {
	.orig_x = 0,
	.orig_y = 0,
	.orig_video_mode = 3,
	.orig_video_cols = 80,
	.orig_video_ega_bx = 3,
	.orig_video_lines = 25,
	.orig_video_isVGA = 1,
	.orig_video_points = 16
};

/*
 * This routine can only be used during init, since
 * smp_boot_data is an init data structure.
 * We have to use smp_boot_data.cpu_phys_id to find
 * the physical id of the processor because the normal
 * cpu_physical_id() relies on data structures that
 * may not be initialized yet.
 */

static int __init pxm_to_nasid(int pxm)
{
	int i;
	int nid;

	nid = pxm_to_node(pxm);
	for (i = 0; i < num_node_memblks; i++) {
		if (node_memblk[i].nid == nid) {
			return NASID_GET(node_memblk[i].start_paddr);
		}
	}
	return -1;
}

/**
 * early_sn_setup - early setup routine for SN platforms
 *
 * Sets up an initial console to aid debugging.  Intended primarily
 * for bringup.  See start_kernel() in init/main.c.
 */

void __init early_sn_setup(void)
{
	efi_system_table_t *efi_systab;
	efi_config_table_t *config_tables;
	struct ia64_sal_systab *sal_systab;
	struct ia64_sal_desc_entry_point *ep;
	char *p;
	int i, j;

	/*
	 * Parse enough of the SAL tables to locate the SAL entry point. Since, console
	 * IO on SN2 is done via SAL calls, early_printk won't work without this.
	 *
	 * This code duplicates some of the ACPI table parsing that is in efi.c & sal.c.
	 * Any changes to those file may have to be made hereas well.
	 */
	efi_systab = (efi_system_table_t *) __va(ia64_boot_param->efi_systab);
	config_tables = __va(efi_systab->tables);
	for (i = 0; i < efi_systab->nr_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, SAL_SYSTEM_TABLE_GUID) ==
		    0) {
			sal_systab = __va(config_tables[i].table);
			p = (char *)(sal_systab + 1);
			for (j = 0; j < sal_systab->entry_count; j++) {
				if (*p == SAL_DESC_ENTRY_POINT) {
					ep = (struct ia64_sal_desc_entry_point
					      *)p;
					ia64_sal_handler_init(__va
							      (ep->sal_proc),
							      __va(ep->gp));
					return;
				}
				p += SAL_DESC_SIZE(*p);
			}
		}
	}
	/* Uh-oh, SAL not available?? */
	printk(KERN_ERR "failed to find SAL entry point\n");
}

extern int platform_intr_list[];
static int __initdata shub_1_1_found;

/*
 * sn_check_for_wars
 *
 * Set flag for enabling shub specific wars
 */

static inline int __init is_shub_1_1(int nasid)
{
	unsigned long id;
	int rev;

	if (is_shub2())
		return 0;
	id = REMOTE_HUB_L(nasid, SH1_SHUB_ID);
	rev = (id & SH1_SHUB_ID_REVISION_MASK) >> SH1_SHUB_ID_REVISION_SHFT;
	return rev <= 2;
}

static void __init sn_check_for_wars(void)
{
	int cnode;

	if (is_shub2()) {
		/* none yet */
	} else {
		for_each_online_node(cnode) {
			if (is_shub_1_1(cnodeid_to_nasid(cnode)))
				shub_1_1_found = 1;
		}
	}
}

/*
 * Scan the EFI PCDP table (if it exists) for an acceptable VGA console
 * output device.  If one exists, pick it and set sn_legacy_{io,mem} to
 * reflect the bus offsets needed to address it.
 *
 * Since pcdp support in SN is not supported in the 2.4 kernel (or at least
 * the one lbs is based on) just declare the needed structs here.
 *
 * Reference spec http://www.dig64.org/specifications/DIG64_PCDPv20.pdf
 *
 * Returns 0 if no acceptable vga is found, !0 otherwise.
 *
 * Note:  This stuff is duped here because Altix requires the PCDP to
 * locate a usable VGA device due to lack of proper ACPI support.  Structures
 * could be used from drivers/firmware/pcdp.h, but it was decided that moving
 * this file to a more public location just for Altix use was undesireable.
 */

struct hcdp_uart_desc {
	u8	pad[45];
};

struct pcdp {
	u8	signature[4];	/* should be 'HCDP' */
	u32	length;
	u8	rev;		/* should be >=3 for pcdp, <3 for hcdp */
	u8	sum;
	u8	oem_id[6];
	u64	oem_tableid;
	u32	oem_rev;
	u32	creator_id;
	u32	creator_rev;
	u32	num_type0;
	struct hcdp_uart_desc uart[0];	/* num_type0 of these */
	/* pcdp descriptors follow */
}  __attribute__((packed));

struct pcdp_device_desc {
	u8	type;
	u8	primary;
	u16	length;
	u16	index;
	/* interconnect specific structure follows */
	/* device specific structure follows that */
}  __attribute__((packed));

struct pcdp_interface_pci {
	u8	type;		/* 1 == pci */
	u8	reserved;
	u16	length;
	u8	segment;
	u8	bus;
	u8 	dev;
	u8	fun;
	u16	devid;
	u16	vendid;
	u32	acpi_interrupt;
	u64	mmio_tra;
	u64	ioport_tra;
	u8	flags;
	u8	translation;
}  __attribute__((packed));

struct pcdp_vga_device {
	u8	num_eas_desc;
	/* ACPI Extended Address Space Desc follows */
}  __attribute__((packed));

/* from pcdp_device_desc.primary */
#define PCDP_PRIMARY_CONSOLE	0x01

/* from pcdp_device_desc.type */
#define PCDP_CONSOLE_INOUT	0x0
#define PCDP_CONSOLE_DEBUG	0x1
#define PCDP_CONSOLE_OUT	0x2
#define PCDP_CONSOLE_IN		0x3
#define PCDP_CONSOLE_TYPE_VGA	0x8

#define PCDP_CONSOLE_VGA	(PCDP_CONSOLE_TYPE_VGA | PCDP_CONSOLE_OUT)

/* from pcdp_interface_pci.type */
#define PCDP_IF_PCI		1

/* from pcdp_interface_pci.translation */
#define PCDP_PCI_TRANS_IOPORT	0x02
#define PCDP_PCI_TRANS_MMIO	0x01

#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE)
static void
sn_scan_pcdp(void)
{
	u8 *bp;
	struct pcdp *pcdp;
	struct pcdp_device_desc device;
	struct pcdp_interface_pci if_pci;
	extern struct efi efi;

	if (efi.hcdp == EFI_INVALID_TABLE_ADDR)
		return;		/* no hcdp/pcdp table */

	pcdp = __va(efi.hcdp);

	if (pcdp->rev < 3)
		return;		/* only support PCDP (rev >= 3) */

	for (bp = (u8 *)&pcdp->uart[pcdp->num_type0];
	     bp < (u8 *)pcdp + pcdp->length;
	     bp += device.length) {
		memcpy(&device, bp, sizeof(device));
		if (! (device.primary & PCDP_PRIMARY_CONSOLE))
			continue;	/* not primary console */

		if (device.type != PCDP_CONSOLE_VGA)
			continue;	/* not VGA descriptor */

		memcpy(&if_pci, bp+sizeof(device), sizeof(if_pci));
		if (if_pci.type != PCDP_IF_PCI)
			continue;	/* not PCI interconnect */

		if (if_pci.translation & PCDP_PCI_TRANS_IOPORT)
			vga_console_iobase =
				if_pci.ioport_tra | __IA64_UNCACHED_OFFSET;

		if (if_pci.translation & PCDP_PCI_TRANS_MMIO)
			vga_console_membase =
				if_pci.mmio_tra | __IA64_UNCACHED_OFFSET;

		break; /* once we find the primary, we're done */
	}
}
#endif

static unsigned long sn2_rtc_initial;

static unsigned long long ia64_sn2_printk_clock(void)
{
	unsigned long rtc_now = rtc_time();

	return (rtc_now - sn2_rtc_initial) *
		(1000000000 / sn_rtc_cycles_per_second);
}

/**
 * sn_setup - SN platform setup routine
 * @cmdline_p: kernel command line
 *
 * Handles platform setup for SN machines.  This includes determining
 * the RTC frequency (via a SAL call), initializing secondary CPUs, and
 * setting up per-node data areas.  The console is also initialized here.
 */
void __init sn_setup(char **cmdline_p)
{
	long status, ticks_per_sec, drift;
	u32 version = sn_sal_rev();
	extern void sn_cpu_init(void);

	sn2_rtc_initial = rtc_time();
	ia64_sn_plat_set_error_handling_features();	// obsolete
	ia64_sn_set_os_feature(OSF_MCA_SLV_TO_OS_INIT_SLV);
	ia64_sn_set_os_feature(OSF_FEAT_LOG_SBES);
	/*
	 * Note: The calls to notify the PROM of ACPI and PCI Segment
	 *	 support must be done prior to acpi_load_tables(), as
	 *	 an ACPI capable PROM will rebuild the DSDT as result
	 *	 of the call.
	 */
	ia64_sn_set_os_feature(OSF_PCISEGMENT_ENABLE);
	ia64_sn_set_os_feature(OSF_ACPI_ENABLE);


#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE)
	/*
	 * Handle SN vga console.
	 *
	 * SN systems do not have enough ACPI table information
	 * being passed from prom to identify VGA adapters and the legacy
	 * addresses to access them.  Until that is done, SN systems rely
	 * on the PCDP table to identify the primary VGA console if one
	 * exists.
	 *
	 * However, kernel PCDP support is optional, and even if it is built
	 * into the kernel, it will not be used if the boot cmdline contains
	 * console= directives.
	 *
	 * So, to work around this mess, we duplicate some of the PCDP code
	 * here so that the primary VGA console (as defined by PCDP) will
	 * work on SN systems even if a different console (e.g. serial) is
	 * selected on the boot line (or CONFIG_EFI_PCDP is off).
	 */

	if (! vga_console_membase)
		sn_scan_pcdp();

	/*
	 *	Setup legacy IO space.
	 *	vga_console_iobase maps to PCI IO Space address 0 on the
	 * 	bus containing the VGA console.
	 */
	if (vga_console_iobase) {
		io_space[0].mmio_base = vga_console_iobase;
		io_space[0].sparse = 0;
	}

	if (vga_console_membase) {
		/* usable vga ... make tty0 the preferred default console */
		if (!strstr(*cmdline_p, "console="))
			add_preferred_console("tty", 0, NULL);
	} else {
		printk(KERN_DEBUG "SGI: Disabling VGA console\n");
		if (!strstr(*cmdline_p, "console="))
			add_preferred_console("ttySG", 0, NULL);
#ifdef CONFIG_DUMMY_CONSOLE
		conswitchp = &dummy_con;
#else
		conswitchp = NULL;
#endif				/* CONFIG_DUMMY_CONSOLE */
	}
#endif				/* def(CONFIG_VT) && def(CONFIG_VGA_CONSOLE) */

	MAX_DMA_ADDRESS = PAGE_OFFSET + MAX_PHYS_MEMORY;

	/*
	 * Build the tables for managing cnodes.
	 */
	build_cnode_tables();

	status =
	    ia64_sal_freq_base(SAL_FREQ_BASE_REALTIME_CLOCK, &ticks_per_sec,
			       &drift);
	if (status != 0 || ticks_per_sec < 100000) {
		printk(KERN_WARNING
		       "unable to determine platform RTC clock frequency, guessing.\n");
		/* PROM gives wrong value for clock freq. so guess */
		sn_rtc_cycles_per_second = 1000000000000UL / 30000UL;
	} else
		sn_rtc_cycles_per_second = ticks_per_sec;

	platform_intr_list[ACPI_INTERRUPT_CPEI] = IA64_CPE_VECTOR;

	ia64_printk_clock = ia64_sn2_printk_clock;

	printk("SGI SAL version %x.%02x\n", version >> 8, version & 0x00FF);

	/*
	 * we set the default root device to /dev/hda
	 * to make simulation easy
	 */
	ROOT_DEV = Root_HDA1;

	/*
	 * Create the PDAs and NODEPDAs for all the cpus.
	 */
	sn_init_pdas(cmdline_p);

	ia64_mark_idle = &snidle;

	/*
	 * For the bootcpu, we do this here. All other cpus will make the
	 * call as part of cpu_init in slave cpu initialization.
	 */
	sn_cpu_init();

#ifdef CONFIG_SMP
	init_smp_config();
#endif
	screen_info = sn_screen_info;

	sn_timer_init();

	/*
	 * set pm_power_off to a SAL call to allow
	 * sn machines to power off. The SAL call can be replaced
	 * by an ACPI interface call when ACPI is fully implemented
	 * for sn.
	 */
	pm_power_off = ia64_sn_power_down;
	current->thread.flags |= IA64_THREAD_MIGRATION;
}

/**
 * sn_init_pdas - setup node data areas
 *
 * One time setup for Node Data Area.  Called by sn_setup().
 */
static void __init sn_init_pdas(char **cmdline_p)
{
	cnodeid_t cnode;

	/*
	 * Allocate & initalize the nodepda for each node.
	 */
	for_each_online_node(cnode) {
		nodepdaindr[cnode] =
		    alloc_bootmem_node(NODE_DATA(cnode), sizeof(nodepda_t));
		memset(nodepdaindr[cnode], 0, sizeof(nodepda_t));
		memset(nodepdaindr[cnode]->phys_cpuid, -1,
		    sizeof(nodepdaindr[cnode]->phys_cpuid));
		spin_lock_init(&nodepdaindr[cnode]->ptc_lock);
	}

	/*
	 * Allocate & initialize nodepda for TIOs.  For now, put them on node 0.
	 */
	for (cnode = num_online_nodes(); cnode < num_cnodes; cnode++) {
		nodepdaindr[cnode] =
		    alloc_bootmem_node(NODE_DATA(0), sizeof(nodepda_t));
		memset(nodepdaindr[cnode], 0, sizeof(nodepda_t));
	}

	/*
	 * Now copy the array of nodepda pointers to each nodepda.
	 */
	for (cnode = 0; cnode < num_cnodes; cnode++)
		memcpy(nodepdaindr[cnode]->pernode_pdaindr, nodepdaindr,
		       sizeof(nodepdaindr));

	/*
	 * Set up IO related platform-dependent nodepda fields.
	 * The following routine actually sets up the hubinfo struct
	 * in nodepda.
	 */
	for_each_online_node(cnode) {
		bte_init_node(nodepdaindr[cnode], cnode);
	}

	/*
	 * Initialize the per node hubdev.  This includes IO Nodes and
	 * headless/memless nodes.
	 */
	for (cnode = 0; cnode < num_cnodes; cnode++) {
		hubdev_init_node(nodepdaindr[cnode], cnode);
	}
}

/**
 * sn_cpu_init - initialize per-cpu data areas
 * @cpuid: cpuid of the caller
 *
 * Called during cpu initialization on each cpu as it starts.
 * Currently, initializes the per-cpu data area for SNIA.
 * Also sets up a few fields in the nodepda.  Also known as
 * platform_cpu_init() by the ia64 machvec code.
 */
void __cpuinit sn_cpu_init(void)
{
	int cpuid;
	int cpuphyid;
	int nasid;
	int subnode;
	int slice;
	int cnode;
	int i;
	static int wars_have_been_checked;

	cpuid = smp_processor_id();
	if (cpuid == 0 && IS_MEDUSA()) {
		if (ia64_sn_is_fake_prom())
			sn_prom_type = 2;
		else
			sn_prom_type = 1;
		printk(KERN_INFO "Running on medusa with %s PROM\n",
		       (sn_prom_type == 1) ? "real" : "fake");
	}

	memset(pda, 0, sizeof(pda));
	if (ia64_sn_get_sn_info(0, &sn_hub_info->shub2,
				&sn_hub_info->nasid_bitmask,
				&sn_hub_info->nasid_shift,
				&sn_system_size, &sn_sharing_domain_size,
				&sn_partition_id, &sn_coherency_id,
				&sn_region_size))
		BUG();
	sn_hub_info->as_shift = sn_hub_info->nasid_shift - 2;

	/*
	 * Don't check status. The SAL call is not supported on all PROMs
	 * but a failure is harmless.
	 */
	(void) ia64_sn_set_cpu_number(cpuid);

	/*
	 * The boot cpu makes this call again after platform initialization is
	 * complete.
	 */
	if (nodepdaindr[0] == NULL)
		return;

	for (i = 0; i < MAX_PROM_FEATURE_SETS; i++)
		if (ia64_sn_get_prom_feature_set(i, &sn_prom_features[i]) != 0)
			break;

	cpuphyid = get_sapicid();

	if (ia64_sn_get_sapic_info(cpuphyid, &nasid, &subnode, &slice))
		BUG();

	for (i=0; i < MAX_NUMNODES; i++) {
		if (nodepdaindr[i]) {
			nodepdaindr[i]->phys_cpuid[cpuid].nasid = nasid;
			nodepdaindr[i]->phys_cpuid[cpuid].slice = slice;
			nodepdaindr[i]->phys_cpuid[cpuid].subnode = subnode;
		}
	}

	cnode = nasid_to_cnodeid(nasid);

	sn_nodepda = nodepdaindr[cnode];

	pda->led_address =
	    (typeof(pda->led_address)) (LED0 + (slice << LED_CPU_SHIFT));
	pda->led_state = LED_ALWAYS_SET;
	pda->hb_count = HZ / 2;
	pda->hb_state = 0;
	pda->idle_flag = 0;

	if (cpuid != 0) {
		/* copy cpu 0's sn_cnodeid_to_nasid table to this cpu's */
		memcpy(sn_cnodeid_to_nasid,
		       (&per_cpu(__sn_cnodeid_to_nasid, 0)),
		       sizeof(__ia64_per_cpu_var(__sn_cnodeid_to_nasid)));
	}

	/*
	 * Check for WARs.
	 * Only needs to be done once, on BSP.
	 * Has to be done after loop above, because it uses this cpu's
	 * sn_cnodeid_to_nasid table which was just initialized if this
	 * isn't cpu 0.
	 * Has to be done before assignment below.
	 */
	if (!wars_have_been_checked) {
		sn_check_for_wars();
		wars_have_been_checked = 1;
	}
	sn_hub_info->shub_1_1_found = shub_1_1_found;

	/*
	 * Set up addresses of PIO/MEM write status registers.
	 */
	{
		u64 pio1[] = {SH1_PIO_WRITE_STATUS_0, 0, SH1_PIO_WRITE_STATUS_1, 0};
		u64 pio2[] = {SH2_PIO_WRITE_STATUS_0, SH2_PIO_WRITE_STATUS_2,
			SH2_PIO_WRITE_STATUS_1, SH2_PIO_WRITE_STATUS_3};
		u64 *pio;
		pio = is_shub1() ? pio1 : pio2;
		pda->pio_write_status_addr =
		   (volatile unsigned long *)GLOBAL_MMR_ADDR(nasid, pio[slice]);
		pda->pio_write_status_val = is_shub1() ? SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK : 0;
	}

	/*
	 * WAR addresses for SHUB 1.x.
	 */
	if (local_node_data->active_cpu_count++ == 0 && is_shub1()) {
		int buddy_nasid;
		buddy_nasid =
		    cnodeid_to_nasid(numa_node_id() ==
				     num_online_nodes() - 1 ? 0 : numa_node_id() + 1);
		pda->pio_shub_war_cam_addr =
		    (volatile unsigned long *)GLOBAL_MMR_ADDR(nasid,
							      SH1_PI_CAM_CONTROL);
	}
}

/*
 * Build tables for converting between NASIDs and cnodes.
 */
static inline int __init board_needs_cnode(int type)
{
	return (type == KLTYPE_SNIA || type == KLTYPE_TIO);
}

void __init build_cnode_tables(void)
{
	int nasid;
	int node;
	lboard_t *brd;

	memset(physical_node_map, -1, sizeof(physical_node_map));
	memset(sn_cnodeid_to_nasid, -1,
			sizeof(__ia64_per_cpu_var(__sn_cnodeid_to_nasid)));

	/*
	 * First populate the tables with C/M bricks. This ensures that
	 * cnode == node for all C & M bricks.
	 */
	for_each_online_node(node) {
		nasid = pxm_to_nasid(node_to_pxm(node));
		sn_cnodeid_to_nasid[node] = nasid;
		physical_node_map[nasid] = node;
	}

	/*
	 * num_cnodes is total number of C/M/TIO bricks. Because of the 256 node
	 * limit on the number of nodes, we can't use the generic node numbers 
	 * for this. Note that num_cnodes is incremented below as TIOs or
	 * headless/memoryless nodes are discovered.
	 */
	num_cnodes = num_online_nodes();

	/* fakeprom does not support klgraph */
	if (IS_RUNNING_ON_FAKE_PROM())
		return;

	/* Find TIOs & headless/memoryless nodes and add them to the tables */
	for_each_online_node(node) {
		kl_config_hdr_t *klgraph_header;
		nasid = cnodeid_to_nasid(node);
		klgraph_header = ia64_sn_get_klconfig_addr(nasid);
		if (klgraph_header == NULL)
			BUG();
		brd = NODE_OFFSET_TO_LBOARD(nasid, klgraph_header->ch_board_info);
		while (brd) {
			if (board_needs_cnode(brd->brd_type) && physical_node_map[brd->brd_nasid] < 0) {
				sn_cnodeid_to_nasid[num_cnodes] = brd->brd_nasid;
				physical_node_map[brd->brd_nasid] = num_cnodes++;
			}
			brd = find_lboard_next(brd);
		}
	}
}

int
nasid_slice_to_cpuid(int nasid, int slice)
{
	long cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		if (cpuid_to_nasid(cpu) == nasid &&
					cpuid_to_slice(cpu) == slice)
			return cpu;

	return -1;
}

int sn_prom_feature_available(int id)
{
	if (id >= BITS_PER_LONG * MAX_PROM_FEATURE_SETS)
		return 0;
	return test_bit(id, sn_prom_features);
}
EXPORT_SYMBOL(sn_prom_feature_available);

