// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <linux/libfdt.h>
#include <linux/pci_ids.h>
#include <linux/string_choices.h>
#include <asm/bootinfo.h>
#include <loongson.h>
#include <boot_param.h>
#include <builtin_dtbs.h>
#include <workarounds.h>

#define HOST_BRIDGE_CONFIG_ADDR	((void __iomem *)TO_UNCAC(0x1a000000))

u32 cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);
struct efi_memory_map_loongson *loongson_memmap;
struct loongson_system_configuration loongson_sysconf;

struct board_devices *eboard;
struct interface_info *einter;
struct loongson_special_attribute *especial;

u64 loongson_chipcfg[MAX_PACKAGES] = {0xffffffffbfc00180};
u64 loongson_chiptemp[MAX_PACKAGES];
u64 loongson_freqctrl[MAX_PACKAGES];

unsigned long long smp_group[4];

const char *get_system_type(void)
{
	return "Generic Loongson64 System";
}


void __init prom_dtb_init_env(void)
{
	if ((fw_arg2 < CKSEG0 || fw_arg2 > CKSEG1)
		&& (fw_arg2 < XKPHYS || fw_arg2 > XKSEG))

		loongson_fdt_blob = __dtb_loongson64_2core_2k1000_begin;
	else
		loongson_fdt_blob = (void *)fw_arg2;
}

static int __init lefi_fixup_fdt_serial(void *fdt, u64 uart_addr, u32 uart_clk)
{
	int node, len, depth = -1;
	const fdt64_t *reg;
	fdt32_t *clk;

	for (node = fdt_next_node(fdt, -1, &depth);
	     node >= 0 && depth >= 0;
	     node = fdt_next_node(fdt, node, &depth)) {
		reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg || len <= 8 || fdt64_ld(reg) != uart_addr)
			continue;

		clk = fdt_getprop_w(fdt, node, "clock-frequency", &len);
		if (!clk) {
			pr_warn("UART 0x%llx misses clock-frequency property\n",
				uart_addr);
			return -ENOENT;
		} else if (len != 4) {
			pr_warn("UART 0x%llx has invalid clock-frequency property\n",
				uart_addr);
			return -EINVAL;
		}

		fdt32_st(clk, uart_clk);

		return 0;
	}

	return -ENODEV;
}

