/*
 * TC956X ethernet driver.
 *
 * tc956xmac.h
 *
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 * Copyright (C) 2023 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : 1. Version update
 *		2. Default Port1 interface selected as SGMII
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. Version update
 *		2. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  23 Jul 2021 : 1. Add support for contiguous allocation of memory
 *  VERSION     : 01-00-06
 *  29 Jul 2021 : 1. Add support to set MAC Address register
 *  VERSION     : 01-00-07
 *  05 Aug 2021 : 1. Store and use Port0 pci_dev for all DMA allocation/mapping for IPA path
 *		: 2. Register Port0 as only PCIe device, incase its PHY is not found
 *  VERSION     : 01-00-08
 *  16 Aug 2021 : 1. PHY interrupt mode supported through .config_intr and .ack_interrupt API
 *  VERSION     : 01-00-09
 *  24 Aug 2021 : 1. Disable TC956X_PCIE_GEN3_SETTING and TC956X_LOAD_FW_HEADER macros and provide support via Makefile
 *		: 2. Platform API supported
 *  VERSION     : 01-00-10
 *  02 Sep 2021 : 1. Configuration of Link state L0 and L1 transaction delay for PCIe switch ports & Endpoint.
 *  VERSION     : 01-00-11
 *  09 Sep 2021 : Reverted changes related to usage of Port-0 pci_dev for all DMA allocation/mapping for IPA path
 *  VERSION     : 01-00-12
 *  14 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-13
 *  23 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-14
 *  29 Sep 2021 : 1. Version update
 *  VERSION     : 01-00-15
 *  14 Oct 2021 : 1. Version update
 *  VERSION     : 01-00-16
 *  19 Oct 2021 : 1. Adding M3 SRAM Debug counters to ethtool statistics
 *		: 2. Adding MTL RX Overflow/packet miss count, TX underflow counts,Rx Watchdog value to ethtool statistics.
 *		: 3. Version update
 *  VERSION     : 01-00-17
 *  21 Oct 2021 : 1. Added support for GPIO configuration API
 *		: 2. Version update 
 *  VERSION     : 01-00-18
 *  26 Oct 2021 : 1. Updated Driver Module Version.
		: 2. Added variable for port-wise suspend status.
		: 3. Added macro to control EEE MAC Control.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-20
 *  08 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-21
 *  24 Nov 2021 : 1. Version update
 		  2. Private member used instead of global for wol interrupt indication
 *  VERSION     : 01-00-22
 *  24 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-23
 *  24 Nov 2021 : 1. EEE macro enabled by default.
 		  2. Module param support for EEE configuration
		  3. Version update
 *  VERSION     : 01-00-24
 *  30 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-25
 *  30 Nov 2021 : 1. Version update
 *  VERSION     : 01-00-26
 *  01 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-27
 *  01 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-28
 *  03 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-29
 *  08 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-30
 *  10 Dec 2021 : 1. Version update
 *  VERSION     : 01-00-31
 *  27 Dec 2021 : 1. Support for eMAC Reset and unused clock disable during Suspend and restoring it back during resume.
		  2. Version update.
 *  VERSION     : 01-00-32
 *  06 Jan 2022 : 1. Version update
 *  VERSION     : 01-00-33
 *  07 Jan 2022 : 1. Version update
 *  VERSION     : 01-00-34
 *  11 Jan 2022 : 1. Version update
 *  VERSION     : 01-00-35
 *  18 Jan 2022 : 1. IRQ device name change
 *		  2. Version update
 *  VERSION     : 01-00-36
 *  20 Jan 2022 : 1. Version update
 *  VERSION     : 01-00-37
 *  24 Jan 2022 : 1. Version update
 *  VERSION     : 01-00-38
 *  31 Jan 2022 : 1. Debug dump API and structures added to dump registers during crash.
 *		  2. Version update.
 *  VERSION     : 01-00-39
 *  02 Feb 2022 : 1. Version update
 *  VERSION     : 01-00-40
 *  04 Feb 2022 : 1. Version update
 *  VERSION     : 01-00-41
 *  14 Feb 2022 : 1. Reset assert and clock disable support during Link Down.
 *		  2. Version update.
 *  VERSION     : 01-00-42
 *  22 Feb 2022 : 1. Supported GPIO configuration save and restoration
 *		  2. Version update.
 *  VERSION     : 01-00-43
 *  25 Feb 2022 : 1. Version update.
 *  VERSION     : 01-00-44
 *  09 Mar 2022 : 1. Version update 
 *  VERSION     : 01-00-45
 *  22 Mar 2022 : 1. Version update 
 *  VERSION     : 01-00-46
 *  05 Apr 2022 : 1. Version update 
 *  VERSION     : 01-00-47
 *  06 Apr 2022 : 1. Version update 
 *  VERSION     : 01-00-48
 *  14 Apr 2022 : 1. Version update 
 *  VERSION     : 01-00-49
 *  25 Apr 2022 : 1. Version update 
 *  VERSION     : 01-00-50
 *  29 Apr 2022 : 1. Added variable for tracking port release status and Lock for syncing linkdown, port rlease and release of offloaded DMA channels
 *		  2. Version update.
 *  VERSION     : 01-00-51
 *  15 Jun 2022 : 1. Added debugfs support for module specific register dump
 *		  2. Version update.
 *  VERSION     : 01-00-52
 *  08 Aug 2022 : 1. Version update 
 *  VERSION     : 01-00-53
 *  31 Aug 2022 : 1. Added Fix for configuring Rx Parser when EEE is enabled and RGMII Interface is used
 *		  2. Version update.
 *  VERSION     : 01-00-54
 *  02 Sep 2022 : 1. 2500Base-X support for line speeds 2.5Gbps, 1Gbps, 100Mbps.
 *		  2. Version update
 *  VERSION     : 01-00-55
 *  21 Oct 2022 : 1. Version update 
 *  VERSION     : 01-00-56
 *  09 Nov 2022 : 1. Version update 
 *  VERSION     : 01-00-57
 *  22 Dec 2022 : 1. Support for SW reset during link down.
 *                2. Version update
 *  VERSION     : 01-00-58
 *  09 May 2023 : 1. Version update 
 *  VERSION     : 01-00-59
 *  10 Nov 2023 : 1. DSP Cascade related modifications
 *                2. Kernel 6.1 Porting changes
 *                3. Version update
 *  VERSION     : 01-02-59
 *
 *  26 Dec 2023 : 1. Kernel 6.6 Porting changes
 *                2. Added the support for TC commands taprio and flower
 *                3. Version update
 *  VERSION     : 01-03-59
 */

