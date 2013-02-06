/* linux/arch/arm/mach-xxxx/board-midas-modems.c
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
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>

#include <plat/gpio-cfg.h>

#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>
#include <linux/interrupt.h>

/* umts target platform data */
static struct modem_io_t umts_io_devices[] = {
	[0] = {
		.name = "umts_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[1] = {
		.name = "umts_rfs0",
		.id = 0x41,
		.format = IPC_RFS,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[2] = {
		.name = "umts_boot0",
		.id = 0x0,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[3] = {
		.name = "multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[4] = {
#ifdef CONFIG_SLP
		.name = "pdp0",
#else
		.name = "rmnet0",
#endif
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[5] = {
#ifdef CONFIG_SLP
		.name = "pdp1",
#else
		.name = "rmnet1",
#endif
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[6] = {
#ifdef CONFIG_SLP
		.name = "pdp2",
#else
		.name = "rmnet2",
#endif
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[7] = {
		.name = "umts_router",
		.id = 0x39,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[8] = {
		.name = "umts_csd",
		.id = 0x21,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[9] = {
		.name = "umts_ramdump0",
		.id = 0x0,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[10] = {
		.name = "umts_loopback0",
		.id = 0x3f,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
};

/* To get modem state, register phone active irq using resource */
static struct resource umts_modem_res[] = {
	[0] = {
		.name = "umts_phone_active",
		.start = IRQ_EINT14,	/* GPIO_PHONE_ACTIVE */
		.end = IRQ_EINT14,	/* GPIO_PHONE_ACTIVE */
		.flags = IORESOURCE_IRQ,
	},
	[1] = {
		.name = "link_pm_hostwake",
		.start = IRQ_EINT9,	/* GPIO_IPC_HOST_WAKEUP */
		.end = IRQ_EINT9,		/* GPIO_IPC_HOST_WAKEUP */
		.flags = IORESOURCE_IRQ,
	},
};

static int umts_link_ldo_enble(bool enable)
{
	/* Exynos HSIC V1.2 LDO was controlled by kernel */
	return 0;
}


static void xmm_gpio_revers_bias_clear(void);
static void xmm_gpio_revers_bias_restore(void);


static int umts_link_reconnect(void);
static struct modemlink_pm_data modem_link_pm_data = {
	.name = "link_pm",
	.link_ldo_enable = umts_link_ldo_enble,
	.gpio_link_enable = 0,
	.gpio_link_active = GPIO_ACTIVE_STATE,
	.gpio_link_hostwake = GPIO_IPC_HOST_WAKEUP,
	.gpio_link_slavewake = GPIO_IPC_SLAVE_WAKEUP,
	.link_reconnect = umts_link_reconnect,
};

static struct modemlink_pm_link_activectl active_ctl;

static struct modem_data umts_modem_data = {
	.name = "xmm6260",

	.gpio_cp_on = GPIO_PHONE_ON,
	.gpio_reset_req_n = GPIO_CP_REQ_RESET,
	.gpio_cp_reset = GPIO_CP_RST,
	.gpio_pda_active = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_PHONE_ACTIVE,
	.gpio_cp_dump_int = GPIO_CP_DUMP_INT,
	.gpio_flm_uart_sel = 0,
	.gpio_cp_warm_reset = 0,
#if defined(CONFIG_SIM_DETECT)
	.gpio_sim_detect = GPIO_SIM_DETECT,
#endif

	.modem_type = IMC_XMM6260,
	.link_types = LINKTYPE(LINKDEV_HSIC),
	.modem_net = UMTS_NETWORK,
	.use_handover = false,

	.num_iodevs = ARRAY_SIZE(umts_io_devices),
	.iodevs = umts_io_devices,

	.link_pm_data = &modem_link_pm_data,
	.gpio_revers_bias_clear = xmm_gpio_revers_bias_clear,
	.gpio_revers_bias_restore = xmm_gpio_revers_bias_restore,

};

static void xmm_gpio_revers_bias_clear(void)
{
	gpio_direction_output(umts_modem_data.gpio_reset_req_n, 0);
	gpio_direction_output(umts_modem_data.gpio_phone_active, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_active, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_slavewake, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_hostwake, 0);
	gpio_direction_output(umts_modem_data.gpio_cp_dump_int, 0);

	if (umts_modem_data.gpio_sim_detect)
		gpio_direction_output(umts_modem_data.gpio_sim_detect, 0);

	msleep(20);
}

static void xmm_gpio_revers_bias_restore(void)
{
	unsigned gpio_link_hostwake = modem_link_pm_data.gpio_link_hostwake;
	unsigned gpio_cp_dump_int = umts_modem_data.gpio_cp_dump_int;
	unsigned gpio_sim_detect = umts_modem_data.gpio_sim_detect;

	s3c_gpio_cfgpin(umts_modem_data.gpio_phone_active, S3C_GPIO_SFN(0xF));
	gpio_direction_output(gpio_link_hostwake, 0);
	s3c_gpio_cfgpin(gpio_link_hostwake, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(gpio_link_hostwake, S3C_GPIO_PULL_NONE);
	irq_set_irq_type(gpio_to_irq(gpio_link_hostwake),
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING);
	enable_irq_wake(gpio_to_irq(gpio_link_hostwake));

	gpio_direction_input(gpio_cp_dump_int);
	/*
	s3c_gpio_cfgpin(gpio_cp_dump_int, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(gpio_cp_dump_int, S3C_GPIO_PULL_DOWN);
	irq_set_irq_type(gpio_to_irq(gpio_cp_dump_int),
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING);
	enable_irq_wake(gpio_to_irq(gpio_cp_dump_int));
	*/

	/* gpio_direction_input(mc->gpio_suspend_request); */

	if (umts_modem_data.gpio_sim_detect) {
		gpio_direction_output(gpio_sim_detect, 0);
		s3c_gpio_cfgpin(gpio_sim_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_sim_detect, S3C_GPIO_PULL_NONE);
		irq_set_irq_type(gpio_to_irq(gpio_sim_detect),
				IRQ_TYPE_EDGE_BOTH);
		enable_irq_wake(gpio_to_irq(gpio_sim_detect));
	}
}

/* HSIC specific function */
void set_slave_wake(void)
{
	int spin = 20;
	if (gpio_get_value(modem_link_pm_data.gpio_link_hostwake)) {
		pr_info("[MODEM_IF]Slave Wake\n");
		if (gpio_get_value(modem_link_pm_data.gpio_link_slavewake)) {
			gpio_direction_output(
			modem_link_pm_data.gpio_link_slavewake, 0);
			mdelay(10);
		}
		gpio_direction_output(
			modem_link_pm_data.gpio_link_slavewake, 1);
	}
}

void set_host_states(struct platform_device *pdev, int type)
{
	int val = gpio_get_value(umts_modem_data.gpio_cp_reset);

	if (!val) {
		pr_info("CP not ready, Active State low\n");
		return;
	}

	if (active_ctl.gpio_initialized) {
		pr_err("[MODEM_IF]Active States =%d, %s\n", type, pdev->name);
		gpio_direction_output(modem_link_pm_data.gpio_link_active,
			type);
	} else
		active_ctl.gpio_request_host_active = 1;
}

void set_hsic_lpa_states(int states)
{
	int val = gpio_get_value(umts_modem_data.gpio_cp_reset);

	mif_trace("\n");

	if (val) {
		switch (states) {
		case STATE_HSIC_LPA_ENTER:
			gpio_set_value(modem_link_pm_data.gpio_link_active, 0);
			gpio_set_value(umts_modem_data.gpio_pda_active, 0);
			pr_info(LOG_TAG "set hsic lpa enter: "
				"active state (%d)" ", pda active (%d)\n",
				gpio_get_value(
					modem_link_pm_data.gpio_link_active),
				gpio_get_value(umts_modem_data.gpio_pda_active)
				);
			break;
		case STATE_HSIC_LPA_WAKE:
			gpio_set_value(umts_modem_data.gpio_pda_active, 1);
			pr_info(LOG_TAG "set hsic lpa wake: "
				"pda active (%d)\n",
				gpio_get_value(umts_modem_data.gpio_pda_active)
				);
			break;
		case STATE_HSIC_LPA_PHY_INIT:
			gpio_set_value(umts_modem_data.gpio_pda_active, 1);
			set_slave_wake();
			pr_info(LOG_TAG "set hsic lpa phy init: "
				"slave wake-up (%d)\n",
				gpio_get_value(
					modem_link_pm_data.gpio_link_slavewake)
				);
			break;
		}
	}
}

int get_cp_active_state(void)
{
	return gpio_get_value(umts_modem_data.gpio_phone_active);
}

static int umts_link_reconnect(void)
{
	if (gpio_get_value(umts_modem_data.gpio_phone_active) &&
		gpio_get_value(umts_modem_data.gpio_cp_reset)) {
		pr_info("[MODEM_IF] trying reconnect link\n");
		gpio_set_value(modem_link_pm_data.gpio_link_active, 0);
		mdelay(10);
		set_slave_wake();
		gpio_set_value(modem_link_pm_data.gpio_link_active, 1);
	} else
		return -ENODEV;

	return 0;
}

/* if use more than one modem device, then set id num */
static struct platform_device umts_modem = {
	.name = "modem_if",
	.id = -1,
	.num_resources = ARRAY_SIZE(umts_modem_res),
	.resource = umts_modem_res,
	.dev = {
		.platform_data = &umts_modem_data,
	},
};

static void umts_modem_cfg_gpio(void)
{
	int err = 0;

	unsigned gpio_reset_req_n = umts_modem_data.gpio_reset_req_n;
	unsigned gpio_cp_on = umts_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = umts_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = umts_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = umts_modem_data.gpio_phone_active;
	unsigned gpio_cp_dump_int = umts_modem_data.gpio_cp_dump_int;
	unsigned gpio_flm_uart_sel = umts_modem_data.gpio_flm_uart_sel;
	unsigned gpio_sim_detect = umts_modem_data.gpio_sim_detect;
	unsigned irq_phone_active = umts_modem_res[0].start;

	if (gpio_reset_req_n) {
		err = gpio_request(gpio_reset_req_n, "RESET_REQ_N");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "RESET_REQ_N", err);
		}
		gpio_direction_output(gpio_reset_req_n, 0);
	}

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "CP_ON");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "CP_ON", err);
		}
		gpio_direction_output(gpio_cp_on, 0);
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "CP_RST");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "CP_RST", err);
		}
		gpio_direction_output(gpio_cp_rst, 0);
		s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
	}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "PDA_ACTIVE", err);
		}
		gpio_direction_output(gpio_pda_active, 0);
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "PHONE_ACTIVE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "PHONE_ACTIVE", err);
		}
		/* gpio_direction_input(gpio_phone_active); */
		s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_NONE);
		pr_err("check phone active = %d\n", irq_phone_active);
	}

	if (gpio_cp_dump_int) {
		err = gpio_request(gpio_cp_dump_int, "CP_DUMP_INT");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "CP_DUMP_INT", err);
		}
		gpio_direction_input(gpio_cp_dump_int);
	}

	if (gpio_flm_uart_sel) {
		err = gpio_request(gpio_flm_uart_sel, "GPS_UART_SEL");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "GPS_UART_SEL", err);
		}
		gpio_direction_output(gpio_reset_req_n, 0);
	}

	if (gpio_phone_active)
		irq_set_irq_type(gpio_to_irq(gpio_phone_active),
							IRQ_TYPE_LEVEL_HIGH);

	if (gpio_sim_detect) {
		err = gpio_request(gpio_sim_detect, "SIM_DETECT");
		if (err)
			printk(KERN_ERR "fail to request gpio %s: %d\n",
				"SIM_DETECT", err);

		/* gpio_direction_input(gpio_sim_detect); */
		s3c_gpio_cfgpin(gpio_sim_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_sim_detect, S3C_GPIO_PULL_NONE);
		irq_set_irq_type(gpio_to_irq(gpio_sim_detect),
							IRQ_TYPE_EDGE_BOTH);
	}

	printk(KERN_INFO "umts_modem_cfg_gpio done\n");
}

