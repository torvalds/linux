// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (c) 2014 Realtek Semiconductor Corp. All rights reserved.
 */

#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include <uapi/linux/mdio.h>
#include <linux/mdio.h>
#include <linux/usb/cdc.h>
#include <linux/suspend.h>
#include <linux/atomic.h>
#include <linux/acpi.h>
#include <linux/firmware.h>
#include <crypto/hash.h>

/* Information for net-next */
#define NETNEXT_VERSION		"11"

/* Information for net */
#define NET_VERSION		"11"

#define DRIVER_VERSION		"v1." NETNEXT_VERSION "." NET_VERSION
#define DRIVER_AUTHOR "Realtek linux nic maintainers <nic_swsd@realtek.com>"
#define DRIVER_DESC "Realtek RTL8152/RTL8153 Based USB Ethernet Adapters"
#define MODULENAME "r8152"

#define R8152_PHY_ID		32

#define PLA_IDR			0xc000
#define PLA_RCR			0xc010
#define PLA_RMS			0xc016
#define PLA_RXFIFO_CTRL0	0xc0a0
#define PLA_RXFIFO_CTRL1	0xc0a4
#define PLA_RXFIFO_CTRL2	0xc0a8
#define PLA_DMY_REG0		0xc0b0
#define PLA_FMC			0xc0b4
#define PLA_CFG_WOL		0xc0b6
#define PLA_TEREDO_CFG		0xc0bc
#define PLA_TEREDO_WAKE_BASE	0xc0c4
#define PLA_MAR			0xcd00
#define PLA_BACKUP		0xd000
#define PLA_BDC_CR		0xd1a0
#define PLA_TEREDO_TIMER	0xd2cc
#define PLA_REALWOW_TIMER	0xd2e8
#define PLA_UPHY_TIMER		0xd388
#define PLA_SUSPEND_FLAG	0xd38a
#define PLA_INDICATE_FALG	0xd38c
#define PLA_MACDBG_PRE		0xd38c	/* RTL_VER_04 only */
#define PLA_MACDBG_POST		0xd38e	/* RTL_VER_04 only */
#define PLA_EXTRA_STATUS	0xd398
#define PLA_EFUSE_DATA		0xdd00
#define PLA_EFUSE_CMD		0xdd02
#define PLA_LEDSEL		0xdd90
#define PLA_LED_FEATURE		0xdd92
#define PLA_PHYAR		0xde00
#define PLA_BOOT_CTRL		0xe004
#define PLA_LWAKE_CTRL_REG	0xe007
#define PLA_GPHY_INTR_IMR	0xe022
#define PLA_EEE_CR		0xe040
#define PLA_EEEP_CR		0xe080
#define PLA_MAC_PWR_CTRL	0xe0c0
#define PLA_MAC_PWR_CTRL2	0xe0ca
#define PLA_MAC_PWR_CTRL3	0xe0cc
#define PLA_MAC_PWR_CTRL4	0xe0ce
#define PLA_WDT6_CTRL		0xe428
#define PLA_TCR0		0xe610
#define PLA_TCR1		0xe612
#define PLA_MTPS		0xe615
#define PLA_TXFIFO_CTRL		0xe618
#define PLA_RSTTALLY		0xe800
#define PLA_CR			0xe813
#define PLA_CRWECR		0xe81c
#define PLA_CONFIG12		0xe81e	/* CONFIG1, CONFIG2 */
#define PLA_CONFIG34		0xe820	/* CONFIG3, CONFIG4 */
#define PLA_CONFIG5		0xe822
#define PLA_PHY_PWR		0xe84c
#define PLA_OOB_CTRL		0xe84f
#define PLA_CPCR		0xe854
#define PLA_MISC_0		0xe858
#define PLA_MISC_1		0xe85a
#define PLA_OCP_GPHY_BASE	0xe86c
#define PLA_TALLYCNT		0xe890
#define PLA_SFF_STS_7		0xe8de
#define PLA_PHYSTATUS		0xe908
#define PLA_CONFIG6		0xe90a /* CONFIG6 */
#define PLA_BP_BA		0xfc26
#define PLA_BP_0		0xfc28
#define PLA_BP_1		0xfc2a
#define PLA_BP_2		0xfc2c
#define PLA_BP_3		0xfc2e
#define PLA_BP_4		0xfc30
#define PLA_BP_5		0xfc32
#define PLA_BP_6		0xfc34
#define PLA_BP_7		0xfc36
#define PLA_BP_EN		0xfc38

#define USB_USB2PHY		0xb41e
#define USB_SSPHYLINK1		0xb426
#define USB_SSPHYLINK2		0xb428
#define USB_U2P3_CTRL		0xb460
#define USB_CSR_DUMMY1		0xb464
#define USB_CSR_DUMMY2		0xb466
#define USB_DEV_STAT		0xb808
#define USB_CONNECT_TIMER	0xcbf8
#define USB_MSC_TIMER		0xcbfc
#define USB_BURST_SIZE		0xcfc0
#define USB_FW_FIX_EN0		0xcfca
#define USB_FW_FIX_EN1		0xcfcc
#define USB_LPM_CONFIG		0xcfd8
#define USB_CSTMR		0xcfef	/* RTL8153A */
#define USB_FW_CTRL		0xd334	/* RTL8153B */
#define USB_FC_TIMER		0xd340
#define USB_USB_CTRL		0xd406
#define USB_PHY_CTRL		0xd408
#define USB_TX_AGG		0xd40a
#define USB_RX_BUF_TH		0xd40c
#define USB_USB_TIMER		0xd428
#define USB_RX_EARLY_TIMEOUT	0xd42c
#define USB_RX_EARLY_SIZE	0xd42e
#define USB_PM_CTRL_STATUS	0xd432	/* RTL8153A */
#define USB_RX_EXTRA_AGGR_TMR	0xd432	/* RTL8153B */
#define USB_TX_DMA		0xd434
#define USB_UPT_RXDMA_OWN	0xd437
#define USB_TOLERANCE		0xd490
#define USB_LPM_CTRL		0xd41a
#define USB_BMU_RESET		0xd4b0
#define USB_U1U2_TIMER		0xd4da
#define USB_FW_TASK		0xd4e8	/* RTL8153B */
#define USB_UPS_CTRL		0xd800
#define USB_POWER_CUT		0xd80a
#define USB_MISC_0		0xd81a
#define USB_MISC_1		0xd81f
#define USB_AFE_CTRL2		0xd824
#define USB_UPS_CFG		0xd842
#define USB_UPS_FLAGS		0xd848
#define USB_WDT1_CTRL		0xe404
#define USB_WDT11_CTRL		0xe43c
#define USB_BP_BA		PLA_BP_BA
#define USB_BP_0		PLA_BP_0
#define USB_BP_1		PLA_BP_1
#define USB_BP_2		PLA_BP_2
#define USB_BP_3		PLA_BP_3
#define USB_BP_4		PLA_BP_4
#define USB_BP_5		PLA_BP_5
#define USB_BP_6		PLA_BP_6
#define USB_BP_7		PLA_BP_7
#define USB_BP_EN		PLA_BP_EN	/* RTL8153A */
#define USB_BP_8		0xfc38		/* RTL8153B */
#define USB_BP_9		0xfc3a
#define USB_BP_10		0xfc3c
#define USB_BP_11		0xfc3e
#define USB_BP_12		0xfc40
#define USB_BP_13		0xfc42
#define USB_BP_14		0xfc44
#define USB_BP_15		0xfc46
#define USB_BP2_EN		0xfc48

/* OCP Registers */
#define OCP_ALDPS_CONFIG	0x2010
#define OCP_EEE_CONFIG1		0x2080
#define OCP_EEE_CONFIG2		0x2092
#define OCP_EEE_CONFIG3		0x2094
#define OCP_BASE_MII		0xa400
#define OCP_EEE_AR		0xa41a
#define OCP_EEE_DATA		0xa41c
#define OCP_PHY_STATUS		0xa420
#define OCP_NCTL_CFG		0xa42c
#define OCP_POWER_CFG		0xa430
#define OCP_EEE_CFG		0xa432
#define OCP_SRAM_ADDR		0xa436
#define OCP_SRAM_DATA		0xa438
#define OCP_DOWN_SPEED		0xa442
#define OCP_EEE_ABLE		0xa5c4
#define OCP_EEE_ADV		0xa5d0
#define OCP_EEE_LPABLE		0xa5d2
#define OCP_PHY_STATE		0xa708		/* nway state for 8153 */
#define OCP_PHY_PATCH_STAT	0xb800
#define OCP_PHY_PATCH_CMD	0xb820
#define OCP_PHY_LOCK		0xb82e
#define OCP_ADC_IOFFSET		0xbcfc
#define OCP_ADC_CFG		0xbc06
#define OCP_SYSCLK_CFG		0xc416

/* SRAM Register */
#define SRAM_GREEN_CFG		0x8011
#define SRAM_LPF_CFG		0x8012
#define SRAM_10M_AMP1		0x8080
#define SRAM_10M_AMP2		0x8082
#define SRAM_IMPEDANCE		0x8084
#define SRAM_PHY_LOCK		0xb82e

/* PLA_RCR */
#define RCR_AAP			0x00000001
#define RCR_APM			0x00000002
#define RCR_AM			0x00000004
#define RCR_AB			0x00000008
#define RCR_ACPT_ALL		(RCR_AAP | RCR_APM | RCR_AM | RCR_AB)

/* PLA_RXFIFO_CTRL0 */
#define RXFIFO_THR1_NORMAL	0x00080002
#define RXFIFO_THR1_OOB		0x01800003

/* PLA_RXFIFO_CTRL1 */
#define RXFIFO_THR2_FULL	0x00000060
#define RXFIFO_THR2_HIGH	0x00000038
#define RXFIFO_THR2_OOB		0x0000004a
#define RXFIFO_THR2_NORMAL	0x00a0

/* PLA_RXFIFO_CTRL2 */
#define RXFIFO_THR3_FULL	0x00000078
#define RXFIFO_THR3_HIGH	0x00000048
#define RXFIFO_THR3_OOB		0x0000005a
#define RXFIFO_THR3_NORMAL	0x0110

/* PLA_TXFIFO_CTRL */
#define TXFIFO_THR_NORMAL	0x00400008
#define TXFIFO_THR_NORMAL2	0x01000008

/* PLA_DMY_REG0 */
#define ECM_ALDPS		0x0002

/* PLA_FMC */
#define FMC_FCR_MCU_EN		0x0001

/* PLA_EEEP_CR */
#define EEEP_CR_EEEP_TX		0x0002

/* PLA_WDT6_CTRL */
#define WDT6_SET_MODE		0x0010

/* PLA_TCR0 */
#define TCR0_TX_EMPTY		0x0800
#define TCR0_AUTO_FIFO		0x0080

/* PLA_TCR1 */
#define VERSION_MASK		0x7cf0

/* PLA_MTPS */
#define MTPS_JUMBO		(12 * 1024 / 64)
#define MTPS_DEFAULT		(6 * 1024 / 64)

/* PLA_RSTTALLY */
#define TALLY_RESET		0x0001

/* PLA_CR */
#define CR_RST			0x10
#define CR_RE			0x08
#define CR_TE			0x04

/* PLA_CRWECR */
#define CRWECR_NORAML		0x00
#define CRWECR_CONFIG		0xc0

/* PLA_OOB_CTRL */
#define NOW_IS_OOB		0x80
#define TXFIFO_EMPTY		0x20
#define RXFIFO_EMPTY		0x10
#define LINK_LIST_READY		0x02
#define DIS_MCU_CLROOB		0x01
#define FIFO_EMPTY		(TXFIFO_EMPTY | RXFIFO_EMPTY)

/* PLA_MISC_1 */
#define RXDY_GATED_EN		0x0008

/* PLA_SFF_STS_7 */
#define RE_INIT_LL		0x8000
#define MCU_BORW_EN		0x4000

/* PLA_CPCR */
#define CPCR_RX_VLAN		0x0040

/* PLA_CFG_WOL */
#define MAGIC_EN		0x0001

/* PLA_TEREDO_CFG */
#define TEREDO_SEL		0x8000
#define TEREDO_WAKE_MASK	0x7f00
#define TEREDO_RS_EVENT_MASK	0x00fe
#define OOB_TEREDO_EN		0x0001

/* PLA_BDC_CR */
#define ALDPS_PROXY_MODE	0x0001

/* PLA_EFUSE_CMD */
#define EFUSE_READ_CMD		BIT(15)
#define EFUSE_DATA_BIT16	BIT(7)

/* PLA_CONFIG34 */
#define LINK_ON_WAKE_EN		0x0010
#define LINK_OFF_WAKE_EN	0x0008

/* PLA_CONFIG6 */
#define LANWAKE_CLR_EN		BIT(0)

/* PLA_CONFIG5 */
#define BWF_EN			0x0040
#define MWF_EN			0x0020
#define UWF_EN			0x0010
#define LAN_WAKE_EN		0x0002

/* PLA_LED_FEATURE */
#define LED_MODE_MASK		0x0700

/* PLA_PHY_PWR */
#define TX_10M_IDLE_EN		0x0080
#define PFM_PWM_SWITCH		0x0040
#define TEST_IO_OFF		BIT(4)

/* PLA_MAC_PWR_CTRL */
#define D3_CLK_GATED_EN		0x00004000
#define MCU_CLK_RATIO		0x07010f07
#define MCU_CLK_RATIO_MASK	0x0f0f0f0f
#define ALDPS_SPDWN_RATIO	0x0f87

/* PLA_MAC_PWR_CTRL2 */
#define EEE_SPDWN_RATIO		0x8007
#define MAC_CLK_SPDWN_EN	BIT(15)

/* PLA_MAC_PWR_CTRL3 */
#define PLA_MCU_SPDWN_EN	BIT(14)
#define PKT_AVAIL_SPDWN_EN	0x0100
#define SUSPEND_SPDWN_EN	0x0004
#define U1U2_SPDWN_EN		0x0002
#define L1_SPDWN_EN		0x0001

/* PLA_MAC_PWR_CTRL4 */
#define PWRSAVE_SPDWN_EN	0x1000
#define RXDV_SPDWN_EN		0x0800
#define TX10MIDLE_EN		0x0100
#define TP100_SPDWN_EN		0x0020
#define TP500_SPDWN_EN		0x0010
#define TP1000_SPDWN_EN		0x0008
#define EEE_SPDWN_EN		0x0001

/* PLA_GPHY_INTR_IMR */
#define GPHY_STS_MSK		0x0001
#define SPEED_DOWN_MSK		0x0002
#define SPDWN_RXDV_MSK		0x0004
#define SPDWN_LINKCHG_MSK	0x0008

/* PLA_PHYAR */
#define PHYAR_FLAG		0x80000000

/* PLA_EEE_CR */
#define EEE_RX_EN		0x0001
#define EEE_TX_EN		0x0002

/* PLA_BOOT_CTRL */
#define AUTOLOAD_DONE		0x0002

/* PLA_LWAKE_CTRL_REG */
#define LANWAKE_PIN		BIT(7)

/* PLA_SUSPEND_FLAG */
#define LINK_CHG_EVENT		BIT(0)

/* PLA_INDICATE_FALG */
#define UPCOMING_RUNTIME_D3	BIT(0)

/* PLA_MACDBG_PRE and PLA_MACDBG_POST */
#define DEBUG_OE		BIT(0)
#define DEBUG_LTSSM		0x0082

/* PLA_EXTRA_STATUS */
#define CUR_LINK_OK		BIT(15)
#define U3P3_CHECK_EN		BIT(7)	/* RTL_VER_05 only */
#define LINK_CHANGE_FLAG	BIT(8)
#define POLL_LINK_CHG		BIT(0)

/* USB_USB2PHY */
#define USB2PHY_SUSPEND		0x0001
#define USB2PHY_L1		0x0002

/* USB_SSPHYLINK1 */
#define DELAY_PHY_PWR_CHG	BIT(1)

/* USB_SSPHYLINK2 */
#define pwd_dn_scale_mask	0x3ffe
#define pwd_dn_scale(x)		((x) << 1)

/* USB_CSR_DUMMY1 */
#define DYNAMIC_BURST		0x0001

/* USB_CSR_DUMMY2 */
#define EP4_FULL_FC		0x0001

/* USB_DEV_STAT */
#define STAT_SPEED_MASK		0x0006
#define STAT_SPEED_HIGH		0x0000
#define STAT_SPEED_FULL		0x0002

/* USB_FW_FIX_EN0 */
#define FW_FIX_SUSPEND		BIT(14)

/* USB_FW_FIX_EN1 */
#define FW_IP_RESET_EN		BIT(9)

/* USB_LPM_CONFIG */
#define LPM_U1U2_EN		BIT(0)

/* USB_TX_AGG */
#define TX_AGG_MAX_THRESHOLD	0x03

/* USB_RX_BUF_TH */
#define RX_THR_SUPPER		0x0c350180
#define RX_THR_HIGH		0x7a120180
#define RX_THR_SLOW		0xffff0180
#define RX_THR_B		0x00010001

/* USB_TX_DMA */
#define TEST_MODE_DISABLE	0x00000001
#define TX_SIZE_ADJUST1		0x00000100

/* USB_BMU_RESET */
#define BMU_RESET_EP_IN		0x01
#define BMU_RESET_EP_OUT	0x02

/* USB_UPT_RXDMA_OWN */
#define OWN_UPDATE		BIT(0)
#define OWN_CLEAR		BIT(1)

/* USB_FW_TASK */
#define FC_PATCH_TASK		BIT(1)

/* USB_UPS_CTRL */
#define POWER_CUT		0x0100

/* USB_PM_CTRL_STATUS */
#define RESUME_INDICATE		0x0001

/* USB_CSTMR */
#define FORCE_SUPER		BIT(0)

/* USB_FW_CTRL */
#define FLOW_CTRL_PATCH_OPT	BIT(1)

/* USB_FC_TIMER */
#define CTRL_TIMER_EN		BIT(15)

/* USB_USB_CTRL */
#define RX_AGG_DISABLE		0x0010
#define RX_ZERO_EN		0x0080

/* USB_U2P3_CTRL */
#define U2P3_ENABLE		0x0001

/* USB_POWER_CUT */
#define PWR_EN			0x0001
#define PHASE2_EN		0x0008
#define UPS_EN			BIT(4)
#define USP_PREWAKE		BIT(5)

/* USB_MISC_0 */
#define PCUT_STATUS		0x0001

/* USB_RX_EARLY_TIMEOUT */
#define COALESCE_SUPER		 85000U
#define COALESCE_HIGH		250000U
#define COALESCE_SLOW		524280U

/* USB_WDT1_CTRL */
#define WTD1_EN			BIT(0)

/* USB_WDT11_CTRL */
#define TIMER11_EN		0x0001

/* USB_LPM_CTRL */
/* bit 4 ~ 5: fifo empty boundary */
#define FIFO_EMPTY_1FB		0x30	/* 0x1fb * 64 = 32448 bytes */
/* bit 2 ~ 3: LMP timer */
#define LPM_TIMER_MASK		0x0c
#define LPM_TIMER_500MS		0x04	/* 500 ms */
#define LPM_TIMER_500US		0x0c	/* 500 us */
#define ROK_EXIT_LPM		0x02

/* USB_AFE_CTRL2 */
#define SEN_VAL_MASK		0xf800
#define SEN_VAL_NORMAL		0xa000
#define SEL_RXIDLE		0x0100

/* USB_UPS_CFG */
#define SAW_CNT_1MS_MASK	0x0fff

/* USB_UPS_FLAGS */
#define UPS_FLAGS_R_TUNE		BIT(0)
#define UPS_FLAGS_EN_10M_CKDIV		BIT(1)
#define UPS_FLAGS_250M_CKDIV		BIT(2)
#define UPS_FLAGS_EN_ALDPS		BIT(3)
#define UPS_FLAGS_CTAP_SHORT_DIS	BIT(4)
#define ups_flags_speed(x)		((x) << 16)
#define UPS_FLAGS_EN_EEE		BIT(20)
#define UPS_FLAGS_EN_500M_EEE		BIT(21)
#define UPS_FLAGS_EN_EEE_CKDIV		BIT(22)
#define UPS_FLAGS_EEE_PLLOFF_100	BIT(23)
#define UPS_FLAGS_EEE_PLLOFF_GIGA	BIT(24)
#define UPS_FLAGS_EEE_CMOD_LV_EN	BIT(25)
#define UPS_FLAGS_EN_GREEN		BIT(26)
#define UPS_FLAGS_EN_FLOW_CTR		BIT(27)

enum spd_duplex {
	NWAY_10M_HALF,
	NWAY_10M_FULL,
	NWAY_100M_HALF,
	NWAY_100M_FULL,
	NWAY_1000M_FULL,
	FORCE_10M_HALF,
	FORCE_10M_FULL,
	FORCE_100M_HALF,
	FORCE_100M_FULL,
};

/* OCP_ALDPS_CONFIG */
#define ENPWRSAVE		0x8000
#define ENPDNPS			0x0200
#define LINKENA			0x0100
#define DIS_SDSAVE		0x0010

/* OCP_PHY_STATUS */
#define PHY_STAT_MASK		0x0007
#define PHY_STAT_EXT_INIT	2
#define PHY_STAT_LAN_ON		3
#define PHY_STAT_PWRDN		5

/* OCP_NCTL_CFG */
#define PGA_RETURN_EN		BIT(1)

/* OCP_POWER_CFG */
#define EEE_CLKDIV_EN		0x8000
#define EN_ALDPS		0x0004
#define EN_10M_PLLOFF		0x0001

/* OCP_EEE_CONFIG1 */
#define RG_TXLPI_MSK_HFDUP	0x8000
#define RG_MATCLR_EN		0x4000
#define EEE_10_CAP		0x2000
#define EEE_NWAY_EN		0x1000
#define TX_QUIET_EN		0x0200
#define RX_QUIET_EN		0x0100
#define sd_rise_time_mask	0x0070
#define sd_rise_time(x)		(min(x, 7) << 4)	/* bit 4 ~ 6 */
#define RG_RXLPI_MSK_HFDUP	0x0008
#define SDFALLTIME		0x0007	/* bit 0 ~ 2 */

/* OCP_EEE_CONFIG2 */
#define RG_LPIHYS_NUM		0x7000	/* bit 12 ~ 15 */
#define RG_DACQUIET_EN		0x0400
#define RG_LDVQUIET_EN		0x0200
#define RG_CKRSEL		0x0020
#define RG_EEEPRG_EN		0x0010

/* OCP_EEE_CONFIG3 */
#define fast_snr_mask		0xff80
#define fast_snr(x)		(min(x, 0x1ff) << 7)	/* bit 7 ~ 15 */
#define RG_LFS_SEL		0x0060	/* bit 6 ~ 5 */
#define MSK_PH			0x0006	/* bit 0 ~ 3 */

/* OCP_EEE_AR */
/* bit[15:14] function */
#define FUN_ADDR		0x0000
#define FUN_DATA		0x4000
/* bit[4:0] device addr */

/* OCP_EEE_CFG */
#define CTAP_SHORT_EN		0x0040
#define EEE10_EN		0x0010

/* OCP_DOWN_SPEED */
#define EN_EEE_CMODE		BIT(14)
#define EN_EEE_1000		BIT(13)
#define EN_EEE_100		BIT(12)
#define EN_10M_CLKDIV		BIT(11)
#define EN_10M_BGOFF		0x0080

/* OCP_PHY_STATE */
#define TXDIS_STATE		0x01
#define ABD_STATE		0x02

/* OCP_PHY_PATCH_STAT */
#define PATCH_READY		BIT(6)

/* OCP_PHY_PATCH_CMD */
#define PATCH_REQUEST		BIT(4)

/* OCP_PHY_LOCK */
#define PATCH_LOCK		BIT(0)

/* OCP_ADC_CFG */
#define CKADSEL_L		0x0100
#define ADC_EN			0x0080
#define EN_EMI_L		0x0040

/* OCP_SYSCLK_CFG */
#define clk_div_expo(x)		(min(x, 5) << 8)

/* SRAM_GREEN_CFG */
#define GREEN_ETH_EN		BIT(15)
#define R_TUNE_EN		BIT(11)

/* SRAM_LPF_CFG */
#define LPF_AUTO_TUNE		0x8000

/* SRAM_10M_AMP1 */
#define GDAC_IB_UPALL		0x0008

/* SRAM_10M_AMP2 */
#define AMP_DN			0x0200

/* SRAM_IMPEDANCE */
#define RX_DRIVING_MASK		0x6000

/* SRAM_PHY_LOCK */
#define PHY_PATCH_LOCK		0x0001

/* MAC PASSTHRU */
#define AD_MASK			0xfee0
#define BND_MASK		0x0004
#define BD_MASK			0x0001
#define EFUSE			0xcfdb
#define PASS_THRU_MASK		0x1

#define BP4_SUPER_ONLY		0x1578	/* RTL_VER_04 only */

enum rtl_register_content {
	_1000bps	= 0x10,
	_100bps		= 0x08,
	_10bps		= 0x04,
	LINK_STATUS	= 0x02,
	FULL_DUP	= 0x01,
};

