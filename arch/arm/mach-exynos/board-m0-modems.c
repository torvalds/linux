/* linux/arch/arm/mach-xxxx/board-m0-modems.c
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

/* Modem configuraiton for M0 (P-Q + XMM6262)*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_def.h>

#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
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
};

static int umts_link_ldo_enble(bool enable)
{
	/* Exynos HSIC V1.2 LDO was controlled by kernel */
	return 0;
}

#ifdef EHCI_REG_DUMP
struct dump_ehci_regs {
	unsigned caps_hc_capbase;
	unsigned caps_hcs_params;
	unsigned caps_hcc_params;
	unsigned reserved0;
	struct ehci_regs regs;
	unsigned port_usb;  /*0x54*/
	unsigned port_hsic0;
	unsigned port_hsic1;
	unsigned reserved[12];
	unsigned insnreg00;	/*0x90*/
	unsigned insnreg01;
	unsigned insnreg02;
	unsigned insnreg03;
	unsigned insnreg04;
	unsigned insnreg05;
	unsigned insnreg06;
	unsigned insnreg07;
};

struct s5p_ehci_hcd_stub {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int power_on;
};
/* for EHCI register dump */
struct dump_ehci_regs sec_debug_ehci_regs;

#define pr_hcd(s, r) printk(KERN_DEBUG "hcd reg(%s):\t 0x%08x\n", s, r)
static void print_ehci_regs(struct dump_ehci_regs *base)
{
	pr_hcd("HCCPBASE", base->caps_hc_capbase);
	pr_hcd("HCSPARAMS", base->caps_hcs_params);
	pr_hcd("HCCPARAMS", base->caps_hcc_params);
	pr_hcd("USBCMD", base->regs.command);
	pr_hcd("USBSTS", base->regs.status);
	pr_hcd("USBINTR", base->regs.intr_enable);
	pr_hcd("FRINDEX", base->regs.frame_index);
	pr_hcd("CTRLDSSEGMENT", base->regs.segment);
	pr_hcd("PERIODICLISTBASE", base->regs.frame_list);
	pr_hcd("ASYNCLISTADDR", base->regs.async_next);
	pr_hcd("CONFIGFLAG", base->regs.configured_flag);
	pr_hcd("PORT0 Status/Control", base->port_usb);
	pr_hcd("PORT1 Status/Control", base->port_hsic0);
	pr_hcd("PORT2 Status/Control", base->port_hsic1);
	pr_hcd("INSNREG00", base->insnreg00);
	pr_hcd("INSNREG01", base->insnreg01);
	pr_hcd("INSNREG02", base->insnreg02);
	pr_hcd("INSNREG03", base->insnreg03);
	pr_hcd("INSNREG04", base->insnreg04);
	pr_hcd("INSNREG05", base->insnreg05);
	pr_hcd("INSNREG06", base->insnreg06);
	pr_hcd("INSNREG07", base->insnreg07);
}

