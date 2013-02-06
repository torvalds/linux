/* linux/arch/arm/mach-xxxx/board-c1-modems.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>

/* inlcude platform specific file */
#include <linux/cpufreq_pegasusq.h>
#include <linux/platform_data/modem.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-srom.h>
#include <plat/devs.h>
#include <plat/ehci.h>

#include <mach/dev.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos4.h>
#include <mach/regs-mem.h>
#include <mach/cpufreq.h>
#include <mach/sec_modem.h>
#include <mach/sromc-exynos4.h>

#ifdef CONFIG_USBHUB_USB3503
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/platform_data/usb3503.h>
#include <plat/usb-phy.h>
#endif

#define C1ATT_REV_0_7	9	/* rev0.7 == system_rev:9 */

static int __init init_modem(void);
static void setup_dpram_access_timing(enum dpram_speed speed);
static int host_port_enable(int port, int enable);
static int exynos_frequency_lock(struct device *dev);
static int exynos_frequency_unlock(struct device *dev);

static struct modem_io_t umts_io_devices[] = {
	[0] = {
		.name = "umts_boot0",
		.id = 0,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "CBD"
	},
	[1] = {
		.name = "umts_ipc0",
		.id = 235,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "RIL"
	},
	[2] = {
		.name = "umts_rfs0",
		.id = 245,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "RFS"
	},
	[3] = {
		.name = "multipdp",
		.id = 0,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
	},
	[4] = {
		.name = "rmnet0",
		.id = 10,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
	},
	[5] = {
		.name = "rmnet1",
		.id = 11,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
	},
	[6] = {
		.name = "rmnet2",
		.id = 12,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
	},
	[7] = {
		.name = "rmnet3",
		.id = 13,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
	},
	[8] = {
		.name = "umts_csd",	/* CS Video Telephony */
		.id = 1,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[9] = {
		.name = "umts_router",	/* AT Iface & Dial-up */
		.id = 25,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "Data Router"
	},
	[10] = {
		.name = "umts_dm0",	/* DM Port */
		.id = 28,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "DIAG"
	},
	[11] = {
		.name = "umts_loopback_cp2ap",
		.id = 30,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
		.tx_link = LINKDEV_DPRAM,
		.app = "CP Loopback"
	},
	[12] = {
		.name = "umts_loopback_ap2cp",
		.id = 31,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
		.app = "AP loopback"
	},
	[13] = {
		.name = "umts_ramdump0",
		.id = 0,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[14] = {
		.name = "umts_log",
		.id = 0,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[15] = {
		.name = "lte_ipc0",
		.id = 235,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_USB),
	},
};

/*
** addr_bits: 14 bits = 13 bits (8K words) + 1 bit (SFR)
** data_bits: 16 bits
** byte_acc: N/A
*/
static struct sromc_bus_cfg c1_sromc_bus_cfg = {
	.addr_bits = 14,
	.data_bits = 16,
	.byte_acc = 0,
};

/* For CMC221 IDPRAM (Internal DPRAM) */
#define CMC_IDPRAM_BASE		SROM_CS0_BASE
#define CMC_IDPRAM_SIZE		DPRAM_SIZE_16KB

#define CMC_IDPRAM_SFR_BASE	(CMC_IDPRAM_BASE + CMC_IDPRAM_SIZE)
#define CMC_IDPRAM_SFR_SIZE	DPRAM_SIZE_16KB

/*
** Static variables for CMC221
*/
static struct sromc_bank_cfg cmc_idpram_bank_cfg = {
	.csn = 0,
	.attr = SROMC_DATA_16,
};

static struct sromc_timing_cfg cmc_idpram_timing_cfg[] = {
	[DPRAM_SPEED_LOW] = {
		/* CP 33 MHz clk, 360 ns (72 cycles) with 200 MHz INT clk */
		.tacs = 0x0F << 28,
		.tcos = 0x0F << 24,
		.tacc = 0x1F << 16,
		.tcoh = 0x0A << 12,
		.tcah = 0x00 << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},
	[DPRAM_SPEED_MID] = {
		/* CP 66 MHz clk, 180 ns (36 cycles) with 200 MHz INT clk */
		.tacs = 0x01 << 28,
		.tcos = 0x02 << 24,
		.tacc = 0x1F << 16,
		.tcoh = 0x01 << 12,
		.tcah = 0x00 << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},
	[DPRAM_SPEED_HIGH] = {
		/* CP 133 MHz clk, 90 ns (18 cycles) with 200 MHz INT clk */
		.tacs = 0x01 << 28,
		.tcos = 0x01 << 24,
		.tacc = 0x0E << 16,
		.tcoh = 0x01 << 12,
		.tcah = 0x00 << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},
};

static struct modemlink_dpram_control cmc_idpram_ctrl = {
	.dp_type = CP_IDPRAM,
	.dpram_irq_flags = (IRQF_NO_SUSPEND | IRQF_TRIGGER_RISING),
	.setup_speed = setup_dpram_access_timing,
};

static struct resource umts_modem_res[] = {
	[RES_CP_ACTIVE_IRQ_ID] = {
		.name = STR_CP_ACTIVE_IRQ,
		.start = LTE_ACTIVE_IRQ,
		.end = LTE_ACTIVE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	[RES_DPRAM_MEM_ID] = {
		.name = STR_DPRAM_BASE,
		.start = CMC_IDPRAM_BASE,
		.end = CMC_IDPRAM_BASE + (CMC_IDPRAM_SIZE - 1),
		.flags = IORESOURCE_MEM,
	},
	[RES_DPRAM_IRQ_ID] = {
		.name = STR_DPRAM_IRQ,
		.start = CMC_IDPRAM_INT_IRQ_00,
		.end = CMC_IDPRAM_INT_IRQ_00,
		.flags = IORESOURCE_IRQ,
	},
	[RES_DPRAM_SFR_ID] = {
		.name = STR_DPRAM_SFR_BASE,
		.start = CMC_IDPRAM_SFR_BASE,
		.end = CMC_IDPRAM_SFR_BASE + (CMC_IDPRAM_SIZE - 1),
		.flags = IORESOURCE_MEM,
	},
};

static struct modem_data umts_modem_data = {
	.name = "cmc221",

	.gpio_cp_on = CP_CMC221_PMIC_PWRON,
	.gpio_cp_reset = CP_CMC221_CPU_RST,
	.gpio_phone_active = GPIO_LTE_ACTIVE,
#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT) || \
defined(CONFIG_MACH_BAFFIN_KOR_SKT) || defined(CONFIG_MACH_BAFFIN_KOR_KT) || \
defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	.gpio_pda_active   = GPIO_PDA_ACTIVE,
#endif

	.gpio_dpram_int = GPIO_CMC_IDPRAM_INT_00,
	.gpio_dpram_status = GPIO_CMC_IDPRAM_STATUS,
	.gpio_dpram_wakeup = GPIO_CMC_IDPRAM_WAKEUP,

	.gpio_slave_wakeup = GPIO_IPC_SLAVE_WAKEUP,
	.gpio_host_active = GPIO_ACTIVE_STATE,
	.gpio_host_wakeup = GPIO_IPC_HOST_WAKEUP,
	.gpio_dynamic_switching = GPIO_AP2CMC_INT2,

	.modem_net = UMTS_NETWORK,
	.modem_type = SEC_CMC221,
	.link_types = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB),
	.link_name = "cmc221_idpram",
	.dpram_ctl = &cmc_idpram_ctrl,

	.num_iodevs = ARRAY_SIZE(umts_io_devices),
	.iodevs = umts_io_devices,

	.use_handover = false,

	.ipc_version = SIPC_VER_50,
	.use_mif_log = true,
};

static struct platform_device umts_modem = {
	.name = "mif_sipc5",
	.id = 1,
	.num_resources = ARRAY_SIZE(umts_modem_res),
	.resource = umts_modem_res,
	.dev = {
		.platform_data = &umts_modem_data,
	},
};

/*
** Function definitions
*/
static void config_umts_modem_gpio(void)
{
	int err;
	unsigned gpio_cp_on = umts_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = umts_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = umts_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = umts_modem_data.gpio_phone_active;
	unsigned gpio_active_state = umts_modem_data.gpio_host_active;
	unsigned gpio_host_wakeup = umts_modem_data.gpio_host_wakeup;
	unsigned gpio_slave_wakeup = umts_modem_data.gpio_slave_wakeup;
	unsigned gpio_dpram_int = umts_modem_data.gpio_dpram_int;
	unsigned gpio_dpram_status = umts_modem_data.gpio_dpram_status;
	unsigned gpio_dpram_wakeup = umts_modem_data.gpio_dpram_wakeup;
	unsigned gpio_dynamic_switching =
			umts_modem_data.gpio_dynamic_switching;

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "CMC_ON");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n", "CMC_ON");
		} else {
			gpio_direction_output(gpio_cp_on, 0);
			s3c_gpio_setpull(gpio_cp_on, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "CMC_RST");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n", "CMC_RST");
		} else {
			gpio_direction_output(gpio_cp_rst, 0);
			s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n", "PDA_ACTIVE");
		} else {
			gpio_direction_output(gpio_pda_active, 0);
			s3c_gpio_setpull(gpio_pda_active, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "CMC_ACTIVE");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n", "CMC_ACTIVE");
		} else {
			/* Configure as a wake-up source */
			gpio_direction_input(gpio_phone_active);
			s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_DOWN);
			s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
		}
	}

	if (gpio_active_state) {
		err = gpio_request(gpio_active_state, "CMC_ACTIVE_STATE");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_ACTIVE_STATE");
		} else {
			gpio_direction_output(gpio_active_state, 0);
			s3c_gpio_setpull(gpio_active_state, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_slave_wakeup) {
		err = gpio_request(gpio_slave_wakeup, "CMC_SLAVE_WAKEUP");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_SLAVE_WAKEUP");
		} else {
			gpio_direction_output(gpio_slave_wakeup, 0);
			s3c_gpio_setpull(gpio_slave_wakeup, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_host_wakeup) {
		err = gpio_request(gpio_host_wakeup, "CMC_HOST_WAKEUP");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_HOST_WAKEUP");
		} else {
			/* Configure as a wake-up source */
			gpio_direction_input(gpio_host_wakeup);
			s3c_gpio_setpull(gpio_host_wakeup, S3C_GPIO_PULL_DOWN);
			s3c_gpio_cfgpin(gpio_host_wakeup, S3C_GPIO_SFN(0xF));
		}
	}

	if (gpio_dpram_int) {
		err = gpio_request(gpio_dpram_int, "CMC_DPRAM_INT");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_DPRAM_INT");
		} else {
			/* Configure as a wake-up source */
			gpio_direction_input(gpio_dpram_int);
			s3c_gpio_setpull(gpio_dpram_int, S3C_GPIO_PULL_NONE);
			s3c_gpio_cfgpin(gpio_dpram_int, S3C_GPIO_SFN(0xF));
		}
	}

	if (gpio_dpram_status) {
		err = gpio_request(gpio_dpram_status, "CMC_DPRAM_STATUS");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_DPRAM_STATUS");
		} else {
			gpio_direction_input(gpio_dpram_status);
			s3c_gpio_setpull(gpio_dpram_status, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_dpram_wakeup) {
		err = gpio_request(gpio_dpram_wakeup, "CMC_DPRAM_WAKEUP");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
				"CMC_DPRAM_WAKEUP");
		} else {
			gpio_direction_output(gpio_dpram_wakeup, 1);
			s3c_gpio_setpull(gpio_dpram_wakeup, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_dynamic_switching) {
		err = gpio_request(gpio_dynamic_switching, "DYNAMIC_SWITCHING");
		if (err) {
			mif_err("ERR: fail to request gpio %s\n",
					"DYNAMIC_SWITCHING\n");
		} else {
			gpio_direction_input(gpio_dynamic_switching);
			s3c_gpio_setpull(gpio_dynamic_switching,
					S3C_GPIO_PULL_DOWN);
		}
	}

	mif_info("done\n");
}