static void __init lefi_fixup_fdt(struct system_loongson *system)
{
	static unsigned char fdt_buf[16 << 10] __initdata;
	struct uart_device *uartdev;
	bool is_loongson64g;
	u64 uart_base;
	int ret, i;

	ret = fdt_open_into(loongson_fdt_blob, fdt_buf, sizeof(fdt_buf));
	if (ret) {
		pr_err("Failed to open FDT to fix up\n");
		return;
	}

	is_loongson64g = (read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64G;

	for (i = 0; i < system->nr_uarts; i++) {
		uartdev = &system->uarts[i];

		ret = lefi_fixup_fdt_serial(fdt_buf, uartdev->uart_base,
					    uartdev->uartclk);
		/*
		 * LOONGSON64G's CPU serials are mapped to two different
		 * addresses, one full-featured but differs from
		 * previous generations, one fully compatible with them.
		 *
		 * It's unspecified that which mapping should uart_base refer
		 * to, thus we should try fixing up with both.
		 */
		if (ret == -ENODEV && is_loongson64g) {
			switch (uartdev->uart_base) {
			case 0x1fe00100:
				uart_base = 0x1fe001e0;
				break;
			case 0x1fe00110:
				uart_base = 0x1fe001e8;
				break;
			case 0x1fe001e0:
				uart_base = 0x1fe00100;
				break;
			case 0x1fe001e8:
				uart_base = 0x1fe00110;
				break;
			default:
				pr_err("Unexpected UART address 0x%llx passed by firmware\n",
				       uartdev->uart_base);
				ret = -EINVAL;
				goto err_fixup;
			}

			ret = lefi_fixup_fdt_serial(fdt_buf, uart_base,
						    uartdev->uartclk);
		}

err_fixup:
		if (ret)
			pr_err("Couldn't fix up FDT node for UART 0x%llx\n",
			       uartdev->uart_base);
	}

	loongson_fdt_blob = fdt_buf;
}

void __init prom_lefi_init_env(void)
{
	struct boot_params *boot_p;
	struct loongson_params *loongson_p;
	struct system_loongson *esys;
	struct efi_cpuinfo_loongson *ecpu;
	struct irq_source_routing_table *eirq_source;
	u32 id;
	u16 vendor;

	/* firmware arguments are initialized in head.S */
	boot_p = (struct boot_params *)fw_arg2;
	loongson_p = &(boot_p->efi.smbios.lp);

	esys = (struct system_loongson *)
		((u64)loongson_p + loongson_p->system_offset);
	ecpu = (struct efi_cpuinfo_loongson *)
		((u64)loongson_p + loongson_p->cpu_offset);
	eboard = (struct board_devices *)
		((u64)loongson_p + loongson_p->boarddev_table_offset);
	einter = (struct interface_info *)
		((u64)loongson_p + loongson_p->interface_offset);
	especial = (struct loongson_special_attribute *)
		((u64)loongson_p + loongson_p->special_offset);
	eirq_source = (struct irq_source_routing_table *)
		((u64)loongson_p + loongson_p->irq_offset);
	loongson_memmap = (struct efi_memory_map_loongson *)
		((u64)loongson_p + loongson_p->memory_offset);

	cpu_clock_freq = ecpu->cpu_clock_freq;
	loongson_sysconf.cputype = ecpu->cputype;
	switch (ecpu->cputype) {
	case Legacy_2K:
	case Loongson_2K:
		smp_group[0] = 0x900000001fe11000;
		loongson_sysconf.cores_per_node = 2;
		loongson_sysconf.cores_per_package = 2;
		break;
	case Legacy_3A:
	case Loongson_3A:
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
		loongson_sysconf.workarounds = WORKAROUND_CPUFREQ;
		break;
	case Legacy_3B:
	case Loongson_3B:
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
		loongson_sysconf.workarounds = WORKAROUND_CPUHOTPLUG;
		break;
	default:
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

	loongson_sysconf.dma_mask_bits = eirq_source->dma_mask_bits;
	if (loongson_sysconf.dma_mask_bits < 32 ||
			loongson_sysconf.dma_mask_bits > 64) {
		loongson_sysconf.dma_mask_bits = 32;
		dma_default_coherent = true;
	} else {
		dma_default_coherent = !eirq_source->dma_noncoherent;
	}

	pr_info("Firmware: Coherent DMA: %s\n", str_on_off(dma_default_coherent));

	loongson_sysconf.restart_addr = boot_p->reset_system.ResetWarm;
	loongson_sysconf.poweroff_addr = boot_p->reset_system.Shutdown;
	loongson_sysconf.suspend_addr = boot_p->reset_system.DoSuspend;

	loongson_sysconf.vgabios_addr = boot_p->efi.smbios.vga_bios;
	pr_debug("Shutdown Addr: %llx, Restart Addr: %llx, VBIOS Addr: %llx\n",
		loongson_sysconf.poweroff_addr, loongson_sysconf.restart_addr,
		loongson_sysconf.vgabios_addr);

	loongson_sysconf.workarounds |= esys->workarounds;

	pr_info("CpuClock = %u\n", cpu_clock_freq);

	/* Read the ID of PCI host bridge to detect bridge type */
	id = readl(HOST_BRIDGE_CONFIG_ADDR);
	vendor = id & 0xffff;

	switch (vendor) {
	case PCI_VENDOR_ID_LOONGSON:
		pr_info("The bridge chip is LS7A\n");
		loongson_sysconf.bridgetype = LS7A;
		loongson_sysconf.early_config = ls7a_early_config;
		break;
	case PCI_VENDOR_ID_AMD:
	case PCI_VENDOR_ID_ATI:
		pr_info("The bridge chip is RS780E or SR5690\n");
		loongson_sysconf.bridgetype = RS780E;
		loongson_sysconf.early_config = rs780e_early_config;
		break;
	default:
		pr_info("The bridge chip is VIRTUAL\n");
		loongson_sysconf.bridgetype = VIRTUAL;
		loongson_sysconf.early_config = virtual_early_config;
		loongson_fdt_blob = __dtb_loongson64v_4core_virtio_begin;
		break;
	}

	if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64C) {
		switch (read_c0_prid() & PRID_REV_MASK) {
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			switch (loongson_sysconf.bridgetype) {
			case LS7A:
				loongson_fdt_blob = __dtb_loongson64c_4core_ls7a_begin;
				break;
			case RS780E:
				loongson_fdt_blob = __dtb_loongson64c_4core_rs780e_begin;
				break;
			default:
				break;
			}
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			if (loongson_sysconf.bridgetype == RS780E)
				loongson_fdt_blob = __dtb_loongson64c_8core_rs780e_begin;
			break;
		default:
			break;
		}
	} else if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R) {
		loongson_fdt_blob = __dtb_loongson64_2core_2k1000_begin;
	} else if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64G) {
		if (loongson_sysconf.bridgetype == LS7A)
			loongson_fdt_blob = __dtb_loongson64g_4core_ls7a_begin;
	}

	if (!loongson_fdt_blob)
		pr_err("Failed to determine built-in Loongson64 dtb\n");
	else
		lefi_fixup_fdt(esys);
}