#ifndef __TC956XMAC_H__
#define __TC956XMAC_H__


#include <linux/clk.h>
#include <linux/if_vlan.h>
#include "tc956xmac_inc.h"
#ifndef TC956X_SRIOV_VF
#include <linux/phylink.h>
#endif
#include <linux/pci.h>
#include "common.h"
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/reset.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
#include <net/page_pool.h>
#else
#include <net/page_pool/helpers.h>
#endif
#ifdef TC956X_SRIOV_PF
#include "tc956x_pf_rsc_mng.h"
#endif
#ifndef TC956X_SRIOV_VF
//#define TC956X_LOAD_FW_HEADER
#endif
#define PF_DRIVER 4

/* Uncomment EEE_MAC_CONTROLLED_MODE macro for MAC controlled EEE Mode & comment for PHY controlled EEE mode */
#define EEE_MAC_CONTROLLED_MODE
/* Uncomment TC956X_5_G_2_5_G_EEE_SUPPORT macro for enabling EEE support for 5G and 2.5G */
#define TC956X_5_G_2_5_G_EEE_SUPPORT
// #define CONFIG_TC956XMAC_SELFTESTS  /*Enable this macro to test Feature selftest*/

#ifdef TC956X
#define VENDOR_ID 0x1179
#ifndef TC956X_SRIOV_VF
#define DEVICE_ID 0x0220	/* PF - 0x0220, VF - 0x0221 */
#elif (defined TC956X_SRIOV_VF)
#define DEVICE_ID 0x0221	/* PF - 0x0220, VF - 0x0221 */
#endif
#endif

#define SUB_SYS_VENDOR_ID 0x1179
#define SUB_SYS_DEVICE_ID 0x0001

#define PCI_ETHC_CLASS_CODE	0x020000
#define PCI_ETHC_CLASS_MASK	0xFFFFFF

#define MOD_PARAM_ACCESS 0444

#define TC956X_PCI_DMA_WIDTH 64

#define TC956X_TX_QUEUES 8
#define TC956X_RX_QUEUES 8

#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME "TC956X_Firmware_PCIeBridge.bin"
#endif

#define TC956X_TOTAL_VFS 3

#ifdef TC956X
#ifndef TC956X_SRIOV_VF

#define TC956X_RESOURCE_NAME	"tc956x_pci-eth"
#define IRQ_DEV_NAME(x)		(((x) == RM_PF0_ID) ? ("eth0") : ("eth1"))
#define WOL_IRQ_DEV_NAME(x)	(((x) == RM_PF0_ID) ? ("eth0_wol") : ("eth1_wol"))

#define DRV_MODULE_VERSION	"V_01-03-59"
#define TC956X_FW_MAX_SIZE	(64*1024)
#elif (defined TC956X_SRIOV_VF)
#define TC956X_RESOURCE_NAME	"tc956x_vf_pci-eth"
#define DRV_MODULE_VERSION	"V_01-01-59"
#endif
#define ATR_AXI4_SLV_BASE		0x0800
#define ATR_AXI4_SLAVE_OFFSET		0x0100
#define ATR_AXI4_TABLE_OFFSET		0x20
#define TC956X_AXI4_SLV(ch, tid)	(ATR_AXI4_SLV_BASE +\
					(ch * ATR_AXI4_SLAVE_OFFSET) +\
					(tid * ATR_AXI4_TABLE_OFFSET))

#define SRC_ADDR_LO_OFFSET		0x00
#define SRC_ADDR_HI_OFFSET		0x04
#define TRSL_ADDR_LO_OFFSET		0x08
#define TRSL_ADDR_HI_OFFSET		0x0C
#define TRSL_PARAM_OFFSET		0x10
#define TRSL_MASK_OFFSET1		0x18
#define TRSL_MASK_OFFSET2		0x1C
#define TC956X_AXI4_SLV_SRC_ADDR_LO(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							SRC_ADDR_LO_OFFSET)
#define TC956X_AXI4_SLV_SRC_ADDR_HI(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							SRC_ADDR_HI_OFFSET)
#define TC956X_AXI4_SLV_TRSL_ADDR_LO(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_ADDR_LO_OFFSET)
#define TC956X_AXI4_SLV_TRSL_ADDR_HI(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_ADDR_HI_OFFSET)
#define TC956X_AXI4_SLV_TRSL_PARAM(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_PARAM_OFFSET)
#define TC956X_AXI4_SLV_TRSL_MASK1(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_MASK_OFFSET1)
#define TC956X_AXI4_SLV_TRSL_MASK2(ch, tid)	(TC956X_AXI4_SLV(ch, tid) +\
							TRSL_MASK_OFFSET2)

#define TC956X_ATR_IMPL 1U
#define TC956X_ATR_SIZE(size) ((size - 1U) << 1U)
#define TC956X_ATR_SIZE_MASK		GENMASK(6, 1)
#define TC956x_ATR_SIZE_SHIFT		1
#define TC956X_SRC_LO_MASK		GENMASK(31, 12)
#define TC956X_SRC_LO_SHIFT		12

#define TC956X_AXI4_SLV00_ATR_SIZE 36U
#define TC956X_AXI4_SLV00_SRC_ADDR_LO_VAL  (0x00000000U)
#define TC956X_AXI4_SLV00_SRC_ADDR_HI_VAL  (0x00000010U)
#define TC956X_AXI4_SLV00_TRSL_ADDR_LO_VAL (0x00000000U)
#define TC956X_AXI4_SLV00_TRSL_ADDR_HI_VAL (0x00000000U)
#define TC956X_AXI4_SLV00_TRSL_PARAM_VAL   (0x00000000U)
#define TC956X_AXI4_SLV00_SRC_ADDR_LO_VAL_DEFAULT  (0x0000007FU)

