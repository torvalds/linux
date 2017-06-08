/*
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2003 ICT CAS
 * Author: Michael Guo <guoyi@ict.ac.cn>
 *
 * Copyright (C) 2007 Lemote Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/export.h>
#include <asm/bootinfo.h>
#include <loongson.h>
#include <boot_param.h>
#include <workarounds.h>

u32 cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);
struct efi_memory_map_loongson *loongson_memmap;
struct loongson_system_configuration loongson_sysconf;

u64 loongson_chipcfg[MAX_PACKAGES] = {0xffffffffbfc00180};
u64 loongson_chiptemp[MAX_PACKAGES];
u64 loongson_freqctrl[MAX_PACKAGES];

unsigned long long smp_group[4];

#define parse_even_earlier(res, option, p)				\
do {									\
	unsigned int tmp __maybe_unused;				\
									\
	if (strncmp(option, (char *)p, strlen(option)) == 0)		\
		tmp = kstrtou32((char *)p + strlen(option"="), 10, &res); \
} while (0)

void __init prom_init_env(void)
{
	/* pmon passes arguments in 32bit pointers */
	unsigned int processor_id;

#ifndef CONFIG_LEFI_FIRMWARE_INTERFACE
	int *_prom_envp;
	long l;

	/* firmware arguments are initialized in head.S */
	_prom_envp = (int *)fw_arg2;

	l = (long)*_prom_envp;
	while (l != 0) {
		parse_even_earlier(cpu_clock_freq, "cpuclock", l);
		parse_even_earlier(memsize, "memsize", l);
		parse_even_earlier(highmemsize, "highmemsize", l);
		_prom_envp++;
		l = (long)*_prom_envp;
	}
	if (memsize == 0)
		memsize = 256;

	loongson_sysconf.nr_uarts = 1;

	pr_info("memsize=%u, highmemsize=%u\n", memsize, highmemsize);
