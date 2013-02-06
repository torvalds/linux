/* linux/arch/arm/mach-xxxx/board-u1-lgt-modems.c
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
#include <linux/vmalloc.h>
#include <linux/if_arp.h>

/* inlcude platform specific file */
#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-mem.h>
#include <plat/regs-srom.h>

#include <plat/devs.h>
#include <plat/ehci.h>

#define SROM_CS0_BASE		0x04000000
#define SROM_WIDTH		0x01000000
#define SROM_NUM_ADDR_BITS	14

/* For "bus width and wait control (BW)" register */
enum sromc_attr {
	SROMC_DATA_16   = 0x1,	/* 16-bit data bus	*/
	SROMC_BYTE_ADDR = 0x2,	/* Byte base address	*/
	SROMC_WAIT_EN   = 0x4,	/* Wait enabled		*/
	SROMC_BYTE_EN   = 0x8,	/* Byte access enabled	*/
	SROMC_MASK      = 0xF
};

/* DPRAM configuration */
struct sromc_cfg {
	enum sromc_attr attr;
	unsigned size;
	unsigned csn;		/* CSn #			*/
	unsigned addr;		/* Start address (physical)	*/
	unsigned end;		/* End address (physical)	*/
};

/* DPRAM access timing configuration */
struct sromc_access_cfg {
	u32 tacs;		/* Address set-up before CSn		*/
	u32 tcos;		/* Chip selection set-up before OEn	*/
	u32 tacc;		/* Access cycle				*/
	u32 tcoh;		/* Chip selection hold on OEn		*/
	u32 tcah;		/* Address holding time after CSn	*/
	u32 tacp;		/* Page mode access cycle at Page mode	*/
	u32 pmc;		/* Page Mode config			*/
};

/* For EDPRAM (External DPRAM) */
#define MDM_EDPRAM_SIZE		0x8000	/* 32 KB */


#define INT_MASK_REQ_ACK_F	0x0020
#define INT_MASK_REQ_ACK_R	0x0010
#define INT_MASK_RES_ACK_F	0x0008
#define INT_MASK_RES_ACK_R	0x0004
#define INT_MASK_SEND_F		0x0002
#define INT_MASK_SEND_R		0x0001

#define INT_MASK_REQ_ACK_RFS	0x0400 /* Request RES_ACK_RFS		*/
#define INT_MASK_RES_ACK_RFS	0x0200 /* Response of REQ_ACK_RFS	*/
#define INT_MASK_SEND_RFS	0x0100 /* Indicate sending RFS data	*/


/* Function prototypes */
static void config_dpram_port_gpio(void);
static void init_sromc(void);
static void setup_sromc(unsigned csn, struct sromc_cfg *cfg,
		struct sromc_access_cfg *acc_cfg);
static int __init init_modem(void);

static struct sromc_cfg mdm_edpram_cfg = {
	.attr = (SROMC_DATA_16 | SROMC_WAIT_EN | SROMC_BYTE_EN),
	.size = MDM_EDPRAM_SIZE,
};

static struct sromc_access_cfg mdm_edpram_access_cfg[] = {
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

/*
	magic_code +
	access_enable +
	fmt_tx_head + fmt_tx_tail + fmt_tx_buff +
	raw_tx_head + raw_tx_tail + raw_tx_buff +
	fmt_rx_head + fmt_rx_tail + fmt_rx_buff +
	raw_rx_head + raw_rx_tail + raw_rx_buff +
	padding +
	mbx_cp2ap +
	mbx_ap2cp
 =	2 +
	2 +
	2 + 2 + 4092 +
	2 + 2 + 12272 +
	2 + 2 + 4092 +
	2 + 2 + 12272 +
	16 +
	2 +
	2
 =	32768
*/

#define MDM_DP_FMT_TX_BUFF_SZ	4092
#define MDM_DP_RAW_TX_BUFF_SZ	12272
#define MDM_DP_FMT_RX_BUFF_SZ	4092
#define MDM_DP_RAW_RX_BUFF_SZ	12272

#define MAX_MDM_EDPRAM_IPC_DEV	2	/* FMT, RAW */

struct mdm_edpram_ipc_cfg {
	u16 magic;
	u16 access;

	u16 fmt_tx_head;
	u16 fmt_tx_tail;
	u8  fmt_tx_buff[MDM_DP_FMT_TX_BUFF_SZ];

	u16 raw_tx_head;
	u16 raw_tx_tail;
	u8  raw_tx_buff[MDM_DP_RAW_TX_BUFF_SZ];