#ifdef TC956X_DMA_OFFLOAD_ENABLE
#define TC956X_AXI4_SLV01_ATR_SIZE	   28U	  /* 28 bit DMA Mask */
#define TC956X_AXI4_SLV01_SRC_ADDR_LO_VAL  (0x60000000U)
#define TC956X_AXI4_SLV01_SRC_ADDR_HI_VAL  (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_ADDR_LO_VAL (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_ADDR_HI_VAL (0x00000000U)
#define TC956X_AXI4_SLV01_TRSL_PARAM_VAL   (0x00000000U)
#endif

#define TC956X_M3_SRAM_FW_VER_OFFSET 0x4F900 /* DMEM addrs 0x2000F900 */
/* M3 Debug Counters in SRAM*/
#define TC956X_M3_SRAM_DEBUG_CNTS_OFFSET	0x4F800 /* DMEM addrs 0x2000F800 */

#define DB_CNT_LEN	4	/* Size of each debug counter in bytes */
#define DB_CNT0		0	/* reserved0 */
#define DB_CNT1		1	/* reserved1 */
#define DB_CNT2		2	/* reserved2 */
#define DB_CNT3		3	/* reserved3 */
#define DB_CNT4		4	/* reserved4 */
#define DB_CNT5		5	/* reserved5 */
#define DB_CNT6		6	/* reserved6 */
#define DB_CNT7		7	/* reserved7 */
#define DB_CNT8		8	/* reserved8 */
#define DB_CNT9		9	/* reserved9 */
#define DB_CNT10	10	/* reserved10 */
#define DB_CNT11	11	/* m3 watchdog expiry count*/
#define DB_CNT12	12	/* m3 watchdog monitor value */
#define DB_CNT13	13	/* reserved13 */
#define DB_CNT14	14	/* reserved14 */
#define DB_CNT15	15	/* m3 systick counter lower 32bits  */
#define DB_CNT16	16	/* m3 systick counter upper 32bits */
#define DB_CNT17	17	/* m3 transmission timeout indication for port0 */
#define DB_CNT18	18	/* m3 transmission timeout indication for port1 */
#define DB_CNT19	19	/* reserved19 */

#define SIXTEEN						16
#define TWENTY						20
#define TWENTY_FOUR					24
#define SIXTY_FOUR					64
#define ONE_TWENTY_EIGHT			128
#define TWO_FIFTY_SIX				256
#define FIVE_HUNDRED_TWELVE			512
#define THOUSAND_TWENTY_FOUR		1024

#define SPD_DIV_10G					10000000
#define SPD_DIV_5G					5000000
#define SPD_DIV_2_5G				2500000
#define SPD_DIV_1G					1000000
#define SPD_DIV_100M				100000

#define NRSTCTRL0_RST_ASRT 0x1
#define NRSTCTRL0_RST_DE_ASRT 0x3

#define TC956X_OFFSET_TAMAP 0x00000010
#define TC956X_MASK_TAMAP 0xFFFFF000
#define TC956X_SHIFT_TAMAP 32
#define TC956X_OFFSET_OW 28
#define TC956X_OFFSET_OW_MAX 53
#define TC956X_HEX_ZERO 0x00000000

#define TC956X_BAR0 0
#define TC956X_BAR2 2
#define TC956X_BAR4 4

#define NMODESTS 0x0004
#define NMODESTS_MODE 0x200
#define NMODESTS_MODE2		0x400
#define NMODESTS_MODE2_SHIFT	10
#define TC956X_PCIE_SETTING_A	0 /* x4x1x1 mode */
#define TC956X_PCIE_SETTING_B	1 /* x2x2x1 mode */

#define TC9563_CFG_NEMACTXCDLY		0x1050U
#define TC9563_CFG_NEMACIOCTL		0x107CU

#define NEMACTXCDLY_DEFAULT		0x00000000U
#define NEMACIOCTL_DEFAULT		0xF300F300
/* Systick count SRAM  address  DMEM addrs 0x2000F83C, Check this value for any change */
#define SYSTCIK_SRAM_OFFSET		0x4F83C

/* Tx Timer count SRAM  address  DMEM addrs 0x2000F844, Check this value for any change */
#define TX_TIMER_SRAM_OFFSET_0		0x4F844

/* Tx Timer count SRAM  address  DMEM addrs 0x2000F848, Check this value for any change */
#define TX_TIMER_SRAM_OFFSET_1		0x4F848

#define TX_TIMER_SRAM_OFFSET(t) (((t) == RM_PF0_ID) ? (TX_TIMER_SRAM_OFFSET_0) : (TX_TIMER_SRAM_OFFSET_1))

#define TC956X_M3_SRAM_EEPROM_MAC_ADDR		0x47000		/* DMEM addrs 0x20007000U */
#define TC956X_M3_SRAM_EEPROM_OFFSET_ADDR	0x47050		/* DMEM addrs 0x20007050U */
#define TC956X_M3_SRAM_EEPROM_MAC_COUNT		0x47051		/* DMEM addrs 0x20007051U */
#define TC956X_M3_INIT_DONE					0x47054		/* DMEM addrs 0x20007054U */
#define TC956X_M3_FW_EXIT					0x47058		/* DMEM addrs 0x20007058U */

#define TC956X_M3_DBG_VER_START			0x4F900

#define ENABLE_USXGMII_INTERFACE	0
#define ENABLE_XFI_INTERFACE		1 /* XFI/SFI, this is same as USXGMII, except XPCS autoneg disabled */
#define ENABLE_RGMII_INTERFACE		2
#define ENABLE_SGMII_INTERFACE		3
#define ENABLE_2500BASE_X_INTERFACE	4
#define MTL_FPE_AFSZ_64		0
#define MTL_FPE_AFSZ_128	1
#define MTL_FPE_AFSZ_192	2
#define MTL_FPE_AFSZ_256	3
#ifdef TC956X_SRIOV_PF
#define TC956X_DISABLE_CHNL 0
#define TC956X_ENABLE_CHNL 1