#else
	struct boot_params *boot_p;
	struct loongson_params *loongson_p;
	struct system_loongson *esys;
	struct efi_cpuinfo_loongson *ecpu;
	struct irq_source_routing_table *eirq_source;

	/* firmware arguments are initialized in head.S */
	boot_p = (struct boot_params *)fw_arg2;
	loongson_p = &(boot_p->efi.smbios.lp);

	esys = (struct system_loongson *)
		((u64)loongson_p + loongson_p->system_offset);
	ecpu = (struct efi_cpuinfo_loongson *)
		((u64)loongson_p + loongson_p->cpu_offset);
	eirq_source = (struct irq_source_routing_table *)
		((u64)loongson_p + loongson_p->irq_offset);
	loongson_memmap = (struct efi_memory_map_loongson *)
		((u64)loongson_p + loongson_p->memory_offset);

	cpu_clock_freq = ecpu->cpu_clock_freq;
	loongson_sysconf.cputype = ecpu->cputype;
	if (ecpu->cputype == Loongson_3A) {
		loongson_sysconf.cores_per_node = 4;
		loongson_sysconf.cores_per_package = 4;
		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff01000;
		smp_group[2] = 0x900020003ff01000;
		smp_group[3] = 0x900030003ff01000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900010001fe00180;
		loongson_chipcfg[2] = 0x900020001fe00180;
		loongson_chipcfg[3] = 0x900030001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900010001fe0019c;
		loongson_chiptemp[2] = 0x900020001fe0019c;
		loongson_chiptemp[3] = 0x900030001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900010001fe001d0;
		loongson_freqctrl[2] = 0x900020001fe001d0;
		loongson_freqctrl[3] = 0x900030001fe001d0;
		loongson_sysconf.ht_control_base = 0x90000EFDFB000000;
		loongson_sysconf.workarounds = WORKAROUND_CPUFREQ;
	} else if (ecpu->cputype == Loongson_3B) {
		loongson_sysconf.cores_per_node = 4; /* One chip has 2 nodes */
		loongson_sysconf.cores_per_package = 8;
		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff05000;
		smp_group[2] = 0x900020003ff09000;
		smp_group[3] = 0x900030003ff0d000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900020001fe00180;
		loongson_chipcfg[2] = 0x900040001fe00180;
		loongson_chipcfg[3] = 0x900060001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900020001fe0019c;
		loongson_chiptemp[2] = 0x900040001fe0019c;
		loongson_chiptemp[3] = 0x900060001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900020001fe001d0;
		loongson_freqctrl[2] = 0x900040001fe001d0;
		loongson_freqctrl[3] = 0x900060001fe001d0;
		loongson_sysconf.ht_control_base = 0x90001EFDFB000000;
		loongson_sysconf.workarounds = WORKAROUND_CPUHOTPLUG;
	} else {
		loongson_sysconf.cores_per_node = 1;
		loongson_sysconf.cores_per_package = 1;
		loongson_chipcfg[0] = 0x900000001fe00180;
	}

	loongson_sysconf.nr_cpus = ecpu->nr_cpus;
	loongson_sysconf.boot_cpu_id = ecpu->cpu_startup_core_id;
	loongson_sysconf.reserved_cpus_mask = ecpu->reserved_cores_mask;
	if (ecpu->nr_cpus > NR_CPUS || ecpu->nr_cpus == 0)
		loongson_sysconf.nr_cpus = NR_CPUS;
	loongson_sysconf.nr_nodes = (loongson_sysconf.nr_cpus +
		loongson_sysconf.cores_per_node - 1) /
		loongson_sysconf.cores_per_node;

	loongson_sysconf.pci_mem_start_addr = eirq_source->pci_mem_start_addr;
	loongson_sysconf.pci_mem_end_addr = eirq_source->pci_mem_end_addr;
	loongson_sysconf.pci_io_base = eirq_source->pci_io_start_addr;
	loongson_sysconf.dma_mask_bits = eirq_source->dma_mask_bits;
	if (loongson_sysconf.dma_mask_bits < 32 ||
		loongson_sysconf.dma_mask_bits > 64)
		loongson_sysconf.dma_mask_bits = 32;

	loongson_sysconf.restart_addr = boot_p->reset_system.ResetWarm;
	loongson_sysconf.poweroff_addr = boot_p->reset_system.Shutdown;
	loongson_sysconf.suspend_addr = boot_p->reset_system.DoSuspend;

	loongson_sysconf.vgabios_addr = boot_p->efi.smbios.vga_bios;
	pr_debug("Shutdown Addr: %llx, Restart Addr: %llx, VBIOS Addr: %llx\n",
		loongson_sysconf.poweroff_addr, loongson_sysconf.restart_addr,
		loongson_sysconf.vgabios_addr);

	memset(loongson_sysconf.ecname, 0, 32);
	if (esys->has_ec)
		memcpy(loongson_sysconf.ecname, esys->ec_name, 32);
	loongson_sysconf.workarounds |= esys->workarounds;

	loongson_sysconf.nr_uarts = esys->nr_uarts;
	if (esys->nr_uarts < 1 || esys->nr_uarts > MAX_UARTS)
		loongson_sysconf.nr_uarts = 1;
	memcpy(loongson_sysconf.uarts, esys->uarts,
		sizeof(struct uart_device) * loongson_sysconf.nr_uarts);

	loongson_sysconf.nr_sensors = esys->nr_sensors;
	if (loongson_sysconf.nr_sensors > MAX_SENSORS)
		loongson_sysconf.nr_sensors = 0;
	if (loongson_sysconf.nr_sensors)
		memcpy(loongson_sysconf.sensors, esys->sensors,
			sizeof(struct sensor_device) * loongson_sysconf.nr_sensors);
#endif
	if (cpu_clock_freq == 0) {
		processor_id = (&current_cpu_data)->processor_id;
		switch (processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			cpu_clock_freq = 533080000;
			break;
		case PRID_REV_LOONGSON2F:
			cpu_clock_freq = 797000000;
			break;
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3A_R2:
			cpu_clock_freq = 900000000;
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			cpu_clock_freq = 1000000000;
			break;
		default:
			cpu_clock_freq = 100000000;
			break;
		}
	}
	pr_info("CpuClock = %u\n", cpu_clock_freq);
}
