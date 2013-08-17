/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/persistent_ram.h>
#include <linux/cma.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/regs-serial.h>
#include <plat/iic.h>
#include <plat/watchdog.h>

#include <mach/exynos_fiq_debugger.h>
#include <mach/map.h>
#include <mach/hs-iic.h>
#include <mach/regs-pmu.h>
#include <mach/pmu.h>

#include "../../../drivers/staging/android/ram_console.h"
#include "common.h"
#include "board-smdk5410.h"
#include "resetreason.h"

static struct ram_console_platform_data ramconsole_pdata;

static struct platform_device ramconsole_device = {
	.name	= "ram_console",
	.id	= -1,
	.dev	= {
		.platform_data = &ramconsole_pdata,
	},
};

static struct platform_device persistent_trace_device = {
	.name	= "persistent_trace",
	.id	= -1,
};

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK5410_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK5410_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK5410_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdk5410_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK5410_UCON_DEFAULT,
		.ulcon		= SMDK5410_ULCON_DEFAULT,
		.ufcon		= SMDK5410_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK5410_UCON_DEFAULT,
		.ulcon		= SMDK5410_ULCON_DEFAULT,
		.ufcon		= SMDK5410_UFCON_DEFAULT,
	},
	[2] = {
#ifndef CONFIG_EXYNOS_FIQ_DEBUGGER
	/*
	 * Don't need to initialize hwport 2, when FIQ debugger is
	 * enabled. Because it will be handled by fiq_debugger.
	 */
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK5410_UCON_DEFAULT,
		.ulcon		= SMDK5410_ULCON_DEFAULT,
		.ufcon		= SMDK5410_UFCON_DEFAULT,
	},
	[3] = {
#endif
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK5410_UCON_DEFAULT,
		.ulcon		= SMDK5410_ULCON_DEFAULT,
		.ufcon		= SMDK5410_UFCON_DEFAULT,
	},
};

static int exynos5_notifier_call(struct notifier_block *this,
		unsigned long code, void *_cmd)
{
	int mode = 0;

	if ((code == SYS_RESTART) && _cmd)
		if (!strcmp((char *)_cmd, "recovery"))
			mode = 0xf;

	__raw_writel(mode, EXYNOS_INFORM4);

	return NOTIFY_DONE;
}

static struct notifier_block exynos5_reboot_notifier = {
	.notifier_call = exynos5_notifier_call,
};

static struct platform_device *smdk5410_devices[] __initdata = {
	&ramconsole_device,
	&persistent_trace_device,
	&s3c_device_wdt,
	&s3c_device_rtc,
	&s3c_device_adc,
#ifdef CONFIG_PVR_SGX
	&exynos5_device_g3d,
#endif
#ifdef CONFIG_S5P_DEV_ACE
	&s5p_device_ace,
#endif
};