#define RTL8152_MAX_TX		4
#define RTL8152_MAX_RX		10
#define INTBUFSIZE		2
#define TX_ALIGN		4
#define RX_ALIGN		8

#define RTL8152_RX_MAX_PENDING	4096
#define RTL8152_RXFG_HEADSZ	256

#define INTR_LINK		0x0004

#define RTL8152_REQT_READ	0xc0
#define RTL8152_REQT_WRITE	0x40
#define RTL8152_REQ_GET_REGS	0x05
#define RTL8152_REQ_SET_REGS	0x05

#define BYTE_EN_DWORD		0xff
#define BYTE_EN_WORD		0x33
#define BYTE_EN_BYTE		0x11
#define BYTE_EN_SIX_BYTES	0x3f
#define BYTE_EN_START_MASK	0x0f
#define BYTE_EN_END_MASK	0xf0

#define RTL8153_MAX_PACKET	9216 /* 9K */
#define RTL8153_MAX_MTU		(RTL8153_MAX_PACKET - VLAN_ETH_HLEN - \
				 ETH_FCS_LEN)
#define RTL8152_RMS		(VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)
#define RTL8153_RMS		RTL8153_MAX_PACKET
#define RTL8152_TX_TIMEOUT	(5 * HZ)
#define RTL8152_NAPI_WEIGHT	64
#define rx_reserved_size(x)	((x) + VLAN_ETH_HLEN + ETH_FCS_LEN + \
				 sizeof(struct rx_desc) + RX_ALIGN)

/* rtl8152 flags */
enum rtl8152_flags {
	RTL8152_UNPLUG = 0,
	RTL8152_SET_RX_MODE,
	WORK_ENABLE,
	RTL8152_LINK_CHG,
	SELECTIVE_SUSPEND,
	PHY_RESET,
	SCHEDULE_TASKLET,
	GREEN_ETHERNET,
	DELL_TB_RX_AGG_BUG,
	LENOVO_MACPASSTHRU,
};

/* Define these values to match your device */
#define VENDOR_ID_REALTEK		0x0bda
#define VENDOR_ID_MICROSOFT		0x045e
#define VENDOR_ID_SAMSUNG		0x04e8
#define VENDOR_ID_LENOVO		0x17ef
#define VENDOR_ID_LINKSYS		0x13b1
#define VENDOR_ID_NVIDIA		0x0955
#define VENDOR_ID_TPLINK		0x2357

#define DEVICE_ID_THINKPAD_THUNDERBOLT3_DOCK_GEN2	0x3082
#define DEVICE_ID_THINKPAD_USB_C_DOCK_GEN2		0xa387

#define MCU_TYPE_PLA			0x0100
#define MCU_TYPE_USB			0x0000

struct tally_counter {
	__le64	tx_packets;
	__le64	rx_packets;
	__le64	tx_errors;
	__le32	rx_errors;
	__le16	rx_missed;
	__le16	align_errors;
	__le32	tx_one_collision;
	__le32	tx_multi_collision;
	__le64	rx_unicast;
	__le64	rx_broadcast;
	__le32	rx_multicast;
	__le16	tx_aborted;
	__le16	tx_underrun;
};

struct rx_desc {
	__le32 opts1;
#define RX_LEN_MASK			0x7fff

	__le32 opts2;
#define RD_UDP_CS			BIT(23)
#define RD_TCP_CS			BIT(22)
#define RD_IPV6_CS			BIT(20)
#define RD_IPV4_CS			BIT(19)

	__le32 opts3;
#define IPF				BIT(23) /* IP checksum fail */
#define UDPF				BIT(22) /* UDP checksum fail */
#define TCPF				BIT(21) /* TCP checksum fail */
#define RX_VLAN_TAG			BIT(16)

	__le32 opts4;
	__le32 opts5;
	__le32 opts6;
};

struct tx_desc {
	__le32 opts1;
#define TX_FS			BIT(31) /* First segment of a packet */
#define TX_LS			BIT(30) /* Final segment of a packet */
#define GTSENDV4		BIT(28)
#define GTSENDV6		BIT(27)
#define GTTCPHO_SHIFT		18
#define GTTCPHO_MAX		0x7fU
#define TX_LEN_MAX		0x3ffffU

	__le32 opts2;
#define UDP_CS			BIT(31) /* Calculate UDP/IP checksum */
#define TCP_CS			BIT(30) /* Calculate TCP/IP checksum */
#define IPV4_CS			BIT(29) /* Calculate IPv4 checksum */
#define IPV6_CS			BIT(28) /* Calculate IPv6 checksum */
#define MSS_SHIFT		17
#define MSS_MAX			0x7ffU
#define TCPHO_SHIFT		17
#define TCPHO_MAX		0x7ffU
#define TX_VLAN_TAG		BIT(16)
};

struct r8152;

struct rx_agg {
	struct list_head list, info_list;
	struct urb *urb;
	struct r8152 *context;
	struct page *page;
	void *buffer;
};

struct tx_agg {
	struct list_head list;
	struct urb *urb;
	struct r8152 *context;
	void *buffer;
	void *head;
	u32 skb_num;
	u32 skb_len;
};

struct r8152 {
	unsigned long flags;
	struct usb_device *udev;
	struct napi_struct napi;
	struct usb_interface *intf;
	struct net_device *netdev;
	struct urb *intr_urb;
	struct tx_agg tx_info[RTL8152_MAX_TX];
	struct list_head rx_info, rx_used;
	struct list_head rx_done, tx_free;
	struct sk_buff_head tx_queue, rx_queue;
	spinlock_t rx_lock, tx_lock;
	struct delayed_work schedule, hw_phy_work;
	struct mii_if_info mii;
	struct mutex control;	/* use for hw setting */
#ifdef CONFIG_PM_SLEEP
	struct notifier_block pm_notifier;
#endif
	struct tasklet_struct tx_tl;

	struct rtl_ops {
		void (*init)(struct r8152 *tp);
		int (*enable)(struct r8152 *tp);
		void (*disable)(struct r8152 *tp);
		void (*up)(struct r8152 *tp);
		void (*down)(struct r8152 *tp);
		void (*unload)(struct r8152 *tp);
		int (*eee_get)(struct r8152 *tp, struct ethtool_eee *eee);
		int (*eee_set)(struct r8152 *tp, struct ethtool_eee *eee);
		bool (*in_nway)(struct r8152 *tp);
		void (*hw_phy_cfg)(struct r8152 *tp);
		void (*autosuspend_en)(struct r8152 *tp, bool enable);
	} rtl_ops;

	struct ups_info {
		u32 _10m_ckdiv:1;
		u32 _250m_ckdiv:1;
		u32 aldps:1;
		u32 lite_mode:2;
		u32 speed_duplex:4;
		u32 eee:1;
		u32 eee_lite:1;
		u32 eee_ckdiv:1;
		u32 eee_plloff_100:1;
		u32 eee_plloff_giga:1;
		u32 eee_cmod_lv:1;
		u32 green:1;
		u32 flow_control:1;
		u32 ctap_short_off:1;
	} ups_info;

#define RTL_VER_SIZE		32

	struct rtl_fw {
		const char *fw_name;
		const struct firmware *fw;

		char version[RTL_VER_SIZE];
		int (*pre_fw)(struct r8152 *tp);
		int (*post_fw)(struct r8152 *tp);

		bool retry;
	} rtl_fw;

	atomic_t rx_count;

	bool eee_en;
	int intr_interval;
	u32 saved_wolopts;
	u32 msg_enable;
	u32 tx_qlen;
	u32 coalesce;
	u32 advertising;
	u32 rx_buf_sz;
	u32 rx_copybreak;
	u32 rx_pending;

	u16 ocp_base;
	u16 speed;
	u16 eee_adv;
	u8 *intr_buff;
	u8 version;
	u8 duplex;
	u8 autoneg;
};

/**
 * struct fw_block - block type and total length
 * @type: type of the current block, such as RTL_FW_END, RTL_FW_PLA,
 *	RTL_FW_USB and so on.
 * @length: total length of the current block.
 */
struct fw_block {
	__le32 type;
	__le32 length;
} __packed;

/**
 * struct fw_header - header of the firmware file
 * @checksum: checksum of sha256 which is calculated from the whole file
 *	except the checksum field of the file. That is, calculate sha256
 *	from the version field to the end of the file.
 * @version: version of this firmware.
 * @blocks: the first firmware block of the file
 */
struct fw_header {
	u8 checksum[32];
	char version[RTL_VER_SIZE];
	struct fw_block blocks[];
} __packed;

/**
 * struct fw_mac - a firmware block used by RTL_FW_PLA and RTL_FW_USB.
 *	The layout of the firmware block is:
 *	<struct fw_mac> + <info> + <firmware data>.
 * @fw_offset: offset of the firmware binary data. The start address of
 *	the data would be the address of struct fw_mac + @fw_offset.
 * @fw_reg: the register to load the firmware. Depends on chip.
 * @bp_ba_addr: the register to write break point base address. Depends on
 *	chip.
 * @bp_ba_value: break point base address. Depends on chip.
 * @bp_en_addr: the register to write break point enabled mask. Depends
 *	on chip.
 * @bp_en_value: break point enabled mask. Depends on the firmware.
 * @bp_start: the start register of break points. Depends on chip.
 * @bp_num: the break point number which needs to be set for this firmware.
 *	Depends on the firmware.
 * @bp: break points. Depends on firmware.
 * @fw_ver_reg: the register to store the fw version.
 * @fw_ver_data: the firmware version of the current type.
 * @info: additional information for debugging, and is followed by the
 *	binary data of firmware.
 */
struct fw_mac {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 fw_reg;
	__le16 bp_ba_addr;
	__le16 bp_ba_value;
	__le16 bp_en_addr;
	__le16 bp_en_value;
	__le16 bp_start;
	__le16 bp_num;
	__le16 bp[16]; /* any value determined by firmware */
	__le32 reserved;
	__le16 fw_ver_reg;
	u8 fw_ver_data;
	char info[];
} __packed;

/**
 * struct fw_phy_patch_key - a firmware block used by RTL_FW_PHY_START.
 *	This is used to set patch key when loading the firmware of PHY.
 * @key_reg: the register to write the patch key.
 * @key_data: patch key.
 */
struct fw_phy_patch_key {
	struct fw_block blk_hdr;
	__le16 key_reg;
	__le16 key_data;
	__le32 reserved;
} __packed;

/**
 * struct fw_phy_nc - a firmware block used by RTL_FW_PHY_NC.
 *	The layout of the firmware block is:
 *	<struct fw_phy_nc> + <info> + <firmware data>.
 * @fw_offset: offset of the firmware binary data. The start address of
 *	the data would be the address of struct fw_phy_nc + @fw_offset.
 * @fw_reg: the register to load the firmware. Depends on chip.
 * @ba_reg: the register to write the base address. Depends on chip.
 * @ba_data: base address. Depends on chip.
 * @patch_en_addr: the register of enabling patch mode. Depends on chip.
 * @patch_en_value: patch mode enabled mask. Depends on the firmware.
 * @mode_reg: the regitster of switching the mode.
 * @mod_pre: the mode needing to be set before loading the firmware.
 * @mod_post: the mode to be set when finishing to load the firmware.
 * @bp_start: the start register of break points. Depends on chip.
 * @bp_num: the break point number which needs to be set for this firmware.
 *	Depends on the firmware.
 * @bp: break points. Depends on firmware.
 * @info: additional information for debugging, and is followed by the
 *	binary data of firmware.
 */
struct fw_phy_nc {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 fw_reg;
	__le16 ba_reg;
	__le16 ba_data;
	__le16 patch_en_addr;
	__le16 patch_en_value;
	__le16 mode_reg;
	__le16 mode_pre;
	__le16 mode_post;
	__le16 reserved;
	__le16 bp_start;
	__le16 bp_num;
	__le16 bp[4];
	char info[];
} __packed;

enum rtl_fw_type {
	RTL_FW_END = 0,
	RTL_FW_PLA,
	RTL_FW_USB,
	RTL_FW_PHY_START,
	RTL_FW_PHY_STOP,
	RTL_FW_PHY_NC,
};

enum rtl_version {
	RTL_VER_UNKNOWN = 0,
	RTL_VER_01,
	RTL_VER_02,
	RTL_VER_03,
	RTL_VER_04,
	RTL_VER_05,
	RTL_VER_06,
	RTL_VER_07,
	RTL_VER_08,
	RTL_VER_09,
	RTL_VER_MAX
};

enum tx_csum_stat {
	TX_CSUM_SUCCESS = 0,
	TX_CSUM_TSO,
	TX_CSUM_NONE
};

#define RTL_ADVERTISED_10_HALF			BIT(0)
#define RTL_ADVERTISED_10_FULL			BIT(1)
#define RTL_ADVERTISED_100_HALF			BIT(2)
#define RTL_ADVERTISED_100_FULL			BIT(3)
#define RTL_ADVERTISED_1000_HALF		BIT(4)
#define RTL_ADVERTISED_1000_FULL		BIT(5)

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
 * The RTL chips use a 64 element hash table based on the Ethernet CRC.
 */
static const int multicast_filter_limit = 32;
static unsigned int agg_buf_sz = 16384;

#define RTL_LIMITED_TSO_SIZE	(agg_buf_sz - sizeof(struct tx_desc) - \
				 VLAN_ETH_HLEN - ETH_FCS_LEN)

static
int get_registers(struct r8152 *tp, u16 value, u16 index, u16 size, void *data)
{
	int ret;
	void *tmp;

	tmp = kmalloc(size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = usb_control_msg(tp->udev, usb_rcvctrlpipe(tp->udev, 0),
			      RTL8152_REQ_GET_REGS, RTL8152_REQT_READ,
			      value, index, tmp, size, 500);
	if (ret < 0)
		memset(data, 0xff, size);
	else
		memcpy(data, tmp, size);

	kfree(tmp);

	return ret;
}

static
int set_registers(struct r8152 *tp, u16 value, u16 index, u16 size, void *data)
{
	int ret;
	void *tmp;

	tmp = kmemdup(data, size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = usb_control_msg(tp->udev, usb_sndctrlpipe(tp->udev, 0),
			      RTL8152_REQ_SET_REGS, RTL8152_REQT_WRITE,
			      value, index, tmp, size, 500);

	kfree(tmp);

	return ret;
}

static void rtl_set_unplug(struct r8152 *tp)
{
	if (tp->udev->state == USB_STATE_NOTATTACHED) {
		set_bit(RTL8152_UNPLUG, &tp->flags);
		smp_mb__after_atomic();
	}
}

static int generic_ocp_read(struct r8152 *tp, u16 index, u16 size,
			    void *data, u16 type)
{
	u16 limit = 64;
	int ret = 0;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data)
		return -EPERM;

	if ((u32)index + (u32)size > 0xffff)
		return -EPERM;

	while (size) {
		if (size > limit) {
			ret = get_registers(tp, index, type, limit, data);
			if (ret < 0)
				break;

			index += limit;
			data += limit;
			size -= limit;
		} else {
			ret = get_registers(tp, index, type, size, data);
			if (ret < 0)
				break;

			index += size;
			data += size;
			size = 0;
			break;
		}
	}

	if (ret == -ENODEV)
		rtl_set_unplug(tp);

	return ret;
}

static int generic_ocp_write(struct r8152 *tp, u16 index, u16 byteen,
			     u16 size, void *data, u16 type)
{
	int ret;
	u16 byteen_start, byteen_end, byen;
	u16 limit = 512;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data)
		return -EPERM;

	if ((u32)index + (u32)size > 0xffff)
		return -EPERM;

	byteen_start = byteen & BYTE_EN_START_MASK;
	byteen_end = byteen & BYTE_EN_END_MASK;

	byen = byteen_start | (byteen_start << 4);
	ret = set_registers(tp, index, type | byen, 4, data);
	if (ret < 0)
		goto error1;

	index += 4;
	data += 4;
	size -= 4;

	if (size) {
		size -= 4;

		while (size) {
			if (size > limit) {
				ret = set_registers(tp, index,
						    type | BYTE_EN_DWORD,
						    limit, data);
				if (ret < 0)
					goto error1;

				index += limit;
				data += limit;
				size -= limit;
			} else {
				ret = set_registers(tp, index,
						    type | BYTE_EN_DWORD,
						    size, data);
				if (ret < 0)
					goto error1;

				index += size;
				data += size;
				size = 0;
				break;
			}
		}

		byen = byteen_end | (byteen_end >> 4);
		ret = set_registers(tp, index, type | byen, 4, data);
		if (ret < 0)
			goto error1;
	}

error1:
	if (ret == -ENODEV)
		rtl_set_unplug(tp);

	return ret;
}

static inline
int pla_ocp_read(struct r8152 *tp, u16 index, u16 size, void *data)
{
	return generic_ocp_read(tp, index, size, data, MCU_TYPE_PLA);
}

static inline
int pla_ocp_write(struct r8152 *tp, u16 index, u16 byteen, u16 size, void *data)
{
	return generic_ocp_write(tp, index, byteen, size, data, MCU_TYPE_PLA);
}

static inline
int usb_ocp_write(struct r8152 *tp, u16 index, u16 byteen, u16 size, void *data)
{
	return generic_ocp_write(tp, index, byteen, size, data, MCU_TYPE_USB);
}

static u32 ocp_read_dword(struct r8152 *tp, u16 type, u16 index)
{
	__le32 data;

	generic_ocp_read(tp, index, sizeof(data), &data, type);

	return __le32_to_cpu(data);
}

static void ocp_write_dword(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	__le32 tmp = __cpu_to_le32(data);

	generic_ocp_write(tp, index, BYTE_EN_DWORD, sizeof(tmp), &tmp, type);
}

static u16 ocp_read_word(struct r8152 *tp, u16 type, u16 index)
{
	u32 data;
	__le32 tmp;
	u16 byen = BYTE_EN_WORD;
	u8 shift = index & 2;

	index &= ~3;
	byen <<= shift;

	generic_ocp_read(tp, index, sizeof(tmp), &tmp, type | byen);

	data = __le32_to_cpu(tmp);
	data >>= (shift * 8);
	data &= 0xffff;

	return (u16)data;
}

static void ocp_write_word(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	u32 mask = 0xffff;
	__le32 tmp;
	u16 byen = BYTE_EN_WORD;
	u8 shift = index & 2;

	data &= mask;

	if (index & 2) {
		byen <<= shift;
		mask <<= (shift * 8);
		data <<= (shift * 8);
		index &= ~3;
	}

	tmp = __cpu_to_le32(data);

	generic_ocp_write(tp, index, byen, sizeof(tmp), &tmp, type);
}

static u8 ocp_read_byte(struct r8152 *tp, u16 type, u16 index)
{
	u32 data;
	__le32 tmp;
	u8 shift = index & 3;

	index &= ~3;

	generic_ocp_read(tp, index, sizeof(tmp), &tmp, type);

	data = __le32_to_cpu(tmp);
	data >>= (shift * 8);
	data &= 0xff;

	return (u8)data;
}

static void ocp_write_byte(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	u32 mask = 0xff;
	__le32 tmp;
	u16 byen = BYTE_EN_BYTE;
	u8 shift = index & 3;

	data &= mask;

	if (index & 3) {
		byen <<= shift;
		mask <<= (shift * 8);
		data <<= (shift * 8);
		index &= ~3;
	}

	tmp = __cpu_to_le32(data);

	generic_ocp_write(tp, index, byen, sizeof(tmp), &tmp, type);
}

static u16 ocp_reg_read(struct r8152 *tp, u16 addr)
{
	u16 ocp_base, ocp_index;

	ocp_base = addr & 0xf000;
	if (ocp_base != tp->ocp_base) {
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, ocp_base);
		tp->ocp_base = ocp_base;
	}

	ocp_index = (addr & 0x0fff) | 0xb000;
	return ocp_read_word(tp, MCU_TYPE_PLA, ocp_index);
}

static void ocp_reg_write(struct r8152 *tp, u16 addr, u16 data)
{
	u16 ocp_base, ocp_index;

	ocp_base = addr & 0xf000;
	if (ocp_base != tp->ocp_base) {
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, ocp_base);
		tp->ocp_base = ocp_base;
	}

	ocp_index = (addr & 0x0fff) | 0xb000;
	ocp_write_word(tp, MCU_TYPE_PLA, ocp_index, data);
}

static inline void r8152_mdio_write(struct r8152 *tp, u32 reg_addr, u32 value)
{
	ocp_reg_write(tp, OCP_BASE_MII + reg_addr * 2, value);
}

static inline int r8152_mdio_read(struct r8152 *tp, u32 reg_addr)
{
	return ocp_reg_read(tp, OCP_BASE_MII + reg_addr * 2);
}

static void sram_write(struct r8152 *tp, u16 addr, u16 data)
{
	ocp_reg_write(tp, OCP_SRAM_ADDR, addr);
	ocp_reg_write(tp, OCP_SRAM_DATA, data);
}

static u16 sram_read(struct r8152 *tp, u16 addr)
{
	ocp_reg_write(tp, OCP_SRAM_ADDR, addr);
	return ocp_reg_read(tp, OCP_SRAM_DATA);
}

static int read_mii_word(struct net_device *netdev, int phy_id, int reg)
{
	struct r8152 *tp = netdev_priv(netdev);
	int ret;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	if (phy_id != R8152_PHY_ID)
		return -EINVAL;

	ret = r8152_mdio_read(tp, reg);

	return ret;
}

static
void write_mii_word(struct net_device *netdev, int phy_id, int reg, int val)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (phy_id != R8152_PHY_ID)
		return;

	r8152_mdio_write(tp, reg, val);
}

static int
r8152_submit_rx(struct r8152 *tp, struct rx_agg *agg, gfp_t mem_flags);

static int rtl8152_set_mac_address(struct net_device *netdev, void *p)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int ret = -EADDRNOTAVAIL;

	if (!is_valid_ether_addr(addr->sa_data))
		goto out1;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out1;

	mutex_lock(&tp->control);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_CONFIG);
	pla_ocp_write(tp, PLA_IDR, BYTE_EN_SIX_BYTES, 8, addr->sa_data);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);
out1:
	return ret;
}

/* Devices containing proper chips can support a persistent
 * host system provided MAC address.
 * Examples of this are Dell TB15 and Dell WD15 docks
 */
static int vendor_mac_passthru_addr_read(struct r8152 *tp, struct sockaddr *sa)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	int ret = -EINVAL;
	u32 ocp_data;
	unsigned char buf[6];
	char *mac_obj_name;
	acpi_object_type mac_obj_type;
	int mac_strlen;

	if (test_bit(LENOVO_MACPASSTHRU, &tp->flags)) {
		mac_obj_name = "\\MACA";
		mac_obj_type = ACPI_TYPE_STRING;
		mac_strlen = 0x16;
	} else {
		/* test for -AD variant of RTL8153 */
		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_MISC_0);
		if ((ocp_data & AD_MASK) == 0x1000) {
			/* test for MAC address pass-through bit */
			ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, EFUSE);
			if ((ocp_data & PASS_THRU_MASK) != 1) {
				netif_dbg(tp, probe, tp->netdev,
						"No efuse for RTL8153-AD MAC pass through\n");
				return -ENODEV;
			}
		} else {
			/* test for RTL8153-BND and RTL8153-BD */
			ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_MISC_1);
			if ((ocp_data & BND_MASK) == 0 && (ocp_data & BD_MASK) == 0) {
				netif_dbg(tp, probe, tp->netdev,
						"Invalid variant for MAC pass through\n");
				return -ENODEV;
			}
		}

		mac_obj_name = "\\_SB.AMAC";
		mac_obj_type = ACPI_TYPE_BUFFER;
		mac_strlen = 0x17;
	}

	/* returns _AUXMAC_#AABBCCDDEEFF# */
	status = acpi_evaluate_object(NULL, mac_obj_name, NULL, &buffer);
	obj = (union acpi_object *)buffer.pointer;
	if (!ACPI_SUCCESS(status))
		return -ENODEV;
	if (obj->type != mac_obj_type || obj->string.length != mac_strlen) {
		netif_warn(tp, probe, tp->netdev,
			   "Invalid buffer for pass-thru MAC addr: (%d, %d)\n",
			   obj->type, obj->string.length);
		goto amacout;
	}

	if (strncmp(obj->string.pointer, "_AUXMAC_#", 9) != 0 ||
	    strncmp(obj->string.pointer + 0x15, "#", 1) != 0) {
		netif_warn(tp, probe, tp->netdev,
			   "Invalid header when reading pass-thru MAC addr\n");
		goto amacout;
	}
	ret = hex2bin(buf, obj->string.pointer + 9, 6);
	if (!(ret == 0 && is_valid_ether_addr(buf))) {
		netif_warn(tp, probe, tp->netdev,
			   "Invalid MAC for pass-thru MAC addr: %d, %pM\n",
			   ret, buf);
		ret = -EINVAL;
		goto amacout;
	}
	memcpy(sa->sa_data, buf, 6);
	netif_info(tp, probe, tp->netdev,
		   "Using pass-thru MAC addr %pM\n", sa->sa_data);