#define TC956X_DISABLE_QUEUE 0
#define TC956X_ENABLE_QUEUE 1
#endif

#define MMC_XGMAC_TX_FPE_FRAG			0x208
#define MMC_XGMAC_RX_PKT_ASSEMBLY_OK	0x230
#define MMC_XGMAC_RX_FPE_FRAG			0x234

#endif

#define MAX_CM3_TAMAP_ENTRIES		3
#define CM3_TAMAP_ATR_SIZE		28 /* ATR Size = 2 ^ (28 + 1) = 512MB */
#define CM3_TAMAP_SIZE			(1 << (CM3_TAMAP_ATR_SIZE + 1))
#define CM3_TAMAP_MASK			(CM3_TAMAP_SIZE - 1)
#define CM3_TAMAP_SRC_ADDR_START	0x60000000

#define	TC956XMAC_ALIGN(x)		ALIGN(ALIGN(x, SMP_CACHE_BYTES), 16)

#define TC956X_MAX_LINK_DELAY		800

#define XDP_PACKET_HEADROOM 256
#define TC956X_MAX_RX_BUF_SIZE(num)	(((num) * PAGE_SIZE) - XDP_PACKET_HEADROOM)

#ifdef TC956X_DMA_OFFLOAD_ENABLE
struct tc956xmac_cm3_tamap {
	u32 trsl_addr_hi;
	u32 trsl_addr_low;
	u32 src_addr_hi;
	u32 src_addr_low;	/* Only [31:12] bits will be considered */
	u32 atr_size;
	bool valid;
};
#endif

struct tc956xmac_resources {

	void __iomem *addr;
#ifdef TC956X_SRIOV_PF
#ifdef CONFIG_PCI_IOV
	u32 sriov_enabled;
#endif
#endif
	void __iomem *tc956x_BRIDGE_CFG_pci_base_addr;
	void __iomem *tc956x_SRAM_pci_base_addr;
	void __iomem *tc956x_SFR_pci_base_addr;

	const char *mac;
	int wol_irq;
	int lpi_irq;
	int irq;
#ifdef TC956X
	unsigned int port_num;
	unsigned int port_interface; /* Kernel module parameter variable for interface */
	unsigned int eee_enabled; /* Parameter to store kernel module parameter to enable/disable EEE */
	unsigned int tx_lpi_timer; /* Parameter to store kernel module parameter for LPI Auto Entry Timer */
#endif
};

struct tc956xmac_tx_info {
	dma_addr_t buf;
	bool map_as_page;
	unsigned int len;
	bool last_segment;
	bool is_jumbo;
};

#define TC956XMAC_TBS_AVAIL	BIT(0)
#define TC956XMAC_TBS_EN		BIT(1)


/* Frequently used values are kept adjacent for cache effect */
struct tc956xmac_tx_queue {
	u32 tx_count_frames;
	int tbs;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
	struct hrtimer txtimer;
#else
	struct timer_list txtimer;
#endif
	u32 queue_index;
	struct tc956xmac_priv *priv_data;
	struct dma_extended_desc *dma_etx ____cacheline_aligned_in_smp;
	struct dma_edesc *dma_entx;
	struct dma_desc *dma_tx;
	struct sk_buff **tx_skbuff;
	struct tc956xmac_tx_info *tx_skbuff_dma;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	dma_addr_t dma_tx_phy;
	u32 tx_tail_addr;
	u32 mss;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	struct sk_buff **tx_offload_skbuff;
	dma_addr_t *tx_offload_skbuff_dma;
	dma_addr_t buff_tx_phy;
	void *buffer_tx_va_addr;
#endif
};

struct tc956xmac_rx_buffer {
	struct page *page;
	struct page *sec_page;
	dma_addr_t addr;
	dma_addr_t sec_addr;
};

