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

#include <linux/platform_data/modem.h>

/* umts target platform data */
static struct modem_io_t umts_io_devices[] = {
	[0] = {
		.name = "umts_boot0",
		.id = 0x0,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_C2C),
	},
	[1] = {
		.name = "umts_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[2] = {
		.name = "umts_rfs0",
		.id = 0x41,
		.format = IPC_RFS,
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
		.name = "rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[5] = {
		.name = "rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_HSIC),
	},
	[6] = {
		.name = "rmnet2",
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
};

/* To get modem state, register phone active irq using resource */
static struct resource umts_modem_res[] = {
	[0] = {
		.name = "umts_phone_active",
		.start = PHONE_ACTIVE_IRQ,
		.end = PHONE_ACTIVE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static int umts_link_ldo_enble(bool enable)
{
	/* Exynos HSIC V1.2 LDO was controlled by kernel */
	return 0;
}

static struct modemlink_pm_data modem_link_pm_data = {
	.name = "link_pm",
	.link_ldo_enable = umts_link_ldo_enble,
	.gpio_link_enable = 0,
	.gpio_link_active = GPIO_ACTIVE_STATE,
	.gpio_link_hostwake = GPIO_IPC_HOST_WAKEUP,
	.gpio_link_slavewake = GPIO_IPC_SLAVE_WAKEUP,
};
static struct modemlink_pm_link_activectl active_ctl;

static struct modem_data umts_modem_data = {
	.name = "xmm6262",

	.gpio_cp_on = CP_ON,
	.gpio_reset_req_n = CP_RESET_REQ_N,
	.gpio_cp_reset = CP_PMU_RST_N,
	.gpio_pda_active = PDA_ACTIVE,
	.gpio_phone_active = PHONE_ACTIVE,
	.gpio_cp_dump_int = CP_DUMP_INT,
	.gpio_flm_uart_sel = 0,
	.gpio_cp_warm_reset = 0,

	.modem_type = IMC_XMM6262,
	.link_types = LINKTYPE(LINKDEV_HSIC) | LINKTYPE(LINKDEV_C2C),
	.modem_net = UMTS_NETWORK,
	.use_handover = false,

	.num_iodevs = ARRAY_SIZE(umts_io_devices),
	.iodevs = umts_io_devices,

	.link_pm_data = &modem_link_pm_data,
};

/* HSIC specific function */
void set_host_states(struct platform_device *pdev, int type)
{
	if (active_ctl.gpio_initialized) {
		pr_err(" [MODEM_IF] Active States =%d, %s\n", type, pdev->name);
		gpio_direction_output(modem_link_pm_data.gpio_link_active,
			type);
	} else
		active_ctl.gpio_request_host_active = 1;
}

static struct platform_device umts_modem = {
	.name = "modem_if",
	.id = -1,
	.num_resources = ARRAY_SIZE(umts_modem_res),
	.resource = umts_modem_res,
	.dev = {
		.platform_data = &umts_modem_data,
	},
};

struct io_list {
	unsigned num;
	char *name;
	unsigned val;
};

static void umts_modem_cfg_gpio(void)
{
	unsigned gpio_reset_req_n = umts_modem_data.gpio_reset_req_n;
	unsigned gpio_cp_on = umts_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = umts_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = umts_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = umts_modem_data.gpio_phone_active;

	if (gpio_reset_req_n) {
		gpio_request(gpio_reset_req_n, "CP_RST_REQ");
		gpio_direction_output(gpio_reset_req_n, 0);
	}

	if (gpio_cp_on) {
		gpio_request(gpio_cp_on, "CP_ON");
		gpio_direction_output(gpio_cp_on, 0);
	}

	if (gpio_cp_rst) {
		gpio_request(gpio_cp_rst, "CP_RST");
		gpio_direction_output(gpio_cp_rst, 0);
	}

	if (gpio_pda_active) {
		gpio_request(gpio_pda_active, "PDA_ACTIVE");
		gpio_direction_output(gpio_pda_active, 0);
	}

	if (gpio_phone_active) {
		gpio_request(gpio_phone_active, "PHONE_ACTIVE");
		gpio_direction_input(gpio_phone_active);
	}
}

static void modem_link_pm_config_gpio(void)
{
	int i, err;
	unsigned gpio_num;
	struct io_list gpio_list[] = {
		{modem_link_pm_data.gpio_link_enable, "hsic_en", 1},
		{modem_link_pm_data.gpio_link_active, "host_active", 0},
		{modem_link_pm_data.gpio_link_hostwake, "host_wakeup", 0},
		{modem_link_pm_data.gpio_link_slavewake, "slave_wakeup", 0},
	};
	unsigned gpio_link_hostwake = modem_link_pm_data.gpio_link_hostwake;

	for (i = 0; i < ARRAY_SIZE(gpio_list); i++) {
		gpio_num = gpio_list[i].num;
		if (gpio_num) {
			err = gpio_request(gpio_num, gpio_list[i].name);
			if (err) {
				pr_err("request gpio fail %s: %d\n",
				gpio_list[i].name, err);
				continue;
			}
			gpio_direction_output(gpio_num, gpio_list[i].val);
			pr_debug("%s gpio cfg done\n", gpio_list[i].name);
		}
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
	pr_debug("[BOARD] <%s> Invoked!!!\n", __func__);

	umts_modem_cfg_gpio();
	modem_link_pm_config_gpio();
	platform_device_register(&umts_modem);

	return 0;
}
late_initcall(init_modem);