static void setup_dpram_access_timing(enum dpram_speed speed)
{
	sromc_config_access_timing(cmc_idpram_bank_cfg.csn,
				&cmc_idpram_timing_cfg[speed]);
}

static struct modemlink_pm_data umts_link_pm_data = {
	.name = "umts_link_pm",

	.gpio_link_enable    = 0,
	.gpio_link_active    = GPIO_ACTIVE_STATE,
	.gpio_link_hostwake  = GPIO_IPC_HOST_WAKEUP,
	.gpio_link_slavewake = GPIO_IPC_SLAVE_WAKEUP,

	.port_enable = host_port_enable,
/*
	.link_reconnect = umts_link_reconnect,
*/
	.freqlock = ATOMIC_INIT(0),
	.freq_lock = exynos_frequency_lock,
	.freq_unlock = exynos_frequency_unlock,

	.autosuspend_delay_ms = 2000,

	.has_usbhub = true, /* change this in init_modem if C1-ATT >= rev0.7 */
};

static struct modemlink_pm_link_activectl active_ctl;

#ifdef CONFIG_EXYNOS4_CPUFREQ
static int exynos_frequency_lock(struct device *dev)
{
	unsigned int level, cpufreq = 600; /* 200 ~ 1400 */
	unsigned int busfreq = 400200; /* 100100 ~ 400200 */
	int ret = 0;
	struct device *busdev = dev_get("exynos-busfreq");

	if (atomic_read(&umts_link_pm_data.freqlock) == 0) {
		/* cpu frequency lock */
		ret = exynos_cpufreq_get_level(cpufreq * 1000, &level);
		if (ret < 0) {
			mif_err("ERR: exynos_cpufreq_get_level fail: %d\n",
					ret);
			goto exit;
		}

		ret = exynos_cpufreq_lock(DVFS_LOCK_ID_USB_IF, level);
		if (ret < 0) {
			mif_err("ERR: exynos_cpufreq_lock fail: %d\n", ret);
			goto exit;
		}

		/* bus frequncy lock */
		if (!busdev) {
			mif_err("ERR: busdev is not exist\n");
			ret = -ENODEV;
			goto exit;
		}

		ret = dev_lock(busdev, dev, busfreq);
		if (ret < 0) {
			mif_err("ERR: dev_lock error: %d\n", ret);
			goto exit;
		}

		/* lock minimum number of cpu cores */
		cpufreq_pegasusq_min_cpu_lock(2);

		atomic_set(&umts_link_pm_data.freqlock, 1);
		mif_debug("level=%d, cpufreq=%d MHz, busfreq=%06d\n",
				level, cpufreq, busfreq);
	}
exit:
	return ret;
}

