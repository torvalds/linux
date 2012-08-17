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

unsigned long nlm_common_ebase = 0x0;

/* default to uniprocessor */
uint32_t nlm_coremask = 1, nlm_cpumask  = 1;
int  nlm_threads_per_core = 1;

static void nlm_linux_exit(void)
{
	nlm_write_sys_reg(nlm_sys_base, SYS_CHIP_RESET, 1);
	for ( ; ; )
		cpu_wait();
}

void __init plat_mem_setup(void)
{
	panic_timeout	= 5;
	_machine_restart = (void (*)(char *))nlm_linux_exit;
	_machine_halt	= nlm_linux_exit;
	pm_power_off	= nlm_linux_exit;
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

void __init prom_init(void)
{
	void *fdtp;

	fdtp = (void *)(long)fw_arg0;
	xlp_mmu_init();
	nlm_hal_init();
	early_init_devtree(fdtp);

	nlm_common_ebase = read_c0_ebase() & (~((1 << 12) - 1));
#ifdef CONFIG_SMP
	nlm_wakeup_secondary_cpus(0xffffffff);

	/* update TLB size after waking up threads */
	current_cpu_data.tlbsize = ((read_c0_config6() >> 16) & 0xffff) + 1;

	register_smp_ops(&nlm_smp_ops);
#endif
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
