// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SoC Power Management Controller Driver Quirks
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_data/x86/amd-fch.h>

#include "pmc.h"

struct quirk_entry {
	u32 s2idle_bug_mmio;
	bool spurious_8042;
};

static struct quirk_entry quirk_s2idle_bug = {
	.s2idle_bug_mmio = FCH_PM_BASE + FCH_PM_SCRATCH,
};

static struct quirk_entry quirk_spurious_8042 = {
	.spurious_8042 = true,
};

static const struct dmi_system_id fwbug_list[] = {
	{
		.ident = "L14 Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20X5"),
		}
	},
	{
		.ident = "T14s Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20XF"),
		}
	},
	{
		.ident = "X13 Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20XH"),
		}
	},
	{
		.ident = "T14 Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20XK"),
		}
	},
	{
		.ident = "T14 Gen1 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20UD"),
		}
	},
	{
		.ident = "T14 Gen1 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20UE"),
		}
	},
	{
		.ident = "T14s Gen1 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20UH"),
		}
	},
	{
		.ident = "T14s Gen1 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20UJ"),
		}
	},
	{
		.ident = "P14s Gen1 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20Y1"),
		}
	},
	{
		.ident = "P14s Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21A0"),
		}
	},
	{
		.ident = "P14s Gen2 AMD",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21A1"),
		}
	},
	/* https://bugzilla.kernel.org/show_bug.cgi?id=218024 */
	{
		.ident = "V14 G4 AMN",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82YT"),
		}
	},
	{
		.ident = "V14 G4 AMN",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "83GE"),
		}
	},
	{
		.ident = "V15 G4 AMN",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82YU"),
		}
	},
	{
		.ident = "V15 G4 AMN",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "83CQ"),
		}
	},
	{
		.ident = "IdeaPad 1 14AMN7",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82VF"),
		}
	},
	{
		.ident = "IdeaPad 1 15AMN7",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82VG"),
		}
	},
	{
		.ident = "IdeaPad 1 15AMN7",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82X5"),
		}
	},
	{
		.ident = "IdeaPad Slim 3 14AMN8",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82XN"),
		}
	},
	{
		.ident = "IdeaPad Slim 3 15AMN8",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82XQ"),
		}
	},
	/* https://gitlab.freedesktop.org/drm/amd/-/issues/2684 */
	{
		.ident = "HP Laptop 15s-eq2xxx",
		.driver_data = &quirk_s2idle_bug,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Laptop 15s-eq2xxx"),
		}
	},
	/* https://community.frame.work/t/tracking-framework-amd-ryzen-7040-series-lid-wakeup-behavior-feedback/39128 */
	{
		.ident = "Framework Laptop 13 (Phoenix)",
		.driver_data = &quirk_spurious_8042,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Laptop 13 (AMD Ryzen 7040Series)"),
			DMI_MATCH(DMI_BIOS_VERSION, "03.03"),
		}
	},
	{
		.ident = "Framework Laptop 13 (Phoenix)",
		.driver_data = &quirk_spurious_8042,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Laptop 13 (AMD Ryzen 7040Series)"),
			DMI_MATCH(DMI_BIOS_VERSION, "03.05"),
		}
	},
	{
		.ident = "MECHREVO Wujie 14X (GX4HRXL)",
		.driver_data = &quirk_spurious_8042,
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "WUJIE14-GX4HRXL"),
		}
	},
	/* https://bugzilla.kernel.org/show_bug.cgi?id=220116 */
	{
		.ident = "PCSpecialist Lafite Pro V 14M",
		.driver_data = &quirk_spurious_8042,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PCSpecialist"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lafite Pro V 14M"),
		}
	},
	{}
};

/*
 * Laptops that run a SMI handler during the D3->D0 transition that occurs
 * specifically when exiting suspend to idle which can cause
 * large delays during resume when the IOMMU translation layer is enabled (the default
 * behavior) for NVME devices:
 *
 * To avoid this firmware problem, skip the SMI handler on these machines before the
 * D0 transition occurs.
 */
static void amd_pmc_skip_nvme_smi_handler(u32 s2idle_bug_mmio)
{
	void __iomem *addr;
	u8 val;

	if (!request_mem_region_muxed(s2idle_bug_mmio, 1, "amd_pmc_pm80"))
		return;

	addr = ioremap(s2idle_bug_mmio, 1);
	if (!addr)
		goto cleanup_resource;

	val = ioread8(addr);
	iowrite8(val & ~BIT(0), addr);

	iounmap(addr);
cleanup_resource:
	release_mem_region(s2idle_bug_mmio, 1);
}

void amd_pmc_process_restore_quirks(struct amd_pmc_dev *dev)
{
	if (dev->quirks && dev->quirks->s2idle_bug_mmio)
		amd_pmc_skip_nvme_smi_handler(dev->quirks->s2idle_bug_mmio);
}

void amd_pmc_quirks_init(struct amd_pmc_dev *dev)
{
	const struct dmi_system_id *dmi_id;

	if (dev->cpu_id == AMD_CPU_ID_CZN)
		dev->disable_8042_wakeup = true;

	dmi_id = dmi_first_match(fwbug_list);
	if (!dmi_id)
		return;
	dev->quirks = dmi_id->driver_data;
	if (dev->quirks->s2idle_bug_mmio)
		pr_info("Using s2idle quirk to avoid %s platform firmware bug\n",
			dmi_id->ident);
	if (dev->quirks->spurious_8042)
		dev->disable_8042_wakeup = true;
}