static int exynos_frequency_unlock(struct device *dev)
{
	int ret = 0;
	struct device *busdev = dev_get("exynos-busfreq");

	if (atomic_read(&umts_link_pm_data.freqlock) == 1) {
		/* cpu frequency unlock */
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_USB_IF);

		/* bus frequency unlock */
		ret = dev_unlock(busdev, dev);
		if (ret < 0) {
			mif_err("ERR: dev_unlock error: %d\n", ret);
			goto exit;
		}

		/* unlock minimum number of cpu cores */
		cpufreq_pegasusq_min_cpu_unlock();

		atomic_set(&umts_link_pm_data.freqlock, 0);
		mif_debug("success\n");
	}
exit:
	return ret;
}
#else
static int exynos_frequency_lock(void)
{
	return 0;
}

static int exynos_frequency_unlock(void)
{
	return 0;
}
#endif

bool modem_using_hub(void)
{
	return umts_link_pm_data.has_usbhub;
}

void set_slave_wake(void)
{
	int slavewake = umts_link_pm_data.gpio_link_slavewake;

	if (gpio_get_value(slavewake)) {
		gpio_direction_output(slavewake, 0);
		mif_info("> S-WUP 0\n");
		mdelay(10);
	}
	gpio_direction_output(slavewake, 1);
	mif_info("> S-WUP 1\n");
}

