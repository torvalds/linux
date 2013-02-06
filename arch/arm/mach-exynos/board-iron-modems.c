/* linux/arch/arm/mach-xxxx/board-iron-modems.c
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

/* Modem configuraiton for Iron (P-Q + XMM6262)*/

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

#if defined(CONFIG_GSM_MODEM_ESC6270)
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-mem.h>
#include <plat/regs-srom.h>


#define SROM_CS0_BASE		0x04000000
#define SROM_WIDTH		0x01000000
#define SROM_NUM_ADDR_BITS	14

/*
 * For SROMC Configuration:
 * SROMC_ADDR_BYTE enable for byte access
 */
#define SROMC_DATA_16		0x1
#define SROMC_ADDR_BYTE		0x2
#define SROMC_WAIT_EN		0x4
#define SROMC_BYTE_EN		0x8
#define SROMC_MASK		0xF

/* Memory attributes */
enum sromc_attr {
	MEM_DATA_BUS_16BIT = 0x00000001,
	MEM_BYTE_ADDRESSABLE = 0x00000002,
	MEM_WAIT_EN = 0x00000004,
	MEM_BYTE_EN = 0x00000008,

};

/* DPRAM configuration */
struct sromc_cfg {
	enum sromc_attr attr;
	unsigned size;
	unsigned csn;		/* CSn #                        */
	unsigned addr;		/* Start address (physical)     */
	unsigned end;		/* End address (physical)       */
};

/* DPRAM access timing configuration */
struct sromc_access_cfg {
	u32 tacs;		/* Address set-up before CSn            */
	u32 tcos;		/* Chip selection set-up before OEn     */
	u32 tacc;		/* Access cycle                         */
	u32 tcoh;		/* Chip selection hold on OEn           */
	u32 tcah;		/* Address holding time after CSn       */
	u32 tacp;		/* Page mode access cycle at Page mode  */
	u32 pmc;		/* Page Mode config                     */
};

/* For MDM6600 EDPRAM (External DPRAM) */
#define MSM_EDPRAM_SIZE		0x4000	/* 16 KB */

#define INT_MASK_REQ_ACK_F	0x0020
#define INT_MASK_REQ_ACK_R	0x0010
#define INT_MASK_RES_ACK_F	0x0008
#define INT_MASK_RES_ACK_R	0x0004
#define INT_MASK_SEND_F		0x0002
#define INT_MASK_SEND_R		0x0001

#define INT_MASK_REQ_ACK_RFS	0x0400	/* Request RES_ACK_RFS           */
#define INT_MASK_RES_ACK_RFS	0x0200	/* Response of REQ_ACK_RFS       */
#define INT_MASK_SEND_RFS	0x0100	/* Indicate sending RFS data     */


#define MSM_DP_FMT_TX_BUFF_SZ	2044
#define MSM_DP_RAW_TX_BUFF_SZ	6128
#define MSM_DP_FMT_RX_BUFF_SZ	2044
#define MSM_DP_RAW_RX_BUFF_SZ	6128

#define MAX_MSM_EDPRAM_IPC_DEV	2	/* FMT, RAW */


struct msm_edpram_ipc_cfg {
	u16 magic;
	u16 access;

	u16 fmt_tx_head;
	u16 fmt_tx_tail;
	u8 fmt_tx_buff[MSM_DP_FMT_TX_BUFF_SZ];

	u16 raw_tx_head;
	u16 raw_tx_tail;
	u8 raw_tx_buff[MSM_DP_RAW_TX_BUFF_SZ];

	u16 fmt_rx_head;
	u16 fmt_rx_tail;
	u8 fmt_rx_buff[MSM_DP_FMT_RX_BUFF_SZ];

	u16 raw_rx_head;
	u16 raw_rx_tail;
	u8 raw_rx_buff[MSM_DP_RAW_RX_BUFF_SZ];

	u8 padding[16];
	u16 mbx_ap2cp;
	u16 mbx_cp2ap;
};



#if (MSM_EDPRAM_SIZE == 0x4000)
/*
------------------
Buffer : 15KByte
------------------
Reserved: 1014Byte
------------------
SIZE: 2Byte
------------------
TAG: 2Byte
------------------
COUNT: 2Byte
------------------
AP -> CP Intr : 2Byte
------------------
CP -> AP Intr : 2Byte
------------------
*/
#define DP_BOOT_CLEAR_OFFSET	4
#define DP_BOOT_RSRVD_OFFSET	0x3C00
#define DP_BOOT_SIZE_OFFSET	    0x3FF6
#define DP_BOOT_TAG_OFFSET	    0x3FF8
#define DP_BOOT_COUNT_OFFSET	0x3FFA

