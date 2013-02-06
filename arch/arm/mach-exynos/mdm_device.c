#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/ehci.h>
#include <linux/msm_charm.h>
#include <mach/mdm2.h>
#include "mdm_private.h"

#include <linux/cpufreq_pegasusq.h>
#include <mach/cpufreq.h>
#include <mach/dev.h>

static struct resource mdm_resources[] = {
	{
		.start	= GPIO_MDM2AP_ERR_FATAL,
		.end	= GPIO_MDM2AP_ERR_FATAL,
		.name	= "MDM2AP_ERRFATAL",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_AP2MDM_ERR_FATAL,
		.end	= GPIO_AP2MDM_ERR_FATAL,
		.name	= "AP2MDM_ERRFATAL",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_MDM2AP_STATUS,
		.end	= GPIO_MDM2AP_STATUS,
		.name	= "MDM2AP_STATUS",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_AP2MDM_STATUS,
		.end	= GPIO_AP2MDM_STATUS,
		.name	= "AP2MDM_STATUS",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_AP2MDM_PON_RESET_N,
		.end	= GPIO_AP2MDM_PON_RESET_N,
		.name	= "AP2MDM_SOFT_RESET",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_AP2MDM_PMIC_RESET_N,
		.end	= GPIO_AP2MDM_PMIC_RESET_N,
		.name	= "AP2MDM_PMIC_PWR_EN",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_AP2MDM_WAKEUP,
		.end	= GPIO_AP2MDM_WAKEUP,
		.name	= "AP2MDM_WAKEUP",
		.flags	= IORESOURCE_IO,
	},
#ifdef CONFIG_SIM_DETECT
	{
		.start	= GPIO_SIM_DETECT,
		.end	= GPIO_SIM_DETECT,
		.name	= "SIM_DETECT",
		.flags	= IORESOURCE_IO,
	},
#endif

};

#ifdef CONFIG_MDM_HSIC_PM
static struct resource mdm_pm_resource[] = {
	{
		.start	= GPIO_AP2MDM_HSIC_PORT_ACTIVE,
		.end	= GPIO_AP2MDM_HSIC_PORT_ACTIVE,
		.name	= "AP2MDM_HSIC_ACTIVE",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_MDM2AP_HSIC_PWR_ACTIVE,
		.end	= GPIO_MDM2AP_HSIC_PWR_ACTIVE,
		.name	= "MDM2AP_DEVICE_PWR_ACTIVE",
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= GPIO_MDM2AP_HSIC_RESUME_REQ,
		.end	= GPIO_MDM2AP_HSIC_RESUME_REQ,
		.name	= "MDM2AP_RESUME_REQ",
		.flags	= IORESOURCE_IO,
	},
};

static int exynos_frequency_lock(struct device *dev);
static int exynos_frequency_unlock(struct device *dev);

static struct mdm_hsic_pm_platform_data mdm_hsic_pm_pdata = {
	.freqlock = ATOMIC_INIT(0),
	.freq_lock = exynos_frequency_lock,
	.freq_unlock = exynos_frequency_unlock,
};

struct platform_device mdm_pm_device = {
	.name		= "mdm_hsic_pm0",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(mdm_pm_resource),
	.resource	= mdm_pm_resource,
};
#endif

static struct mdm_platform_data mdm_platform_data = {
	.mdm_version = "3.0",
	.ramdump_delay_ms = 2000,
	.early_power_on = 1,
	.sfr_query = 0,
	.vddmin_resource = NULL,
#ifdef CONFIG_USB_EHCI_S5P
	.peripheral_platform_device_ehci = &s5p_device_ehci,
#endif
#ifdef CONFIG_USB_OHCI_S5P
	.peripheral_platform_device_ohci = &s5p_device_ohci,
#endif
	.ramdump_timeout_ms = 120000,
};

static int exynos_frequency_lock(struct device *dev)
{
	unsigned int level, cpufreq = 1400; /* 200 ~ 1400 */
	unsigned int busfreq = 400200; /* 100100 ~ 400200 */
	int ret = 0;
	struct device *busdev = dev_get("exynos-busfreq");

	if (atomic_read(&mdm_hsic_pm_pdata.freqlock) == 0) {
		/* cpu frequency lock */
		ret = exynos_cpufreq_get_level(cpufreq * 1000, &level);
		if (ret < 0) {
			pr_err("ERR: exynos_cpufreq_get_level fail: %d\n",
					ret);
			goto exit;
		}

		ret = exynos_cpufreq_lock(DVFS_LOCK_ID_USB_IF, level);
		if (ret < 0) {
			pr_err("ERR: exynos_cpufreq_lock fail: %d\n", ret);
			goto exit;
		}

		/* bus frequncy lock */
		if (!busdev) {
			pr_err("ERR: busdev is not exist\n");
			ret = -ENODEV;
			goto exit;
		}

		ret = dev_lock(busdev, dev, busfreq);
		if (ret < 0) {
			pr_err("ERR: dev_lock error: %d\n", ret);
			goto exit;
		}

		/* lock minimum number of cpu cores */
		cpufreq_pegasusq_min_cpu_lock(2);

		atomic_set(&mdm_hsic_pm_pdata.freqlock, 1);
		pr_debug("level=%d, cpufreq=%d MHz, busfreq=%06d\n",
				level, cpufreq, busfreq);
	}
exit:
	return ret;
}

static int exynos_frequency_unlock(struct device *dev)
{
	int ret = 0;
	struct device *busdev = dev_get("exynos-busfreq");

	if (atomic_read(&mdm_hsic_pm_pdata.freqlock) == 1) {
		/* cpu frequency unlock */
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_USB_IF);

		/* bus frequency unlock */
		ret = dev_unlock(busdev, dev);
		if (ret < 0) {
			pr_err("ERR: dev_unlock error: %d\n", ret);
			goto exit;
		}

		/* unlock minimum number of cpu cores */
		cpufreq_pegasusq_min_cpu_unlock();

		atomic_set(&mdm_hsic_pm_pdata.freqlock, 0);
		pr_debug("success\n");
	}
exit:
	return ret;
}

struct platform_device mdm_device = {
	.name		= "mdm2_modem",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(mdm_resources),
	.resource	= mdm_resources,
};

static int __init init_mdm_modem(void)
{
	int ret;
	pr_info("%s: registering modem dev, pm dev\n", __func__);

	mdm_pm_device.dev.platform_data = &mdm_hsic_pm_pdata;
	((struct mdm_hsic_pm_platform_data *)
	 mdm_pm_device.dev.platform_data)->dev =
		&mdm_pm_device.dev;
#ifdef CONFIG_MDM_HSIC_PM
	ret = platform_device_register(&mdm_pm_device);
	if (ret < 0) {
		pr_err("%s: fail to register mdm hsic pm dev(err:%d)\n",
								__func__, ret);
		return ret;
	}
#endif
	mdm_device.dev.platform_data = &mdm_platform_data;
	ret = platform_device_register(&mdm_device);
	if (ret < 0) {
		pr_err("%s: fail to register mdm modem dev(err:%d)\n",
								__func__, ret);
		return ret;
	}
	return 0;
}
module_init(init_mdm_modem);