void set_hsic_lpa_states(int states)
{
	int val = gpio_get_value(umts_modem_data.gpio_cp_reset);
	struct modemlink_pm_data *pm_data = &umts_link_pm_data;

	mif_trace("\n");

	if (val) {
		switch (states) {
		case STATE_HSIC_LPA_ENTER:
			mif_info("lpa_enter\n");
			/* gpio_link_active == gpio_host_active in C1 */
			gpio_set_value(umts_modem_data.gpio_host_active, 0);
			mif_info("> H-ACT %d\n", 0);
			if (pm_data->hub_standby && pm_data->hub_pm_data)
				pm_data->hub_standby(pm_data->hub_pm_data);
			break;
		case STATE_HSIC_LPA_WAKE:
			mif_info("lpa_wake\n");
			gpio_set_value(umts_modem_data.gpio_host_active, 1);
			mif_info("> H-ACT %d\n", 1);
			break;
		case STATE_HSIC_LPA_PHY_INIT:
			mif_info("lpa_phy_init\n");
			if (!modem_using_hub() && active_ctl.gpio_initialized)
				set_slave_wake();
			break;
		}
	}
}

int get_cp_active_state(void)
{
	return gpio_get_value(umts_modem_data.gpio_phone_active);
}

static int (*usbhub_set_mode)(struct usb3503_hubctl *, int);
static struct usb3503_hubctl *usbhub_ctl;