#define DP_BOOT_FRAME_SIZE_LIMIT     0x3C00	/* 15KB = 15360byte = 0x3C00 */
#else
/*
------------------
Buffer : 31KByte
------------------
Reserved: 1014Byte
------------------
SIZE: 2Byte
------------------
TAG: 2Byte
------------------
COUNT: 2Byte
------------------
AP -> CP Intr : 2Byte
------------------
CP -> AP Intr : 2Byte
------------------
*/
#define DP_BOOT_CLEAR_OFFSET	4
#define DP_BOOT_RSRVD_OFFSET	0x7C00
#define DP_BOOT_SIZE_OFFSET	    0x7FF6
#define DP_BOOT_TAG_OFFSET	    0x7FF8
#define DP_BOOT_COUNT_OFFSET	0x7FFA

#define DP_BOOT_FRAME_SIZE_LIMIT     0x7C00	/* 31KB = 31744byte = 0x7C00 */
#endif

#endif

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

static void xmm_gpio_revers_bias_clear(void);
static void xmm_gpio_revers_bias_restore(void);

#ifndef GPIO_AP_DUMP_INT
#define GPIO_AP_DUMP_INT 0
#endif
static struct modem_data umts_modem_data = {
	.name = "xmm6262",

	.gpio_cp_on = GPIO_PHONE_ON,
	.gpio_reset_req_n = GPIO_CP_REQ_RESET,
	.gpio_cp_reset = GPIO_CP_RST,
	.gpio_pda_active = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_PHONE_ACTIVE,
	.gpio_cp_dump_int = GPIO_CP_DUMP_INT,
	.gpio_ap_dump_int = GPIO_AP_DUMP_INT,
	.gpio_flm_uart_sel = 0,
	.gpio_cp_warm_reset = 0,

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
	unsigned gpio_ap_dump_int = umts_modem_data.gpio_ap_dump_int;
	unsigned gpio_flm_uart_sel = umts_modem_data.gpio_flm_uart_sel;
	unsigned irq_phone_active = umts_modem_res[0].start;

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

	if (gpio_cp_dump_int) {
		err = gpio_request(gpio_cp_dump_int, "CP_DUMP_INT");
		if (err) {
			pr_err(LOG_TAG "fail to request gpio %s : %d\n",
			       "CP_DUMP_INT", err);
		}
		gpio_direction_input(gpio_cp_dump_int);
	}