amacout:
	kfree(obj);
	return ret;
}

static int determine_ethernet_addr(struct r8152 *tp, struct sockaddr *sa)
{
	struct net_device *dev = tp->netdev;
	int ret;

	sa->sa_family = dev->type;

	ret = eth_platform_get_mac_address(&tp->udev->dev, sa->sa_data);
	if (ret < 0) {
		if (tp->version == RTL_VER_01) {
			ret = pla_ocp_read(tp, PLA_IDR, 8, sa->sa_data);
		} else {
			/* if device doesn't support MAC pass through this will
			 * be expected to be non-zero
			 */
			ret = vendor_mac_passthru_addr_read(tp, sa);
			if (ret < 0)
				ret = pla_ocp_read(tp, PLA_BACKUP, 8,
						   sa->sa_data);
		}
	}

	if (ret < 0) {
		netif_err(tp, probe, dev, "Get ether addr fail\n");
	} else if (!is_valid_ether_addr(sa->sa_data)) {
		netif_err(tp, probe, dev, "Invalid ether addr %pM\n",
			  sa->sa_data);
		eth_hw_addr_random(dev);
		ether_addr_copy(sa->sa_data, dev->dev_addr);
		netif_info(tp, probe, dev, "Random ether addr %pM\n",
			   sa->sa_data);
		return 0;
	}

	return ret;
}

static int set_ethernet_addr(struct r8152 *tp)
{
	struct net_device *dev = tp->netdev;
	struct sockaddr sa;
	int ret;

	ret = determine_ethernet_addr(tp, &sa);
	if (ret < 0)
		return ret;

	if (tp->version == RTL_VER_01)
		ether_addr_copy(dev->dev_addr, sa.sa_data);
	else
		ret = rtl8152_set_mac_address(dev, &sa);

	return ret;
}

static void read_bulk_callback(struct urb *urb)
{
	struct net_device *netdev;
	int status = urb->status;
	struct rx_agg *agg;
	struct r8152 *tp;
	unsigned long flags;

	agg = urb->context;
	if (!agg)
		return;

	tp = agg->context;
	if (!tp)
		return;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (!test_bit(WORK_ENABLE, &tp->flags))
		return;

	netdev = tp->netdev;

	/* When link down, the driver would cancel all bulks. */
	/* This avoid the re-submitting bulk */
	if (!netif_carrier_ok(netdev))
		return;

	usb_mark_last_busy(tp->udev);

	switch (status) {
	case 0:
		if (urb->actual_length < ETH_ZLEN)
			break;

		spin_lock_irqsave(&tp->rx_lock, flags);
		list_add_tail(&agg->list, &tp->rx_done);
		spin_unlock_irqrestore(&tp->rx_lock, flags);
		napi_schedule(&tp->napi);
		return;
	case -ESHUTDOWN:
		rtl_set_unplug(tp);
		netif_device_detach(tp->netdev);
		return;
	case -ENOENT:
		return;	/* the urb is in unlink state */
	case -ETIME:
		if (net_ratelimit())
			netdev_warn(netdev, "maybe reset is needed?\n");
		break;
	default:
		if (net_ratelimit())
			netdev_warn(netdev, "Rx status %d\n", status);
		break;
	}

	r8152_submit_rx(tp, agg, GFP_ATOMIC);
}

static void write_bulk_callback(struct urb *urb)
{
	struct net_device_stats *stats;
	struct net_device *netdev;
	struct tx_agg *agg;
	struct r8152 *tp;
	unsigned long flags;
	int status = urb->status;

	agg = urb->context;
	if (!agg)
		return;

	tp = agg->context;
	if (!tp)
		return;

	netdev = tp->netdev;
	stats = &netdev->stats;
	if (status) {
		if (net_ratelimit())
			netdev_warn(netdev, "Tx status %d\n", status);
		stats->tx_errors += agg->skb_num;
	} else {
		stats->tx_packets += agg->skb_num;
		stats->tx_bytes += agg->skb_len;
	}

	spin_lock_irqsave(&tp->tx_lock, flags);
	list_add_tail(&agg->list, &tp->tx_free);
	spin_unlock_irqrestore(&tp->tx_lock, flags);

	usb_autopm_put_interface_async(tp->intf);

	if (!netif_carrier_ok(netdev))
		return;

	if (!test_bit(WORK_ENABLE, &tp->flags))
		return;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (!skb_queue_empty(&tp->tx_queue))
		tasklet_schedule(&tp->tx_tl);
}

static void intr_callback(struct urb *urb)
{
	struct r8152 *tp;
	__le16 *d;
	int status = urb->status;
	int res;

	tp = urb->context;
	if (!tp)
		return;

	if (!test_bit(WORK_ENABLE, &tp->flags))
		return;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	switch (status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ESHUTDOWN:
		netif_device_detach(tp->netdev);
		fallthrough;
	case -ENOENT:
	case -EPROTO:
		netif_info(tp, intr, tp->netdev,
			   "Stop submitting intr, status %d\n", status);
		return;
	case -EOVERFLOW:
		netif_info(tp, intr, tp->netdev, "intr status -EOVERFLOW\n");
		goto resubmit;
	/* -EPIPE:  should clear the halt */
	default:
		netif_info(tp, intr, tp->netdev, "intr status %d\n", status);
		goto resubmit;
	}

	d = urb->transfer_buffer;
	if (INTR_LINK & __le16_to_cpu(d[0])) {
		if (!netif_carrier_ok(tp->netdev)) {
			set_bit(RTL8152_LINK_CHG, &tp->flags);
			schedule_delayed_work(&tp->schedule, 0);
		}
	} else {
		if (netif_carrier_ok(tp->netdev)) {
			netif_stop_queue(tp->netdev);
			set_bit(RTL8152_LINK_CHG, &tp->flags);
			schedule_delayed_work(&tp->schedule, 0);
		}
	}

resubmit:
	res = usb_submit_urb(urb, GFP_ATOMIC);
	if (res == -ENODEV) {
		rtl_set_unplug(tp);
		netif_device_detach(tp->netdev);
	} else if (res) {
		netif_err(tp, intr, tp->netdev,
			  "can't resubmit intr, status %d\n", res);
	}
}

static inline void *rx_agg_align(void *data)
{
	return (void *)ALIGN((uintptr_t)data, RX_ALIGN);
}

static inline void *tx_agg_align(void *data)
{
	return (void *)ALIGN((uintptr_t)data, TX_ALIGN);
}

static void free_rx_agg(struct r8152 *tp, struct rx_agg *agg)
{
	list_del(&agg->info_list);

	usb_free_urb(agg->urb);
	put_page(agg->page);
	kfree(agg);

	atomic_dec(&tp->rx_count);
}

static struct rx_agg *alloc_rx_agg(struct r8152 *tp, gfp_t mflags)
{
	struct net_device *netdev = tp->netdev;
	int node = netdev->dev.parent ? dev_to_node(netdev->dev.parent) : -1;
	unsigned int order = get_order(tp->rx_buf_sz);
	struct rx_agg *rx_agg;
	unsigned long flags;

	rx_agg = kmalloc_node(sizeof(*rx_agg), mflags, node);
	if (!rx_agg)
		return NULL;

	rx_agg->page = alloc_pages(mflags | __GFP_COMP, order);
	if (!rx_agg->page)
		goto free_rx;

	rx_agg->buffer = page_address(rx_agg->page);

	rx_agg->urb = usb_alloc_urb(0, mflags);
	if (!rx_agg->urb)
		goto free_buf;

	rx_agg->context = tp;

	INIT_LIST_HEAD(&rx_agg->list);
	INIT_LIST_HEAD(&rx_agg->info_list);
	spin_lock_irqsave(&tp->rx_lock, flags);
	list_add_tail(&rx_agg->info_list, &tp->rx_info);
	spin_unlock_irqrestore(&tp->rx_lock, flags);

	atomic_inc(&tp->rx_count);

	return rx_agg;

free_buf:
	__free_pages(rx_agg->page, order);
free_rx:
	kfree(rx_agg);
	return NULL;
}

static void free_all_mem(struct r8152 *tp)
{
	struct rx_agg *agg, *agg_next;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&tp->rx_lock, flags);

	list_for_each_entry_safe(agg, agg_next, &tp->rx_info, info_list)
		free_rx_agg(tp, agg);

	spin_unlock_irqrestore(&tp->rx_lock, flags);

	WARN_ON(atomic_read(&tp->rx_count));

	for (i = 0; i < RTL8152_MAX_TX; i++) {
		usb_free_urb(tp->tx_info[i].urb);
		tp->tx_info[i].urb = NULL;

		kfree(tp->tx_info[i].buffer);
		tp->tx_info[i].buffer = NULL;
		tp->tx_info[i].head = NULL;
	}

	usb_free_urb(tp->intr_urb);
	tp->intr_urb = NULL;

	kfree(tp->intr_buff);
	tp->intr_buff = NULL;
}

static int alloc_all_mem(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;
	struct usb_interface *intf = tp->intf;
	struct usb_host_interface *alt = intf->cur_altsetting;
	struct usb_host_endpoint *ep_intr = alt->endpoint + 2;
	int node, i;

	node = netdev->dev.parent ? dev_to_node(netdev->dev.parent) : -1;

	spin_lock_init(&tp->rx_lock);
	spin_lock_init(&tp->tx_lock);
	INIT_LIST_HEAD(&tp->rx_info);
	INIT_LIST_HEAD(&tp->tx_free);
	INIT_LIST_HEAD(&tp->rx_done);
	skb_queue_head_init(&tp->tx_queue);
	skb_queue_head_init(&tp->rx_queue);
	atomic_set(&tp->rx_count, 0);

	for (i = 0; i < RTL8152_MAX_RX; i++) {
		if (!alloc_rx_agg(tp, GFP_KERNEL))
			goto err1;
	}

	for (i = 0; i < RTL8152_MAX_TX; i++) {
		struct urb *urb;
		u8 *buf;

		buf = kmalloc_node(agg_buf_sz, GFP_KERNEL, node);
		if (!buf)
			goto err1;

		if (buf != tx_agg_align(buf)) {
			kfree(buf);
			buf = kmalloc_node(agg_buf_sz + TX_ALIGN, GFP_KERNEL,
					   node);
			if (!buf)
				goto err1;
		}

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(buf);
			goto err1;
		}

		INIT_LIST_HEAD(&tp->tx_info[i].list);
		tp->tx_info[i].context = tp;
		tp->tx_info[i].urb = urb;
		tp->tx_info[i].buffer = buf;
		tp->tx_info[i].head = tx_agg_align(buf);

		list_add_tail(&tp->tx_info[i].list, &tp->tx_free);
	}

	tp->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!tp->intr_urb)
		goto err1;

	tp->intr_buff = kmalloc(INTBUFSIZE, GFP_KERNEL);
	if (!tp->intr_buff)
		goto err1;

	tp->intr_interval = (int)ep_intr->desc.bInterval;
	usb_fill_int_urb(tp->intr_urb, tp->udev, usb_rcvintpipe(tp->udev, 3),
			 tp->intr_buff, INTBUFSIZE, intr_callback,
			 tp, tp->intr_interval);

	return 0;

err1:
	free_all_mem(tp);
	return -ENOMEM;
}

static struct tx_agg *r8152_get_tx_agg(struct r8152 *tp)
{
	struct tx_agg *agg = NULL;
	unsigned long flags;

	if (list_empty(&tp->tx_free))
		return NULL;

	spin_lock_irqsave(&tp->tx_lock, flags);
	if (!list_empty(&tp->tx_free)) {
		struct list_head *cursor;

		cursor = tp->tx_free.next;
		list_del_init(cursor);
		agg = list_entry(cursor, struct tx_agg, list);
	}
	spin_unlock_irqrestore(&tp->tx_lock, flags);

	return agg;
}

/* r8152_csum_workaround()
 * The hw limits the value of the transport offset. When the offset is out of
 * range, calculate the checksum by sw.
 */
static void r8152_csum_workaround(struct r8152 *tp, struct sk_buff *skb,
				  struct sk_buff_head *list)
{
	if (skb_shinfo(skb)->gso_size) {
		netdev_features_t features = tp->netdev->features;
		struct sk_buff *segs, *seg, *next;
		struct sk_buff_head seg_list;

		features &= ~(NETIF_F_SG | NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
		segs = skb_gso_segment(skb, features);
		if (IS_ERR(segs) || !segs)
			goto drop;

		__skb_queue_head_init(&seg_list);

		skb_list_walk_safe(segs, seg, next) {
			skb_mark_not_on_list(seg);
			__skb_queue_tail(&seg_list, seg);
		}

		skb_queue_splice(&seg_list, list);
		dev_kfree_skb(skb);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb_checksum_help(skb) < 0)
			goto drop;

		__skb_queue_head(list, skb);
	} else {
		struct net_device_stats *stats;

drop:
		stats = &tp->netdev->stats;
		stats->tx_dropped++;
		dev_kfree_skb(skb);
	}
}

static inline void rtl_tx_vlan_tag(struct tx_desc *desc, struct sk_buff *skb)
{
	if (skb_vlan_tag_present(skb)) {
		u32 opts2;

		opts2 = TX_VLAN_TAG | swab16(skb_vlan_tag_get(skb));
		desc->opts2 |= cpu_to_le32(opts2);
	}
}

static inline void rtl_rx_vlan_tag(struct rx_desc *desc, struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->opts2);

	if (opts2 & RX_VLAN_TAG)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       swab16(opts2 & 0xffff));
}

static int r8152_tx_csum(struct r8152 *tp, struct tx_desc *desc,
			 struct sk_buff *skb, u32 len, u32 transport_offset)
{
	u32 mss = skb_shinfo(skb)->gso_size;
	u32 opts1, opts2 = 0;
	int ret = TX_CSUM_SUCCESS;

	WARN_ON_ONCE(len > TX_LEN_MAX);

	opts1 = len | TX_FS | TX_LS;

	if (mss) {
		if (transport_offset > GTTCPHO_MAX) {
			netif_warn(tp, tx_err, tp->netdev,
				   "Invalid transport offset 0x%x for TSO\n",
				   transport_offset);
			ret = TX_CSUM_TSO;
			goto unavailable;
		}

		switch (vlan_get_protocol(skb)) {
		case htons(ETH_P_IP):
			opts1 |= GTSENDV4;
			break;

		case htons(ETH_P_IPV6):
			if (skb_cow_head(skb, 0)) {
				ret = TX_CSUM_TSO;
				goto unavailable;
			}
			tcp_v6_gso_csum_prep(skb);
			opts1 |= GTSENDV6;
			break;

		default:
			WARN_ON_ONCE(1);
			break;
		}

		opts1 |= transport_offset << GTTCPHO_SHIFT;
		opts2 |= min(mss, MSS_MAX) << MSS_SHIFT;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 ip_protocol;

		if (transport_offset > TCPHO_MAX) {
			netif_warn(tp, tx_err, tp->netdev,
				   "Invalid transport offset 0x%x\n",
				   transport_offset);
			ret = TX_CSUM_NONE;
			goto unavailable;
		}

		switch (vlan_get_protocol(skb)) {
		case htons(ETH_P_IP):
			opts2 |= IPV4_CS;
			ip_protocol = ip_hdr(skb)->protocol;
			break;

		case htons(ETH_P_IPV6):
			opts2 |= IPV6_CS;
			ip_protocol = ipv6_hdr(skb)->nexthdr;
			break;

		default:
			ip_protocol = IPPROTO_RAW;
			break;
		}

		if (ip_protocol == IPPROTO_TCP)
			opts2 |= TCP_CS;
		else if (ip_protocol == IPPROTO_UDP)
			opts2 |= UDP_CS;
		else
			WARN_ON_ONCE(1);

		opts2 |= transport_offset << TCPHO_SHIFT;
	}

	desc->opts2 = cpu_to_le32(opts2);
	desc->opts1 = cpu_to_le32(opts1);

unavailable:
	return ret;
}

static int r8152_tx_agg_fill(struct r8152 *tp, struct tx_agg *agg)
{
	struct sk_buff_head skb_head, *tx_queue = &tp->tx_queue;
	int remain, ret;
	u8 *tx_data;

	__skb_queue_head_init(&skb_head);
	spin_lock(&tx_queue->lock);
	skb_queue_splice_init(tx_queue, &skb_head);
	spin_unlock(&tx_queue->lock);

	tx_data = agg->head;
	agg->skb_num = 0;
	agg->skb_len = 0;
	remain = agg_buf_sz;

	while (remain >= ETH_ZLEN + sizeof(struct tx_desc)) {
		struct tx_desc *tx_desc;
		struct sk_buff *skb;
		unsigned int len;
		u32 offset;

		skb = __skb_dequeue(&skb_head);
		if (!skb)
			break;

		len = skb->len + sizeof(*tx_desc);

		if (len > remain) {
			__skb_queue_head(&skb_head, skb);
			break;
		}

		tx_data = tx_agg_align(tx_data);
		tx_desc = (struct tx_desc *)tx_data;

		offset = (u32)skb_transport_offset(skb);

		if (r8152_tx_csum(tp, tx_desc, skb, skb->len, offset)) {
			r8152_csum_workaround(tp, skb, &skb_head);
			continue;
		}

		rtl_tx_vlan_tag(tx_desc, skb);

		tx_data += sizeof(*tx_desc);

		len = skb->len;
		if (skb_copy_bits(skb, 0, tx_data, len) < 0) {
			struct net_device_stats *stats = &tp->netdev->stats;

			stats->tx_dropped++;
			dev_kfree_skb_any(skb);
			tx_data -= sizeof(*tx_desc);
			continue;
		}

		tx_data += len;
		agg->skb_len += len;
		agg->skb_num += skb_shinfo(skb)->gso_segs ?: 1;

		dev_kfree_skb_any(skb);

		remain = agg_buf_sz - (int)(tx_agg_align(tx_data) - agg->head);

		if (test_bit(DELL_TB_RX_AGG_BUG, &tp->flags))
			break;
	}

	if (!skb_queue_empty(&skb_head)) {
		spin_lock(&tx_queue->lock);
		skb_queue_splice(&skb_head, tx_queue);
		spin_unlock(&tx_queue->lock);
	}

	netif_tx_lock(tp->netdev);

	if (netif_queue_stopped(tp->netdev) &&
	    skb_queue_len(&tp->tx_queue) < tp->tx_qlen)
		netif_wake_queue(tp->netdev);

	netif_tx_unlock(tp->netdev);

	ret = usb_autopm_get_interface_async(tp->intf);
	if (ret < 0)
		goto out_tx_fill;

	usb_fill_bulk_urb(agg->urb, tp->udev, usb_sndbulkpipe(tp->udev, 2),
			  agg->head, (int)(tx_data - (u8 *)agg->head),
			  (usb_complete_t)write_bulk_callback, agg);

	ret = usb_submit_urb(agg->urb, GFP_ATOMIC);
	if (ret < 0)
		usb_autopm_put_interface_async(tp->intf);

out_tx_fill:
	return ret;
}

static u8 r8152_rx_csum(struct r8152 *tp, struct rx_desc *rx_desc)
{
	u8 checksum = CHECKSUM_NONE;
	u32 opts2, opts3;

	if (!(tp->netdev->features & NETIF_F_RXCSUM))
		goto return_result;

	opts2 = le32_to_cpu(rx_desc->opts2);
	opts3 = le32_to_cpu(rx_desc->opts3);

	if (opts2 & RD_IPV4_CS) {
		if (opts3 & IPF)
			checksum = CHECKSUM_NONE;
		else if ((opts2 & RD_UDP_CS) && !(opts3 & UDPF))
			checksum = CHECKSUM_UNNECESSARY;
		else if ((opts2 & RD_TCP_CS) && !(opts3 & TCPF))
			checksum = CHECKSUM_UNNECESSARY;
	} else if (opts2 & RD_IPV6_CS) {
		if ((opts2 & RD_UDP_CS) && !(opts3 & UDPF))
			checksum = CHECKSUM_UNNECESSARY;
		else if ((opts2 & RD_TCP_CS) && !(opts3 & TCPF))
			checksum = CHECKSUM_UNNECESSARY;
	}

return_result:
	return checksum;
}

static inline bool rx_count_exceed(struct r8152 *tp)
{
	return atomic_read(&tp->rx_count) > RTL8152_MAX_RX;
}

static inline int agg_offset(struct rx_agg *agg, void *addr)
{
	return (int)(addr - agg->buffer);
}

static struct rx_agg *rtl_get_free_rx(struct r8152 *tp, gfp_t mflags)
{
	struct rx_agg *agg, *agg_next, *agg_free = NULL;
	unsigned long flags;

	spin_lock_irqsave(&tp->rx_lock, flags);

	list_for_each_entry_safe(agg, agg_next, &tp->rx_used, list) {
		if (page_count(agg->page) == 1) {
			if (!agg_free) {
				list_del_init(&agg->list);
				agg_free = agg;
				continue;
			}
			if (rx_count_exceed(tp)) {
				list_del_init(&agg->list);
				free_rx_agg(tp, agg);
			}
			break;
		}
	}

	spin_unlock_irqrestore(&tp->rx_lock, flags);

	if (!agg_free && atomic_read(&tp->rx_count) < tp->rx_pending)
		agg_free = alloc_rx_agg(tp, mflags);

	return agg_free;
}

static int rx_bottom(struct r8152 *tp, int budget)
{
	unsigned long flags;
	struct list_head *cursor, *next, rx_queue;
	int ret = 0, work_done = 0;
	struct napi_struct *napi = &tp->napi;

	if (!skb_queue_empty(&tp->rx_queue)) {
		while (work_done < budget) {
			struct sk_buff *skb = __skb_dequeue(&tp->rx_queue);
			struct net_device *netdev = tp->netdev;
			struct net_device_stats *stats = &netdev->stats;
			unsigned int pkt_len;

			if (!skb)
				break;

			pkt_len = skb->len;
			napi_gro_receive(napi, skb);
			work_done++;
			stats->rx_packets++;
			stats->rx_bytes += pkt_len;
		}
	}

	if (list_empty(&tp->rx_done))
		goto out1;

	INIT_LIST_HEAD(&rx_queue);
	spin_lock_irqsave(&tp->rx_lock, flags);
	list_splice_init(&tp->rx_done, &rx_queue);
	spin_unlock_irqrestore(&tp->rx_lock, flags);

	list_for_each_safe(cursor, next, &rx_queue) {
		struct rx_desc *rx_desc;
		struct rx_agg *agg, *agg_free;
		int len_used = 0;
		struct urb *urb;
		u8 *rx_data;

		list_del_init(cursor);

		agg = list_entry(cursor, struct rx_agg, list);
		urb = agg->urb;
		if (urb->actual_length < ETH_ZLEN)
			goto submit;

		agg_free = rtl_get_free_rx(tp, GFP_ATOMIC);

		rx_desc = agg->buffer;
		rx_data = agg->buffer;
		len_used += sizeof(struct rx_desc);

		while (urb->actual_length > len_used) {
			struct net_device *netdev = tp->netdev;
			struct net_device_stats *stats = &netdev->stats;
			unsigned int pkt_len, rx_frag_head_sz;
			struct sk_buff *skb;

			/* limite the skb numbers for rx_queue */
			if (unlikely(skb_queue_len(&tp->rx_queue) >= 1000))
				break;

			pkt_len = le32_to_cpu(rx_desc->opts1) & RX_LEN_MASK;
			if (pkt_len < ETH_ZLEN)
				break;

			len_used += pkt_len;
			if (urb->actual_length < len_used)
				break;

			pkt_len -= ETH_FCS_LEN;
			rx_data += sizeof(struct rx_desc);

			if (!agg_free || tp->rx_copybreak > pkt_len)
				rx_frag_head_sz = pkt_len;
			else
				rx_frag_head_sz = tp->rx_copybreak;

			skb = napi_alloc_skb(napi, rx_frag_head_sz);
			if (!skb) {
				stats->rx_dropped++;
				goto find_next_rx;
			}

			skb->ip_summed = r8152_rx_csum(tp, rx_desc);
			memcpy(skb->data, rx_data, rx_frag_head_sz);
			skb_put(skb, rx_frag_head_sz);
			pkt_len -= rx_frag_head_sz;
			rx_data += rx_frag_head_sz;
			if (pkt_len) {
				skb_add_rx_frag(skb, 0, agg->page,
						agg_offset(agg, rx_data),
						pkt_len,
						SKB_DATA_ALIGN(pkt_len));
				get_page(agg->page);
			}

			skb->protocol = eth_type_trans(skb, netdev);
			rtl_rx_vlan_tag(rx_desc, skb);
			if (work_done < budget) {
				work_done++;
				stats->rx_packets++;
				stats->rx_bytes += skb->len;
				napi_gro_receive(napi, skb);
			} else {
				__skb_queue_tail(&tp->rx_queue, skb);
			}

find_next_rx:
			rx_data = rx_agg_align(rx_data + pkt_len + ETH_FCS_LEN);
			rx_desc = (struct rx_desc *)rx_data;
			len_used = agg_offset(agg, rx_data);
			len_used += sizeof(struct rx_desc);
		}

		WARN_ON(!agg_free && page_count(agg->page) > 1);

		if (agg_free) {
			spin_lock_irqsave(&tp->rx_lock, flags);
			if (page_count(agg->page) == 1) {
				list_add(&agg_free->list, &tp->rx_used);
			} else {
				list_add_tail(&agg->list, &tp->rx_used);
				agg = agg_free;
				urb = agg->urb;
			}
			spin_unlock_irqrestore(&tp->rx_lock, flags);
		}

submit:
		if (!ret) {
			ret = r8152_submit_rx(tp, agg, GFP_ATOMIC);
		} else {
			urb->actual_length = 0;
			list_add_tail(&agg->list, next);
		}
	}

	if (!list_empty(&rx_queue)) {
		spin_lock_irqsave(&tp->rx_lock, flags);
		list_splice_tail(&rx_queue, &tp->rx_done);
		spin_unlock_irqrestore(&tp->rx_lock, flags);
	}

out1:
	return work_done;
}

