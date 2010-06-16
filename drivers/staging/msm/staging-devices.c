#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/irqs.h>
#include <mach/sirc.h>
#include <mach/gpio.h>

#include "msm_mdp.h"
#include "memory_ll.h"
//#include "android_pmem.h"
#include <mach/board.h>

#ifdef CONFIG_MSM_SOC_REV_A
#define MSM_SMI_BASE 0xE0000000
#else
#define MSM_SMI_BASE 0x00000000
#endif


#define TOUCHPAD_SUSPEND 	34
#define TOUCHPAD_IRQ 		38

#define MSM_PMEM_MDP_SIZE	0x1591000

#ifdef CONFIG_MSM_SOC_REV_A
#define SMEM_SPINLOCK_I2C	"D:I2C02000021"
#else
#define SMEM_SPINLOCK_I2C	"S:6"
#endif

#define MSM_PMEM_ADSP_SIZE	0x1C00000

#define MSM_FB_SIZE             0x500000
#define MSM_FB_SIZE_ST15	0x800000
#define MSM_AUDIO_SIZE		0x80000
#define MSM_GPU_PHYS_SIZE 	SZ_2M

#ifdef CONFIG_MSM_SOC_REV_A
#define MSM_SMI_BASE		0xE0000000
#else
#define MSM_SMI_BASE		0x00000000
#endif

#define MSM_SHARED_RAM_PHYS	(MSM_SMI_BASE + 0x00100000)

#define MSM_PMEM_SMI_BASE	(MSM_SMI_BASE + 0x02B00000)
#define MSM_PMEM_SMI_SIZE	0x01500000

#define MSM_FB_BASE		MSM_PMEM_SMI_BASE
#define MSM_GPU_PHYS_BASE 	(MSM_FB_BASE + MSM_FB_SIZE)
#define MSM_PMEM_SMIPOOL_BASE	(MSM_GPU_PHYS_BASE + MSM_GPU_PHYS_SIZE)
#define MSM_PMEM_SMIPOOL_SIZE	(MSM_PMEM_SMI_SIZE - MSM_FB_SIZE \
					- MSM_GPU_PHYS_SIZE)

#if defined(CONFIG_FB_MSM_MDP40)
#define MDP_BASE          0xA3F00000
#define PMDH_BASE         0xAD600000
#define EMDH_BASE         0xAD700000
#define TVENC_BASE        0xAD400000
#else
#define MDP_BASE          0xAA200000
#define PMDH_BASE         0xAA600000
#define EMDH_BASE         0xAA700000
#define TVENC_BASE        0xAA400000
#endif

#define PMEM_KERNEL_EBI1_SIZE	(CONFIG_PMEM_KERNEL_SIZE * 1024 * 1024)

static struct resource msm_fb_resources[] = {
	{
		.flags  = IORESOURCE_DMA,
	}
};

static struct resource msm_mdp_resources[] = {
	{
		.name   = "mdp",
		.start  = MDP_BASE,
		.end    = MDP_BASE + 0x000F0000 - 1,
		.flags  = IORESOURCE_MEM,
	}
};

static struct platform_device msm_mdp_device = {
	.name   = "mdp",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(msm_mdp_resources),
	.resource       = msm_mdp_resources,
};

static struct platform_device msm_lcdc_device = {
	.name   = "lcdc",
	.id     = 0,
};

static int msm_fb_detect_panel(const char *name)
{
	int ret = -EPERM;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		if (!strncmp(name, "mddi_toshiba_wvga_pt", 20))
			ret = 0;
		else
			ret = -ENODEV;
	} else if ((machine_is_qsd8x50_surf() || machine_is_qsd8x50a_surf())
			&& !strcmp(name, "lcdc_external"))
		ret = 0;
	else if (0 /*machine_is_qsd8x50_grapefruit() */) {
		if (!strcmp(name, "lcdc_grapefruit_vga"))
			ret = 0;
		else
			ret = -ENODEV;
	} else if (machine_is_qsd8x50_st1()) {
		if (!strcmp(name, "lcdc_st1_wxga"))
			ret = 0;
		else
			ret = -ENODEV;
	} else if (machine_is_qsd8x50a_st1_5()) {
		if (!strcmp(name, "lcdc_st15") ||
		    !strcmp(name, "hdmi_sii9022"))
			ret = 0;
		else
			ret = -ENODEV;
	}

	return ret;
}

/* Only allow a small subset of machines to set the offset via
   FB PAN_DISPLAY */

static int msm_fb_allow_set_offset(void)
{
	return (machine_is_qsd8x50_st1() ||
		machine_is_qsd8x50a_st1_5()) ? 1 : 0;
}


static struct msm_fb_platform_data msm_fb_pdata = {
	.detect_client = msm_fb_detect_panel,
	.allow_set_offset = msm_fb_allow_set_offset,
};

static struct platform_device msm_fb_device = {
	.name   = "msm_fb",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(msm_fb_resources),
	.resource       = msm_fb_resources,
	.dev    = {
		.platform_data = &msm_fb_pdata,
	}
};