	if (gpio_ap_dump_int /*&& system_rev >= 11*/) {    /* MO rev1.0*/
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

#if !defined(CONFIG_GSM_MODEM_ESC6270)
	/* set low unused gpios between AP and CP */
	err = gpio_request(GPIO_FLM_RXD, "FLM_RXD");
	if (err)
		pr_err(LOG_TAG "fail to request gpio %s : %d\n", "FLM_RXD",
		err);
	else {
		gpio_direction_output(GPIO_FLM_RXD, 0);
		s3c_gpio_setpull(GPIO_FLM_RXD, S3C_GPIO_PULL_NONE);
	}
	err = gpio_request(GPIO_FLM_TXD, "FLM_TXD");
	if (err)
		pr_err(LOG_TAG "fail to request gpio %s : %d\n", "FLM_TXD",
		err);
	else {
		gpio_direction_output(GPIO_FLM_TXD, 0);
		s3c_gpio_setpull(GPIO_FLM_TXD, S3C_GPIO_PULL_NONE);
	}
#endif

	err = gpio_request(GPIO_SUSPEND_REQUEST, "SUS_REQ");
	if (err)
		pr_err(LOG_TAG "fail to request gpio %s : %d\n", "SUS_REQ",
		err);
	else {
		gpio_direction_output(GPIO_SUSPEND_REQUEST, 0);
		s3c_gpio_setpull(GPIO_SUSPEND_REQUEST, S3C_GPIO_PULL_NONE);
	}
	err = gpio_request(GPIO_GPS_CNTL, "GPS_CNTL");
	if (err)
		pr_err(LOG_TAG "fail to request gpio %s : %d\n", "GPS_CNTL",
		err);
	else {
		gpio_direction_output(GPIO_GPS_CNTL, 0);
		s3c_gpio_setpull(GPIO_GPS_CNTL, S3C_GPIO_PULL_NONE);
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

	msleep(20);
}

static void xmm_gpio_revers_bias_restore(void)
{
	s3c_gpio_cfgpin(umts_modem_data.gpio_phone_active, S3C_GPIO_SFN(0xF));
	s3c_gpio_cfgpin(modem_link_pm_data.gpio_link_hostwake,
		S3C_GPIO_SFN(0xF));
	gpio_direction_input(umts_modem_data.gpio_cp_dump_int);
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

/* For ESC6270 modem */
#if defined(CONFIG_GSM_MODEM_ESC6270)
static struct dpram_ipc_map gsm_ipc_map;

static struct sromc_cfg gsm_edpram_cfg = {
	.attr = (MEM_DATA_BUS_16BIT | MEM_WAIT_EN | MEM_BYTE_EN),
	.size = MSM_EDPRAM_SIZE,
};

static struct sromc_access_cfg gsm_edpram_access_cfg[] = {
	[DPRAM_SPEED_LOW] = {
		.tacs = 0x2 << 28,
		.tcos = 0x2 << 24,
		.tacc = 0x3 << 16,
		.tcoh = 0x2 << 12,
		.tcah = 0x2 << 8,
		.tacp = 0x2 << 4,
		.pmc  = 0x0 << 0,
	},
};

static struct modemlink_dpram_control gsm_edpram_ctrl = {
	.dp_type = EXT_DPRAM,

	.dpram_irq        = ESC_DPRAM_INT_IRQ,
	.dpram_irq_flags  = IRQF_TRIGGER_FALLING,

	.max_ipc_dev = IPC_RFS,
	.ipc_map = &gsm_ipc_map,

	.boot_size_offset = DP_BOOT_SIZE_OFFSET,
	.boot_tag_offset = DP_BOOT_TAG_OFFSET,
	.boot_count_offset = DP_BOOT_COUNT_OFFSET,
	.max_boot_frame_size = DP_BOOT_FRAME_SIZE_LIMIT,
};

/*
** GSM target platform data
*/
static struct modem_io_t gsm_io_devices[] = {
	[0] = {
		.name = "gsm_ipc0",
		.id = 0x01,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[1] = {
		.name = "gsm_rfs0",
		.id = 0x28,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[2] = {
		.name = "gsm_boot0",
		.id = 0x1,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[3] = {
		.name = "gsm_multi_pdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[4] = {
		.name = "gsm_rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[5] = {
		.name = "gsm_rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[6] = {
		.name = "gsm_rmnet2",
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[7] = {
		.name = "gsm_router",
		.id = 0x39,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[8] = {
		.name = "gsm_csd",
		.id = 0x21,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[9] = {
		.name = "gsm_ramdump0",
		.id = 0x1,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[10] = {
		.name = "gsm_loopback0",
		.id = 0x3F,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
};

static struct modem_data gsm_modem_data = {
	.name = "esc6270",

	.gpio_cp_on        = GPIO_CP2_MSM_PWRON,
	.gpio_cp_off       = 0,
	.gpio_reset_req_n  = 0,	/* GPIO_CP_MSM_PMU_RST, */
	.gpio_cp_reset     = GPIO_CP2_MSM_RST,
	.gpio_pda_active   = 0,
	.gpio_phone_active = GPIO_ESC_PHONE_ACTIVE,
	.gpio_flm_uart_sel = GPIO_BOOT_SW_SEL_CP2,

	.gpio_dpram_int = GPIO_ESC_DPRAM_INT,

	.gpio_cp_dump_int   = 0,
	.gpio_cp_warm_reset = 0,

	.use_handover = false,

	.modem_net  = CDMA_NETWORK,
	.modem_type = QC_ESC6270,
	.link_types = LINKTYPE(LINKDEV_DPRAM),
	.link_name  = "esc6270_edpram",
	.dpram_ctl  = &gsm_edpram_ctrl,

	.ipc_version	= SIPC_VER_41,

	.num_iodevs = ARRAY_SIZE(gsm_io_devices),
	.iodevs     = gsm_io_devices,
};

static struct resource gsm_modem_res[] = {
	[0] = {
		.name  = "cp_active_irq",
		.start = ESC_PHONE_ACTIVE_IRQ,
		.end   = ESC_PHONE_ACTIVE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	[1] = {
		.name = "dpram_irq",
		.start = ESC_DPRAM_INT_IRQ,
		.end = ESC_DPRAM_INT_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device gsm_modem = {
	.name = "modem_if",
	.id = 1,
	.num_resources = ARRAY_SIZE(gsm_modem_res),
	.resource = gsm_modem_res,
	.dev = {
		.platform_data = &gsm_modem_data,
	},
};

static void config_dpram_port_gpio(void)
{
	int addr_bits = SROM_NUM_ADDR_BITS;

	pr_info("[MDM] <%s> address line = %d bits\n", __func__, addr_bits);

	/*
	 ** Config DPRAM address/data GPIO pins
	 */

	/* Set GPIO for dpram address */
	switch (addr_bits) {
	case 0:
		break;

	case 13 ... 14:
		s3c_gpio_cfgrange_nopull(EXYNOS4_GPY3(0), EXYNOS4_GPIO_Y3_NR,
					 S3C_GPIO_SFN(2));
		s3c_gpio_cfgrange_nopull(EXYNOS4_GPY4(0),
					 addr_bits - EXYNOS4_GPIO_Y3_NR,
					 S3C_GPIO_SFN(2));
		pr_info("[MDM] <%s> last data gpio EXYNOS4_GPY4(0) ~ %d\n",
			__func__, addr_bits - EXYNOS4_GPIO_Y3_NR);
		break;

	default:
		pr_err("[MDM/E] <%s> Invalid addr_bits!!!\n", __func__);
		return;
	}

	/* Set GPIO for dpram data - 16bit */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPY5(0), 8, S3C_GPIO_SFN(2));
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPY6(0), 8, S3C_GPIO_SFN(2));

#if 0
	/* Setup SROMC CSn pins */
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN0, S3C_GPIO_SFN(2));
#endif

#if defined(CONFIG_GSM_MODEM_ESC6270)
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN1, S3C_GPIO_SFN(2));
#endif

	/* Config OEn, WEn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_REN, 2, S3C_GPIO_SFN(2));

	/* Config LBn, UBn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_LBN, 2, S3C_GPIO_SFN(2));

	/* Config BUSY */
	s3c_gpio_cfgpin(GPIO_DPRAM_BUSY, S3C_GPIO_SFN(2));
}


static void init_sromc(void)
{
	struct clk *clk = NULL;

	/* SROMC clk enable */
	clk = clk_get(NULL, "sromc");
	if (!clk) {
		pr_err("[MDM/E] <%s> SROMC clock gate fail\n", __func__);
		return;
	}
	clk_enable(clk);
}

static void setup_sromc(unsigned csn, struct sromc_cfg *cfg,
			struct sromc_access_cfg *acc_cfg)
{
	unsigned bw = 0;
	unsigned bc = 0;
	void __iomem *bank_sfr = S5P_SROM_BC0 + (4 * csn);

	pr_err("[MDM] <%s> SROMC settings for CS%d...\n", __func__, csn);

	bw = __raw_readl(S5P_SROM_BW);
	bc = __raw_readl(bank_sfr);
	pr_err("[MDM] <%s> Old SROMC settings = BW(0x%08X), BC%d(0x%08X)\n",
	       __func__, bw, csn, bc);

	/* Set the BW control field for the CSn */
	bw &= ~(SROMC_MASK << (csn * 4));

	if (cfg->attr | MEM_DATA_BUS_16BIT)
		bw |= (SROMC_DATA_16 << (csn * 4));

	if (cfg->attr | MEM_WAIT_EN)
		bw |= (SROMC_WAIT_EN << (csn * 4));

	if (cfg->attr | MEM_BYTE_EN)
		bw |= (SROMC_BYTE_EN << (csn * 4));

	writel(bw, S5P_SROM_BW);

	/* Set SROMC memory access timing for the CSn */
	bc = acc_cfg->tacs | acc_cfg->tcos | acc_cfg->tacc |
	    acc_cfg->tcoh | acc_cfg->tcah | acc_cfg->tacp | acc_cfg->pmc;

	writel(bc, bank_sfr);

	/* Verify SROMC settings */
	bw = __raw_readl(S5P_SROM_BW);
	bc = __raw_readl(bank_sfr);
	pr_err("[MDM] <%s> New SROMC settings = BW(0x%08X), BC%d(0x%08X)\n",
	       __func__, bw, csn, bc);
}


void config_gsm_modem_gpio(void)
{
	int err = 0;
	unsigned gpio_cp_on = gsm_modem_data.gpio_cp_on;
	unsigned gpio_cp_off = gsm_modem_data.gpio_cp_off;
	unsigned gpio_rst_req_n = gsm_modem_data.gpio_reset_req_n;
	unsigned gpio_cp_rst = gsm_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = gsm_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = gsm_modem_data.gpio_phone_active;
	unsigned gpio_flm_uart_sel = gsm_modem_data.gpio_flm_uart_sel;
	unsigned gpio_dpram_int = gsm_modem_data.gpio_dpram_int;

	pr_err("[MODEMS] <%s>\n", __func__);

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"PDA_ACTIVE", gpio_pda_active, err);
		} else {
			gpio_direction_output(gpio_pda_active, 1);
			s3c_gpio_setpull(gpio_pda_active, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio_pda_active, 0);
		}
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "ESC_ACTIVE");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_ACTIVE", gpio_phone_active, err);
		} else {
			s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_NONE);
			irq_set_irq_type(gpio_phone_active, IRQ_TYPE_EDGE_BOTH);
		}
	}

	if (gpio_flm_uart_sel) {
		err = gpio_request(gpio_flm_uart_sel, "BOOT_SW_SEL2");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"BOOT_SW_SEL2", gpio_flm_uart_sel, err);
		} else {
			gpio_direction_output(gpio_flm_uart_sel, 1);
			s3c_gpio_setpull(gpio_flm_uart_sel, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio_flm_uart_sel, 1);
		}
	}

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "ESC_ON");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_ON", gpio_cp_on, err);
		} else {
			gpio_direction_output(gpio_cp_on, 0);
			s3c_gpio_setpull(gpio_cp_on, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio_cp_on, S5P_GPIO_DRVSTR_LV1);
			gpio_set_value(gpio_cp_on, 0);
		}
	}

	if (gpio_cp_off) {
		err = gpio_request(gpio_cp_off, "ESC_OFF");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_OFF", (gpio_cp_off), err);
		} else {
			gpio_direction_output(gpio_cp_off, 1);
			s3c_gpio_setpull(gpio_cp_off, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio_cp_off, 1);
		}
	}

	if (gpio_rst_req_n) {
		err = gpio_request(gpio_rst_req_n, "ESC_RST_REQ");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_RST_REQ", gpio_rst_req_n, err);
		} else {
			gpio_direction_output(gpio_rst_req_n, 1);
			s3c_gpio_setpull(gpio_rst_req_n, S3C_GPIO_PULL_NONE);
		}
		gpio_set_value(gpio_rst_req_n, 0);
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "ESC_RST");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_RST", gpio_cp_rst, err);
		} else {
			gpio_direction_output(gpio_cp_rst, 0);
			s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio_cp_rst, S5P_GPIO_DRVSTR_LV4);
		}
		gpio_set_value(gpio_cp_rst, 0);
	}

	if (gpio_dpram_int) {
		err = gpio_request(gpio_dpram_int, "ESC_DPRAM_INT");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"ESC_DPRAM_INT", gpio_dpram_int, err);
		} else {
			/* Configure as a wake-up source */
			s3c_gpio_cfgpin(gpio_dpram_int, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_dpram_int, S3C_GPIO_PULL_NONE);
		}
	}

	err = gpio_request(EXYNOS4_GPA1(4), "AP_CP2_UART_RXD");
	if (err) {
		pr_err("fail to request gpio %s, gpio %d, errno %d\n",
				"AP_CP2_UART_RXD", EXYNOS4_GPA1(4), err);
	} else {
		s3c_gpio_cfgpin(EXYNOS4_GPA1(4), S3C_GPIO_SFN(0x2));
		s3c_gpio_setpull(EXYNOS4_GPA1(4), S3C_GPIO_PULL_NONE);
	}

	err = gpio_request(EXYNOS4_GPA1(5), "AP_CP2_UART_TXD");
	if (err) {
		pr_err("fail to request gpio %s, gpio %d, errno %d\n",
				"AP_CP2_UART_TXD", EXYNOS4_GPA1(5), err);
	} else {
		s3c_gpio_cfgpin(EXYNOS4_GPA1(5), S3C_GPIO_SFN(0x2));
		s3c_gpio_setpull(EXYNOS4_GPA1(5), S3C_GPIO_PULL_NONE);
	}
}