static void tx_bottom(struct r8152 *tp)
{
	int res;

	do {
		struct net_device *netdev = tp->netdev;
		struct tx_agg *agg;

		if (skb_queue_empty(&tp->tx_queue))
			break;

		agg = r8152_get_tx_agg(tp);
		if (!agg)
			break;

		res = r8152_tx_agg_fill(tp, agg);
		if (!res)
			continue;

		if (res == -ENODEV) {
			rtl_set_unplug(tp);
			netif_device_detach(netdev);
		} else {
			struct net_device_stats *stats = &netdev->stats;
			unsigned long flags;

			netif_warn(tp, tx_err, netdev,
				   "failed tx_urb %d\n", res);
			stats->tx_dropped += agg->skb_num;

			spin_lock_irqsave(&tp->tx_lock, flags);
			list_add_tail(&agg->list, &tp->tx_free);
			spin_unlock_irqrestore(&tp->tx_lock, flags);
		}
	} while (res == 0);
}

static void bottom_half(unsigned long data)
{
	struct r8152 *tp;

	tp = (struct r8152 *)data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (!test_bit(WORK_ENABLE, &tp->flags))
		return;

	/* When link down, the driver would cancel all bulks. */
	/* This avoid the re-submitting bulk */
	if (!netif_carrier_ok(tp->netdev))
		return;

	clear_bit(SCHEDULE_TASKLET, &tp->flags);

	tx_bottom(tp);
}

static int r8152_poll(struct napi_struct *napi, int budget)
{
	struct r8152 *tp = container_of(napi, struct r8152, napi);
	int work_done;

	work_done = rx_bottom(tp, budget);

	if (work_done < budget) {
		if (!napi_complete_done(napi, work_done))
			goto out;
		if (!list_empty(&tp->rx_done))
			napi_schedule(napi);
	}

out:
	return work_done;
}

static
int r8152_submit_rx(struct r8152 *tp, struct rx_agg *agg, gfp_t mem_flags)
{
	int ret;

	/* The rx would be stopped, so skip submitting */
	if (test_bit(RTL8152_UNPLUG, &tp->flags) ||
	    !test_bit(WORK_ENABLE, &tp->flags) || !netif_carrier_ok(tp->netdev))
		return 0;

	usb_fill_bulk_urb(agg->urb, tp->udev, usb_rcvbulkpipe(tp->udev, 1),
			  agg->buffer, tp->rx_buf_sz,
			  (usb_complete_t)read_bulk_callback, agg);

	ret = usb_submit_urb(agg->urb, mem_flags);
	if (ret == -ENODEV) {
		rtl_set_unplug(tp);
		netif_device_detach(tp->netdev);
	} else if (ret) {
		struct urb *urb = agg->urb;
		unsigned long flags;

		urb->actual_length = 0;
		spin_lock_irqsave(&tp->rx_lock, flags);
		list_add_tail(&agg->list, &tp->rx_done);
		spin_unlock_irqrestore(&tp->rx_lock, flags);

		netif_err(tp, rx_err, tp->netdev,
			  "Couldn't submit rx[%p], ret = %d\n", agg, ret);

		napi_schedule(&tp->napi);
	}

	return ret;
}

static void rtl_drop_queued_tx(struct r8152 *tp)
{
	struct net_device_stats *stats = &tp->netdev->stats;
	struct sk_buff_head skb_head, *tx_queue = &tp->tx_queue;
	struct sk_buff *skb;

	if (skb_queue_empty(tx_queue))
		return;

	__skb_queue_head_init(&skb_head);
	spin_lock_bh(&tx_queue->lock);
	skb_queue_splice_init(tx_queue, &skb_head);
	spin_unlock_bh(&tx_queue->lock);

	while ((skb = __skb_dequeue(&skb_head))) {
		dev_kfree_skb(skb);
		stats->tx_dropped++;
	}
}

static void rtl8152_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct r8152 *tp = netdev_priv(netdev);

	netif_warn(tp, tx_err, netdev, "Tx timeout\n");

	usb_queue_reset_device(tp->intf);
}

static void rtl8152_set_rx_mode(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (netif_carrier_ok(netdev)) {
		set_bit(RTL8152_SET_RX_MODE, &tp->flags);
		schedule_delayed_work(&tp->schedule, 0);
	}
}

static void _rtl8152_set_rx_mode(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	u32 mc_filter[2];	/* Multicast hash filter */
	__le32 tmp[2];
	u32 ocp_data;

	netif_stop_queue(netdev);
	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_data |= RCR_AB | RCR_APM;

	if (netdev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		netif_notice(tp, link, netdev, "Promiscuous mode enabled\n");
		ocp_data |= RCR_AM | RCR_AAP;
		mc_filter[1] = 0xffffffff;
		mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(netdev) > multicast_filter_limit) ||
		   (netdev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		ocp_data |= RCR_AM;
		mc_filter[1] = 0xffffffff;
		mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		mc_filter[1] = 0;
		mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, netdev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			ocp_data |= RCR_AM;
		}
	}

	tmp[0] = __cpu_to_le32(swab32(mc_filter[1]));
	tmp[1] = __cpu_to_le32(swab32(mc_filter[0]));

	pla_ocp_write(tp, PLA_MAR, BYTE_EN_DWORD, sizeof(tmp), tmp);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
	netif_wake_queue(netdev);
}

static netdev_features_t
rtl8152_features_check(struct sk_buff *skb, struct net_device *dev,
		       netdev_features_t features)
{
	u32 mss = skb_shinfo(skb)->gso_size;
	int max_offset = mss ? GTTCPHO_MAX : TCPHO_MAX;
	int offset = skb_transport_offset(skb);

	if ((mss || skb->ip_summed == CHECKSUM_PARTIAL) && offset > max_offset)
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
	else if ((skb->len + sizeof(struct tx_desc)) > agg_buf_sz)
		features &= ~NETIF_F_GSO_MASK;

	return features;
}

static netdev_tx_t rtl8152_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);

	skb_tx_timestamp(skb);

	skb_queue_tail(&tp->tx_queue, skb);

	if (!list_empty(&tp->tx_free)) {
		if (test_bit(SELECTIVE_SUSPEND, &tp->flags)) {
			set_bit(SCHEDULE_TASKLET, &tp->flags);
			schedule_delayed_work(&tp->schedule, 0);
		} else {
			usb_mark_last_busy(tp->udev);
			tasklet_schedule(&tp->tx_tl);
		}
	} else if (skb_queue_len(&tp->tx_queue) > tp->tx_qlen) {
		netif_stop_queue(netdev);
	}

	return NETDEV_TX_OK;
}

static void r8152b_reset_packet_filter(struct r8152 *tp)
{
	u32	ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_FMC);
	ocp_data &= ~FMC_FCR_MCU_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_FMC, ocp_data);
	ocp_data |= FMC_FCR_MCU_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_FMC, ocp_data);
}

static void rtl8152_nic_reset(struct r8152 *tp)
{
	int	i;

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, CR_RST);

	for (i = 0; i < 1000; i++) {
		if (!(ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CR) & CR_RST))
			break;
		usleep_range(100, 400);
	}
}

static void set_tx_qlen(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;

	tp->tx_qlen = agg_buf_sz / (netdev->mtu + VLAN_ETH_HLEN + ETH_FCS_LEN +
				    sizeof(struct tx_desc));
}

static inline u8 rtl8152_get_speed(struct r8152 *tp)
{
	return ocp_read_byte(tp, MCU_TYPE_PLA, PLA_PHYSTATUS);
}

static void rtl_set_eee_plus(struct r8152 *tp)
{
	u32 ocp_data;
	u8 speed;

	speed = rtl8152_get_speed(tp);
	if (speed & _10bps) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR);
		ocp_data |= EEEP_CR_EEEP_TX;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR, ocp_data);
	} else {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR);
		ocp_data &= ~EEEP_CR_EEEP_TX;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR, ocp_data);
	}
}

static void rxdy_gated_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MISC_1);
	if (enable)
		ocp_data |= RXDY_GATED_EN;
	else
		ocp_data &= ~RXDY_GATED_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MISC_1, ocp_data);
}

static int rtl_start_rx(struct r8152 *tp)
{
	struct rx_agg *agg, *agg_next;
	struct list_head tmp_list;
	unsigned long flags;
	int ret = 0, i = 0;

	INIT_LIST_HEAD(&tmp_list);

	spin_lock_irqsave(&tp->rx_lock, flags);

	INIT_LIST_HEAD(&tp->rx_done);
	INIT_LIST_HEAD(&tp->rx_used);

	list_splice_init(&tp->rx_info, &tmp_list);

	spin_unlock_irqrestore(&tp->rx_lock, flags);

	list_for_each_entry_safe(agg, agg_next, &tmp_list, info_list) {
		INIT_LIST_HEAD(&agg->list);

		/* Only RTL8152_MAX_RX rx_agg need to be submitted. */
		if (++i > RTL8152_MAX_RX) {
			spin_lock_irqsave(&tp->rx_lock, flags);
			list_add_tail(&agg->list, &tp->rx_used);
			spin_unlock_irqrestore(&tp->rx_lock, flags);
		} else if (unlikely(ret < 0)) {
			spin_lock_irqsave(&tp->rx_lock, flags);
			list_add_tail(&agg->list, &tp->rx_done);
			spin_unlock_irqrestore(&tp->rx_lock, flags);
		} else {
			ret = r8152_submit_rx(tp, agg, GFP_KERNEL);
		}
	}

	spin_lock_irqsave(&tp->rx_lock, flags);
	WARN_ON(!list_empty(&tp->rx_info));
	list_splice(&tmp_list, &tp->rx_info);
	spin_unlock_irqrestore(&tp->rx_lock, flags);

	return ret;
}

static int rtl_stop_rx(struct r8152 *tp)
{
	struct rx_agg *agg, *agg_next;
	struct list_head tmp_list;
	unsigned long flags;

	INIT_LIST_HEAD(&tmp_list);

	/* The usb_kill_urb() couldn't be used in atomic.
	 * Therefore, move the list of rx_info to a tmp one.
	 * Then, list_for_each_entry_safe could be used without
	 * spin lock.
	 */

	spin_lock_irqsave(&tp->rx_lock, flags);
	list_splice_init(&tp->rx_info, &tmp_list);
	spin_unlock_irqrestore(&tp->rx_lock, flags);

	list_for_each_entry_safe(agg, agg_next, &tmp_list, info_list) {
		/* At least RTL8152_MAX_RX rx_agg have the page_count being
		 * equal to 1, so the other ones could be freed safely.
		 */
		if (page_count(agg->page) > 1)
			free_rx_agg(tp, agg);
		else
			usb_kill_urb(agg->urb);
	}

	/* Move back the list of temp to the rx_info */
	spin_lock_irqsave(&tp->rx_lock, flags);
	WARN_ON(!list_empty(&tp->rx_info));
	list_splice(&tmp_list, &tp->rx_info);
	spin_unlock_irqrestore(&tp->rx_lock, flags);

	while (!skb_queue_empty(&tp->rx_queue))
		dev_kfree_skb(__skb_dequeue(&tp->rx_queue));

	return 0;
}

static inline void r8153b_rx_agg_chg_indicate(struct r8152 *tp)
{
	ocp_write_byte(tp, MCU_TYPE_USB, USB_UPT_RXDMA_OWN,
		       OWN_UPDATE | OWN_CLEAR);
}

static int rtl_enable(struct r8152 *tp)
{
	u32 ocp_data;

	r8152b_reset_packet_filter(tp);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CR);
	ocp_data |= CR_RE | CR_TE;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, ocp_data);

	switch (tp->version) {
	case RTL_VER_08:
	case RTL_VER_09:
		r8153b_rx_agg_chg_indicate(tp);
		break;
	default:
		break;
	}

	rxdy_gated_en(tp, false);

	return 0;
}

static int rtl8152_enable(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	set_tx_qlen(tp);
	rtl_set_eee_plus(tp);

	return rtl_enable(tp);
}

static void r8153_set_rx_early_timeout(struct r8152 *tp)
{
	u32 ocp_data = tp->coalesce / 8;

	switch (tp->version) {
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ocp_write_word(tp, MCU_TYPE_USB, USB_RX_EARLY_TIMEOUT,
			       ocp_data);
		break;

	case RTL_VER_08:
	case RTL_VER_09:
		/* The RTL8153B uses USB_RX_EXTRA_AGGR_TMR for rx timeout
		 * primarily. For USB_RX_EARLY_TIMEOUT, we fix it to 128ns.
		 */
		ocp_write_word(tp, MCU_TYPE_USB, USB_RX_EARLY_TIMEOUT,
			       128 / 8);
		ocp_write_word(tp, MCU_TYPE_USB, USB_RX_EXTRA_AGGR_TMR,
			       ocp_data);
		break;

	default:
		break;
	}
}

static void r8153_set_rx_early_size(struct r8152 *tp)
{
	u32 ocp_data = tp->rx_buf_sz - rx_reserved_size(tp->netdev->mtu);

	switch (tp->version) {
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ocp_write_word(tp, MCU_TYPE_USB, USB_RX_EARLY_SIZE,
			       ocp_data / 4);
		break;
	case RTL_VER_08:
	case RTL_VER_09:
		ocp_write_word(tp, MCU_TYPE_USB, USB_RX_EARLY_SIZE,
			       ocp_data / 8);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static int rtl8153_enable(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	set_tx_qlen(tp);
	rtl_set_eee_plus(tp);
	r8153_set_rx_early_timeout(tp);
	r8153_set_rx_early_size(tp);

	if (tp->version == RTL_VER_09) {
		u32 ocp_data;

		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_TASK);
		ocp_data &= ~FC_PATCH_TASK;
		ocp_write_word(tp, MCU_TYPE_USB, USB_FW_TASK, ocp_data);
		usleep_range(1000, 2000);
		ocp_data |= FC_PATCH_TASK;
		ocp_write_word(tp, MCU_TYPE_USB, USB_FW_TASK, ocp_data);
	}

	return rtl_enable(tp);
}

static void rtl_disable(struct r8152 *tp)
{
	u32 ocp_data;
	int i;

	if (test_bit(RTL8152_UNPLUG, &tp->flags)) {
		rtl_drop_queued_tx(tp);
		return;
	}

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);

	rtl_drop_queued_tx(tp);

	for (i = 0; i < RTL8152_MAX_TX; i++)
		usb_kill_urb(tp->tx_info[i].urb);

	rxdy_gated_en(tp, true);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if ((ocp_data & FIFO_EMPTY) == FIFO_EMPTY)
			break;
		usleep_range(1000, 2000);
	}

	for (i = 0; i < 1000; i++) {
		if (ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR0) & TCR0_TX_EMPTY)
			break;
		usleep_range(1000, 2000);
	}

	rtl_stop_rx(tp);

	rtl8152_nic_reset(tp);
}

static void r8152_power_cut_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_UPS_CTRL);
	if (enable)
		ocp_data |= POWER_CUT;
	else
		ocp_data &= ~POWER_CUT;
	ocp_write_word(tp, MCU_TYPE_USB, USB_UPS_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS);
	ocp_data &= ~RESUME_INDICATE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS, ocp_data);
}

static void rtl_rx_vlan_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CPCR);
	if (enable)
		ocp_data |= CPCR_RX_VLAN;
	else
		ocp_data &= ~CPCR_RX_VLAN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CPCR, ocp_data);
}

static int rtl8152_set_features(struct net_device *dev,
				netdev_features_t features)
{
	netdev_features_t changed = features ^ dev->features;
	struct r8152 *tp = netdev_priv(dev);
	int ret;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	mutex_lock(&tp->control);

	if (changed & NETIF_F_HW_VLAN_CTAG_RX) {
		if (features & NETIF_F_HW_VLAN_CTAG_RX)
			rtl_rx_vlan_en(tp, true);
		else
			rtl_rx_vlan_en(tp, false);
	}

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)

static u32 __rtl_get_wol(struct r8152 *tp)
{
	u32 ocp_data;
	u32 wolopts = 0;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG34);
	if (ocp_data & LINK_ON_WAKE_EN)
		wolopts |= WAKE_PHY;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG5);
	if (ocp_data & UWF_EN)
		wolopts |= WAKE_UCAST;
	if (ocp_data & BWF_EN)
		wolopts |= WAKE_BCAST;
	if (ocp_data & MWF_EN)
		wolopts |= WAKE_MCAST;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CFG_WOL);
	if (ocp_data & MAGIC_EN)
		wolopts |= WAKE_MAGIC;

	return wolopts;
}

static void __rtl_set_wol(struct r8152 *tp, u32 wolopts)
{
	u32 ocp_data;

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_CONFIG);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG34);
	ocp_data &= ~LINK_ON_WAKE_EN;
	if (wolopts & WAKE_PHY)
		ocp_data |= LINK_ON_WAKE_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CONFIG34, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG5);
	ocp_data &= ~(UWF_EN | BWF_EN | MWF_EN);
	if (wolopts & WAKE_UCAST)
		ocp_data |= UWF_EN;
	if (wolopts & WAKE_BCAST)
		ocp_data |= BWF_EN;
	if (wolopts & WAKE_MCAST)
		ocp_data |= MWF_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CONFIG5, ocp_data);

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CFG_WOL);
	ocp_data &= ~MAGIC_EN;
	if (wolopts & WAKE_MAGIC)
		ocp_data |= MAGIC_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CFG_WOL, ocp_data);

	if (wolopts & WAKE_ANY)
		device_set_wakeup_enable(&tp->udev->dev, true);
	else
		device_set_wakeup_enable(&tp->udev->dev, false);
}

static void r8153_u1u2en(struct r8152 *tp, bool enable)
{
	u8 u1u2[8];

	if (enable)
		memset(u1u2, 0xff, sizeof(u1u2));
	else
		memset(u1u2, 0x00, sizeof(u1u2));

	usb_ocp_write(tp, USB_TOLERANCE, BYTE_EN_SIX_BYTES, sizeof(u1u2), u1u2);
}

static void r8153b_u1u2en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_LPM_CONFIG);
	if (enable)
		ocp_data |= LPM_U1U2_EN;
	else
		ocp_data &= ~LPM_U1U2_EN;

	ocp_write_word(tp, MCU_TYPE_USB, USB_LPM_CONFIG, ocp_data);
}

static void r8153_u2p3en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_U2P3_CTRL);
	if (enable)
		ocp_data |= U2P3_ENABLE;
	else
		ocp_data &= ~U2P3_ENABLE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_U2P3_CTRL, ocp_data);
}

static void r8153b_ups_flags(struct r8152 *tp)
{
	u32 ups_flags = 0;

	if (tp->ups_info.green)
		ups_flags |= UPS_FLAGS_EN_GREEN;

	if (tp->ups_info.aldps)
		ups_flags |= UPS_FLAGS_EN_ALDPS;

	if (tp->ups_info.eee)
		ups_flags |= UPS_FLAGS_EN_EEE;

	if (tp->ups_info.flow_control)
		ups_flags |= UPS_FLAGS_EN_FLOW_CTR;

	if (tp->ups_info.eee_ckdiv)
		ups_flags |= UPS_FLAGS_EN_EEE_CKDIV;

	if (tp->ups_info.eee_cmod_lv)
		ups_flags |= UPS_FLAGS_EEE_CMOD_LV_EN;

	if (tp->ups_info._10m_ckdiv)
		ups_flags |= UPS_FLAGS_EN_10M_CKDIV;

	if (tp->ups_info.eee_plloff_100)
		ups_flags |= UPS_FLAGS_EEE_PLLOFF_100;

	if (tp->ups_info.eee_plloff_giga)
		ups_flags |= UPS_FLAGS_EEE_PLLOFF_GIGA;

	if (tp->ups_info._250m_ckdiv)
		ups_flags |= UPS_FLAGS_250M_CKDIV;

	if (tp->ups_info.ctap_short_off)
		ups_flags |= UPS_FLAGS_CTAP_SHORT_DIS;

	switch (tp->ups_info.speed_duplex) {
	case NWAY_10M_HALF:
		ups_flags |= ups_flags_speed(1);
		break;
	case NWAY_10M_FULL:
		ups_flags |= ups_flags_speed(2);
		break;
	case NWAY_100M_HALF:
		ups_flags |= ups_flags_speed(3);
		break;
	case NWAY_100M_FULL:
		ups_flags |= ups_flags_speed(4);
		break;
	case NWAY_1000M_FULL:
		ups_flags |= ups_flags_speed(5);
		break;
	case FORCE_10M_HALF:
		ups_flags |= ups_flags_speed(6);
		break;
	case FORCE_10M_FULL:
		ups_flags |= ups_flags_speed(7);
		break;
	case FORCE_100M_HALF:
		ups_flags |= ups_flags_speed(8);
		break;
	case FORCE_100M_FULL:
		ups_flags |= ups_flags_speed(9);
		break;
	default:
		break;
	}

	ocp_write_dword(tp, MCU_TYPE_USB, USB_UPS_FLAGS, ups_flags);
}

static void r8153b_green_en(struct r8152 *tp, bool enable)
{
	u16 data;

	if (enable) {
		sram_write(tp, 0x8045, 0);	/* 10M abiq&ldvbias */
		sram_write(tp, 0x804d, 0x1222);	/* 100M short abiq&ldvbias */
		sram_write(tp, 0x805d, 0x0022);	/* 1000M short abiq&ldvbias */
	} else {
		sram_write(tp, 0x8045, 0x2444);	/* 10M abiq&ldvbias */
		sram_write(tp, 0x804d, 0x2444);	/* 100M short abiq&ldvbias */
		sram_write(tp, 0x805d, 0x2444);	/* 1000M short abiq&ldvbias */
	}

	data = sram_read(tp, SRAM_GREEN_CFG);
	data |= GREEN_ETH_EN;
	sram_write(tp, SRAM_GREEN_CFG, data);

	tp->ups_info.green = enable;
}

static u16 r8153_phy_status(struct r8152 *tp, u16 desired)
{
	u16 data;
	int i;

	for (i = 0; i < 500; i++) {
		data = ocp_reg_read(tp, OCP_PHY_STATUS);
		data &= PHY_STAT_MASK;
		if (desired) {
			if (data == desired)
				break;
		} else if (data == PHY_STAT_LAN_ON || data == PHY_STAT_PWRDN ||
			   data == PHY_STAT_EXT_INIT) {
			break;
		}

		msleep(20);
		if (test_bit(RTL8152_UNPLUG, &tp->flags))
			break;
	}

	return data;
}

static void r8153b_ups_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_POWER_CUT);

	if (enable) {
		r8153b_ups_flags(tp);

		ocp_data |= UPS_EN | USP_PREWAKE | PHASE2_EN;
		ocp_write_byte(tp, MCU_TYPE_USB, USB_POWER_CUT, ocp_data);

		ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, 0xcfff);
		ocp_data |= BIT(0);
		ocp_write_byte(tp, MCU_TYPE_USB, 0xcfff, ocp_data);
	} else {
		u16 data;

		ocp_data &= ~(UPS_EN | USP_PREWAKE);
		ocp_write_byte(tp, MCU_TYPE_USB, USB_POWER_CUT, ocp_data);

		ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, 0xcfff);
		ocp_data &= ~BIT(0);
		ocp_write_byte(tp, MCU_TYPE_USB, 0xcfff, ocp_data);

		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_MISC_0);
		ocp_data &= ~PCUT_STATUS;
		ocp_write_word(tp, MCU_TYPE_USB, USB_MISC_0, ocp_data);

		data = r8153_phy_status(tp, 0);

		switch (data) {
		case PHY_STAT_PWRDN:
		case PHY_STAT_EXT_INIT:
			r8153b_green_en(tp,
					test_bit(GREEN_ETHERNET, &tp->flags));

			data = r8152_mdio_read(tp, MII_BMCR);
			data &= ~BMCR_PDOWN;
			data |= BMCR_RESET;
			r8152_mdio_write(tp, MII_BMCR, data);

			data = r8153_phy_status(tp, PHY_STAT_LAN_ON);
			fallthrough;

		default:
			if (data != PHY_STAT_LAN_ON)
				netif_warn(tp, link, tp->netdev,
					   "PHY not ready");
			break;
		}
	}
}