static void __init qsd8x50_allocate_memory_regions(void)
{
	void *addr;
	unsigned long size;
	if (machine_is_qsd8x50a_st1_5())
		size = MSM_FB_SIZE_ST15;
	else
		size = MSM_FB_SIZE;

	addr = alloc_bootmem(size); // (void *)MSM_FB_BASE;
	if (!addr)
		printk("Failed to allocate bootmem for framebuffer\n");


	msm_fb_resources[0].start = __pa(addr);
	msm_fb_resources[0].end = msm_fb_resources[0].start + size - 1;
	pr_info(KERN_ERR "using %lu bytes of SMI at %lx physical for fb\n",
		size, (unsigned long)addr);
}

static int msm_fb_lcdc_gpio_config(int on)
{
//	return 0;
	if (machine_is_qsd8x50_st1()) {
		if (on) {
			gpio_set_value(32, 1);
			mdelay(100);
			gpio_set_value(20, 1);
			gpio_set_value(17, 1);
			gpio_set_value(19, 1);
		} else {
			gpio_set_value(17, 0);
			gpio_set_value(19, 0);
			gpio_set_value(20, 0);
			mdelay(100);
			gpio_set_value(32, 0);
		}
	} else if (machine_is_qsd8x50a_st1_5()) {
		if (on) {
			gpio_set_value(17, 1);
			gpio_set_value(19, 1);
			gpio_set_value(20, 1);
			gpio_set_value(22, 0);
			gpio_set_value(32, 1);
			gpio_set_value(155, 1);
			//st15_hdmi_power(1);
			gpio_set_value(22, 1);

		} else {
			gpio_set_value(17, 0);
			gpio_set_value(19, 0);
			gpio_set_value(22, 0);
			gpio_set_value(32, 0);
			gpio_set_value(155, 0);
		//	st15_hdmi_power(0);
		}
	}
	return 0;
}


static struct lcdc_platform_data lcdc_pdata = {
	.lcdc_gpio_config = msm_fb_lcdc_gpio_config,
};

static struct msm_gpio msm_fb_st15_gpio_config_data[] = {
	{ GPIO_CFG(17, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "lcdc_en0" },
	{ GPIO_CFG(19, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "dat_pwr_sv" },
	{ GPIO_CFG(20, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "lvds_pwr_dn" },
	{ GPIO_CFG(22, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "lcdc_en1" },
	{ GPIO_CFG(32, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "lcdc_en2" },
	{ GPIO_CFG(103, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA), "hdmi_irq" },
	{ GPIO_CFG(155, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "hdmi_3v3" },
};

static struct msm_panel_common_pdata mdp_pdata = {
	.gpio = 98,
};

static struct platform_device *devices[] __initdata = {
	&msm_fb_device,
};


static void __init msm_register_device(struct platform_device *pdev, void *data)
{
	int ret;

	pdev->dev.platform_data = data;

	ret = platform_device_register(pdev);
	if (ret)
		dev_err(&pdev->dev,
			  "%s: platform_device_register() failed = %d\n",
			  __func__, ret);
}

void __init msm_fb_register_device(char *name, void *data)
{
	if (!strncmp(name, "mdp", 3))
		msm_register_device(&msm_mdp_device, data);
/*
	else if (!strncmp(name, "pmdh", 4))
		msm_register_device(&msm_mddi_device, data);
	else if (!strncmp(name, "emdh", 4))
		msm_register_device(&msm_mddi_ext_device, data);
	else if (!strncmp(name, "ebi2", 4))
		msm_register_device(&msm_ebi2_lcd_device, data);
	else if (!strncmp(name, "tvenc", 5))
		msm_register_device(&msm_tvenc_device, data);
	else */

	if (!strncmp(name, "lcdc", 4))
		msm_register_device(&msm_lcdc_device, data);
	/*else
		printk(KERN_ERR "%s: unknown device! %s\n", __func__, name);
*/
}

static void __init msm_fb_add_devices(void)
{
	int rc;
	msm_fb_register_device("mdp", &mdp_pdata);
//	msm_fb_register_device("pmdh", &mddi_pdata);
//	msm_fb_register_device("emdh", &mddi_pdata);
//	msm_fb_register_device("tvenc", 0);

	if (machine_is_qsd8x50a_st1_5()) {
/*		rc = st15_hdmi_vreg_init();
		if (rc)
			return;
*/
		rc = msm_gpios_request_enable(
			msm_fb_st15_gpio_config_data,
			ARRAY_SIZE(msm_fb_st15_gpio_config_data));
		if (rc) {
			printk(KERN_ERR "%s: unable to init lcdc gpios\n",
			       __func__);
			return;
		}
		msm_fb_register_device("lcdc", &lcdc_pdata);
	} else
		msm_fb_register_device("lcdc", 0);
}

int __init staging_init_pmem(void)
{
	qsd8x50_allocate_memory_regions();
	return 0;
}

int __init staging_init_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
	msm_fb_add_devices();
	return 0;
}

arch_initcall(staging_init_pmem);
arch_initcall(staging_init_devices);