void set_host_states(struct platform_device *pdev, int type)
{
	if (modem_using_hub())
		return;

	if (active_ctl.gpio_initialized) {
		mif_err("%s: > H-ACT %d\n", pdev->name, type);
		gpio_direction_output(umts_link_pm_data.gpio_link_active, type);
	} else {
		active_ctl.gpio_request_host_active = 1;
	}
}

static int usb3503_hub_handler(void (*set_mode)(void), void *ctl)
{
	if (!set_mode || !ctl)
		return -EINVAL;

	usbhub_set_mode = (int (*)(struct usb3503_hubctl *, int))set_mode;
	usbhub_ctl = (struct usb3503_hubctl *)ctl;

	mif_info("set_mode(%pF)\n", set_mode);

	return 0;
}

static int usb3503_hw_config(void)
{
	int err;

	err = gpio_request(GPIO_USB_HUB_RST, "HUB_RST");
	if (err) {
		mif_err("ERR: fail to request gpio %s\n", "HUB_RST");
	} else {
		gpio_direction_output(GPIO_USB_HUB_RST, 0);
		s3c_gpio_setpull(GPIO_USB_HUB_RST, S3C_GPIO_PULL_NONE);
	}
	s5p_gpio_set_drvstr(GPIO_USB_HUB_RST, S5P_GPIO_DRVSTR_LV1);
	/* need to check drvstr 1 or 2 */

	/* for USB3503 26Mhz Reference clock setting */
	err = gpio_request(GPIO_USB_HUB_INT, "HUB_INT");
	if (err) {
		mif_err("ERR: fail to request gpio %s\n", "HUB_INT");
	} else {
		gpio_direction_output(GPIO_USB_HUB_INT, 1);
		s3c_gpio_setpull(GPIO_USB_HUB_INT, S3C_GPIO_PULL_NONE);
	}

	return 0;
}

static int usb3503_reset_n(int val)
{
	gpio_set_value(GPIO_USB_HUB_RST, 0);

	/* hub off from cpuidle(LPA), skip the msleep schedule*/
	if (val) {
		usleep_range(3000, 3100);
		mif_info("val = %d\n", gpio_get_value(GPIO_USB_HUB_RST));

		gpio_set_value(GPIO_USB_HUB_RST, !!val);

		mif_info("val = %d\n", gpio_get_value(GPIO_USB_HUB_RST));
		usleep_range(7000, 7100);
	}
	return 0;
}

static struct usb3503_platform_data usb3503_pdata = {
	.initial_mode = USB3503_MODE_STANDBY,
	.reset_n = usb3503_reset_n,
	.register_hub_handler = usb3503_hub_handler,
	.port_enable = host_port_enable,
};

static struct i2c_board_info i2c_devs20_emul[] __initdata = {
	{
		I2C_BOARD_INFO(USB3503_I2C_NAME, 0x08),
		.platform_data = &usb3503_pdata,
	},
};