struct tc956xmac_rx_queue {
	u32 rx_count_frames;
	u32 queue_index;
	struct page_pool *page_pool;
	struct tc956xmac_rx_buffer *buf_pool;
	struct tc956xmac_priv *priv_data;
	struct dma_extended_desc *dma_erx;
	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	u32 rx_zeroc_thresh;
	dma_addr_t dma_rx_phy;
	u32 rx_tail_addr;
	unsigned int state_saved;
	struct {
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	struct sk_buff **rx_offload_skbuff;
	dma_addr_t *rx_offload_skbuff_dma;
	dma_addr_t buff_rx_phy;
	void *buffer_rx_va_addr;
#endif
};

struct tc956xmac_channel {
	struct napi_struct rx_napi ____cacheline_aligned_in_smp;
	struct napi_struct tx_napi ____cacheline_aligned_in_smp;
	struct tc956xmac_priv *priv_data;
	spinlock_t lock;
	u32 index;
};

struct tc956xmac_tc_entry {
	bool in_use;
	bool in_hw;
	bool is_last;
	bool is_frag;
	void *frag_ptr;
	unsigned int table_pos;
	u32 handle;
	u32 prio;
	struct {
		u32 match_data;
		u32 match_en;
		u8 af:1;
		u8 rf:1;
		u8 im:1;
		u8 nc:1;
		u8 res1:4;
		u8 frame_offset:6;
		u8 res2:2;
		u8 ok_index;
		u8 res3;
		u16 dma_ch_no;
		u16 res4;
	} __packed val;
};

#ifdef TC956X
#define TC956XMAC_PPS_MAX		3 /* Two are for output signal generation and one is internal use for eMAC */
#else
#define TC956XMAC_PPS_MAX		4
#endif
struct tc956xmac_pps_cfg {
	bool available;
	struct timespec64 start;
	struct timespec64 period;
};

struct tc956xmac_rss {
	int enable;
	u8 key[TC956XMAC_RSS_HASH_KEY_SIZE];
	u32 table[TC956XMAC_RSS_MAX_TABLE_SIZE];
};

#define TC956XMAC_FLOW_ACTION_DROP		BIT(0)
struct tc956xmac_flow_entry {
	unsigned long cookie;
	unsigned long action;
	u8 ip_proto;
	int in_use;
	int idx;
	int is_l4;
};

struct tc956xmac_rfs_entry {
	unsigned long cookie;
	u16 etype;
	int in_use;
	int type;
	int tc;
};

struct tc956x_cbs_params {
	u32 send_slope;
	u32 idle_slope;
	u32 high_credit;
	u32 low_credit;
	u32 percentage;
};

struct tc956x_gpio_config {
	u8 config; /* 1: configured, 0: not configured*/
	u8 out_val; /* 0 or 1 */
};

struct drv_cap {
	u8 csum_en;
	u8 crc_en;
	u8 tso_en;
	u8 jumbo_en;
};

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
struct sync_locks {
	spinlock_t mac_filter;
	spinlock_t vlan_filter;
	spinlock_t est;
	spinlock_t fpe;
	spinlock_t frp;
	spinlock_t cbs;
};
#endif

#ifdef TC956X_SRIOV_PF
struct work_queue_param {
	struct ethtool_pauseparam pause;
	struct ifreq rq;
	u16 val_out[MAX_NO_OF_VFS];
	u32 queue_no;
	u8 fn_id;
	u8 vf_no;
};
#endif

/* Rx Frame Steering */
enum tc956x_rfs_type {
	TC956X_RFS_T_VLAN,
	TC956X_RFS_T_LLDP,
	TC956X_RFS_T_1588,
	TC956X_RFS_T_MAX,
};

struct tc956xmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	u32 tx_coal_frames[MTL_MAX_TX_QUEUES];
	u32 tx_coal_timer[MTL_MAX_TX_QUEUES];
	u32 rx_coal_frames[MTL_MAX_TX_QUEUES];
#else
	u32 tx_coal_frames;
	u32 tx_coal_timer;
	u32 rx_coal_frames;
#endif
	int tx_coalesce;
	int hwts_tx_en;
	bool tx_path_in_lpi_mode;
	bool tso;
	int sph;
	u32 sarc_type;

	unsigned int dma_buf_sz;
	unsigned int rx_copybreak;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	u32 rx_riwt[MTL_MAX_RX_QUEUES];
#else
	u32 rx_riwt;
#endif
	int hwts_rx_en;

	void __iomem *ioaddr;
#ifdef TC956X_SRIOV_PF
#ifdef CONFIG_PCI_IOV
	s32 sriov_enabled;
#endif
#endif
	void __iomem *tc956x_BRIDGE_CFG_pci_base_addr;
	void __iomem *tc956x_SRAM_pci_base_addr;
	void __iomem *tc956x_SFR_pci_base_addr;
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
	void __iomem *tc956x_SRAM_mailbox_base_addr;
#endif
#ifdef TC956X_SRIOV_PF
	unsigned char clear_to_send[MAX_NO_OF_VFS];
#endif
	struct net_device *dev;
	struct device *device;
	struct mac_device_info *hw;
	int (*hwif_quirks)(struct tc956xmac_priv *priv);
	struct mutex lock;

	/* RX Queue */
	struct tc956xmac_rx_queue rx_queue[MTL_MAX_RX_QUEUES];

	/* TX Queue */
	struct tc956xmac_tx_queue tx_queue[MTL_MAX_TX_QUEUES];

	/* Generic channel for NAPI */
	struct tc956xmac_channel channel[TC956XMAC_CH_MAX];

	unsigned int l2_filtering_mode; /* 0 - if perfect and 1 - if hash filtering */
	unsigned int vlan_hash_filtering;
	struct tc956x_mac_addr *mac_table;
	struct tc956x_vlan_id *vlan_table;
	u32 sa_vlan_ins_via_reg;
	unsigned char ins_mac_addr[ETH_ALEN];

	/* RX Parser */
	bool rxp_enabled;

	/* Phy Link State */
	u32 link;
	int speed;
	u32 duplex;
	bool oldlink;
	int oldduplex;

	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];
#ifndef TC956X_SRIOV_VF
	struct phylink_config phylink_config;
	struct phylink *phylink;
#endif
	struct tc956xmac_extra_stats xstats ____cacheline_aligned_in_smp;
	struct tc956xmac_safety_stats sstats;
	struct plat_tc956xmacenet_data *plat;
	struct dma_features dma_cap;
	struct tc956xmac_counters mmc;
#ifdef TC956X_SRIOV_VF
	struct tc956x_sw_counters sw_stats;
#endif
	int hw_cap_support;
	int synopsys_id;
	u32 msg_enable;
	int wolopts;
	int wol_irq;
	int clk_csr;
	int lpi_irq;
	unsigned int eee_enabled;
	int eee_active;
	unsigned int tx_lpi_timer;
	unsigned int mode;
	unsigned int chain_mode;
	int extend_desc;
	struct hwtstamp_config tstamp_config;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_ops;
	unsigned int default_addend;
	u32 sub_second_inc;
	u32 systime_flags;
	u32 adv_ts;
	int use_riwt;
	int irq_wake;
	spinlock_t ptp_lock;
	void __iomem *mmcaddr;
	void __iomem *ptpaddr;
#ifdef TC956X
	void __iomem *xpcsaddr;
	void __iomem *pmaaddr;
#endif
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif

	unsigned long state;
	struct workqueue_struct *wq;
	struct work_struct service_task;
#ifdef TC956X_SRIOV_PF
	struct workqueue_struct *mbx_wq;
	struct work_struct service_mbx_task;
	struct work_queue_param mbx_wq_param;
#endif
	spinlock_t wq_lock;
#ifdef TC956X_SRIOV_VF
	struct workqueue_struct *mbx_wq;
	struct work_struct mbx_service_task;
	unsigned char flag;