static void r8153_power_cut_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_POWER_CUT);
	if (enable)
		ocp_data |= PWR_EN | PHASE2_EN;
	else
		ocp_data &= ~(PWR_EN | PHASE2_EN);
	ocp_write_word(tp, MCU_TYPE_USB, USB_POWER_CUT, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_MISC_0);
	ocp_data &= ~PCUT_STATUS;
	ocp_write_word(tp, MCU_TYPE_USB, USB_MISC_0, ocp_data);
}

static void r8153b_power_cut_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_POWER_CUT);
	if (enable)
		ocp_data |= PWR_EN | PHASE2_EN;
	else
		ocp_data &= ~PWR_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_POWER_CUT, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_MISC_0);
	ocp_data &= ~PCUT_STATUS;
	ocp_write_word(tp, MCU_TYPE_USB, USB_MISC_0, ocp_data);
}

static void r8153_queue_wake(struct r8152 *tp, bool enable)
{
	u32 ocp_data;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_INDICATE_FALG);
	if (enable)
		ocp_data |= UPCOMING_RUNTIME_D3;
	else
		ocp_data &= ~UPCOMING_RUNTIME_D3;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_INDICATE_FALG, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_SUSPEND_FLAG);
	ocp_data &= ~LINK_CHG_EVENT;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_SUSPEND_FLAG, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS);
	ocp_data &= ~LINK_CHANGE_FLAG;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS, ocp_data);
}

static bool rtl_can_wakeup(struct r8152 *tp)
{
	struct usb_device *udev = tp->udev;

	return (udev->actconfig->desc.bmAttributes & USB_CONFIG_ATT_WAKEUP);
}

static void rtl_runtime_suspend_enable(struct r8152 *tp, bool enable)
{
	if (enable) {
		u32 ocp_data;

		__rtl_set_wol(tp, WAKE_ANY);

		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_CONFIG);

		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG34);
		ocp_data |= LINK_OFF_WAKE_EN;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_CONFIG34, ocp_data);

		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);
	} else {
		u32 ocp_data;

		__rtl_set_wol(tp, tp->saved_wolopts);

		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_CONFIG);

		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CONFIG34);
		ocp_data &= ~LINK_OFF_WAKE_EN;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_CONFIG34, ocp_data);

		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);
	}
}

static void rtl8153_runtime_enable(struct r8152 *tp, bool enable)
{
	if (enable) {
		r8153_u1u2en(tp, false);
		r8153_u2p3en(tp, false);
		rtl_runtime_suspend_enable(tp, true);
	} else {
		rtl_runtime_suspend_enable(tp, false);

		switch (tp->version) {
		case RTL_VER_03:
		case RTL_VER_04:
			break;
		case RTL_VER_05:
		case RTL_VER_06:
		default:
			r8153_u2p3en(tp, true);
			break;
		}

		r8153_u1u2en(tp, true);
	}
}

static void rtl8153b_runtime_enable(struct r8152 *tp, bool enable)
{
	if (enable) {
		r8153_queue_wake(tp, true);
		r8153b_u1u2en(tp, false);
		r8153_u2p3en(tp, false);
		rtl_runtime_suspend_enable(tp, true);
		r8153b_ups_en(tp, true);
	} else {
		r8153b_ups_en(tp, false);
		r8153_queue_wake(tp, false);
		rtl_runtime_suspend_enable(tp, false);
		if (tp->udev->speed != USB_SPEED_HIGH)
			r8153b_u1u2en(tp, true);
	}
}

static void r8153_teredo_off(struct r8152 *tp)
{
	u32 ocp_data;

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
	case RTL_VER_07:
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG);
		ocp_data &= ~(TEREDO_SEL | TEREDO_RS_EVENT_MASK |
			      OOB_TEREDO_EN);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, ocp_data);
		break;

	case RTL_VER_08:
	case RTL_VER_09:
		/* The bit 0 ~ 7 are relative with teredo settings. They are
		 * W1C (write 1 to clear), so set all 1 to disable it.
		 */
		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, 0xff);
		break;

	default:
		break;
	}

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_WDT6_CTRL, WDT6_SET_MODE);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_REALWOW_TIMER, 0);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TEREDO_TIMER, 0);
}

static void rtl_reset_bmu(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_BMU_RESET);
	ocp_data &= ~(BMU_RESET_EP_IN | BMU_RESET_EP_OUT);
	ocp_write_byte(tp, MCU_TYPE_USB, USB_BMU_RESET, ocp_data);
	ocp_data |= BMU_RESET_EP_IN | BMU_RESET_EP_OUT;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_BMU_RESET, ocp_data);
}

/* Clear the bp to stop the firmware before loading a new one */
static void rtl_clear_bp(struct r8152 *tp, u16 type)
{
	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		break;
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ocp_write_byte(tp, type, PLA_BP_EN, 0);
		break;
	case RTL_VER_08:
	case RTL_VER_09:
	default:
		if (type == MCU_TYPE_USB) {
			ocp_write_byte(tp, MCU_TYPE_USB, USB_BP2_EN, 0);

			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_8, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_9, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_10, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_11, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_12, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_13, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_14, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_15, 0);
		} else {
			ocp_write_byte(tp, MCU_TYPE_PLA, PLA_BP_EN, 0);
		}
		break;
	}

	ocp_write_word(tp, type, PLA_BP_0, 0);
	ocp_write_word(tp, type, PLA_BP_1, 0);
	ocp_write_word(tp, type, PLA_BP_2, 0);
	ocp_write_word(tp, type, PLA_BP_3, 0);
	ocp_write_word(tp, type, PLA_BP_4, 0);
	ocp_write_word(tp, type, PLA_BP_5, 0);
	ocp_write_word(tp, type, PLA_BP_6, 0);
	ocp_write_word(tp, type, PLA_BP_7, 0);

	/* wait 3 ms to make sure the firmware is stopped */
	usleep_range(3000, 6000);
	ocp_write_word(tp, type, PLA_BP_BA, 0);
}

static int r8153_patch_request(struct r8152 *tp, bool request)
{
	u16 data;
	int i;

	data = ocp_reg_read(tp, OCP_PHY_PATCH_CMD);
	if (request)
		data |= PATCH_REQUEST;
	else
		data &= ~PATCH_REQUEST;
	ocp_reg_write(tp, OCP_PHY_PATCH_CMD, data);

	for (i = 0; request && i < 5000; i++) {
		usleep_range(1000, 2000);
		if (ocp_reg_read(tp, OCP_PHY_PATCH_STAT) & PATCH_READY)
			break;
	}

	if (request && !(ocp_reg_read(tp, OCP_PHY_PATCH_STAT) & PATCH_READY)) {
		netif_err(tp, drv, tp->netdev, "patch request fail\n");
		r8153_patch_request(tp, false);
		return -ETIME;
	} else {
		return 0;
	}
}

static int r8153_pre_ram_code(struct r8152 *tp, u16 key_addr, u16 patch_key)
{
	if (r8153_patch_request(tp, true)) {
		dev_err(&tp->intf->dev, "patch request fail\n");
		return -ETIME;
	}

	sram_write(tp, key_addr, patch_key);
	sram_write(tp, SRAM_PHY_LOCK, PHY_PATCH_LOCK);

	return 0;
}

static int r8153_post_ram_code(struct r8152 *tp, u16 key_addr)
{
	u16 data;

	sram_write(tp, 0x0000, 0x0000);

	data = ocp_reg_read(tp, OCP_PHY_LOCK);
	data &= ~PATCH_LOCK;
	ocp_reg_write(tp, OCP_PHY_LOCK, data);

	sram_write(tp, key_addr, 0x0000);

	r8153_patch_request(tp, false);

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, tp->ocp_base);

	return 0;
}

static bool rtl8152_is_fw_phy_nc_ok(struct r8152 *tp, struct fw_phy_nc *phy)
{
	u32 length;
	u16 fw_offset, fw_reg, ba_reg, patch_en_addr, mode_reg, bp_start;
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		fw_reg = 0xa014;
		ba_reg = 0xa012;
		patch_en_addr = 0xa01a;
		mode_reg = 0xb820;
		bp_start = 0xa000;
		break;
	default:
		goto out;
	}

	fw_offset = __le16_to_cpu(phy->fw_offset);
	if (fw_offset < sizeof(*phy)) {
		dev_err(&tp->intf->dev, "fw_offset too small\n");
		goto out;
	}

	length = __le32_to_cpu(phy->blk_hdr.length);
	if (length < fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= __le16_to_cpu(phy->fw_offset);
	if (!length || (length & 1)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(phy->fw_reg) != fw_reg) {
		dev_err(&tp->intf->dev, "invalid register to load firmware\n");
		goto out;
	}

	if (__le16_to_cpu(phy->ba_reg) != ba_reg) {
		dev_err(&tp->intf->dev, "invalid base address register\n");
		goto out;
	}

	if (__le16_to_cpu(phy->patch_en_addr) != patch_en_addr) {
		dev_err(&tp->intf->dev,
			"invalid patch mode enabled register\n");
		goto out;
	}

	if (__le16_to_cpu(phy->mode_reg) != mode_reg) {
		dev_err(&tp->intf->dev,
			"invalid register to switch the mode\n");
		goto out;
	}

	if (__le16_to_cpu(phy->bp_start) != bp_start) {
		dev_err(&tp->intf->dev,
			"invalid start register of break point\n");
		goto out;
	}

	if (__le16_to_cpu(phy->bp_num) > 4) {
		dev_err(&tp->intf->dev, "invalid break point number\n");
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_mac_ok(struct r8152 *tp, struct fw_mac *mac)
{
	u16 fw_reg, bp_ba_addr, bp_en_addr, bp_start, fw_offset;
	bool rc = false;
	u32 length, type;
	int i, max_bp;

	type = __le32_to_cpu(mac->blk_hdr.type);
	if (type == RTL_FW_PLA) {
		switch (tp->version) {
		case RTL_VER_01:
		case RTL_VER_02:
		case RTL_VER_07:
			fw_reg = 0xf800;
			bp_ba_addr = PLA_BP_BA;
			bp_en_addr = 0;
			bp_start = PLA_BP_0;
			max_bp = 8;
			break;
		case RTL_VER_03:
		case RTL_VER_04:
		case RTL_VER_05:
		case RTL_VER_06:
		case RTL_VER_08:
		case RTL_VER_09:
			fw_reg = 0xf800;
			bp_ba_addr = PLA_BP_BA;
			bp_en_addr = PLA_BP_EN;
			bp_start = PLA_BP_0;
			max_bp = 8;
			break;
		default:
			goto out;
		}
	} else if (type == RTL_FW_USB) {
		switch (tp->version) {
		case RTL_VER_03:
		case RTL_VER_04:
		case RTL_VER_05:
		case RTL_VER_06:
			fw_reg = 0xf800;
			bp_ba_addr = USB_BP_BA;
			bp_en_addr = USB_BP_EN;
			bp_start = USB_BP_0;
			max_bp = 8;
			break;
		case RTL_VER_08:
		case RTL_VER_09:
			fw_reg = 0xe600;
			bp_ba_addr = USB_BP_BA;
			bp_en_addr = USB_BP2_EN;
			bp_start = USB_BP_0;
			max_bp = 16;
			break;
		case RTL_VER_01:
		case RTL_VER_02:
		case RTL_VER_07:
		default:
			goto out;
		}
	} else {
		goto out;
	}

	fw_offset = __le16_to_cpu(mac->fw_offset);
	if (fw_offset < sizeof(*mac)) {
		dev_err(&tp->intf->dev, "fw_offset too small\n");
		goto out;
	}

	length = __le32_to_cpu(mac->blk_hdr.length);
	if (length < fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= fw_offset;
	if (length < 4 || (length & 3)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(mac->fw_reg) != fw_reg) {
		dev_err(&tp->intf->dev, "invalid register to load firmware\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_ba_addr) != bp_ba_addr) {
		dev_err(&tp->intf->dev, "invalid base address register\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_en_addr) != bp_en_addr) {
		dev_err(&tp->intf->dev, "invalid enabled mask register\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_start) != bp_start) {
		dev_err(&tp->intf->dev,
			"invalid start register of break point\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_num) > max_bp) {
		dev_err(&tp->intf->dev, "invalid break point number\n");
		goto out;
	}

	for (i = __le16_to_cpu(mac->bp_num); i < max_bp; i++) {
		if (mac->bp[i]) {
			dev_err(&tp->intf->dev, "unused bp%u is not zero\n", i);
			goto out;
		}
	}

	rc = true;
out:
	return rc;
}

/* Verify the checksum for the firmware file. It is calculated from the version
 * field to the end of the file. Compare the result with the checksum field to
 * make sure the file is correct.
 */
static long rtl8152_fw_verify_checksum(struct r8152 *tp,
				       struct fw_header *fw_hdr, size_t size)
{
	unsigned char checksum[sizeof(fw_hdr->checksum)];
	struct crypto_shash *alg;
	struct shash_desc *sdesc;
	size_t len;
	long rc;

	alg = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(alg)) {
		rc = PTR_ERR(alg);
		goto out;
	}

	if (crypto_shash_digestsize(alg) != sizeof(fw_hdr->checksum)) {
		rc = -EFAULT;
		dev_err(&tp->intf->dev, "digestsize incorrect (%u)\n",
			crypto_shash_digestsize(alg));
		goto free_shash;
	}

	len = sizeof(*sdesc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(len, GFP_KERNEL);
	if (!sdesc) {
		rc = -ENOMEM;
		goto free_shash;
	}
	sdesc->tfm = alg;

	len = size - sizeof(fw_hdr->checksum);
	rc = crypto_shash_digest(sdesc, fw_hdr->version, len, checksum);
	kfree(sdesc);
	if (rc)
		goto free_shash;

	if (memcmp(fw_hdr->checksum, checksum, sizeof(fw_hdr->checksum))) {
		dev_err(&tp->intf->dev, "checksum fail\n");
		rc = -EFAULT;
	}

free_shash:
	crypto_free_shash(alg);
out:
	return rc;
}

static long rtl8152_check_firmware(struct r8152 *tp, struct rtl_fw *rtl_fw)
{
	const struct firmware *fw = rtl_fw->fw;
	struct fw_header *fw_hdr = (struct fw_header *)fw->data;
	struct fw_mac *pla = NULL, *usb = NULL;
	struct fw_phy_patch_key *start = NULL;
	struct fw_phy_nc *phy_nc = NULL;
	struct fw_block *stop = NULL;
	long ret = -EFAULT;
	int i;

	if (fw->size < sizeof(*fw_hdr)) {
		dev_err(&tp->intf->dev, "file too small\n");
		goto fail;
	}

	ret = rtl8152_fw_verify_checksum(tp, fw_hdr, fw->size);
	if (ret)
		goto fail;

	ret = -EFAULT;

	for (i = sizeof(*fw_hdr); i < fw->size;) {
		struct fw_block *block = (struct fw_block *)&fw->data[i];
		u32 type;

		if ((i + sizeof(*block)) > fw->size)
			goto fail;

		type = __le32_to_cpu(block->type);
		switch (type) {
		case RTL_FW_END:
			if (__le32_to_cpu(block->length) != sizeof(*block))
				goto fail;
			goto fw_end;
		case RTL_FW_PLA:
			if (pla) {
				dev_err(&tp->intf->dev,
					"multiple PLA firmware encountered");
				goto fail;
			}

			pla = (struct fw_mac *)block;
			if (!rtl8152_is_fw_mac_ok(tp, pla)) {
				dev_err(&tp->intf->dev,
					"check PLA firmware failed\n");
				goto fail;
			}
			break;
		case RTL_FW_USB:
			if (usb) {
				dev_err(&tp->intf->dev,
					"multiple USB firmware encountered");
				goto fail;
			}

			usb = (struct fw_mac *)block;
			if (!rtl8152_is_fw_mac_ok(tp, usb)) {
				dev_err(&tp->intf->dev,
					"check USB firmware failed\n");
				goto fail;
			}
			break;
		case RTL_FW_PHY_START:
			if (start || phy_nc || stop) {
				dev_err(&tp->intf->dev,
					"check PHY_START fail\n");
				goto fail;
			}

			if (__le32_to_cpu(block->length) != sizeof(*start)) {
				dev_err(&tp->intf->dev,
					"Invalid length for PHY_START\n");
				goto fail;
			}

			start = (struct fw_phy_patch_key *)block;
			break;
		case RTL_FW_PHY_STOP:
			if (stop || !start) {
				dev_err(&tp->intf->dev,
					"Check PHY_STOP fail\n");
				goto fail;
			}

			if (__le32_to_cpu(block->length) != sizeof(*block)) {
				dev_err(&tp->intf->dev,
					"Invalid length for PHY_STOP\n");
				goto fail;
			}

			stop = block;
			break;
		case RTL_FW_PHY_NC:
			if (!start || stop) {
				dev_err(&tp->intf->dev,
					"check PHY_NC fail\n");
				goto fail;
			}

			if (phy_nc) {
				dev_err(&tp->intf->dev,
					"multiple PHY NC encountered\n");
				goto fail;
			}

			phy_nc = (struct fw_phy_nc *)block;
			if (!rtl8152_is_fw_phy_nc_ok(tp, phy_nc)) {
				dev_err(&tp->intf->dev,
					"check PHY NC firmware failed\n");
				goto fail;
			}

			break;
		default:
			dev_warn(&tp->intf->dev, "Unknown type %u is found\n",
				 type);
			break;
		}

		/* next block */
		i += ALIGN(__le32_to_cpu(block->length), 8);
	}

fw_end:
	if ((phy_nc || start) && !stop) {
		dev_err(&tp->intf->dev, "without PHY_STOP\n");
		goto fail;
	}

	return 0;
fail:
	return ret;
}

static void rtl8152_fw_phy_nc_apply(struct r8152 *tp, struct fw_phy_nc *phy)
{
	u16 mode_reg, bp_index;
	u32 length, i, num;
	__le16 *data;

	mode_reg = __le16_to_cpu(phy->mode_reg);
	sram_write(tp, mode_reg, __le16_to_cpu(phy->mode_pre));
	sram_write(tp, __le16_to_cpu(phy->ba_reg),
		   __le16_to_cpu(phy->ba_data));

	length = __le32_to_cpu(phy->blk_hdr.length);
	length -= __le16_to_cpu(phy->fw_offset);
	num = length / 2;
	data = (__le16 *)((u8 *)phy + __le16_to_cpu(phy->fw_offset));

	ocp_reg_write(tp, OCP_SRAM_ADDR, __le16_to_cpu(phy->fw_reg));
	for (i = 0; i < num; i++)
		ocp_reg_write(tp, OCP_SRAM_DATA, __le16_to_cpu(data[i]));

	sram_write(tp, __le16_to_cpu(phy->patch_en_addr),
		   __le16_to_cpu(phy->patch_en_value));

	bp_index = __le16_to_cpu(phy->bp_start);
	num = __le16_to_cpu(phy->bp_num);
	for (i = 0; i < num; i++) {
		sram_write(tp, bp_index, __le16_to_cpu(phy->bp[i]));
		bp_index += 2;
	}

	sram_write(tp, mode_reg, __le16_to_cpu(phy->mode_post));

	dev_dbg(&tp->intf->dev, "successfully applied %s\n", phy->info);
}

static void rtl8152_fw_mac_apply(struct r8152 *tp, struct fw_mac *mac)
{
	u16 bp_en_addr, bp_index, type, bp_num, fw_ver_reg;
	u32 length;
	u8 *data;
	int i;

	switch (__le32_to_cpu(mac->blk_hdr.type)) {
	case RTL_FW_PLA:
		type = MCU_TYPE_PLA;
		break;
	case RTL_FW_USB:
		type = MCU_TYPE_USB;
		break;
	default:
		return;
	}

	rtl_clear_bp(tp, type);

	/* Enable backup/restore of MACDBG. This is required after clearing PLA
	 * break points and before applying the PLA firmware.
	 */
	if (tp->version == RTL_VER_04 && type == MCU_TYPE_PLA &&
	    !(ocp_read_word(tp, MCU_TYPE_PLA, PLA_MACDBG_POST) & DEBUG_OE)) {
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_MACDBG_PRE, DEBUG_LTSSM);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_MACDBG_POST, DEBUG_LTSSM);
	}

	length = __le32_to_cpu(mac->blk_hdr.length);
	length -= __le16_to_cpu(mac->fw_offset);

	data = (u8 *)mac;
	data += __le16_to_cpu(mac->fw_offset);

	generic_ocp_write(tp, __le16_to_cpu(mac->fw_reg), 0xff, length, data,
			  type);

	ocp_write_word(tp, type, __le16_to_cpu(mac->bp_ba_addr),
		       __le16_to_cpu(mac->bp_ba_value));

	bp_index = __le16_to_cpu(mac->bp_start);
	bp_num = __le16_to_cpu(mac->bp_num);
	for (i = 0; i < bp_num; i++) {
		ocp_write_word(tp, type, bp_index, __le16_to_cpu(mac->bp[i]));
		bp_index += 2;
	}

	bp_en_addr = __le16_to_cpu(mac->bp_en_addr);
	if (bp_en_addr)
		ocp_write_word(tp, type, bp_en_addr,
			       __le16_to_cpu(mac->bp_en_value));

	fw_ver_reg = __le16_to_cpu(mac->fw_ver_reg);
	if (fw_ver_reg)
		ocp_write_byte(tp, MCU_TYPE_USB, fw_ver_reg,
			       mac->fw_ver_data);

	dev_dbg(&tp->intf->dev, "successfully applied %s\n", mac->info);
}

static void rtl8152_apply_firmware(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;
	const struct firmware *fw;
	struct fw_header *fw_hdr;
	struct fw_phy_patch_key *key;
	u16 key_addr = 0;
	int i;

	if (IS_ERR_OR_NULL(rtl_fw->fw))
		return;

	fw = rtl_fw->fw;
	fw_hdr = (struct fw_header *)fw->data;

	if (rtl_fw->pre_fw)
		rtl_fw->pre_fw(tp);

	for (i = offsetof(struct fw_header, blocks); i < fw->size;) {
		struct fw_block *block = (struct fw_block *)&fw->data[i];

		switch (__le32_to_cpu(block->type)) {
		case RTL_FW_END:
			goto post_fw;
		case RTL_FW_PLA:
		case RTL_FW_USB:
			rtl8152_fw_mac_apply(tp, (struct fw_mac *)block);
			break;
		case RTL_FW_PHY_START:
			key = (struct fw_phy_patch_key *)block;
			key_addr = __le16_to_cpu(key->key_reg);
			r8153_pre_ram_code(tp, key_addr,
					   __le16_to_cpu(key->key_data));
			break;
		case RTL_FW_PHY_STOP:
			WARN_ON(!key_addr);
			r8153_post_ram_code(tp, key_addr);
			break;
		case RTL_FW_PHY_NC:
			rtl8152_fw_phy_nc_apply(tp, (struct fw_phy_nc *)block);
			break;
		default:
			break;
		}

		i += ALIGN(__le32_to_cpu(block->length), 8);
	}

post_fw:
	if (rtl_fw->post_fw)
		rtl_fw->post_fw(tp);

	strscpy(rtl_fw->version, fw_hdr->version, RTL_VER_SIZE);
	dev_info(&tp->intf->dev, "load %s successfully\n", rtl_fw->version);
}

static void rtl8152_release_firmware(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;

	if (!IS_ERR_OR_NULL(rtl_fw->fw)) {
		release_firmware(rtl_fw->fw);
		rtl_fw->fw = NULL;
	}
}

static int rtl8152_request_firmware(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;
	long rc;

	if (rtl_fw->fw || !rtl_fw->fw_name) {
		dev_info(&tp->intf->dev, "skip request firmware\n");
		rc = 0;
		goto result;
	}

	rc = request_firmware(&rtl_fw->fw, rtl_fw->fw_name, &tp->intf->dev);
	if (rc < 0)
		goto result;

	rc = rtl8152_check_firmware(tp, rtl_fw);
	if (rc < 0)
		release_firmware(rtl_fw->fw);

result:
	if (rc) {
		rtl_fw->fw = ERR_PTR(rc);

		dev_warn(&tp->intf->dev,
			 "unable to load firmware patch %s (%ld)\n",
			 rtl_fw->fw_name, rc);
	}

	return rc;
}

static void r8152_aldps_en(struct r8152 *tp, bool enable)
{
	if (enable) {
		ocp_reg_write(tp, OCP_ALDPS_CONFIG, ENPWRSAVE | ENPDNPS |
						    LINKENA | DIS_SDSAVE);
	} else {
		ocp_reg_write(tp, OCP_ALDPS_CONFIG, ENPDNPS | LINKENA |
						    DIS_SDSAVE);
		msleep(20);
	}
}

static inline void r8152_mmd_indirect(struct r8152 *tp, u16 dev, u16 reg)
{
	ocp_reg_write(tp, OCP_EEE_AR, FUN_ADDR | dev);
	ocp_reg_write(tp, OCP_EEE_DATA, reg);
	ocp_reg_write(tp, OCP_EEE_AR, FUN_DATA | dev);
}

static u16 r8152_mmd_read(struct r8152 *tp, u16 dev, u16 reg)
{
	u16 data;

	r8152_mmd_indirect(tp, dev, reg);
	data = ocp_reg_read(tp, OCP_EEE_DATA);
	ocp_reg_write(tp, OCP_EEE_AR, 0x0000);

	return data;
}

static void r8152_mmd_write(struct r8152 *tp, u16 dev, u16 reg, u16 data)
{
	r8152_mmd_indirect(tp, dev, reg);
	ocp_reg_write(tp, OCP_EEE_DATA, data);
	ocp_reg_write(tp, OCP_EEE_AR, 0x0000);
}

static void r8152_eee_en(struct r8152 *tp, bool enable)
{
	u16 config1, config2, config3;
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEE_CR);
	config1 = ocp_reg_read(tp, OCP_EEE_CONFIG1) & ~sd_rise_time_mask;
	config2 = ocp_reg_read(tp, OCP_EEE_CONFIG2);
	config3 = ocp_reg_read(tp, OCP_EEE_CONFIG3) & ~fast_snr_mask;

	if (enable) {
		ocp_data |= EEE_RX_EN | EEE_TX_EN;
		config1 |= EEE_10_CAP | EEE_NWAY_EN | TX_QUIET_EN | RX_QUIET_EN;
		config1 |= sd_rise_time(1);
		config2 |= RG_DACQUIET_EN | RG_LDVQUIET_EN;
		config3 |= fast_snr(42);
	} else {
		ocp_data &= ~(EEE_RX_EN | EEE_TX_EN);
		config1 &= ~(EEE_10_CAP | EEE_NWAY_EN | TX_QUIET_EN |
			     RX_QUIET_EN);
		config1 |= sd_rise_time(7);
		config2 &= ~(RG_DACQUIET_EN | RG_LDVQUIET_EN);
		config3 |= fast_snr(511);
	}

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEE_CR, ocp_data);
	ocp_reg_write(tp, OCP_EEE_CONFIG1, config1);
	ocp_reg_write(tp, OCP_EEE_CONFIG2, config2);
	ocp_reg_write(tp, OCP_EEE_CONFIG3, config3);
}

static void r8153_eee_en(struct r8152 *tp, bool enable)
{
	u32 ocp_data;
	u16 config;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEE_CR);
	config = ocp_reg_read(tp, OCP_EEE_CFG);

	if (enable) {
		ocp_data |= EEE_RX_EN | EEE_TX_EN;
		config |= EEE10_EN;
	} else {
		ocp_data &= ~(EEE_RX_EN | EEE_TX_EN);
		config &= ~EEE10_EN;
	}

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEE_CR, ocp_data);
	ocp_reg_write(tp, OCP_EEE_CFG, config);

	tp->ups_info.eee = enable;
}

static void rtl_eee_enable(struct r8152 *tp, bool enable)
{
	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		if (enable) {
			r8152_eee_en(tp, true);
			r8152_mmd_write(tp, MDIO_MMD_AN, MDIO_AN_EEE_ADV,
					tp->eee_adv);
		} else {
			r8152_eee_en(tp, false);
			r8152_mmd_write(tp, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0);
		}
		break;
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
	case RTL_VER_08:
	case RTL_VER_09:
		if (enable) {
			r8153_eee_en(tp, true);
			ocp_reg_write(tp, OCP_EEE_ADV, tp->eee_adv);
		} else {
			r8153_eee_en(tp, false);
			ocp_reg_write(tp, OCP_EEE_ADV, 0);
		}
		break;
	default:
		break;
	}
}