static u8 *gsm_edpram_remap_mem_region(struct sromc_cfg *cfg)
{
	int			      dp_addr = 0;
	int			      dp_size = 0;
	u8 __iomem                   *dp_base = NULL;
	struct msm_edpram_ipc_cfg    *ipc_map = NULL;
	struct dpram_ipc_device *dev = NULL;

	dp_addr = cfg->addr;
	dp_size = cfg->size;
	dp_base = (u8 *)ioremap_nocache(dp_addr, dp_size);
	if (!dp_base) {
		pr_err("[MDM] <%s> dpram base ioremap fail\n", __func__);
		return NULL;
	}
	pr_info("[MDM] <%s> DPRAM VA=0x%08X\n", __func__, (int)dp_base);

	gsm_edpram_ctrl.dp_base = (u8 __iomem *)dp_base;
	gsm_edpram_ctrl.dp_size = dp_size;

	/* Map for IPC */
	ipc_map = (struct msm_edpram_ipc_cfg *)dp_base;

	/* Magic code and access enable fields */
	gsm_ipc_map.magic  = (u16 __iomem *)&ipc_map->magic;
	gsm_ipc_map.access = (u16 __iomem *)&ipc_map->access;

	/* FMT */
	dev = &gsm_ipc_map.dev[IPC_FMT];

	strcpy(dev->name, "FMT");
	dev->id = IPC_FMT;

	dev->txq.head = (u16 __iomem *)&ipc_map->fmt_tx_head;
	dev->txq.tail = (u16 __iomem *)&ipc_map->fmt_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->fmt_tx_buff[0];
	dev->txq.size = MSM_DP_FMT_TX_BUFF_SZ;

	dev->rxq.head = (u16 __iomem *)&ipc_map->fmt_rx_head;
	dev->rxq.tail = (u16 __iomem *)&ipc_map->fmt_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->fmt_rx_buff[0];
	dev->rxq.size = MSM_DP_FMT_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_F;
	dev->mask_res_ack = INT_MASK_RES_ACK_F;
	dev->mask_send    = INT_MASK_SEND_F;

	/* RAW */
	dev = &gsm_ipc_map.dev[IPC_RAW];

	strcpy(dev->name, "RAW");
	dev->id = IPC_RAW;

	dev->txq.head = (u16 __iomem *)&ipc_map->raw_tx_head;
	dev->txq.tail = (u16 __iomem *)&ipc_map->raw_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->raw_tx_buff[0];
	dev->txq.size = MSM_DP_RAW_TX_BUFF_SZ;

	dev->rxq.head = (u16 __iomem *)&ipc_map->raw_rx_head;
	dev->rxq.tail = (u16 __iomem *)&ipc_map->raw_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->raw_rx_buff[0];
	dev->rxq.size = MSM_DP_RAW_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_R;
	dev->mask_res_ack = INT_MASK_RES_ACK_R;
	dev->mask_send    = INT_MASK_SEND_R;

	/* Mailboxes */
	gsm_ipc_map.mbx_ap2cp = (u16 __iomem *)&ipc_map->mbx_ap2cp;
	gsm_ipc_map.mbx_cp2ap = (u16 __iomem *)&ipc_map->mbx_cp2ap;

	return dp_base;
}
#endif


