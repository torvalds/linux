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

#include <asm/idle.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/bootinfo.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/psb-bootinfo.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>

#include <asm/netlogic/xlr/xlr.h>
#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/gpio.h>
#include <asm/netlogic/xlr/fmn.h>

uint64_t nlm_io_base = DEFAULT_NETLOGIC_IO_BASE;
struct psb_info nlm_prom_info;

/* default to uniprocessor */
unsigned int  nlm_threads_per_core = 1;
struct nlm_soc_info nlm_nodes[NLM_NR_NODES];
cpumask_t nlm_cpumask = CPU_MASK_CPU0;

static void nlm_linux_exit(void)
{
	uint64_t gpiobase;

	gpiobase = nlm_mmio_base(NETLOGIC_IO_GPIO_OFFSET);
	/* trigger a chip reset by writing 1 to GPIO_SWRESET_REG */
	nlm_write_reg(gpiobase, GPIO_SWRESET_REG, 1);
	for ( ; ; )
		cpu_wait();
}

void __init plat_mem_setup(void)
{
	_machine_restart = (void (*)(char *))nlm_linux_exit;
	_machine_halt	= nlm_linux_exit;
	pm_power_off	= nlm_linux_exit;
}

const char *get_system_type(void)
{
	return "Netlogic XLR/XLS Series";
}

unsigned int nlm_get_cpu_frequency(void)
{
	return (unsigned int)nlm_prom_info.cpu_frequency;
}

void __init prom_free_prom_memory(void)
{
	/* Nothing yet */
}

void nlm_percpu_init(int hwcpuid)
{
	if (hwcpuid % 4 == 0)
		xlr_percpu_fmn_init();
}

static void __init build_arcs_cmdline(int *argv)
{
	int i, remain, len;
	char *arg;

	remain = sizeof(arcs_cmdline) - 1;
	arcs_cmdline[0] = '\0';
	for (i = 0; argv[i] != 0; i++) {
		arg = (char *)(long)argv[i];
		len = strlen(arg);
		if (len + 1 > remain)
			break;
		strcat(arcs_cmdline, arg);
		strcat(arcs_cmdline, " ");
		remain -=  len + 1;
	}

	/* Add the default options here */
	if ((strstr(arcs_cmdline, "console=")) == NULL) {
		arg = "console=ttyS0,38400 ";
		len = strlen(arg);
		if (len > remain)
			goto fail;
		strcat(arcs_cmdline, arg);
		remain -= len;
	}
#ifdef CONFIG_BLK_DEV_INITRD
	if ((strstr(arcs_cmdline, "rdinit=")) == NULL) {
		arg = "rdinit=/sbin/init ";
		len = strlen(arg);
		if (len > remain)
			goto fail;
		strcat(arcs_cmdline, arg);
		remain -= len;
	}
#endif
	return;
fail:
	panic("Cannot add %s, command line too big!", arg);
}

static void prom_add_memory(void)
{
	struct nlm_boot_mem_map *bootm;
	u64 start, size;
	u64 pref_backup = 512;	/* avoid pref walking beyond end */
	int i;

	bootm = (void *)(long)nlm_prom_info.psb_mem_map;
	for (i = 0; i < bootm->nr_map; i++) {
		if (bootm->map[i].type != BOOT_MEM_RAM)
			continue;
		start = bootm->map[i].addr;
		size   = bootm->map[i].size;

		/* Work around for using bootloader mem */
		if (i == 0 && start == 0 && size == 0x0c000000)
			size = 0x0ff00000;

		add_memory_region(start, size - pref_backup, BOOT_MEM_RAM);
	}
}

static void nlm_init_node(void)
{
	struct nlm_soc_info *nodep;

	nodep = nlm_current_node();
	nodep->picbase = nlm_mmio_base(NETLOGIC_IO_PIC_OFFSET);
	nodep->ebase = read_c0_ebase() & MIPS_EBASE_BASE;
	spin_lock_init(&nodep->piclock);
}

void __init prom_init(void)
{
	int *argv, *envp;		/* passed as 32 bit ptrs */
	struct psb_info *prom_infop;
	void *reset_vec;
#ifdef CONFIG_SMP
	int i;
#endif

	/* truncate to 32 bit and sign extend all args */
	argv = (int *)(long)(int)fw_arg1;
	envp = (int *)(long)(int)fw_arg2;
	prom_infop = (struct psb_info *)(long)(int)fw_arg3;

	nlm_prom_info = *prom_infop;
	nlm_init_node();

	/* Update reset entry point with CPU init code */
	reset_vec = (void *)CKSEG1ADDR(RESET_VEC_PHYS);
	memset(reset_vec, 0, RESET_VEC_SIZE);
	memcpy(reset_vec, (void *)nlm_reset_entry,
			(nlm_reset_entry_end - nlm_reset_entry));

	build_arcs_cmdline(argv);
	prom_add_memory();

#ifdef CONFIG_SMP
	for (i = 0; i < 32; i++)
		if (nlm_prom_info.online_cpu_map & (1 << i))
			cpumask_set_cpu(i, &nlm_cpumask);
	nlm_wakeup_secondary_cpus();
	register_smp_ops(&nlm_smp_ops);
#endif
	xlr_board_info_setup();
	xlr_percpu_fmn_init();
}