#endif
	/* CBS configurations */
	struct tc956x_cbs_params cbs_speed100_cfg[8];
	struct tc956x_cbs_params cbs_speed1000_cfg[8];
	struct tc956x_cbs_params cbs_speed2500_cfg[8];
	struct tc956x_cbs_params cbs_speed5000_cfg[8];
	struct tc956x_cbs_params cbs_speed10000_cfg[8];
	/* TC Handling */
	unsigned int tc_entries_max;
	unsigned int tc_off_max;
	struct tc956xmac_tc_entry *tc_entries;
	unsigned int flow_entries_max;
	struct tc956xmac_flow_entry *flow_entries;

	unsigned int rfs_entries_max[TC956X_RFS_T_MAX];
	unsigned int rfs_entries_cnt[TC956X_RFS_T_MAX];
	unsigned int rfs_entries_total;
	struct tc956xmac_rfs_entry *rfs_entries;

	/* Pulse Per Second output */
	struct tc956xmac_pps_cfg pps[TC956XMAC_PPS_MAX];

	/* Receive Side Scaling */
	struct tc956xmac_rss rss;
#ifdef TC956X_SRIOV_PF
	u8 dma_vf_map[TC956XMAC_CH_MAX];
	u8 pf_queue_dma_map[MTL_MAX_TX_QUEUES];
	/* Features enabled in PF */
	struct drv_cap pf_drv_cap;
#endif
	/* Function ID information */
	struct fn_id fn_id_info;
#ifdef TC956X_SRIOV_VF
	/* Features enabled in PF */
	struct drv_cap pf_drv_cap;
#endif
	/* Tx Checksum Insertion */
	u32 csum_insertion;
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
	u32 rx_csum_state;
#endif
	/* CRC Tx Rx Configuraion */
	u32 tx_crc_pad_state;
	u32 rx_crc_pad_state;
#ifdef TC956X_SRIOV_PF
	u8 rsc_dma_ch_alloc[MAX_FUNCTIONS_PER_PF];
#endif
	/* eMAC port number */
#ifdef TC956X
	u32 port_num;
	u32 mac_loopback_mode;
	u32 phy_loopback_mode;
	bool is_sgmii_2p5g; /* For 2.5G SGMI, XPCS doesn't support AN. This flag is to identify 2.5G Speed for SGMII interface. */
	u32 port_interface; /* Kernel module parameter variable for interface */
	bool tc956x_port_pm_suspend; /* Port Suspend Status; True : port suspended, False : port resume */
	bool tc956xmac_pm_wol_interrupt; /* Port-wise flag for clearing interrupt after resume. */
#endif

	/* set to 1 when ptp offload is enabled, else 0. */
	u32 ptp_offload;
	/*
	 *ptp offloading mode - ORDINARY_SLAVE, ORDINARY_MASTER,
	 * TRANSPARENT_SLAVE, TRANSPARENT_MASTER, PTOP_TRANSPERENT.
	 */
	u32 ptp_offloading_mode;

	/* set to 1 when onestep timestamp is enabled, else 0. */
	u32 ost_en;

	/* Private data store for platform layer */
	void *plat_priv;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	void *client_priv;
	struct tc956xmac_cm3_tamap cm3_tamap[MAX_CM3_TAMAP_ENTRIES];
#endif
	/* Work struct for handling phy interrupt */
	struct work_struct emac_phy_work;
	u32 pm_saved_emac_rst; /* Save and restore EMAC Resets during suspend-resume sequence */
	u32 pm_saved_emac_clk; /* Save and restore EMAC Clocks during suspend-resume sequence */
#ifdef TC956X_SRIOV_PF
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	bool wol_config_enabled; /* Flag to indicate SerDes configuration for SGMII, 1Gbps for WOL */
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
	u32 pm_saved_linkdown_rst; /* Save and restore Resets during link-down sequence */
	u32 pm_saved_linkdown_clk; /* Save and restore Clocks during link-down sequence */
	bool port_link_down; /* Flag to save per port link down state */
	bool port_release; /* Flag to notify appropriate sequence of link down & up */
	struct mutex port_ld_release_lock; /* Mutex lock to handle (set and clear) flag to notify 
						appropriate sequence of link down & up */

#endif
	struct tc956x_gpio_config saved_gpio_config[GPIO_12 + 1]; /* Only GPIO0- GPIO06, GPI010-GPIO12 are used */
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dir; /* debugfs structure pointer for port specific */
#endif

#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
	void *pbridge_buffaddr;
	dma_addr_t pbridge_handle;
	size_t pbridge_buffsize;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	struct sync_locks spn_lock;
#endif
	unsigned long link_state;
	bool link_down_rst; /* For light-weight release and open during link-down */
};

struct tc956x_version {
	unsigned char rel_dbg; /* 'R' for release, 'D' for debug */
	unsigned char major;
	unsigned char minor;
	unsigned char sub_minor;
	unsigned char patch_rel_major;
	unsigned char patch_rel_minor;
};

enum tc956xmac_state {
	TC956XMAC_DOWN,
	TC956XMAC_RESET_REQUESTED,
	TC956XMAC_RESETING,
	TC956XMAC_SERVICE_SCHED,
};

struct tc956x_regs_config {
	u32 ncid;
	u32 nclkctrl0;
	u32 nrstctrl0;
	u32 nclkctrl1;
	u32 nrstctrl1;
	u32 nemac0ctl;
	u32 nemac1ctl;
	u32 nemacsts;
	u32 gpioi0;
	u32 gpioi1;
	u32 gpioe0;
	u32 gpioe1;
};

struct tc956x_regs_pcie {
	u32 rsc_mng_id;
};

struct tc956x_regs_msi {
	u32 msi_out_en;
	u32 msi_mask_set;
	u32 msi_mask_clr;
	u32 int_sts;
	u32 int_raw_sts;
	u32 msi_sts;
	u32 cnt_int0;
	u32 cnt_int1;
	u32 cnt_int2;
	u32 cnt_int3;
	u32 cnt_int4;
	u32 cnt_int11;
	u32 cnt_int12;
	u32 cnt_int20;
	u32 cnt_int24;
};

struct tc956x_regs_intc {
	u32 intmcumask0;
	u32 intmcumask1;
	u32 intmcumask2;
};