static void modem_link_pm_config_gpio(void)
{
	int err = 0;

	unsigned gpio_link_enable = modem_link_pm_data.gpio_link_enable;
	unsigned gpio_link_active = modem_link_pm_data.gpio_link_active;
	unsigned gpio_link_hostwake = modem_link_pm_data.gpio_link_hostwake;
	unsigned gpio_link_slavewake = modem_link_pm_data.gpio_link_slavewake;
	/* unsigned irq_link_hostwake = umts_modem_res[1].start; */

	if (gpio_link_enable) {
		err = gpio_request(gpio_link_enable, "LINK_EN");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "LINK_EN", err);
		}
		gpio_direction_output(gpio_link_enable, 0);
	}

	if (gpio_link_active) {
		err = gpio_request(gpio_link_active, "LINK_ACTIVE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "LINK_ACTIVE", err);
		}
		gpio_direction_output(gpio_link_active, 0);
	}

	if (gpio_link_hostwake) {
		err = gpio_request(gpio_link_hostwake, "HOSTWAKE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "HOSTWAKE", err);
		}
		gpio_direction_input(gpio_link_hostwake);
		s3c_gpio_cfgpin(gpio_link_hostwake, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_link_hostwake, S3C_GPIO_PULL_NONE);
	}

	if (gpio_link_slavewake) {
		err = gpio_request(gpio_link_slavewake, "SLAVEWAKE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
			       "SLAVEWAKE", err);
		}
		gpio_direction_output(gpio_link_slavewake, 0);
		s3c_gpio_setpull(gpio_link_slavewake, S3C_GPIO_PULL_NONE);
	}

	if (gpio_link_hostwake)
		irq_set_irq_type(gpio_to_irq(gpio_link_hostwake),
				IRQ_TYPE_EDGE_BOTH);

	active_ctl.gpio_initialized = 1;
	if (active_ctl.gpio_request_host_active) {
		pr_err(" [MODEM_IF] Active States = 1, %s\n", __func__);
		gpio_direction_output(modem_link_pm_data.gpio_link_active, 1);
	}

	printk(KERN_INFO "modem_link_pm_config_gpio done\n");
}

static int __init init_modem(void)
{
	int ret;
	printk(KERN_INFO "[MODEM_IF] init_modem\n");

	/* umts gpios configuration */
	umts_modem_cfg_gpio();
	modem_link_pm_config_gpio();
	ret = platform_device_register(&umts_modem);
	if (ret < 0)
		return ret;

	return ret;
}
late_initcall(init_modem);