static void r8152b_enable_fc(struct r8152 *tp)
{
	u16 anar;

	anar = r8152_mdio_read(tp, MII_ADVERTISE);
	anar |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	r8152_mdio_write(tp, MII_ADVERTISE, anar);

	tp->ups_info.flow_control = true;
}

static void rtl8152_disable(struct r8152 *tp)
{
	r8152_aldps_en(tp, false);
	rtl_disable(tp);
	r8152_aldps_en(tp, true);
}

static void r8152b_hw_phy_cfg(struct r8152 *tp)
{
	rtl8152_apply_firmware(tp);
	rtl_eee_enable(tp, tp->eee_en);
	r8152_aldps_en(tp, true);
	r8152b_enable_fc(tp);

	set_bit(PHY_RESET, &tp->flags);
}

static void wait_oob_link_list_ready(struct r8152 *tp)
{
	u32 ocp_data;
	int i;

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if (ocp_data & LINK_LIST_READY)
			break;
		usleep_range(1000, 2000);
	}
}

static void r8152b_exit_oob(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);

	rxdy_gated_en(tp, true);
	r8153_teredo_off(tp);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, 0x00);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data &= ~MCU_BORW_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	rtl8152_nic_reset(tp);

	/* rx share fifo credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);

	if (tp->udev->speed == USB_SPEED_FULL ||
	    tp->udev->speed == USB_SPEED_LOW) {
		/* rx share fifo credit near full threshold */
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1,
				RXFIFO_THR2_FULL);
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2,
				RXFIFO_THR3_FULL);
	} else {
		/* rx share fifo credit near full threshold */
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1,
				RXFIFO_THR2_HIGH);
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2,
				RXFIFO_THR3_HIGH);
	}

	/* TX share fifo free credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL);

	ocp_write_byte(tp, MCU_TYPE_USB, USB_TX_AGG, TX_AGG_MAX_THRESHOLD);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_RX_BUF_TH, RX_THR_HIGH);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_TX_DMA,
			TEST_MODE_DISABLE | TX_SIZE_ADJUST1);

	rtl_rx_vlan_en(tp, tp->netdev->features & NETIF_F_HW_VLAN_CTAG_RX);

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, RTL8152_RMS);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR0);
	ocp_data |= TCR0_AUTO_FIFO;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_TCR0, ocp_data);
}

static void r8152b_enter_oob(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_OOB);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_OOB);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_OOB);

	rtl_disable(tp);

	wait_oob_link_list_ready(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, RTL8152_RMS);

	rtl_rx_vlan_en(tp, true);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BDC_CR);
	ocp_data |= ALDPS_PROXY_MODE;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_BDC_CR, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data |= NOW_IS_OOB | DIS_MCU_CLROOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	rxdy_gated_en(tp, false);

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data |= RCR_APM | RCR_AM | RCR_AB;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
}

static int r8153_pre_firmware_1(struct r8152 *tp)
{
	int i;

	/* Wait till the WTD timer is ready. It would take at most 104 ms. */
	for (i = 0; i < 104; i++) {
		u32 ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_WDT1_CTRL);

		if (!(ocp_data & WTD1_EN))
			break;
		usleep_range(1000, 2000);
	}

	return 0;
}

static int r8153_post_firmware_1(struct r8152 *tp)
{
	/* set USB_BP_4 to support USB_SPEED_SUPER only */
	if (ocp_read_byte(tp, MCU_TYPE_USB, USB_CSTMR) & FORCE_SUPER)
		ocp_write_word(tp, MCU_TYPE_USB, USB_BP_4, BP4_SUPER_ONLY);

	/* reset UPHY timer to 36 ms */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_UPHY_TIMER, 36000 / 16);

	return 0;
}

static int r8153_pre_firmware_2(struct r8152 *tp)
{
	u32 ocp_data;

	r8153_pre_firmware_1(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0);
	ocp_data &= ~FW_FIX_SUSPEND;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0, ocp_data);

	return 0;
}

static int r8153_post_firmware_2(struct r8152 *tp)
{
	u32 ocp_data;

	/* enable bp0 if support USB_SPEED_SUPER only */
	if (ocp_read_byte(tp, MCU_TYPE_USB, USB_CSTMR) & FORCE_SUPER) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BP_EN);
		ocp_data |= BIT(0);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_EN, ocp_data);
	}

	/* reset UPHY timer to 36 ms */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_UPHY_TIMER, 36000 / 16);

	/* enable U3P3 check, set the counter to 4 */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS, U3P3_CHECK_EN | 4);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0);
	ocp_data |= FW_FIX_SUSPEND;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_USB2PHY);
	ocp_data |= USB2PHY_L1 | USB2PHY_SUSPEND;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_USB2PHY, ocp_data);

	return 0;
}

static int r8153_post_firmware_3(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_USB2PHY);
	ocp_data |= USB2PHY_L1 | USB2PHY_SUSPEND;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_USB2PHY, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1);
	ocp_data |= FW_IP_RESET_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1, ocp_data);

	return 0;
}

static int r8153b_pre_firmware_1(struct r8152 *tp)
{
	/* enable fc timer and set timer to 1 second. */
	ocp_write_word(tp, MCU_TYPE_USB, USB_FC_TIMER,
		       CTRL_TIMER_EN | (1000 / 8));

	return 0;
}

static int r8153b_post_firmware_1(struct r8152 *tp)
{
	u32 ocp_data;

	/* enable bp0 for RTL8153-BND */
	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_MISC_1);
	if (ocp_data & BND_MASK) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BP_EN);
		ocp_data |= BIT(0);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_EN, ocp_data);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_CTRL);
	ocp_data |= FLOW_CTRL_PATCH_OPT;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_TASK);
	ocp_data |= FC_PATCH_TASK;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_TASK, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1);
	ocp_data |= FW_IP_RESET_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1, ocp_data);

	return 0;
}

static void r8153_aldps_en(struct r8152 *tp, bool enable)
{
	u16 data;

	data = ocp_reg_read(tp, OCP_POWER_CFG);
	if (enable) {
		data |= EN_ALDPS;
		ocp_reg_write(tp, OCP_POWER_CFG, data);
	} else {
		int i;

		data &= ~EN_ALDPS;
		ocp_reg_write(tp, OCP_POWER_CFG, data);
		for (i = 0; i < 20; i++) {
			usleep_range(1000, 2000);
			if (ocp_read_word(tp, MCU_TYPE_PLA, 0xe000) & 0x0100)
				break;
		}
	}

	tp->ups_info.aldps = enable;
}

static void r8153_hw_phy_cfg(struct r8152 *tp)
{
	u32 ocp_data;
	u16 data;

	/* disable ALDPS before updating the PHY parameters */
	r8153_aldps_en(tp, false);

	/* disable EEE before updating the PHY parameters */
	rtl_eee_enable(tp, false);

	rtl8152_apply_firmware(tp);

	if (tp->version == RTL_VER_03) {
		data = ocp_reg_read(tp, OCP_EEE_CFG);
		data &= ~CTAP_SHORT_EN;
		ocp_reg_write(tp, OCP_EEE_CFG, data);
	}

	data = ocp_reg_read(tp, OCP_POWER_CFG);
	data |= EEE_CLKDIV_EN;
	ocp_reg_write(tp, OCP_POWER_CFG, data);

	data = ocp_reg_read(tp, OCP_DOWN_SPEED);
	data |= EN_10M_BGOFF;
	ocp_reg_write(tp, OCP_DOWN_SPEED, data);
	data = ocp_reg_read(tp, OCP_POWER_CFG);
	data |= EN_10M_PLLOFF;
	ocp_reg_write(tp, OCP_POWER_CFG, data);
	sram_write(tp, SRAM_IMPEDANCE, 0x0b13);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR);
	ocp_data |= PFM_PWM_SWITCH;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR, ocp_data);

	/* Enable LPF corner auto tune */
	sram_write(tp, SRAM_LPF_CFG, 0xf70f);

	/* Adjust 10M Amplitude */
	sram_write(tp, SRAM_10M_AMP1, 0x00af);
	sram_write(tp, SRAM_10M_AMP2, 0x0208);

	if (tp->eee_en)
		rtl_eee_enable(tp, true);

	r8153_aldps_en(tp, true);
	r8152b_enable_fc(tp);

	switch (tp->version) {
	case RTL_VER_03:
	case RTL_VER_04:
		break;
	case RTL_VER_05:
	case RTL_VER_06:
	default:
		r8153_u2p3en(tp, true);
		break;
	}

	set_bit(PHY_RESET, &tp->flags);
}

static u32 r8152_efuse_read(struct r8152 *tp, u8 addr)
{
	u32 ocp_data;

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EFUSE_CMD, EFUSE_READ_CMD | addr);
	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EFUSE_CMD);
	ocp_data = (ocp_data & EFUSE_DATA_BIT16) << 9;	/* data of bit16 */
	ocp_data |= ocp_read_word(tp, MCU_TYPE_PLA, PLA_EFUSE_DATA);

	return ocp_data;
}

static void r8153b_hw_phy_cfg(struct r8152 *tp)
{
	u32 ocp_data;
	u16 data;

	/* disable ALDPS before updating the PHY parameters */
	r8153_aldps_en(tp, false);

	/* disable EEE before updating the PHY parameters */
	rtl_eee_enable(tp, false);

	rtl8152_apply_firmware(tp);

	r8153b_green_en(tp, test_bit(GREEN_ETHERNET, &tp->flags));

	data = sram_read(tp, SRAM_GREEN_CFG);
	data |= R_TUNE_EN;
	sram_write(tp, SRAM_GREEN_CFG, data);
	data = ocp_reg_read(tp, OCP_NCTL_CFG);
	data |= PGA_RETURN_EN;
	ocp_reg_write(tp, OCP_NCTL_CFG, data);

	/* ADC Bias Calibration:
	 * read efuse offset 0x7d to get a 17-bit data. Remove the dummy/fake
	 * bit (bit3) to rebuild the real 16-bit data. Write the data to the
	 * ADC ioffset.
	 */
	ocp_data = r8152_efuse_read(tp, 0x7d);
	data = (u16)(((ocp_data & 0x1fff0) >> 1) | (ocp_data & 0x7));
	if (data != 0xffff)
		ocp_reg_write(tp, OCP_ADC_IOFFSET, data);

	/* ups mode tx-link-pulse timing adjustment:
	 * rg_saw_cnt = OCP reg 0xC426 Bit[13:0]
	 * swr_cnt_1ms_ini = 16000000 / rg_saw_cnt
	 */
	ocp_data = ocp_reg_read(tp, 0xc426);
	ocp_data &= 0x3fff;
	if (ocp_data) {
		u32 swr_cnt_1ms_ini;

		swr_cnt_1ms_ini = (16000000 / ocp_data) & SAW_CNT_1MS_MASK;
		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_UPS_CFG);
		ocp_data = (ocp_data & ~SAW_CNT_1MS_MASK) | swr_cnt_1ms_ini;
		ocp_write_word(tp, MCU_TYPE_USB, USB_UPS_CFG, ocp_data);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR);
	ocp_data |= PFM_PWM_SWITCH;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR, ocp_data);

	/* Advnace EEE */
	if (!r8153_patch_request(tp, true)) {
		data = ocp_reg_read(tp, OCP_POWER_CFG);
		data |= EEE_CLKDIV_EN;
		ocp_reg_write(tp, OCP_POWER_CFG, data);
		tp->ups_info.eee_ckdiv = true;

		data = ocp_reg_read(tp, OCP_DOWN_SPEED);
		data |= EN_EEE_CMODE | EN_EEE_1000 | EN_10M_CLKDIV;
		ocp_reg_write(tp, OCP_DOWN_SPEED, data);
		tp->ups_info.eee_cmod_lv = true;
		tp->ups_info._10m_ckdiv = true;
		tp->ups_info.eee_plloff_giga = true;

		ocp_reg_write(tp, OCP_SYSCLK_CFG, 0);
		ocp_reg_write(tp, OCP_SYSCLK_CFG, clk_div_expo(5));
		tp->ups_info._250m_ckdiv = true;

		r8153_patch_request(tp, false);
	}

	if (tp->eee_en)
		rtl_eee_enable(tp, true);

	r8153_aldps_en(tp, true);
	r8152b_enable_fc(tp);

	set_bit(PHY_RESET, &tp->flags);
}

static void r8153_first_init(struct r8152 *tp)
{
	u32 ocp_data;

	rxdy_gated_en(tp, true);
	r8153_teredo_off(tp);

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);

	rtl8152_nic_reset(tp);
	rtl_reset_bmu(tp);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data &= ~MCU_BORW_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	rtl_rx_vlan_en(tp, tp->netdev->features & NETIF_F_HW_VLAN_CTAG_RX);

	ocp_data = tp->netdev->mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, ocp_data);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_MTPS, MTPS_JUMBO);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR0);
	ocp_data |= TCR0_AUTO_FIFO;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_TCR0, ocp_data);

	rtl8152_nic_reset(tp);

	/* rx share fifo credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_NORMAL);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_NORMAL);
	/* TX share fifo free credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL2);
}

static void r8153_enter_oob(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	rtl_disable(tp);
	rtl_reset_bmu(tp);

	wait_oob_link_list_ready(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	wait_oob_link_list_ready(tp);

	ocp_data = tp->netdev->mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, ocp_data);

	switch (tp->version) {
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG);
		ocp_data &= ~TEREDO_WAKE_MASK;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_TEREDO_CFG, ocp_data);
		break;

	case RTL_VER_08:
	case RTL_VER_09:
		/* Clear teredo wake event. bit[15:8] is the teredo wakeup
		 * type. Set it to zero. bits[7:0] are the W1C bits about
		 * the events. Set them to all 1 to clear them.
		 */
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_TEREDO_WAKE_BASE, 0x00ff);
		break;

	default:
		break;
	}

	rtl_rx_vlan_en(tp, true);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BDC_CR);
	ocp_data |= ALDPS_PROXY_MODE;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_BDC_CR, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data |= NOW_IS_OOB | DIS_MCU_CLROOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	rxdy_gated_en(tp, false);

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data |= RCR_APM | RCR_AM | RCR_AB;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
}

static void rtl8153_disable(struct r8152 *tp)
{
	r8153_aldps_en(tp, false);
	rtl_disable(tp);
	rtl_reset_bmu(tp);
	r8153_aldps_en(tp, true);
}

static int rtl8152_set_speed(struct r8152 *tp, u8 autoneg, u32 speed, u8 duplex,
			     u32 advertising)
{
	u16 bmcr;
	int ret = 0;

	if (autoneg == AUTONEG_DISABLE) {
		if (duplex != DUPLEX_HALF && duplex != DUPLEX_FULL)
			return -EINVAL;

		switch (speed) {
		case SPEED_10:
			bmcr = BMCR_SPEED10;
			if (duplex == DUPLEX_FULL) {
				bmcr |= BMCR_FULLDPLX;
				tp->ups_info.speed_duplex = FORCE_10M_FULL;
			} else {
				tp->ups_info.speed_duplex = FORCE_10M_HALF;
			}
			break;
		case SPEED_100:
			bmcr = BMCR_SPEED100;
			if (duplex == DUPLEX_FULL) {
				bmcr |= BMCR_FULLDPLX;
				tp->ups_info.speed_duplex = FORCE_100M_FULL;
			} else {
				tp->ups_info.speed_duplex = FORCE_100M_HALF;
			}
			break;
		case SPEED_1000:
			if (tp->mii.supports_gmii) {
				bmcr = BMCR_SPEED1000 | BMCR_FULLDPLX;
				tp->ups_info.speed_duplex = NWAY_1000M_FULL;
				break;
			}
			fallthrough;
		default:
			ret = -EINVAL;
			goto out;
		}

		if (duplex == DUPLEX_FULL)
			tp->mii.full_duplex = 1;
		else
			tp->mii.full_duplex = 0;

		tp->mii.force_media = 1;
	} else {
		u16 anar, tmp1;
		u32 support;

		support = RTL_ADVERTISED_10_HALF | RTL_ADVERTISED_10_FULL |
			  RTL_ADVERTISED_100_HALF | RTL_ADVERTISED_100_FULL;

		if (tp->mii.supports_gmii)
			support |= RTL_ADVERTISED_1000_FULL;

		if (!(advertising & support))
			return -EINVAL;

		anar = r8152_mdio_read(tp, MII_ADVERTISE);
		tmp1 = anar & ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
				ADVERTISE_100HALF | ADVERTISE_100FULL);
		if (advertising & RTL_ADVERTISED_10_HALF) {
			tmp1 |= ADVERTISE_10HALF;
			tp->ups_info.speed_duplex = NWAY_10M_HALF;
		}
		if (advertising & RTL_ADVERTISED_10_FULL) {
			tmp1 |= ADVERTISE_10FULL;
			tp->ups_info.speed_duplex = NWAY_10M_FULL;
		}

		if (advertising & RTL_ADVERTISED_100_HALF) {
			tmp1 |= ADVERTISE_100HALF;
			tp->ups_info.speed_duplex = NWAY_100M_HALF;
		}
		if (advertising & RTL_ADVERTISED_100_FULL) {
			tmp1 |= ADVERTISE_100FULL;
			tp->ups_info.speed_duplex = NWAY_100M_FULL;
		}

		if (anar != tmp1) {
			r8152_mdio_write(tp, MII_ADVERTISE, tmp1);
			tp->mii.advertising = tmp1;
		}

		if (tp->mii.supports_gmii) {
			u16 gbcr;

			gbcr = r8152_mdio_read(tp, MII_CTRL1000);
			tmp1 = gbcr & ~(ADVERTISE_1000FULL |
					ADVERTISE_1000HALF);

			if (advertising & RTL_ADVERTISED_1000_FULL) {
				tmp1 |= ADVERTISE_1000FULL;
				tp->ups_info.speed_duplex = NWAY_1000M_FULL;
			}

			if (gbcr != tmp1)
				r8152_mdio_write(tp, MII_CTRL1000, tmp1);
		}

		bmcr = BMCR_ANENABLE | BMCR_ANRESTART;

		tp->mii.force_media = 0;
	}

	if (test_and_clear_bit(PHY_RESET, &tp->flags))
		bmcr |= BMCR_RESET;

	r8152_mdio_write(tp, MII_BMCR, bmcr);

	if (bmcr & BMCR_RESET) {
		int i;

		for (i = 0; i < 50; i++) {
			msleep(20);
			if ((r8152_mdio_read(tp, MII_BMCR) & BMCR_RESET) == 0)
				break;
		}
	}

out:
	return ret;
}

static void rtl8152_up(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8152_aldps_en(tp, false);
	r8152b_exit_oob(tp);
	r8152_aldps_en(tp, true);
}

static void rtl8152_down(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags)) {
		rtl_drop_queued_tx(tp);
		return;
	}

	r8152_power_cut_en(tp, false);
	r8152_aldps_en(tp, false);
	r8152b_enter_oob(tp);
	r8152_aldps_en(tp, true);
}

static void rtl8153_up(struct r8152 *tp)
{
	u32 ocp_data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153_u1u2en(tp, false);
	r8153_u2p3en(tp, false);
	r8153_aldps_en(tp, false);
	r8153_first_init(tp);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6);
	ocp_data |= LANWAKE_CLR_EN;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_LWAKE_CTRL_REG);
	ocp_data &= ~LANWAKE_PIN;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_LWAKE_CTRL_REG, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_SSPHYLINK1);
	ocp_data &= ~DELAY_PHY_PWR_CHG;
	ocp_write_word(tp, MCU_TYPE_USB, USB_SSPHYLINK1, ocp_data);

	r8153_aldps_en(tp, true);

	switch (tp->version) {
	case RTL_VER_03:
	case RTL_VER_04:
		break;
	case RTL_VER_05:
	case RTL_VER_06:
	default:
		r8153_u2p3en(tp, true);
		break;
	}

	r8153_u1u2en(tp, true);
}

static void rtl8153_down(struct r8152 *tp)
{
	u32 ocp_data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags)) {
		rtl_drop_queued_tx(tp);
		return;
	}

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6);
	ocp_data &= ~LANWAKE_CLR_EN;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6, ocp_data);

	r8153_u1u2en(tp, false);
	r8153_u2p3en(tp, false);
	r8153_power_cut_en(tp, false);
	r8153_aldps_en(tp, false);
	r8153_enter_oob(tp);
	r8153_aldps_en(tp, true);
}

static void rtl8153b_up(struct r8152 *tp)
{
	u32 ocp_data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153b_u1u2en(tp, false);
	r8153_u2p3en(tp, false);
	r8153_aldps_en(tp, false);

	r8153_first_init(tp);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_RX_BUF_TH, RX_THR_B);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3);
	ocp_data &= ~PLA_MCU_SPDWN_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3, ocp_data);

	r8153_aldps_en(tp, true);

	if (tp->udev->speed != USB_SPEED_HIGH)
		r8153b_u1u2en(tp, true);
}

static void rtl8153b_down(struct r8152 *tp)
{
	u32 ocp_data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags)) {
		rtl_drop_queued_tx(tp);
		return;
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3);
	ocp_data |= PLA_MCU_SPDWN_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3, ocp_data);

	r8153b_u1u2en(tp, false);
	r8153_u2p3en(tp, false);
	r8153b_power_cut_en(tp, false);
	r8153_aldps_en(tp, false);
	r8153_enter_oob(tp);
	r8153_aldps_en(tp, true);
}

static bool rtl8152_in_nway(struct r8152 *tp)
{
	u16 nway_state;

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, 0x2000);
	tp->ocp_base = 0x2000;
	ocp_write_byte(tp, MCU_TYPE_PLA, 0xb014, 0x4c);		/* phy state */
	nway_state = ocp_read_word(tp, MCU_TYPE_PLA, 0xb01a);

	/* bit 15: TXDIS_STATE, bit 14: ABD_STATE */
	if (nway_state & 0xc000)
		return false;
	else
		return true;
}