static int __init init_modem(void)
{
#if defined(CONFIG_GSM_MODEM_ESC6270)
	struct sromc_cfg *cfg = NULL;
	struct sromc_access_cfg *acc_cfg = NULL;
#endif
	int ret;
	pr_info(LOG_TAG "init_modem, system_rev = %d\n", system_rev);

	/* umts gpios configuration */
	umts_modem_cfg_gpio();
	modem_link_pm_config_gpio();
	ret = platform_device_register(&umts_modem);
	if (ret < 0)
		return ret;

	/* For ESC6270 modem */
#if defined(CONFIG_GSM_MODEM_ESC6270)
	config_dpram_port_gpio();
	init_sromc();

	gsm_edpram_cfg.csn = 1;
	gsm_edpram_cfg.addr = SROM_CS0_BASE + (SROM_WIDTH * gsm_edpram_cfg.csn);
	gsm_edpram_cfg.end = gsm_edpram_cfg.addr + gsm_edpram_cfg.size - 1;

	config_gsm_modem_gpio();

	cfg = &gsm_edpram_cfg;
	acc_cfg = &gsm_edpram_access_cfg[DPRAM_SPEED_LOW];
	setup_sromc(cfg->csn, cfg, acc_cfg);

	if (!gsm_edpram_remap_mem_region(&gsm_edpram_cfg))
		return -1;

	platform_device_register(&gsm_modem);
#endif

	return ret;
}
late_initcall(init_modem);