#if defined(CONFIG_CMA)
#include "reserve-mem.h"
static void __init exynos_reserve_mem(void)
{
	static struct cma_region regions[] = {
		{
			.name = "ion",
			.size = (
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD
			CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD +
#endif
			128) * SZ_1M,
			{ .alignment = SZ_1M },
		},
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
#ifdef CONFIG_ION_EXYNOS_DRM_MFC_SH
		{
			.name = "drm_mfc_sh",
			.size = SZ_1M,
		},
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_G2D_WFD
		{
			.name = "drm_g2d_wfd",
			.size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_G2D_WFD *
				SZ_1K,
		},
#endif
#endif
		{
			.size = 0
		},
	};
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	static struct cma_region regions_secure[] = {
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_FIMD_VIDEO
	       {
			.name = "drm_fimd_video",
			.size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_FIMD_VIDEO *
				SZ_1K,
			{
				.alignment = SZ_1M,
			}
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_OUTPUT
	       {
			.name = "drm_mfc_output",
			.size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_OUTPUT *
				SZ_1K,
			{
				.alignment = SZ_1M,
			}
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_INPUT
	       {
			.name = "drm_mfc_input",
			.size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_INPUT *
				SZ_1K,
			{
				.alignment = SZ_1M,
			}
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MFC_FW
		{
			.name = "drm_mfc_fw",
			.size = SZ_1M,
			{
				.alignment = SZ_1M,
			}
		},
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_SECTBL
		{
			.name = "drm_sectbl",
			.size = SZ_1M,
			{
				.alignment = SZ_1M,
			}
		},
#endif
		{
			.size = 0
		},
	};
#else /* !CONFIG_EXYNOS_CONTENT_PATH_PROTECTION */
	struct cma_region *regions_secure = NULL;
#endif /* CONFIG_EXYNOS_CONTENT_PATH_PROTECTION */
	static const char map[] __initconst =
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
		"ion-exynos/mfc_sh=drm_mfc_sh;"
		"ion-exynos/g2d_wfd=drm_g2d_wfd;"
		"ion-exynos/fimd_video=drm_fimd_video;"
		"ion-exynos/mfc_output=drm_mfc_output;"
		"ion-exynos/mfc_input=drm_mfc_input;"
		"ion-exynos/mfc_fw=drm_mfc_fw;"
		"ion-exynos/sectbl=drm_sectbl;"
		"s5p-smem/mfc_sh=drm_mfc_sh;"
		"s5p-smem/g2d_wfd=drm_g2d_wfd;"
		"s5p-smem/fimd_video=drm_fimd_video;"
		"s5p-smem/mfc_output=drm_mfc_output;"
		"s5p-smem/mfc_input=drm_mfc_input;"
		"s5p-smem/mfc_fw=drm_mfc_fw;"
		"s5p-smem/sectbl=drm_sectbl;"
#endif
		"ion-exynos=ion;";
	exynos_cma_region_reserve(regions, regions_secure, NULL, map);
}
#else /*!CONFIG_CMA*/
static inline void exynos_reserve_mem(void)
{
}
#endif

static void __init smdk5410_map_io(void)
{
	clk_xusbxti.rate = 24000000;
	clk_xxti.rate = 24000000;

	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(clk_xusbxti.rate);
	s3c24xx_init_uarts(smdk5410_uartcfgs, ARRAY_SIZE(smdk5410_uartcfgs));
}

static struct persistent_ram_descriptor smdk5410_prd[] __initdata = {
	{
		.name = "ram_console",
		.size = SZ_2M,
	},
#ifdef CONFIG_PERSISTENT_TRACER
	{
		.name = "persistent_trace",
		.size = SZ_1M,
	},
#endif
};

static struct persistent_ram smdk5410_pr __initdata = {
	.descs		= smdk5410_prd,
	.num_descs	= ARRAY_SIZE(smdk5410_prd),
	.start		= PLAT_PHYS_OFFSET + SZ_512M,
#ifdef CONFIG_PERSISTENT_TRACER
	.size		= 3 * SZ_1M,
#else
	.size		= SZ_2M,
#endif
};

/* WDT */
static struct s3c_watchdog_platdata smdk5410_watchdog_platform_data = {
	exynos_pmu_wdt_control,
	PMU_WDT_RESET_TYPE1,
};

static void __init smdk5410_init_early(void)
{
	persistent_ram_early_init(&smdk5410_pr);
}

static void __init smdk5410_machine_init(void)
{
#ifdef CONFIG_EXYNOS_FIQ_DEBUGGER
	exynos_serial_debug_init(2, 0);
#endif

	s3c_watchdog_set_platdata(&smdk5410_watchdog_platform_data);

	exynos5_smdk5410_clock_init();
	exynos5_smdk5410_mmc_init();

	s3c_i2c0_set_platdata(NULL);
	exynos5_smdk5410_audio_init();
	exynos5_smdk5410_usb_init();
	exynos5_smdk5410_power_init();
	exynos5_smdk5410_input_init();
	exynos5_smdk5410_media_init();
	exynos5_smdk5410_display_init();

	ramconsole_pdata.bootinfo = exynos_get_resetreason();
	platform_add_devices(smdk5410_devices, ARRAY_SIZE(smdk5410_devices));

	register_reboot_notifier(&exynos5_reboot_notifier);
}

MACHINE_START(SMDK5410, "SMDK5410")
	.atag_offset	= 0x100,
	.init_early	= smdk5410_init_early,
	.init_irq	= exynos5_init_irq,
	.map_io		= smdk5410_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdk5410_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos5_restart,
	.reserve	= exynos_reserve_mem,
MACHINE_END
