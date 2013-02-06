/* linux/arch/arm/mach-xxxx/board-t0cu-duos-modems.c
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

/* Modem configuraiton for T0_CU (P-Q + ESC6270)*/

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

#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>

#if defined(CONFIG_LINK_DEVICE_PLD)
#include <linux/spi/spi.h>
#include <mach/pld_pdata.h>
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
#define SROM_NUM_ADDR_BITS	15
#else
#define SROM_NUM_ADDR_BITS	14
#endif

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
	MEM_DATA_BUS_8BIT = 0x00000000,
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
#if defined(CONFIG_LINK_DEVICE_PLD)
#define MSM_EDPRAM_SIZE		0x10000	/* 8 KB (virtual)*/
#else
#define MSM_EDPRAM_SIZE		0x4000	/* 16 KB */
#endif


#define INT_MASK_REQ_ACK_F	0x0020
#define INT_MASK_REQ_ACK_R	0x0010
#define INT_MASK_RES_ACK_F	0x0008
#define INT_MASK_RES_ACK_R	0x0004
#define INT_MASK_SEND_F		0x0002
#define INT_MASK_SEND_R		0x0001

#define INT_MASK_REQ_ACK_RFS	0x0400	/* Request RES_ACK_RFS           */
#define INT_MASK_RES_ACK_RFS	0x0200	/* Response of REQ_ACK_RFS       */
#define INT_MASK_SEND_RFS	0x0100	/* Indicate sending RFS data     */


#if (MSM_EDPRAM_SIZE == 0x4000)
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
	2 + 2 + 2044 +
	2 + 2 + 6128 +
	2 + 2 + 2044 +
	2 + 2 + 6128 +
	16 +
	2 +
	2
 =	16384
*/

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
#define DP_BOOT_SIZE_OFFSET	0x3FF6
#define DP_BOOT_TAG_OFFSET	0x3FF8
#define DP_BOOT_COUNT_OFFSET	0x3FFA

#define DP_BOOT_FRAME_SIZE_LIMIT     0x3C00	/* 15KB = 15360byte = 0x3C00 */

#elif (MSM_EDPRAM_SIZE == 0x10000)

/*
	mbx_ap2cp +			0x0
	magic_code +
	access_enable +
	padding +
	mbx_cp2ap +			0x1000
	magic_code +
	access_enable +
	padding +
	fmt_tx_head + fmt_tx_tail + fmt_tx_buff +		0x2000
	raw_tx_head + raw_tx_tail + raw_tx_buff +
	fmt_rx_head + fmt_rx_tail + fmt_rx_buff +		0x3000
	raw_rx_head + raw_rx_tail + raw_rx_buff +
  =	2 +
	4094 +
	2 +
	4094 +
	2 +
	2 +
	2 + 2 + 1020 +
	2 + 2 + 3064 +
	2 + 2 + 1020 +
	2 + 2 + 3064
 */

#define MSM_DP_FMT_TX_BUFF_SZ	1024
#define MSM_DP_RAW_TX_BUFF_SZ	3072
#define MSM_DP_FMT_RX_BUFF_SZ	1024
#define MSM_DP_RAW_RX_BUFF_SZ	3072

#define MAX_MSM_EDPRAM_IPC_DEV	2	/* FMT, RAW */

struct msm_edpram_ipc_cfg {
	u16 mbx_ap2cp;
	u16 magic_ap2cp;
	u16 access_ap2cp;
	u16 fmt_tx_head;
	u16 raw_tx_head;
	u16 fmt_rx_tail;
	u16 raw_rx_tail;
	u16 temp1;
	u8 padding1[4080];

	u16 mbx_cp2ap;
	u16 magic_cp2ap;
	u16 access_cp2ap;
	u16 fmt_tx_tail;
	u16 raw_tx_tail;
	u16 fmt_rx_head;
	u16 raw_rx_head;
	u16 temp2;
	u8 padding2[4080];

	u8 fmt_tx_buff[MSM_DP_FMT_TX_BUFF_SZ];
	u8 raw_tx_buff[MSM_DP_RAW_TX_BUFF_SZ];
	u8 fmt_rx_buff[MSM_DP_FMT_RX_BUFF_SZ];
	u8 raw_rx_buff[MSM_DP_RAW_RX_BUFF_SZ];

	u8 padding3[16384];

	u16 address_buffer;
};

#define DP_BOOT_CLEAR_OFFSET    0
#define DP_BOOT_RSRVD_OFFSET    0
#define DP_BOOT_SIZE_OFFSET     0x2
#define DP_BOOT_TAG_OFFSET      0x4
#define DP_BOOT_COUNT_OFFSET    0x6

