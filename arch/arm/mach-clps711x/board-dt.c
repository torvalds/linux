// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Author: Alexander Shiyan <shc_work@mail.ru>, 2016
 */

#include <linux/io.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/sizes.h>

#include <linux/mfd/syscon/clps711x.h>

#include <asm/system_info.h>
#include <asm/system_misc.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#define CLPS711X_VIRT_BASE	IOMEM(0xfeff4000)
#define CLPS711X_PHYS_BASE	(0x80000000)
# define SYSFLG1		(0x0140)
# define HALT			(0x0800)
# define UNIQID			(0x2440)
# define RANDID0		(0x2700)
# define RANDID1		(0x2704)
# define RANDID2		(0x2708)
# define RANDID3		(0x270c)

static struct map_desc clps711x_io_desc __initdata = {
	.virtual	= (unsigned long)CLPS711X_VIRT_BASE,
	.pfn		= __phys_to_pfn(CLPS711X_PHYS_BASE),
	.length		= 48 * SZ_1K,
	.type		= MT_DEVICE,
};

static void __init clps711x_map_io(void)
{
	iotable_init(&clps711x_io_desc, 1);
}

static const struct resource clps711x_cpuidle_res =
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + HALT, SZ_128);

static void __init clps711x_init(void)
{
	u32 id[5];

	id[0] = readl(CLPS711X_VIRT_BASE + UNIQID);
	id[1] = readl(CLPS711X_VIRT_BASE + RANDID0);
	id[2] = readl(CLPS711X_VIRT_BASE + RANDID1);
	id[3] = readl(CLPS711X_VIRT_BASE + RANDID2);
	id[4] = readl(CLPS711X_VIRT_BASE + RANDID3);
	system_rev = SYSFLG1_VERID(readl(CLPS711X_VIRT_BASE + SYSFLG1));

	add_device_randomness(id, sizeof(id));

	system_serial_low = id[0];

	platform_device_register_simple("clps711x-cpuidle", PLATFORM_DEVID_NONE,
					&clps711x_cpuidle_res, 1);
}

static void clps711x_restart(enum reboot_mode mode, const char *cmd)
{
	soft_restart(0);
}

static const char *const clps711x_compat[] __initconst = {
	"cirrus,ep7209",
	NULL
};

DT_MACHINE_START(CLPS711X_DT, "Cirrus Logic CLPS711X (Device Tree Support)")
	.dt_compat	= clps711x_compat,
	.map_io		= clps711x_map_io,
	.init_late	= clps711x_init,
	.restart	= clps711x_restart,
MACHINE_END