	u16 fmt_rx_head;
	u16 fmt_rx_tail;
	u8  fmt_rx_buff[MDM_DP_FMT_RX_BUFF_SZ];

	u16 raw_rx_head;
	u16 raw_rx_tail;
	u8  raw_rx_buff[MDM_DP_RAW_RX_BUFF_SZ];

	u8  padding[16];
	u16 mbx_ap2cp;
	u16 mbx_cp2ap;
};

static struct dpram_ipc_map mdm_ipc_map;

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


#define DP_BOOT_FRAME_SIZE_LIMIT     0x7C00 /* 31KB = 31744byte = 0x7C00 */


static void mdm_vbus_on(void);
static void mdm_vbus_off(void);

static struct modemlink_dpram_control mdm_edpram_ctrl = {
	.dp_type = EXT_DPRAM,
	.disabled = true,

	.dpram_irq = IRQ_EINT(8),
	.dpram_irq_flags = IRQF_TRIGGER_FALLING,

	.max_ipc_dev = IPC_RFS,
	.ipc_map = &mdm_ipc_map,

	.boot_size_offset = DP_BOOT_SIZE_OFFSET,
	.boot_tag_offset = DP_BOOT_TAG_OFFSET,
	.boot_count_offset = DP_BOOT_COUNT_OFFSET,
	.max_boot_frame_size = DP_BOOT_FRAME_SIZE_LIMIT,

};

/*
** CDMA target platform data
*/
static struct modem_io_t cdma_io_devices[] = {
	[0] = {
		.name = "cdma_boot0",
		.id = 0x1,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[1] = {
		.name = "cdma_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[2] = {
		.name = "cdma_multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[3] = {
		.name = "cdma_CSD",
		.id = (1|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[4] = {
		.name = "cdma_FOTA",
		.id = (2|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[5] = {
		.name = "cdma_GPS",
		.id = (5|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[6] = {
		.name = "cdma_XTRA",
		.id = (6|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[7] = {
		.name = "cdma_CDMA",
		.id = (7|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[8] = {
		.name = "cdma_EFS",
		.id = (8|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[9] = {
		.name = "cdma_TRFB",
		.id = (9|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[10] = {
		.name = "rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[11] = {
		.name = "rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[12] = {
		.name = "rmnet2",
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[13] = {
		.name = "rmnet3",
		.id = 0x2D,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[14] = {
		.name = "cdma_SMD",
		.id = (25|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[15] = {
		.name = "cdma_VTVD",
		.id = (26|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[16] = {
		.name = "cdma_VTAD",
		.id = (27|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[17] = {
		.name = "cdma_VTCTRL",
		.id = (28|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[18] = {
		.name = "cdma_VTENT",
		.id = (29|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[19] = {
		.name = "cdma_ramdump0",
		.id = 0x1,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[20] = {
		.name = "umts_loopback0",
		.id = (31|0x20),
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
};

static struct modem_data cdma_modem_data = {
	.name = "mdm6600",

	.gpio_cp_on = GPIO_PHONE_ON,
	.gpio_cp_reset = GPIO_CP_RST,
	.gpio_pda_active = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_PHONE_ACTIVE,
	.gpio_cp_reset_msm = GPIO_CP_RST_MSM,
	.gpio_boot_sw_sel = GPIO_BOOT_SW_SEL,
	.vbus_on = mdm_vbus_on,
	.vbus_off = mdm_vbus_off,
	.cp_vbus = NULL,
	.gpio_cp_dump_int   = 0,
	.gpio_cp_warm_reset = 0,

	.modem_net = CDMA_NETWORK,
	.modem_type = QC_MDM6600,
	.link_types = LINKTYPE(LINKDEV_DPRAM),
	.link_name  = "mdm6600_edpram",
	.dpram_ctl  = &mdm_edpram_ctrl,

	.ipc_version = SIPC_VER_41,

	.num_iodevs = ARRAY_SIZE(cdma_io_devices),
	.iodevs = cdma_io_devices,
};

static struct resource cdma_modem_res[] = {
	[0] = {
		.name  = "cp_active_irq",
		.start = IRQ_EINT(14),
		.end   = IRQ_EINT(14),
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device cdma_modem = {
	.name = "modem_if",
	.id = 2,
	.num_resources = ARRAY_SIZE(cdma_modem_res),
	.resource = cdma_modem_res,
	.dev = {
		.platform_data = &cdma_modem_data,
	},
};

static void mdm_vbus_on(void)
{
	int err;

	if (system_rev >= 0x06) {
#ifdef GPIO_USB_BOOT_EN
		pr_info("%s : set USB_BOOT_EN\n", __func__);
		gpio_request(GPIO_USB_BOOT_EN, "USB_BOOT_EN");
		gpio_direction_output(GPIO_USB_BOOT_EN, 1);
		gpio_free(GPIO_USB_BOOT_EN);
#endif
	} else {
#ifdef GPIO_USB_OTG_EN
		gpio_request(GPIO_USB_OTG_EN, "USB_OTG_EN");
		gpio_direction_output(GPIO_USB_OTG_EN, 1);
		gpio_free(GPIO_USB_OTG_EN);
#endif
	}
	mdelay(10);

	if (!cdma_modem_data.cp_vbus) {
		cdma_modem_data.cp_vbus = regulator_get(NULL, "safeout2");
		if (IS_ERR(cdma_modem_data.cp_vbus)) {
			err = PTR_ERR(cdma_modem_data.cp_vbus);
			pr_err(" <%s> regualtor: %d\n", __func__, err);
			cdma_modem_data.cp_vbus = NULL;
		}
	}

	if (cdma_modem_data.cp_vbus) {
		pr_info("%s\n", __func__);
		regulator_enable(cdma_modem_data.cp_vbus);
	}
}

static void mdm_vbus_off(void)
{
	if (cdma_modem_data.cp_vbus) {
		pr_info("%s\n", __func__);
		regulator_disable(cdma_modem_data.cp_vbus);
	}

	if (system_rev >= 0x06) {
#ifdef GPIO_USB_BOOT_EN
		gpio_request(GPIO_USB_BOOT_EN, "USB_BOOT_EN");
		gpio_direction_output(GPIO_USB_BOOT_EN, 0);
		gpio_free(GPIO_USB_BOOT_EN);
#endif
	} else {
#ifdef GPIO_USB_OTG_EN
		gpio_request(GPIO_USB_OTG_EN, "USB_OTG_EN");
		gpio_direction_output(GPIO_USB_OTG_EN, 0);
		gpio_free(GPIO_USB_OTG_EN);
#endif
	}

}

static void config_cdma_modem_gpio(void)
{
	int err;

	unsigned gpio_cp_on = cdma_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = cdma_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = cdma_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = cdma_modem_data.gpio_phone_active;
	unsigned gpio_cp_reset_mdm = cdma_modem_data.gpio_cp_reset_msm;
	unsigned gpio_boot_sw_sel = cdma_modem_data.gpio_boot_sw_sel;

	pr_info("[MDM] <%s>\n", __func__);

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

	if (gpio_cp_reset_mdm) {
		err = gpio_request(gpio_cp_reset_mdm, "CP_RST_MSM");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
				   "CP_RST_MSM", err);
		}
		gpio_direction_output(gpio_cp_reset_mdm, 0);
		s3c_gpio_cfgpin(gpio_cp_reset_mdm, S3C_GPIO_SFN(0x1));
		s3c_gpio_setpull(gpio_cp_reset_mdm, S3C_GPIO_PULL_NONE);
	}

	if (gpio_boot_sw_sel) {
		err = gpio_request(gpio_boot_sw_sel, "BOOT_SW_SEL");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s : %d\n",
				   "BOOT_SW_SEL", err);
		}
		gpio_direction_output(gpio_boot_sw_sel, 0);
		s3c_gpio_setpull(gpio_boot_sw_sel, S3C_GPIO_PULL_NONE);
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
		gpio_direction_input(gpio_phone_active);
		s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_NONE);
	}

	printk(KERN_INFO "<%s> done\n", __func__);
}


static u8 *mdm_edpram_remap_mem_region(struct sromc_cfg *cfg)
{
	int			      dp_addr = 0;
	int			      dp_size = 0;
	u8 __iomem                   *dp_base = NULL;
	struct mdm_edpram_ipc_cfg    *ipc_map = NULL;
	struct dpram_ipc_device *dev = NULL;

	dp_addr = cfg->addr;
	dp_size = cfg->size;
	dp_base = (u8 *)ioremap_nocache(dp_addr, dp_size);
	if (!dp_base) {
		pr_err("[MDM] <%s> dpram base ioremap fail\n", __func__);
		return NULL;
	}
	pr_info("[MDM] <%s> DPRAM VA=0x%08X\n", __func__, (int)dp_base);

	mdm_edpram_ctrl.dp_base = (u8 __iomem *)dp_base;
	mdm_edpram_ctrl.dp_size = dp_size;

	/* Map for IPC */
	ipc_map = (struct mdm_edpram_ipc_cfg *)dp_base;

	/* Magic code and access enable fields */
	mdm_ipc_map.magic  = (u16 __iomem *)&ipc_map->magic;
	mdm_ipc_map.access = (u16 __iomem *)&ipc_map->access;

	/* FMT */
	dev = &mdm_ipc_map.dev[IPC_FMT];

	strcpy(dev->name, "FMT");
	dev->id = IPC_FMT;

	dev->txq.head = (u16 __iomem *)&ipc_map->fmt_tx_head;
	dev->txq.tail = (u16 __iomem *)&ipc_map->fmt_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->fmt_tx_buff[0];
	dev->txq.size = MDM_DP_FMT_TX_BUFF_SZ;

	dev->rxq.head = (u16 __iomem *)&ipc_map->fmt_rx_head;
	dev->rxq.tail = (u16 __iomem *)&ipc_map->fmt_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->fmt_rx_buff[0];
	dev->rxq.size = MDM_DP_FMT_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_F;
	dev->mask_res_ack = INT_MASK_RES_ACK_F;
	dev->mask_send    = INT_MASK_SEND_F;

	/* RAW */
	dev = &mdm_ipc_map.dev[IPC_RAW];

	strcpy(dev->name, "RAW");
	dev->id = IPC_RAW;

	dev->txq.head = (u16 __iomem *)&ipc_map->raw_tx_head;
	dev->txq.tail = (u16 __iomem *)&ipc_map->raw_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->raw_tx_buff[0];
	dev->txq.size = MDM_DP_RAW_TX_BUFF_SZ;

	dev->rxq.head = (u16 __iomem *)&ipc_map->raw_rx_head;
	dev->rxq.tail = (u16 __iomem *)&ipc_map->raw_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->raw_rx_buff[0];
	dev->rxq.size = MDM_DP_RAW_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_R;
	dev->mask_res_ack = INT_MASK_RES_ACK_R;
	dev->mask_send    = INT_MASK_SEND_R;

	/* Mailboxes */
	mdm_ipc_map.mbx_ap2cp = (u16 __iomem *)&ipc_map->mbx_ap2cp;
	mdm_ipc_map.mbx_cp2ap = (u16 __iomem *)&ipc_map->mbx_cp2ap;

	return dp_base;
}

/**
 *	DPRAM GPIO settings
 *
 *	SROM_NUM_ADDR_BITS value indicate the address line number or
 *	the mux/demux dpram type. if you want to set mux mode, define the
 *	SROM_NUM_ADDR_BITS to zero.
 *
 *	for CMC22x
 *	CMC22x has 16KB + a SFR register address.
 *	It used 14 bits (13bits for 16KB word address and 1 bit for SFR
 *	register)
 */
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
			addr_bits - EXYNOS4_GPIO_Y3_NR, S3C_GPIO_SFN(2));
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

	/* Setup SROMC CSn pins */
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN0, S3C_GPIO_SFN(2));

	/* Config OEn, WEn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_REN, 2, S3C_GPIO_SFN(2));

	/* Config LBn, UBn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_LBN, 2, S3C_GPIO_SFN(2));
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

static void setup_sromc
(
	unsigned csn,
	struct sromc_cfg *cfg,
	struct sromc_access_cfg *acc_cfg
)
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
	bw &= ~(SROMC_MASK << (csn << 2));
	bw |= (cfg->attr << (csn << 2));
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

static int __init init_modem(void)
{
	struct sromc_cfg *cfg = NULL;
	struct sromc_access_cfg *acc_cfg = NULL;

	mdm_edpram_cfg.csn = 0;
	mdm_edpram_ctrl.dpram_irq = IRQ_EINT(8);
	mdm_edpram_cfg.addr = SROM_CS0_BASE + (SROM_WIDTH * mdm_edpram_cfg.csn);
	mdm_edpram_cfg.end  = mdm_edpram_cfg.addr + mdm_edpram_cfg.size - 1;

	config_dpram_port_gpio();
	config_cdma_modem_gpio();

	init_sromc();

	cfg = &mdm_edpram_cfg;
	acc_cfg = &mdm_edpram_access_cfg[DPRAM_SPEED_LOW];
	setup_sromc(cfg->csn, cfg, acc_cfg);

	if (!mdm_edpram_remap_mem_region(&mdm_edpram_cfg))
		return -1;
	platform_device_register(&cdma_modem);

	return 0;
}
late_initcall(init_modem);
/*device_initcall(init_modem);*/