#define DP_BOOT_FRAME_SIZE_LIMIT     0x1000	/* 15KB = 15360byte = 0x3C00 */

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
#define DP_BOOT_SIZE_OFFSET	0x7FF6
#define DP_BOOT_TAG_OFFSET	0x7FF8
#define DP_BOOT_COUNT_OFFSET	0x7FFA

#define DP_BOOT_FRAME_SIZE_LIMIT     0x7C00	/* 31KB = 31744byte = 0x7C00 */
#endif

static struct dpram_ipc_map gsm_ipc_map;

#if defined(CONFIG_LINK_DEVICE_PLD)
static struct sromc_cfg gsm_edpram_cfg = {
	.attr = (MEM_DATA_BUS_8BIT),
	.size = 0x10000,
};
#else
static struct sromc_cfg gsm_edpram_cfg = {
	.attr = (MEM_DATA_BUS_16BIT | MEM_WAIT_EN | MEM_BYTE_EN),
	.size = MSM_EDPRAM_SIZE,
};
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
	.aligned = 1,
#endif
};

/*
** GSM target platform data
*/
#if defined(CONFIG_LINK_DEVICE_PLD)
static struct modem_io_t gsm_io_devices[] = {
	[0] = {
		.name = "gsm_ipc0",
		.id = 0x01,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[1] = {
		.name = "gsm_rfs0",
		.id = 0x28,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[2] = {
		.name = "gsm_boot0",
		.id = 0x1,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[3] = {
		.name = "gsm_multi_pdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[4] = {
		.name = "gsm_rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[5] = {
		.name = "gsm_rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[6] = {
		.name = "gsm_rmnet2",
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[7] = {
		.name = "gsm_router",
		.id = 0x39,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[8] = {
		.name = "gsm_csd",
		.id = 0x21,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[9] = {
		.name = "gsm_ramdump0",
		.id = 0x1,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
	[10] = {
		.name = "gsm_loopback0",
		.id = 0x3F,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_PLD),
	},
};
#else
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
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
	.gpio_fpga1_creset = GPIO_FPGA1_CRESET,
	.gpio_fpga1_cdone = GPIO_FPGA1_CDONE,
	.gpio_fpga1_rst_n = GPIO_FPGA1_RST_N,
	.gpio_fpga1_cs_n = GPIO_FPGA1_CS_N,
#endif

	.use_handover = false,

	.modem_net  = CDMA_NETWORK,
	.modem_type = QC_ESC6270,
#if defined(CONFIG_LINK_DEVICE_PLD)
	.link_types = LINKTYPE(LINKDEV_PLD),
#else
	.link_types = LINKTYPE(LINKDEV_DPRAM),
#endif

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
	.id = 2,
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

	case 15:
		s3c_gpio_cfgrange_nopull(EXYNOS4_GPY3(0), EXYNOS4_GPIO_Y3_NR,
					 S3C_GPIO_SFN(2));
		s3c_gpio_cfgrange_nopull(EXYNOS4_GPY4(0), EXYNOS4_GPIO_Y4_NR,
					 S3C_GPIO_SFN(2));
		pr_info("[MDM] <%s> last data gpio EXYNOS4_GPY4(0) ~ %d\n",
			__func__, EXYNOS4_GPIO_Y4_NR);
		break;

	default:
		pr_err("[MDM/E] <%s> Invalid addr_bits!!!\n", __func__);
		return;
	}

	/* Set GPIO for dpram data - 16bit */
#if defined(CONFIG_LINK_DEVICE_PLD)
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPY5(0), 8, S3C_GPIO_SFN(2));
#elif defined(CONFIG_LINK_DEVICE_DPRAM)
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPY5(0), 8, S3C_GPIO_SFN(2));
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPY6(0), 8, S3C_GPIO_SFN(2));
#endif

	/* Setup SROMC CSn pins */
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN1, S3C_GPIO_SFN(2));

#if defined(CONFIG_GSM_MODEM_ESC6270)
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN0, S3C_GPIO_SFN(2));
#endif

	/* Config OEn, WEn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_REN, 2, S3C_GPIO_SFN(2));

	/* Config LBn, UBn */
	s3c_gpio_cfgrange_nopull(GPIO_DPRAM_LBN, 2, S3C_GPIO_SFN(2));

	/* Config BUSY */
