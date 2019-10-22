/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/cpumask.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/sgialib.h>
#include <asm/time.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/mipsregs.h>
#include <asm/sn/gda.h>
#include <asm/sn/hub.h>
#include <asm/sn/intr.h>
#include <asm/current.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/thread_info.h>
#include <asm/sn/launch.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/mapped_kernel.h>

#include "ip27-common.h"

#define CPU_NONE		(cpuid_t)-1

static DECLARE_BITMAP(hub_init_mask, MAX_COMPACT_NODES);
nasid_t master_nasid = INVALID_NASID;

struct cpuinfo_ip27 sn_cpu_info[NR_CPUS];
EXPORT_SYMBOL_GPL(sn_cpu_info);

static void per_hub_init(nasid_t nasid)
{
	struct hub_data *hub = hub_data(nasid);

	cpumask_set_cpu(smp_processor_id(), &hub->h_cpus);

	if (test_and_set_bit(nasid, hub_init_mask))
		return;
	/*
	 * Set CRB timeout at 5ms, (< PI timeout of 10ms)
	 */
	REMOTE_HUB_S(nasid, IIO_ICTP, 0x800);
	REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);

	hub_rtc_init(nasid);

#ifdef CONFIG_REPLICATE_EXHANDLERS
	/*
	 * If this is not a headless node initialization,
	 * copy over the caliased exception handlers.
	 */
	if (get_nasid() == nasid) {
		extern char except_vec2_generic, except_vec3_generic;
		extern void build_tlb_refill_handler(void);

		memcpy((void *)(CKSEG0 + 0x100), &except_vec2_generic, 0x80);
		memcpy((void *)(CKSEG0 + 0x180), &except_vec3_generic, 0x80);
		build_tlb_refill_handler();
		memcpy((void *)(CKSEG0 + 0x100), (void *) CKSEG0, 0x80);
		memcpy((void *)(CKSEG0 + 0x180), &except_vec3_generic, 0x100);
		__flush_cache_all();
	}
#endif
}

void per_cpu_init(void)
{
	int cpu = smp_processor_id();
	int slice = LOCAL_HUB_L(PI_CPU_NUM);
	nasid_t nasid = get_nasid();
	struct hub_data *hub = hub_data(nasid);

	if (test_and_set_bit(slice, &hub->slice_map))
		return;

	clear_c0_status(ST0_IM);

	per_hub_init(nasid);

	cpu_time_init();
	install_ipi();

	/* Install our NMI handler if symmon hasn't installed one. */
	install_cpu_nmi_handler(cputoslice(cpu));

	enable_percpu_irq(IP27_HUB_PEND0_IRQ, IRQ_TYPE_NONE);
	enable_percpu_irq(IP27_HUB_PEND1_IRQ, IRQ_TYPE_NONE);
}

/*
 * get_nasid() returns the physical node id number of the caller.
 */
nasid_t
get_nasid(void)
{
	return (nasid_t)((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_NODEID_MASK)
			 >> NSRI_NODEID_SHFT);
}

void __init plat_mem_setup(void)
{
	u64 p, e, n_mode;
	nasid_t nid;

	register_smp_ops(&ip27_smp_ops);

	ip27_reboot_setup();

	/*
	 * hub_rtc init and cpu clock intr enabled for later calibrate_delay.
	 */
	nid = get_nasid();
	printk("IP27: Running on node %d.\n", nid);

	p = LOCAL_HUB_L(PI_CPU_PRESENT_A) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_A) & 1;
	printk("Node %d has %s primary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	p = LOCAL_HUB_L(PI_CPU_PRESENT_B) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_B) & 1;
	printk("Node %d has %s secondary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	/*
	 * Try to catch kernel missconfigurations and give user an
	 * indication what option to select.
	 */
	n_mode = LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_MORENODES_MASK;
	printk("Machine is in %c mode.\n", n_mode ? 'N' : 'M');
#ifdef CONFIG_SGI_SN_N_MODE
	if (!n_mode)
		panic("Kernel compiled for M mode.");
#else
	if (n_mode)
		panic("Kernel compiled for N mode.");
#endif

	ioport_resource.start = 0;
	ioport_resource.end = ~0UL;
	set_io_port_base(IO_BASE);
}

const char *get_system_type(void)
{
	return "SGI Origin";
}

void __init prom_init(void)
{
	prom_init_cmdline(fw_arg0, (LONG *)fw_arg1);
	prom_meminit();
}

