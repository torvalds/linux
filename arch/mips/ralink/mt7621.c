// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2015 Nikolay Martynov <mar.kolya@gmail.com>
 * Copyright (C) 2015 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/memblock.h>
#include <linux/pci.h>
#include <linux/bug.h>

#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/smp-ops.h>
#include <asm/mips-cps.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7621.h>

#include "common.h"

#define MT7621_MEM_TEST_PATTERN         0xaa5555aa

static u32 detect_magic __initdata;
static struct ralink_soc_info *soc_info_ptr;

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	struct resource_entry *entry;
	resource_size_t mask;

	entry = resource_list_first_type(&bridge->windows, IORESOURCE_MEM);
	if (!entry) {
		pr_err("Cannot get memory resource\n");
		return -EINVAL;
	}

	if (mips_cps_numiocu(0)) {
		/*
		 * Hardware doesn't accept mask values with 1s after
		 * 0s (e.g. 0xffef), so warn if that's happen
		 */
		mask = ~(entry->res->end - entry->res->start) & CM_GCR_REGn_MASK_ADDRMASK;
		WARN_ON(mask && BIT(ffz(~mask)) - 1 != ~mask);

		write_gcr_reg1_base(entry->res->start);
		write_gcr_reg1_mask(mask | CM_GCR_REGn_MASK_CMTGT_IOCU0);
		pr_info("PCI coherence region base: 0x%08llx, mask/settings: 0x%08llx\n",
			(unsigned long long)read_gcr_reg1_base(),
			(unsigned long long)read_gcr_reg1_mask());
	}

	return 0;
}

phys_addr_t mips_cpc_default_phys_base(void)
{
	panic("Cannot detect cpc address");
}

static bool __init mt7621_addr_wraparound_test(phys_addr_t size)
{
	void *dm = (void *)KSEG1ADDR(&detect_magic);

	if (CPHYSADDR(dm + size) >= MT7621_LOWMEM_MAX_SIZE)
		return true;
	__raw_writel(MT7621_MEM_TEST_PATTERN, dm);
	if (__raw_readl(dm) != __raw_readl(dm + size))
		return false;
	__raw_writel(~MT7621_MEM_TEST_PATTERN, dm);
	return __raw_readl(dm) == __raw_readl(dm + size);
}

static void __init mt7621_memory_detect(void)
{
	phys_addr_t size;

	for (size = 32 * SZ_1M; size <= 256 * SZ_1M; size <<= 1) {
		if (mt7621_addr_wraparound_test(size)) {
			memblock_add(MT7621_LOWMEM_BASE, size);
			return;
		}
	}

	memblock_add(MT7621_LOWMEM_BASE, MT7621_LOWMEM_MAX_SIZE);
	memblock_add(MT7621_HIGHMEM_BASE, MT7621_HIGHMEM_SIZE);
}

static unsigned int __init mt7621_get_soc_name0(void)
{
	return __raw_readl(MT7621_SYSC_BASE + SYSC_REG_CHIP_NAME0);
}

static unsigned int __init mt7621_get_soc_name1(void)
{
	return __raw_readl(MT7621_SYSC_BASE + SYSC_REG_CHIP_NAME1);
}

static bool __init mt7621_soc_valid(void)
{
	if (mt7621_get_soc_name0() == MT7621_CHIP_NAME0 &&
			mt7621_get_soc_name1() == MT7621_CHIP_NAME1)
		return true;
	else
		return false;
}

static const char __init *mt7621_get_soc_id(void)
{
	if (mt7621_soc_valid())
		return "MT7621";
	else
		return "invalid";
}

static unsigned int __init mt7621_get_soc_rev(void)
{
	return __raw_readl(MT7621_SYSC_BASE + SYSC_REG_CHIP_REV);
}

static unsigned int __init mt7621_get_soc_ver(void)
{
	return (mt7621_get_soc_rev() >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK;
}

static unsigned int __init mt7621_get_soc_eco(void)
{
	return (mt7621_get_soc_rev() & CHIP_REV_ECO_MASK);
}

static const char __init *mt7621_get_soc_revision(void)
{
	if (mt7621_get_soc_rev() == 1 && mt7621_get_soc_eco() == 1)
		return "E2";
	else
		return "E1";
}

static int __init mt7621_soc_dev_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->soc_id = "mt7621";
	soc_dev_attr->family = "Ralink";
	soc_dev_attr->revision = mt7621_get_soc_revision();

	soc_dev_attr->data = soc_info_ptr;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
device_initcall(mt7621_soc_dev_init);

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	/* Early detection of CMP support */
	mips_cm_probe();
	mips_cpc_probe();

	if (mips_cps_numiocu(0)) {
		/*
		 * mips_cm_probe() wipes out bootloader
		 * config for CM regions and we have to configure them
		 * again. This SoC cannot talk to pamlbus devices
		 * without proper iocu region set up.
		 *
		 * FIXME: it would be better to do this with values
		 * from DT, but we need this very early because
		 * without this we cannot talk to pretty much anything
		 * including serial.
		 */
		write_gcr_reg0_base(MT7621_PALMBUS_BASE);
		write_gcr_reg0_mask(~MT7621_PALMBUS_SIZE |
				    CM_GCR_REGn_MASK_CMTGT_IOCU0);
		__sync();
	}

	if (mt7621_soc_valid())
		soc_info->compatible = "mediatek,mt7621-soc";
	else
		panic("mt7621: unknown SoC, n0:%08x n1:%08x\n",
				mt7621_get_soc_name0(),
				mt7621_get_soc_name1());
	ralink_soc = MT762X_SOC_MT7621AT;

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"MediaTek %s ver:%u eco:%u",
		mt7621_get_soc_id(),
		mt7621_get_soc_ver(),
		mt7621_get_soc_eco());

	soc_info->mem_detect = mt7621_memory_detect;

	soc_info_ptr = soc_info;

	if (!register_cps_smp_ops())
		return;
	if (!register_vsmp_smp_ops())
		return;
}