#if !defined(CONFIG_LINK_DEVICE_PLD)
	s3c_gpio_cfgpin(GPIO_DPRAM_BUSY, S3C_GPIO_SFN(2));
#endif
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

	if (cfg->attr & MEM_DATA_BUS_16BIT)
		bw |= (SROMC_DATA_16 << (csn * 4));

	if (cfg->attr & MEM_WAIT_EN)
		bw |= (SROMC_WAIT_EN << (csn * 4));

	if (cfg->attr & MEM_BYTE_EN)
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

#if defined(CONFIG_LINK_DEVICE_PLD)
	unsigned gpio_fpga1_creset = gsm_modem_data.gpio_fpga1_creset;
	unsigned gpio_fpga1_cdone = gsm_modem_data.gpio_fpga1_cdone;
	unsigned gpio_fpga1_rst_n = gsm_modem_data.gpio_fpga1_rst_n;
	unsigned gpio_fpga1_cs_n = gsm_modem_data.gpio_fpga1_cs_n;
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
	if (gpio_fpga1_creset) {
		err = gpio_request(gpio_fpga1_creset, "FPGA1_CRESET");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"FPGA1_CRESET", gpio_fpga1_creset, err);
		} else {
			gpio_direction_output(gpio_fpga1_creset, 0);
			s3c_gpio_setpull(gpio_fpga1_creset, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_fpga1_cdone) {
		err = gpio_request(gpio_fpga1_cdone, "FPGA1_CDONE");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"FPGA1_CDONE", gpio_fpga1_cdone, err);
		} else {
			s3c_gpio_cfgpin(gpio_fpga1_cdone, S3C_GPIO_INPUT);
			s3c_gpio_setpull(gpio_fpga1_cdone, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_fpga1_rst_n)	{
		err = gpio_request(gpio_fpga1_rst_n, "FPGA1_RST");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"FPGA1_RST", gpio_fpga1_rst_n, err);
		} else {
			gpio_direction_output(gpio_fpga1_rst_n, 0);
			s3c_gpio_setpull(gpio_fpga1_rst_n, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_fpga1_cs_n)	{
		err = gpio_request(gpio_fpga1_cs_n, "SPI_CS2_1");
		if (err) {
			pr_err("fail to request gpio %s, gpio %d, errno %d\n",
					"SPI_CS2_1", gpio_fpga1_cs_n, err);
		} else {
			gpio_direction_output(gpio_fpga1_cs_n, 0);
			s3c_gpio_setpull(gpio_fpga1_cs_n, S3C_GPIO_PULL_NONE);
		}
	}
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
	/* Magic code and access enable fields */
	gsm_ipc_map.magic_ap2cp = (u16 __iomem *) &ipc_map->magic_ap2cp;
	gsm_ipc_map.access_ap2cp = (u16 __iomem *) &ipc_map->access_ap2cp;

	gsm_ipc_map.magic_cp2ap = (u16 __iomem *) &ipc_map->magic_cp2ap;
	gsm_ipc_map.access_cp2ap = (u16 __iomem *) &ipc_map->access_cp2ap;

	gsm_ipc_map.address_buffer = (u16 __iomem *) &ipc_map->address_buffer;
#else
	/* Magic code and access enable fields */
	gsm_ipc_map.magic = (u16 __iomem *) &ipc_map->magic;
	gsm_ipc_map.access = (u16 __iomem *) &ipc_map->access;
#endif

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

#if defined(CONFIG_LINK_DEVICE_PLD)
#define PLD_BLOCK_SIZE	0x8000

static struct spi_device *p_spi;

static int pld_spi_probe(struct spi_device *spi)
{
	int ret = 0;

	mif_err("pld spi proble.\n");

	p_spi = spi;
	p_spi->mode = SPI_MODE_0;
	p_spi->bits_per_word = 32;

	ret = spi_setup(p_spi);
	if (ret != 0) {
		mif_err("spi_setup ERROR : %d\n", ret);
		return ret;
	}

	dev_info(&p_spi->dev, "(%d) spi probe Done.\n", __LINE__);

	return ret;
}

static int pld_spi_remove(struct spi_device *spi)
{
	return 0;
}

static struct spi_driver pld_spi_driver = {
	.probe = pld_spi_probe,
	.remove = __devexit_p(pld_spi_remove),
	.driver = {
		.name = "modem_if_spi",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
};

static int config_spi_dev_init(void)
{
	int ret = 0;

	ret = spi_register_driver(&pld_spi_driver);
	if (ret < 0) {
		pr_info("spi_register_driver() fail : %d\n", ret);
		return ret;
	}

	pr_info("[%s] Done\n", __func__);
	return 0;
}

int spi_tx_rx_sync(u8 *tx_d, u8 *rx_d, unsigned len)
{
	struct spi_transfer t;
	struct spi_message msg;

	memset(&t, 0, sizeof t);

	t.len = len;

	t.tx_buf = tx_d;
	t.rx_buf = rx_d;

	t.cs_change = 0;

	t.bits_per_word = 8;
	t.speed_hz = 12000000;

	spi_message_init(&msg);
	spi_message_add_tail(&t, &msg);

	return spi_sync(p_spi, &msg);
}

static int pld_send_fgpa_bin(void)
{
	int retval = 0;
	char *tx_b, *rx_b;

	unsigned gpio_fpga1_creset = gsm_modem_data.gpio_fpga1_creset;
	unsigned gpio_fpga1_cdone = gsm_modem_data.gpio_fpga1_cdone;
	unsigned gpio_fpga1_rst_n = gsm_modem_data.gpio_fpga1_rst_n;
	unsigned gpio_fpga1_cs_n = gsm_modem_data.gpio_fpga1_cs_n;

	char dummy_data[8] = "abcdefg";

	mif_info("sizeofpld : 0%d ", sizeof(fpga_bin));

	if (gpio_fpga1_cs_n)	{
		s3c_gpio_cfgpin(gpio_fpga1_cs_n, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(gpio_fpga1_cs_n, S3C_GPIO_PULL_NONE);
		gpio_direction_output(gpio_fpga1_cs_n, 0);
	}

	msleep(20);

	if (gpio_fpga1_creset) {
		gpio_direction_output(gpio_fpga1_creset, 1);
		s3c_gpio_setpull(gpio_fpga1_creset, S3C_GPIO_PULL_NONE);
	}

	msleep(20);

	tx_b = kmalloc(PLD_BLOCK_SIZE*2, GFP_ATOMIC);
	if (!tx_b) {
		mif_err("(%d) tx_b kmalloc fail.",
			__LINE__);
		return -ENOMEM;
	}
	memset(tx_b, 0, PLD_BLOCK_SIZE*2);
	memcpy(tx_b, fpga_bin, sizeof(fpga_bin));

	rx_b = kmalloc(PLD_BLOCK_SIZE*2, GFP_ATOMIC);
	if (!rx_b) {
		mif_err("(%d) rx_b kmalloc fail.",
			__LINE__);
		retval = -ENOMEM;
		goto err;
	}
	memset(rx_b, 0, PLD_BLOCK_SIZE*2);

	retval = spi_tx_rx_sync(tx_b, rx_b, sizeof(fpga_bin));
	if (retval != 0) {
		mif_err("(%d) spi sync error : %d\n",
			__LINE__, retval);
		goto err;
	}

	memset(tx_b, 0, PLD_BLOCK_SIZE*2);
	memcpy(tx_b, dummy_data, sizeof(dummy_data));

	retval = spi_tx_rx_sync(tx_b, rx_b, sizeof(dummy_data));
	if (retval != 0) {
		mif_err("(%d) spi sync error : %d\n",
			__LINE__, retval);
		goto err;
	}

	msleep(20);

	mif_info("PLD_CDone1[%d]\n",
		gpio_get_value(gpio_fpga1_cdone));

	if (gpio_fpga1_rst_n)	{
		gpio_direction_output(gpio_fpga1_rst_n, 1);
		s3c_gpio_setpull(gpio_fpga1_rst_n, S3C_GPIO_PULL_NONE);
	}

err:
	kfree(tx_b);
	kfree(rx_b);

	return retval;

}
#endif

static int __init init_modem(void)
{
#if defined(CONFIG_GSM_MODEM_ESC6270)
	struct sromc_cfg *cfg = NULL;
	struct sromc_access_cfg *acc_cfg = NULL;

	int ret;
	pr_info(LOG_TAG "init_modem, system_rev = %d\n", system_rev);

	gsm_edpram_cfg.csn = 0;
	gsm_edpram_cfg.addr = SROM_CS0_BASE + (SROM_WIDTH * gsm_edpram_cfg.csn);
	gsm_edpram_cfg.end = gsm_edpram_cfg.addr + gsm_edpram_cfg.size - 1;

	config_dpram_port_gpio();
	config_gsm_modem_gpio();

#if defined(CONFIG_LINK_DEVICE_PLD)
	config_spi_dev_init();
	pld_send_fgpa_bin();
#endif

	init_sromc();
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