struct tc956x_regs_dma_ch {
	u32 control;
	u32 list_haddr;
	u32 list_laddr;
	u32 ring_len;
	u32 curr_haddr;
	u32 curr_laddr;
	u32 tail_ptr;
	u32 buf_haddr;
	u32 buf_laddr;
};

struct tx956x_tx_desc_buf_addrs {
	dma_addr_t desc_phy_addr;
	struct dma_desc *desc_va_addr;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	dma_addr_t buff_phy_addr;
	void *buff_va_addr;
#endif
	struct sk_buff **tx_skbuff;
	struct tc956xmac_tx_info *tx_skbuff_dma;
};

struct tx956x_rx_desc_buf_addrs {
	dma_addr_t desc_phy_addr;
	struct dma_desc *desc_va_addr;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	dma_addr_t buff_phy_addr;
	void *buff_va_addr;
#endif
	struct tc956xmac_rx_buffer *buf_pool;
};

struct tc956x_regs_dma {
	u32 debug_sts0;
	u32 ch_control[TC956XMAC_CH_MAX];
	u32 interrupt_enable[TC956XMAC_CH_MAX];
	u32 ch_status[TC956XMAC_CH_MAX];
	u32 debug_status[TC956XMAC_CH_MAX];
	u32 rxch_watchdog_timer[TC956XMAC_CH_MAX];
	struct tc956x_regs_dma_ch tx_ch[TC956XMAC_CH_MAX];
	struct tc956x_regs_dma_ch rx_ch[TC956XMAC_CH_MAX];
	/* RX Channels */
	struct tx956x_rx_desc_buf_addrs rx_queue[MTL_MAX_RX_QUEUES];
	/* TX Channels */
	struct tx956x_tx_desc_buf_addrs tx_queue[MTL_MAX_TX_QUEUES];
};

struct tc956x_regs_mac {
	u32 mac_tx_config;
	u32 mac_rx_config;
	u32 mac_pkt_filter;
	u32 mac_tx_rx_status;
	u32 mac_debug;
};

struct tc956x_regs_mtl_tx {
	u32 op_mode;
	u32 underflow;
	u32 debug;
};

struct tc956x_regs_mtl_rx {
	u32 op_mode;
	u32 miss_pkt_overflow;
	u32 debug;
	u32 flow_control;
};

struct tc956x_regs_mtl {
	u32 op_mode;
	u32 mtl_rxq_dma_map0;
	u32 mtl_rxq_dma_map1;
	struct tc956x_regs_mtl_tx tx_info[MTL_MAX_TX_QUEUES];
	struct tc956x_regs_mtl_rx rx_info[MTL_MAX_RX_QUEUES];
};

struct tc956x_regs_m3 {
	u32 sram_tx_pcie_addr[TC956XMAC_CH_MAX];
	u32 sram_rx_pcie_addr[TC956XMAC_CH_MAX];

	u32 m3_fw_init_done;
	u32 m3_fw_exit;

	u32 m3_debug_cnt0;
	u32 m3_debug_cnt1;
	u32 m3_debug_cnt2;
	u32 m3_debug_cnt3;
	u32 m3_debug_cnt4;
	u32 m3_debug_cnt5;
	u32 m3_debug_cnt6;
	u32 m3_debug_cnt7;
	u32 m3_debug_cnt8;
	u32 m3_debug_cnt9;
	u32 m3_debug_cnt10;
	u32 m3_watchdog_exp_cnt;
	u32 m3_watchdog_monitor_cnt;
	u32 m3_debug_cnt13;
	u32 m3_debug_cnt14;
	u32 m3_systick_cnt_upper_value;
	u32 m3_systick_cnt_lower_value;
	u32 m3_tx_timeout_port0;
	u32 m3_tx_timeout_port1;
	u32 m3_debug_cnt19;
};

struct tc956x_tamap {
	u32 trsl_addr_hi;
	u32 trsl_addr_low;
	u32 src_addr_hi;
	u32 src_addr_low;	/* Only [31:12] bits will be considered */
	u32 atr_size;
	u32 atr_impl;
};

struct tx956x_driver_info {
	u8 driver[32];
	u8 version[32];
	u8 fw_version[32];
};

struct tc956x_statistics {
	u64 rx_buf_unav_irq[TC956XMAC_CH_MAX];
	u64 tx_pkt_n[TC956XMAC_CH_MAX];
	u64 tx_pkt_errors_n[TC956XMAC_CH_MAX];
	u64 rx_pkt_n[TC956XMAC_CH_MAX];

	u64 mmc_tx_broadcastframe_g;
	u64 mmc_tx_multicastframe_g;
	u64 mmc_tx_64_octets_gb;
	u64 mmc_tx_framecount_gb;
	u64 mmc_tx_65_to_127_octets_gb;
	u64 mmc_tx_128_to_255_octets_gb;
	u64 mmc_tx_256_to_511_octets_gb;
	u64 mmc_tx_512_to_1023_octets_gb;
	u64 mmc_tx_1024_to_max_octets_gb;
	u64 mmc_tx_unicast_gb;
	u64 mmc_tx_underflow_error;
	u64 mmc_tx_framecount_g;
	u64 mmc_tx_pause_frame;
	u64 mmc_tx_vlan_frame_g;
	u64 mmc_tx_lpi_us_cntr;
	u64 mmc_tx_lpi_tran_cntr;

	/* MMC RX counter registers */
	u64 mmc_rx_framecount_gb;
	u64 mmc_rx_broadcastframe_g;
	u64 mmc_rx_multicastframe_g;
	u64 mmc_rx_crc_error;
	u64 mmc_rx_jabber_error;
	u64 mmc_rx_undersize_g;
	u64 mmc_rx_oversize_g;
	u64 mmc_rx_64_octets_gb;
	u64 mmc_rx_65_to_127_octets_gb;
	u64 mmc_rx_128_to_255_octets_gb;
	u64 mmc_rx_256_to_511_octets_gb;
	u64 mmc_rx_512_to_1023_octets_gb;
	u64 mmc_rx_1024_to_max_octets_gb;
	u64 mmc_rx_unicast_g;
	u64 mmc_rx_length_error;
	u64 mmc_rx_pause_frames;
	u64 mmc_rx_fifo_overflow;
	u64 mmc_rx_lpi_us_cntr;
	u64 mmc_rx_lpi_tran_cntr;
};