static bool rtl8153_in_nway(struct r8152 *tp)
{
	u16 phy_state = ocp_reg_read(tp, OCP_PHY_STATE) & 0xff;

	if (phy_state == TXDIS_STATE || phy_state == ABD_STATE)
		return false;
	else
		return true;
}

static void set_carrier(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;
	struct napi_struct *napi = &tp->napi;
	u8 speed;

	speed = rtl8152_get_speed(tp);

	if (speed & LINK_STATUS) {
		if (!netif_carrier_ok(netdev)) {
			tp->rtl_ops.enable(tp);
			netif_stop_queue(netdev);
			napi_disable(napi);
			netif_carrier_on(netdev);
			rtl_start_rx(tp);
			clear_bit(RTL8152_SET_RX_MODE, &tp->flags);
			_rtl8152_set_rx_mode(netdev);
			napi_enable(&tp->napi);
			netif_wake_queue(netdev);
			netif_info(tp, link, netdev, "carrier on\n");
		} else if (netif_queue_stopped(netdev) &&
			   skb_queue_len(&tp->tx_queue) < tp->tx_qlen) {
			netif_wake_queue(netdev);
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			netif_carrier_off(netdev);
			tasklet_disable(&tp->tx_tl);
			napi_disable(napi);
			tp->rtl_ops.disable(tp);
			napi_enable(napi);
			tasklet_enable(&tp->tx_tl);
			netif_info(tp, link, netdev, "carrier off\n");
		}
	}
}

static void rtl_work_func_t(struct work_struct *work)
{
	struct r8152 *tp = container_of(work, struct r8152, schedule.work);

	/* If the device is unplugged or !netif_running(), the workqueue
	 * doesn't need to wake the device, and could return directly.
	 */
	if (test_bit(RTL8152_UNPLUG, &tp->flags) || !netif_running(tp->netdev))
		return;

	if (usb_autopm_get_interface(tp->intf) < 0)
		return;

	if (!test_bit(WORK_ENABLE, &tp->flags))
		goto out1;

	if (!mutex_trylock(&tp->control)) {
		schedule_delayed_work(&tp->schedule, 0);
		goto out1;
	}

	if (test_and_clear_bit(RTL8152_LINK_CHG, &tp->flags))
		set_carrier(tp);

	if (test_and_clear_bit(RTL8152_SET_RX_MODE, &tp->flags))
		_rtl8152_set_rx_mode(tp->netdev);

	/* don't schedule tasket before linking */
	if (test_and_clear_bit(SCHEDULE_TASKLET, &tp->flags) &&
	    netif_carrier_ok(tp->netdev))
		tasklet_schedule(&tp->tx_tl);

	mutex_unlock(&tp->control);

out1:
	usb_autopm_put_interface(tp->intf);
}

static void rtl_hw_phy_work_func_t(struct work_struct *work)
{
	struct r8152 *tp = container_of(work, struct r8152, hw_phy_work.work);

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (usb_autopm_get_interface(tp->intf) < 0)
		return;

	mutex_lock(&tp->control);

	if (rtl8152_request_firmware(tp) == -ENODEV && tp->rtl_fw.retry) {
		tp->rtl_fw.retry = false;
		tp->rtl_fw.fw = NULL;

		/* Delay execution in case request_firmware() is not ready yet.
		 */
		queue_delayed_work(system_long_wq, &tp->hw_phy_work, HZ * 10);
		goto ignore_once;
	}

	tp->rtl_ops.hw_phy_cfg(tp);

	rtl8152_set_speed(tp, tp->autoneg, tp->speed, tp->duplex,
			  tp->advertising);

ignore_once:
	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);
}

#ifdef CONFIG_PM_SLEEP
static int rtl_notifier(struct notifier_block *nb, unsigned long action,
			void *data)
{
	struct r8152 *tp = container_of(nb, struct r8152, pm_notifier);

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		usb_autopm_get_interface(tp->intf);
		break;

	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		usb_autopm_put_interface(tp->intf);
		break;

	case PM_POST_RESTORE:
	case PM_RESTORE_PREPARE:
	default:
		break;
	}

	return NOTIFY_DONE;
}
#endif

static int rtl8152_open(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	int res = 0;

	if (work_busy(&tp->hw_phy_work.work) & WORK_BUSY_PENDING) {
		cancel_delayed_work_sync(&tp->hw_phy_work);
		rtl_hw_phy_work_func_t(&tp->hw_phy_work.work);
	}

	res = alloc_all_mem(tp);
	if (res)
		goto out;

	res = usb_autopm_get_interface(tp->intf);
	if (res < 0)
		goto out_free;

	mutex_lock(&tp->control);

	tp->rtl_ops.up(tp);

	netif_carrier_off(netdev);
	netif_start_queue(netdev);
	set_bit(WORK_ENABLE, &tp->flags);

	res = usb_submit_urb(tp->intr_urb, GFP_KERNEL);
	if (res) {
		if (res == -ENODEV)
			netif_device_detach(tp->netdev);
		netif_warn(tp, ifup, netdev, "intr_urb submit failed: %d\n",
			   res);
		goto out_unlock;
	}
	napi_enable(&tp->napi);
	tasklet_enable(&tp->tx_tl);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);
#ifdef CONFIG_PM_SLEEP
	tp->pm_notifier.notifier_call = rtl_notifier;
	register_pm_notifier(&tp->pm_notifier);
#endif
	return 0;

out_unlock:
	mutex_unlock(&tp->control);
	usb_autopm_put_interface(tp->intf);
out_free:
	free_all_mem(tp);
out:
	return res;
}

static int rtl8152_close(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	int res = 0;

#ifdef CONFIG_PM_SLEEP
	unregister_pm_notifier(&tp->pm_notifier);
#endif
	tasklet_disable(&tp->tx_tl);
	clear_bit(WORK_ENABLE, &tp->flags);
	usb_kill_urb(tp->intr_urb);
	cancel_delayed_work_sync(&tp->schedule);
	napi_disable(&tp->napi);
	netif_stop_queue(netdev);

	res = usb_autopm_get_interface(tp->intf);
	if (res < 0 || test_bit(RTL8152_UNPLUG, &tp->flags)) {
		rtl_drop_queued_tx(tp);
		rtl_stop_rx(tp);
	} else {
		mutex_lock(&tp->control);

		tp->rtl_ops.down(tp);

		mutex_unlock(&tp->control);

		usb_autopm_put_interface(tp->intf);
	}

	free_all_mem(tp);

	return res;
}

static void rtl_tally_reset(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_RSTTALLY);
	ocp_data |= TALLY_RESET;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RSTTALLY, ocp_data);
}

static void r8152b_init(struct r8152 *tp)
{
	u32 ocp_data;
	u16 data;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	data = r8152_mdio_read(tp, MII_BMCR);
	if (data & BMCR_PDOWN) {
		data &= ~BMCR_PDOWN;
		r8152_mdio_write(tp, MII_BMCR, data);
	}

	r8152_aldps_en(tp, false);

	if (tp->version == RTL_VER_01) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE);
		ocp_data &= ~LED_MODE_MASK;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE, ocp_data);
	}

	r8152_power_cut_en(tp, false);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR);
	ocp_data |= TX_10M_IDLE_EN | PFM_PWM_SWITCH;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR, ocp_data);
	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL);
	ocp_data &= ~MCU_CLK_RATIO_MASK;
	ocp_data |= MCU_CLK_RATIO | D3_CLK_GATED_EN;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL, ocp_data);
	ocp_data = GPHY_STS_MSK | SPEED_DOWN_MSK |
		   SPDWN_RXDV_MSK | SPDWN_LINKCHG_MSK;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_GPHY_INTR_IMR, ocp_data);

	rtl_tally_reset(tp);

	/* enable rx aggregation */
	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_USB_CTRL);
	ocp_data &= ~(RX_AGG_DISABLE | RX_ZERO_EN);
	ocp_write_word(tp, MCU_TYPE_USB, USB_USB_CTRL, ocp_data);
}

static void r8153_init(struct r8152 *tp)
{
	u32 ocp_data;
	u16 data;
	int i;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153_u1u2en(tp, false);

	for (i = 0; i < 500; i++) {
		if (ocp_read_word(tp, MCU_TYPE_PLA, PLA_BOOT_CTRL) &
		    AUTOLOAD_DONE)
			break;

		msleep(20);
		if (test_bit(RTL8152_UNPLUG, &tp->flags))
			break;
	}

	data = r8153_phy_status(tp, 0);

	if (tp->version == RTL_VER_03 || tp->version == RTL_VER_04 ||
	    tp->version == RTL_VER_05)
		ocp_reg_write(tp, OCP_ADC_CFG, CKADSEL_L | ADC_EN | EN_EMI_L);

	data = r8152_mdio_read(tp, MII_BMCR);
	if (data & BMCR_PDOWN) {
		data &= ~BMCR_PDOWN;
		r8152_mdio_write(tp, MII_BMCR, data);
	}

	data = r8153_phy_status(tp, PHY_STAT_LAN_ON);

	r8153_u2p3en(tp, false);

	if (tp->version == RTL_VER_04) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_SSPHYLINK2);
		ocp_data &= ~pwd_dn_scale_mask;
		ocp_data |= pwd_dn_scale(96);
		ocp_write_word(tp, MCU_TYPE_USB, USB_SSPHYLINK2, ocp_data);

		ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_USB2PHY);
		ocp_data |= USB2PHY_L1 | USB2PHY_SUSPEND;
		ocp_write_byte(tp, MCU_TYPE_USB, USB_USB2PHY, ocp_data);
	} else if (tp->version == RTL_VER_05) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_DMY_REG0);
		ocp_data &= ~ECM_ALDPS;
		ocp_write_byte(tp, MCU_TYPE_PLA, PLA_DMY_REG0, ocp_data);

		ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY1);
		if (ocp_read_word(tp, MCU_TYPE_USB, USB_BURST_SIZE) == 0)
			ocp_data &= ~DYNAMIC_BURST;
		else
			ocp_data |= DYNAMIC_BURST;
		ocp_write_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY1, ocp_data);
	} else if (tp->version == RTL_VER_06) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY1);
		if (ocp_read_word(tp, MCU_TYPE_USB, USB_BURST_SIZE) == 0)
			ocp_data &= ~DYNAMIC_BURST;
		else
			ocp_data |= DYNAMIC_BURST;
		ocp_write_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY1, ocp_data);

		r8153_queue_wake(tp, false);

		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS);
		if (rtl8152_get_speed(tp) & LINK_STATUS)
			ocp_data |= CUR_LINK_OK;
		else
			ocp_data &= ~CUR_LINK_OK;
		ocp_data |= POLL_LINK_CHG;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS, ocp_data);
	}

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY2);
	ocp_data |= EP4_FULL_FC;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_CSR_DUMMY2, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_WDT11_CTRL);
	ocp_data &= ~TIMER11_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_WDT11_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE);
	ocp_data &= ~LED_MODE_MASK;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE, ocp_data);

	ocp_data = FIFO_EMPTY_1FB | ROK_EXIT_LPM;
	if (tp->version == RTL_VER_04 && tp->udev->speed < USB_SPEED_SUPER)
		ocp_data |= LPM_TIMER_500MS;
	else
		ocp_data |= LPM_TIMER_500US;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_LPM_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_AFE_CTRL2);
	ocp_data &= ~SEN_VAL_MASK;
	ocp_data |= SEN_VAL_NORMAL | SEL_RXIDLE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_AFE_CTRL2, ocp_data);

	ocp_write_word(tp, MCU_TYPE_USB, USB_CONNECT_TIMER, 0x0001);

	/* MAC clock speed down */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL, 0);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL2, 0);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3, 0);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL4, 0);

	r8153_power_cut_en(tp, false);
	rtl_runtime_suspend_enable(tp, false);
	r8153_u1u2en(tp, true);
	usb_enable_lpm(tp->udev);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6);
	ocp_data |= LANWAKE_CLR_EN;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CONFIG6, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_LWAKE_CTRL_REG);
	ocp_data &= ~LANWAKE_PIN;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_LWAKE_CTRL_REG, ocp_data);

	/* rx aggregation */
	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_USB_CTRL);
	ocp_data &= ~(RX_AGG_DISABLE | RX_ZERO_EN);
	if (test_bit(DELL_TB_RX_AGG_BUG, &tp->flags))
		ocp_data |= RX_AGG_DISABLE;

	ocp_write_word(tp, MCU_TYPE_USB, USB_USB_CTRL, ocp_data);

	rtl_tally_reset(tp);

	switch (tp->udev->speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		tp->coalesce = COALESCE_SUPER;
		break;
	case USB_SPEED_HIGH:
		tp->coalesce = COALESCE_HIGH;
		break;
	default:
		tp->coalesce = COALESCE_SLOW;
		break;
	}
}

static void r8153b_init(struct r8152 *tp)
{
	u32 ocp_data;
	u16 data;
	int i;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153b_u1u2en(tp, false);

	for (i = 0; i < 500; i++) {
		if (ocp_read_word(tp, MCU_TYPE_PLA, PLA_BOOT_CTRL) &
		    AUTOLOAD_DONE)
			break;

		msleep(20);
		if (test_bit(RTL8152_UNPLUG, &tp->flags))
			break;
	}

	data = r8153_phy_status(tp, 0);

	data = r8152_mdio_read(tp, MII_BMCR);
	if (data & BMCR_PDOWN) {
		data &= ~BMCR_PDOWN;
		r8152_mdio_write(tp, MII_BMCR, data);
	}

	data = r8153_phy_status(tp, PHY_STAT_LAN_ON);

	r8153_u2p3en(tp, false);

	/* MSC timer = 0xfff * 8ms = 32760 ms */
	ocp_write_word(tp, MCU_TYPE_USB, USB_MSC_TIMER, 0x0fff);

	/* U1/U2/L1 idle timer. 500 us */
	ocp_write_word(tp, MCU_TYPE_USB, USB_U1U2_TIMER, 500);

	r8153b_power_cut_en(tp, false);
	r8153b_ups_en(tp, false);
	r8153_queue_wake(tp, false);
	rtl_runtime_suspend_enable(tp, false);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS);
	if (rtl8152_get_speed(tp) & LINK_STATUS)
		ocp_data |= CUR_LINK_OK;
	else
		ocp_data &= ~CUR_LINK_OK;
	ocp_data |= POLL_LINK_CHG;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS, ocp_data);

	if (tp->udev->speed != USB_SPEED_HIGH)
		r8153b_u1u2en(tp, true);
	usb_enable_lpm(tp->udev);

	/* MAC clock speed down */
	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL2);
	ocp_data |= MAC_CLK_SPDWN_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL2, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3);
	ocp_data &= ~PLA_MCU_SPDWN_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL3, ocp_data);

	if (tp->version == RTL_VER_09) {
		/* Disable Test IO for 32QFN */
		if (ocp_read_byte(tp, MCU_TYPE_PLA, 0xdc00) & BIT(5)) {
			ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR);
			ocp_data |= TEST_IO_OFF;
			ocp_write_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR, ocp_data);
		}
	}

	set_bit(GREEN_ETHERNET, &tp->flags);

	/* rx aggregation */
	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_USB_CTRL);
	ocp_data &= ~(RX_AGG_DISABLE | RX_ZERO_EN);
	ocp_write_word(tp, MCU_TYPE_USB, USB_USB_CTRL, ocp_data);

	rtl_tally_reset(tp);

	tp->coalesce = 15000;	/* 15 us */
}

static int rtl8152_pre_reset(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);
	struct net_device *netdev;

	if (!tp)
		return 0;

	netdev = tp->netdev;
	if (!netif_running(netdev))
		return 0;

	netif_stop_queue(netdev);
	tasklet_disable(&tp->tx_tl);
	clear_bit(WORK_ENABLE, &tp->flags);
	usb_kill_urb(tp->intr_urb);
	cancel_delayed_work_sync(&tp->schedule);
	napi_disable(&tp->napi);
	if (netif_carrier_ok(netdev)) {
		mutex_lock(&tp->control);
		tp->rtl_ops.disable(tp);
		mutex_unlock(&tp->control);
	}

	return 0;
}

static int rtl8152_post_reset(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);
	struct net_device *netdev;
	struct sockaddr sa;

	if (!tp)
		return 0;

	/* reset the MAC adddress in case of policy change */
	if (determine_ethernet_addr(tp, &sa) >= 0) {
		rtnl_lock();
		dev_set_mac_address (tp->netdev, &sa, NULL);
		rtnl_unlock();
	}

	netdev = tp->netdev;
	if (!netif_running(netdev))
		return 0;

	set_bit(WORK_ENABLE, &tp->flags);
	if (netif_carrier_ok(netdev)) {
		mutex_lock(&tp->control);
		tp->rtl_ops.enable(tp);
		rtl_start_rx(tp);
		_rtl8152_set_rx_mode(netdev);
		mutex_unlock(&tp->control);
	}

	napi_enable(&tp->napi);
	tasklet_enable(&tp->tx_tl);
	netif_wake_queue(netdev);
	usb_submit_urb(tp->intr_urb, GFP_KERNEL);

	if (!list_empty(&tp->rx_done))
		napi_schedule(&tp->napi);

	return 0;
}

static bool delay_autosuspend(struct r8152 *tp)
{
	bool sw_linking = !!netif_carrier_ok(tp->netdev);
	bool hw_linking = !!(rtl8152_get_speed(tp) & LINK_STATUS);

	/* This means a linking change occurs and the driver doesn't detect it,
	 * yet. If the driver has disabled tx/rx and hw is linking on, the
	 * device wouldn't wake up by receiving any packet.
	 */
	if (work_busy(&tp->schedule.work) || sw_linking != hw_linking)
		return true;

	/* If the linking down is occurred by nway, the device may miss the
	 * linking change event. And it wouldn't wake when linking on.
	 */
	if (!sw_linking && tp->rtl_ops.in_nway(tp))
		return true;
	else if (!skb_queue_empty(&tp->tx_queue))
		return true;
	else
		return false;
}

static int rtl8152_runtime_resume(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;

	if (netif_running(netdev) && netdev->flags & IFF_UP) {
		struct napi_struct *napi = &tp->napi;

		tp->rtl_ops.autosuspend_en(tp, false);
		napi_disable(napi);
		set_bit(WORK_ENABLE, &tp->flags);

		if (netif_carrier_ok(netdev)) {
			if (rtl8152_get_speed(tp) & LINK_STATUS) {
				rtl_start_rx(tp);
			} else {
				netif_carrier_off(netdev);
				tp->rtl_ops.disable(tp);
				netif_info(tp, link, netdev, "linking down\n");
			}
		}

		napi_enable(napi);
		clear_bit(SELECTIVE_SUSPEND, &tp->flags);
		smp_mb__after_atomic();

		if (!list_empty(&tp->rx_done))
			napi_schedule(&tp->napi);

		usb_submit_urb(tp->intr_urb, GFP_NOIO);
	} else {
		if (netdev->flags & IFF_UP)
			tp->rtl_ops.autosuspend_en(tp, false);

		clear_bit(SELECTIVE_SUSPEND, &tp->flags);
	}

	return 0;
}

static int rtl8152_system_resume(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;

	netif_device_attach(netdev);

	if (netif_running(netdev) && (netdev->flags & IFF_UP)) {
		tp->rtl_ops.up(tp);
		netif_carrier_off(netdev);
		set_bit(WORK_ENABLE, &tp->flags);
		usb_submit_urb(tp->intr_urb, GFP_NOIO);
	}

	return 0;
}

static int rtl8152_runtime_suspend(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;
	int ret = 0;

	set_bit(SELECTIVE_SUSPEND, &tp->flags);
	smp_mb__after_atomic();

	if (netif_running(netdev) && test_bit(WORK_ENABLE, &tp->flags)) {
		u32 rcr = 0;

		if (netif_carrier_ok(netdev)) {
			u32 ocp_data;

			rcr = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
			ocp_data = rcr & ~RCR_ACPT_ALL;
			ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
			rxdy_gated_en(tp, true);
			ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA,
						 PLA_OOB_CTRL);
			if (!(ocp_data & RXFIFO_EMPTY)) {
				rxdy_gated_en(tp, false);
				ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, rcr);
				clear_bit(SELECTIVE_SUSPEND, &tp->flags);
				smp_mb__after_atomic();
				ret = -EBUSY;
				goto out1;
			}
		}

		clear_bit(WORK_ENABLE, &tp->flags);
		usb_kill_urb(tp->intr_urb);

		tp->rtl_ops.autosuspend_en(tp, true);

		if (netif_carrier_ok(netdev)) {
			struct napi_struct *napi = &tp->napi;

			napi_disable(napi);
			rtl_stop_rx(tp);
			rxdy_gated_en(tp, false);
			ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, rcr);
			napi_enable(napi);
		}

		if (delay_autosuspend(tp)) {
			rtl8152_runtime_resume(tp);
			ret = -EBUSY;
		}
	}

out1:
	return ret;
}

static int rtl8152_system_suspend(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;

	netif_device_detach(netdev);

	if (netif_running(netdev) && test_bit(WORK_ENABLE, &tp->flags)) {
		struct napi_struct *napi = &tp->napi;

		clear_bit(WORK_ENABLE, &tp->flags);
		usb_kill_urb(tp->intr_urb);
		tasklet_disable(&tp->tx_tl);
		napi_disable(napi);
		cancel_delayed_work_sync(&tp->schedule);
		tp->rtl_ops.down(tp);
		napi_enable(napi);
		tasklet_enable(&tp->tx_tl);
	}

	return 0;
}

static int rtl8152_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct r8152 *tp = usb_get_intfdata(intf);
	int ret;

	mutex_lock(&tp->control);

	if (PMSG_IS_AUTO(message))
		ret = rtl8152_runtime_suspend(tp);
	else
		ret = rtl8152_system_suspend(tp);

	mutex_unlock(&tp->control);

	return ret;
}

static int rtl8152_resume(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);
	int ret;

	mutex_lock(&tp->control);

	if (test_bit(SELECTIVE_SUSPEND, &tp->flags))
		ret = rtl8152_runtime_resume(tp);
	else
		ret = rtl8152_system_resume(tp);

	mutex_unlock(&tp->control);

	return ret;
}

static int rtl8152_reset_resume(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);

	clear_bit(SELECTIVE_SUSPEND, &tp->flags);
	tp->rtl_ops.init(tp);
	queue_delayed_work(system_long_wq, &tp->hw_phy_work, 0);
	set_ethernet_addr(tp);
	return rtl8152_resume(intf);
}

static void rtl8152_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct r8152 *tp = netdev_priv(dev);

	if (usb_autopm_get_interface(tp->intf) < 0)
		return;

	if (!rtl_can_wakeup(tp)) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		mutex_lock(&tp->control);
		wol->supported = WAKE_ANY;
		wol->wolopts = __rtl_get_wol(tp);
		mutex_unlock(&tp->control);
	}

	usb_autopm_put_interface(tp->intf);
}

static int rtl8152_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct r8152 *tp = netdev_priv(dev);
	int ret;

	if (!rtl_can_wakeup(tp))
		return -EOPNOTSUPP;

	if (wol->wolopts & ~WAKE_ANY)
		return -EINVAL;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out_set_wol;

	mutex_lock(&tp->control);

	__rtl_set_wol(tp, wol->wolopts);
	tp->saved_wolopts = wol->wolopts & WAKE_ANY;

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out_set_wol:
	return ret;
}

