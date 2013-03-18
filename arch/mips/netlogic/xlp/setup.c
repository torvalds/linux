/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/serial_8250.h>
#include <linux/pm.h>
#include <linux/bootmem.h>

#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/bootinfo.h>

#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/sys.h>

uint64_t nlm_io_base;
struct nlm_soc_info nlm_nodes[NLM_NR_NODES];
cpumask_t nlm_cpumask = CPU_MASK_CPU0;
unsigned int nlm_threads_per_core;
extern u32 __dtb_start[];

static void nlm_linux_exit(void)
{
	uint64_t sysbase = nlm_get_node(0)->sysbase;

	nlm_write_sys_reg(sysbase, SYS_CHIP_RESET, 1);
	for ( ; ; )
		cpu_wait();
}

void __init plat_mem_setup(void)
{
	void *fdtp;

	panic_timeout	= 5;
	_machine_restart = (void (*)(char *))nlm_linux_exit;
	_machine_halt	= nlm_linux_exit;
	pm_power_off	= nlm_linux_exit;

	/*
	 * If no FDT pointer is passed in, use the built-in FDT.
	 * device_tree_init() does not handle CKSEG0 pointers in
	 * 64-bit, so convert pointer.
	 */
	fdtp = (void *)(long)fw_arg0;
	if (!fdtp)
		fdtp = __dtb_start;
	fdtp = phys_to_virt(__pa(fdtp));
	early_init_devtree(fdtp);
}

const char *get_system_type(void)
{
	return "Netlogic XLP Series";
}

void __init prom_free_prom_memory(void)
{
	/* Nothing yet */
}

void xlp_mmu_init(void)
{
	/* enable extended TLB and Large Fixed TLB */
	write_c0_config6(read_c0_config6() | 0x24);

	/* set page mask of Fixed TLB in config7 */
	write_c0_config7(PM_DEFAULT_MASK >>
		(13 + (ffz(PM_DEFAULT_MASK >> 13) / 2)));
}

void nlm_percpu_init(int hwcpuid)
{
}

void __init prom_init(void)
{
	nlm_io_base = CKSEG1ADDR(XLP_DEFAULT_IO_BASE);
	xlp_mmu_init();
	nlm_node_init(0);

#ifdef CONFIG_SMP
	cpumask_setall(&nlm_cpumask);
	nlm_wakeup_secondary_cpus();

	/* update TLB size after waking up threads */
	current_cpu_data.tlbsize = ((read_c0_config6() >> 16) & 0xffff) + 1;

	register_smp_ops(&nlm_smp_ops);
#endif
}

void __init device_tree_init(void)
{
	unsigned long base, size;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/* Before we do anything, lets reserve the dt blob */
	reserve_bootmem(base, size, BOOTMEM_DEFAULT);

	unflatten_device_tree();

	/* free the space reserved for the dt blob */
	free_bootmem(base, size);
}

static struct of_device_id __initdata xlp_ids[] = {
	{ .compatible = "simple-bus", },
	{},
};

int __init xlp8xx_ds_publish_devices(void)
{
	if (!of_have_populated_dt())
		return 0;
	return of_platform_bus_probe(NULL, xlp_ids, NULL);
}

device_initcall(xlp8xx_ds_publish_devices);