struct tc956x_regs {

	/*PCIe register*/
	struct tc956x_regs_pcie pcie_reg;

	/*Configuration register*/
	struct tc956x_regs_config config_reg;

	/*MSI register*/
	struct tc956x_regs_msi msi_reg;

	/*INTC register*/
	struct tc956x_regs_intc intc_reg;

	/*DMA Descriptor stats*/
	struct tc956x_regs_dma dma_reg;

	/*MAC debug stats*/
	struct tc956x_regs_mac mac_reg;

	/*MTL debug stats*/
	struct tc956x_regs_mtl mtl_reg;

	/*M3 stats*/
	struct tc956x_regs_m3 m3_reg;

	/*FRP Table*/
	struct tc956xmac_rx_parser_cfg *rxp_cfg;

	/* TAMAP */
	struct tc956x_tamap tamap[MAX_CM3_TAMAP_ENTRIES + 1]; /*0th for PCIe-eMAC 1,2,3 for IPA*/

	/*Driver & FW Information */
	struct tx956x_driver_info info;

	/* Statistics counters*/
	struct tc956x_statistics stats;

};

int tc956x_dump_regs(struct net_device *net_device, struct tc956x_regs *regs);
int tc956x_print_debug_regs(struct net_device *net_device, struct tc956x_regs *regs);
int tc956xmac_mdio_unregister(struct net_device *ndev);
int tc956xmac_mdio_register(struct net_device *ndev);
int tc956xmac_mdio_reset(struct mii_bus *mii);
void tc956xmac_set_ethtool_ops(struct net_device *netdev);

void tc956xmac_ptp_register(struct tc956xmac_priv *priv);
void tc956xmac_ptp_unregister(struct tc956xmac_priv *priv);
#ifdef TC956X_SRIOV_PF
int tc956xmac_resume(struct device *dev);
int tc956xmac_suspend(struct device *dev);
int tc956xmac_dvr_remove(struct device *dev);
int tc956xmac_dvr_probe(struct device *device,
		     struct plat_tc956xmacenet_data *plat_dat,
		     struct tc956xmac_resources *res);
#elif defined TC956X_SRIOV_VF
int tc956xmac_vf_resume(struct device *dev);
int tc956xmac_vf_suspend(struct device *dev);
int tc956xmac_vf_dvr_remove(struct device *dev);
int tc956xmac_vf_dvr_probe(struct device *device,
		     struct plat_tc956xmacenet_data *plat_dat,
		     struct tc956xmac_resources *res);
#endif
void tc956xmac_disable_eee_mode(struct tc956xmac_priv *priv);
bool tc956xmac_eee_init(struct tc956xmac_priv *priv);

#ifdef CONFIG_TC956XMAC_SELFTESTS
void tc956xmac_selftest_run(struct net_device *dev,
			 struct ethtool_test *etest, u64 *buf);
void tc956xmac_selftest_get_strings(struct tc956xmac_priv *priv, u8 *data);
int tc956xmac_selftest_get_count(struct tc956xmac_priv *priv);
#else
static inline void tc956xmac_selftest_run(struct net_device *dev,
				       struct ethtool_test *etest, u64 *buf)
{
	/* Not enabled */
}
static inline void tc956xmac_selftest_get_strings(struct tc956xmac_priv *priv,
					       u8 *data)
{
	/* Not enabled */
}
static inline int tc956xmac_selftest_get_count(struct tc956xmac_priv *priv)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_TC956XMAC_SELFTESTS */

/* Function Prototypes */

#ifndef TC956X_SRIOV_VF
s32 tc956x_load_firmware(struct device *dev, struct tc956xmac_resources *res);
#endif
extern int tc956xmac_pm_usage_counter;
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
int tc956x_pf_get_fn_idx_from_int_sts(struct tc956xmac_priv *priv,
					     struct fn_id *fn_id_info);
void tc956x_pf_parse_mbx(struct tc956xmac_priv *priv,
				enum mbx_msg_fns msg_src);
#endif

#ifdef CONFIG_TC956X_PLATFORM_SUPPORT
int tc956x_platform_probe(struct tc956xmac_priv *priv, struct tc956xmac_resources *res);
int tc956x_platform_remove(struct tc956xmac_priv *priv);
int tc956x_platform_suspend(struct tc956xmac_priv *priv);
int tc956x_platform_resume(struct tc956xmac_priv *priv);
#else
static inline int tc956x_platform_probe(struct tc956xmac_priv *priv, struct tc956xmac_resources *res) { return 0; }
static inline int tc956x_platform_remove(struct tc956xmac_priv *priv) { return 0; }
static inline int tc956x_platform_suspend(struct tc956xmac_priv *priv) { return 0; }
static inline int tc956x_platform_resume(struct tc956xmac_priv *priv) { return 0; }
#endif

int tc956x_GPIO_OutputConfigPin(struct tc956xmac_priv *priv, u32 gpio_pin, u8 out_value);
int tc956x_gpio_restore_configuration(struct tc956xmac_priv *priv);

#ifdef TC956X_SRIOV_VF
int tc956x_vf_get_fn_idx_from_int_sts(struct tc956xmac_priv *priv,
					     struct fn_id *fn_id_info);
void tc956x_vf_parse_mbx(struct tc956xmac_priv *priv,
				enum mbx_msg_fns msg_src);
int tc956x_vf_rsc_mng_get_fn_id(struct tc956xmac_priv *priv, void __iomem *reg_pci_bridge_config_addr,
				       struct fn_id *fn_id_info);

#endif
int tc956x_set_pci_speed(struct pci_dev *pdev, u32 speed);
void tc956xmac_link_change_set_power(struct tc956xmac_priv *priv, enum TC956X_PORT_LINK_CHANGE_STATE state);

#endif /* __TC956XMAC_H__ */
