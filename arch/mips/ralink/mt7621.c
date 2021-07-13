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

#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/smp-ops.h>
#include <asm/mips-cps.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7621.h>

#include "common.h"

static void *detect_magic __initdata = detect_memory_region;

phys_addr_t mips_cpc_default_phys_base(void)
{
	panic("Cannot detect cpc address");
}

static void __init mt7621_memory_detect(void)
{
	void *dm = &detect_magic;
	phys_addr_t size;

	for (size = 32 * SZ_1M; size < 256 * SZ_1M; size <<= 1) {
		if (!__builtin_memcmp(dm, dm + size, sizeof(detect_magic)))
			break;
	}

	if ((size == 256 * SZ_1M) &&
	    (CPHYSADDR(dm + size) < MT7621_LOWMEM_MAX_SIZE) &&
	    __builtin_memcmp(dm, dm + size, sizeof(detect_magic))) {
		memblock_add(MT7621_LOWMEM_BASE, MT7621_LOWMEM_MAX_SIZE);
		memblock_add(MT7621_HIGHMEM_BASE, MT7621_HIGHMEM_SIZE);
	} else {
		memblock_add(MT7621_LOWMEM_BASE, size);
	}
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("mediatek,mt7621-sysc");
	rt_memc_membase = plat_of_remap_node("mediatek,mt7621-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

static void soc_dev_init(struct ralink_soc_info *soc_info, u32 rev)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	soc_dev_attr->soc_id = "mt7621";
	soc_dev_attr->family = "Ralink";

	if (((rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK) == 1 &&
	    (rev & CHIP_REV_ECO_MASK) == 1)
		soc_dev_attr->revision = "E2";
	else
		soc_dev_attr->revision = "E1";

	soc_dev_attr->data = soc_info;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return;
	}
}

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(MT7621_SYSC_BASE);
	unsigned char *name = NULL;
	u32 n0;
	u32 n1;
	u32 rev;

	/* Early detection of CMP support */
	mips_cm_probe();
	mips_cpc_probe();

	if (mips_cps_numiocu(0)) {
		/*
		 * mips_cm_probe() wipes out bootloader
		 * config for CM regions and we have to configure them
		 * again. This SoC cannot talk to pamlbus devices
		 * witout proper iocu region set up.
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

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);

	if (n0 == MT7621_CHIP_NAME0 && n1 == MT7621_CHIP_NAME1) {
		name = "MT7621";
		soc_info->compatible = "mediatek,mt7621-soc";
	} else {
		panic("mt7621: unknown SoC, n0:%08x n1:%08x\n", n0, n1);
	}
	ralink_soc = MT762X_SOC_MT7621AT;
	rev = __raw_readl(sysc + SYSC_REG_CHIP_REV);

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"MediaTek %s ver:%u eco:%u",
		name,
		(rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK,
		(rev & CHIP_REV_ECO_MASK));

	soc_info->mem_detect = mt7621_memory_detect;

	soc_dev_init(soc_info, rev);

	if (!register_cps_smp_ops())
		return;
	if (!register_cmp_smp_ops())
		return;
	if (!register_vsmp_smp_ops())
		return;
}