static void debug_ehci_reg_dump(struct device *hdev)
{
	struct s5p_ehci_hcd_stub *s5p_ehci = dev_get_drvdata(hdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	char *buf = (char *)&sec_debug_ehci_regs;

	if (s5p_ehci->power_on && hcd) {
		memcpy(buf, hcd->regs, 0xB);
		memcpy(buf + 0x10, hcd->regs + 0x10, 0x1F);
		memcpy(buf + 0x50, hcd->regs + 0x50, 0xF);
		memcpy(buf + 0x90, hcd->regs + 0x90, 0x1F);
		print_ehci_regs(hcd->regs);
	}
}
#else
#define debug_ehci_reg_dump (NULL)
#endif

static int umts_link_reconnect(void);
static struct modemlink_pm_data modem_link_pm_data = {
	.name = "link_pm",
	.link_ldo_enable = umts_link_ldo_enble,
	.gpio_link_enable = 0,
	.gpio_link_active = GPIO_ACTIVE_STATE,
	.gpio_link_hostwake = GPIO_IPC_HOST_WAKEUP,
	.gpio_link_slavewake = GPIO_IPC_SLAVE_WAKEUP,
	.link_reconnect = umts_link_reconnect,
	.ehci_reg_dump = debug_ehci_reg_dump,
};

static struct modemlink_pm_link_activectl active_ctl;

static void xmm_gpio_revers_bias_clear(void);
static void xmm_gpio_revers_bias_restore(void);

static struct modem_data umts_modem_data = {
	.name = "xmm6262",

	.gpio_cp_on = GPIO_PHONE_ON,
	.gpio_reset_req_n = GPIO_CP_REQ_RESET,
	.gpio_cp_reset = GPIO_CP_RST,
	.gpio_pda_active = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_PHONE_ACTIVE,
	.gpio_cp_dump_int = GPIO_CP_DUMP_INT,
#ifdef GPIO_AP_DUMP_INT
	.gpio_ap_dump_int = GPIO_AP_DUMP_INT,
#endif
	.gpio_flm_uart_sel = 0,
	.gpio_cp_warm_reset = 0,

#ifdef CONFIG_SEC_DUAL_MODEM_MODE
	.gpio_sim_io_sel = GPIO_SIM_IO_SEL,
	.gpio_cp_ctrl1 = GPIO_CP_CTRL1,
	.gpio_cp_ctrl2 = GPIO_CP_CTRL2,
#endif

#ifdef GPIO_SIM_DETECT
	.gpio_sim_detect = GPIO_SIM_DETECT,
#endif

	.modem_type = IMC_XMM6262,
	.link_types = LINKTYPE(LINKDEV_HSIC),
	.modem_net = UMTS_NETWORK,
	.use_handover = false,

	.num_iodevs = ARRAY_SIZE(umts_io_devices),
	.iodevs = umts_io_devices,

	.link_pm_data = &modem_link_pm_data,
	.gpio_revers_bias_clear = xmm_gpio_revers_bias_clear,
	.gpio_revers_bias_restore = xmm_gpio_revers_bias_restore,
};

/* HSIC specific function */
void set_slave_wake(void)
{
	if (gpio_get_value(modem_link_pm_data.gpio_link_hostwake)) {
		pr_info("[MODEM_IF]Slave Wake\n");
		if (gpio_get_value(modem_link_pm_data.gpio_link_slavewake)) {
			pr_info("[MODEM_IF]Slave Wake set _-\n");
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
		pr_err(LOG_TAG "Active States =%d, %s\n", type, pdev->name);
		gpio_direction_output(modem_link_pm_data.gpio_link_active,
			type);
	}
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
	.id = 1,
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
	unsigned gpio_ap_dump_int = umts_modem_data.gpio_ap_dump_int;
	unsigned gpio_flm_uart_sel = umts_modem_data.gpio_flm_uart_sel;
	unsigned gpio_sim_detect = umts_modem_data.gpio_sim_detect;
	unsigned irq_phone_active = umts_modem_res[0].start;

#ifdef CONFIG_SEC_DUAL_MODEM_MODE
	unsigned gpio_sim_io_sel = umts_modem_data.gpio_sim_io_sel;
	unsigned gpio_cp_ctrl1 = umts_modem_data.gpio_cp_ctrl1;
	unsigned gpio_cp_ctrl2 = umts_modem_data.gpio_cp_ctrl2;
#endif

	if (gpio_reset_req_n) {
		err = gpio_request(gpio_reset_req_n, "RESET_REQ_N");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			"RESET_REQ_N", err);
		}
		s3c_gpio_slp_cfgpin(gpio_reset_req_n, S3C_GPIO_SLP_OUT1);
		gpio_direction_output(gpio_reset_req_n, 0);
	}

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "CP_ON");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_ON", err);
		}
		gpio_direction_output(gpio_cp_on, 0);
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "CP_RST");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_RST", err);
		}
		s3c_gpio_slp_cfgpin(gpio_cp_rst, S3C_GPIO_SLP_OUT1);
		gpio_direction_output(gpio_cp_rst, 0);
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
		pr_err(LOG_TAG "check phone active = %d\n", irq_phone_active);
	}

	if (gpio_sim_detect) {
		err = gpio_request(gpio_sim_detect, "SIM_DETECT");
		if (err)
			printk(KERN_ERR "fail to request gpio %s: %d\n",
				"SIM_DETECT", err);

		/* gpio_direction_input(gpio_sim_detect); */
		s3c_gpio_cfgpin(gpio_sim_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_sim_detect, S3C_GPIO_PULL_DOWN);
		irq_set_irq_type(gpio_to_irq(gpio_sim_detect),
							IRQ_TYPE_EDGE_BOTH);
	}

	if (gpio_cp_dump_int) {
		err = gpio_request(gpio_cp_dump_int, "CP_DUMP_INT");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_DUMP_INT", err);
		}
		gpio_direction_input(gpio_cp_dump_int);
	}

	if (gpio_ap_dump_int) {
		err = gpio_request(gpio_ap_dump_int, "AP_DUMP_INT");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "AP_DUMP_INT", err);
		}
		gpio_direction_output(gpio_ap_dump_int, 0);
	}

	if (gpio_flm_uart_sel) {
		err = gpio_request(gpio_flm_uart_sel, "GPS_UART_SEL");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "GPS_UART_SEL", err);
		}
		gpio_direction_output(gpio_reset_req_n, 0);
	}

	if (gpio_phone_active)
		irq_set_irq_type(gpio_to_irq(gpio_phone_active),
							IRQ_TYPE_LEVEL_HIGH);
	/* set low unused gpios between AP and CP */
	err = gpio_request(GPIO_SUSPEND_REQUEST, "SUS_REQ");
	if (err)
		pr_err(LOG_TAG "fail to request gpio %s : %d\n", "SUS_REQ",
		err);
	else {
		gpio_direction_output(GPIO_SUSPEND_REQUEST, 0);
		s3c_gpio_setpull(GPIO_SUSPEND_REQUEST, S3C_GPIO_PULL_NONE);
	}
	pr_info(LOG_TAG "umts_modem_cfg_gpio done\n");
}