static u32 rtl8152_get_msglevel(struct net_device *dev)
{
	struct r8152 *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void rtl8152_set_msglevel(struct net_device *dev, u32 value)
{
	struct r8152 *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static void rtl8152_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *info)
{
	struct r8152 *tp = netdev_priv(netdev);

	strlcpy(info->driver, MODULENAME, sizeof(info->driver));
	strlcpy(info->version, DRIVER_VERSION, sizeof(info->version));
	usb_make_path(tp->udev, info->bus_info, sizeof(info->bus_info));
	if (!IS_ERR_OR_NULL(tp->rtl_fw.fw))
		strlcpy(info->fw_version, tp->rtl_fw.version,
			sizeof(info->fw_version));
}

static
int rtl8152_get_link_ksettings(struct net_device *netdev,
			       struct ethtool_link_ksettings *cmd)
{
	struct r8152 *tp = netdev_priv(netdev);
	int ret;

	if (!tp->mii.mdio_read)
		return -EOPNOTSUPP;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	mutex_lock(&tp->control);

	mii_ethtool_get_link_ksettings(&tp->mii, cmd);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

static int rtl8152_set_link_ksettings(struct net_device *dev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct r8152 *tp = netdev_priv(dev);
	u32 advertising = 0;
	int ret;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	if (test_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_10_HALF;

	if (test_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_10_FULL;

	if (test_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_100_HALF;

	if (test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_100_FULL;

	if (test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_1000_HALF;

	if (test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
		     cmd->link_modes.advertising))
		advertising |= RTL_ADVERTISED_1000_FULL;

	mutex_lock(&tp->control);

	ret = rtl8152_set_speed(tp, cmd->base.autoneg, cmd->base.speed,
				cmd->base.duplex, advertising);
	if (!ret) {
		tp->autoneg = cmd->base.autoneg;
		tp->speed = cmd->base.speed;
		tp->duplex = cmd->base.duplex;
		tp->advertising = advertising;
	}

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

static const char rtl8152_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"rx_unicast",
	"rx_broadcast",
	"rx_multicast",
	"tx_aborted",
	"tx_underrun",
};

static int rtl8152_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rtl8152_gstrings);
	default:
		return -EOPNOTSUPP;
	}
}

static void rtl8152_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct r8152 *tp = netdev_priv(dev);
	struct tally_counter tally;

	if (usb_autopm_get_interface(tp->intf) < 0)
		return;

	generic_ocp_read(tp, PLA_TALLYCNT, sizeof(tally), &tally, MCU_TYPE_PLA);

	usb_autopm_put_interface(tp->intf);

	data[0] = le64_to_cpu(tally.tx_packets);
	data[1] = le64_to_cpu(tally.rx_packets);
	data[2] = le64_to_cpu(tally.tx_errors);
	data[3] = le32_to_cpu(tally.rx_errors);
	data[4] = le16_to_cpu(tally.rx_missed);
	data[5] = le16_to_cpu(tally.align_errors);
	data[6] = le32_to_cpu(tally.tx_one_collision);
	data[7] = le32_to_cpu(tally.tx_multi_collision);
	data[8] = le64_to_cpu(tally.rx_unicast);
	data[9] = le64_to_cpu(tally.rx_broadcast);
	data[10] = le32_to_cpu(tally.rx_multicast);
	data[11] = le16_to_cpu(tally.tx_aborted);
	data[12] = le16_to_cpu(tally.tx_underrun);
}

static void rtl8152_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *rtl8152_gstrings, sizeof(rtl8152_gstrings));
		break;
	}
}

static int r8152_get_eee(struct r8152 *tp, struct ethtool_eee *eee)
{
	u32 lp, adv, supported = 0;
	u16 val;

	val = r8152_mmd_read(tp, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
	supported = mmd_eee_cap_to_ethtool_sup_t(val);

	val = r8152_mmd_read(tp, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
	adv = mmd_eee_adv_to_ethtool_adv_t(val);

	val = r8152_mmd_read(tp, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
	lp = mmd_eee_adv_to_ethtool_adv_t(val);

	eee->eee_enabled = tp->eee_en;
	eee->eee_active = !!(supported & adv & lp);
	eee->supported = supported;
	eee->advertised = tp->eee_adv;
	eee->lp_advertised = lp;

	return 0;
}

static int r8152_set_eee(struct r8152 *tp, struct ethtool_eee *eee)
{
	u16 val = ethtool_adv_to_mmd_eee_adv_t(eee->advertised);

	tp->eee_en = eee->eee_enabled;
	tp->eee_adv = val;

	rtl_eee_enable(tp, tp->eee_en);

	return 0;
}

static int r8153_get_eee(struct r8152 *tp, struct ethtool_eee *eee)
{
	u32 lp, adv, supported = 0;
	u16 val;

	val = ocp_reg_read(tp, OCP_EEE_ABLE);
	supported = mmd_eee_cap_to_ethtool_sup_t(val);

	val = ocp_reg_read(tp, OCP_EEE_ADV);
	adv = mmd_eee_adv_to_ethtool_adv_t(val);

	val = ocp_reg_read(tp, OCP_EEE_LPABLE);
	lp = mmd_eee_adv_to_ethtool_adv_t(val);

	eee->eee_enabled = tp->eee_en;
	eee->eee_active = !!(supported & adv & lp);
	eee->supported = supported;
	eee->advertised = tp->eee_adv;
	eee->lp_advertised = lp;

	return 0;
}

static int
rtl_ethtool_get_eee(struct net_device *net, struct ethtool_eee *edata)
{
	struct r8152 *tp = netdev_priv(net);
	int ret;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	mutex_lock(&tp->control);

	ret = tp->rtl_ops.eee_get(tp, edata);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

static int
rtl_ethtool_set_eee(struct net_device *net, struct ethtool_eee *edata)
{
	struct r8152 *tp = netdev_priv(net);
	int ret;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	mutex_lock(&tp->control);

	ret = tp->rtl_ops.eee_set(tp, edata);
	if (!ret)
		ret = mii_nway_restart(&tp->mii);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

static int rtl8152_nway_reset(struct net_device *dev)
{
	struct r8152 *tp = netdev_priv(dev);
	int ret;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		goto out;

	mutex_lock(&tp->control);

	ret = mii_nway_restart(&tp->mii);

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

out:
	return ret;
}

static int rtl8152_get_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coalesce)
{
	struct r8152 *tp = netdev_priv(netdev);

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		return -EOPNOTSUPP;
	default:
		break;
	}

	coalesce->rx_coalesce_usecs = tp->coalesce;

	return 0;
}

static int rtl8152_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coalesce)
{
	struct r8152 *tp = netdev_priv(netdev);
	int ret;

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		return -EOPNOTSUPP;
	default:
		break;
	}

	if (coalesce->rx_coalesce_usecs > COALESCE_SLOW)
		return -EINVAL;

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&tp->control);

	if (tp->coalesce != coalesce->rx_coalesce_usecs) {
		tp->coalesce = coalesce->rx_coalesce_usecs;

		if (netif_running(netdev) && netif_carrier_ok(netdev)) {
			netif_stop_queue(netdev);
			napi_disable(&tp->napi);
			tp->rtl_ops.disable(tp);
			tp->rtl_ops.enable(tp);
			rtl_start_rx(tp);
			clear_bit(RTL8152_SET_RX_MODE, &tp->flags);
			_rtl8152_set_rx_mode(netdev);
			napi_enable(&tp->napi);
			netif_wake_queue(netdev);
		}
	}

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

	return ret;
}

static int rtl8152_get_tunable(struct net_device *netdev,
			       const struct ethtool_tunable *tunable, void *d)
{
	struct r8152 *tp = netdev_priv(netdev);

	switch (tunable->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)d = tp->rx_copybreak;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rtl8152_set_tunable(struct net_device *netdev,
			       const struct ethtool_tunable *tunable,
			       const void *d)
{
	struct r8152 *tp = netdev_priv(netdev);
	u32 val;

	switch (tunable->id) {
	case ETHTOOL_RX_COPYBREAK:
		val = *(u32 *)d;
		if (val < ETH_ZLEN) {
			netif_err(tp, rx_err, netdev,
				  "Invalid rx copy break value\n");
			return -EINVAL;
		}

		if (tp->rx_copybreak != val) {
			if (netdev->flags & IFF_UP) {
				mutex_lock(&tp->control);
				napi_disable(&tp->napi);
				tp->rx_copybreak = val;
				napi_enable(&tp->napi);
				mutex_unlock(&tp->control);
			} else {
				tp->rx_copybreak = val;
			}
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void rtl8152_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct r8152 *tp = netdev_priv(netdev);

	ring->rx_max_pending = RTL8152_RX_MAX_PENDING;
	ring->rx_pending = tp->rx_pending;
}

static int rtl8152_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (ring->rx_pending < (RTL8152_MAX_RX * 2))
		return -EINVAL;

	if (tp->rx_pending != ring->rx_pending) {
		if (netdev->flags & IFF_UP) {
			mutex_lock(&tp->control);
			napi_disable(&tp->napi);
			tp->rx_pending = ring->rx_pending;
			napi_enable(&tp->napi);
			mutex_unlock(&tp->control);
		} else {
			tp->rx_pending = ring->rx_pending;
		}
	}

	return 0;
}

static const struct ethtool_ops ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS,
	.get_drvinfo = rtl8152_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.nway_reset = rtl8152_nway_reset,
	.get_msglevel = rtl8152_get_msglevel,
	.set_msglevel = rtl8152_set_msglevel,
	.get_wol = rtl8152_get_wol,
	.set_wol = rtl8152_set_wol,
	.get_strings = rtl8152_get_strings,
	.get_sset_count = rtl8152_get_sset_count,
	.get_ethtool_stats = rtl8152_get_ethtool_stats,
	.get_coalesce = rtl8152_get_coalesce,
	.set_coalesce = rtl8152_set_coalesce,
	.get_eee = rtl_ethtool_get_eee,
	.set_eee = rtl_ethtool_set_eee,
	.get_link_ksettings = rtl8152_get_link_ksettings,
	.set_link_ksettings = rtl8152_set_link_ksettings,
	.get_tunable = rtl8152_get_tunable,
	.set_tunable = rtl8152_set_tunable,
	.get_ringparam = rtl8152_get_ringparam,
	.set_ringparam = rtl8152_set_ringparam,
};

static int rtl8152_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(rq);
	int res;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	res = usb_autopm_get_interface(tp->intf);
	if (res < 0)
		goto out;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = R8152_PHY_ID; /* Internal PHY */
		break;

	case SIOCGMIIREG:
		mutex_lock(&tp->control);
		data->val_out = r8152_mdio_read(tp, data->reg_num);
		mutex_unlock(&tp->control);
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		mutex_lock(&tp->control);
		r8152_mdio_write(tp, data->reg_num, data->val_in);
		mutex_unlock(&tp->control);
		break;

	default:
		res = -EOPNOTSUPP;
	}

	usb_autopm_put_interface(tp->intf);

out:
	return res;
}

static int rtl8152_change_mtu(struct net_device *dev, int new_mtu)
{
	struct r8152 *tp = netdev_priv(dev);
	int ret;

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		dev->mtu = new_mtu;
		return 0;
	default:
		break;
	}

	ret = usb_autopm_get_interface(tp->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&tp->control);

	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		u32 rms = new_mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

		ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, rms);

		if (netif_carrier_ok(dev))
			r8153_set_rx_early_size(tp);
	}

	mutex_unlock(&tp->control);

	usb_autopm_put_interface(tp->intf);

	return ret;
}

static const struct net_device_ops rtl8152_netdev_ops = {
	.ndo_open		= rtl8152_open,
	.ndo_stop		= rtl8152_close,
	.ndo_do_ioctl		= rtl8152_ioctl,
	.ndo_start_xmit		= rtl8152_start_xmit,
	.ndo_tx_timeout		= rtl8152_tx_timeout,
	.ndo_set_features	= rtl8152_set_features,
	.ndo_set_rx_mode	= rtl8152_set_rx_mode,
	.ndo_set_mac_address	= rtl8152_set_mac_address,
	.ndo_change_mtu		= rtl8152_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_features_check	= rtl8152_features_check,
};

static void rtl8152_unload(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	if (tp->version != RTL_VER_01)
		r8152_power_cut_en(tp, true);
}

static void rtl8153_unload(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153_power_cut_en(tp, false);
}

static void rtl8153b_unload(struct r8152 *tp)
{
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;

	r8153b_power_cut_en(tp, false);
}

static int rtl_ops_init(struct r8152 *tp)
{
	struct rtl_ops *ops = &tp->rtl_ops;
	int ret = 0;

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		ops->init		= r8152b_init;
		ops->enable		= rtl8152_enable;
		ops->disable		= rtl8152_disable;
		ops->up			= rtl8152_up;
		ops->down		= rtl8152_down;
		ops->unload		= rtl8152_unload;
		ops->eee_get		= r8152_get_eee;
		ops->eee_set		= r8152_set_eee;
		ops->in_nway		= rtl8152_in_nway;
		ops->hw_phy_cfg		= r8152b_hw_phy_cfg;
		ops->autosuspend_en	= rtl_runtime_suspend_enable;
		tp->rx_buf_sz		= 16 * 1024;
		tp->eee_en		= true;
		tp->eee_adv		= MDIO_EEE_100TX;
		break;

	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ops->init		= r8153_init;
		ops->enable		= rtl8153_enable;
		ops->disable		= rtl8153_disable;
		ops->up			= rtl8153_up;
		ops->down		= rtl8153_down;
		ops->unload		= rtl8153_unload;
		ops->eee_get		= r8153_get_eee;
		ops->eee_set		= r8152_set_eee;
		ops->in_nway		= rtl8153_in_nway;
		ops->hw_phy_cfg		= r8153_hw_phy_cfg;
		ops->autosuspend_en	= rtl8153_runtime_enable;
		if (tp->udev->speed < USB_SPEED_SUPER)
			tp->rx_buf_sz	= 16 * 1024;
		else
			tp->rx_buf_sz	= 32 * 1024;
		tp->eee_en		= true;
		tp->eee_adv		= MDIO_EEE_1000T | MDIO_EEE_100TX;
		break;

	case RTL_VER_08:
	case RTL_VER_09:
		ops->init		= r8153b_init;
		ops->enable		= rtl8153_enable;
		ops->disable		= rtl8153_disable;
		ops->up			= rtl8153b_up;
		ops->down		= rtl8153b_down;
		ops->unload		= rtl8153b_unload;
		ops->eee_get		= r8153_get_eee;
		ops->eee_set		= r8152_set_eee;
		ops->in_nway		= rtl8153_in_nway;
		ops->hw_phy_cfg		= r8153b_hw_phy_cfg;
		ops->autosuspend_en	= rtl8153b_runtime_enable;
		tp->rx_buf_sz		= 32 * 1024;
		tp->eee_en		= true;
		tp->eee_adv		= MDIO_EEE_1000T | MDIO_EEE_100TX;
		break;

	default:
		ret = -ENODEV;
		netif_err(tp, probe, tp->netdev, "Unknown Device\n");
		break;
	}

	return ret;
}

#define FIRMWARE_8153A_2	"rtl_nic/rtl8153a-2.fw"
#define FIRMWARE_8153A_3	"rtl_nic/rtl8153a-3.fw"
#define FIRMWARE_8153A_4	"rtl_nic/rtl8153a-4.fw"
#define FIRMWARE_8153B_2	"rtl_nic/rtl8153b-2.fw"

MODULE_FIRMWARE(FIRMWARE_8153A_2);
MODULE_FIRMWARE(FIRMWARE_8153A_3);
MODULE_FIRMWARE(FIRMWARE_8153A_4);
MODULE_FIRMWARE(FIRMWARE_8153B_2);

static int rtl_fw_init(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;

	switch (tp->version) {
	case RTL_VER_04:
		rtl_fw->fw_name		= FIRMWARE_8153A_2;
		rtl_fw->pre_fw		= r8153_pre_firmware_1;
		rtl_fw->post_fw		= r8153_post_firmware_1;
		break;
	case RTL_VER_05:
		rtl_fw->fw_name		= FIRMWARE_8153A_3;
		rtl_fw->pre_fw		= r8153_pre_firmware_2;
		rtl_fw->post_fw		= r8153_post_firmware_2;
		break;
	case RTL_VER_06:
		rtl_fw->fw_name		= FIRMWARE_8153A_4;
		rtl_fw->post_fw		= r8153_post_firmware_3;
		break;
	case RTL_VER_09:
		rtl_fw->fw_name		= FIRMWARE_8153B_2;
		rtl_fw->pre_fw		= r8153b_pre_firmware_1;
		rtl_fw->post_fw		= r8153b_post_firmware_1;
		break;
	default:
		break;
	}

	return 0;
}

static u8 rtl_get_version(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	u32 ocp_data = 0;
	__le32 *tmp;
	u8 version;
	int ret;

	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return 0;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      RTL8152_REQ_GET_REGS, RTL8152_REQT_READ,
			      PLA_TCR0, MCU_TYPE_PLA, tmp, sizeof(*tmp), 500);
	if (ret > 0)
		ocp_data = (__le32_to_cpu(*tmp) >> 16) & VERSION_MASK;

	kfree(tmp);

	switch (ocp_data) {
	case 0x4c00:
		version = RTL_VER_01;
		break;
	case 0x4c10:
		version = RTL_VER_02;
		break;
	case 0x5c00:
		version = RTL_VER_03;
		break;
	case 0x5c10:
		version = RTL_VER_04;
		break;
	case 0x5c20:
		version = RTL_VER_05;
		break;
	case 0x5c30:
		version = RTL_VER_06;
		break;
	case 0x4800:
		version = RTL_VER_07;
		break;
	case 0x6000:
		version = RTL_VER_08;
		break;
	case 0x6010:
		version = RTL_VER_09;
		break;
	default:
		version = RTL_VER_UNKNOWN;
		dev_info(&intf->dev, "Unknown version 0x%04x\n", ocp_data);
		break;
	}

	dev_dbg(&intf->dev, "Detected version 0x%04x\n", version);

	return version;
}

static int rtl8152_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	u8 version = rtl_get_version(intf);
	struct r8152 *tp;
	struct net_device *netdev;
	int ret;

	if (version == RTL_VER_UNKNOWN)
		return -ENODEV;

	if (udev->actconfig->desc.bConfigurationValue != 1) {
		usb_driver_set_configuration(udev, 1);
		return -ENODEV;
	}

	if (intf->cur_altsetting->desc.bNumEndpoints < 3)
		return -ENODEV;

	usb_reset_device(udev);
	netdev = alloc_etherdev(sizeof(struct r8152));
	if (!netdev) {
		dev_err(&intf->dev, "Out of memory\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(netdev, &intf->dev);
	tp = netdev_priv(netdev);
	tp->msg_enable = 0x7FFF;

	tp->udev = udev;
	tp->netdev = netdev;
	tp->intf = intf;
	tp->version = version;

	switch (version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		tp->mii.supports_gmii = 0;
		break;
	default:
		tp->mii.supports_gmii = 1;
		break;
	}

	ret = rtl_ops_init(tp);
	if (ret)
		goto out;

	rtl_fw_init(tp);

	mutex_init(&tp->control);
	INIT_DELAYED_WORK(&tp->schedule, rtl_work_func_t);
	INIT_DELAYED_WORK(&tp->hw_phy_work, rtl_hw_phy_work_func_t);
	tasklet_init(&tp->tx_tl, bottom_half, (unsigned long)tp);
	tasklet_disable(&tp->tx_tl);

	netdev->netdev_ops = &rtl8152_netdev_ops;
	netdev->watchdog_timeo = RTL8152_TX_TIMEOUT;

	netdev->features |= NETIF_F_RXCSUM | NETIF_F_IP_CSUM | NETIF_F_SG |
			    NETIF_F_TSO | NETIF_F_FRAGLIST | NETIF_F_IPV6_CSUM |
			    NETIF_F_TSO6 | NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_features = NETIF_F_RXCSUM | NETIF_F_IP_CSUM | NETIF_F_SG |
			      NETIF_F_TSO | NETIF_F_FRAGLIST |
			      NETIF_F_IPV6_CSUM | NETIF_F_TSO6 |
			      NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX;
	netdev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
				NETIF_F_HIGHDMA | NETIF_F_FRAGLIST |
				NETIF_F_IPV6_CSUM | NETIF_F_TSO6;

	if (tp->version == RTL_VER_01) {
		netdev->features &= ~NETIF_F_RXCSUM;
		netdev->hw_features &= ~NETIF_F_RXCSUM;
	}

	if (le16_to_cpu(udev->descriptor.idVendor) == VENDOR_ID_LENOVO) {
		switch (le16_to_cpu(udev->descriptor.idProduct)) {
		case DEVICE_ID_THINKPAD_THUNDERBOLT3_DOCK_GEN2:
		case DEVICE_ID_THINKPAD_USB_C_DOCK_GEN2:
			set_bit(LENOVO_MACPASSTHRU, &tp->flags);
		}
	}

	if (le16_to_cpu(udev->descriptor.bcdDevice) == 0x3011 && udev->serial &&
	    (!strcmp(udev->serial, "000001000000") ||
	     !strcmp(udev->serial, "000002000000"))) {
		dev_info(&udev->dev, "Dell TB16 Dock, disable RX aggregation");
		set_bit(DELL_TB_RX_AGG_BUG, &tp->flags);
	}

	netdev->ethtool_ops = &ops;
	netif_set_gso_max_size(netdev, RTL_LIMITED_TSO_SIZE);

	/* MTU range: 68 - 1500 or 9194 */
	netdev->min_mtu = ETH_MIN_MTU;
	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
		netdev->max_mtu = ETH_DATA_LEN;
		break;
	default:
		netdev->max_mtu = RTL8153_MAX_MTU;
		break;
	}

	tp->mii.dev = netdev;
	tp->mii.mdio_read = read_mii_word;
	tp->mii.mdio_write = write_mii_word;
	tp->mii.phy_id_mask = 0x3f;
	tp->mii.reg_num_mask = 0x1f;
	tp->mii.phy_id = R8152_PHY_ID;

	tp->autoneg = AUTONEG_ENABLE;
	tp->speed = SPEED_100;
	tp->advertising = RTL_ADVERTISED_10_HALF | RTL_ADVERTISED_10_FULL |
			  RTL_ADVERTISED_100_HALF | RTL_ADVERTISED_100_FULL;
	if (tp->mii.supports_gmii) {
		tp->speed = SPEED_1000;
		tp->advertising |= RTL_ADVERTISED_1000_FULL;
	}
	tp->duplex = DUPLEX_FULL;

	tp->rx_copybreak = RTL8152_RXFG_HEADSZ;
	tp->rx_pending = 10 * RTL8152_MAX_RX;

	intf->needs_remote_wakeup = 1;

	if (!rtl_can_wakeup(tp))
		__rtl_set_wol(tp, 0);
	else
		tp->saved_wolopts = __rtl_get_wol(tp);

	tp->rtl_ops.init(tp);
#if IS_BUILTIN(CONFIG_USB_RTL8152)
	/* Retry in case request_firmware() is not ready yet. */
	tp->rtl_fw.retry = true;
#endif
	queue_delayed_work(system_long_wq, &tp->hw_phy_work, 0);
	set_ethernet_addr(tp);

	usb_set_intfdata(intf, tp);
	netif_napi_add(netdev, &tp->napi, r8152_poll, RTL8152_NAPI_WEIGHT);

	ret = register_netdev(netdev);
	if (ret != 0) {
		netif_err(tp, probe, netdev, "couldn't register the device\n");
		goto out1;
	}

	if (tp->saved_wolopts)
		device_set_wakeup_enable(&udev->dev, true);
	else
		device_set_wakeup_enable(&udev->dev, false);

	netif_info(tp, probe, netdev, "%s\n", DRIVER_VERSION);

	return 0;

out1:
	tasklet_kill(&tp->tx_tl);
	usb_set_intfdata(intf, NULL);
out:
	free_netdev(netdev);
	return ret;
}

static void rtl8152_disconnect(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (tp) {
		rtl_set_unplug(tp);

		unregister_netdev(tp->netdev);
		tasklet_kill(&tp->tx_tl);
		cancel_delayed_work_sync(&tp->hw_phy_work);
		tp->rtl_ops.unload(tp);
		rtl8152_release_firmware(tp);
		free_netdev(tp->netdev);
	}
}

#define REALTEK_USB_DEVICE(vend, prod)	\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE | \
		       USB_DEVICE_ID_MATCH_INT_CLASS, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC \
}, \
{ \
	.match_flags = USB_DEVICE_ID_MATCH_INT_INFO | \
		       USB_DEVICE_ID_MATCH_DEVICE, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bInterfaceClass = USB_CLASS_COMM, \
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ETHERNET, \
	.bInterfaceProtocol = USB_CDC_PROTO_NONE

/* table of devices that work with this driver */
static const struct usb_device_id rtl8152_table[] = {
	{REALTEK_USB_DEVICE(VENDOR_ID_REALTEK, 0x8050)},
	{REALTEK_USB_DEVICE(VENDOR_ID_REALTEK, 0x8152)},
	{REALTEK_USB_DEVICE(VENDOR_ID_REALTEK, 0x8153)},
	{REALTEK_USB_DEVICE(VENDOR_ID_MICROSOFT, 0x07ab)},
	{REALTEK_USB_DEVICE(VENDOR_ID_MICROSOFT, 0x07c6)},
	{REALTEK_USB_DEVICE(VENDOR_ID_MICROSOFT, 0x0927)},
	{REALTEK_USB_DEVICE(VENDOR_ID_SAMSUNG, 0xa101)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x304f)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x3062)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x3069)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x3082)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x7205)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x720c)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x7214)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0x721e)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LENOVO,  0xa387)},
	{REALTEK_USB_DEVICE(VENDOR_ID_LINKSYS, 0x0041)},
	{REALTEK_USB_DEVICE(VENDOR_ID_NVIDIA,  0x09ff)},
	{REALTEK_USB_DEVICE(VENDOR_ID_TPLINK,  0x0601)},
	{}
};

MODULE_DEVICE_TABLE(usb, rtl8152_table);

static struct usb_driver rtl8152_driver = {
	.name =		MODULENAME,
	.id_table =	rtl8152_table,
	.probe =	rtl8152_probe,
	.disconnect =	rtl8152_disconnect,
	.suspend =	rtl8152_suspend,
	.resume =	rtl8152_resume,
	.reset_resume =	rtl8152_reset_resume,
	.pre_reset =	rtl8152_pre_reset,
	.post_reset =	rtl8152_post_reset,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(rtl8152_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