/* I2C20_EMUL */
static struct i2c_gpio_platform_data i2c20_platdata = {
	.sda_pin = GPIO_USB_HUB_SDA,
	.scl_pin = GPIO_USB_HUB_SCL,
	/*FIXME: need to timming tunning...  */
	.udelay	= 20,
};

static struct platform_device s3c_device_i2c20 = {
	.name = "i2c-gpio",
	.id = 20,
	.dev.platform_data = &i2c20_platdata,
};

static int host_port_enable(int port, int enable)
{
	int err;

	mif_info("port(%d) control(%d)\n", port, enable);

	if (enable) {
		if (modem_using_hub()) {
			err = usbhub_set_mode(usbhub_ctl, USB3503_MODE_HUB);
			if (err < 0) {
				mif_err("ERR: hub on fail\n");
				goto exit;
			}
		}
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 1);
		if (err < 0) {
			mif_err("ERR: port(%d) enable fail\n", port);
			goto exit;
		}
	} else {
		if (modem_using_hub()) {
			err = usbhub_set_mode(usbhub_ctl, USB3503_MODE_STANDBY);
			if (err < 0) {
				mif_err("ERR: hub off fail\n");
				goto exit;
			}
		}
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 0);
		if (err < 0) {
			mif_err("ERR: port(%d) enable fail\n", port);
			goto exit;
		}
	}

	err = gpio_direction_output(umts_modem_data.gpio_host_active, enable);
	mif_info("active state err(%d), en(%d), level(%d)\n",
		err, enable, gpio_get_value(umts_modem_data.gpio_host_active));

exit:
	return err;
}

static int __init init_usbhub(void)
{
	usb3503_hw_config();
	i2c_register_board_info(20, i2c_devs20_emul,
				ARRAY_SIZE(i2c_devs20_emul));

	platform_device_register(&s3c_device_i2c20);
	return 0;
}
#if !defined(CONFIG_MACH_BAFFIN)
device_initcall(init_usbhub);
#endif

static int __init init_modem(void)
{
	struct sromc_bus_cfg *bus_cfg;
	struct sromc_bank_cfg *bnk_cfg;
	struct sromc_timing_cfg *tm_cfg;

	mif_err("System Revision = %d\n", system_rev);

#ifdef CONFIG_MACH_C1_USA_ATT
	/* check C1-ATT rev >=0.7
	 * <=rev0.6: <EXYNOS>--hsic--<HUB>--usb--<CMC221S>
	 * >=rev0.7: <EXYNOS>--hsic--------------<CMC221D>
	 */
	if (system_rev >= C1ATT_REV_0_7)
		umts_link_pm_data.has_usbhub = false;
#endif
#ifdef CONFIG_MACH_BAFFIN
		umts_link_pm_data.has_usbhub = false;
#endif
#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT)
	if (system_rev >= 15)
		umts_link_pm_data.has_usbhub = false;
#endif

	/*
	** Complete modem_data configuration including link_pm_data
	*/
	umts_modem_data.link_pm_data = &umts_link_pm_data,

	/*
	** Configure GPIO pins for the modem
	*/
	config_umts_modem_gpio();
	active_ctl.gpio_initialized = 1;

	/*
	** Configure SROM controller
	*/
	if (sromc_enable() < 0)
		return -1;

	bus_cfg = &c1_sromc_bus_cfg;
	if (sromc_config_demux_gpio(bus_cfg) < 0)
		return -1;

	bnk_cfg = &cmc_idpram_bank_cfg;
	if (sromc_config_csn_gpio(bnk_cfg->csn) < 0)
		return -1;
	sromc_config_access_attr(bnk_cfg->csn, bnk_cfg->attr);

	tm_cfg = &cmc_idpram_timing_cfg[DPRAM_SPEED_LOW];
	sromc_config_access_timing(bnk_cfg->csn, tm_cfg);

	/*
	** Register the modem device
	*/
	platform_device_register(&umts_modem);

	return 0;
}
late_initcall(init_modem);

