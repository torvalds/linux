// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Zhangjin Wu, wuzhangjin@gmail.com
 */
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include <asm/bootinfo.h>
#include <asm/idle.h>
#include <asm/reboot.h>

#include <loongson.h>
#include <boot_param.h>

static void loongson_restart(char *command)
{

	void (*fw_restart)(void) = (void *)loongson_sysconf.restart_addr;

	fw_restart();
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_poweroff(void)
{
	void (*fw_poweroff)(void) = (void *)loongson_sysconf.poweroff_addr;

	fw_poweroff();
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_halt(void)
{
	pr_notice("\n\n** You can safely turn off the power now **\n\n");
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

#ifdef CONFIG_KEXEC

/* 0X80000000~0X80200000 is safe */
#define MAX_ARGS	64
#define KEXEC_CTRL_CODE	0xFFFFFFFF80100000UL
#define KEXEC_ARGV_ADDR	0xFFFFFFFF80108000UL
#define KEXEC_ARGV_SIZE	COMMAND_LINE_SIZE
#define KEXEC_ENVP_SIZE	4800

static int kexec_argc;
static int kdump_argc;
static void *kexec_argv;
static void *kdump_argv;
static void *kexec_envp;

static int loongson_kexec_prepare(struct kimage *image)
{
	int i, argc = 0;
	unsigned int *argv;
	char *str, *ptr, *bootloader = "kexec";

	/* argv at offset 0, argv[] at offset KEXEC_ARGV_SIZE/2 */
	if (image->type == KEXEC_TYPE_DEFAULT)
		argv = (unsigned int *)kexec_argv;
	else
		argv = (unsigned int *)kdump_argv;

	argv[argc++] = (unsigned int)(KEXEC_ARGV_ADDR + KEXEC_ARGV_SIZE/2);

	for (i = 0; i < image->nr_segments; i++) {
		if (!strncmp(bootloader, (char *)image->segment[i].buf,
				strlen(bootloader))) {
			/*
			 * convert command line string to array
			 * of parameters (as bootloader does).
			 */
			int offt;
			str = (char *)argv + KEXEC_ARGV_SIZE/2;
			memcpy(str, image->segment[i].buf, KEXEC_ARGV_SIZE/2);
			ptr = strchr(str, ' ');

			while (ptr && (argc < MAX_ARGS)) {
				*ptr = '\0';
				if (ptr[1] != ' ') {
					offt = (int)(ptr - str + 1);
					argv[argc] = KEXEC_ARGV_ADDR + KEXEC_ARGV_SIZE/2 + offt;
					argc++;
				}
				ptr = strchr(ptr + 1, ' ');
			}
			break;
		}
	}

	if (image->type == KEXEC_TYPE_DEFAULT)
		kexec_argc = argc;
	else
		kdump_argc = argc;

	/* kexec/kdump need a safe page to save reboot_code_buffer */
	image->control_code_page = virt_to_page((void *)KEXEC_CTRL_CODE);

	return 0;
}

static void loongson_kexec_shutdown(void)
{
#ifdef CONFIG_SMP
	int cpu;

	/* All CPUs go to reboot_code_buffer */
	for_each_possible_cpu(cpu)
		if (!cpu_online(cpu))
			cpu_device_up(get_cpu_device(cpu));

	secondary_kexec_args[0] = TO_UNCAC(0x3ff01000);
#endif
	kexec_args[0] = kexec_argc;
	kexec_args[1] = fw_arg1;
	kexec_args[2] = fw_arg2;
	memcpy((void *)fw_arg1, kexec_argv, KEXEC_ARGV_SIZE);
	memcpy((void *)fw_arg2, kexec_envp, KEXEC_ENVP_SIZE);
}

static void loongson_crash_shutdown(struct pt_regs *regs)
{
	default_machine_crash_shutdown(regs);
	kexec_args[0] = kdump_argc;
	kexec_args[1] = fw_arg1;
	kexec_args[2] = fw_arg2;
#ifdef CONFIG_SMP
	secondary_kexec_args[0] = TO_UNCAC(0x3ff01000);
#endif
	memcpy((void *)fw_arg1, kdump_argv, KEXEC_ARGV_SIZE);
	memcpy((void *)fw_arg2, kexec_envp, KEXEC_ENVP_SIZE);
}

#endif

static int __init mips_reboot_setup(void)
{
	_machine_restart = loongson_restart;
	_machine_halt = loongson_halt;
	pm_power_off = loongson_poweroff;

#ifdef CONFIG_KEXEC
	kexec_argv = kmalloc(KEXEC_ARGV_SIZE, GFP_KERNEL);
	kdump_argv = kmalloc(KEXEC_ARGV_SIZE, GFP_KERNEL);
	kexec_envp = kmalloc(KEXEC_ENVP_SIZE, GFP_KERNEL);
	fw_arg1 = KEXEC_ARGV_ADDR;
	memcpy(kexec_envp, (void *)fw_arg2, KEXEC_ENVP_SIZE);

	_machine_kexec_prepare = loongson_kexec_prepare;
	_machine_kexec_shutdown = loongson_kexec_shutdown;
	_machine_crash_shutdown = loongson_crash_shutdown;
#endif

	return 0;
}

arch_initcall(mips_reboot_setup);
