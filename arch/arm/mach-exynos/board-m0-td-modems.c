/* linux/arch/arm/mach-xxxx/board-m0-td-modems.c
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

/* Modem configuraiton for M0 CMCC (P-Q + SPRD8803)*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>

/* tdscdma target platform data */
static struct modem_io_t tdscdma_io_devices[] = {
	[0] = {
		.name = "td_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[1] = {
		.name = "td_rfs0",
		.id = 0x41,
		.format = IPC_RFS,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[2] = {
		.name = "td_boot0",
		.id = 0x0,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[3] = {
		.name = "td_multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[4] = {
#ifdef CONFIG_SLP
		.name = "pdp0",
#else
		.name = "td_rmnet0",
#endif
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[5] = {
#ifdef CONFIG_SLP
		.name = "pdp1",
#else
		.name = "td_rmnet1",
#endif
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[6] = {
#ifdef CONFIG_SLP
		.name = "pdp2",
#else
		.name = "td_rmnet2",
#endif
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[7] = {
		.name = "td_router",
		.id = 0x39,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[8] = {
		.name = "td_csd",
		.id = 0x21,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[9] = {
		.name = "td_ramdump0",
		.id = 0x0,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
	[10] = {
		.name = "td_loopback0",
		.id = 0x3f,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_SPI),
	},
};

static struct modemlink_pm_data modem_link_pm_data = {
	.name = "td_link_pm",
	.gpio_link_enable = 0,
};

/* To get modem state, register phone active irq using resource */
static struct resource tdscdma_modem_res[] = {
};

static struct modem_data tdscdma_modem_data = {
	.name = "sprd8803",

	.gpio_cp_on = GPIO_TD_PHONE_ON,
	.gpio_pda_active = GPIO_TD_PDA_ACTIVE,
	.gpio_phone_active = GPIO_TD_PHONE_ACTIVE,
	.gpio_cp_dump_int = GPIO_TD_DUMP_INT,
	.gpio_ap_cp_int1 = GPIO_AP_TD_INT1,
	.gpio_ap_cp_int2 = GPIO_AP_TD_INT2,
	.gpio_ipc_mrdy = GPIO_IPC_MRDY,
	.gpio_ipc_srdy = GPIO_IPC_SRDY,
	.gpio_ipc_sub_mrdy = GPIO_IPC_SUB_MRDY,
	.gpio_ipc_sub_srdy = GPIO_IPC_SUB_SRDY,
#ifdef CONFIG_SEC_DUAL_MODEM_MODE
	.gpio_sim_io_sel = GPIO_SIM_IO_SEL,
	.gpio_cp_ctrl1 = GPIO_CP_CTRL1,
	.gpio_cp_ctrl2 = GPIO_CP_CTRL2,
#endif
	.modem_type = SPRD_SC8803,
	.link_types = LINKTYPE(LINKDEV_SPI),
	.modem_net = TDSCDMA_NETWORK,

	.num_iodevs = ARRAY_SIZE(tdscdma_io_devices),
	.iodevs = tdscdma_io_devices,

	.link_pm_data = &modem_link_pm_data,
};

/* if use more than one modem device, then set id num */
static struct platform_device tdscdma_modem = {
	.name = "modem_if",
	.id = 2,
	.num_resources = ARRAY_SIZE(tdscdma_modem_res),
	.resource = tdscdma_modem_res,
	.dev = {
		.platform_data = &tdscdma_modem_data,
	},
};

static void tdscdma_modem_cfg_gpio(void)
{
	int err = 0;

	unsigned gpio_cp_on = tdscdma_modem_data.gpio_cp_on;
	unsigned gpio_pda_active = tdscdma_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = tdscdma_modem_data.gpio_phone_active;
	unsigned gpio_cp_dump_int = tdscdma_modem_data.gpio_cp_dump_int;
	unsigned gpio_ipc_mrdy = tdscdma_modem_data.gpio_ipc_mrdy;
	unsigned gpio_ipc_srdy = tdscdma_modem_data.gpio_ipc_srdy;
	unsigned gpio_ipc_sub_mrdy = tdscdma_modem_data.gpio_ipc_sub_mrdy;
	unsigned gpio_ipc_sub_srdy = tdscdma_modem_data.gpio_ipc_sub_srdy;
	unsigned gpio_ap_cp_int2 = tdscdma_modem_data.gpio_ap_cp_int2;
#ifdef CONFIG_SEC_DUAL_MODEM_MODE
	unsigned gpio_sim_io_sel = tdscdma_modem_data.gpio_sim_io_sel;
	unsigned gpio_cp_ctrl1 = tdscdma_modem_data.gpio_cp_ctrl1;
	unsigned gpio_cp_ctrl2 = tdscdma_modem_data.gpio_cp_ctrl2;

	/* these 3 gpios need to set again before cp booting */
	if (gpio_sim_io_sel) {
		err = gpio_request(gpio_sim_io_sel, "SIM_IO_SEL");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
				"SIM_IO_SEL", err);
		}
		gpio_direction_output(gpio_sim_io_sel, 0);
	}

	if (gpio_cp_ctrl1) {
		err = gpio_request(gpio_cp_ctrl1, "CP_CTRL1");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
				"CP_CTRL1", err);
		}
		gpio_direction_output(gpio_cp_ctrl1, 0);
	}

	if (gpio_cp_ctrl2) {
		err = gpio_request(gpio_cp_ctrl2, "CP_CTRL2");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
				"CP_CTRL2", err);
		}
		gpio_direction_output(gpio_cp_ctrl2, 0);
	}
#endif
	/*TODO: check uart init func AP FLM BOOT RX -- */
	s3c_gpio_setpull(EXYNOS4_GPA1(4), S3C_GPIO_PULL_UP);

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "CP_ON");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_ON", err);
		}
		gpio_direction_output(gpio_cp_on, 0);
	}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "PDA_ACTIVE", err);
		}
		gpio_direction_output(gpio_pda_active, 0);
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "PHONE_ACTIVE");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "PHONE_ACTIVE", err);
		}
		gpio_direction_input(gpio_phone_active);
		s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_DOWN);
	}

	if (gpio_cp_dump_int) {
		err = gpio_request(gpio_cp_dump_int, "CP_DUMP_INT");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_DUMP_INT", err);
		}
		gpio_direction_input(gpio_cp_dump_int);
		s3c_gpio_setpull(gpio_cp_dump_int, S3C_GPIO_PULL_DOWN);
	}

	if (gpio_phone_active)
		irq_set_irq_type(gpio_to_irq(gpio_phone_active),
							IRQ_TYPE_EDGE_BOTH);

	if (gpio_ipc_mrdy) {
		err = gpio_request(gpio_ipc_mrdy, "IPC_MRDY");
		if (err) {
			printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
				"IPC_MRDY", err);
		} else {
			gpio_direction_output(gpio_ipc_mrdy, 0);
		}
	}

	if (gpio_ipc_srdy) {
		err = gpio_request(gpio_ipc_srdy, "IPC_SRDY");
		if (err) {
			printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
				"IPC_SRDY", err);
		} else {
			gpio_direction_input(gpio_ipc_srdy);
			s3c_gpio_cfgpin(gpio_ipc_srdy, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_ipc_srdy, S3C_GPIO_PULL_DOWN);

			irq_set_irq_type(gpio_to_irq(gpio_ipc_srdy),
				IRQ_TYPE_EDGE_RISING);
		}
	}

	if (gpio_ipc_sub_mrdy) {
		err = gpio_request(gpio_ipc_sub_mrdy, "IPC_SUB_MRDY");
		if (err) {
			printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
				"IPC_SUB_MRDY", err);
		} else {
			gpio_direction_output(gpio_ipc_sub_mrdy, 0);
		}
	}

	if (gpio_ipc_sub_srdy) {
		err = gpio_request(gpio_ipc_sub_srdy, "IPC_SUB_SRDY");
		if (err) {
			printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
				"IPC_SUB_SRDY", err);
		} else {
			gpio_direction_input(gpio_ipc_sub_srdy);
			s3c_gpio_cfgpin(gpio_ipc_sub_srdy, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_ipc_sub_srdy, S3C_GPIO_PULL_DOWN);

			irq_set_irq_type(gpio_to_irq(gpio_ipc_sub_srdy),
				IRQ_TYPE_EDGE_RISING);
		}
	}

	if (gpio_ap_cp_int2) {
		err = gpio_request(gpio_ap_cp_int2, "AP_CP_INT2");
		if (err) {
			printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
				"AP_CP_INT2", err);
		} else {
			gpio_direction_output(gpio_ap_cp_int2, 0);
		}
	}

	pr_info(LOG_TAG "tdscdma_modem_cfg_gpio done\n");
}

static int __init init_modem(void)
{
	int ret;
	pr_info(LOG_TAG "tdscdma init_modem\n");

	/* tdscdma gpios configuration */
	tdscdma_modem_cfg_gpio();
	ret = platform_device_register(&tdscdma_modem);
	if (ret < 0)
		return ret;

	return ret;
}
late_initcall(init_modem);