static void xmm_gpio_revers_bias_clear(void)
{
	gpio_direction_output(umts_modem_data.gpio_pda_active, 0);
	gpio_direction_output(umts_modem_data.gpio_phone_active, 0);
	gpio_direction_output(umts_modem_data.gpio_cp_dump_int, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_active, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_hostwake, 0);
	gpio_direction_output(modem_link_pm_data.gpio_link_slavewake, 0);

	if (umts_modem_data.gpio_sim_detect)
		gpio_direction_output(umts_modem_data.gpio_sim_detect, 0);

	msleep(20);
}

static void xmm_gpio_revers_bias_restore(void)
{
	unsigned gpio_sim_detect = umts_modem_data.gpio_sim_detect;

	s3c_gpio_cfgpin(umts_modem_data.gpio_phone_active, S3C_GPIO_SFN(0xF));
	s3c_gpio_cfgpin(modem_link_pm_data.gpio_link_hostwake,
		S3C_GPIO_SFN(0xF));
	gpio_direction_input(umts_modem_data.gpio_cp_dump_int);

	if (gpio_sim_detect) {
		gpio_direction_input(gpio_sim_detect);
		s3c_gpio_cfgpin(gpio_sim_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_sim_detect, S3C_GPIO_PULL_NONE);
		irq_set_irq_type(gpio_to_irq(gpio_sim_detect),
				IRQ_TYPE_EDGE_BOTH);
		enable_irq_wake(gpio_to_irq(gpio_sim_detect));
	}
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
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "LINK_EN", err);
		}
		gpio_direction_output(gpio_link_enable, 0);
	}

	if (gpio_link_active) {
		err = gpio_request(gpio_link_active, "LINK_ACTIVE");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "LINK_ACTIVE", err);
		}
		gpio_direction_output(gpio_link_active, 0);
	}

	if (gpio_link_hostwake) {
		err = gpio_request(gpio_link_hostwake, "HOSTWAKE");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "HOSTWAKE", err);
		}
		gpio_direction_input(gpio_link_hostwake);
	}

	if (gpio_link_slavewake) {
		err = gpio_request(gpio_link_slavewake, "SLAVEWAKE");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "SLAVEWAKE", err);
		}
		gpio_direction_output(gpio_link_slavewake, 0);
	}

	if (gpio_link_hostwake)
		irq_set_irq_type(gpio_to_irq(gpio_link_hostwake),
							IRQ_TYPE_EDGE_BOTH);

	active_ctl.gpio_initialized = 1;

	pr_info(LOG_TAG "modem_link_pm_config_gpio done\n");
}

static void board_set_simdetect_polarity(void)
{
	if (umts_modem_data.gpio_sim_detect) {
#if defined(CONFIG_MACH_GC1)
		if (system_rev >= 6) /* GD1 3G real B'd*/
			umts_modem_data.sim_polarity = 1;
		else
			umts_modem_data.sim_polarity = 0;
#else
		umts_modem_data.sim_polarity = 0;
#endif
	}
}

static int __init init_modem(void)
{
	int ret;
	pr_info(LOG_TAG "init_modem, system_rev = %d\n", system_rev);

	/* umts gpios configuration */
	umts_modem_cfg_gpio();
	modem_link_pm_config_gpio();
	board_set_simdetect_polarity();
	ret = platform_device_register(&umts_modem);
	if (ret < 0)
		pr_err("(%s) register fail\n", umts_modem.name);

	return ret;
}
late_initcall(init_modem);
