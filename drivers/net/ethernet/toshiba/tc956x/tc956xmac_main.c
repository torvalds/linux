/*
 * TC956X ethernet driver.
 *
 * tc956xmac_main.c
 *
 * Copyright(C) 2007-2011 STMicroelectronics Ltd
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
 *  20 Jul 2021 : 1. Flash MDIO ioctl supported
 *		  2. Rxp statistics function removed
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  05 Aug 2021 : 1. Register Port0 as only PCIe device, incase its PHY is not found
 *  VERSION     : 01-00-08
 *  16 Aug 2021 : 1. PHY interrupt mode supported through .config_intr and .ack_interrupt API
 *  VERSION     : 01-00-09
 *  24 Aug 2021 : 1. Platform API supported
 *  VERSION     : 01-00-10
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 *  23 Sep 2021 : 1. Capturing RBU status using MAC EVENT Interupt and updating to ethtool statistics for both S/W & IPA DMA channels
 *  VERSION     : 01-00-14
 *  21 Oct 2021 : 1. Added support for GPIO configuration API
 *  VERSION     : 01-00-18
 *  26 Oct 2021 : 1. Added support for EEE PHY and MAC Control Mode.
		  2. Added dev_pm_ops interface support for suspend-resume.
		  3. Changed IRQF_SHARED to IRQF_NO_SUSPEND and added WOL Interrupt Handler support.
		  4. Added Platform Apis.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Stopped disabling/enabling of MAC TX during Link down/up.
 *  VERSION     : 01-00-20
 *  08 Nov 2021 : 1. Skip queuing PHY Work during suspend.
 *  VERSION     : 01-00-21
 *  24 Nov 2021 : 1. Private member used instead of global for wol interrupt indication
 *  VERSION     : 01-00-22
 *  24 Nov 2021 : 1. EEE update for runtime configuration and LPI interrupt disabled.
 		  2. EEE SW timers removed. Only HW timers used to control EEE LPI entry/exit
 		  3. USXGMII support during link change
 *  VERSION     : 01-00-24
 *  30 Nov 2021 : 1. Added PHY Workqueue Cancel during suspend only if network interface available.
 *  VERSION     : 01-00-26
 *  01 Dec 2021 : 1. Free EMAC IRQ during suspend and request EMAC IRQ during resume.
 *  VERSION     : 01-00-27
 *  03 Dec 2021 : 1. Added error check for phydev in tc956xmac_suspend().
 *  VERSION     : 01-00-29
 *  08 Dec 2021 : 1. Added Module parameter for Rx & Tx Queue Size configuration.
 *  VERSION     : 01-00-30
 *  10 Dec 2021 : 1. Added Module parameter to count Link partner pause frames and output to ethtool.
 *  VERSION     : 01-00-31
 *  27 Dec 2021 : 1. Support for eMAC Reset and unused clock disable during Suspend and restoring it back during resume.
		  2. Resetting and disabling of unused clocks for eMAC Port, when no-found PHY for that particular port.
		  3. Valid phy-address and mii-pointer NULL check in tc956xmac_suspend().
		  4. Version update.
 *  VERSION     : 01-00-32
 *  06 Jan 2022 : 1. Null check added while freeing skb buff data
 *  VERSION     : 01-00-33
 *  07 Jan 2022 : 1. During emac resume, attach the net device after initializing the queues
 *  VERSION     : 01-00-34
 *  11 Jan 2022 : 1. Fixed phymode support added
 *	          2. Error return when no phy driver found during ISR work queue execution
 *  VERSION     : 01-00-35
 *  18 Jan 2022 : 1. IRQ device name change
 *  VERSION     : 01-00-36
 *  20 Jan 2022 : 1. Reset eMAC if port unavailable (PHY not connected) during suspend-resume.
 *  VERSION     : 01-00-37
 *  31 Jan 2022 : 1. Debug dump API supported to dump registers during crash.
 *  VERSION     : 01-00-39
 *  04 Feb 2022 : 1. DMA channel status cleared only for SW path allocated DMA channels. IPA path DMA channel status clearing is skipped.
 *  VERSION     : 01-00-41
 *  14 Feb 2022 : 1. Reset assert and clock disable support during Link Down.
 *  VERSION     : 01-00-42
 *  22 Feb 2022 : 1. Supported GPIO configuration save and restoration
 *  VERSION     : 01-00-43
 *  25 Feb 2022 : 1. XPCS module is re-initialized after link-up as MACxPONRST is asserted during link-down.
 *		  2. Disable Rx side EEE LPI before configuring Rx Parser (FRP). Enable the same after Rx Parser configuration.
 *  VERSION     : 01-00-44
 *  09 Mar 2022 : 1. Handling of Non S/W path DMA channel abnormal interrupts in Driver and only TI & RI interrupts handled in FW.
 *		  2. Reading MSI status for checking interrupt status of SW MSI.
 *  VERSION     : 01-00-45
 *  05 Apr 2022 : 1. Disable MSI and flush phy work queue during driver release.
 *  VERSION     : 01-00-47
 *  06 Apr 2022 : 1. Dynamic MTU change supported. Max MTU supported is 2000 bytes.
 *		  2. Constant buffer size of 2K used, so that during MTU change there is no buffer reconfiguration.
 *  VERSION     : 01-00-48
 *  14 Apr 2022 : 1. Ignoring error from tc956xmac_hw_setup in tc956xmac_open API.
 *  VERSION     : 01-00-49
 *  25 Apr 2022 : 1. Perform platform remove after MDIO deregistration.
 *  VERSION     : 01-00-50
 *  29 Apr 2022 : 1. Added Lock for syncing linkdown, port rlease and release of offloaded DMA channels.
 *		  2. Added kernel Module parameter for selecting Power saving at Link down.
 *  VERSION     : 01-00-51
 *  15 Jun 2022 : 1. Added debugfs support for module specific register dump
 *  VERSION     : 01-00-52
 *  08 Aug 2022 : 1. Disable RBU interrupt on RBU interrupt occurance. IPA SW should enable it back.
 *  VERSION     : 01-00-53
 *  31 Aug 2022 : 1. Added Fix for configuring Rx Parser when EEE is enabled and RGMII Interface is used
 *  VERSION     : 01-00-54
 *  02 Sep 2022 : 1. 2500Base-X support for line speeds 2.5Gbps, 1Gbps, 100Mbps.
 *  VERSION     : 01-00-55
 *  09 Nov 2022 : 1. Update of fix for configuring Rx Parser when EEE is enabled
 *  VERSION     : 01-00-57
 *  22 Dec 2022 : 1. Support for SW reset during link down.
                  2. Module parameters introduced for the control of SW reset and by default SW reset is disabled.
 *  VERSION     : 01-00-58
 *  10 Nov 2023 : 1. Kernel 6.1 Porting changes.
                  2. DSP Cascade related modifications.
 *  VERSION     : 01-02-59
 *
 *  26 Dec 2023 : 1. Kernel 6.6 Porting changes
 *                2. Added the support for TC commands taprio and flower
 *  VERSION     : 01-03-59
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/pinctrl/consumer.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif /* CONFIG_DEBUG_FS */
#include <linux/net_tstamp.h>
#ifdef TC956X_SRIOV_PF
#include <linux/phylink.h>
#endif
#include <linux/udp.h>
#include <net/pkt_cls.h>
#include "tc956xmac_ptp.h"
#include "tc956xmac.h"
#include <linux/reset.h>
#include <linux/of_mdio.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include "dwxgmac2.h"
#include "hwif.h"
#include "common.h"
#include "tc956xmac_ioctl.h"
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
#include <linux/phy.h>
#include <linux/linkmode.h>
#endif
#ifdef TC956X_SRIOV_PF
#include "tc956x_pf_rsc_mng.h"
#include "tc956x_pf_mbx.h"
#include "tc956x_pf_rsc_mng.h"
#endif

#ifdef TC956X_SRIOV_VF
#include "tc956x_vf_mbx.h"
#endif
#ifdef TC956X_PCIE_LOGSTAT
#include "tc956x_pcie_logstat.h"
#endif /* #ifdef TC956X_PCIE_LOGSTAT */

#define	TSO_MAX_BUFF_SIZE	(SZ_16K - 1)
#define PPS_START_DELAY		100000000	/* 100 ms, in unit of ns */

#ifdef TC956X_DYNAMIC_LOAD_CBS
int prev_speed;
#endif
/* Module parameters */
#define TX_TIMEO	5000
static int watchdog = TX_TIMEO;
module_param(watchdog, int, 0644);
MODULE_PARM_DESC(watchdog, "Transmit timeout in milliseconds (default 5s)");

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Message Level (-1: default, 0: no output, 16: all)");

static int phyaddr = -1;
module_param(phyaddr, int, 0444);
MODULE_PARM_DESC(phyaddr, "Physical device address");

#define TC956XMAC_TX_THRESH	(DMA_TX_SIZE / 4)
#define TC956XMAC_RX_THRESH	(DMA_RX_SIZE / 4)

static int flow_ctrl = FLOW_AUTO;
module_param(flow_ctrl, int, 0644);
MODULE_PARM_DESC(flow_ctrl, "Flow control ability [on/off]");

static int pause = PAUSE_TIME;
module_param(pause, int, 0644);
MODULE_PARM_DESC(pause, "Flow Control Pause Time");

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;
module_param(tc, int, 0644);
MODULE_PARM_DESC(tc, "DMA threshold control value");

#define	DEFAULT_BUFSIZE	1536
static int buf_sz = DEFAULT_BUFSIZE;
module_param(buf_sz, int, 0644);
MODULE_PARM_DESC(buf_sz, "DMA buffer size");

#define	TC956XMAC_RX_COPYBREAK	256

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
				      NETIF_MSG_LINK | NETIF_MSG_IFUP |
				      NETIF_MSG_IFDOWN | NETIF_MSG_TIMER);

#ifdef TC956X_SRIOV_PF
static void tc956x_ptp_configuration(struct tc956xmac_priv *priv, u32 tcr_config);
#endif

#define TC956XMAC_DEFAULT_LPI_TIMER	1000
static int eee_timer = TC956XMAC_DEFAULT_LPI_TIMER;
module_param(eee_timer, int, 0644);
MODULE_PARM_DESC(eee_timer, "LPI tx expiration time in msec");
#define TC956XMAC_LPI_T(x) (jiffies + msecs_to_jiffies(x))

#if defined(TC956X_SRIOV_PF) && defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
static bool port_brige_state;
static DEFINE_MUTEX(tc956x_port_bridge_lock);
#endif
/* By default the driver will use the ring mode to manage tx and rx descriptors,
 * but allow user to force to use the chain instead of the ring
 */
static unsigned int chain_mode;
module_param(chain_mode, int, 0444);
MODULE_PARM_DESC(chain_mode, "To use chain instead of ring mode");
#if defined(TC956X_SRIOV_PF)
static irqreturn_t tc956xmac_interrupt_v0(int irq, void *dev_id);
static irqreturn_t tc956xmac_interrupt_v1(int irq, void *dev_id);
#elif defined(TC956X_SRIOV_VF)
static irqreturn_t tc956xmac_interrupt_v0(int irq, void *dev_id);
#endif
#ifdef CONFIG_DEBUG_FS
static const struct net_device_ops tc956xmac_netdev_ops;
static void tc956xmac_init_fs(struct net_device *dev);
static void tc956xmac_exit_fs(struct net_device *dev);
#endif
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
extern int phy_ethtool_set_eee_2p5(struct phy_device *phydev, struct ethtool_eee *data);
#endif
#ifdef TC956X_SRIOV_PF
static u32 tc956xmac_link_down_counter = 0; /* Counter to count Link Down/Up for both port */

extern void tc956x_pf_del_umac_addr(struct tc956xmac_priv *priv, int index, int vf);
extern void tc956x_pf_del_mac_filter(struct net_device *dev, int vf, const u8 *mac);
extern void tc956x_pf_del_vlan_filter(struct net_device *dev, u16 vf, u16 vid);
extern void tc956xmac_get_pauseparam(struct net_device *netdev,
									struct ethtool_pauseparam *pause);

int tc956xmac_ioctl_set_cbs(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_get_cbs(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_get_est(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_set_est(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_get_fpe(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_set_fpe(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_get_rxp(struct tc956xmac_priv *priv, void *data);
int tc956xmac_ioctl_set_rxp(struct tc956xmac_priv *priv, void *data);
void tc956xmac_service_mbx_event_schedule(struct tc956xmac_priv *priv);
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
extern int tc956x_pf_set_mac_filter(struct net_device *dev, int vf, const u8 *mac);
#endif

#endif /* TC956X_SRIOV_PF */
#define TC956XMAC_COAL_TIMER(x) (jiffies + usecs_to_jiffies(x))

/* MAC address */
#ifdef TC956X_SRIOV_PF
static u8 dev_addr[6][6] = {
				{0xEC, 0x21, 0xE5, 0x10, 0x4F, 0xEA},
				{0xEC, 0x21, 0xE5, 0x11, 0x4F, 0xEA},
				{0xEC, 0x21, 0xE5, 0x12, 0x4F, 0xEA},
				{0xEC, 0x21, 0xE5, 0x13, 0x4F, 0xEA},
				{0xEC, 0x21, 0xE5, 0x14, 0x4F, 0xEA},
				{0xEC, 0x21, 0xE5, 0x15, 0x4F, 0xEA},
			};

#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
u8 flow_ctrl_addr[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};
#endif

#elif defined(TC956X_SRIOV_VF) /* Defining different MAC address for VF */
static u8 dev_addr[2][3][6] = {
				{ /* PF-0 */
				{0xEC, 0x21, 0xE5, 0x12, 0x4F, 0xEA}, /* VF-0 */
				{0xEC, 0x21, 0xE5, 0x13, 0x4F, 0xEA}, /* VF-1 */
				{0xEC, 0x21, 0xE5, 0x14, 0x4F, 0xEA}, /* VF-2 */
				},
				{ /* PF-1 */
				{0xEC, 0x21, 0xE5, 0x15, 0x4F, 0xEA}, /* VF-0 */
				{0xEC, 0x21, 0xE5, 0x16, 0x4F, 0xEA}, /* VF-1 */
				{0xEC, 0x21, 0xE5, 0x17, 0x4F, 0xEA}, /* VF-2 */
				},
			};
#endif

#ifdef TC956X
#ifndef TC956X_SRIOV_VF
#define TX_PRESET_MAX		11
static u8 tx_demphasis_setting[TX_PRESET_MAX][2] =	{
			{0x00, 0x14}, /*Preshoot: 0 dB, De-emphasis: -6 dB*/
			{0x00, 0x0E}, /*Preshoot: 0 dB, De-emphasis: -3.5 dB*/
			{0x00, 0x10}, /*Preshoot: 0 dB, De-emphasis: -4.4 dB*/
			{0x00, 0x0A}, /*Preshoot: 0 dB, De-emphasis: -2.5 dB*/
			{0x00, 0x00}, /*Preshoot: 0 dB, De-emphasis: 0 dB*/
			{0x08, 0x00}, /*Preshoot: 1.9 dB, De-emphasis: 0 dB*/
			{0x0A, 0x00}, /*Preshoot: 2.5 dB, De-emphasis: 0 dB*/
			{0x08, 0x10}, /*Preshoot: 3.5 dB, De-emphasis: -6 dB*/
			{0x0A, 0x0A}, /*Preshoot: 3.5 dB, De-emphasis: -3.5 dB*/
			{0x0E, 0x00}, /*Preshoot: 3.5 dB, De-emphasis: 0 dB*/
			{0x00, 0x1B}, /*Preshoot: 0 dB, De-emphasis: -9.1 dB*/
};
#endif /* TC956X_SRIOV_VF */
#endif

struct config_parameter_list {
	char mdio_key[32];
	unsigned short mdio_key_len;
	char mac_key[32];
	unsigned short mac_key_len;
	unsigned short mac_str_len;
	char mac_str_def[20];
};

static const struct config_parameter_list config_param_list[] = {
{"MDIOBUSID", 9, "MAC_ID", 6, 17, "00:00:00:00:00:00"},
};

static uint16_t mdio_bus_id;
#define CONFIG_PARAM_NUM ARRAY_SIZE(config_param_list)
int tc956xmac_rx_parser_configuration(struct tc956xmac_priv *);

/* Source Address in Pause frame from PHY */
static u8 phy_sa_addr[2][6] = {
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}, /*For Port-0*/
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}, /*For Port-1*/
};
extern unsigned int mac0_en_lp_pause_frame_cnt;
extern unsigned int mac1_en_lp_pause_frame_cnt;
#ifndef TC956X_SRIOV_VF

extern unsigned int mac_power_save_at_link_down;

extern unsigned int mac0_force_speed_mode;
extern unsigned int mac1_force_speed_mode;

extern unsigned int mac0_link_down_macrst;
extern unsigned int mac1_link_down_macrst;

static int dwxgmac2_rx_parser_read_entry(struct tc956xmac_priv *priv,
		struct tc956xmac_rx_parser_entry *entry, int entry_pos)
{
	void __iomem *ioaddr = priv->ioaddr;
	int limit;
	int i;
	u32 reg_val;

	for (i = 0; i < (sizeof(*entry) / sizeof(u32)); i++) {
		int real_pos = entry_pos * (sizeof(*entry) / sizeof(u32)) + i;

		/* Set Entry Position */
		reg_val = readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);
		reg_val = reg_val & (~XGMAC_ADDR);
		reg_val |= (real_pos & XGMAC_ADDR);
		writel(reg_val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Set Read op */
		reg_val = readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);
		reg_val &= ~XGMAC_WRRDN;
		writel(reg_val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Start read */
		reg_val = readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);
		reg_val |= XGMAC_STARTBUSY;
		writel(reg_val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);


		limit = 10000;
		while (limit--) {
			if (!(readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST) &
			      XGMAC_STARTBUSY))
				break;
			udelay(1);
		}
		if (limit < 0)
			return -EBUSY;

		/* Read Data Reg */
		*((unsigned int  *)entry + i) = readl(ioaddr  + XGMAC_MTL_RXP_IACC_DATA);
	}

	return 0;
}


int tc956x_dump_regs(struct net_device *net_device, struct tc956x_regs *regs)
{
	struct tc956xmac_priv *priv = netdev_priv(net_device);
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 ch, queue, table_entry, reg = 0;
	struct tc956xmac_tx_queue *tx_q;
	struct tc956xmac_rx_queue *rx_q;
	u8 fw_version_str[32];
	struct tc956x_version *fw_version;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* PCIe register dump */
	regs->pcie_reg.rsc_mng_id = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG);

	/* Configuration register dump */
	regs->config_reg.ncid = readl(priv->ioaddr + NCID_OFFSET);
	regs->config_reg.nclkctrl0 = readl(priv->ioaddr + NCLKCTRL0_OFFSET);
	regs->config_reg.nrstctrl0 = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
	regs->config_reg.nclkctrl1 = readl(priv->ioaddr + NCLKCTRL1_OFFSET);
	regs->config_reg.nrstctrl1 = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
	regs->config_reg.nemac0ctl = readl(priv->ioaddr + NEMAC0CTL_OFFSET);
	regs->config_reg.nemac1ctl = readl(priv->ioaddr + NEMAC1CTL_OFFSET);
	regs->config_reg.nemacsts = readl(priv->ioaddr + NEMACSTS_OFFSET);
	regs->config_reg.gpioi0 = readl(priv->ioaddr + GPIOI0_OFFSET);
	regs->config_reg.gpioi1 = readl(priv->ioaddr + GPIOI1_OFFSET);
	regs->config_reg.gpioe0 = readl(priv->ioaddr + GPIOE0_OFFSET);
	regs->config_reg.gpioe1 = readl(priv->ioaddr + GPIOE1_OFFSET);

	/* MSI register dump */
	regs->msi_reg.msi_out_en = readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num, 0));
	regs->msi_reg.msi_mask_set = readl(priv->ioaddr + TC956X_MSI_MASK_SET_OFFSET(priv->port_num, 0));
	regs->msi_reg.msi_mask_clr = readl(priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->port_num, 0));
	regs->msi_reg.int_sts = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->port_num, 0));
	regs->msi_reg.int_raw_sts = readl(priv->ioaddr + TC956X_MSI_INT_RAW_STS_OFFSET(priv->port_num, 0));
	regs->msi_reg.msi_sts = readl(priv->ioaddr + TC956X_MSI_STS_OFFSET(priv->port_num, 0));
	regs->msi_reg.cnt_int0 = readl(priv->ioaddr + TC956X_MSI_CNT0(priv->port_num, 0));
	regs->msi_reg.cnt_int1 = readl(priv->ioaddr + TC956X_MSI_CNT1(priv->port_num, 0));
	regs->msi_reg.cnt_int2 = readl(priv->ioaddr + TC956X_MSI_CNT2(priv->port_num, 0));
	regs->msi_reg.cnt_int3 = readl(priv->ioaddr + TC956X_MSI_CNT3(priv->port_num, 0));
	regs->msi_reg.cnt_int4 = readl(priv->ioaddr + TC956X_MSI_CNT4(priv->port_num, 0));
	regs->msi_reg.cnt_int11 = readl(priv->ioaddr + TC956X_MSI_CNT11(priv->port_num, 0));
	regs->msi_reg.cnt_int12 = readl(priv->ioaddr + TC956X_MSI_CNT12(priv->port_num, 0));
	regs->msi_reg.cnt_int20 = readl(priv->ioaddr + TC956X_MSI_CNT20(priv->port_num, 0));
	regs->msi_reg.cnt_int24 = readl(priv->ioaddr + TC956X_MSI_CNT24(priv->port_num, 0));

	/* INTC register dump */
	regs->intc_reg.intmcumask0 = readl(priv->ioaddr + INTMCUMASK0);
	regs->intc_reg.intmcumask1 = readl(priv->ioaddr + INTMCUMASK1);
	regs->intc_reg.intmcumask2 = readl(priv->ioaddr + INTMCUMASK2);

	/* DMA channel register dump */
	regs->dma_reg.debug_sts0 = readl(priv->ioaddr + XGMAC_DMA_DEBUG_STATUS0);

	for (ch = 0; ch < maxq; ch++) {
		regs->dma_reg.ch_control[ch] = readl(priv->ioaddr + XGMAC_DMA_CH_CONTROL(ch));
		regs->dma_reg.interrupt_enable[ch] = readl(priv->ioaddr + XGMAC_DMA_CH_INT_EN(ch));
		regs->dma_reg.ch_status[ch] = readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(ch));
		regs->dma_reg.debug_status[ch] = readl(priv->ioaddr + XGMAC_DMA_CH_DBG_STATUS(ch));
		regs->dma_reg.rxch_watchdog_timer[ch] = readl(priv->ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(ch));
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		regs->dma_reg.tx_ch[ch].control = readl(priv->ioaddr + XGMAC_DMA_CH_TX_CONTROL(ch));
		regs->dma_reg.tx_ch[ch].list_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_HADDR(ch));
		regs->dma_reg.tx_ch[ch].list_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(ch));
		regs->dma_reg.tx_ch[ch].ring_len = readl(priv->ioaddr + XGMAC_DMA_CH_TX_CONTROL2(ch));
		regs->dma_reg.tx_ch[ch].curr_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxDESC_HADDR(ch));
		regs->dma_reg.tx_ch[ch].curr_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxDESC_LADDR(ch));
		regs->dma_reg.tx_ch[ch].tail_ptr = readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_TAIL_LPTR(ch));
		regs->dma_reg.tx_ch[ch].buf_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxBuff_HADDR(ch));
		regs->dma_reg.tx_ch[ch].buf_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxBuff_LADDR(ch));
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		regs->dma_reg.rx_ch[ch].control = readl(priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL(ch));
		regs->dma_reg.rx_ch[ch].list_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(ch));
		regs->dma_reg.rx_ch[ch].list_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(ch));
		regs->dma_reg.rx_ch[ch].ring_len = readl(priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL2(ch));
		regs->dma_reg.rx_ch[ch].curr_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxDESC_HADDR(ch));
		regs->dma_reg.rx_ch[ch].curr_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxDESC_LADDR(ch));
		regs->dma_reg.rx_ch[ch].tail_ptr = readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_TAIL_LPTR(ch));
		regs->dma_reg.rx_ch[ch].buf_haddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxBuff_HADDR(ch));
		regs->dma_reg.rx_ch[ch].buf_laddr = readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxBuff_LADDR(ch));
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		tx_q = &priv->tx_queue[ch];
		regs->dma_reg.tx_queue[ch].desc_phy_addr = tx_q->dma_tx_phy;
		regs->dma_reg.tx_queue[ch].desc_va_addr = tx_q->dma_tx;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
		regs->dma_reg.tx_queue[ch].buff_phy_addr = tx_q->buff_tx_phy;
		regs->dma_reg.tx_queue[ch].buff_va_addr = (void *)tx_q->buffer_tx_va_addr;
#endif
		regs->dma_reg.tx_queue[ch].tx_skbuff = tx_q->tx_skbuff;
		regs->dma_reg.tx_queue[ch].tx_skbuff_dma = tx_q->tx_skbuff_dma;
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		rx_q = &priv->rx_queue[ch];
		regs->dma_reg.rx_queue[ch].desc_phy_addr = rx_q->dma_rx_phy;
		regs->dma_reg.rx_queue[ch].desc_va_addr = rx_q->dma_rx;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
		regs->dma_reg.rx_queue[ch].buff_phy_addr = rx_q->buff_rx_phy;
		regs->dma_reg.rx_queue[ch].buff_va_addr = (void *)rx_q->buffer_rx_va_addr;
#endif
		regs->dma_reg.rx_queue[ch].buf_pool = rx_q->buf_pool;
	}

	/* MAC register dump */
	regs->mac_reg.mac_tx_config = readl(priv->ioaddr + XGMAC_TX_CONFIG);
	regs->mac_reg.mac_rx_config = readl(priv->ioaddr + XGMAC_RX_CONFIG);
	regs->mac_reg.mac_pkt_filter = readl(priv->ioaddr + XGMAC_PACKET_FILTER);
	regs->mac_reg.mac_tx_rx_status = readl(priv->ioaddr + XGMAC_RX_TX_STS);
	regs->mac_reg.mac_debug = readl(priv->ioaddr + XGMAC_DEBUG);

	/* MTL register dump */
	regs->mtl_reg.op_mode = readl(priv->ioaddr + XGMAC_MTL_OPMODE);
	regs->mtl_reg.mtl_rxq_dma_map0 = readl(priv->ioaddr + XGMAC_MTL_RXQ_DMA_MAP0);
	regs->mtl_reg.mtl_rxq_dma_map1 = readl(priv->ioaddr + XGMAC_MTL_RXQ_DMA_MAP1);

	for (queue = 0; queue < MTL_MAX_TX_QUEUES; queue++) {
		regs->mtl_reg.tx_info[queue].op_mode = readl(priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(queue));
		regs->mtl_reg.tx_info[queue].underflow = readl(priv->ioaddr + XGMAC_MTL_TXQ_UF_OFFSET(queue));
		regs->mtl_reg.tx_info[queue].debug = readl(priv->ioaddr + XGMAC_MTL_TXQ_Debug(queue));
	}

	for (queue = 0; queue < MTL_MAX_RX_QUEUES; queue++) {
		regs->mtl_reg.rx_info[queue].op_mode = readl(priv->ioaddr + XGMAC_MTL_RXQ_OPMODE(queue));
		regs->mtl_reg.rx_info[queue].miss_pkt_overflow = readl(priv->ioaddr + XGMAC_MTL_RXQ_MISS_PKT_OF_CNT_OFFSET(queue));
		regs->mtl_reg.rx_info[queue].debug = readl(priv->ioaddr + XGMAC_MTL_RXQ_Debug(queue));
		regs->mtl_reg.rx_info[queue].flow_control = readl(priv->ioaddr + XGMAC_MTL_RXQ_FLOW_CONTROL(queue));
	}

	/* M3 SRAM dump */
	for (ch = 0; ch < maxq; ch++) {
		regs->m3_reg.sram_tx_pcie_addr[ch] = readl(priv->tc956x_SRAM_pci_base_addr + SRAM_TX_PCIE_ADDR_LOC +
							(priv->port_num * TC956XMAC_CH_MAX * 4) + (ch * 4));
		regs->m3_reg.sram_rx_pcie_addr[ch] = readl(priv->tc956x_SRAM_pci_base_addr + SRAM_RX_PCIE_ADDR_LOC +
							(priv->port_num * TC956XMAC_CH_MAX * 4) + (ch * 4));
	}

	regs->m3_reg.m3_fw_init_done = readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_INIT_DONE);
	regs->m3_reg.m3_fw_exit	= readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT);

	regs->m3_reg.m3_debug_cnt0 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT0)));
	regs->m3_reg.m3_debug_cnt1 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT1)));
	regs->m3_reg.m3_debug_cnt2 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT2)));
	regs->m3_reg.m3_debug_cnt3 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT3)));
	regs->m3_reg.m3_debug_cnt4 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT4)));
	regs->m3_reg.m3_debug_cnt5 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT5)));
	regs->m3_reg.m3_debug_cnt6 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT6)));
	regs->m3_reg.m3_debug_cnt7 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT7)));
	regs->m3_reg.m3_debug_cnt8 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT8)));
	regs->m3_reg.m3_debug_cnt9 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT9)));
	regs->m3_reg.m3_debug_cnt10 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT10)));
	regs->m3_reg.m3_watchdog_exp_cnt = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT11)));
	regs->m3_reg.m3_watchdog_monitor_cnt = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT12)));
	regs->m3_reg.m3_debug_cnt13 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT13)));
	regs->m3_reg.m3_debug_cnt14 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT14)));
	regs->m3_reg.m3_systick_cnt_upper_value = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT16)));
	regs->m3_reg.m3_systick_cnt_lower_value = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT15)));
	regs->m3_reg.m3_tx_timeout_port0 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT17)));
	regs->m3_reg.m3_tx_timeout_port1 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT18)));
	regs->m3_reg.m3_debug_cnt19 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT19)));

	regs->rxp_cfg = (struct tc956xmac_rx_parser_cfg *)&priv->plat->rxp_cfg;

	/* TAMAP Information */
	for (table_entry = 0; table_entry <= MAX_CM3_TAMAP_ENTRIES; table_entry++) {
		regs->tamap[table_entry].trsl_addr_hi =	readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry));
		regs->tamap[table_entry].trsl_addr_low = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry));
		regs->tamap[table_entry].src_addr_hi =	readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_SRC_ADDR_HI(0, table_entry));
		regs->tamap[table_entry].src_addr_low = (readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_SRC_LO_MASK);
		regs->tamap[table_entry].atr_size = ((readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_ATR_SIZE_MASK) >> 1);
		regs->tamap[table_entry].atr_impl = (readl(priv->tc956x_BRIDGE_CFG_pci_base_addr +
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_ATR_IMPL);
	}

	/* Driver & FW Information */
	strlcpy(regs->info.driver, TC956X_RESOURCE_NAME, sizeof(regs->info.driver));
	strlcpy(regs->info.version, DRV_MODULE_VERSION, sizeof(regs->info.version));

	reg = readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_DBG_VER_START);
	fw_version = (struct tc956x_version *)(&reg);
	scnprintf(fw_version_str, sizeof(fw_version_str), "FW Version %s_%d.%d-%d", (fw_version->rel_dbg == 'D')?"DBG":"REL",
					fw_version->major, fw_version->minor, fw_version->sub_minor);
	strlcpy(regs->info.fw_version, fw_version_str, sizeof(regs->info.fw_version));

	/* Updating statistics */
	tc956xmac_mmc_read(priv, priv->mmcaddr, &priv->mmc);
	for (ch = 0; ch < tx_queues_cnt; ch++) {
		regs->stats.rx_buf_unav_irq[ch] = priv->xstats.rx_buf_unav_irq[ch];
		regs->stats.tx_pkt_n[ch] = priv->xstats.tx_pkt_n[ch];
		regs->stats.tx_pkt_errors_n[ch] = priv->xstats.tx_pkt_errors_n[ch];
		regs->stats.rx_pkt_n[ch] = priv->xstats.rx_pkt_n[ch];
	}
	regs->stats.mmc_tx_broadcastframe_g = priv->mmc.mmc_tx_broadcastframe_g;
	regs->stats.mmc_tx_multicastframe_g = priv->mmc.mmc_tx_multicastframe_g;
	regs->stats.mmc_tx_64_octets_gb = priv->mmc.mmc_tx_64_octets_gb;
	regs->stats.mmc_tx_framecount_gb = priv->mmc.mmc_tx_framecount_gb;
	regs->stats.mmc_tx_65_to_127_octets_gb = priv->mmc.mmc_tx_65_to_127_octets_gb;
	regs->stats.mmc_tx_128_to_255_octets_gb = priv->mmc.mmc_tx_128_to_255_octets_gb;
	regs->stats.mmc_tx_256_to_511_octets_gb = priv->mmc.mmc_tx_256_to_511_octets_gb;
	regs->stats.mmc_tx_512_to_1023_octets_gb = priv->mmc.mmc_tx_512_to_1023_octets_gb;
	regs->stats.mmc_tx_1024_to_max_octets_gb = priv->mmc.mmc_tx_1024_to_max_octets_gb;
	regs->stats.mmc_tx_unicast_gb = priv->mmc.mmc_tx_unicast_gb;
	regs->stats.mmc_tx_underflow_error = priv->mmc.mmc_tx_underflow_error;
	regs->stats.mmc_tx_framecount_g = priv->mmc.mmc_tx_framecount_g;
	regs->stats.mmc_tx_pause_frame = priv->mmc.mmc_tx_pause_frame;
	regs->stats.mmc_tx_vlan_frame_g = priv->mmc.mmc_tx_vlan_frame_g;
	regs->stats.mmc_tx_lpi_us_cntr = priv->mmc.mmc_tx_lpi_us_cntr;
	regs->stats.mmc_tx_lpi_tran_cntr = priv->mmc.mmc_tx_lpi_tran_cntr;

	regs->stats.mmc_rx_framecount_gb = priv->mmc.mmc_rx_framecount_gb;
	regs->stats.mmc_rx_broadcastframe_g = priv->mmc.mmc_rx_broadcastframe_g;
	regs->stats.mmc_rx_multicastframe_g = priv->mmc.mmc_rx_multicastframe_g;
	regs->stats.mmc_rx_crc_error = priv->mmc.mmc_rx_crc_error;
	regs->stats.mmc_rx_jabber_error = priv->mmc.mmc_rx_jabber_error;
	regs->stats.mmc_rx_undersize_g = priv->mmc.mmc_rx_undersize_g;
	regs->stats.mmc_rx_oversize_g = priv->mmc.mmc_rx_oversize_g;
	regs->stats.mmc_rx_64_octets_gb = priv->mmc.mmc_rx_64_octets_gb;
	regs->stats.mmc_rx_65_to_127_octets_gb = priv->mmc.mmc_rx_65_to_127_octets_gb;
	regs->stats.mmc_rx_128_to_255_octets_gb = priv->mmc.mmc_rx_128_to_255_octets_gb;
	regs->stats.mmc_rx_256_to_511_octets_gb = priv->mmc.mmc_rx_256_to_511_octets_gb;
	regs->stats.mmc_rx_512_to_1023_octets_gb = priv->mmc.mmc_rx_512_to_1023_octets_gb;
	regs->stats.mmc_rx_1024_to_max_octets_gb = priv->mmc.mmc_rx_1024_to_max_octets_gb;
	regs->stats.mmc_rx_unicast_g = priv->mmc.mmc_rx_unicast_g;
	regs->stats.mmc_rx_length_error = priv->mmc.mmc_rx_length_error;
	regs->stats.mmc_rx_pause_frames = priv->mmc.mmc_rx_pause_frames;
	regs->stats.mmc_rx_fifo_overflow = priv->mmc.mmc_rx_fifo_overflow;
	regs->stats.mmc_rx_lpi_us_cntr = priv->mmc.mmc_rx_lpi_us_cntr;
	regs->stats.mmc_rx_lpi_tran_cntr = priv->mmc.mmc_rx_lpi_tran_cntr;

	/* Reading FRP Table information from Registers */
	regs->rxp_cfg->nve = (readl(priv->ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS) & 0xFF);
	regs->rxp_cfg->npe = ((readl(priv->ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS) >> 16) & 0xFF);
	for (table_entry = 0; table_entry <= (regs->rxp_cfg->nve); table_entry++) {
		dwxgmac2_rx_parser_read_entry(priv,
		&(regs->rxp_cfg->entries[table_entry]), table_entry);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tc956x_dump_regs);
#endif

int tc956x_print_debug_regs(struct net_device *net_device, struct tc956x_regs *regs)
{

	struct tc956xmac_priv *priv = netdev_priv(net_device);
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 ch, queue, index;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	KPRINT_DEBUG1("regs->pcie_reg.rsc_mng_id = 0x%x\n", regs->pcie_reg.rsc_mng_id);

	/* Configuration register dump */
	KPRINT_DEBUG1("regs->config_reg.ncid = 0x%x\n", regs->config_reg.ncid);
	KPRINT_DEBUG1("regs->config_reg.nclkctrl0 = 0x%x\n", regs->config_reg.nclkctrl0);
	KPRINT_DEBUG1("regs->config_reg.nrstctrl0 = 0x%x\n", regs->config_reg.nrstctrl0);
	KPRINT_DEBUG1("regs->config_reg.nclkctrl1 = 0x%x\n", regs->config_reg.nclkctrl1);
	KPRINT_DEBUG1("regs->config_reg.nrstctrl1 = 0x%x\n", regs->config_reg.nrstctrl1);
	KPRINT_DEBUG1("regs->config_reg.nemac0ctl = 0x%x\n", regs->config_reg.nemac0ctl);
	KPRINT_DEBUG1("regs->config_reg.nemac1ctl = 0x%x\n", regs->config_reg.nemac1ctl);
	KPRINT_DEBUG1("regs->config_reg.nemacsts = 0x%x\n", regs->config_reg.nemacsts);
	KPRINT_DEBUG1("regs->config_reg.gpioi0 = 0x%x\n", regs->config_reg.gpioi0);
	KPRINT_DEBUG1("regs->config_reg.gpioi1 = 0x%x\n", regs->config_reg.gpioi1);
	KPRINT_DEBUG1("regs->config_reg.gpioe0 = 0x%x\n", regs->config_reg.gpioe0);
	KPRINT_DEBUG1("regs->config_reg.gpioe1 = 0x%x\n", regs->config_reg.gpioe1);

	/* MSI register dump */
	KPRINT_DEBUG1("regs->msi_reg.msi_out_en = 0x%x\n", regs->msi_reg.msi_out_en);
	KPRINT_DEBUG1("regs->msi_reg.msi_mask_set = 0x%x\n", regs->msi_reg.msi_mask_set);
	KPRINT_DEBUG1("regs->msi_reg.msi_mask_clr = 0x%x\n", regs->msi_reg.msi_mask_clr);
	KPRINT_DEBUG1("regs->msi_reg.int_sts = 0x%x\n", regs->msi_reg.int_sts);
	KPRINT_DEBUG1("regs->msi_reg.int_raw_sts = 0x%x\n", regs->msi_reg.int_raw_sts);
	KPRINT_DEBUG1("regs->msi_reg.msi_sts = 0x%x\n", regs->msi_reg.msi_sts);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int0 = 0x%x\n", regs->msi_reg.cnt_int0);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int1 = 0x%x\n", regs->msi_reg.cnt_int1);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int2 = 0x%x\n", regs->msi_reg.cnt_int2);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int3 = 0x%x\n", regs->msi_reg.cnt_int3);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int4 = 0x%x\n", regs->msi_reg.cnt_int4);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int11 = 0x%x\n", regs->msi_reg.cnt_int11);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int12 = 0x%x\n", regs->msi_reg.cnt_int12);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int20 = 0x%x\n", regs->msi_reg.cnt_int20);
	KPRINT_DEBUG1("regs->msi_reg.cnt_int24 = 0x%x\n", regs->msi_reg.cnt_int24);

	/* INTC register dump */
	KPRINT_DEBUG1("regs->intc_reg.intmcumask0 = 0x%x\n", regs->intc_reg.intmcumask0);
	KPRINT_DEBUG1("regs->intc_reg.intmcumask1 = 0x%x\n", regs->intc_reg.intmcumask1);
	KPRINT_DEBUG1("regs->intc_reg.intmcumask2 = 0x%x\n", regs->intc_reg.intmcumask2);

	/* DMA channel register dump */
	KPRINT_DEBUG1("regs->dma_reg.debug_sts0 = 0x%x\n", regs->dma_reg.debug_sts0);

	for (ch = 0; ch < maxq; ch++) {
		KPRINT_DEBUG1("regs->dma_reg.ch_control[%d] = 0x%x\n", ch, regs->dma_reg.ch_control[ch]);
		KPRINT_DEBUG1("regs->dma_reg.interrupt_enable[%d] = 0x%x\n", ch, regs->dma_reg.interrupt_enable[ch]);
		KPRINT_DEBUG1("regs->dma_reg.ch_status[%d] = 0x%x\n", ch, regs->dma_reg.ch_status[ch]);
		KPRINT_DEBUG1("regs->dma_reg.debug_status[%d] = 0x%x\n", ch, regs->dma_reg.debug_status[ch]);
		KPRINT_DEBUG1("regs->dma_reg.rxch_watchdog_timer[%d] = 0x%x\n", ch, regs->dma_reg.rxch_watchdog_timer[ch]);
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].control = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].control);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].list_haddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].list_haddr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].list_laddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].list_laddr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].ring_len = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].ring_len);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].curr_haddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].curr_haddr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].curr_laddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].curr_laddr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].tail_ptr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].tail_ptr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].buf_haddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].buf_haddr);
		KPRINT_DEBUG1("regs->dma_reg.tx_ch[%d].buf_laddr = 0x%x\n", ch, regs->dma_reg.tx_ch[ch].buf_laddr);
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].control = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].control);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].list_haddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].list_haddr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].list_laddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].list_laddr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].ring_len = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].ring_len);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].curr_haddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].curr_haddr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].curr_laddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].curr_laddr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].tail_ptr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].tail_ptr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].buf_haddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].buf_haddr);
		KPRINT_DEBUG1("regs->dma_reg.rx_ch[%d].buf_laddr = 0x%x\n", ch, regs->dma_reg.rx_ch[ch].buf_laddr);
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].desc_phy_addr = 0x%llx\n", ch, regs->dma_reg.tx_queue[ch].desc_phy_addr);
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].desc_va_addr = 0x%zx\n", ch, (size_t)(regs->dma_reg.tx_queue[ch].desc_va_addr));
#ifdef TC956X_DMA_OFFLOAD_ENABLE
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].buff_phy_addr = 0x%llx\n", ch, regs->dma_reg.tx_queue[ch].buff_phy_addr);
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].buff_va_addr = 0x%zx\n", ch, (size_t)(regs->dma_reg.tx_queue[ch].buff_va_addr));
#endif
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].tx_skbuff = 0x%zx\n", ch, (size_t)(regs->dma_reg.tx_queue[ch].tx_skbuff));
		KPRINT_DEBUG1("regs->dma_reg.tx_queue[%d].tx_skbuff_dma = 0x%zx\n", ch, (size_t)(regs->dma_reg.tx_queue[ch].tx_skbuff_dma));
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		KPRINT_DEBUG1("regs->dma_reg.rx_queue[%d].desc_phy_addr = 0x%llx\n", ch, regs->dma_reg.rx_queue[ch].desc_phy_addr);
		KPRINT_DEBUG1("regs->dma_reg.rx_queue[%d].desc_va_addr = 0x%zx\n", ch, (size_t)(regs->dma_reg.rx_queue[ch].desc_va_addr));
#ifdef TC956X_DMA_OFFLOAD_ENABLE
		KPRINT_DEBUG1("regs->dma_reg.rx_queue[%d].buff_phy_addr = 0x%llx\n", ch, regs->dma_reg.rx_queue[ch].buff_phy_addr);
		KPRINT_DEBUG1("regs->dma_reg.rx_queue[%d].buff_va_addr = 0x%zx\n", ch, (size_t)(regs->dma_reg.rx_queue[ch].buff_va_addr));
#endif
		KPRINT_DEBUG1("regs->dma_reg.rx_queue[%d].buf_pool = 0x%zx\n", ch, (size_t)(regs->dma_reg.rx_queue[ch].buf_pool));
	}

	/* MAC register dump */
	KPRINT_DEBUG1("regs->mac_reg.mac_tx_config = 0x%x\n", regs->mac_reg.mac_tx_config);
	KPRINT_DEBUG1("regs->mac_reg.mac_rx_config = 0x%x\n", regs->mac_reg.mac_rx_config);
	KPRINT_DEBUG1("regs->mac_reg.mac_pkt_filter = 0x%x\n", regs->mac_reg.mac_pkt_filter);
	KPRINT_DEBUG1("regs->mac_reg.mac_tx_rx_status = 0x%x\n", regs->mac_reg.mac_tx_rx_status);
	KPRINT_DEBUG1("regs->mac_reg.mac_debug = 0x%x\n", regs->mac_reg.mac_debug);

	/* MTL register dump */
	KPRINT_DEBUG1("regs->mtl_reg.op_mode = 0x%x\n", regs->mtl_reg.op_mode);
	KPRINT_DEBUG1("regs->mtl_reg.mtl_rxq_dma_map0 = 0x%x\n",  regs->mtl_reg.mtl_rxq_dma_map0);
	KPRINT_DEBUG1("regs->mtl_reg.mtl_rxq_dma_map1 = 0x%x\n", regs->mtl_reg.mtl_rxq_dma_map1);

	for (queue = 0; queue < MTL_MAX_TX_QUEUES; queue++) {
		KPRINT_DEBUG1("regs->mtl_reg.tx_info[%d].op_mode = 0x%x\n", queue, regs->mtl_reg.tx_info[queue].op_mode);
		KPRINT_DEBUG1("regs->mtl_reg.tx_info[%d].underflow = 0x%x\n", queue, regs->mtl_reg.tx_info[queue].underflow);
		KPRINT_DEBUG1("regs->mtl_reg.tx_info[%d].debug = 0x%x\n", queue, regs->mtl_reg.tx_info[queue].debug);
	}

	for (queue = 0; queue < MTL_MAX_RX_QUEUES; queue++) {
		KPRINT_DEBUG1("regs->mtl_reg.rx_info[%d].op_mode = 0x%x\n", queue, regs->mtl_reg.rx_info[queue].op_mode);
		KPRINT_DEBUG1("regs->mtl_reg.rx_info[%d].miss_pkt_overflow = 0x%x\n", queue, regs->mtl_reg.rx_info[queue].miss_pkt_overflow);
		KPRINT_DEBUG1("regs->mtl_reg.rx_info[%d].debug = 0x%x\n", queue, regs->mtl_reg.rx_info[queue].debug);
		KPRINT_DEBUG1("regs->mtl_reg.rx_info[%d].flow_control = 0x%x\n",  queue, regs->mtl_reg.rx_info[queue].flow_control);
	}

	/* M3 SRAM dump */
	for (ch = 0; ch < maxq; ch++) {
		KPRINT_DEBUG1("regs->m3_reg.sram_tx_pcie_addr[%d] = 0x%x\n", ch, regs->m3_reg.sram_tx_pcie_addr[ch]);
		KPRINT_DEBUG1("regs->m3_reg.sram_rx_pcie_addr[%d] = 0x%x\n", ch, regs->m3_reg.sram_rx_pcie_addr[ch]);
	}

	KPRINT_DEBUG1("regs->m3_reg.m3_fw_init_done = 0x%x\n", regs->m3_reg.m3_fw_init_done);
	KPRINT_DEBUG1("regs->m3_reg.m3_fw_exit	= 0x%x\n", regs->m3_reg.m3_fw_exit);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt0 = 0x%x\n", regs->m3_reg.m3_debug_cnt0);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt1 = 0x%x\n", regs->m3_reg.m3_debug_cnt1);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt2 = 0x%x\n", regs->m3_reg.m3_debug_cnt2);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt3 = 0x%x\n", regs->m3_reg.m3_debug_cnt3);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt4 = 0x%x\n", regs->m3_reg.m3_debug_cnt4);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt5 = 0x%x\n", regs->m3_reg.m3_debug_cnt5);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt6 = 0x%x\n", regs->m3_reg.m3_debug_cnt6);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt7 = 0x%x\n", regs->m3_reg.m3_debug_cnt7);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt8 = 0x%x\n", regs->m3_reg.m3_debug_cnt8);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt9 = 0x%x\n", regs->m3_reg.m3_debug_cnt9);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt10 = 0x%x\n", regs->m3_reg.m3_debug_cnt10);
	KPRINT_DEBUG1("regs->m3_reg.m3_watchdog_exp_cnt = 0x%x\n", regs->m3_reg.m3_watchdog_exp_cnt);
	KPRINT_DEBUG1("regs->m3_reg.m3_watchdog_monitor_cnt = 0x%x\n", regs->m3_reg.m3_watchdog_monitor_cnt);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt13 = 0x%x\n", regs->m3_reg.m3_debug_cnt13);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt14 = 0x%x\n", regs->m3_reg.m3_debug_cnt14);
	KPRINT_DEBUG1("regs->m3_reg.m3_systick_cnt_upper_value = 0x%x\n", regs->m3_reg.m3_systick_cnt_upper_value);
	KPRINT_DEBUG1("regs->m3_reg.m3_systick_cnt_lower_value = 0x%x\n", regs->m3_reg.m3_systick_cnt_lower_value);
	KPRINT_DEBUG1("regs->m3_reg.m3_tx_timeout_port0 = 0x%x\n", regs->m3_reg.m3_tx_timeout_port0);
	KPRINT_DEBUG1("regs->m3_reg.m3_tx_timeout_port1 = 0x%x\n", regs->m3_reg.m3_tx_timeout_port1);
	KPRINT_DEBUG1("regs->m3_reg.m3_debug_cnt19 = 0x%x\n", regs->m3_reg.m3_debug_cnt19);

	/* FRP Table Dump */
	KPRINT_DEBUG1("regs->rxp_cfg->npe = %d\n", regs->rxp_cfg->npe);
	KPRINT_DEBUG1("regs->rxp_cfg->nve = %d\n", regs->rxp_cfg->nve);
	for (index = 0; index <= (regs->rxp_cfg->nve); index++) {
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].match_data = 0x%x\n", index, regs->rxp_cfg->entries[index].match_data);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].match_en = 0x%x\n", index, regs->rxp_cfg->entries[index].match_en);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].af = 0x%x\n", index, regs->rxp_cfg->entries[index].af);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].rf = 0x%x\n", index, regs->rxp_cfg->entries[index].rf);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].im = 0x%x\n", index, regs->rxp_cfg->entries[index].im);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].nc = 0x%x\n", index, regs->rxp_cfg->entries[index].nc);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].frame_offset = 0x%x\n", index, regs->rxp_cfg->entries[index].frame_offset);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].ok_index = 0x%x\n", index, regs->rxp_cfg->entries[index].ok_index);
		KPRINT_DEBUG1("regs->rxp_cfg->entries[%d].dma_ch_no = 0x%x\n", index, regs->rxp_cfg->entries[index].dma_ch_no);
	}

	/* TAMAP entries */
	for (index = 0; index <= MAX_CM3_TAMAP_ENTRIES; index++) {
		KPRINT_DEBUG1("regs->tamap[%d].trsl_addr_hi = 0x%x\n", index, regs->tamap[index].trsl_addr_hi);
		KPRINT_DEBUG1("regs->tamap[%d].trsl_addr_low = 0x%x\n", index, regs->tamap[index].trsl_addr_low);
		KPRINT_DEBUG1("regs->tamap[%d].src_addr_hi = 0x%x\n", index, regs->tamap[index].src_addr_hi);
		KPRINT_DEBUG1("regs->tamap[%d].src_addr_low = 0x%x\n", index, regs->tamap[index].src_addr_low);
		KPRINT_DEBUG1("regs->tamap[%d].atr_size = 0x%x\n", index, regs->tamap[index].atr_size);
		KPRINT_DEBUG1("regs->tamap[%d].atr_impl = 0x%x\n", index, regs->tamap[index].atr_impl);
	}

	/* Driver & FW Information */
	KPRINT_DEBUG1("regs->info.driver = %s\n", regs->info.driver);
	KPRINT_DEBUG1("regs->info.version = %s\n", regs->info.version);
	KPRINT_DEBUG1("regs->info.fw_version = %s\n", regs->info.fw_version);

	/* statistics */
	for (ch = 0; ch < tx_queues_cnt; ch++) {
		KPRINT_DEBUG1("regs->stats.rx_buf_unav_irq[%d] = 0x%llx\n", ch, regs->stats.rx_buf_unav_irq[ch]);
		KPRINT_DEBUG1("regs->stats.tx_pkt_n[%d] = 0x%llx\n", ch, regs->stats.tx_pkt_n[ch]);
		KPRINT_DEBUG1("regs->stats.tx_pkt_errors_n[%d] = 0x%llx\n", ch, regs->stats.tx_pkt_errors_n[ch]);
		KPRINT_DEBUG1("regs->stats.rx_pkt_n[%d] = 0x%llx\n", ch, regs->stats.rx_pkt_n[ch]);
	}
	KPRINT_DEBUG1("regs->stats.mmc_tx_broadcastframe_g = 0x%llx\n", regs->stats.mmc_tx_broadcastframe_g);
	KPRINT_DEBUG1("regs->stats.mmc_tx_multicastframe_g = 0x%llx\n", regs->stats.mmc_tx_multicastframe_g);
	KPRINT_DEBUG1("regs->stats.mmc_tx_64_octets_gb = 0x%llx\n", regs->stats.mmc_tx_64_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_framecount_gb = 0x%llx\n", regs->stats.mmc_tx_framecount_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_65_to_127_octets_gb = 0x%llx\n", regs->stats.mmc_tx_65_to_127_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_128_to_255_octets_gb = 0x%llx\n", regs->stats.mmc_tx_128_to_255_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_256_to_511_octets_gb = 0x%llx\n", regs->stats.mmc_tx_256_to_511_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_512_to_1023_octets_gb = 0x%llx\n", regs->stats.mmc_tx_512_to_1023_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_1024_to_max_octets_gb = 0x%llx\n", regs->stats.mmc_tx_1024_to_max_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_unicast_gb = 0x%llx\n", regs->stats.mmc_tx_unicast_gb);
	KPRINT_DEBUG1("regs->stats.mmc_tx_underflow_error = 0x%llx\n", regs->stats.mmc_tx_underflow_error);
	KPRINT_DEBUG1("regs->stats.mmc_tx_framecount_g = 0x%llx\n", regs->stats.mmc_tx_framecount_g);
	KPRINT_DEBUG1("regs->stats.mmc_tx_pause_frame = 0x%llx\n", regs->stats.mmc_tx_pause_frame);
	KPRINT_DEBUG1("regs->stats.mmc_tx_vlan_frame_g = 0x%llx\n", regs->stats.mmc_tx_vlan_frame_g);
	KPRINT_DEBUG1("regs->stats.mmc_tx_lpi_us_cntr = 0x%llx\n", regs->stats.mmc_tx_lpi_us_cntr);
	KPRINT_DEBUG1("regs->stats.mmc_tx_lpi_tran_cntr = 0x%llx\n", regs->stats.mmc_tx_lpi_tran_cntr);

	KPRINT_DEBUG1("regs->stats.mmc_rx_framecount_gb = 0x%llx\n", regs->stats.mmc_rx_framecount_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_broadcastframe_g = 0x%llx\n", regs->stats.mmc_rx_broadcastframe_g);
	KPRINT_DEBUG1("regs->stats.mmc_rx_multicastframe_g = 0x%llx\n", regs->stats.mmc_rx_multicastframe_g);
	KPRINT_DEBUG1("regs->stats.mmc_rx_crc_error = 0x%llx\n", regs->stats.mmc_rx_crc_error);
	KPRINT_DEBUG1("regs->stats.mmc_rx_jabber_error = 0x%llx\n", regs->stats.mmc_rx_jabber_error);
	KPRINT_DEBUG1("regs->stats.mmc_rx_undersize_g = 0x%llx\n", regs->stats.mmc_rx_undersize_g);
	KPRINT_DEBUG1("regs->stats.mmc_rx_oversize_g = 0x%llx\n", regs->stats.mmc_rx_oversize_g);
	KPRINT_DEBUG1("regs->stats.mmc_rx_64_octets_gb = 0x%llx\n", regs->stats.mmc_rx_64_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_65_to_127_octets_gb = 0x%llx\n", regs->stats.mmc_rx_65_to_127_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_128_to_255_octets_gb = 0x%llx\n", regs->stats.mmc_rx_128_to_255_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_256_to_511_octets_gb = 0x%llx\n", regs->stats.mmc_rx_256_to_511_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_512_to_1023_octets_gb = 0x%llx\n", regs->stats.mmc_rx_512_to_1023_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_1024_to_max_octets_gb = 0x%llx\n", regs->stats.mmc_rx_1024_to_max_octets_gb);
	KPRINT_DEBUG1("regs->stats.mmc_rx_unicast_g = 0x%llx\n", regs->stats.mmc_rx_unicast_g);
	KPRINT_DEBUG1("regs->stats.mmc_rx_length_error = 0x%llx\n", regs->stats.mmc_rx_length_error);
	KPRINT_DEBUG1("regs->stats.mmc_rx_pause_frames = 0x%llx\n", regs->stats.mmc_rx_pause_frames);
	KPRINT_DEBUG1("regs->stats.mmc_rx_fifo_overflow = 0x%llx\n", regs->stats.mmc_rx_fifo_overflow);
	KPRINT_DEBUG1("regs->stats.mmc_rx_lpi_us_cntr = 0x%llx\n", regs->stats.mmc_rx_lpi_us_cntr);
	KPRINT_DEBUG1("regs->stats.mmc_rx_lpi_tran_cntr = 0x%llx\n", regs->stats.mmc_rx_lpi_tran_cntr);

	return 0;
}

extern unsigned int mac0_force_speed_mode;
extern unsigned int mac1_force_speed_mode;

static DEFINE_SPINLOCK(reg_dump_lock);
static void dump_all_reg(struct tc956xmac_priv *priv)
{
	u32 i, j;
	unsigned long flags;

	spin_lock_irqsave(&reg_dump_lock, flags);

	printk("--------------------------------------------------------\n");
	printk("CNFREG dump \n");
	for (i = 0; i <= 0x1800; i=i+4) {
		if(i <= 0x20 || i >= 0x1000)
		printk("CNFREG 0x%08x = 0x%08x\n", i, readl(priv->ioaddr + i));
	}
	printk("--------------------------------------------------------\n");
	printk("PMATOP register dump\n");
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x441b8, readl(priv->ioaddr + 0x441b8));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45888, readl(priv->ioaddr + 0x45888));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45890, readl(priv->ioaddr + 0x45890));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45898, readl(priv->ioaddr + 0x45898));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x458a0, readl(priv->ioaddr + 0x458a0));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x458a8, readl(priv->ioaddr + 0x458a8));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45080, readl(priv->ioaddr + 0x45080));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45090, readl(priv->ioaddr + 0x45090));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x45094, readl(priv->ioaddr + 0x45094));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450a4, readl(priv->ioaddr + 0x450a4));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450a8, readl(priv->ioaddr + 0x450a8));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450b8, readl(priv->ioaddr + 0x450b8));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450bc, readl(priv->ioaddr + 0x450bc));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450cc, readl(priv->ioaddr + 0x450cc));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450d0, readl(priv->ioaddr + 0x450d0));
	printk("PMATOP Reg 0x%08x = 0x%08x\n", 0x450e0, readl(priv->ioaddr + 0x450e0));
	printk("--------------------------------------------------------\n");
	printk("XPCS register dump\n");
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_SR_MII_CTRL, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL));
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_SR_XS_PCS_CTRL2, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_XS_PCS_CTRL2));
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_VR_MII_AN_CTRL, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_CTRL));
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_VR_MII_DIG_CTRL1, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_DIG_CTRL1));
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_VR_XS_PCS_DIG_CTRL1, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1));
	printk("XPCS Reg 0x%08x = 0x%08x\n", XGMAC_VR_MII_AN_INTR_STS, tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS));
	printk("--------------------------------------------------------\n");
	printk("XGMAC-MAC register dump\n");
	for (i = 0; i <= 0x810; i=i+4) {
		printk("XGMAC MAC Reg 0x%08x = 0x%08x\n", MAC_OFFSET + i, readl(priv->ioaddr + MAC_OFFSET + i));
	}
	printk("--------------------------------------------------------\n");
	printk("XGMAC-MTL register dump\n");
	for (i = 0x1000; i <= 0x10B4; i=i+4) {
		printk("XGMAC MTL Reg 0x%08x = 0x%08x\n", MAC_OFFSET + i, readl(priv->ioaddr + MAC_OFFSET + i));
	}
	for (i = 0x1100; i <= 0x1174; i=i+4) {
		for(j = 0; j < 2; j++) {
			printk("XGMAC MTL Reg 0x%08x = 0x%08x\n", MAC_OFFSET + i + (j*0x80), readl(priv->ioaddr + MAC_OFFSET + i + (j*0x80) ));
		}
	}
	printk("--------------------------------------------------------\n");
	printk("XGMAC-DMA register dump\n");
	for (i = 0x3000; i <= 0x3084; i=i+4) {
		printk("XGMAC DMA Reg 0x%08x = 0x%08x\n", MAC_OFFSET + i, readl(priv->ioaddr + MAC_OFFSET + i));
	}
	for (i = 0x3100; i <= 0x317c; i=i+4) {
		for(j = 0; j < 2; j++) {
			printk("XGMAC DMA Reg 0x%08x = 0x%08x\n", MAC_OFFSET + i + (j*0x80), readl(priv->ioaddr + MAC_OFFSET + i + (j*0x80) ));
		}
	}
	printk("--------------------------------------------------------\n");

	spin_unlock_irqrestore(&reg_dump_lock, flags);
}

#ifdef CONFIG_DEBUG_FS
/**
 * read_tc956x_status() - Debugfs read command for m3 status info
 *
 */
static ssize_t read_tc956x_m3_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 ch;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* M3 SRAM dump */
	for (ch = 0; ch < maxq; ch++) {
		printk("sram_tx_pcie_addr[%d] = 0x%x \n", ch, readl(priv->tc956x_SRAM_pci_base_addr + SRAM_TX_PCIE_ADDR_LOC + 
							(priv->port_num * TC956XMAC_CH_MAX * 4) + (ch * 4)));
		printk("sram_rx_pcie_addr[%d] = 0x%x \n", ch, readl(priv->tc956x_SRAM_pci_base_addr + SRAM_RX_PCIE_ADDR_LOC + 
							(priv->port_num * TC956XMAC_CH_MAX * 4) + (ch * 4)));
	}

	printk("m3_fw_init_done = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_INIT_DONE));
	printk("m3_fw_exit	= 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT));

	printk("m3_debug_cnt0 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT0 ))));
	printk("m3_debug_cnt1 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT1 ))));
	printk("m3_debug_cnt2 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT2 ))));
	printk("m3_debug_cnt3 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT3 ))));
	printk("m3_debug_cnt4 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT4 ))));
	printk("m3_debug_cnt5 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT5 ))));
	printk("m3_debug_cnt6 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT6 ))));
	printk("m3_debug_cnt7 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT7 ))));
	printk("m3_debug_cnt8 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT8 ))));
	printk("m3_debug_cnt9 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT9 ))));
	printk("m3_debug_cnt10 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT10 ))));
	printk("m3_watchdog_exp_cnt = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT11 ))));
	printk("m3_watchdog_monitor_cnt = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT12 ))));
	printk("m3_debug_cnt13 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT13 ))));
	printk("m3_debug_cnt14 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT14 ))));
	printk("m3_systick_cnt_upper_value = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT16 ))));
	printk("m3_systick_cnt_lower_value = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT15 ))));
	printk("m3_tx_timeout_port0 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT17 ))));
	printk("m3_tx_timeout_port1 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT18 ))));
	printk("m3_debug_cnt19 = 0x%x \n", readl(priv->tc956x_SRAM_pci_base_addr + 
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT19 ))));

	return 0;
}

static const struct file_operations fops_m3_stats = {
	.read = read_tc956x_m3_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for mac info
 *
 */
static ssize_t read_tc956x_mac_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* MAC register dump */
	printk("mac_reg.mac_tx_config = 0x%x \n", readl(priv->ioaddr + XGMAC_TX_CONFIG));
	printk("mac_reg.mac_rx_config = 0x%x \n", readl(priv->ioaddr + XGMAC_RX_CONFIG));
	printk("mac_reg.mac_pkt_filter = 0x%x \n", readl(priv->ioaddr + XGMAC_PACKET_FILTER));
	printk("mac_reg.mac_tx_rx_status = 0x%x \n", readl(priv->ioaddr + XGMAC_RX_TX_STS));
	printk("mac_reg.mac_debug = 0x%x \n", readl(priv->ioaddr + XGMAC_DEBUG));

	return 0;

}

static const struct file_operations fops_mac_stats = {
	.read = read_tc956x_mac_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for mtl status info
 *
 */
static ssize_t read_tc956x_mtl_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;
	u32 queue;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));


	/* MTL register dump */
	printk("mtl_reg.op_mode = 0x%x \n", readl(priv->ioaddr + XGMAC_MTL_OPMODE));
	printk("mtl_reg.mtl_rxq_dma_map0 = 0x%x \n", readl(priv->ioaddr + XGMAC_MTL_RXQ_DMA_MAP0));
	printk("mtl_reg.mtl_rxq_dma_map1 = 0x%x \n", readl(priv->ioaddr + XGMAC_MTL_RXQ_DMA_MAP1));

	for (queue = 0; queue < MTL_MAX_TX_QUEUES; queue++) {
		printk("mtl_reg.tx_info[%d].op_mode = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_TXQ_OPMODE(queue)));
		printk("mtl_reg.tx_info[%d].underflow = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_TXQ_UF_OFFSET(queue)));
		printk("mtl_reg.tx_info[%d].debug = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_TXQ_Debug(queue)));
	}

	for (queue = 0; queue < MTL_MAX_RX_QUEUES; queue++) {
		printk("mtl_reg.rx_info[%d].op_mode = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_RXQ_OPMODE(queue)));
		printk("mtl_reg.rx_info[%d].miss_pkt_overflow = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_RXQ_MISS_PKT_OF_CNT_OFFSET(queue)));		
		printk("mtl_reg.rx_info[%d].debug = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_RXQ_Debug(queue)));
		printk("mtl_reg.rx_info[%d].flow_control = 0x%x \n", queue, readl(priv->ioaddr + XGMAC_MTL_RXQ_FLOW_CONTROL(queue)));
	}

	return 0;
}

static const struct file_operations fops_mtl_stats = {
	.read = read_tc956x_mtl_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for dma status info
 *
 */
static ssize_t read_tc956x_dma_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 ch;
	struct tc956xmac_tx_queue *tx_q;
	struct tc956xmac_rx_queue *rx_q;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* DMA channel register dump */
	printk("dma_reg.debug_sts0 = 0x%x \n", readl(priv->ioaddr + XGMAC_DMA_DEBUG_STATUS0)); 

	for (ch = 0; ch < maxq; ch++) {
		printk("dma_reg.ch_control[%d] = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_CONTROL(ch)));
		printk("dma_reg.interrupt_enable[%d] = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_INT_EN(ch)));
		printk("dma_reg.ch_status[%d] = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(ch)));
		printk("dma_reg.debug_status[%d] = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_DBG_STATUS(ch)));
		printk("dma_reg.rxch_watchdog_timer[%d] = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(ch)));
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		printk("dma_reg.tx_ch[%d].control = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_TX_CONTROL(ch)));
		printk("dma_reg.tx_ch[%d].list_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_HADDR(ch)));
		printk("dma_reg.tx_ch[%d].list_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(ch)));
		printk("dma_reg.tx_ch[%d].ring_len = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_TX_CONTROL2(ch)));
		printk("dma_reg.tx_ch[%d].curr_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxDESC_HADDR(ch)));
		printk("dma_reg.tx_ch[%d].curr_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxDESC_LADDR(ch)));
		printk("dma_reg.tx_ch[%d].tail_ptr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_TxDESC_TAIL_LPTR(ch)));
		printk("dma_reg.tx_ch[%d].buf_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxBuff_HADDR(ch)));
		printk("dma_reg.tx_ch[%d].buf_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_TxBuff_LADDR(ch)));
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		printk("dma_reg.rx_ch[%d].control = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL(ch)));
		printk("dma_reg.rx_ch[%d].list_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(ch)));
		printk("dma_reg.rx_ch[%d].list_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(ch)));
		printk("dma_reg.rx_ch[%d].ring_len = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL2(ch)));
		printk("dma_reg.rx_ch[%d].curr_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxDESC_HADDR(ch)));
		printk("dma_reg.rx_ch[%d].curr_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxDESC_LADDR(ch)));
		printk("dma_reg.rx_ch[%d].tail_ptr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_RxDESC_TAIL_LPTR(ch)));
		printk("dma_reg.rx_ch[%d].buf_haddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxBuff_HADDR(ch)));
		printk("dma_reg.rx_ch[%d].buf_laddr = 0x%x \n", ch, readl(priv->ioaddr + XGMAC_DMA_CH_Cur_RxBuff_LADDR(ch)));
	}

	for (ch = 0; ch < tx_queues_cnt; ch++) {
		tx_q = &priv->tx_queue[ch];
		printk("dma_reg.tx_queue[%d].desc_phy_addr = 0x%lx \n", ch, (unsigned long)tx_q->dma_tx_phy);
		printk("dma_reg.tx_queue[%d].desc_va_addr = 0x%lx \n", ch, (unsigned long)tx_q->dma_tx);
		#ifdef TC956X_DMA_OFFLOAD_ENABLE
		printk("dma_reg.tx_queue[%d].buff_phy_addr = 0x%lx \n", ch, (unsigned long)tx_q->buff_tx_phy);
		printk("dma_reg.tx_queue[%d].buff_va_addr = 0x%lx \n", ch, (unsigned long)tx_q->buffer_tx_va_addr);
		#endif
		printk("dma_reg.tx_queue[%d].tx_skbuff = 0x%lx \n", ch, (unsigned long)tx_q->tx_skbuff);
		printk("dma_reg.tx_queue[%d].tx_skbuff_dma = 0x%lx \n", ch, (unsigned long)tx_q->tx_skbuff_dma);
	}

	for (ch = 0; ch < rx_queues_cnt; ch++) {
		rx_q = &priv->rx_queue[ch];
		printk("dma_reg.rx_queue[%d].desc_phy_addr = 0x%lx \n", ch, (unsigned long)rx_q->dma_rx_phy);
		printk("dma_reg.rx_queue[%d].desc_va_addr = 0x%lx \n", ch, (unsigned long)rx_q->dma_rx);
		#ifdef TC956X_DMA_OFFLOAD_ENABLE
		printk("dma_reg.rx_queue[%d].buff_phy_addr = 0x%lx \n", ch, (unsigned long)rx_q->buff_rx_phy);
		printk("dma_reg.rx_queue[%d].buff_va_addr = 0x%lx \n", ch, (unsigned long)(void *)rx_q->buffer_rx_va_addr);
		#endif
		printk("dma_reg.rx_queue[%d].buf_pool = 0x%lx \n", ch, (unsigned long)rx_q->buf_pool);
	}

	return 0;
}

static const struct file_operations fops_dma_stats = {
	.read = read_tc956x_dma_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for interrupt status info
 *
 */
static ssize_t read_tc956x_intr_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* MSI register dump */
	printk("msi_reg.msi_out_en = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num, 0)));
	printk("msi_reg.msi_mask_set = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_MASK_SET_OFFSET(priv->port_num, 0)));
	printk("msi_reg.msi_mask_clr = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->port_num, 0)));
	printk("msi_reg.int_sts = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->port_num, 0)));
	printk("msi_reg.int_raw_sts = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_INT_RAW_STS_OFFSET(priv->port_num, 0)));
	printk("msi_reg.msi_sts = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_STS_OFFSET(priv->port_num, 0)));
	printk("msi_reg.cnt_int0 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT0(priv->port_num, 0)));
	printk("msi_reg.cnt_int1 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT1(priv->port_num, 0)));
	printk("msi_reg.cnt_int2 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT2(priv->port_num, 0)));
	printk("msi_reg.cnt_int3 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT3(priv->port_num, 0)));
	printk("msi_reg.cnt_int4 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT4(priv->port_num, 0)));
	printk("msi_reg.cnt_int11 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT11(priv->port_num, 0)));
	printk("msi_reg.cnt_int12 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT12(priv->port_num, 0)));
	printk("msi_reg.cnt_int20 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT20(priv->port_num, 0)));
	printk("msi_reg.cnt_int24 = 0x%x \n", readl(priv->ioaddr + TC956X_MSI_CNT24(priv->port_num, 0)));

	/* INTC register dump */
	printk("intc_reg.intmcumask0 = 0x%x \n", readl(priv->ioaddr + INTMCUMASK0));
	printk("intc_reg.intmcumask1 = 0x%x \n", readl(priv->ioaddr + INTMCUMASK1));
	printk("intc_reg.intmcumask2 = 0x%x \n", readl(priv->ioaddr + INTMCUMASK2));

	return 0;
}

static const struct file_operations fops_intr_stats = {
	.read = read_tc956x_intr_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for configuration register status info
 *
 */
static ssize_t read_tc956x_cnfg_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* Configuration register dump */
	printk("config_reg.ncid = 0x%x \n", readl(priv->ioaddr + NCID_OFFSET));
	printk("config_reg.nclkctrl0 = 0x%x \n", readl(priv->ioaddr + NCLKCTRL0_OFFSET));
	printk("config_reg.nrstctrl0 = 0x%x \n", readl(priv->ioaddr + NRSTCTRL0_OFFSET));
	printk("config_reg.nclkctrl1 = 0x%x \n", readl(priv->ioaddr + NCLKCTRL1_OFFSET));
	printk("config_reg.nrstctrl1 = 0x%x\n", readl(priv->ioaddr + NRSTCTRL1_OFFSET));
	printk("config_reg.nemac0ctl = 0x%x \n", readl(priv->ioaddr + NEMAC0CTL_OFFSET));
	printk("config_reg.nemac1ctl = 0x%x \n", readl(priv->ioaddr + NEMAC1CTL_OFFSET));
	printk("config_reg.nemacsts = 0x%x \n", readl(priv->ioaddr + NEMACSTS_OFFSET));
	printk("config_reg.gpioi0 = 0x%x \n", readl(priv->ioaddr + GPIOI0_OFFSET));
	printk("config_reg.gpioi1 = 0x%x \n", readl(priv->ioaddr + GPIOI1_OFFSET));
	printk("config_reg.gpioe0 = 0x%x \n", readl(priv->ioaddr + GPIOE0_OFFSET));
	printk("config_reg.gpioe1 = 0x%x \n", readl(priv->ioaddr + GPIOE1_OFFSET));

	return 0;
}

static const struct file_operations fops_cnfg_stats = {
	.read = read_tc956x_cnfg_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for other status info like TAMP, FRP Table, MMC counters, Version Info
 *
 */
static ssize_t read_tc956x_other_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 ch, table_entry, reg = 0;
	u8 fw_version_str[32];
	struct tc956x_version *fw_version;
	struct tc956xmac_rx_parser_cfg *rxp_cfg;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}

	/* PCIe register dump */
	printk("pcie_reg.rsc_mng_id = 0x%x \n",readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + RSCMNG_ID_REG));

	/* TAMAP Information */
	for(table_entry = 0; table_entry <= MAX_CM3_TAMAP_ENTRIES; table_entry++) {
		printk("TAMAP table %d, trsl_addr_hi = 0x%x \n", table_entry, (u32)readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry)));
		printk("TAMAP table %d, trsl_addr_low = 0x%x \n", table_entry, (u32) readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry)));
		printk("TAMAP table %d, src_addr_hi = 0x%x \n", table_entry, (u32)readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_SRC_ADDR_HI(0, table_entry)));
		printk("TAMAP table %d, src_addr_low = 0x%x \n", table_entry, (u32)(readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_SRC_LO_MASK));
		printk("TAMAP table %d, atr_size = 0x%x \n", table_entry,  (u32)((readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_ATR_SIZE_MASK) >> 1));
		printk("TAMAP table %d, atr_impl = 0x%x \n", table_entry,  (u32)(readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + 
						TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_ATR_IMPL));
	}

	/* Driver & FW Information */
	printk("info.driver = %s \n", TC956X_RESOURCE_NAME);
	printk("info.version = %s \n", DRV_MODULE_VERSION);

	reg = readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_DBG_VER_START);
	fw_version = (struct tc956x_version *)(&reg);
	scnprintf(fw_version_str, sizeof(fw_version_str), "FW Version %s_%d.%d-%d", (fw_version->rel_dbg == 'D')?"DBG":"REL",
					fw_version->major, fw_version->minor, fw_version->sub_minor);
	printk("info.fw_version = %s\n", fw_version_str);

	/* Updating statistics */
	tc956xmac_mmc_read(priv, priv->mmcaddr, &priv->mmc);
	for (ch = 0; ch < tx_queues_cnt; ch++) {
		printk("rx_buf_unav_irq[%d] = 0x%llx \n", ch, priv->xstats.rx_buf_unav_irq[ch]);
		printk("tx_pkt_n[%d] = 0x%llx \n", ch, priv->xstats.tx_pkt_n[ch]);
		printk("tx_pkt_errors_n[%d] = 0x%llx \n", ch, priv->xstats.tx_pkt_errors_n[ch]);
		printk("rx_pkt_n[%d] = 0x%llx \n", ch, priv->xstats.rx_pkt_n[ch]);
	}
	printk("mmc_tx_broadcastframe_g = 0x%llx \n" , priv->mmc.mmc_tx_broadcastframe_g);
	printk("mmc_tx_multicastframe_g = 0x%llx \n" , priv->mmc.mmc_tx_multicastframe_g);
	printk("mmc_tx_64_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_64_octets_gb);
	printk("mmc_tx_framecount_gb = 0x%llx \n" , priv->mmc.mmc_tx_framecount_gb);
	printk("mmc_tx_65_to_127_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_65_to_127_octets_gb);
	printk("mmc_tx_128_to_255_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_128_to_255_octets_gb);
	printk("mmc_tx_256_to_511_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_256_to_511_octets_gb);
	printk("mmc_tx_512_to_1023_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_512_to_1023_octets_gb);
	printk("mmc_tx_1024_to_max_octets_gb = 0x%llx \n" , priv->mmc.mmc_tx_1024_to_max_octets_gb);
	printk("mmc_tx_unicast_gb = 0x%llx \n" , priv->mmc.mmc_tx_unicast_gb);
	printk("mmc_tx_underflow_error = 0x%llx \n" , priv->mmc.mmc_tx_underflow_error);
	printk("mmc_tx_framecount_g = 0x%llx \n" , priv->mmc.mmc_tx_framecount_g);
	printk("mmc_tx_pause_frame = 0x%llx \n" , priv->mmc.mmc_tx_pause_frame);
	printk("mmc_tx_vlan_frame_g = 0x%llx \n" , priv->mmc.mmc_tx_vlan_frame_g);
	printk("mmc_tx_lpi_us_cntr = 0x%llx \n" , priv->mmc.mmc_tx_lpi_us_cntr);
	printk("mmc_tx_lpi_tran_cntr = 0x%llx \n" , priv->mmc.mmc_tx_lpi_tran_cntr);

	printk("mmc_rx_framecount_gb = 0x%llx \n" , priv->mmc.mmc_rx_framecount_gb);
	printk("mmc_rx_broadcastframe_g = 0x%llx \n" , priv->mmc.mmc_rx_broadcastframe_g);
	printk("mmc_rx_multicastframe_g = 0x%llx \n" , priv->mmc.mmc_rx_multicastframe_g);
	printk("mmc_rx_crc_error = 0x%llx \n" , priv->mmc.mmc_rx_crc_error);
	printk("mmc_rx_jabber_error = 0x%llx \n" , priv->mmc.mmc_rx_jabber_error);
	printk("mmc_rx_undersize_g = 0x%llx \n" , priv->mmc.mmc_rx_undersize_g);
	printk("mmc_rx_oversize_g = 0x%llx \n" , priv->mmc.mmc_rx_oversize_g);
	printk("mmc_rx_64_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_64_octets_gb);
	printk("mmc_rx_65_to_127_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_65_to_127_octets_gb);
	printk("mmc_rx_128_to_255_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_128_to_255_octets_gb);
	printk("mmc_rx_256_to_511_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_256_to_511_octets_gb);
	printk("mmc_rx_512_to_1023_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_512_to_1023_octets_gb);
	printk("mmc_rx_1024_to_max_octets_gb = 0x%llx \n" , priv->mmc.mmc_rx_1024_to_max_octets_gb);
	printk("mmc_rx_unicast_g = 0x%llx \n" , priv->mmc.mmc_rx_unicast_g);
	printk("mmc_rx_length_error = 0x%llx \n" , priv->mmc.mmc_rx_length_error);
	printk("mmc_rx_pause_frames = 0x%llx \n" , priv->mmc.mmc_rx_pause_frames);
	printk("mmc_rx_fifo_overflow = 0x%llx \n" , priv->mmc.mmc_rx_fifo_overflow);
	printk("mmc_rx_lpi_us_cntr = 0x%llx \n" , priv->mmc.mmc_rx_lpi_us_cntr);
	printk("mmc_rx_lpi_tran_cntr = 0x%llx \n" , priv->mmc.mmc_rx_lpi_tran_cntr);

	/* Reading FRP Table information from Registers */
	rxp_cfg = (struct tc956xmac_rx_parser_cfg *)&priv->plat->rxp_cfg;
	printk("rxp_cfg->nve = 0x%x \n" , (readl(priv->ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS) & 0xFF));
	printk("rxp_cfg->npe = 0x%x \n" , ((readl(priv->ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS) >> 16) & 0xFF));
	for(table_entry = 0; table_entry <= (rxp_cfg->nve); table_entry++) {
		dwxgmac2_rx_parser_read_entry(priv,
		&(rxp_cfg->entries[table_entry]), table_entry);

		printk("rxp_cfg->entries[%d].match_data = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].match_data);
		printk("rxp_cfg->entries[%d].match_en = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].match_en);
		printk("rxp_cfg->entries[%d].af = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].af);
		printk("rxp_cfg->entries[%d].rf = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].rf);
		printk("rxp_cfg->entries[%d].im = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].im);
		printk("rxp_cfg->entries[%d].nc = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].nc);
		printk("rxp_cfg->entries[%d].frame_offset = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].frame_offset);
		printk("rxp_cfg->entries[%d].ok_index = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].ok_index);
		printk("rxp_cfg->entries[%d].dma_ch_no = 0x%x\n", table_entry, rxp_cfg->entries[table_entry].dma_ch_no);
	}

	return 0;
}

static const struct file_operations fops_other_stats = {
	.read = read_tc956x_other_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * read_tc956x_status() - Debugfs read command for dumping all registers of MAC, MTL, DMA...
 *
 */
static ssize_t read_tc956x_reg_dump_status(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct tc956xmac_priv *priv = file->private_data;

	if (!priv) {
		pr_err(" %s  ERROR: Invalid private data pointer\n",__func__);
		return -EINVAL;
	}
	dump_all_reg(priv);

	return 0;
}

static const struct file_operations fops_reg_dump_stats = {
	.read = read_tc956x_reg_dump_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/**
 * tc956xmac_create_debugfs() - API to create debugfs node
 * for debugging.
 *
 * IN: Network device structure: TC956x network interface structure specific to port.
 * OUT: 0 on success and -1 on failure
 */
int tc956xmac_create_debugfs(struct net_device *net_device)
{

	struct tc956xmac_priv *priv;
	static struct dentry *stats = NULL;

	if(!net_device) {
		pr_err("%s: ERROR: Invalid netdevice pointer \n", __func__);
		return -1;
	}

	priv = netdev_priv(net_device);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -1;
	}

	if(RM_PF0_ID == priv->port_num) {
		priv->debugfs_dir = debugfs_create_dir("tc956x_port0_debug", NULL);
		if (!priv->debugfs_dir) {
			pr_err( "Cannot create TC956x debugfs dir for port-0 %ld \n", (long)priv->debugfs_dir);
			return -1;
		} else {
			printk( "Created TC956x debugfs dir for port-0 \n");
		}
	} else {
		priv->debugfs_dir = debugfs_create_dir("tc956x_port1_debug", NULL);
		if (!priv->debugfs_dir) {
			pr_err( "Cannot create TC956x debugfs dir for port-1 %ld \n", (long)priv->debugfs_dir);
			return -1;
		} else {
			printk( "Created TC956x debugfs dir for port-1 \n");
		}
	}

	stats = debugfs_create_file("m3_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_m3_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x m3 debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("mac_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_mac_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x mac debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("mtl_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_mtl_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x mtl debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("dma_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_dma_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x dma debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("interrupt_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_intr_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x interrupt debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("config_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_cnfg_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x cnfg debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("other_stats", S_IRUSR, priv->debugfs_dir,
				priv, &fops_other_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x other debugfs stats file%ld \n", (long)stats);
		goto fail;
	}

	stats = debugfs_create_file("reg_dump", S_IRUSR, priv->debugfs_dir,
				priv, &fops_reg_dump_stats);
	if (!stats || IS_ERR(stats)) {
		pr_err( "Cannot create TC956x registers dump debugfs file%ld \n", (long)stats);
		goto fail;
	}

	return 0;

fail:
	debugfs_remove_recursive(priv->debugfs_dir);
	return -1;

}

/**
 * tc956xmac_cleanup_debugfs() - API to cleanup debugfs node
 * for debugging.
 *
 * IN: Network device structure: TC956x network interface structure specific to port.
 * OUT: 0 on success and -1 on failure
 */
int tc956xmac_cleanup_debugfs(struct net_device *net_device)
{
	struct tc956xmac_priv *priv;

	if(!net_device) {
		pr_err("%s: ERROR: Invalid netdevice pointer \n", __func__);
		return -1;
	}

	priv = netdev_priv(net_device);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -1;
	}

	if (priv->debugfs_dir)
		debugfs_remove_recursive(priv->debugfs_dir);

	printk("TC956x port-%d, debugfs Deleted Successfully \n", priv->port_num);
	return 0;

}
#endif/* End of CONFIG_DEBUG_FS */

/**
 *  tc956x_GPIO_OutputConfigPin - to configure GPIO as output and write the value
 *  @priv: driver private structure
 *  @gpio_pin: GPIO pin number
 *  @out_value : value to write to the GPIO pin. Can be 0 or 1
 *  @remarks : Only GPIO0- GPIO06, GPI010-GPIO12 are allowed
 */
int tc956x_GPIO_OutputConfigPin(struct tc956xmac_priv *priv, u32 gpio_pin, u8 out_value)
{
	u32 config, val;

	/* Only GPIO0- GPIO06, GPI010-GPIO12 are allowed */
	switch (gpio_pin) {
		case GPIO_00:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_00;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_00_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_01:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_01;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_01_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_02:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_02;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_02_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_03:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_03;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_03_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_04:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_04;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_04_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_05:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_05;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_05_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_06:
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= ~NFUNCEN4_GPIO_06;
			val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_06_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
			break;
		case GPIO_10:
			val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
			val &= ~NFUNCEN5_GPIO_10;
			val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_10_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
			break;
		case GPIO_11:
			val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
			val &= ~NFUNCEN5_GPIO_11;
			val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_11_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
			break;
		case GPIO_12:
			val = readl(priv->ioaddr + NFUNCEN6_OFFSET);
			val &= ~NFUNCEN6_GPIO_12;
			val |= (NFUNCEN_FUNC0 << NFUNCEN6_GPIO_12_SHIFT);
			writel(val, priv->ioaddr + NFUNCEN6_OFFSET);
			break;
		default:
			netdev_err(priv->dev, "Invalid GPIO pin - %d\n", gpio_pin);
			return -EPERM;
	}

	priv->saved_gpio_config[gpio_pin].config = 1;

	/* Write data to GPIO pin */
	if (gpio_pin < GPIO_32) {
		config = 1 << gpio_pin;
		val = readl(priv->ioaddr + GPIOO0_OFFSET);
		val &= ~config;
		if (out_value)
			val |= config;

		writel(val, priv->ioaddr + GPIOO0_OFFSET);
	}  else {
		config = 1 << (gpio_pin - GPIO_32);
		val = readl(priv->ioaddr + GPIOO1_OFFSET);
		val &= ~config;
		if (out_value)
			val |= config;

		writel(val, priv->ioaddr + GPIOO1_OFFSET);
	}

	priv->saved_gpio_config[gpio_pin].out_val = out_value;

	/* Configure the GPIO pin in output direction */
	if (gpio_pin < GPIO_32) {
		config = ~(1 << gpio_pin);
		val = readl(priv->ioaddr + GPIOE0_OFFSET);
		writel(val & config, priv->ioaddr + GPIOE0_OFFSET);
	} else {
		config = ~(1 << (gpio_pin - GPIO_32));
		val = readl(priv->ioaddr + GPIOE1_OFFSET);
		writel(val & config, priv->ioaddr + GPIOE1_OFFSET);
	}

	return 0;
}

/**
 *  tc956x_gpio_restore_configuration - to restore the saved configuration of GPIO
 *  @priv: driver private structure
 *  @remarks : Only GPIO0- GPIO06, GPI010-GPIO12 are allowed
 */
int tc956x_gpio_restore_configuration(struct tc956xmac_priv *priv)
{
	u32 config, val, gpio_pin, out_value;

	DBGPR_FUNC(priv->device, "-->%s", __func__);

	for (gpio_pin = 0; gpio_pin <= GPIO_12; gpio_pin++) {

		/* Restore only the GPIOs which were configured/saved */
		if (!(priv->saved_gpio_config[gpio_pin].config))
			continue;

		DBGPR_FUNC(priv->device, "%s : Restoring GPIO configuration for pin: %d, val: %d",
				__func__, gpio_pin, priv->saved_gpio_config[gpio_pin].out_val);

		/* Only GPIO0- GPIO06, GPI010-GPIO12 are allowed */
		switch (gpio_pin) {
			case GPIO_00:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_00;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_00_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_01:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_01;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_01_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_02:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_02;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_02_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_03:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_03;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_03_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_04:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_04;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_04_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_05:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_05;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_05_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_06:
				val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
				val &= ~NFUNCEN4_GPIO_06;
				val |= (NFUNCEN_FUNC0 << NFUNCEN4_GPIO_06_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
				break;
			case GPIO_10:
				val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
				val &= ~NFUNCEN5_GPIO_10;
				val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_10_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
				break;
			case GPIO_11:
				val = readl(priv->ioaddr + NFUNCEN5_OFFSET);
				val &= ~NFUNCEN5_GPIO_11;
				val |= (NFUNCEN_FUNC0 << NFUNCEN5_GPIO_11_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN5_OFFSET);
				break;
			case GPIO_12:
				val = readl(priv->ioaddr + NFUNCEN6_OFFSET);
				val &= ~NFUNCEN6_GPIO_12;
				val |= (NFUNCEN_FUNC0 << NFUNCEN6_GPIO_12_SHIFT);
				writel(val, priv->ioaddr + NFUNCEN6_OFFSET);
				break;
			default :
				netdev_err(priv->dev, "Invalid GPIO pin - %d\n", gpio_pin);
				return -EPERM;
		}

		out_value = priv->saved_gpio_config[gpio_pin].out_val;

		/* Write data to GPIO pin */
		if(gpio_pin < GPIO_32) {
			config = 1 << gpio_pin;
			val = readl(priv->ioaddr + GPIOO0_OFFSET);
			val &= ~config;
			if(out_value)
				val |= config;

			writel(val, priv->ioaddr + GPIOO0_OFFSET);
		}  else {
			config = 1 << (gpio_pin - GPIO_32);
			val = readl(priv->ioaddr + GPIOO1_OFFSET);
			val &= ~config;
			if(out_value)
				val |= config;

			writel(val, priv->ioaddr + GPIOO1_OFFSET);
		}

		/* Configure the GPIO pin in output direction */
		if(gpio_pin < GPIO_32) {
			config = ~(1 << gpio_pin) ;
			val = readl(priv->ioaddr + GPIOE0_OFFSET);
			writel(val & config, priv->ioaddr + GPIOE0_OFFSET);
		} else {
			config = ~(1 << (gpio_pin - GPIO_32)) ;
			val = readl(priv->ioaddr + GPIOE1_OFFSET);
			writel(val & config, priv->ioaddr + GPIOE1_OFFSET);
		}
	}
	return 0;
}

#ifdef TC956X_SRIOV_PF
/**
 *  tc956xmac_wol_interrupt - ISR to handle WoL PHY interrupt
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 */
static irqreturn_t tc956xmac_wol_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	/* Set flag to clear interrupt after resume */
	DBGPR_FUNC(priv->device, "%s\n", __func__);
	/* Set flag to indicate WOL interrupt trigger */
	priv->tc956xmac_pm_wol_interrupt = true;
	return IRQ_HANDLED;
}
#endif

/**
 * tc956xmac_verify_args - verify the driver parameters.
 * Description: it checks the driver parameters and set a default in case of
 * errors.
 */
static void tc956xmac_verify_args(void)
{
	if (unlikely(watchdog < 0))
		watchdog = TX_TIMEO;
	if (unlikely((buf_sz < DEFAULT_BUFSIZE) || (buf_sz > BUF_SIZE_16KiB)))
		buf_sz = DEFAULT_BUFSIZE;
	if (unlikely(flow_ctrl > 1))
		flow_ctrl = FLOW_AUTO;
	else if (likely(flow_ctrl < 0))
		flow_ctrl = FLOW_OFF;
	if (unlikely((pause < 0) || (pause > 0xffff)))
		pause = PAUSE_TIME;
	if (eee_timer < 0)
		eee_timer = TC956XMAC_DEFAULT_LPI_TIMER;
}

#ifdef TC956X_SRIOV_PF
#ifdef TC956X_DYNAMIC_LOAD_CBS
static void tc956xmac_set_cbs_speed(struct tc956xmac_priv *priv)
{
	u32 queue_idx;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	for (queue_idx = CLASS_B_CH ; queue_idx <= CLASS_CDT_CH; queue_idx++) {
		if (priv->plat->tx_queues_cfg[queue_idx].mode_to_use == MTL_QUEUE_AVB)
		{
			if (priv->speed == SPEED_100) {
				priv->plat->tx_queues_cfg[queue_idx].send_slope =
										priv->cbs_speed100_cfg[queue_idx].send_slope;
				priv->plat->tx_queues_cfg[queue_idx].idle_slope =
										priv->cbs_speed100_cfg[queue_idx].idle_slope;
				priv->plat->tx_queues_cfg[queue_idx].high_credit =
										priv->cbs_speed100_cfg[queue_idx].high_credit;
				priv->plat->tx_queues_cfg[queue_idx].low_credit =
										priv->cbs_speed100_cfg[queue_idx].low_credit;
			} else if (priv->speed == SPEED_1000) {
				priv->plat->tx_queues_cfg[queue_idx].send_slope =
										priv->cbs_speed1000_cfg[queue_idx].send_slope;
				priv->plat->tx_queues_cfg[queue_idx].idle_slope =
										priv->cbs_speed1000_cfg[queue_idx].idle_slope;
				priv->plat->tx_queues_cfg[queue_idx].high_credit =
										priv->cbs_speed1000_cfg[queue_idx].high_credit;
				priv->plat->tx_queues_cfg[queue_idx].low_credit =
										priv->cbs_speed1000_cfg[queue_idx].low_credit;
			} else if (priv->speed == SPEED_10000) {
				priv->plat->tx_queues_cfg[queue_idx].send_slope =
										priv->cbs_speed10000_cfg[queue_idx].send_slope;
				priv->plat->tx_queues_cfg[queue_idx].idle_slope =
										priv->cbs_speed10000_cfg[queue_idx].idle_slope;
				priv->plat->tx_queues_cfg[queue_idx].high_credit =
										priv->cbs_speed10000_cfg[queue_idx].high_credit;
				priv->plat->tx_queues_cfg[queue_idx].low_credit =
										priv->cbs_speed10000_cfg[queue_idx].low_credit;
			} else if (priv->speed == SPEED_2500) {
				priv->plat->tx_queues_cfg[queue_idx].send_slope =
										priv->cbs_speed2500_cfg[queue_idx].send_slope;
				priv->plat->tx_queues_cfg[queue_idx].idle_slope =
										priv->cbs_speed2500_cfg[queue_idx].idle_slope;
				priv->plat->tx_queues_cfg[queue_idx].high_credit =
										priv->cbs_speed2500_cfg[queue_idx].high_credit;
				priv->plat->tx_queues_cfg[queue_idx].low_credit =
										priv->cbs_speed2500_cfg[queue_idx].low_credit;
			} else if (priv->speed == SPEED_5000) {
				priv->plat->tx_queues_cfg[queue_idx].send_slope =
										priv->cbs_speed5000_cfg[queue_idx].send_slope;
				priv->plat->tx_queues_cfg[queue_idx].idle_slope =
										priv->cbs_speed5000_cfg[queue_idx].idle_slope;
				priv->plat->tx_queues_cfg[queue_idx].high_credit =
										priv->cbs_speed5000_cfg[queue_idx].high_credit;
				priv->plat->tx_queues_cfg[queue_idx].low_credit =
										priv->cbs_speed5000_cfg[queue_idx].low_credit;
			}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
		tc956xmac_config_cbs(priv, priv->hw, priv->plat->tx_queues_cfg[queue_idx].send_slope,
					priv->plat->tx_queues_cfg[queue_idx].idle_slope,
					priv->plat->tx_queues_cfg[queue_idx].high_credit,
					priv->plat->tx_queues_cfg[queue_idx].low_credit,
					queue_idx);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
		}
	}
}
#endif /* DYNAMIC_LOAD_CBS */
#endif
/**
 * tc956xmac_disable_all_queues - Disable all queues
 * @priv: driver private structure
 */
static void tc956xmac_disable_all_queues(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;

		if (queue < rx_queues_cnt)
#else
		if (queue < rx_queues_cnt && priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
#endif
			napi_disable(&ch->rx_napi);
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;

		if (queue < tx_queues_cnt)
#else
		if (queue < tx_queues_cnt && priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
#endif
			napi_disable(&ch->tx_napi);
	}
}

/**
 * tc956xmac_enable_all_queues - Enable all queues
 * @priv: driver private structure
 */
static void tc956xmac_enable_all_queues(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

#ifdef TC956X_SRIOV_PF
		if (queue < rx_queues_cnt) {

			if (priv->plat->rx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;

		if (queue < rx_queues_cnt) {
#else
		if (queue < rx_queues_cnt && priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW) {
#endif
			napi_enable(&ch->rx_napi);
		}
#ifdef TC956X_SRIOV_PF
		if (queue < tx_queues_cnt) {
			if (priv->plat->tx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;

		if (queue < tx_queues_cnt) {
#else
		if (queue < tx_queues_cnt && priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW) {
#endif
			napi_enable(&ch->tx_napi);
		}
	}
}

/**
 * tc956xmac_stop_all_queues - Stop all queues
 * @priv: driver private structure
 */
static void tc956xmac_stop_all_queues(struct tc956xmac_priv *priv)
{
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < tx_queues_cnt; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}
}

/**
 * tc956xmac_start_all_queues - Start all queues
 * @priv: driver private structure
 */
static void tc956xmac_start_all_queues(struct tc956xmac_priv *priv)
{
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < tx_queues_cnt; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		netif_tx_start_queue(netdev_get_tx_queue(priv->dev, queue));
	}
}

#ifdef TC956X_UNSUPPORTED_UNTESTED
static void tc956xmac_service_event_schedule(struct tc956xmac_priv *priv)
{
	if (!test_bit(TC956XMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(TC956XMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->wq, &priv->service_task);
}
#endif

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
/**
 * tc956xmac_service_mbx_event_schedule - Schedule work queue
 * @priv: driver private structure
 */
void tc956xmac_service_mbx_event_schedule(struct tc956xmac_priv *priv)
{
	if (!test_bit(TC956XMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(TC956XMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->mbx_wq, &priv->service_mbx_task);
}
#endif
#ifdef TC956X_SRIOV_VF
/**
 * tc956xmac_mailbox_service_event_schedule - Schedule work queue
 * @priv: driver private structure
 */
void tc956xmac_mailbox_service_event_schedule(struct tc956xmac_priv *priv)
{
	if (!test_bit(TC956XMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(TC956XMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->mbx_wq, &priv->mbx_service_task);
}
#endif
static void tc956xmac_global_err(struct tc956xmac_priv *priv)
{
#ifdef TC956X_SRIOV_VF
	netdev_alert(priv->dev, "%s PF %d VF %d disabling carrier\n", __func__, priv->fn_id_info.pf_no, priv->fn_id_info.vf_no);
#endif
	netif_carrier_off(priv->dev);
	set_bit(TC956XMAC_RESET_REQUESTED, &priv->state);
#ifdef TC956X_UNSUPPORTED_UNTESTED
	tc956xmac_service_event_schedule(priv);
#endif
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifndef TC956X_SRIOV_VF
/**
 * tc956xmac_clk_csr_set - dynamically set the MDC clock
 * @priv: driver private structure
 * Description: this is to dynamically set the MDC clock according to the csr
 * clock input.
 * Note:
 *	If a specific clk_csr value is passed from the platform
 *	this means that the CSR Clock Range selection cannot be
 *	changed at run-time and it is fixed (as reported in the driver
 *	documentation). Viceversa the driver will try to set the MDC
 *	clock dynamically according to the actual clock input.
 */
static void tc956xmac_clk_csr_set(struct tc956xmac_priv *priv)
{
	u32 clk_rate;

	clk_rate = clk_get_rate(priv->plat->tc956xmac_clk);

	/* Platform provided default clk_csr would be assumed valid
	 * for all other cases except for the below mentioned ones.
	 * For values higher than the IEEE 802.3 specified frequency
	 * we can not estimate the proper divider as it is not known
	 * the frequency of clk_csr_i. So we do not change the default
	 * divider.
	 */
	if (!(priv->clk_csr & MAC_CSR_H_FRQ_MASK)) {
		if (clk_rate < CSR_F_35M)
			priv->clk_csr = TC956XMAC_CSR_20_35M;
		else if ((clk_rate >= CSR_F_35M) && (clk_rate < CSR_F_60M))
			priv->clk_csr = TC956XMAC_CSR_35_60M;
		else if ((clk_rate >= CSR_F_60M) && (clk_rate < CSR_F_100M))
			priv->clk_csr = TC956XMAC_CSR_60_100M;
		else if ((clk_rate >= CSR_F_100M) && (clk_rate < CSR_F_150M))
			priv->clk_csr = TC956XMAC_CSR_100_150M;
		else if ((clk_rate >= CSR_F_150M) && (clk_rate < CSR_F_250M))
			priv->clk_csr = TC956XMAC_CSR_150_250M;
		else if ((clk_rate >= CSR_F_250M) && (clk_rate < CSR_F_300M))
			priv->clk_csr = TC956XMAC_CSR_250_300M;
	}

	if (priv->plat->has_sun8i) {
		if (clk_rate > 160000000)
			priv->clk_csr = 0x03;
		else if (clk_rate > 80000000)
			priv->clk_csr = 0x02;
		else if (clk_rate > 40000000)
			priv->clk_csr = 0x01;
		else
			priv->clk_csr = 0;
	}

	if (priv->plat->has_xgmac) {
		if (clk_rate > 400000000)
			priv->clk_csr = 0x5;
		else if (clk_rate > 350000000)
			priv->clk_csr = 0x4;
		else if (clk_rate > 300000000)
			priv->clk_csr = 0x3;
		else if (clk_rate > 250000000)
			priv->clk_csr = 0x2;
		else if (clk_rate > 150000000)
			priv->clk_csr = 0x1;
		else
			priv->clk_csr = 0x0;
	}
}
#endif /* TC956X_SRIOV_VF */
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static void print_pkt(unsigned char *buf, int len)
{
	pr_debug("len = %d byte, buf addr: 0x%p\n", len, buf);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len);
}

static inline u32 tc956xmac_tx_avail(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	u32 avail;

	if (tx_q->dirty_tx > tx_q->cur_tx)
		avail = tx_q->dirty_tx - tx_q->cur_tx - 1;
	else
		avail = DMA_TX_SIZE - tx_q->cur_tx + tx_q->dirty_tx - 1;

	return avail;
}

/**
 * tc956xmac_rx_dirty - Get RX queue dirty
 * @priv: driver private structure
 * @queue: RX queue index
 */
static inline u32 tc956xmac_rx_dirty(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	u32 dirty;

	if (rx_q->dirty_rx <= rx_q->cur_rx)
		dirty = rx_q->cur_rx - rx_q->dirty_rx;
	else
		dirty = DMA_RX_SIZE - rx_q->dirty_rx + rx_q->cur_rx;

	return dirty;
}

/**
 * tc956xmac_enable_eee_mode - check and enter in LPI mode
 * @priv: driver private structure
 * Description: this function is to verify and enter in LPI mode in case of
 * EEE.
 */
static void tc956xmac_enable_eee_mode(struct tc956xmac_priv *priv)
{
#ifdef TC956X_SRIOV_PF
	tc956xmac_set_eee_mode(priv, priv->hw,
			priv->plat->en_tx_lpi_clockgating);
#endif
}

/**
 * tc956xmac_disable_eee_mode - disable and exit from LPI mode
 * @priv: driver private structure
 * Description: this function is to exit and disable EEE in case of
 * LPI state is true. This is called by the xmit.
 */
void tc956xmac_disable_eee_mode(struct tc956xmac_priv *priv)
{
	tc956xmac_reset_eee_mode(priv, priv->hw);
}

/**
 * tc956xmac_eee_init - init EEE
 * @priv: driver private structure
 * Description:
 *  if the GMAC supports the EEE (from the HW cap reg) and the phy device
 *  can also manage EEE, this function enable the LPI state and start related
 *  timer.
 */
bool tc956xmac_eee_init(struct tc956xmac_priv *priv)
{
#ifdef EEE_MAC_CONTROLLED_MODE
	int value;
#endif

	/* Using PCS we cannot dial with the phy registers at this stage
	 * so we do not support extra feature like EEE.
	 */
	if ((priv->hw->pcs == TC956XMAC_PCS_RGMII) ||
	    (priv->hw->pcs == TC956XMAC_PCS_TBI) ||
	    (priv->hw->pcs == TC956XMAC_PCS_RTBI))
		return false;

	/* Check if MAC core supports the EEE feature. */
	if (!priv->dma_cap.eee)
		return false;

	mutex_lock(&priv->lock);

	tc956xmac_enable_eee_mode(priv);
#ifdef EEE_MAC_CONTROLLED_MODE
	tc956xmac_set_eee_timer(priv, priv->hw, TC956XMAC_LIT_LS, TC956XMAC_TWT_LS);
	value = TC956XMAC_TIC_1US_CNTR;
	writel(value, priv->ioaddr + XGMAC_LPI_1US_Tic_Counter);
	value = readl(priv->ioaddr + XGMAC_LPI_Auto_Entry_Timer);
	/* Setting LPIET bit [19...3] */
	value &= ~(XGMAC_LPIET);
	/* LPI Entry timer is in the units of 8 micro second granularity considering last reserved 2:0 bits as zero
	 * So mask the last 3 bits
	 */
	value |= (priv->tx_lpi_timer & XGMAC_LPIET);
	DBGPR_FUNC(priv->device, "%s Writing LPI timer value of [%d]\n", __func__, value);
	writel(value, priv->ioaddr + XGMAC_LPI_Auto_Entry_Timer);
#endif
	mutex_unlock(&priv->lock);
	netdev_dbg(priv->dev, "Energy-Efficient Ethernet initialized\n");
	return true;
}

/* tc956xmac_get_tx_hwtstamp - get HW TX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read timestamp from the descriptor & pass it to stack.
 * and also perform some sanity checks.
 */
static void tc956xmac_get_tx_hwtstamp(struct tc956xmac_priv *priv,
				   struct dma_desc *p, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps shhwtstamp;
	bool found = false;
	u64 ns = 0;
#if defined(TX_LOGGING_TRACE)
	static unsigned int ccnt1, ccnt2, ccnt3;
	u32 qno = skb_get_queue_mapping(skb);
#endif

#ifdef PKT_RATE_DBG
	static unsigned int count;
	static u64 prev_ns;
	u64 rate;
#endif

	if (!priv->hwts_tx_en)
		return;

#if !defined(TX_LOGGING_TRACE)
	/* exit if skb doesn't support hw tstamp */
	if (likely(!skb || !(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)))
		return;
#endif
	/* check tx tstamp status */
	if (tc956xmac_get_tx_timestamp_status(priv, p)) {
		tc956xmac_get_timestamp(priv, p, priv->adv_ts, &ns);
		found = true;
	} else if (!tc956xmac_get_mac_tx_timestamp(priv, priv->hw, &ns)) {
		found = true;
	}

	if (found) {
		memset(&shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp.hwtstamp = ns_to_ktime(ns);

		netdev_dbg(priv->dev, "get valid TX hw timestamp %llu\n", ns);
		/* pass tstamp to stack */
		skb_tstamp_tx(skb, &shhwtstamp);
	}

#if defined(TX_LOGGING_TRACE)
	if (found) {

#ifdef PKT_RATE_DBG
		if (count % 8000 == 0) {
			rate = (8000*1000*1000000000ULL)/(unsigned int)(ns - prev_ns);
#ifdef FPE
			KPRINT_INFO("Tx FPE mmc_tx_frag_cnt:%d\n", priv->mmc.mmc_tx_fpe_fragment_cntr);
#endif
			prev_ns = ns;
		}
		count++;
#endif

#ifdef FPE
		priv->mmc.mmc_tx_fpe_fragment_cntr += readl(priv->mmcaddr + MMC_XGMAC_TX_FPE_FRAG);
#endif
#ifdef TC956X_SRIOV_VF
		if ((qno == priv->plat->avb_class_a_ch_no)
			&& (priv->plat->best_effort_ch_no != priv->plat->avb_class_a_ch_no)) {/* For AVB */
#else
		if (qno == AVB_CLASS_A_TX_CH) {/* For AVB */
#endif
			if (skb->data[20] == 0)
				ccnt1++;

#ifdef FPE
		trace_printk("[AVB]TS,%llu,%d,%03d,%02d,\n",
				ns, ccnt1, skb->data[20], qno);
#else
		trace_printk("[AVB]TS,%llu,%d,%03d,%02d, ,\n",
				ns, ccnt1, skb->data[20], qno);
#endif
#ifdef TC956X_SRIOV_VF
		} else if ((qno == priv->plat->avb_class_b_ch_no)
			&& (priv->plat->best_effort_ch_no != priv->plat->avb_class_b_ch_no)) {
#else
		} else if (qno == AVB_CLASS_B_TX_CH) {
#endif

			if (skb->data[20] == 0)
				ccnt2++;

			trace_printk("[CLASS_B]TS,%llu,%d,%03d,%02d, ,\n",
					ns, ccnt2, skb->data[20], qno);
#ifdef TC956X_SRIOV_VF
		} else if ((qno == priv->plat->tsn_ch_no)
			&& (priv->plat->best_effort_ch_no != priv->plat->tsn_ch_no)) {/*  For CDT */
#else
		} else if (qno == TSN_CLASS_CDT_TX_CH) {/*  For CDT */
#endif

			if (skb->data[20] == 0)
				ccnt3++;

#ifdef FPE
			trace_printk("[CDT]TS,%llu,%d,%03d,%02d,\n",
					ns, ccnt3, skb->data[20], qno);
#else
			trace_printk("[CDT]TS,%llu,%d,%03d,%02d, ,\n",
					ns, ccnt3, skb->data[20], qno);
#endif

#ifdef TC956X_SRIOV_VF
		} else if ((qno == priv->plat->gptp_ch_no)
			&& (priv->plat->best_effort_ch_no != priv->plat->gptp_ch_no)) {
#else
		} else if (qno == TC956X_GPTP_TX_CH) {
#endif
			u16 gPTP_ID = 0;
			u16 MsgType = 0;

			MsgType = skb->data[14] & 0x0F;
			gPTP_ID = skb->data[44];
			gPTP_ID = (gPTP_ID<<8) | skb->data[45];

			if (MsgType != 0x0b)
				trace_printk("[gPTP]TS,%019llu,%04d,0x%x,%02d\n",
						ns, gPTP_ID, MsgType, qno);

#ifdef TC956X_SRIOV_VF
		} else if (qno == priv->plat->best_effort_ch_no) {
#else
		} else if (qno == HOST_BEST_EFF_CH) {
#endif
#ifdef FPE
			trace_printk("[LE]TS,%llu,00,000,%02d,\n", ns, qno);
#else
			trace_printk("[LE]TS,%llu,00,000,%02d, ,\n", ns, qno);
#endif
		} else {
		}
	}
#endif

}

/* tc956xmac_get_rx_hwtstamp - get HW RX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @np : next descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read received packet's timestamp from the descriptor
 * and pass it to stack. It also perform some sanity checks.
 */

#if defined(RX_LOGGING_TRACE)
static void tc956xmac_get_rx_hwtstamp(struct tc956xmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb,
				   u32 qno)

#else
static void tc956xmac_get_rx_hwtstamp(struct tc956xmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb)
#endif
{
	struct skb_shared_hwtstamps *shhwtstamp = NULL;
	struct dma_desc *desc = p;
	u64 ns = 0;
#if defined(RX_LOGGING_TRACE)
	static unsigned int ccnt1, ccnt2;
	unsigned int proto = 0;
#endif

#ifdef FPE
#if defined(RX_LOGGING_TRACE)
	priv->mmc.mmc_rx_packet_assembly_ok_cntr += readl(priv->mmcaddr + MMC_XGMAC_RX_PKT_ASSEMBLY_OK);
	priv->mmc.mmc_rx_fpe_fragment_cntr += readl(priv->mmcaddr + MMC_XGMAC_RX_FPE_FRAG);
#endif
#endif

	if (!priv->hwts_rx_en)
		return;


	/* For GMAC4, the valid timestamp is from CTX next desc. */
	if (priv->plat->has_gmac4 || priv->plat->has_xgmac)
		desc = np;

	/* Check if timestamp is available */
	if (tc956xmac_get_rx_timestamp_status(priv, p, np, priv->adv_ts)) {
#ifdef FPE
#if defined(RX_LOGGING_TRACE)
		static unsigned int count;

		if (count % 8000 == 0) {
			KPRINT_INFO("Rx FPE asmbly_ok_cnt:%lld,frag_cnt:%lld\n",
				priv->mmc.mmc_rx_packet_assembly_ok_cntr,
				priv->mmc.mmc_rx_fpe_fragment_cntr);
		}
		count++;
#endif
#endif
		tc956xmac_get_timestamp(priv, desc, priv->adv_ts, &ns);
		netdev_dbg(priv->dev, "get valid RX hw timestamp %llu\n", ns);
		shhwtstamp = skb_hwtstamps(skb);
		memset(shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp->hwtstamp = ns_to_ktime(ns);

#if defined(RX_LOGGING_TRACE)
		proto = htons(((skb->data[13]<<8) | skb->data[12]));
		if (proto == ETH_P_8021Q) {
			proto = htons(((skb->data[17]<<8) | skb->data[16]));
			if (proto == ETH_P_TSN) {
				if (skb->data[24] == 0 && skb->data[25] == 2) {
				/* Differentiated by stream IDs 2 for CDT */
					if ((unsigned char)skb->data[16] == 0)
						ccnt1++;
					trace_printk("[CDT]TS,%llu,%d,%03d,%02d\n",
							ns, ccnt1, skb->data[16], qno);
				} else if (skb->data[24] == 0 && skb->data[25] == 1) {
					/* Differentiated by stream IDs 1 for AVB */
					if ((unsigned char)skb->data[16] == 0)
						ccnt2++;
					trace_printk("[AVB]TS,%llu,%d,%03d,%02d\n",
							ns, ccnt2, skb->data[16], qno);
				}
			}
		} else if (proto == ETH_P_1588) {
			if ((skb->data[14]&0x0F) != 0xb) {
				u16 MsgType = skb->data[14] & 0x0F;
				u16 gPTP_ID = (skb->data[44] << 8) | skb->data[45];

				trace_printk("[gPTP]TS,%019llu,%04d,0x%x,%02d\n",
						ns, gPTP_ID, MsgType, qno);
			}
		} else {
		}
#endif
	} else {
		netdev_dbg(priv->dev, "cannot get RX hw timestamp\n");
	}
}

/**
 *  tc956xmac_hwtstamp_set - control hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function configures the MAC to enable/disable both outgoing(TX)
 *  and incoming(RX) packets time stamping based on user input.
 *  Return Value:
 *  0 on success and an appropriate -ve integer on failure.
 */
#ifndef TC956X_SRIOV_VF
static int tc956xmac_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;
	u32 ptp_v2 = 0;
	u32 tstamp_all = 0;
	u32 ptp_over_ipv4_udp = 0;
	u32 ptp_over_ipv6_udp = 0;
	u32 ptp_over_ethernet = 0;
	u32 snap_type_sel = 0;
	u32 ts_master_en = 0;
	u32 ts_event_en = 0;
	u32 value = 0;
	u32 tcr_config = 0;
	bool xmac;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->adv_ts)) {
		netdev_alert(priv->dev, "No support for HW time stamping\n");
		priv->hwts_tx_en = 0;
		priv->hwts_rx_en = 0;

		return -EOPNOTSUPP;
	}

	if (copy_from_user(&config, ifr->ifr_data,
			   sizeof(config)))
		return -EFAULT;

	netdev_dbg(priv->dev, "%s config flags:0x%x, tx_type:0x%x, rx_filter:0x%x\n",
		   __func__, config.flags, config.tx_type, config.rx_filter);

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if (config.tx_type != HWTSTAMP_TX_OFF &&
	    config.tx_type != HWTSTAMP_TX_ON)
		return -ERANGE;

	if (priv->adv_ts) {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			/* time stamp no incoming packet at all */
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			/* 'xmac' hardware can support Sync, Pdelay_Req and
			 * Pdelay_resp by setting bit14 and bits17/16 to 01
			 * This leaves Delay_Req timestamps out.
			 * Enable all events *and* general purpose message
			 * timestamping
			 */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
			/* PTP v1, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_SYNC;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
			/* PTP v1, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
			/* PTP v2, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for all event messages */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
			/* PTP v2, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
			/* PTP v2, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_EVENT:
			/* PTP v2/802.AS1 any layer, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			ts_event_en = PTP_TCR_TSEVNTENA;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_SYNC:
			/* PTP v2/802.AS1, any layer, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
			/* PTP v2/802.AS1, any layer, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_NTP_ALL:
		case HWTSTAMP_FILTER_ALL:
			/* time stamp any incoming packet */
			config.rx_filter = HWTSTAMP_FILTER_ALL;
			tstamp_all = PTP_TCR_TSENALL;
			break;

		default:
			return -ERANGE;
		}
	} else {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;
		default:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			break;
		}
	}
	priv->hwts_rx_en = ((config.rx_filter == HWTSTAMP_FILTER_NONE) ? 0 : 1);
	priv->hwts_tx_en = config.tx_type == HWTSTAMP_TX_ON;

	if (!priv->hwts_tx_en && !priv->hwts_rx_en)
		tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, 0);
	else {
		tcr_config = (PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSCTRLSSR |
			tstamp_all | ptp_v2 | ptp_over_ethernet |
			ptp_over_ipv6_udp | ptp_over_ipv4_udp | ts_event_en |
			ts_master_en | snap_type_sel | PTP_TCR_ASMEN);

		value = readl(priv->ptpaddr + PTP_TCR);
		/* Note : Values will never be set. "tc956x_ptp_configuration" function
		 * call should be same as during probe.
		 */
		if (!(value & 0x00000001)) {
			tc956x_ptp_configuration(priv, 0);
			DBGPR_FUNC(priv->device, "--> %s\n", __func__);
		}

	}

	memcpy(&priv->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

/*
 *  tc956xmac_hwtstamp_get - read hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function obtain the current hardware timestamping settings
 *  as requested.
 */
static int tc956xmac_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config *config = &priv->tstamp_config;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, config,
			    sizeof(*config)) ? -EFAULT : 0;
}

/**
 * tc956xmac_init_ptp - init PTP
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PTPv1 or PTPv2.
 * This is done by looking at the HW cap. register.
 * This function also registers the ptp driver.
 */
static int tc956xmac_init_ptp(struct tc956xmac_priv *priv)
{
	bool xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	priv->adv_ts = 0;
	/* Check if adv_ts can be enabled for dwmac 4.x / xgmac core */
	if (xmac && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;
	/* Dwmac 3.x core with extend_desc can support adv_ts */
	else if (priv->extend_desc && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;

	if (priv->dma_cap.time_stamp)
		netdev_info(priv->dev, "IEEE 1588-2002 Timestamp supported\n");

	if (priv->adv_ts)
		netdev_info(priv->dev,
			    "IEEE 1588-2008 Advanced Timestamp supported\n");

	priv->hwts_tx_en = 0;
	priv->hwts_rx_en = 0;

	tc956xmac_ptp_register(priv);

	return 0;
}
#endif /* TC956X_SRIOV_VF */

static void tc956xmac_release_ptp(struct tc956xmac_priv *priv)
{
	if (priv->plat->clk_ptp_ref)
		clk_disable_unprepare(priv->plat->clk_ptp_ref);
	tc956xmac_ptp_unregister(priv);
}
#ifndef TC956X_SRIOV_VF
/**
 *  tc956xmac_mac_flow_ctrl - Configure flow control in all queues
 *  @priv: driver private structure
 *  Description: It is used for configuring the flow control in all queues
 */
static void tc956xmac_mac_flow_ctrl(struct tc956xmac_priv *priv, u32 duplex)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;

	tc956xmac_flow_ctrl(priv, priv->hw, duplex, priv->flow_ctrl,
			priv->pause, tx_cnt);
}

static void tc956xmac_validate(struct phylink_config *config,
			    unsigned long *supported,
			    struct phylink_link_state *state)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mac_supported) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	int tx_cnt = priv->plat->tx_queues_to_use;
	int max_speed = priv->plat->max_speed;

	phylink_set(mac_supported, 10baseT_Half);
	phylink_set(mac_supported, 10baseT_Full);
	phylink_set(mac_supported, 100baseT_Half);
	phylink_set(mac_supported, 100baseT_Full);
	phylink_set(mac_supported, 1000baseT_Half);
	phylink_set(mac_supported, 1000baseT_Full);
	phylink_set(mac_supported, 1000baseKX_Full);

	phylink_set(mac_supported, Autoneg);
	phylink_set(mac_supported, Pause);
	phylink_set(mac_supported, Asym_Pause);
	phylink_set_port_modes(mac_supported);

	/*USXGMII interface does not support speed of 1000/100/10*/
	if (priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) {
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 10baseT_Full);
		phylink_set(mask, 100baseT_Half);
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 1000baseT_Half);
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseKX_Full);
	}


	/* Cut down 1G if asked to */
	if ((max_speed > 0) && (max_speed < 1000)) {
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
	} else if (priv->plat->has_xgmac) {
		if (!max_speed || (max_speed >= 2500)) {
			phylink_set(mac_supported, 2500baseT_Full);
			phylink_set(mac_supported, 2500baseX_Full);
		}
		if (!max_speed || (max_speed >= 5000))
			phylink_set(mac_supported, 5000baseT_Full);
		if (!max_speed || (max_speed >= 10000)) {
			phylink_set(mac_supported, 10000baseSR_Full);
			phylink_set(mac_supported, 10000baseLR_Full);
			phylink_set(mac_supported, 10000baseER_Full);
			phylink_set(mac_supported, 10000baseLRM_Full);
			phylink_set(mac_supported, 10000baseT_Full);
			phylink_set(mac_supported, 10000baseKX4_Full);
			phylink_set(mac_supported, 10000baseKR_Full);
		}
	}

	/* Half-Duplex can only work with single queue */
	if (tx_cnt > 1) {
#ifdef TC956X
		KPRINT_INFO("Half duplex not supported in Port0");
#else
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 100baseT_Half);
#endif

#ifdef TC956X
		phylink_set(mask, 1000baseT_Half);
#endif
	}

	bitmap_and(supported, supported, mac_supported,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_andnot(supported, supported, mask,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mac_supported,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_andnot(state->advertising, state->advertising, mask,
			__ETHTOOL_LINK_MODE_MASK_NBITS);
}
#endif  /* TC956X_SRIOV_VF */

#ifndef TC956X_SRIOV_VF
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
static void tc956xmac_mac_pcs_get_state(struct phylink_config *config,
					struct phylink_link_state *state)
{
#ifndef TC956X
	state->link = 0;
#else
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
	if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
		KPRINT_INFO("AN clause 37 completed");
		if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
		   (priv->plat->interface == PHY_INTERFACE_MODE_10GKR)) {
			if (reg_value & XGMAC_USXG_AN_STS_LINK_MASK) {/*check link status*/
				state->link = 1;
				KPRINT_INFO("XPCS USXGMII link up");

				if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x3)/*If USXG_AN_STS is 10G*/
					state->speed = SPEED_10000;
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x5)/*If USXG_AN_STS is 5G*/
					state->speed = SPEED_5000;
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x4)/*If USXG_AN_STS is 5G*/
					state->speed = SPEED_2500;

				if (reg_value & XGMAC_USXG_AN_STS_DUPLEX_MASK)/*If USXG_AN_STS is full duplex*/
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
			} else {
				KPRINT_INFO("XPCS USXGMII link down");
				state->link = 0;
			}

		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (reg_value & XGMAC_SGM_STS_LINK_MASK) {/*SGMII autonegotiated link */
				state->link = 1;
				KPRINT_INFO("XPCS SGMII link up");

				if (reg_value & XGMAC_SGM_STS_DUPLEX)/*SGMII autonegotiated duplex */
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;

				if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x2) {/*SGMII autonegotiated speed */
					if (priv->is_sgmii_2p5g == true)
						state->speed = SPEED_2500; /*There is no seperate bit check for 2.5Gbps, so set here */
					else
						state->speed = SPEED_1000;
				} else if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x1)
					state->speed = SPEED_100;
			} else {
				state->link = 0;
				KPRINT_INFO("XPCS SGMII link down");
			}
		}
	}
#endif
}
#endif
#else	/* Required when using with Kernel v5.4 */
static int tc956xmac_mac_link_state(struct phylink_config *config,
				 struct phylink_link_state *state)
{
#ifndef TC956X
	return -EOPNOTSUPP;
#else
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
	if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
		KPRINT_INFO("AN clause 37 completed");
		if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
		   (priv->plat->interface == PHY_INTERFACE_MODE_10GKR)) {
			if (reg_value & XGMAC_USXG_AN_STS_LINK_MASK) {/*check link status*/
				state->link = 1;
				KPRINT_INFO("XPCS USXGMII link up");

				/*If USXG_AN_STS is 10G*/
				if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x3)
					state->speed = SPEED_10000;
				/*If USXG_AN_STS is 5G*/
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x5)
					state->speed = SPEED_5000;
				/*If USXG_AN_STS is 5G*/
				else if (((reg_value & XGMAC_USXG_AN_STS_SPEED_MASK) >> 9) == 0x4)
					state->speed = SPEED_2500;

				/*If USXG_AN_STS is full duplex*/
				if (reg_value & XGMAC_USXG_AN_STS_DUPLEX_MASK)
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
			} else {
				KPRINT_INFO("XPCS USXGMII link down");
				state->link = 0;
			}

		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (reg_value & XGMAC_SGM_STS_LINK_MASK) {/*SGMII autonegotiated link */
				state->link = 1;
				KPRINT_INFO("XPCS SGMII link up");
				/*SGMII autonegotiated duplex */
				if (reg_value & XGMAC_SGM_STS_DUPLEX)
					state->duplex = DUPLEX_FULL;
				else
					state->duplex = DUPLEX_HALF;
				/*SGMII autonegotiated speed */
				if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x2) {
					if (priv->is_sgmii_2p5g == true)
						state->speed = SPEED_2500; /*There is no seperate bit check for 2.5Gbps, so set here */
					else
						state->speed = SPEED_1000;
				} else if (((reg_value & XGMAC_SGM_STS_SPEED_MASK) >> 2) == 0x1)
					state->speed = SPEED_100;
			} else {
				state->link = 0;
				KPRINT_INFO("XPCS SGMII link down");
			}
		}
	}

	return 0;
#endif
}
#endif

/**
 *  tc956xmac_speed_change_init_mac - Initialize MAC during speed change.
 *  @priv: driver private structure
 *  @state : phy state structure
 *  Description: It is used for initializing MAC during speed change of
 *  USXGMII and SGMII.
 */
static void tc956xmac_speed_change_init_mac(struct tc956xmac_priv *priv,
					const struct phylink_link_state *state)
{
	/* use signal from MSPHY */
	uint8_t SgmSigPol = 0;
	int ret = 0;
	bool enable_an = true;

	if (priv->port_num == RM_PF0_ID) {
		/* Enable all clocks to eMAC Port0 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);
		if ((state->interface == PHY_INTERFACE_MODE_SGMII) &&
		   (state->speed == SPEED_2500)) {
			ret &= ~NCLKCTRL0_MAC0125CLKEN;
			ret &= ~NCLKCTRL0_MAC0312CLKEN;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET);

		/* Interface configuration for port0*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (state->interface == PHY_INTERFACE_MODE_SGMII) {
			if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_SGMII_2500M;
			else
				ret |= NEMACCTL_SP_SEL_SGMII_1000M;
		} else {
			/*else if ((PORT0_INTERFACE == ENABLE_USXGMII_INTERFACE)*/
			if (state->speed == SPEED_10000)
				ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
			else if (state->speed == SPEED_5000)
				ret |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
			else if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
		}
		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */
		ret |= (NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN);
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC0CTL_OFFSET);
	}
	if (priv->port_num == RM_PF1_ID) {
		/* Enable all clocks to eMAC Port1 */
		ret = readl(priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);
		if ((state->interface == PHY_INTERFACE_MODE_SGMII) &&
		   (state->speed == SPEED_2500)) {
			ret &= ~NCLKCTRL1_MAC1125CLKEN1;
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
		} else {
			ret &= ~NCLKCTRL1_MAC1312CLKEN1;
			ret |= NCLKCTRL1_MAC1125CLKEN1;
		}
		writel(ret, priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET);

		/* Interface configuration for port1*/
		ret = readl(priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);
		ret &= ~(NEMACCTL_SP_SEL_MASK | NEMACCTL_PHY_INF_SEL_MASK);
		if (state->interface == PHY_INTERFACE_MODE_SGMII) {
			if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_SGMII_2500M;
			else
				ret |= NEMACCTL_SP_SEL_SGMII_1000M;
		} else {
			if (state->speed == SPEED_10000)
				ret |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
			else if (state->speed == SPEED_5000)
				ret |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
			else if (state->speed == SPEED_2500)
				ret |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
		}

		ret &= ~(0x00000040); /* Mask Polarity */
		if (SgmSigPol == 1)
			ret |= 0x00000040; /* Set Active low */
		ret |= (NEMACCTL_PHY_INF_SEL | NEMACCTL_LPIHWCLKEN);
		writel(ret, priv->tc956x_SFR_pci_base_addr + NEMAC1CTL_OFFSET);
	}

	/*PMA module init*/
	if (priv->hw->xpcs && (state->interface == PHY_INTERFACE_MODE_SGMII)) {
		if (priv->port_num == RM_PF0_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret |= (NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}
		if (priv->port_num == RM_PF1_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret |= (NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		ret = tc956x_pma_setup(priv, priv->pmaaddr);
		if (ret < 0)
			KPRINT_ERR("PMA switching to internal clock Failed\n");

		if (priv->port_num == RM_PF0_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret &= ~(NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}
		if (priv->port_num == RM_PF1_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret &= ~(NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		if (priv->port_num == RM_PF0_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC0CTL_OFFSET);
			} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}
		if (priv->port_num == RM_PF1_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC1CTL_OFFSET);
			} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}
		if ((state->interface == PHY_INTERFACE_MODE_SGMII)
		&& (state->speed == SPEED_2500)) {
			/* XPCS doesn't support AN for 2.5G SGMII.
			 * Disable AN only if SGMII 2.5G is Enabled.
			 */
			priv->is_sgmii_2p5g = true;
			enable_an = false;
		} else {
			priv->is_sgmii_2p5g = false;
			enable_an = true;
		}

		ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
		if (ret < 0)
			KPRINT_INFO("XPCS initialization error\n");
		tc956x_xpcs_ctrl_ane(priv, enable_an);
	}
}

static void tc956xmac_mac_config(struct phylink_config *config, unsigned int mode,
				const struct phylink_link_state *state)
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 ctrl, emac_ctrl;
	u32 val;
	bool config_done = false;
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	int ret = 0;
#endif

#ifdef TC956X
	u32 reg_value;

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	emac_ctrl = readl(priv->ioaddr + NEMACCTL_OFFSET);
	emac_ctrl &= ~NEMACCTL_SP_SEL_MASK;

	if (priv->hw->xpcs) {
		reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
		if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
			KPRINT_INFO("AN clause 37 completed");
			reg_value &= ~(XGMAC_C37_AN_COMPL);
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
			KPRINT_INFO("AN clause 37 complete bit cleared");
		}

#ifdef TC956X_MAGIC_PACKET_WOL_CONF
		if (priv->wol_config_enabled != true) {
#endif
			if (state->interface == PHY_INTERFACE_MODE_USXGMII) {
				/* Invoke this only during speed change */
				if ((state->speed != SPEED_UNKNOWN) && (state->speed != 0)) {
					if (state->speed != priv->speed) {
						tc956xmac_speed_change_init_mac(priv, state);
					}
				} else {
					return;
				}

				/* Program autonegotiated speed to SR_MII_CTRL */
				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
				val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */

				switch (state->speed) {
				case SPEED_10000:
					ctrl |= priv->hw->link.xgmii.speed10000;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_10G;
					break;
				case SPEED_5000:
					ctrl |= priv->hw->link.xgmii.speed5000;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_5G;
					break;
				case SPEED_2500:
					ctrl |= priv->hw->link.xgmii.speed2500;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_2_5G;
					break;
				default:
					return;
				}

				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);

				/* USRA_RST set to 1 */
				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
				val |= XGMAC_USRA_RST;
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, val);
				config_done = true;
			}
		if ((state->interface == PHY_INTERFACE_MODE_SGMII)
			&& (priv->port_interface != ENABLE_2500BASE_X_INTERFACE)) { /* Autonegotiation not supported for SGMII */
				reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
				/* Clear autonegotiation only if completed. As for XPCS, 2.5G autonegotiation is not supported */
				/* Switching from SGMII 2.5G to any speed doesn't cause AN completion */
				if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
					KPRINT_INFO("AN clause 37 completed");
					reg_value &= ~(XGMAC_C37_AN_COMPL);
					tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
					KPRINT_INFO("AN clause 37 complete bit cleared");
				}
				/* Invoke this only during speed change */
				if ((state->speed != SPEED_UNKNOWN) && (state->speed != 0)) {
					if (state->speed != priv->speed)
						tc956xmac_speed_change_init_mac(priv, state);
				} else {
					return;
				}
				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
				val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */
				switch (state->speed) {
				case SPEED_2500:
					ctrl |= priv->hw->link.speed2500;
					/* Program autonegotiated speed to SR_MII_CTRL */
					val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_2500M;
					break;
				case SPEED_1000:
					ctrl |= priv->hw->link.speed1000;
					val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_1000M;
					break;
				case SPEED_100:
					ctrl |= priv->hw->link.speed100;
					val |= XPCS_SS_SGMII_100M; /*100 Mbps setting */
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_100M;
					break;
				case SPEED_10:
					ctrl |= priv->hw->link.speed10;
					val |= XPCS_SS_SGMII_10M; /*10 Mbps setting */
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_10M;
					break;
				default:
					return;
				}
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);
				config_done = true;
			}
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
		} else {
			/* Configure Speed for WOL SGMII 1Gbps */
			KPRINT_INFO("%s Port %d : Entered with flag priv->wol_config_enabled %d", __func__, priv->port_num, priv->wol_config_enabled);
			KPRINT_INFO("%s Port %d : Speed to configure %d", __func__, priv->port_num, state->speed);
			reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
			/* Clear autonegotiation only if completed. As for XPCS, 2.5G autonegotiation is not supported */
			/* Switching from SGMII 2.5G to any speed doesn't cause AN completion */
			if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
				KPRINT_INFO("AN clause 37 completed");
				reg_value &= ~(XGMAC_C37_AN_COMPL);
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
				KPRINT_INFO("AN clause 37 complete bit cleared");
			}
			ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
			if (ret < 0)
			      KPRINT_INFO("XPCS initialization error\n");
			tc956x_xpcs_ctrl_ane(priv, true);
			val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
			val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */
			switch (state->speed) {
			case SPEED_1000:
				ctrl |= priv->hw->link.speed1000;
				val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_1000M;
				break;
			case SPEED_100:
				ctrl |= priv->hw->link.speed100;
				val |= XPCS_SS_SGMII_100M; /*100 Mbps setting */
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_100M;
				break;
			default:
				return;
			}
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);
			config_done = true;
		} /* End of if (priv->wol_config_enabled != true) */
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
	} else if (state->interface == PHY_INTERFACE_MODE_RGMII) {
		switch (state->speed) {
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_1000M;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_100M;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_10M;
			break;
		default:
			return;
		}
		config_done = true;
	} else {
		switch (state->speed) {
		case SPEED_2500:
			ctrl |= priv->hw->link.speed2500;
			break;
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			break;
		default:
			return;
		}
		config_done = true;
	}

	priv->speed = state->speed;
#ifdef TC956X_SRIOV_PF
	priv->duplex = state->duplex;
#endif
	if (priv->plat->fix_mac_speed)
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, state->speed);

	if (!state->duplex)
		ctrl &= ~priv->hw->link.duplex;
	else
		ctrl |= priv->hw->link.duplex;

	/* Flow Control operation */
	if (state->pause)
		tc956xmac_mac_flow_ctrl(priv, state->duplex);

	if (config_done) {
		writel(ctrl, priv->ioaddr + MAC_CTRL_REG);
		writel(emac_ctrl, priv->ioaddr + NEMACCTL_OFFSET);
	}
#endif
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
static void tc956xmac_mac_an_restart(struct phylink_config *config)
{
#ifdef TC956X
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	bool enable_en = true;

	if (priv->hw->xpcs) {
		/*Enable XPCS Autoneg*/
		if ((priv->plat->interface == PHY_INTERFACE_MODE_10GKR) || 
			(priv->plat->interface == ENABLE_2500BASE_X_INTERFACE)) {
			enable_en = false;
			KPRINT_INFO("%s :Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
		} else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (priv->is_sgmii_2p5g == true) {
				enable_en = false;
				KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
			} else {
				enable_en = true;
				KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
			}
		} else {
			enable_en = true;
			KPRINT_INFO("%s : Port %d AN Enable:%d", __func__, priv->port_num, enable_en);
		}
		tc956x_xpcs_ctrl_ane(priv, enable_en);
	}
#else
	/*Not supported*/
#endif
}
#endif

static int tc956xmac_open(struct net_device *dev);
static int tc956xmac_release(struct net_device *dev);
static void tc956xmac_mac_link_down(struct phylink_config *config,
				 unsigned int mode, phy_interface_t interface)
{
	u32 ch;
	u32 offload_release_sts = true;
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	struct net_device *ndev = to_net_dev(config->dev);
	tc956xmac_mac_set_rx(priv, priv->ioaddr, false);

	/* In SRIOV code, EEE is handled by PF driver */
#ifndef TC956X_SRIOV_VF
#ifdef EEE
	priv->eee_active = false;
	DBGPR_FUNC(priv->device, "%s Disable EEE\n", __func__);
	tc956xmac_disable_eee_mode(priv);
	tc956xmac_set_eee_pls(priv, priv->hw, false);
#endif
#ifdef TC956X_PM_DEBUG
	pm_generic_suspend(priv->device);
#endif

	if (((mac0_link_down_macrst == ENABLE && priv->port_num == RM_PF0_ID) ||
		(mac1_link_down_macrst == ENABLE && priv->port_num == RM_PF1_ID)) &&
		netif_running(ndev))
		priv->link_down_rst = true;
	else
		priv->link_down_rst = false;

	KPRINT_INFO("link down1 priv->link_down_rst = %d \n", priv->link_down_rst);
	if (priv->link_down_rst == true) {
		tc956xmac_release(ndev);
		tc956xmac_open(ndev);
		priv->link_down_rst = false;
	}

	KPRINT_INFO("link down2 priv->link_down_rst = %d \n", priv->link_down_rst);
	mutex_lock(&priv->port_ld_release_lock);
	/* Checking whether all offload Tx channels released or not*/
	for (ch = 0; ch < MAX_TX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->tx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}
	/* Checking whether all offload Rx channels released or not*/
	for (ch = 0; ch < MAX_RX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->rx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}

	/* If all channels are freed, call API for power saving*/
	if(priv->port_link_down == false && offload_release_sts == true) {
	tc956xmac_link_change_set_power(priv, LINK_DOWN); /* Save, Assert and Disable Reset and Clock */
	}
	priv->port_release = true; /* setting port release to true as link-down invoked, and clear from open or link-up */
	mutex_unlock(&priv->port_ld_release_lock);

#endif
}

#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
static inline bool tc956x_phy_check_valid(int speed, int duplex,
				   unsigned long *features)
{
	return !!phy_lookup_setting(speed, duplex, features, true);
}

static void tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(unsigned long *advertising, u16 eee_adv)
{
	linkmode_zero(advertising);

	if (eee_adv & MDIO_EEE_5GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_2_5GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 advertising);
}

static int tc956x_phy_init_eee(struct phy_device *phydev, bool clk_stop_enable)
{
	if (!phydev->drv)
		return -EIO;

	KPRINT_INFO("%s EEE phy init for 5G/2.5G\n", __func__);

	/* According to 802.3az,the EEE is supported only in full duplex-mode.
	 */
	if (phydev->duplex == DUPLEX_FULL) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(adv);
		int eee_lp, eee_cap, eee_adv;
		int status;
		u32 cap;

		/* Read phy status to properly get the right settings */
		status = phy_read_status(phydev);
		if (status) {
			KPRINT_ERR("Error 0: %d\n", status);
			return status;
		}

		/* First check if the EEE ability is supported */
		eee_cap = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE2);
		if (eee_cap <= 0) {
			KPRINT_ERR("Error 2\n");
			goto eee_exit_err;
		}

		cap = mmd_eee_cap_to_ethtool_sup_t(eee_cap);
		if (!cap) {
			KPRINT_ERR("Error 3\n");
			goto eee_exit_err;
		}

		/* Check which link settings negotiated and verify it in
		 * the EEE advertising registers.
		 */
		eee_lp = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE2);
		if (eee_lp <= 0) {
			KPRINT_ERR("Error 4\n");
			goto eee_exit_err;
		}

		eee_adv = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2);
		if (eee_adv <= 0) {
			KPRINT_ERR("Error 5\n");
			goto eee_exit_err;
		}

		tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(adv, eee_adv);
		tc956x_mmd_eee_adv_to_linkmode_5G_2_5G(lp, eee_lp);
		linkmode_and(common, adv, lp);

		if (!tc956x_phy_check_valid(phydev->speed, phydev->duplex, common)) {
			KPRINT_ERR("Error 6\n");
			goto eee_exit_err;
		}

		if (clk_stop_enable)
			phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
					 MDIO_PCS_CTRL1_CLKSTOP_EN);

		return 0;
	}
eee_exit_err:
	return -EPROTONOSUPPORT;
}
#endif

#ifdef DEBUG_EEE
static void mmd_eee_adv_to_linkmode_local(unsigned long *advertising, u16 eee_adv)
{
	linkmode_zero(advertising);

	if (eee_adv & MDIO_EEE_100TX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_1000T)
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GT)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_1000KX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GKX4)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
				 advertising);
	if (eee_adv & MDIO_EEE_10GKR)
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
				 advertising);
}

int phy_init_eee_local(struct phy_device *phydev, bool clk_stop_enable)
{
	if (!phydev->drv)
		return -EIO;

	KPRINT_INFO("----> %s\n", __func__);


	/* According to 802.3az,the EEE is supported only in full duplex-mode.
	 */
	if (phydev->duplex == DUPLEX_FULL) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(adv);
		int eee_lp, eee_cap, eee_adv;
		int status;
		u32 cap;

		/* Read phy status to properly get the right settings */
		status = phy_read_status(phydev);
		if (status) {
			KPRINT_ERR("Error 0: %d\n", status);
			return status;
		}

		/* First check if the EEE ability is supported */
		eee_cap = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
		if (eee_cap <= 0) {
			KPRINT_ERR("Error 1\n");
			goto eee_exit_err;
		}

		cap = mmd_eee_cap_to_ethtool_sup_t(eee_cap);
		if (!cap) {
			KPRINT_ERR("Error 2\n");
			goto eee_exit_err;
		}

		/* Check which link settings negotiated and verify it in
		 * the EEE advertising registers.
		 */
		eee_lp = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
		if (eee_lp <= 0) {
			KPRINT_ERR("Error 3\n");
			goto eee_exit_err;
		}

		eee_adv = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
		if (eee_adv <= 0) {
			KPRINT_ERR("Error 4\n");
			goto eee_exit_err;
		}

		KPRINT_INFO("%s eee_adv: 0x%x\n", __func__, eee_adv);
		KPRINT_INFO("%s eee_lp: 0x%x\n", __func__, eee_lp);


		mmd_eee_adv_to_linkmode_local(adv, eee_adv);
		mmd_eee_adv_to_linkmode_local(lp, eee_lp);

		KPRINT_INFO("%s adv: 0x%x\n", __func__, adv);
		KPRINT_INFO("%s eee_lp: 0x%x\n", __func__, lp);

		linkmode_and(common, adv, lp);

		KPRINT_INFO("%s common: 0x%x\n", __func__, common);

		if (!tc956x_phy_check_valid(phydev->speed, phydev->duplex, common)) {
			KPRINT_ERR("Error 5\n");
			goto eee_exit_err;
		}

		if (clk_stop_enable)
			/* Configure the PHY to stop receiving xMII
			 * clock while it is signaling LPI.
			 */
			phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
					 MDIO_PCS_CTRL1_CLKSTOP_EN);

		return 0; /* EEE supported */
	}
eee_exit_err:
	return -EPROTONOSUPPORT;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
static void tc956xmac_mac_link_up(struct phylink_config *config,
				   struct phy_device *phy,
				   unsigned int mode, phy_interface_t interface,
			       int speed, int duplex, bool tx_pause, bool rx_pause)
#else
static void tc956xmac_mac_link_up(struct phylink_config *config,
			       unsigned int mode, phy_interface_t interface,
			       struct phy_device *phy)
#endif
{
	struct tc956xmac_priv *priv = netdev_priv(to_net_dev(config->dev));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
	u32 ctrl, emac_ctrl;
	bool config_done = false;
	u32 reg_value;
	u32 val;
	struct phylink_link_state state;

	state.interface = interface;
	state.speed = speed;
	state.duplex = duplex;

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	emac_ctrl = readl(priv->ioaddr + NEMACCTL_OFFSET);
	emac_ctrl &= ~NEMACCTL_SP_SEL_MASK;

	if (priv->hw->xpcs) {
		reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
		if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
			KPRINT_INFO("AN clause 37 completed");
			reg_value &= ~(XGMAC_C37_AN_COMPL);
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
			KPRINT_INFO("AN clause 37 complete bit cleared");
		}

#ifdef TC956X_MAGIC_PACKET_WOL_CONF
		if (priv->wol_config_enabled != true) {
#endif
			if (interface == PHY_INTERFACE_MODE_USXGMII) {
				/* Invoke this only during speed change */
				if ((speed != SPEED_UNKNOWN) && (speed != 0)) {
					if (speed != priv->speed)
						tc956xmac_speed_change_init_mac(priv, &state);
				} else {
					return;
				}
				/* Program autonegotiated speed to SR_MII_CTRL */
				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
				val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */

				switch (speed) {
				case SPEED_10000:
					ctrl |= priv->hw->link.xgmii.speed10000;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_10G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_10G;
					break;
				case SPEED_5000:
					ctrl |= priv->hw->link.xgmii.speed5000;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_5G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_5G;
					break;
				case SPEED_2500:
					ctrl |= priv->hw->link.xgmii.speed2500;
					emac_ctrl |= NEMACCTL_SP_SEL_USXGMII_2_5G_10G;
					val |= XGMAC_SR_MII_CTRL_SPEED_2_5G;
					break;
				default:
					return;
				}
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);

				/* USRA_RST set to 1 */
				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
				val |= XGMAC_USRA_RST;
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, val);
				config_done = true;
			}
			if ((interface == PHY_INTERFACE_MODE_SGMII) &&
			(priv->port_interface != ENABLE_2500BASE_X_INTERFACE)) {
				reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);
				/* Clear autonegotiation only if completed. As for XPCS, 2.5G autonegotiation is not supported */
				/* Switching from SGMII 2.5G to any speed doesn't cause AN completion */

				if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
					KPRINT_INFO("AN clause 37 completed");
					reg_value &= ~(XGMAC_C37_AN_COMPL);
					tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
					KPRINT_INFO("AN clause 37 complete bit cleared");
				}

				/* Invoke this only during speed change */
				if ((speed != SPEED_UNKNOWN) && (speed != 0)) {
					if (speed != priv->speed)
						tc956xmac_speed_change_init_mac(priv, &state);
				} else {
					return;
				}

				val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
				val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */

				switch (speed) {
				case SPEED_2500:
					ctrl |= priv->hw->link.speed2500;
					/* Program autonegotiated speed to SR_MII_CTRL */
					val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_2500M;
					break;
				case SPEED_1000:
					ctrl |= priv->hw->link.speed1000;
					val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_1000M;
					break;
				case SPEED_100:
					ctrl |= priv->hw->link.speed100;
					val |= XPCS_SS_SGMII_100M; /*100 Mbps setting */
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_100M;
					break;
				case SPEED_10:
					ctrl |= priv->hw->link.speed10;
					val |= XPCS_SS_SGMII_10M; /*10 Mbps setting */
					emac_ctrl |= NEMACCTL_SP_SEL_SGMII_10M;
					break;
				default:
					return;
				}
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);
				config_done = true;
			}
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
		} else {
			/* Configure Speed for WOL SGMII 1Gbps */
			KPRINT_INFO("%s Port %d : Entered with flag priv->wol_config_enabled %d", __func__, priv->port_num, priv->wol_config_enabled);
			KPRINT_INFO("%s Port %d : Speed to configure %d", __func__, priv->port_num, speed);
			reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS);

			/* Clear autonegotiation only if completed. As for XPCS, 2.5G autonegotiation is not supported */
			/* Switching from SGMII 2.5G to any speed doesn't cause AN completion */
			if (reg_value & XGMAC_C37_AN_COMPL) {/*check if AN 37 is complete CL37_ANCMPLT_INTR*/
				KPRINT_INFO("AN clause 37 completed");
				reg_value &= ~(XGMAC_C37_AN_COMPL);
				tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_MII_AN_INTR_STS, reg_value);
				KPRINT_INFO("AN clause 37 complete bit cleared");
			}

			ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
			if (ret < 0)
				KPRINT_INFO("XPCS initialization error\n");
			tc956x_xpcs_ctrl_ane(priv, true);
			val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
			val &= ~XGMAC_SR_MII_CTRL_SPEED; /* Mask speed ss13, ss6, ss5 */
			switch (speed) {
			case SPEED_1000:
				ctrl |= priv->hw->link.speed1000;
				val |= XPCS_SS_SGMII_1G; /*1000 Mbps setting only available, so set the same*/
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_1000M;
				break;
			case SPEED_100:
				ctrl |= priv->hw->link.speed100;
				val |= XPCS_SS_SGMII_100M; /*100 Mbps setting */
				emac_ctrl |= NEMACCTL_SP_SEL_SGMII_100M;
				break;
			default:
				return;
			}
			tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, val);
			config_done = true;
		} /* End of if (priv->wol_config_enabled != true) */
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
	} else if (interface == PHY_INTERFACE_MODE_RGMII) {
		switch (speed) {
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_1000M;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_100M;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			emac_ctrl |= NEMACCTL_SP_SEL_RGMII_10M;
			break;
		default:
			return;
		}
		config_done = true;
	} else {
		switch (speed) {
		case SPEED_2500:
			ctrl |= priv->hw->link.speed2500;
			break;
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			break;
		default:
			return;
		}
		config_done = true;
	}
	priv->speed = speed;

#ifdef TC956X_SRIOV_PF
	priv->duplex = duplex;
#endif

	if (priv->plat->fix_mac_speed)
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, speed);

	if (!duplex)
		ctrl &= ~priv->hw->link.duplex;
	else
		ctrl |= priv->hw->link.duplex;

	/* Flow Control operation */
	if (rx_pause && tx_pause)
		priv->flow_ctrl = FLOW_AUTO;
	else if (rx_pause && !tx_pause)
		priv->flow_ctrl = FLOW_RX;
	else if (!rx_pause && tx_pause)
		priv->flow_ctrl = FLOW_TX;
	else
		priv->flow_ctrl = FLOW_OFF;

	tc956xmac_mac_flow_ctrl(priv, duplex);

	if (config_done) {
		writel(ctrl, priv->ioaddr + MAC_CTRL_REG);
		writel(emac_ctrl, priv->ioaddr + NEMACCTL_OFFSET);
	}

	tc956xmac_mac_set(priv, priv->ioaddr, true);
#endif
#ifndef TC956X_SRIOV_VF
	mutex_lock(&priv->port_ld_release_lock);
	priv->port_release = false; /* setting port release to false as link-up invoked, and set to true from release or link down */
	if (priv->port_link_down == true) {
		tc956xmac_link_change_set_power(priv, LINK_UP); /* Restore, De-assert and Enable Reset and Clock */
	}
	mutex_unlock(&priv->port_ld_release_lock);
#endif
	tc956xmac_mac_set_rx(priv, priv->ioaddr, true);

	/* In SRIOV code, EEE is handled by PF driver */
#ifndef TC956X_SRIOV_VF
#ifdef EEE
	if (phy && priv->dma_cap.eee && priv->eee_enabled) {
		DBGPR_FUNC(priv->device, "%s EEE Enable, checking to enable acive\n", __func__);
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
		if (phy->speed == TC956X_PHY_SPEED_5G || phy->speed == TC956X_PHY_SPEED_2_5G) {
			priv->eee_active = tc956x_phy_init_eee(phy, 1) >= 0;
		} else {
#ifndef DEBUG_EEE
			priv->eee_active = phy_init_eee(phy, 1) >= 0;
#else
			priv->eee_active = phy_init_eee_local(phy, 1) >= 0;
#endif
		}
#else
#ifndef DEBUG_EEE
		priv->eee_active = phy_init_eee(phy, 1) >= 0;
#else
		priv->eee_active = phy_init_eee_local(phy, 1) >= 0;
#endif
#endif
		if (priv->eee_active) {
			tc956xmac_eee_init(priv);
			tc956xmac_set_eee_pls(priv, priv->hw, true);
		}
	}
#endif
	/* Send Link parameters to all VFs */
	priv->link = true;
#endif
	DBGPR_FUNC(priv->device, "%s priv->eee_enabled: %d priv->eee_active: %d\n", __func__, priv->eee_enabled, priv->eee_active);
	clear_bit(TC956XMAC_DOWN, &priv->link_state);

#ifdef TC956X_SRIOV_PF
#ifdef TC956X_DYNAMIC_LOAD_CBS
	if (prev_speed != priv->speed)
	{
		tc956xmac_set_cbs_speed(priv);
	}
	prev_speed = priv->speed;
#endif
	tc956x_mbx_wrap_phy_link(priv);
#endif
#ifdef TC956X_PM_DEBUG
	pm_generic_resume(priv->device);
#endif
}

static const struct phylink_mac_ops tc956xmac_phylink_mac_ops = {
	.validate = tc956xmac_validate,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
	.mac_pcs_get_state = tc956xmac_mac_pcs_get_state,
#endif
#else	/* Required when using with Kernel v5.4 */
	.mac_link_state = tc956xmac_mac_link_state,
#endif
	.mac_config = tc956xmac_mac_config,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
	.mac_an_restart = tc956xmac_mac_an_restart,
#endif
	.mac_link_down = tc956xmac_mac_link_down,
	.mac_link_up = tc956xmac_mac_link_up,
};
#endif  /* TC956X_SRIOV_VF */
#ifndef TC956X_SRIOV_VF
/**
 * tc956xmac_check_pcs_mode - verify if RGMII/SGMII is supported
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PCS.
 * Physical Coding Sublayer (PCS) interface that can be used when the MAC is
 * configured for the TBI, RTBI, or SGMII PHY interface.
 */
static void tc956xmac_check_pcs_mode(struct tc956xmac_priv *priv)
{
	int interface = priv->plat->interface;

	if (priv->dma_cap.pcs) {
		if ((interface == PHY_INTERFACE_MODE_RGMII) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_ID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_RXID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_TXID)) {
			netdev_dbg(priv->dev, "PCS RGMII support enabled\n");
#ifdef TC956X
			priv->hw->pcs = 0;
#else
			priv->hw->pcs = TC956XMAC_PCS_RGMII;
#endif
		} else if (interface == PHY_INTERFACE_MODE_SGMII) {
			netdev_dbg(priv->dev, "PCS SGMII support enabled\n");
#ifdef TC956X
			priv->hw->pcs = TC956XMAC_PCS_SGMII;
#endif
		} else if ((interface == PHY_INTERFACE_MODE_USXGMII) ||
			  (interface == PHY_INTERFACE_MODE_10GKR)) {
			netdev_dbg(priv->dev, "PCS USXGMII/XFI support enabled\n");
#ifdef TC956X
			priv->hw->pcs = TC956XMAC_PCS_USXGMII;
#endif
		}
	}
#ifdef TC956X
	priv->hw->pcs = 0;
	if (interface == PHY_INTERFACE_MODE_RGMII) {
		priv->hw->xpcs = 0;
	} else if (interface == PHY_INTERFACE_MODE_SGMII) {
		netdev_dbg(priv->dev, "PCS SGMII support enabled\n");
		priv->hw->xpcs = TC956XMAC_PCS_SGMII;
	} else if ((interface == PHY_INTERFACE_MODE_USXGMII) ||
		  (interface == PHY_INTERFACE_MODE_10GKR)) {
		netdev_dbg(priv->dev, "PCS USXGMII support enabled\n");
		priv->hw->xpcs = TC956XMAC_PCS_USXGMII;
	}
#endif
}

/**
 * tc956xmac_defer_phy_isr_work - Scheduled by the PHY Ext Interrupt from ISR Handler
 *  @work: work_struct
 */
static void tc956xmac_defer_phy_isr_work(struct work_struct *work)
{
	struct phy_device *phydev;
	int rd_val = 0;
	struct tc956xmac_priv *priv =
		container_of(work, struct tc956xmac_priv, emac_phy_work);
	int addr = priv->plat->phy_addr;

	DBGPR_FUNC(priv->device, "Entry: tc956xmac_defer_phy_isr_work\n");

	phydev = mdiobus_get_phy(priv->mii, addr);

	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return;
	}

	if (!phydev->drv) {
		netdev_err(priv->dev, "no phy driver\n");
		return;
	}

	/* Call ack interrupt to clear the WOL interrupt status fields */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
	if (phydev->drv->handle_interrupt)
		phydev->drv->handle_interrupt(phydev);
#else
	if (phydev->drv->ack_interrupt)
		phydev->drv->ack_interrupt(phydev);
#endif
	phy_mac_interrupt(phydev);

	/* PHY MSI interrupt Enable */
	rd_val = readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num, 0)); /* MSI_OUT_EN: Reading */
	rd_val |= (1 << MSI_INT_EXT_PHY);
	writel(rd_val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num, 0)); /* MSI_OUT_EN: Enable MAC Ext Interrupt */

	DBGPR_FUNC(priv->device, "Exit: tc956xmac_defer_phy_isr_work\n");
}

/**
 * tc956xmac_init_phy - PHY initialization
 * @dev: net device structure
 * Description: it initializes the driver's PHY state, and attaches the PHY
 * to the mac driver.
 *  Return value:
 *  0 on success
 */
static int tc956xmac_init_phy(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct device_node *node;
	int ret;
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;
	struct ethtool_eee edata;

	node = priv->plat->phylink_node;

	phydev = mdiobus_get_phy(priv->mii, addr);

	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return -ENODEV;
	}
	if (phydev->drv != NULL) {
		if (true == priv->plat->phy_interrupt_mode && (phydev->drv->config_intr)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
			phydev->irq = PHY_MAC_INTERRUPT;
#else
			phydev->irq = PHY_IGNORE_INTERRUPT;
#endif
			KPRINT_INFO("PHY configured in interrupt mode\n");
			DBGPR_FUNC(priv->device, "%s PHY configured in interrupt mode\n", __func__);

			INIT_WORK(&priv->emac_phy_work, tc956xmac_defer_phy_isr_work);
		} else {
			phydev->irq = PHY_POLL;
			DBGPR_FUNC(priv->device, "%s [1] PHY configured in polling mode\n", __func__);
		}
	} else {
		phydev->irq = PHY_POLL;
		phydev->interrupts =  PHY_INTERRUPT_DISABLED;
		DBGPR_FUNC(priv->device, "%s [2] PHY configured in polling mode\n", __func__);
	}
	if (node)
		ret = phylink_of_phy_connect(priv->phylink, node, 0);

	/* Some DT bindings do not set-up the PHY handle. Let's try to
	 * manually parse it
	 */
	if (!node || ret) {
		if (!phydev || (!phydev->phy_id && !phydev->is_c45)) {
			/* Try C45 */
			phydev = get_phy_device(priv->mii, addr, true);
			if (phydev && !IS_ERR(phydev)) {
				ret = phy_device_register(phydev);
				if (ret) {
					phy_device_free(phydev);
					phydev = NULL;
				}
			} else {
				phydev = NULL;
			}
		}

		if (!phydev) {
			netdev_err(priv->dev, "no phy at addr %d\n", addr);
			return -ENODEV;
		}

		ret = phylink_connect_phy(priv->phylink, phydev);

		phy_attached_info(phydev);
	}

	if (phydev->drv != NULL) {
		if (true == priv->plat->phy_interrupt_mode && (phydev->drv->config_intr))
			phydev->interrupts =  PHY_INTERRUPT_ENABLED;
		else
			phydev->interrupts =  PHY_INTERRUPT_DISABLED;
	}

	if (phydev->interrupts ==  PHY_INTERRUPT_ENABLED) {
		if (!(phydev->drv->config_intr &&
			!phydev->drv->config_intr(phydev))) {
			KPRINT_ERR("Failed to configure PHY interrupt port number is %d", priv->port_num);
		}
	}
	/* Enable or disable EEE Advertisement based on eee_enabled settings which might be set using module param */
	edata.eee_enabled = priv->eee_enabled;
	edata.advertised = 0;

	if (priv->phylink) {
		if (priv->plat->interface != PHY_INTERFACE_MODE_RGMII) {
			netdev_info(priv->dev, "Ethtool EEE Setting\n");
			phylink_ethtool_set_eee(priv->phylink, &edata);
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
			/* Enable EEE for 2.5G and 5G speeds, when driver is loaded with EEE ON module param. */
			netdev_info(priv->dev, "Ethtool EEE Setting for 5G/2.5G\n");
			ret |= phy_ethtool_set_eee_2p5(phydev, &edata);
#endif
		}
	}
	/* In forced speed mode, donot return error here */
	if (((priv->port_num == RM_PF1_ID) && (mac1_force_speed_mode == ENABLE)) ||
		((priv->port_num == RM_PF0_ID) && (mac0_force_speed_mode == ENABLE)))
		ret = 0;

	return ret;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))
static void tc956xmac_phylink_fixed_state(struct phylink_config *config, struct phylink_link_state *state)
#else
static void tc956xmac_phylink_fixed_state(struct net_device *dev, struct phylink_link_state *state)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))
	struct net_device *dev = to_net_dev(config->dev);
#endif
	struct tc956xmac_priv *priv = netdev_priv(dev);

	state->link = 1;
	state->duplex = DUPLEX_FULL;
	state->speed = priv->plat->forced_speed;

	DBGPR_FUNC(priv->device, "%s state->speed: %d\n", __func__, state->speed);

	return;
}

static int tc956xmac_phy_setup(struct tc956xmac_priv *priv)
{
	struct fwnode_handle *fwnode = of_fwnode_handle(priv->plat->phylink_node);
	int mode = priv->plat->phy_interface;
	struct phylink *phylink;

	priv->phylink_config.dev = &priv->dev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0))
	/* Set the platform/firmware specified interface mode */
	__set_bit(mode, priv->phylink_config.supported_interfaces);
#endif

	phylink = phylink_create(&priv->phylink_config, fwnode,
				 mode, &tc956xmac_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	/* Fixed phy mode should be set using device tree, driver just registers callback here */
	if (((priv->port_num == RM_PF1_ID) && (mac1_force_speed_mode == ENABLE)) ||
		((priv->port_num == RM_PF0_ID) && (mac0_force_speed_mode == ENABLE)))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))
		priv->phylink_config.get_fixed_state=tc956xmac_phylink_fixed_state;
#else
		phylink_fixed_state_cb(phylink, tc956xmac_phylink_fixed_state);
#endif
	priv->phylink = phylink;
	return 0;
}
#endif /*#ifdef TC956X_SRIOV_VF*/
static void tc956xmac_display_rx_rings(struct tc956xmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	void *head_rx;
	u32 queue;

	/* Display RX rings */
	for (queue = 0; queue < rx_cnt; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		pr_info("\tRX Queue %u rings\n", queue);

		if (priv->extend_desc)
			head_rx = (void *)rx_q->dma_erx;
		else
			head_rx = (void *)rx_q->dma_rx;

		/* Display RX ring */
		tc956xmac_display_ring(priv, head_rx, DMA_RX_SIZE, true);
	}
}

static void tc956xmac_display_tx_rings(struct tc956xmac_priv *priv)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	void *head_tx;
	u32 queue;

	/* Display TX rings */
	for (queue = 0; queue < tx_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		pr_info("\tTX Queue %d rings\n", queue);

		if (priv->extend_desc)
			head_tx = (void *)tx_q->dma_etx;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			head_tx = (void *)tx_q->dma_entx;
		else
			head_tx = (void *)tx_q->dma_tx;

		tc956xmac_display_ring(priv, head_tx, DMA_TX_SIZE, false);
	}
}

static void tc956xmac_display_rings(struct tc956xmac_priv *priv)
{
	/* Display RX ring */
	tc956xmac_display_rx_rings(priv);

	/* Display TX ring */
	tc956xmac_display_tx_rings(priv);
}

static int tc956xmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_8KiB)
		ret = BUF_SIZE_16KiB;
	else if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu > DEFAULT_BUFSIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DEFAULT_BUFSIZE;

	return ret;
}

/**
 * tc956xmac_clear_rx_descriptors - clear RX descriptors
 * @priv: driver private structure
 * @queue: RX queue index
 * Description: this function is called to clear the RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_rx_descriptors(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int i;

	/* Clear the RX descriptors */
	for (i = 0; i < DMA_RX_SIZE; i++)
		if (priv->extend_desc)
			tc956xmac_init_rx_desc(priv, &rx_q->dma_erx[i].basic,
					priv->use_riwt, priv->mode,
					(i == DMA_RX_SIZE - 1),
					priv->dma_buf_sz);
		else
			tc956xmac_init_rx_desc(priv, &rx_q->dma_rx[i],
					priv->use_riwt, priv->mode,
					(i == DMA_RX_SIZE - 1),
					priv->dma_buf_sz);
}

/**
 * tc956xmac_clear_tx_descriptors - clear tx descriptors
 * @priv: driver private structure
 * @queue: TX queue index.
 * Description: this function is called to clear the TX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_tx_descriptors(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	int i;

	/* Clear the TX descriptors */
	for (i = 0; i < DMA_TX_SIZE; i++) {
		int last = (i == (DMA_TX_SIZE - 1));
		struct dma_desc *p;

		if (priv->extend_desc)
			p = &tx_q->dma_etx[i].basic;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[i].basic;
		else
			p = &tx_q->dma_tx[i];

		tc956xmac_init_tx_desc(priv, p, priv->mode, last);
	}
}

/**
 * tc956xmac_clear_descriptors - clear descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the TX and RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void tc956xmac_clear_descriptors(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_queue_cnt = priv->plat->rx_queues_to_use;
#endif

	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Clear the RX descriptors */
	for (queue = 0; queue < rx_queue_cnt; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_clear_rx_descriptors(priv, queue);
	}

	/* Clear the TX descriptors */
	for (queue = 0; queue < tx_queue_cnt; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_clear_tx_descriptors(priv, queue);
	}
}

/**
 * tc956xmac_init_rx_buffers - init the RX descriptor buffer.
 * @priv: driver private structure
 * @p: descriptor pointer
 * @i: descriptor index
 * @flags: gfp flag
 * @queue: RX queue index
 * Description: this function is called to allocate a receive buffer, perform
 * the DMA mapping and init the descriptor.
 */
static int tc956xmac_init_rx_buffers(struct tc956xmac_priv *priv, struct dma_desc *p,
				  int i, gfp_t flags, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[i];

	buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
	if (!buf->page)
		return -ENOMEM;

	if (priv->sph) {
		buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
		if (!buf->sec_page)
			return -ENOMEM;

		buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);
		tc956xmac_set_desc_sec_addr(priv, p, buf->sec_addr);
	} else {
		buf->sec_page = NULL;
	}

	buf->addr = page_pool_get_dma_addr(buf->page);
	tc956xmac_set_desc_addr(priv, p, buf->addr);
	if (priv->dma_buf_sz == BUF_SIZE_16KiB)
		tc956xmac_init_desc3(priv, p);

	return 0;
}

/**
 * tc956xmac_free_rx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void tc956xmac_free_rx_buffer(struct tc956xmac_priv *priv, u32 queue, int i)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[i];

	if (buf->page)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
		page_pool_put_full_page(rx_q->page_pool, buf->page, false);
#else
		page_pool_put_page(rx_q->page_pool, buf->page, false);
#endif
	buf->page = NULL;

	if (buf->sec_page)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
		page_pool_put_full_page(rx_q->page_pool, buf->sec_page, false);
#else
		page_pool_put_page(rx_q->page_pool, buf->sec_page, false);
#endif
	buf->sec_page = NULL;
}

/**
 * tc956xmac_free_tx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void tc956xmac_free_tx_buffer(struct tc956xmac_priv *priv, u32 queue, int i)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

	if (tx_q->tx_skbuff_dma) {
		if (tx_q->tx_skbuff_dma[i].buf) {
			if (tx_q->tx_skbuff_dma[i].map_as_page)
				dma_unmap_page(priv->device,
					       tx_q->tx_skbuff_dma[i].buf,
					       tx_q->tx_skbuff_dma[i].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(priv->device,
						 tx_q->tx_skbuff_dma[i].buf,
						 tx_q->tx_skbuff_dma[i].len,
						 DMA_TO_DEVICE);
		}
	}

	if (tx_q->tx_skbuff) {
		if (tx_q->tx_skbuff[i]) {
			dev_kfree_skb_any(tx_q->tx_skbuff[i]);
			tx_q->tx_skbuff[i] = NULL;
			tx_q->tx_skbuff_dma[i].buf = 0;
			tx_q->tx_skbuff_dma[i].map_as_page = false;
		}
	}
}

/**
 * init_dma_rx_desc_rings - init the RX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_rx_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	int ret = -ENOMEM;
	int queue;
	int i;

	/* RX INITIALIZATION */
	netif_dbg(priv, probe, priv->dev,
		  "SKB addresses:\nskb\t\tskb data\tdma data\n");

	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_rx_phy=0x%08x\n", __func__,
			  (u32)rx_q->dma_rx_phy);

		tc956xmac_clear_rx_descriptors(priv, queue);

		for (i = 0; i < DMA_RX_SIZE; i++) {
			struct dma_desc *p;

			if (priv->extend_desc)
				p = &((rx_q->dma_erx + i)->basic);
			else
				p = rx_q->dma_rx + i;

			ret = tc956xmac_init_rx_buffers(priv, p, i, flags,
						     queue);
			if (ret)
				goto err_init_rx_buffers;
		}

		rx_q->cur_rx = 0;
		rx_q->dirty_rx = (unsigned int)(i - DMA_RX_SIZE);

		/* Setup the chained descriptor addresses */
		if (priv->mode == TC956XMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				tc956xmac_mode_init(priv, rx_q->dma_erx,
						rx_q->dma_rx_phy, DMA_RX_SIZE, 1);
			else
				tc956xmac_mode_init(priv, rx_q->dma_rx,
						rx_q->dma_rx_phy, DMA_RX_SIZE, 0);
		}
	}

	return 0;

err_init_rx_buffers:
	while (queue >= 0) {
#ifdef TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0) {
			i = DMA_RX_SIZE;
			queue--;
			continue;
		}
#endif
		while (--i >= 0)
			tc956xmac_free_rx_buffer(priv, queue, i);

		if (queue == 0)
			break;

		i = DMA_RX_SIZE;
		queue--;
	}

	return ret;
}

/**
 * init_dma_tx_desc_rings - init the TX descriptor rings
 * @dev: net device structure.
 * Description: this function initializes the DMA TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_tx_desc_rings(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;
	int i;

	for (queue = 0; queue < tx_queue_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		/* skip configuring for unallocated channel */
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_tx_phy=0x%08x\n", __func__,
			 (u32)tx_q->dma_tx_phy);

		/* Setup the chained descriptor addresses */
		if (priv->mode == TC956XMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				tc956xmac_mode_init(priv, tx_q->dma_etx,
						tx_q->dma_tx_phy, DMA_TX_SIZE, 1);
			else if (!(tx_q->tbs & TC956XMAC_TBS_AVAIL))
				tc956xmac_mode_init(priv, tx_q->dma_tx,
						tx_q->dma_tx_phy, DMA_TX_SIZE, 0);
		}

		for (i = 0; i < DMA_TX_SIZE; i++) {
			struct dma_desc *p;

			if (priv->extend_desc)
				p = &((tx_q->dma_etx + i)->basic);
			else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
				p = &((tx_q->dma_entx + i)->basic);
			else
				p = tx_q->dma_tx + i;

			tc956xmac_clear_desc(priv, p);

			tx_q->tx_skbuff_dma[i].buf = 0;
			tx_q->tx_skbuff_dma[i].map_as_page = false;
			tx_q->tx_skbuff_dma[i].len = 0;
			tx_q->tx_skbuff_dma[i].last_segment = false;
			tx_q->tx_skbuff[i] = NULL;
		}

		tx_q->dirty_tx = 0;
		tx_q->cur_tx = 0;
		tx_q->mss = 0;

		netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	return 0;
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret;

	ret = init_dma_rx_desc_rings(dev, flags);
	if (ret)
		return ret;

	ret = init_dma_tx_desc_rings(dev);

	tc956xmac_clear_descriptors(priv);

	if (netif_msg_hw(priv))
		tc956xmac_display_rings(priv);

	return ret;
}

/**
 * dma_free_rx_skbufs - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 */
static void dma_free_rx_skbufs(struct tc956xmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < DMA_RX_SIZE; i++)
		tc956xmac_free_rx_buffer(priv, queue, i);
}

/**
 * dma_free_tx_skbufs - free TX dma buffers
 * @priv: private structure
 * @queue: TX queue index
 */
static void dma_free_tx_skbufs(struct tc956xmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < DMA_TX_SIZE; i++)
		tc956xmac_free_tx_buffer(priv, queue, i);
}

/**
 * free_dma_rx_desc_resources - free RX dma desc resources
 * @priv: private structure
 */
static void free_dma_rx_desc_resources(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	u32 queue;

	/* Free RX queue resources */
	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		/* Release the DMA RX socket buffers */
		dma_free_rx_skbufs(priv, queue);

		/* Free DMA regions of consistent memory previously allocated */
		if (!priv->extend_desc)
			dma_free_coherent(priv->device,
					  DMA_RX_SIZE * sizeof(struct dma_desc),
					  rx_q->dma_rx, rx_q->dma_rx_phy);
		else
			dma_free_coherent(priv->device, DMA_RX_SIZE *
					  sizeof(struct dma_extended_desc),
					  rx_q->dma_erx, rx_q->dma_rx_phy);

		kfree(rx_q->buf_pool);
		if (rx_q->page_pool)
			page_pool_destroy(rx_q->page_pool);
	}
}

/**
 * free_dma_tx_desc_resources - free TX dma desc resources
 * @priv: private structure
 */
static void free_dma_tx_desc_resources(struct tc956xmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Free TX queue resources */
	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		/* Release the DMA TX socket buffers */
		dma_free_tx_skbufs(priv, queue);

		if (priv->extend_desc) {
			size = sizeof(struct dma_extended_desc);
			addr = tx_q->dma_etx;
		} else if (tx_q->tbs & TC956XMAC_TBS_AVAIL) {
			size = sizeof(struct dma_edesc);
			addr = tx_q->dma_entx;
		} else {
			size = sizeof(struct dma_desc);
			addr = tx_q->dma_tx;
		}

		size *= DMA_TX_SIZE;

		dma_free_coherent(priv->device, size, addr, tx_q->dma_tx_phy);

		kfree(tx_q->tx_skbuff_dma);
		tx_q->tx_skbuff_dma = NULL;

		kfree(tx_q->tx_skbuff);
		tx_q->tx_skbuff = NULL;

	}
}

/**
 * alloc_dma_rx_desc_resources - alloc RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_rx_desc_resources(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_count = priv->plat->rx_queues_to_use;
#endif

	int ret = -ENOMEM;
	u32 queue;

	/* RX queues buffers and DMA */
	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
		struct page_pool_params pp_params = { 0 };
		unsigned int num_pages;

#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		/* Create Rx DMA resources for Host owned channels only */
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		rx_q->queue_index = queue;
		rx_q->priv_data = priv;

		pp_params.flags = PP_FLAG_DMA_MAP;
		pp_params.pool_size = DMA_RX_SIZE;
		num_pages = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE);
		pp_params.order = ilog2(num_pages);
		pp_params.nid = dev_to_node(priv->device);
		pp_params.dev = priv->device;
		pp_params.dma_dir = DMA_FROM_DEVICE;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
		pp_params.offset = 0;
		pp_params.max_len = TC956X_MAX_RX_BUF_SIZE(num_pages);
#endif
		rx_q->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rx_q->page_pool)) {
			ret = PTR_ERR(rx_q->page_pool);
			rx_q->page_pool = NULL;
			goto err_dma;
		}

		rx_q->buf_pool = kcalloc(DMA_RX_SIZE, sizeof(*rx_q->buf_pool),
					 GFP_KERNEL);
		if (!rx_q->buf_pool)
			goto err_dma;

		if (priv->extend_desc) {
			rx_q->dma_erx = dma_alloc_coherent(priv->device,
							   DMA_RX_SIZE * sizeof(struct dma_extended_desc),
							   &rx_q->dma_rx_phy,
							   GFP_KERNEL);
			if (!rx_q->dma_erx)
				goto err_dma;

		} else {
			rx_q->dma_rx = dma_alloc_coherent(priv->device,
							  DMA_RX_SIZE * sizeof(struct dma_desc),
							  &rx_q->dma_rx_phy,
							  GFP_KERNEL);
			if (!rx_q->dma_rx)
				goto err_dma;
		}
	}

	return 0;

err_dma:
	free_dma_rx_desc_resources(priv);

	return ret;
}

/**
 * alloc_dma_tx_desc_resources - alloc TX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_tx_desc_resources(struct tc956xmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	int ret = -ENOMEM;
	u32 queue;

	/* TX queues buffers and DMA */
	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		tx_q->queue_index = queue;
		tx_q->priv_data = priv;

		tx_q->tx_skbuff_dma = kcalloc(DMA_TX_SIZE,
					      sizeof(*tx_q->tx_skbuff_dma),
					      GFP_KERNEL);
		if (!tx_q->tx_skbuff_dma)
			goto err_dma;

		tx_q->tx_skbuff = kcalloc(DMA_TX_SIZE,
					  sizeof(struct sk_buff *),
					  GFP_KERNEL);
		if (!tx_q->tx_skbuff)
			goto err_dma;

		if (priv->extend_desc)
			size = sizeof(struct dma_extended_desc);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			size = sizeof(struct dma_edesc);
		else
			size = sizeof(struct dma_desc);

		size *= DMA_TX_SIZE;

		addr = dma_alloc_coherent(priv->device, size,
					  &tx_q->dma_tx_phy, GFP_KERNEL);
		if (!addr)
			goto err_dma;

		if (priv->extend_desc)
			tx_q->dma_etx = addr;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			tx_q->dma_entx = addr;
		else
			tx_q->dma_tx = addr;
	}

	return 0;

err_dma:
	free_dma_tx_desc_resources(priv);
	return ret;
}

/**
 * alloc_dma_desc_resources - alloc TX/RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_desc_resources(struct tc956xmac_priv *priv)
{
	/* RX Allocation */
	int ret = alloc_dma_rx_desc_resources(priv);

	if (ret)
		return ret;

	ret = alloc_dma_tx_desc_resources(priv);

	return ret;
}

/**
 * free_dma_desc_resources - free dma desc resources
 * @priv: private structure
 */
static void free_dma_desc_resources(struct tc956xmac_priv *priv)
{
	/* Release the DMA RX socket buffers */
	free_dma_rx_desc_resources(priv);

	/* Release the DMA TX socket buffers */
	free_dma_tx_desc_resources(priv);
}

#ifndef TC956X_SRIOV_VF
/**
 *  tc956xmac_mac_enable_rx_queues - Enable MAC rx queues
 *  @priv: driver private structure
 *  Description: It is used for enabling the rx queues in the MAC
 */
static void tc956xmac_mac_enable_rx_queues(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	int queue;
	u8 mode;

	for (queue = 0; queue < rx_queues_count; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_q_in_use[queue] == TC956X_DISABLE_QUEUE)
			continue;
#endif
		mode = priv->plat->rx_queues_cfg[queue].mode_to_use;
		tc956xmac_rx_queue_enable(priv, priv->hw, mode, queue);
	}
}
#endif /* TC956X_SRIOV_VF */
/**
 * tc956xmac_start_rx_dma - start RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This starts a RX DMA channel
 */
static void tc956xmac_start_rx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes started in channel %d\n", chan);
	tc956xmac_start_rx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_start_tx_dma - start TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This starts a TX DMA channel
 */
static void tc956xmac_start_tx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes started in channel %d\n", chan);
	tc956xmac_start_tx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_stop_rx_dma - stop RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This stops a RX DMA channel
 */
static void tc956xmac_stop_rx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes stopped in channel %d\n", chan);
	tc956xmac_stop_rx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_stop_tx_dma - stop TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This stops a TX DMA channel
 */
static void tc956xmac_stop_tx_dma(struct tc956xmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes stopped in channel %d\n", chan);
	tc956xmac_stop_tx(priv, priv->ioaddr, chan);
}

/**
 * tc956xmac_start_all_dma - start all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This starts all the RX and TX DMA channels
 */
static void tc956xmac_start_all_dma(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_start_rx_dma(priv, chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_start_tx_dma(priv, chan);
	}
}

/**
 * tc956xmac_stop_all_dma - stop all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This stops the RX and TX DMA channels
 */
static void tc956xmac_stop_all_dma(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_stop_rx_dma(priv, chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_stop_tx_dma(priv, chan);
	}
}

/**
 *  tc956xmac_dma_operation_mode - HW DMA operation mode
 *  @priv: driver private structure
 *  Description: it is used for configuring the DMA operation mode register in
 *  order to program the tx/rx DMA thresholds or Store-And-Forward mode.
 */
static void tc956xmac_dma_operation_mode(struct tc956xmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = 0;
	u32 rxmode = 0;
	u32 chan = 0;
	u8 qmode = 0;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	if (priv->plat->force_thresh_dma_mode) {
		txmode = tc;
		rxmode = tc;
	} else if (priv->plat->force_sf_dma_mode || priv->plat->tx_coe) {
		/*
		 * In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *    that needs to not insert csum in the TDES.
		 */
		txmode = SF_DMA_MODE;
		rxmode = SF_DMA_MODE;
		priv->xstats.threshold = SF_DMA_MODE;
	} else {
		txmode = tc;
		rxmode = SF_DMA_MODE;
	}

#ifndef TC956X_SRIOV_VF
	/* configure all channels */
	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_q_in_use[chan] == TC956X_DISABLE_QUEUE)
			continue;
#endif
		switch (chan) {
		case 0:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
			rxfifosz = priv->plat->rx_queues_cfg[0].size;
#else
			rxfifosz = RX_QUEUE0_SIZE;
#endif
			break;
		case 1:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
			rxfifosz = priv->plat->rx_queues_cfg[1].size;
#else
			rxfifosz = RX_QUEUE1_SIZE;
#endif
			break;
		case 2:
			rxfifosz = RX_QUEUE2_SIZE;
			break;
		case 3:
			rxfifosz = RX_QUEUE3_SIZE;
			break;
		case 4:
			rxfifosz = RX_QUEUE4_SIZE;
			break;
		case 5:
			rxfifosz = RX_QUEUE5_SIZE;
			break;
		case 6:
			rxfifosz = RX_QUEUE6_SIZE;
			break;
		case 7:
			rxfifosz = RX_QUEUE7_SIZE;
			break;
		default:
			rxfifosz = RX_QUEUE0_SIZE;
			break;
		}
#endif

		qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;

		tc956xmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan,
				rxfifosz, qmode);
	}
#endif

	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
	/* configure only size in DMA register for the
	 * channels used by VF, other channels skip
	 */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
#endif
			tc956xmac_set_dma_bfsize(priv, priv->ioaddr, priv->dma_buf_sz,
						chan);
#endif
	}


	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_q_in_use[chan] == TC956X_DISABLE_QUEUE)
			continue;

		switch (chan) {
		case 0:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
			txfifosz = priv->plat->tx_queues_cfg[0].size;
#else
			txfifosz = TX_QUEUE0_SIZE;
#endif
			break;
		case 1:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
			txfifosz = priv->plat->tx_queues_cfg[1].size;
#else
			txfifosz = TX_QUEUE1_SIZE;
#endif
			break;
		case 2:
			txfifosz = TX_QUEUE2_SIZE;
			break;
		case 3:
			txfifosz = TX_QUEUE3_SIZE;
			break;
		case 4:
			txfifosz = TX_QUEUE4_SIZE;
			break;
		case 5:
			txfifosz = TX_QUEUE5_SIZE;
			break;
		case 6:
			txfifosz = TX_QUEUE6_SIZE;
			break;
		case 7:
			txfifosz = TX_QUEUE7_SIZE;
			break;
		default:
			txfifosz = TX_QUEUE0_SIZE;
			break;
		}
#elif defined TC956X_SRIOV_VF
		/* configure only size in DMA register for the
		 * channels used by VF, other channels skip
		 */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;

		txfifosz = priv->plat->tx_q_size[chan];
#endif	/* TC956X_SRIOV_VF */
#endif

		qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;

		/* Use mailbox to set tx mode */
#ifdef TC956X_SRIOV_PF
		tc956xmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				txfifosz, qmode);
#elif defined TC956X_SRIOV_VF
		tc956xmac_dma_tx_mode(priv, txmode, chan,
				txfifosz, qmode);
#endif

	}
}

/**
 * tc956xmac_tx_clean - to manage the transmission completion
 * @priv: driver private structure
 * @queue: TX queue index
 * Description: it reclaims the transmit resources after transmission completes.
 */
static int tc956xmac_tx_clean(struct tc956xmac_priv *priv, int budget, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	unsigned int bytes_compl = 0, pkts_compl = 0;
	unsigned int entry, count = 0;

	__netif_tx_lock_bh(netdev_get_tx_queue(priv->dev, queue));

	priv->xstats.tx_clean[queue]++;

	entry = tx_q->dirty_tx;
	while ((entry != tx_q->cur_tx) && (count < budget)) {
		struct sk_buff *skb = tx_q->tx_skbuff[entry];
		struct dma_desc *p;
		int status;

		if (priv->extend_desc)
			p = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[entry].basic;
		else
			p = tx_q->dma_tx + entry;

		status = tc956xmac_tx_status(priv, &priv->dev->stats,
				&priv->xstats, p, priv->ioaddr);
		/* Check if the descriptor is owned by the DMA */
		if (unlikely(status & tx_dma_own))
			break;

		count++;

		/* Make sure descriptor fields are read after reading
		 * the own bit.
		 */
		dma_rmb();

		/* Just consider the last segment and ...*/
		if (likely(!(status & tx_not_ls))) {
			/* ... verify the status error condition */
			if (unlikely(status & tx_err)) {
				priv->dev->stats.tx_errors++;
				priv->xstats.tx_pkt_errors_n[queue]++;
			} else {
				priv->dev->stats.tx_packets++;
				priv->xstats.tx_pkt_n[queue]++;
			}
#if defined(TX_LOGGING_TRACE)
			if (queue != skb_get_queue_mapping(skb))
				pr_err("Tx Queue no. is different\n");
#endif
			tc956xmac_get_tx_hwtstamp(priv, p, skb);
		}

		if (likely(tx_q->tx_skbuff_dma[entry].buf)) {
			if (tx_q->tx_skbuff_dma[entry].map_as_page)
				dma_unmap_page(priv->device,
					       tx_q->tx_skbuff_dma[entry].buf,
					       tx_q->tx_skbuff_dma[entry].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(priv->device,
						 tx_q->tx_skbuff_dma[entry].buf,
						 tx_q->tx_skbuff_dma[entry].len,
						 DMA_TO_DEVICE);
			tx_q->tx_skbuff_dma[entry].buf = 0;
			tx_q->tx_skbuff_dma[entry].len = 0;
			tx_q->tx_skbuff_dma[entry].map_as_page = false;
		}

		tc956xmac_clean_desc3(priv, tx_q, p);

		tx_q->tx_skbuff_dma[entry].last_segment = false;
		tx_q->tx_skbuff_dma[entry].is_jumbo = false;

		if (likely(skb != NULL)) {
			pkts_compl++;
			bytes_compl += skb->len;
			dev_consume_skb_any(skb);
			tx_q->tx_skbuff[entry] = NULL;
		}

		tc956xmac_release_tx_desc(priv, p, priv->mode);

		entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	}
	tx_q->dirty_tx = entry;

	netdev_tx_completed_queue(netdev_get_tx_queue(priv->dev, queue),
				  pkts_compl, bytes_compl);

	if (unlikely(netif_tx_queue_stopped(netdev_get_tx_queue(priv->dev, queue))) &&
	    tc956xmac_tx_avail(priv, queue) > TC956XMAC_TX_THRESH) {

		netif_dbg(priv, tx_done, priv->dev,
			  "%s: restart transmit\n", __func__);
		netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, queue));
	}
#ifdef ENABLE_TX_TIMER
	/* We still have pending packets, let's call for a new scheduling */
	if (tx_q->dirty_tx != tx_q->cur_tx) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		hrtimer_start(&tx_q->txtimer,
			TC956XMAC_COAL_TIMER(priv->tx_coal_timer[queue]), HRTIMER_MODE_REL);
#else
		mod_timer(&tx_q->txtimer, TC956XMAC_COAL_TIMER(priv->tx_coal_timer));
#endif
	}
#endif
	__netif_tx_unlock_bh(netdev_get_tx_queue(priv->dev, queue));

	return count;
}

/**
 * tc956xmac_tx_err - to manage the tx error
 * @priv: driver private structure
 * @chan: channel index
 * Description: it cleans the descriptors and restarts the transmission
 * in case of transmission errors.
 */
static void tc956xmac_tx_err(struct tc956xmac_priv *priv, u32 chan)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];

	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, chan));

	tc956xmac_stop_tx_dma(priv, chan);
	dma_free_tx_skbufs(priv, chan);
	tc956xmac_clear_tx_descriptors(priv, chan);
	tx_q->dirty_tx = 0;
	tx_q->cur_tx = 0;
	tx_q->mss = 0;
	netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, chan));
	tc956xmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    tx_q->dma_tx_phy, chan);
	tc956xmac_start_tx_dma(priv, chan);

	priv->dev->stats.tx_errors++;
	netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, chan));
}

#ifndef TC956X_SRIOV_VF
/**
 *  tc956xmac_set_dma_operation_mode - Set DMA operation mode by channel
 *  @priv: driver private structure
 *  @txmode: TX operating mode
 *  @rxmode: RX operating mode
 *  @chan: channel index
 *  Description: it is used for configuring of the DMA operation mode in
 *  runtime in order to program the tx/rx DMA thresholds or Store-And-Forward
 *  mode.
 */
static void tc956xmac_set_dma_operation_mode(struct tc956xmac_priv *priv, u32 txmode,
					  u32 rxmode, u32 chan)
{
	u8 rxqmode = priv->plat->rx_queues_cfg[chan].mode_to_use;
	u8 txqmode = priv->plat->tx_queues_cfg[chan].mode_to_use;
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

#ifndef TC956X_SRIOV_VF
#ifdef TC956X
	switch (chan) {
	case 0:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
		rxfifosz = priv->plat->rx_queues_cfg[0].size;
		txfifosz = priv->plat->tx_queues_cfg[0].size;
#else
		rxfifosz = RX_QUEUE0_SIZE;
		txfifosz = TX_QUEUE0_SIZE;
#endif
		break;
	case 1:
#if defined(TC956X_AUTOMOTIVE_CONFIG)
		rxfifosz = priv->plat->rx_queues_cfg[1].size;
		txfifosz = priv->plat->tx_queues_cfg[1].size;
#else
		rxfifosz = RX_QUEUE1_SIZE;
		txfifosz = TX_QUEUE1_SIZE;
#endif
		break;
	case 2:
		rxfifosz = RX_QUEUE2_SIZE;
		txfifosz = TX_QUEUE2_SIZE;
		break;
	case 3:
		rxfifosz = RX_QUEUE3_SIZE;
		txfifosz = TX_QUEUE3_SIZE;
		break;
	case 4:
		rxfifosz = RX_QUEUE4_SIZE;
		txfifosz = TX_QUEUE4_SIZE;
		break;
	case 5:
		rxfifosz = RX_QUEUE5_SIZE;
		txfifosz = TX_QUEUE5_SIZE;
		break;
	case 6:
		rxfifosz = RX_QUEUE6_SIZE;
		txfifosz = TX_QUEUE6_SIZE;
		break;
	case 7:
		rxfifosz = RX_QUEUE7_SIZE;
		txfifosz = TX_QUEUE7_SIZE;
		break;
	default:
		rxfifosz = RX_QUEUE0_SIZE;
		txfifosz = TX_QUEUE0_SIZE;
		break;
	}
#endif

	tc956xmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan, rxfifosz, rxqmode);
#endif /* TC956X_SRIOV_VF */

	/* Use mailbox to set tx mode */
#ifndef TC956X_SRIOV_VF
	tc956xmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan, txfifosz, txqmode);
#elif (defined TC956X_SRIOV_VF)
	tc956xmac_dma_tx_mode(priv, priv, txmode, chan, txfifosz, txqmode);
#endif
}
#endif /*#ifdef TC956X_SRIOV_VF*/

#ifndef TC956X_SRIOV_VF
static bool tc956xmac_safety_feat_interrupt(struct tc956xmac_priv *priv)
{
	int ret;

	ret = tc956xmac_safety_feat_irq_status(priv, priv->dev,
			priv->ioaddr, priv->dma_cap.asp, &priv->sstats);
	if (ret && (ret != -EINVAL)) {
		tc956xmac_global_err(priv);
		return true;
	}

	return false;
}
#endif

static int tc956xmac_napi_check(struct tc956xmac_priv *priv, u32 chan)
{
	int status = tc956xmac_dma_interrupt_status(priv, priv->ioaddr,
						 &priv->xstats, chan);
	struct tc956xmac_channel *ch = &priv->channel[chan];
	unsigned long flags;

#ifdef TC956X
#ifdef TC956X_SRIOV_PF
	if ((status & handle_rx) && (chan < priv->plat->rx_queues_to_use) && (priv->plat->rx_ch_in_use[chan] == TC956X_ENABLE_CHNL)) {
#elif defined TC956X_SRIOV_VF
	if ((status & handle_rx) && (chan < priv->plat->rx_queues_to_use) &&
					(priv->plat->ch_in_use[chan] == 1)) {
#else
	if ((status & handle_rx) && (chan < priv->plat->rx_queues_to_use) &&
		(priv->plat->rx_dma_ch_owner[chan] == USE_IN_TC956X_SW)) {
#endif
#endif

		if (napi_schedule_prep(&ch->rx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			tc956xmac_disable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule_irqoff(&ch->rx_napi);
		}
	}

#ifdef TC956X_SRIOV_PF
	if ((status & handle_tx) && (chan < priv->plat->tx_queues_to_use) && (priv->plat->tx_ch_in_use[chan] == TC956X_ENABLE_CHNL)) {
#elif defined TC956X_SRIOV_VF
	if ((status & handle_tx) && (chan < priv->plat->tx_queues_to_use) &&
					(priv->plat->ch_in_use[chan] == 1)) {
#else
	if ((status & handle_tx) && (chan < priv->plat->tx_queues_to_use) &&
		(priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)) {
#endif
#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));

#endif
		if (napi_schedule_prep(&ch->tx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			tc956xmac_disable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule_irqoff(&ch->tx_napi);
		}
	}

	return status;
}

/**
 * tc956xmac_dma_interrupt - DMA ISR
 * @priv: driver private structure
 * Description: this is the DMA ISR. It is called by the main ISR.
 * It calls the dwmac dma routine and schedule poll method in case of some
 * work can be done.
 */
static void tc956xmac_dma_interrupt(struct tc956xmac_priv *priv)
{
	u32 tx_channel_count = priv->plat->tx_queues_to_use;

#ifdef TC956X
	u32 rx_channel_count = priv->plat->rx_queues_to_use;
#endif
	u32 channels_to_check = tx_channel_count > rx_channel_count ?
				tx_channel_count : rx_channel_count;
	u32 chan;
	int status[max_t(u32, MTL_MAX_TX_QUEUES, MTL_MAX_RX_QUEUES)];

	/* Make sure we never check beyond our status buffer. */
	if (WARN_ON_ONCE(channels_to_check > ARRAY_SIZE(status)))
		channels_to_check = ARRAY_SIZE(status);

	for (chan = 0; chan < channels_to_check; chan++) {
#ifdef TC956X_SRIOV_PF
		if ((priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL) && (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL))
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#endif
		status[chan] = tc956xmac_napi_check(priv, chan);
	}

	for (chan = 0; chan < tx_channel_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#endif
		if (unlikely(status[chan] & tx_hard_error_bump_tc)) {
			/* Try to bump up the dma threshold on this failure */
			if (unlikely(priv->xstats.threshold != SF_DMA_MODE) &&
			    (tc <= 256)) {
				tc += 64;

				// Note: Threshold mode is not used as per configuration. And setting of Rx DMA operation mode will be
				// tricky as Rx queue is shared among more than on VF, this operation may affect other VF/PF
				// So as of now this code is not used in VF driver
#ifndef TC956X_SRIOV_VF
				if (priv->plat->force_thresh_dma_mode)
					tc956xmac_set_dma_operation_mode(priv,
								      tc,
								      tc,
								      chan);
				else
					tc956xmac_set_dma_operation_mode(priv,
								    tc,
								    SF_DMA_MODE,
								    chan);
#endif
				priv->xstats.threshold = tc;
			}
		} else if (unlikely(status[chan] == tx_hard_error)) {
			tc956xmac_tx_err(priv, chan);
		}
	}
}

/**
 * tc956xmac_mmc_setup: setup the Mac Management Counters (MMC)
 * @priv: driver private structure
 * Description: this masks the MMC irq, in fact, the counters are managed in SW.
 */
static void tc956xmac_mmc_setup(struct tc956xmac_priv *priv)
{
#ifndef TC956X_SRIOV_VF //CPE_DRV
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
			    MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	tc956xmac_mmc_intr_all_mask(priv, priv->mmcaddr);

	if (priv->dma_cap.rmon) {
		tc956xmac_mmc_ctrl(priv, priv->mmcaddr, mode);
		memset(&priv->mmc, 0, sizeof(struct tc956xmac_counters));
	} else
		netdev_info(priv->dev, "No MAC Management Counters available\n");
#else
	memset(&priv->sw_stats, 0, sizeof(struct tc956x_sw_counters));
#endif
}

/**
 * tc956xmac_get_hw_features - get MAC capabilities from the HW cap. register.
 * @priv: driver private structure
 * Description:
 *  new GMAC chip generations have a new register to indicate the
 *  presence of the optional feature/functions.
 *  This can be also used to override the value passed through the
 *  platform and necessary for old MAC10/100 and GMAC chips.
 */
static int tc956xmac_get_hw_features(struct tc956xmac_priv *priv)
{
	return tc956xmac_get_hw_feature(priv, priv->ioaddr, &priv->dma_cap) == 0;
}

#ifdef TC956X_SRIOV_VF
/**
 * tc956xmac_check_ether_addr - check if the MAC addr is valid
 * @priv: driver private structure
 * Description:
 * it is to verify if the MAC address is valid, in case of failures it
 * generates a random MAC address
 */
static void tc956xmac_check_ether_addr(struct tc956xmac_priv *priv)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	u8 addr[ETH_ALEN];
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

#ifndef TC956X_SRIOV_VF
	if (!is_valid_ether_addr(priv->dev->dev_addr)) {

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		tc956xmac_get_umac_addr(priv, priv->hw, addr, 0);
#else
		tc956xmac_get_umac_addr(priv, priv->hw, priv->dev->dev_addr, 0);
#endif

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		if (is_valid_ether_addr(addr))
			eth_hw_addr_set(priv->dev, addr);
		else
#else
		if (!is_valid_ether_addr(priv->dev->dev_addr))
#endif
			eth_hw_addr_random(priv->dev);
		dev_info(priv->device, "device MAC address %pM\n",
			 priv->dev->dev_addr);
#ifndef TC956X_SRIOV_VF
	}
#endif
}
#endif

/**
 * tc956xmac_init_dma_engine - DMA init.
 * @priv: driver private structure
 * Description:
 * It inits the DMA invoking the specific MAC/GMAC callback.
 * Some DMA parameters can be passed from the platform;
 * in case of these are not passed a default is kept for the MAC or GMAC.
 */
static int tc956xmac_init_dma_engine(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	struct tc956xmac_rx_queue *rx_q;
	struct tc956xmac_tx_queue *tx_q;
	u32 chan = 0;
	int atds = 0;
	int ret = 0;

	if (!priv->plat->dma_cfg || !priv->plat->dma_cfg->txpbl ||
	    !priv->plat->dma_cfg->rxpbl) {
		dev_err(priv->device, "Invalid DMA configuration\n");
		return -EINVAL;
	}

	if (priv->extend_desc && (priv->mode == TC956XMAC_RING_MODE))
		atds = 1;

#ifndef TC956X_SRIOV_VF
	ret = tc956xmac_reset(priv, priv->ioaddr);
	if (ret) {
		dev_err(priv->device, "Failed to reset the dma\n");
		return ret;
	}

	/* DMA Configuration */
	tc956xmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg, atds);

	if (priv->plat->axi)
		tc956xmac_axi(priv, priv->ioaddr, priv->plat->axi);
#endif

	/* DMA CSR Channel configuration */
	for (chan = 0; chan < dma_csr_ch; chan++) {
#ifdef TC956X_SRIOV_PF
		if ((priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL) &&
		(priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL))
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#endif
		tc956xmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);
	}

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		rx_q = &priv->rx_queue[chan];

		tc956xmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg, rx_q->dma_rx_phy, chan);

		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (DMA_RX_SIZE * sizeof(struct dma_desc));
		tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, chan);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tx_q = &priv->tx_queue[chan];

		tc956xmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    tx_q->dma_tx_phy, chan);

		tx_q->tx_tail_addr = tx_q->dma_tx_phy;
		tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);
	}

	return ret;
}

#ifdef ENABLE_TX_TIMER
static void tc956xmac_tx_timer_arm(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
	hrtimer_start(&tx_q->txtimer,
		TC956XMAC_COAL_TIMER(priv->tx_coal_timer[queue]), HRTIMER_MODE_REL);
#else
	mod_timer(&tx_q->txtimer, TC956XMAC_COAL_TIMER(priv->tx_coal_timer));
#endif
}

/**
 * tc956xmac_tx_timer - mitigation sw timer for tx.
 * @data: data pointer
 * Description:
 * This is the timer handler to directly invoke the tc956xmac_tx_clean.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
static enum hrtimer_restart tc956xmac_tx_timer(struct hrtimer *t)
#else
static void tc956xmac_tx_timer(struct timer_list *t)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
	struct tc956xmac_tx_queue *tx_q = container_of(t, struct tc956xmac_tx_queue, txtimer);
#else
	struct tc956xmac_tx_queue *tx_q = from_timer(tx_q, t, txtimer);
#endif
	struct tc956xmac_priv *priv = tx_q->priv_data;
	struct tc956xmac_channel *ch;

	ch = &priv->channel[tx_q->queue_index];

#ifdef TC956X_SRIOV_PF
	if (priv->plat->tx_ch_in_use[tx_q->queue_index] == TC956X_DISABLE_CHNL)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		return HRTIMER_NORESTART;
#else
		return;
#endif
#elif defined TC956X_SRIOV_VF
	if (priv->plat->ch_in_use[tx_q->queue_index] == 0)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		return HRTIMER_NORESTART;
#else
		return;
#endif
#else
	if (priv->plat->tx_dma_ch_owner[tx_q->queue_index] != USE_IN_TC956X_SW)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		return HRTIMER_NORESTART;
#else
		return;
#endif
#endif
	/*
	 * If NAPI is already running we can miss some events. Let's rearm
	 * the timer and try again.
	 */
	if (likely(napi_schedule_prep(&ch->tx_napi))) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_disable_dma_irq(priv, priv->ioaddr, ch->index, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
		__napi_schedule(&ch->tx_napi);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		return HRTIMER_NORESTART;
#else
		return;
#endif
}

#endif

/**
 * tc956xmac_init_coalesce - init mitigation options.
 * @priv: driver private structure
 * Description:
 * This inits the coalesce parameters: i.e. timer rate,
 * timer handler and default threshold used for enabling the
 * interrupt on completion bit.
 */
static void tc956xmac_init_coalesce(struct tc956xmac_priv *priv)
{
#ifdef ENABLE_TX_TIMER
	u32 tx_channel_count = priv->plat->tx_queues_to_use;
	u32 chan;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	u32 rx_channel_count = priv->plat->rx_queues_to_use;
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0))
	priv->tx_coal_frames = TC956XMAC_TX_FRAMES;
	priv->tx_coal_timer = TC956XMAC_COAL_TX_TIMER;
	priv->rx_coal_frames = TC956XMAC_RX_FRAMES;
#endif

#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < tx_channel_count; chan++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
		priv->tx_coal_frames[chan] = TC956XMAC_TX_FRAMES;
		priv->tx_coal_timer[chan] = TC956XMAC_COAL_TX_TIMER;
#endif
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
		hrtimer_init(&tx_q->txtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		tx_q->txtimer.function = tc956xmac_tx_timer;
#else
		timer_setup(&tx_q->txtimer, tc956xmac_tx_timer, 0);
#endif
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	for (chan = 0; chan < rx_channel_count; chan++)
		priv->rx_coal_frames[chan] = TC956XMAC_RX_FRAMES;
#endif
#endif
}

static void tc956xmac_set_rings_length(struct tc956xmac_priv *priv)
{
#ifdef TC956X
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
#endif

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan;

	/* set TX ring length */
	for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_set_tx_ring_len(priv, priv->ioaddr,
				(DMA_TX_SIZE - 1), chan);
	}

	/* set RX ring length */
	for (chan = 0; chan < rx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[chan] != USE_IN_TC956X_SW)
			continue;
#endif
		tc956xmac_set_rx_ring_len(priv, priv->ioaddr,
				(DMA_RX_SIZE - 1), chan);
	}
}

/**
 *  tc956xmac_set_tx_queue_weight - Set TX queue weight
 *  @priv: driver private structure
 *  Description: It is used for setting TX queues weight
 */
static void tc956xmac_set_tx_queue_weight(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 weight;
	u32 queue, traffic_class;

	/* Set weights for Traffic class based on Queue enable state */

	for (queue = 0; queue < tx_queues_count; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_q_in_use[queue] == TC956X_DISABLE_QUEUE)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated queue */
		if (priv->plat->tx_q_in_use[queue] == 0)
			continue;
#endif

		traffic_class = priv->plat->tx_queues_cfg[queue].traffic_class;
#ifdef TC956X_SRIOV_VF
		/* TC0 related configuration is done in PF only */
		if (traffic_class == TX_TC_ZERO)
			continue;
#endif
		weight = priv->plat->tx_queues_cfg[queue].weight;

		/* Use mailbox wrapper API to pass to PF for updation */
#ifdef TC956X_SRIOV_PF
		tc956xmac_set_mtl_tx_queue_weight(priv, priv->hw, weight, traffic_class);
#elif (defined TC956X_SRIOV_VF)
		tc956xmac_set_mtl_tx_queue_weight(priv, weight, traffic_class);
#endif

	}
}

/**
 *  tc956xmac_configure_cbs - Configure CBS in TX queue
 *  @priv: driver private structure
 *  Description: It is used for configuring CBS in AVB TX queues
 */
static void tc956xmac_configure_cbs(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 mode_to_use;
	u32 queue;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	/* queue 0 is reserved for legacy traffic */
	for (queue = 1; queue < tx_queues_count; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_q_in_use[queue] == TC956X_DISABLE_QUEUE)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated queue */
		if (priv->plat->tx_q_in_use[queue] == 0)
			continue;
#endif
		mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
		if (mode_to_use != MTL_QUEUE_AVB)
			continue;

/* Mailbox to be used for CBS configuration */
#ifdef TC956X_SRIOV_PF
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
		tc956xmac_config_cbs(priv, priv->hw,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
#elif (defined TC956X_SRIOV_VF)
		tc956xmac_config_cbs(priv,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
#endif

	}
}

#ifndef TC956X_SRIOV_VF
/**
 *  tc956xmac_rx_queue_dma_chan_map - Map RX queue to RX dma channel
 *  @priv: driver private structure
 *  Description: It is used for mapping RX queues to RX dma channels
 */
static void tc956xmac_rx_queue_dma_chan_map(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 chan;

	for (queue = 0; queue < rx_queues_count; queue++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_q_in_use[queue] == TC956X_DISABLE_QUEUE)
			continue;
#endif
		/* Enable DA based Queue-Ch mapping for elabled Queues */
		chan = priv->plat->rx_queues_cfg[queue].chan;
		tc956xmac_map_mtl_to_dma(priv, priv->hw, queue, chan);
	}
}

/**
 *  tc956xmac_mac_config_rx_queues_prio - Configure RX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX Queue Priority
 */
static void tc956xmac_mac_config_rx_queues_prio(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 prio;

	for (queue = 0; queue < rx_queues_count; queue++) {
		if (!priv->plat->rx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->rx_queues_cfg[queue].prio;
		tc956xmac_rx_queue_prio(priv, priv->hw, prio, queue);
	}
}
#endif
/**
 *  tc956xmac_mac_config_tx_queues_prio - Configure TX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the TX Queue Priority
 */
static void tc956xmac_mac_config_tx_queues_prio(struct tc956xmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 queue;
	u32 prio;
	u32 traffic_class;

	for (queue = 0; queue < tx_queues_count; queue++) {
#ifdef TC956X_SRIOV_VF
		/* skip configuring for unallocated queue */
		if (priv->plat->tx_q_in_use[queue] == 0)
			continue;
#endif
		if (!priv->plat->tx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->tx_queues_cfg[queue].prio;

		traffic_class = priv->plat->tx_queues_cfg[queue].traffic_class;

#ifdef TC956X_SRIOV_PF
		tc956xmac_tx_queue_prio(priv, priv->hw, prio, traffic_class);
#elif defined TC956X_SRIOV_VF
		tc956xmac_tx_queue_prio(priv, prio, traffic_class);
#else
		tc956xmac_tx_queue_prio(priv, priv->hw, prio, queue);
#endif
	}
}

#ifndef TC956X_SRIOV_VF

/**
 *  tc956xmac_mac_config_rx_queues_routing - Configure RX Queue Routing
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX queue routing
 */
static void tc956xmac_mac_config_rx_queues_routing(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u8 packet;

	for (queue = 0; queue < rx_queues_count; queue++) {
		/* no specific packet type routing specified for the queue */
		if (priv->plat->rx_queues_cfg[queue].pkt_route == 0x0)
			continue;

		packet = priv->plat->rx_queues_cfg[queue].pkt_route;
		tc956xmac_rx_queue_routing(priv, priv->hw, packet, queue);
	}
}

static void tc956xmac_mac_config_rss(struct tc956xmac_priv *priv)
{
	if (!priv->dma_cap.rssen || !priv->plat->rss_en) {
		priv->rss.enable = false;
		return;
	}

	if (priv->dev->features & NETIF_F_RXHASH)
		priv->rss.enable = true;
	else
		priv->rss.enable = false;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	tc956xmac_rss_configure(priv, priv->hw, &priv->rss,
			     priv->plat->rx_queues_to_use);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
}
#endif /* TC956X_SRIOV_VF */
/**
 *  tc956xmac_mtl_configuration - Configure MTL
 *  @priv: driver private structure
 *  Description: It is used for configurring MTL
 */
static void tc956xmac_mtl_configuration(struct tc956xmac_priv *priv)
{
#ifndef TC956X_SRIOV_VF
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
#endif
	u32 tx_queues_count = priv->plat->tx_queues_to_use;

	if (tx_queues_count > 1)
		tc956xmac_set_tx_queue_weight(priv);

#ifndef TC956X_SRIOV_VF
	/* Configure MTL RX algorithms */
	if (rx_queues_count > 1)
		tc956xmac_prog_mtl_rx_algorithms(priv, priv->hw,
				priv->plat->rx_sched_algorithm);

	/* Configure MTL TX algorithms */
	if (tx_queues_count > 1)
		tc956xmac_prog_mtl_tx_algorithms(priv, priv->hw,
				priv->plat->tx_sched_algorithm);
#endif

	/* Configure CBS in AVB TX queues */
	if (tx_queues_count > 1)
		tc956xmac_configure_cbs(priv);

/* Some of Rx queue are shared among more than one VF (DMA channels),
 * so Rx queue configuration will be done by PF commonly
 */
#ifndef TC956X_SRIOV_VF
	/* Map RX MTL to DMA channels */
	tc956xmac_rx_queue_dma_chan_map(priv);

	/* Enable MAC RX Queues */
	tc956xmac_mac_enable_rx_queues(priv);

	/* Set RX priorities */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rx_queues_prio(priv);

	/* Set TX priorities */
	if (tx_queues_count > 1)
		tc956xmac_mac_config_tx_queues_prio(priv);

	/* Set RX routing */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rx_queues_routing(priv);

	/* Receive Side Scaling */
	if (rx_queues_count > 1)
		tc956xmac_mac_config_rss(priv);
#else
	/* Set TX priorities */
	if (tx_queues_count > 1)
		tc956xmac_mac_config_tx_queues_prio(priv);
#endif
}

#ifndef TC956X_SRIOV_VF

static void tc956xmac_safety_feat_configuration(struct tc956xmac_priv *priv)
{
	if (priv->dma_cap.asp) {
		netdev_info(priv->dev, "Enabling Safety Features\n");
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
		tc956xmac_safety_feat_config(priv, priv->ioaddr, priv->dma_cap.asp);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	} else {
		netdev_info(priv->dev, "No Safety Features support found\n");
	}
}

/**
 * tc956x_rx_crc_pad_config - Rx packets CRC and Padding configuration.
 *
 * @priv: pointer to private structure
 * @crc_pad: CRC padding configuration
 */
static void tc956x_rx_crc_pad_config(struct tc956xmac_priv *priv, u32 crc_pad)
{

#ifdef TC956X
	u32 value = readl(priv->ioaddr + XGMAC_RX_CONFIG);

	if (crc_pad & XGMAC_CONFIG_CST)
		value |= XGMAC_CONFIG_CST;
	else
		value &= ~XGMAC_CONFIG_CST;

	if (crc_pad & XGMAC_CONFIG_ACS)
		value |= XGMAC_CONFIG_ACS;
	else
		value &= ~XGMAC_CONFIG_ACS;

	writel(value, priv->ioaddr + XGMAC_RX_CONFIG);
#endif
}
#endif
/**
 * tc956xmac_hw_setup - setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  Description:
 *  this is the main function to setup the HW in a usable state because the
 *  dma engine is reset, the core registers are configured (e.g. AXI,
 *  Checksum features, timers). The DMA is ready to start receiving and
 *  transmitting.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_hw_setup(struct net_device *dev, bool init_ptp)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u32 rx_cnt = priv->plat->rx_queues_to_use;
#ifndef TC956X_SRIOV_VF
	bool enable_en = true;
#endif
#endif
	u8 dev_addr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	u32 queue;
#endif
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 chan;
	int ret;

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	/* Back up MMC registers into internal SW MMC counters */
	if (priv->link_down_rst == true)
		tc956xmac_mmc_read(priv, priv->mmcaddr, &priv->mmc);

	/* DMA initialization and SW reset */
	ret = tc956xmac_init_dma_engine(priv);
	if (ret < 0) {
		netdev_err(priv->dev, 
		"%s: DMA engine initialization failed check availability of clock if supplied from external source/PHY \n",
			   __func__);
		return ret;
	}

#ifdef TC956X_SRIOV_PF
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
	/* Copy the MAC addr into the HW  */
	tc956xmac_set_umac_addr(priv, priv->hw, (unsigned char *)dev->dev_addr, HOST_MAC_ADDR_OFFSET, PF_DRIVER);

	/* Adding Broadcast address to offset 0 to divert Rx packet to PF Legacy Channel */
	tc956xmac_set_umac_addr(priv, priv->hw, &dev_addr[0], HOST_BC_ADDR_OFFSET, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

#elif defined TC956X_SRIOV_VF
	ret = -EBUSY;
	while (ret == -EBUSY)
		ret = tc956xmac_set_umac_addr(priv, dev_addr, 0);
	ret = -EBUSY;
	while (ret == -EBUSY)
		ret = tc956xmac_set_umac_addr(priv, dev->dev_addr, HOST_MAC_ADDR_OFFSET + priv->fn_id_info.vf_no);
#endif

#ifndef TC956X_SRIOV_VF /* No speed related and core init in VF */
	/* PS and related bits will be programmed according to the speed */
#ifdef TC956X
	if (priv->hw->pcs || priv->hw->xpcs) {
#else
	if (priv->hw->pcs) {
#endif
		int speed = priv->plat->mac_port_sel_speed;

		if ((speed == SPEED_10) || (speed == SPEED_100) ||
		    (speed == SPEED_1000) || (speed == SPEED_2500) ||
		    (speed == SPEED_10000)) {
			priv->hw->ps = speed;
		} else {
			dev_warn(priv->device, "invalid port speed\n");
			priv->hw->ps = 0;
		}
	}

	/* Initialize the MAC Core */
	tc956xmac_core_init(priv, priv->hw, dev);
#endif
#ifndef TC956X_SRIOV_VF
	/* Enable Jumbo Frame Support */
	tc956xmac_jumbo_en(priv, dev, TC956X_ENABLE);

#ifdef TC956X_SRIOV_PF
	/* Update driver cap to let VF know about feature enable/disable */
	priv->pf_drv_cap.jumbo_en = true;
#elif (defined TC956X_SRIOV_VF)
	/* Check if Jumbo frames Initialized in PF */
	if (priv->pf_drv_cap.jumbo_en)
		NMSGPR_INFO(priv->device, "Jumbo Frames supported\n");
#endif
#endif
	/* Initialize MTL*/
	tc956xmac_mtl_configuration(priv);

	/* Safety feature not supported aswell not configured by VF*/
#ifndef TC956X_SRIOV_VF
	/* Initialize Safety Features */
	tc956xmac_safety_feat_configuration(priv);
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.frp, flags);
#endif
	ret = tc956xmac_rx_parser_configuration(priv);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif
	/* Rx checksum offload configuration is done by PF
	 * VF driver should know the status of core register configuration
	 */
#ifndef TC956X_SRIOV_VF
	ret = tc956xmac_rx_ipc(priv, priv->hw);
	if (!ret) {
#else
	if (!priv->pf_drv_cap.csum_en) {
#endif
		netdev_warn(priv->dev, "RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = TC956XMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
#ifdef TC956X_SRIOV_PF
		/* Update driver cap to let VF know about feature enable/disable
		 */
		priv->pf_drv_cap.csum_en = false;
	} else {
		/* Update driver cap to let VF know about feature enable/disable */
		priv->pf_drv_cap.csum_en = true;
		priv->rx_csum_state = priv->hw->rx_csum;
#endif
	}

#ifndef TC956X_SRIOV_VF
	/* Enable the MAC Rx/Tx */
	tc956xmac_mac_set(priv, priv->ioaddr, true);
#endif
	/* Set the HW DMA mode and the COE */
	tc956xmac_dma_operation_mode(priv);

	/* Avoid SW MMC counters reset during link down MAC reset */
	if (priv->link_down_rst == false)
		tc956xmac_mmc_setup(priv);

	/* CRC & Padding Configuration */

	priv->tx_crc_pad_state = TC956X_TX_CRC_PAD_INSERT;
	priv->rx_crc_pad_state = TC956X_RX_CRC_DEFAULT;
	/* In SRIOV case the RX CRC configuration is handled by PF as it
	 * impacts all drivers
	 */

#ifndef TC956X_SRIOV_VF
	tc956x_rx_crc_pad_config(priv, priv->rx_crc_pad_state);
#ifdef TC956X_SRIOV_PF
	/* Update driver cap to let VF know about feature enable/disable */
	priv->pf_drv_cap.crc_en = priv->rx_crc_pad_state;
#endif

	if (init_ptp) {
		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0)
			netdev_warn(priv->dev, "failed to enable PTP reference clock: %d\n", ret);

		ret = tc956xmac_init_ptp(priv);
		if (ret == -EOPNOTSUPP)
			netdev_warn(priv->dev, "PTP not supported by HW\n");
		else if (ret)
			netdev_warn(priv->dev, "PTP init failed\n");
	}
#endif
	if (priv->use_riwt) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
		for (queue = 0; queue < rx_cnt; queue++) {
			if (!priv->rx_riwt[queue])
				priv->rx_riwt[queue] = DEF_DMA_RIWT;

			ret = tc956xmac_rx_watchdog(priv, priv->ioaddr, priv->rx_riwt[queue], queue);
		}
#else
		if (!priv->rx_riwt)
			priv->rx_riwt = DEF_DMA_RIWT;

		ret = tc956xmac_rx_watchdog(priv, priv->ioaddr, priv->rx_riwt, rx_cnt);

#endif
	}

	/* Auto negotiation not applicable for VF
	 * PTP configuration not applicable for VF
	 */
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
	if (priv->hw->xpcs) {
		/*C37 AN enable*/
		if ((priv->plat->interface == PHY_INTERFACE_MODE_10GKR) ||
			(priv->plat->interface == ENABLE_2500BASE_X_INTERFACE))
			enable_en = false;
		else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			if (priv->is_sgmii_2p5g == true)
				enable_en = false;
			else
				enable_en = true;
		} else
			enable_en = true;
		tc956x_xpcs_ctrl_ane(priv, enable_en);
	}
#else
	if (priv->hw->pcs)
		tc956xmac_pcs_ctrl_ane(priv, priv->ioaddr, 1, priv->hw->ps, 0);
#endif
	tc956x_ptp_configuration(priv, 0);
#else
	/* PTP init should have been done in PF */
	priv->hwts_rx_en = 1;
#endif /*#ifdef TC956X_SRIOV_VF*/

	/* set TX and RX rings length */
	tc956xmac_set_rings_length(priv);

	/* Enable TSO */
	if (priv->tso) {
		for (chan = 0; chan < tx_cnt; chan++) {
#ifdef TC956X_SRIOV_VF
			if (priv->plat->ch_in_use[chan] == 0)
				continue;
			if (priv->plat->tx_queues_cfg[chan].tso_en && priv->tso)
#else
			if (priv->plat->tx_queues_cfg[chan].tso_en)
#endif

				tc956xmac_enable_tso(priv, priv->ioaddr, 1, chan);
		}
	}

	/* Enable Split Header */
	if (priv->sph && priv->hw->rx_csum) {
		for (chan = 0; chan < rx_cnt; chan++) {
#ifdef TC956X_SRIOV_PF
			if (priv->plat->tx_queues_cfg[chan].tso_en)
#elif defined TC956X_SRIOV_VF
			if (priv->plat->ch_in_use[chan] == 0)
				continue;
#endif
				tc956xmac_enable_sph(priv, priv->ioaddr, 1, chan);
		}
	}

#ifndef TC956X_SRIOV_VF
	/* VLAN Tag Insertion */
#ifndef TC956X
	if (priv->dma_cap.vlins)
#else
	if ((priv->dma_cap.vlins) && (dev->features & NETIF_F_HW_VLAN_CTAG_TX))
#endif
		tc956xmac_enable_vlan(priv, priv->hw, TC956XMAC_VLAN_INSERT);
#endif

	/* TBS */
	for (chan = 0; chan < tx_cnt; chan++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
		int enable = tx_q->tbs & TC956XMAC_TBS_AVAIL;

#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;

		if (priv->plat->tx_queues_cfg[chan].tbs_en == TC956X_DISABLE)
			continue;
#elif defined TC956X_SRIOV_VF
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#endif
		tc956xmac_enable_tbs(priv, priv->ioaddr, enable, chan);
	}

	/* PF driver to configure EST for all functions */
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.est, flags);
#endif
#ifndef TC956X_SRIOV_VF
	if (priv->plat->est->enable)
		tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
				   priv->plat->clk_ptp_rate);
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.est, flags);
#endif

	/* Start the ball rolling... */
	tc956xmac_start_all_dma(priv);

	return 0;
}

#ifndef TC956X_SRIOV_VF
static void tc956xmac_hw_teardown(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	clk_disable_unprepare(priv->plat->clk_ptp_ref);
}


static void tc956xmac_set_cbs_default(struct tc956xmac_priv *priv)
{
	u32 queue_idx;
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;

	for (queue_idx = 0 ; queue_idx < tx_queue_cnt; queue_idx++) {
		if (priv->plat->tx_queues_cfg[queue_idx].mode_to_use == MTL_QUEUE_AVB) {
			/* CBS: queue 5/6/7 -> Class A/B/CDT traffic (25% BW) */
			priv->cbs_speed100_cfg[queue_idx].idle_slope = 0x400;
			priv->cbs_speed100_cfg[queue_idx].send_slope = 0xc00;
			priv->cbs_speed100_cfg[queue_idx].high_credit = 0x320000;
			priv->cbs_speed100_cfg[queue_idx].low_credit = 0xFF6A0000;

			priv->cbs_speed1000_cfg[queue_idx].idle_slope = 0x800;
			priv->cbs_speed1000_cfg[queue_idx].send_slope = 0x1800;
			priv->cbs_speed1000_cfg[queue_idx].high_credit = 0x320000;
			priv->cbs_speed1000_cfg[queue_idx].low_credit = 0xFF6A0000;

			priv->cbs_speed2500_cfg[queue_idx].idle_slope = 0x800;
			priv->cbs_speed2500_cfg[queue_idx].send_slope = 0x1800;
			priv->cbs_speed2500_cfg[queue_idx].high_credit = 0x320000;
			priv->cbs_speed2500_cfg[queue_idx].low_credit = 0xFF6A0000;

			priv->cbs_speed5000_cfg[queue_idx].idle_slope = 0x2000;
			priv->cbs_speed5000_cfg[queue_idx].send_slope = 0x6000;
			priv->cbs_speed5000_cfg[queue_idx].high_credit = 0x320000;
			priv->cbs_speed5000_cfg[queue_idx].low_credit = 0xFF6A0000;

			priv->cbs_speed10000_cfg[queue_idx].idle_slope = 0x2000;
			priv->cbs_speed10000_cfg[queue_idx].send_slope = 0x6000;
			priv->cbs_speed10000_cfg[queue_idx].high_credit = 0x320000;
			priv->cbs_speed10000_cfg[queue_idx].low_credit = 0xFF6A0000;
		}
	}

}

#ifdef TC956X_SRIOV_PF
/**
 *  tc956x_pf_vf_ch_alloc - Configure Resource Manager Module to allocate
 *  EMAC DMA channel for PF and VF.
 *  @ndev : pointer to the device structure.
 *  Description:
 *  Configure Resource Manager Module to allocate EMAC DMA channel for
 *  PF and VF.
 *  Return value:
 *  0 on success and (-)ve integer on failure.
 */
static int tc956x_pf_vf_ch_alloc(struct net_device *ndev)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	tc956xmac_rsc_mng_set_rscs(priv, ndev, &priv->rsc_dma_ch_alloc[0]);

	return 0;
}

#endif
#endif

/**
 *  tc956xmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_open(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = container_of(priv->device, struct pci_dev, dev);

	int bfsize = 0;
	u32 chan, rd_val;
	int ret, irq_no;
#ifdef TC956X_SRIOV_VF
	u32 link_status = 0;
	u32 speed;
	u32 duplex;
	int state_count = 0;
#endif

#ifndef TC956X_SRIOV_VF
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;

	KPRINT_INFO("---> light weight = %d %s : Port %d", priv->link_down_rst, __func__, priv->port_num);
	phydev = mdiobus_get_phy(priv->mii, addr);
	KPRINT_INFO("Open priv->link_down_rst = %d priv->tc956x_port_pm_suspend = %d\n", priv->link_down_rst, priv->tc956x_port_pm_suspend);
#ifdef CONFIG_DEBUG_FS
	if (priv->link_down_rst == false)
		tc956xmac_create_debugfs(priv->dev);/*Creating Debugfs*/
#endif


#ifndef TC956X_SRIOV_VF
	mutex_lock(&priv->port_ld_release_lock);
	priv->port_release = false; /* setting port release to false as Open invoked, and set to true from release or link down */
	if (priv->port_link_down == true) {
		tc956xmac_link_change_set_power(priv, LINK_UP); /* Restore, De-assert and Enable Reset and Clock */
	}
	mutex_unlock(&priv->port_ld_release_lock);

#endif

	if (!phydev) {
		netdev_err(priv->dev, "no phy at addr %d\n", addr);
		return -ENODEV;
	}

	if (priv->link_down_rst == false) {
		if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
		    priv->hw->pcs != TC956XMAC_PCS_TBI &&
		    priv->hw->pcs != TC956XMAC_PCS_RTBI) {
			ret = tc956xmac_init_phy(dev);
			if (ret) {
				netdev_err(priv->dev,
					   "%s: Cannot attach to PHY (error: %d)\n",
					   __func__, ret);
				KPRINT_INFO("<--- %s(1) : Port %d", __func__, priv->port_num);
				return ret;
			}
		}
	}
#ifdef TC956X_AUTOMOTIVE_CONFIG
	/* Do not re-allocate host resources during resume sequence. Only re-initialize resources */
	if (priv->tc956x_port_pm_suspend == false) {
		/* Extra statistics */
		memset(&priv->xstats, 0, sizeof(struct tc956xmac_extra_stats));
		priv->xstats.threshold = tc;

		bfsize = tc956xmac_set_16kib_bfsize(priv, dev->mtu);
		if (bfsize < 0)
			bfsize = 0;

		if (bfsize < BUF_SIZE_16KiB)
			bfsize = tc956xmac_set_bfsize(dev->mtu, priv->dma_buf_sz);

		priv->dma_buf_sz = bfsize;
		buf_sz = bfsize;

		priv->rx_copybreak = TC956XMAC_RX_COPYBREAK;

		/* Earlier check for TBS */
		for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
			struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
			int tbs_en = priv->plat->tx_queues_cfg[chan].tbs_en;

			/* Set TC956XMAC_TBS_EN by default. Later allow tc command to
			 *enable/disable
			 */
			tx_q->tbs |= tbs_en ? TC956XMAC_TBS_AVAIL | TC956XMAC_TBS_EN : 0;

			if (tc956xmac_enable_tbs(priv, priv->ioaddr, tbs_en, chan))
				tx_q->tbs &= ~TC956XMAC_TBS_AVAIL;
		}
	}
#endif
#ifdef TC956X_SRIOV_PF
	/* Retrieve Function ID */
	ret = tc956xmac_rsc_mng_get_fn_id(priv, priv->tc956x_BRIDGE_CFG_pci_base_addr, &priv->fn_id_info);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Invalid SRIOV Function ID\n",
			   __func__);
		return ret;
	}

	priv->port_num = priv->fn_id_info.pf_no;

	if (priv->fn_id_info.fn_type == RM_IS_PF) {
		/* Allocate DMA channels for PF & VF.
		 * To be called from both PFs
		 */
		KPRINT_INFO("%s: SRIOV Rsc Mgr Channel Allocation\n", __func__);
		ret = tc956x_pf_vf_ch_alloc(dev);
		if (ret < 0) {
			netdev_err(priv->dev, "%s: SRIOV CH Alloc failed\n",
					__func__);
			return ret;
		}
	}
#endif
#ifdef TC956X_SRIOV_PF
	priv->dma_vf_map[0] = 0;	//vf1
	priv->dma_vf_map[1] = 1;	//vf2
	priv->dma_vf_map[2] = 2;	//vf3
	priv->dma_vf_map[3] = 3;	//pf
	priv->dma_vf_map[4] = 3;
	priv->dma_vf_map[5] = 0;
	priv->dma_vf_map[6] = 2;
	priv->dma_vf_map[7] = 2;

	priv->pf_queue_dma_map[0] = 3;
	priv->pf_queue_dma_map[1] = 3;
	priv->pf_queue_dma_map[2] = 4;
	priv->pf_queue_dma_map[3] = 3;
	priv->pf_queue_dma_map[4] = 8;	//NA to	PF
	priv->pf_queue_dma_map[5] = 8;	//NA to	PF
	priv->pf_queue_dma_map[6] = 8;	//NA to	PF
	priv->pf_queue_dma_map[7] = 3;
#endif
#ifdef TC956X_SRIOV_PF
	tc956xmac_mbx_init(priv, NULL);
#endif

	/* Extra statistics */
	memset(&priv->xstats, 0, sizeof(struct tc956xmac_extra_stats));
	priv->xstats.threshold = tc;
#elif defined TC956X_SRIOV_VF
	memset(&priv->sw_stats, 0, sizeof(struct tc956x_sw_counters));
#endif

#ifdef TC956X_SRIOV_VF
	tc956xmac_get_link_status(priv, &link_status, &speed, &duplex);

	while (link_status != true && state_count < 10) {
		tc956xmac_get_link_status(priv, &link_status, &speed, &duplex);
		state_count++;
		udelay(100);
	}
#endif
	bfsize = tc956xmac_set_16kib_bfsize(priv, dev->mtu);
	if (bfsize < 0)
		bfsize = 0;

	if (bfsize < BUF_SIZE_16KiB)
		bfsize = tc956xmac_set_bfsize(dev->mtu, priv->dma_buf_sz);

	priv->dma_buf_sz = bfsize;
	buf_sz = bfsize;

#ifdef TC956X_SRIOV_PF
	tc956xmac_set_cbs_default(priv);
#endif

	priv->rx_copybreak = TC956XMAC_RX_COPYBREAK;

	/* Earlier check for TBS */
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];
		int tbs_en = priv->plat->tx_queues_cfg[chan].tbs_en;
#ifdef TC956X_SRIOV_PF
			if (priv->plat->tx_ch_in_use[chan] ==
						TC956X_DISABLE_CHNL)
				continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#endif
		/* Set TC956XMAC_TBS_EN by default. Later allow tc command to
		 *enable/disable
		 */
		tx_q->tbs |= tbs_en ? TC956XMAC_TBS_AVAIL | TC956XMAC_TBS_EN : 0;

		if (tc956xmac_enable_tbs(priv, priv->ioaddr, tbs_en, chan))
			tx_q->tbs &= ~TC956XMAC_TBS_AVAIL;
	}

	ret = alloc_dma_desc_resources(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors allocation failed\n",
			   __func__);
		goto dma_desc_error;
	}

	ret = init_dma_desc_rings(dev, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors initialization failed\n",
			   __func__);
		goto init_error;
	}

	ret = tc956xmac_hw_setup(dev, true);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		/*goto init_error;*/
	}


#ifdef TC956X

#ifdef TC956X_SRIOV_PF

	/* Masked all interrupts.
	 * Interrupts to CM3 to be enabled in FW
	 */
	if ((priv->fn_id_info.fn_type == RM_IS_PF) && (priv->fn_id_info.pf_no == RM_PF0_ID)) {
		rd_val = readl(priv->ioaddr + INTMCUMASK0);
		rd_val |= TC956X_INT_MASK0;
		writel(rd_val, priv->ioaddr + INTMCUMASK0);
	}
	if ((priv->fn_id_info.fn_type == RM_IS_PF) && (priv->fn_id_info.pf_no == RM_PF1_ID)) {
		rd_val = readl(priv->ioaddr + INTMCUMASK1);
		rd_val |= TC956X_INT_MASK1;
		writel(rd_val, priv->ioaddr + INTMCUMASK1);
	}

#ifdef EEE_MAC_CONTROLLED_MODE
	if (priv->port_num == RM_PF0_ID) {
		rd_val |= (NCLKCTRL0_MAC0312CLKEN | NCLKCTRL0_MAC0125CLKEN);
	}
	rd_val |= (NCLKCTRL0_POEPLLCEN | NCLKCTRL0_SGMPCIEN | NCLKCTRL0_REFCLKOCEN);
#endif
	/* Initialize MSIGEN */
	tc956x_msi_init(priv, dev);

#elif defined TC956X_SRIOV_VF
	/* Initialize MSIGEN */
	tc956x_msi_init(priv, dev, &priv->fn_id_info);

#endif
#endif /* TC956X */

	tc956xmac_init_coalesce(priv);


#ifndef TC956X_SRIOV_VF
	if (priv->link_down_rst == false)  {
		if (priv->phylink)
			phylink_start(priv->phylink);
	}

	KPRINT_INFO("%s phylink started", __func__);
#endif

#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)

	irq_no = pci_irq_vector(pdev, TC956X_MSI_VECTOR_0);
	if (priv->link_down_rst == false) {

		/* Request the IRQ lines */
		ret = request_irq(irq_no, tc956xmac_interrupt_v0,
				  IRQF_NO_SUSPEND, dev->name, dev);
		if (unlikely(ret < 0)) {
			netdev_err(priv->dev,
				   "%s: ERROR: allocating the IRQ 0 : %d (error: %d)\n",
				   __func__, dev->irq, ret);
			goto irq_error;
		}
#ifdef TC956X_SRIOV_PF
		/* Do not re-request WOL irq resources during resume sequence. */
		if (priv->tc956x_port_pm_suspend == false) {
			/* Request the Wake IRQ in case of another line is used for WoL */
			if (priv->wol_irq != dev->irq) {
				ret = request_irq(priv->wol_irq, tc956xmac_wol_interrupt,
						  IRQF_NO_SUSPEND, WOL_IRQ_DEV_NAME(priv->port_num), dev);
				if (unlikely(ret < 0)) {
					netdev_err(priv->dev,
						   "%s: ERROR: allocating the WoL IRQ %d (%d)\n",
						   __func__, priv->wol_irq, ret);
					goto wolirq_error;
				}
			}
	#ifndef TC956X
			/* Request the IRQ lines */
			if (priv->lpi_irq > 0) {
				ret = request_irq(priv->lpi_irq, tc956xmac_interrupt, IRQF_SHARED,
						  dev->name, dev);
				if (unlikely(ret < 0)) {
					netdev_err(priv->dev,
						   "%s: ERROR: allocating the LPI IRQ %d (%d)\n",
						   __func__, priv->lpi_irq, ret);
					goto lpiirq_error;
				}
			}
	#endif
			priv->tc956xmac_pm_wol_interrupt = false; /* Initialize flag for PHY Work queue */
		}
	}
#endif /* TC956X_SRIOV_PF */

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	irq_no = pci_irq_vector(pdev, TC956X_MSI_VECTOR_1);

	ret = request_irq(irq_no, tc956xmac_interrupt_v1,
			  IRQF_SHARED, dev->name, dev);
	if (unlikely(ret < 0)) {
		netdev_err(priv->dev,
			   "%s: ERROR: allocating the IRQ 1 : %d (error: %d)\n",
			   __func__, dev->irq, ret);
		goto irq_error;
	}
#else
	netdev_info(priv->dev, "%s: Only one interrupt handler registered\n",
				   __func__);
#endif
#endif

#ifdef TC956X_SRIOV_PF
	/* Enable MSIGEN interrupt */
	tc956x_msi_intr_en(priv, dev, TC956X_ENABLE);

#elif defined TC956X_SRIOV_VF
	/* Enable MSIGEN interrupt */
	tc956x_msi_intr_en(priv, dev, TC956X_ENABLE, &priv->fn_id_info);
#endif
	tc956xmac_enable_all_queues(priv);

	tc956xmac_start_all_queues(priv);

#ifdef TC956X_SRIOV_PF
	if (readl_poll_timeout_atomic(priv->ioaddr +  TC956X_MSI_EVENT_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no),
					rd_val, !(rd_val & 0x1), 100, 10000)) {

		netdev_warn(priv->dev, "MSI Vector not clear. MSI_MASK_CLR = 0x0%x\n",
				readl(priv->ioaddr +  TC956X_MSI_MASK_CLR_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no)));

	}

	/* MSI_MASK_CLR: unmask vector 0 & 1*/
	tc956x_msi_intr_clr(priv, dev, TC956X_MSI_VECTOR_0);
#if !defined(TC956X_AUTOMOTIVE_CONFIG)
	tc956x_msi_intr_clr(priv, dev, TC956X_MSI_VECTOR_1);
#endif

#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif

#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE

	/* Route all multicast Flow control packets to PCI path */
	tc956x_pf_set_mac_filter(dev, PF_DRIVER, (const u8*) &flow_ctrl_addr[0]);

	if (priv->port_num == RM_PF0_ID) {
		/* Write Eth0 RxBuffer Head address to DMEM */
		writel(priv->pbridge_handle, priv->tc956x_SRAM_pci_base_addr
				+ TC956X_M3_DMEM_OFFSET + (MAC2MAC_ETH0_RXDESC_L));
		writel(upper_32_bits(priv->pbridge_handle),
			priv->tc956x_SRAM_pci_base_addr	+ TC956X_M3_DMEM_OFFSET + (MAC2MAC_ETH0_RXDESC_H));
		writel(0x1 << 0, priv->ioaddr + INTC_MCUFLG);
	} else if (priv->port_num == RM_PF1_ID) {
		/* Write Eth1 RxBuffer Head address to DMEM */
		writel(priv->pbridge_handle, priv->tc956x_SRAM_pci_base_addr
			+ TC956X_M3_DMEM_OFFSET + (MAC2MAC_ETH1_RXDESC_L));
		writel(upper_32_bits(priv->pbridge_handle), priv->tc956x_SRAM_pci_base_addr
			+ TC956X_M3_DMEM_OFFSET + (MAC2MAC_ETH1_RXDESC_H));
		writel(0x1 << 1, priv->ioaddr + INTC_MCUFLG);
	}
	mutex_lock(&tc956x_port_bridge_lock);
	port_brige_state = 1;
	mutex_unlock(&tc956x_port_bridge_lock);
	netdev_info(priv->dev, "%s: Port bridge Feature enabled\n", __func__);
#endif

#elif defined TC956X_SRIOV_VF
	if (readl_poll_timeout_atomic(priv->ioaddr +  TC956X_MSI_EVENT_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no),
					rd_val, !(rd_val & 0x1), 100, 10000)) {

		netdev_warn(priv->dev, "MSI Vector not clear. MSI_MASK_CLR = 0x0%x\n",
				readl(priv->ioaddr +  TC956X_MSI_MASK_CLR_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no)));
	}

	/* MSI_MASK_CLR: unmask vector 0 */
	writel(0x00000001, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	if (link_status == true) {
		netif_carrier_on(dev);
		NMSGPR_INFO(priv->device, "PHY Link : UP\n");
	} else {
		netif_carrier_off(dev);
		NMSGPR_INFO(priv->device, "PHY Link : DOWN\n");
	}

	tc956xmac_vf_reset(priv, VF_UP);
#endif
#ifdef TX_COMPLETION_WITHOUT_TIMERS
		writel(0, priv->tc956x_SRAM_pci_base_addr
				+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif
	KPRINT_INFO("<--- light weight = %d %s(2) : Port %d", priv->link_down_rst,__func__, priv->port_num);
	return 0;
#ifdef TC956X_SRIOV_PF
#ifndef TC956X
lpiirq_error:
	if (priv->wol_irq != dev->irq)
		free_irq(priv->wol_irq, dev);
#endif
wolirq_error:
	free_irq(dev->irq, dev);
#endif
irq_error:
#ifndef TC956X_SRIOV_VF
	phylink_stop(priv->phylink);
#endif
#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
			hrtimer_cancel(&priv->tx_queue[chan].txtimer);
#else
			del_timer_sync(&priv->tx_queue[chan].txtimer);
#endif
	}
#endif
#ifndef TC956X_SRIOV_VF
	tc956xmac_hw_teardown(dev);
#endif

init_error:
	free_dma_desc_resources(priv);
dma_desc_error:
#ifndef TC956X_SRIOV_VF
	phylink_disconnect_phy(priv->phylink);
	KPRINT_INFO("<--- light weight = %d %s(3) : Port %d", priv->link_down_rst,__func__, priv->port_num);
#endif
	return ret;
}

/**
 *  tc956xmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int tc956xmac_release(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = container_of(priv->device, struct pci_dev, dev);
#ifdef TC956X_SRIOV_PF
#if defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;
#endif
	struct tc956x_mac_addr *mac_table = &priv->mac_table[0];
	struct tc956x_vlan_id *vlan_table = &priv->vlan_table[0];
	int i, vf_number;
#endif
	u32 ch;
	u32 offload_release_sts = true;

	int irq_no;
#ifdef ENABLE_TX_TIMER
	u32 chan;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	KPRINT_INFO(" ---> light weight = %d %s : Port %d", priv->link_down_rst, __func__, priv->port_num);
#ifdef TX_COMPLETION_WITHOUT_TIMERS
	writel(0, priv->tc956x_SRAM_pci_base_addr
			+ TX_TIMER_SRAM_OFFSET(priv->port_num));

#endif
	KPRINT_INFO("Release priv->link_down_rst = %d priv->tc956x_port_pm_suspend = %d\n", priv->link_down_rst, priv->tc956x_port_pm_suspend);
#ifdef TC956X_SRIOV_VF
	tc956xmac_vf_reset(priv, VF_RELEASE);
#endif

#ifdef TC956X_SRIOV_PF
	/* Disable all interrupt sources */
	tc956x_msi_intr_en(priv, dev, TC956X_DISABLE);

#if defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	mutex_lock(&tc956x_port_bridge_lock);
	if (port_brige_state) {
		port_brige_state = 0;
		writel(0x1 << 2, priv->ioaddr + INTC_MCUFLG);
		KPRINT_INFO("Sending MCU Flag Port bridge exit signal\n");
	}
	mutex_unlock(&tc956x_port_bridge_lock);
#endif

#elif defined TC956X_SRIOV_VF
	/* Disable all interrupt sources */
	tc956x_msi_intr_en(priv, dev, 0, &priv->fn_id_info);
#endif

#ifndef TC956X_SRIOV_VF
	/*if (priv->eee_enabled)
		del_timer_sync(&priv->eee_ctrl_timer);*/

#ifdef CONFIG_DEBUG_FS
	if (priv->link_down_rst == false)
	tc956xmac_cleanup_debugfs(priv->dev);
#endif


	/* Stop and disconnect the PHY */
	if (priv->link_down_rst == false) {
		if (priv->phylink) {
			phylink_stop(priv->phylink);
			phylink_disconnect_phy(priv->phylink);
		}
	}

	mutex_lock(&priv->port_ld_release_lock);
	if (priv->port_link_down == true) {
		KPRINT_INFO("Link down happened before %s, restoring clocks to stop DMA\n",__func__);
		tc956xmac_link_change_set_power(priv, LINK_UP); /* Restore, De-assert and Enable Reset and Clock */
	}

#endif
	tc956xmac_stop_all_queues(priv);

	tc956xmac_disable_all_queues(priv);

	/* MSI_OUT_EN: Disable all MSI*/
	if (priv->link_down_rst == false)
		writel(0x00000000, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->port_num, 0));

#ifdef ENABLE_TX_TIMER
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] == TC956X_DISABLE_CHNL)
			continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[chan] == USE_IN_TC956X_SW)
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
			hrtimer_cancel(&priv->tx_queue[chan].txtimer);
#else
			del_timer_sync(&priv->tx_queue[chan].txtimer);
#endif
	}
#endif
#ifdef TC956X_SRIOV_PF
	/* Free the IRQ lines */
	if (priv->link_down_rst == false) {
		irq_no = pci_irq_vector(pdev, TC956X_MSI_VECTOR_0);
		free_irq(irq_no, dev);

		/* Do not Free Host Irq resources during suspend sequence */
		if (priv->tc956x_port_pm_suspend == false) {
			if (priv->wol_irq != dev->irq)
				free_irq(priv->wol_irq, dev);
	#ifndef TC956X
			if (priv->lpi_irq > 0)
				free_irq(priv->lpi_irq, dev);
	#endif
		}
	}

#if defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	phydev = mdiobus_get_phy(priv->mii, addr);

	if(phydev->drv != NULL) {
		if ((true == priv->plat->phy_interrupt_mode) && (phydev->drv->config_intr)) {
			DBGPR_FUNC((priv->device), "-->%s Flush work queue\n", __func__);
			flush_work(&priv->emac_phy_work);
		}
	}
#endif

#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	irq_no = pci_irq_vector(pdev, TC956X_MSI_VECTOR_1);
	free_irq(irq_no, dev);
#endif
#elif defined TC956X_SRIOV_VF
	irq_no = pci_irq_vector(pdev, TC956X_MSI_VECTOR_0);
	free_irq(irq_no, dev);
#endif

#ifndef TC956X
	if (priv->lpi_irq > 0)
		free_irq(priv->lpi_irq, dev);
#endif
	/* Stop TX/RX DMA and clear the descriptors */
	tc956xmac_stop_all_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv);

#ifndef TC956X_SRIOV_VF
	/* Disable the MAC Rx/Tx */
	tc956xmac_mac_set(priv, priv->ioaddr, false);
#endif
	if (priv->link_down_rst == false)
		netif_carrier_off(dev);
	NMSGPR_INFO(priv->device, "PHY Link : DOWN\n");

	tc956xmac_release_ptp(priv);

	/* mutex_lock(&priv->port_ld_release_lock);*/
	/* Checking whether all offload Tx channels released or not*/
	for (ch = 0; ch < MAX_TX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->tx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}
	/* Checking whether all offload Rx channels released or not*/
	for (ch = 0; ch < MAX_RX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->rx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}
	/* If all channels are freed, call API for power saving*/
	if (priv->port_link_down == false && offload_release_sts == true) {
		KPRINT_INFO(" %s, Assert Reset and Disabling clock \n",__func__);
		tc956xmac_link_change_set_power(priv, LINK_DOWN); /* Save, Assert and Disable Reset and Clock */
	}
	priv->port_release = true; /* setting port release to true as release invoked, and clear from open or link-up */
	mutex_unlock(&priv->port_ld_release_lock);

#ifdef TC956X_SRIOV_PF
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
	for (i = XGMAC_ADDR_ADD_SKIP_OFST; i < (TC956X_MAX_PERFECT_ADDRESSES);
			i++, mac_table++) {
		for (vf_number = 0; vf_number < 4; vf_number++) {
			if (mac_table->vf[vf_number] != 0)
				tc956x_pf_del_mac_filter(priv->dev, mac_table->vf[vf_number], (u8 *)&mac_table->mac_address);
		}
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif
	for (i = 0; i < TC956X_MAX_PERFECT_VLAN; i++, vlan_table++) {
		for (vf_number = 0; vf_number < 4; vf_number++) {
			if (vlan_table->vf[vf_number].vf_number != 0)
				tc956x_pf_del_vlan_filter(priv->dev, vlan_table->vf[vf_number].vf_number, vlan_table->vid);
		}
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
#endif
	KPRINT_INFO("<--- light weight = %d %s : Port %d", priv->link_down_rst, __func__, priv->port_num);
	return 0;
}

static bool tc956xmac_vlan_insert(struct tc956xmac_priv *priv, struct sk_buff *skb,
			       struct tc956xmac_tx_queue *tx_q)
{
	u16 tag = 0x0, inner_tag = 0x0;
	u32 inner_type = 0x0;
	struct dma_desc *p;

	if (!priv->dma_cap.vlins)
		return false;
	if (!skb_vlan_tag_present(skb))
		return false;
	if (skb->vlan_proto == htons(ETH_P_8021AD)) {
		inner_tag = skb_vlan_tag_get(skb);
		inner_type = TC956XMAC_VLAN_INSERT;
	}

	tag = skb_vlan_tag_get(skb);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		p = &tx_q->dma_entx[tx_q->cur_tx].basic;
	else
		p = &tx_q->dma_tx[tx_q->cur_tx];

	if (tc956xmac_set_desc_vlan_tag(priv, p, tag, inner_tag, inner_type))
		return false;

	tc956xmac_set_tx_owner(priv, p);
	tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
	return true;
}

/**
 *  tc956xmac_tso_allocator - close entry point of the driver
 *  @priv: driver private structure
 *  @des: buffer start address
 *  @total_len: total length to fill in descriptors
 *  @last_segmant: condition for the last descriptor
 *  @queue: TX queue index
 *  Description:
 *  This function fills descriptor and request new descriptors according to
 *  buffer length to fill
 */
static void tc956xmac_tso_allocator(struct tc956xmac_priv *priv, dma_addr_t des,
				 int total_len, bool last_segment, u32 queue)
{
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
	struct dma_desc *desc;
	u32 buff_size;
	int tmp_len;

	tmp_len = total_len;

	while (tmp_len > 0) {
		dma_addr_t curr_addr;

		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);

		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		curr_addr = des + (total_len - tmp_len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		if (priv->dma_cap.addr64 <= 32)
			desc->des0 = cpu_to_le32(curr_addr);
		else
#endif
			tc956xmac_set_desc_addr(priv, desc, curr_addr);

		buff_size = tmp_len >= TSO_MAX_BUFF_SIZE ?
			    TSO_MAX_BUFF_SIZE : tmp_len;

		tc956xmac_prepare_tso_tx_desc(priv, desc, 0, buff_size,
				0, 1,
				(last_segment) && (tmp_len <= TSO_MAX_BUFF_SIZE),
				0, 0);

		tmp_len -= TSO_MAX_BUFF_SIZE;
	}
}

/**
 *  tc956xmac_tso_xmit - Tx entry point of the driver for oversized frames (TSO)
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description: this is the transmit function that is called on TSO frames
 *  (support available on GMAC4 and newer chips).
 *  Diagram below show the ring programming in case of TSO frames:
 *
 *  First Descriptor
 *   --------
 *   | DES0 |---> buffer1 = L2/L3/L4 header
 *   | DES1 |---> TCP Payload (can continue on next descr...)
 *   | DES2 |---> buffer 1 and 2 len
 *   | DES3 |---> must set TSE, TCP hdr len-> [22:19]. TCP payload len [17:0]
 *   --------
 *	|
 *     ...
 *	|
 *   --------
 *   | DES0 | --| Split TCP Payload on Buffers 1 and 2
 *   | DES1 | --|
 *   | DES2 | --> buffer 1 and 2 len
 *   | DES3 |
 *   --------
 *
 * mss is fixed when enable tso, so w/o programming the TDES3 ctx field.
 */
static netdev_tx_t tc956xmac_tso_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dma_desc *desc, *first, *mss_desc = NULL;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int desc_size, tmp_pay_len = 0, first_tx;
	int nfrags = skb_shinfo(skb)->nr_frags;
	u32 queue = skb_get_queue_mapping(skb);
	unsigned int first_entry, tx_packets;
	struct tc956xmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	u8 proto_hdr_len, hdr;
	u32 pay_len, mss;
	dma_addr_t des;
	int i;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	/* Compute header lengths */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		proto_hdr_len = skb_transport_offset(skb) + sizeof(struct udphdr);
		hdr = sizeof(struct udphdr);
	} else {
		proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		hdr = tcp_hdrlen(skb);
	}

	/* Desc availability based on threshold should be enough safe */
	if (unlikely(tc956xmac_tx_avail(priv, queue) <
		(((skb->len - proto_hdr_len) / TSO_MAX_BUFF_SIZE + 1)))) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	pay_len = skb_headlen(skb) - proto_hdr_len; /* no frags */

	mss = skb_shinfo(skb)->gso_size;

	/* set new MSS value if needed */
	if (mss != tx_q->mss) {
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			mss_desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			mss_desc = &tx_q->dma_tx[tx_q->cur_tx];

		tc956xmac_set_mss(priv, mss_desc, mss);
		tx_q->mss = mss;
		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	if (netif_msg_tx_queued(priv)) {
		pr_info("%s: hdrlen %d, hdr_len %d, pay_len %d, mss %d\n",
			__func__, hdr, proto_hdr_len, pay_len, mss);
		pr_info("\tskb->len %d, skb->data_len %d\n", skb->len,
			skb->data_len);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = tc956xmac_vlan_insert(priv, skb, tx_q);

	first_entry = tx_q->cur_tx;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[first_entry].basic;
	else
		desc = &tx_q->dma_tx[first_entry];
	first = desc;

	if (has_vlan)
		tc956xmac_set_desc_vlan(priv, first, TC956XMAC_VLAN_INSERT);

	/* first descriptor: fill Headers on Buf1 */
	des = dma_map_single(priv->device, skb->data, skb_headlen(skb),
			     DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, des))
		goto dma_map_err;

	tx_q->tx_skbuff_dma[first_entry].buf = des;
	tx_q->tx_skbuff_dma[first_entry].len = skb_headlen(skb);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (priv->dma_cap.addr64 <= 32) {
		first->des0 = cpu_to_le32(des);

		/* Fill start of payload in buff2 of first descriptor */
		if (pay_len)
			first->des1 = cpu_to_le32(des + proto_hdr_len);

		/* If needed take extra descriptors to fill the remaining
		 * payload
		 */
		tmp_pay_len = pay_len - TSO_MAX_BUFF_SIZE;
	} else {
#endif
		tc956xmac_set_desc_addr(priv, first, des);
		tmp_pay_len = pay_len;
		des += proto_hdr_len;
		pay_len = 0;
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	}
#endif

	tc956xmac_tso_allocator(priv, des, tmp_pay_len, (nfrags == 0), queue);

	/* Prepare fragments */
	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		des = skb_frag_dma_map(priv->device, frag, 0,
				       skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		tc956xmac_tso_allocator(priv, des, skb_frag_size(frag),
				     (i == nfrags - 1), queue);

		tx_q->tx_skbuff_dma[tx_q->cur_tx].buf = des;
		tx_q->tx_skbuff_dma[tx_q->cur_tx].len = skb_frag_size(frag);
		tx_q->tx_skbuff_dma[tx_q->cur_tx].map_as_page = true;
	}

	tx_q->tx_skbuff_dma[tx_q->cur_tx].last_segment = true;

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[tx_q->cur_tx] = skb;

	/* Manage tx mitigation */
	tx_packets = (tx_q->cur_tx + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames[queue])
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames[queue])
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames[queue]) < tx_packets)
		set_ic = true;
	else
		set_ic = false;
#else
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;
#endif
	if (set_ic) {
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		tx_q->tx_count_frames = 0;
		tc956xmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and tc956xmac_tx_clean may clean up to this descriptor.
	 */
	tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);

	if (unlikely(tc956xmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;
	priv->xstats.tx_tso_frames[queue]++;
	priv->xstats.tx_tso_nfrags[queue] += nfrags;

	if (priv->sarc_type)
		tc956xmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     priv->hwts_tx_en)) {
		/* declare that device is doing timestamping */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		tc956xmac_enable_tx_timestamp(priv, first);
	}

	/* Complete the first descriptor before granting the DMA */
	tc956xmac_prepare_tso_tx_desc(priv, first, 1,
			proto_hdr_len,
			pay_len,
			1, tx_q->tx_skbuff_dma[first_entry].last_segment,
			hdr / 4, (skb->len - proto_hdr_len));

	/* If context desc is used to change MSS */
	if (mss_desc) {
		/* Make sure that first descriptor has been completely
		 * written, including its own bit. This is because MSS is
		 * actually before first descriptor, so we need to make
		 * sure that MSS's own bit is the last thing written.
		 */
		dma_wmb();
		tc956xmac_set_tx_owner(priv, mss_desc);
	}

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	if (netif_msg_pktdata(priv)) {
		pr_info("%s: curr=%d dirty=%d f=%d, e=%d, f_p=%p, nfrags %d\n",
			__func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			tx_q->cur_tx, first, nfrags);
		pr_info(">>> frame to be transmitted: ");
		print_pkt(skb->data, skb_headlen(skb));
	}

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
#ifdef ENABLE_TX_TIMER
	tc956xmac_tx_timer_arm(priv, queue);
#endif

#ifdef TX_COMPLETION_WITHOUT_TIMERS
	writel(1, priv->tc956x_SRAM_pci_base_addr
			+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif
	return NETDEV_TX_OK;

dma_map_err:
	dev_err(priv->device, "Tx dma map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

/**
 *  tc956xmac_xmit - Tx entry point of the driver
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : this is the tx entry point of the driver.
 *  It programs the chain or the ring and supports oversized frames
 *  and SG feature.
 */
static netdev_tx_t tc956xmac_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int first_entry, tx_packets, enh_desc;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int i, csum_insertion = 0, is_jumbo = 0;
	u32 queue = skb_get_queue_mapping(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	int gso = skb_shinfo(skb)->gso_type;
	struct dma_edesc *tbs_desc = NULL;
	int entry, desc_size, first_tx;
	struct dma_desc *desc, *first;
	struct tc956xmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	dma_addr_t des;
	u64 ns = 0;
	u32 ts_low, ts_high;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	KPRINT_DEBUG1("tso en = %d\n", priv->tso);
	KPRINT_DEBUG1("skb tso en = %d\n", skb_is_gso(skb));
	/* Manage oversized TCP frames for GMAC4 device */

	/* TSO feature is supported based on configuration in PF */
	if (skb_is_gso(skb) && priv->tso) {
		KPRINT_DEBUG1("XMIT TSO IF\n");
		if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
			return tc956xmac_tso_xmit(skb, dev);
		if (priv->plat->has_gmac4 && (gso & SKB_GSO_UDP_L4))
			return tc956xmac_tso_xmit(skb, dev);
	}
	KPRINT_DEBUG1("XMIT Normal\n");

	if (unlikely(tc956xmac_tx_avail(priv, queue) < nfrags + 1)) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	/* Prepare context descriptor for one-step timestamp correction */
#ifdef TC956X_SRIOV_VF
	if ((tx_q->queue_index == priv->plat->gptp_ch_no) && (priv->ost_en == 1)
		&& (priv->plat->best_effort_ch_no != priv->plat->gptp_ch_no)) {
#else
	if ((tx_q->queue_index == TC956X_GPTP_TX_CH) && (priv->ost_en == 1)) {
#endif
		if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];
		tc956xmac_get_mac_tx_timestamp(priv, priv->hw, &ns);
		ts_low = (u32)ns;
		ts_high = (u32)(ns >> 32);
		tc956xmac_set_desc_ostc(priv, desc, ts_high, ts_low);
		tx_q->cur_tx = TC956XMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = tc956xmac_vlan_insert(priv, skb, tx_q);

	entry = tx_q->cur_tx;
	first_entry = entry;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	/* Update checksum value as per Ethtool configuration */
	/*csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);*/
#ifdef TC956X_SRIOV_PF
	if (queue == HOST_BEST_EFF_CH)
		csum_insertion = priv->csum_insertion;
#elif defined TC956X_SRIOV_VF
	if (queue == priv->plat->best_effort_ch_no)
	csum_insertion = priv->csum_insertion;
#endif
	else
		csum_insertion = 0;

	KPRINT_DEBUG1("csum_insertion = %d\n", csum_insertion);
	KPRINT_DEBUG1("priv->tx_crc_pad_state = %d\n", priv->tx_crc_pad_state);
	if (likely(priv->extend_desc))
		desc = (struct dma_desc *)(tx_q->dma_etx + entry);
	else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[entry].basic;
	else
		desc = tx_q->dma_tx + entry;

	first = desc;

	if (has_vlan)
		tc956xmac_set_desc_vlan(priv, first, TC956XMAC_VLAN_INSERT);

	enh_desc = priv->plat->enh_desc;
	/* To program the descriptors according to the size of the frame */
	if (enh_desc)
		is_jumbo = tc956xmac_is_jumbo_frm(priv, skb->len, enh_desc);

	if (unlikely(is_jumbo)) {
		entry = tc956xmac_jumbo_frm(priv, tx_q, skb, csum_insertion);
		if (unlikely(entry < 0) && (entry != -EINVAL))
			goto dma_map_err;
	}

	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);
		bool last_segment = (i == (nfrags - 1));

		entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[entry]);

		if (likely(priv->extend_desc))
			desc = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = tx_q->dma_tx + entry;

		des = skb_frag_dma_map(priv->device, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err; /* should reuse desc w/o issues */

		tx_q->tx_skbuff_dma[entry].buf = des;

		tc956xmac_set_desc_addr(priv, desc, des);

		tx_q->tx_skbuff_dma[entry].map_as_page = true;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].last_segment = last_segment;

		/* Prepare the descriptor and set the own bit too */
		tc956xmac_prepare_tx_desc(priv, desc, 0, len, csum_insertion,
				priv->tx_crc_pad_state,	priv->mode, 1,
				last_segment, skb->len);
	}

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[entry] = skb;

	/* According to the coalesce parameter the IC bit for the latest
	 * segment is reset and the timer re-started to clean the tx status.
	 * This approach takes care about the fragments: desc is the first
	 * element in case of no SG.
	 */
	tx_packets = (entry + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames[queue])
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames[queue])
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames[queue]) < tx_packets)
		set_ic = true;
	else
		set_ic = false;
#else
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;
#endif
	if (set_ic) {
		if (likely(priv->extend_desc))
			desc = &tx_q->dma_etx[entry].basic;
		else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = &tx_q->dma_tx[entry];

		tx_q->tx_count_frames = 0;
		tc956xmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and tc956xmac_tx_clean may clean up to this descriptor.
	 */
	entry = TC956XMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	tx_q->cur_tx = entry;

	if (netif_msg_pktdata(priv)) {
		netdev_dbg(priv->dev,
			   "%s: curr=%d dirty=%d f=%d, e=%d, first=%p, nfrags=%d",
			   __func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			   entry, first, nfrags);

		netdev_dbg(priv->dev, ">>> frame to be transmitted: ");
		print_pkt(skb->data, skb->len);
	}

	if (unlikely(tc956xmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;

	if (priv->sarc_type)
		tc956xmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	/* Ready to fill the first descriptor and set the OWN bit w/o any
	 * problems because all the descriptors are actually ready to be
	 * passed to the DMA engine.
	 */
	if (likely(!is_jumbo)) {
		bool last_segment = (nfrags == 0);

		des = dma_map_single(priv->device, skb->data,
				     nopaged_len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		tx_q->tx_skbuff_dma[first_entry].buf = des;

		tc956xmac_set_desc_addr(priv, first, des);

		tx_q->tx_skbuff_dma[first_entry].len = nopaged_len;
		tx_q->tx_skbuff_dma[first_entry].last_segment = last_segment;

		if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			     priv->hwts_tx_en)) {
			/* declare that device is doing timestamping */
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			tc956xmac_enable_tx_timestamp(priv, first);
		}

		/* Prepare the first descriptor setting the OWN bit too */
		tc956xmac_prepare_tx_desc(priv, first, 1, nopaged_len,
				csum_insertion, priv->tx_crc_pad_state,
				priv->mode, 0, last_segment, skb->len);
	}

	if (tx_q->tbs & TC956XMAC_TBS_EN) {
		/* Check if timestamp_valid bit is enabled */
		if (skb->data[19] & 0x01) {
			struct timespec64 ts = ns_to_timespec64(skb->tstamp);
			u32 Presentation_time, Traverse_time, app_launch_time;
			u64 ns, lt;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
			u64 quotient;
			u32 reminder;
#endif
			unsigned long flags;

			if (skb->tstamp) {
				ts = ns_to_timespec64(skb->tstamp);
			} else {
				Presentation_time = skb->data[30];
				Presentation_time = (Presentation_time<<8) |
							 skb->data[31];
				Presentation_time = (Presentation_time<<8) |
							skb->data[32];
				Presentation_time = (Presentation_time<<8) |
							skb->data[33];
#ifdef TC956X_SRIOV_VF
				if ((priv->plat->avb_class_b_ch_no == queue)
					&& (priv->plat->best_effort_ch_no != priv->plat->avb_class_b_ch_no))
#else
				if (queue == AVB_CLASS_B_TX_CH)
#endif
					Traverse_time = 50000000; /* Class B - 50ms */
#ifdef TC956X_SRIOV_VF
				else if ((priv->plat->avb_class_a_ch_no == queue)
					&& (priv->plat->best_effort_ch_no != priv->plat->avb_class_a_ch_no))
#else
				else if (queue == AVB_CLASS_A_TX_CH)
#endif
					Traverse_time = 2000000; /* Class A- 2ms */
				else
					Traverse_time = 0; /* default */

				if (Presentation_time >= Traverse_time)
					app_launch_time =
						Presentation_time - Traverse_time;
				else
					app_launch_time =
						0x100000000ULL - Traverse_time +
						 Presentation_time;

				spin_lock_irqsave(&priv->ptp_lock, flags);
				tc956xmac_get_systime(priv, priv->ptpaddr, &ns);
				spin_unlock_irqrestore(&priv->ptp_lock, flags);

				lt = ((ns >> 32) << 32) | app_launch_time;
				if (((signed long)app_launch_time - (signed long)(ns & 0xFFFFFFFF)) < 0) {
					/* If the difference is 2 seconds apart, it is judged as rollover */
					if (((ns & 0xFFFFFFFF) - app_launch_time) > ((1ULL << 32) / 2))
						lt += (u64)((u64)1 << 32);
				}
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
				quotient = div_u64_rem(lt, 1000000000ULL,
						&reminder);
				ts.tv_sec = quotient & 0xFF;
				ts.tv_nsec = reminder;
#else
				ts.tv_sec  = (lt / 1000000000) & 0xFF;
				ts.tv_nsec = (lt % 1000000000);
#endif
			}
			tbs_desc = &tx_q->dma_entx[first_entry];
			tc956xmac_set_desc_tbs(priv, tbs_desc, ts.tv_sec, ts.tv_nsec, true);
		} else {
			tbs_desc = &tx_q->dma_entx[first_entry];
			tc956xmac_set_desc_tbs(priv, tbs_desc, 0, 0, false);
		}
	}

	tc956xmac_set_tx_owner(priv, first);

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	tc956xmac_enable_dma_transmission(priv, priv->ioaddr);

	if (likely(priv->extend_desc))
		desc_size = sizeof(struct dma_extended_desc);
	else if (tx_q->tbs & TC956XMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);

#ifdef ENABLE_TX_TIMER
	tc956xmac_tx_timer_arm(priv, queue);
#endif

#ifdef TX_COMPLETION_WITHOUT_TIMERS
	writel(1, priv->tc956x_SRAM_pci_base_addr
			+ TX_TIMER_SRAM_OFFSET(priv->port_num));
#endif

	return NETDEV_TX_OK;

dma_map_err:
	netdev_err(priv->dev, "Tx DMA map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

#ifndef TC956X
/**
 *  tc956xmac_rx_vlan - Rx VLAN Stripping Function
 *  @dev : device pointer
 *  @skb : the socket buffer
 *  Description : this function strips vlan id from the skb
 *  before forwarding to application.
 */
static void tc956xmac_rx_vlan(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_ethhdr *veth;
	__be16 vlan_proto;
	u16 vlanid;

	veth = (struct vlan_ethhdr *)skb->data;
	vlan_proto = veth->h_vlan_proto;

	if ((vlan_proto == htons(ETH_P_8021Q) &&
	     dev->features & NETIF_F_HW_VLAN_CTAG_RX) ||
	    (vlan_proto == htons(ETH_P_8021AD) &&
	     dev->features & NETIF_F_HW_VLAN_STAG_RX)) {
		/* pop the vlan tag */
		vlanid = ntohs(veth->h_vlan_TCI);
		memmove(skb->data + VLAN_HLEN, veth, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
		__vlan_hwaccel_put_tag(skb, vlan_proto, vlanid);
	}
}
#else
/**
 *  tc956xmac_rx_vlan - Rx VLAN Stripping Function
 *  @dev : device pointer
 *  @rdesc : rx descriptor
 *  @skb : the socket buffer
 *  Description : this function extracts vlan id from the descriptor
 *  stripped by MAC VLAN filter.
 */
static void tc956xmac_rx_vlan(struct net_device *dev,
				struct dma_desc *rdesc,
				struct sk_buff *skb)
{
	u16 vlanid;
	u32 err, etlt;

	/* Check for error in descriptor */
	err = XGMAC_GET_BITS_LE(rdesc->des3, XGMAC_RDES3, ES);
	/* Check for L2 Packet Type Encoding */
	etlt = XGMAC_GET_BITS_LE(rdesc->des3, XGMAC_RDES3, ETLT);
	if (!err) {
		/* No error if err is 0 or etlt is 0 */
		/* Check packet type is Single CVLAN tag and
		netdev supports VLAN CTAG*/
		if ((etlt == PKT_TYPE_SINGLE_CVLAN) &&
		    (dev->features & NETIF_F_HW_VLAN_CTAG_RX)) {
			vlanid = XGMAC_GET_BITS_LE(rdesc->des0,
							XGMAC_RDES0,
							OVT);
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
						vlanid);
		}
	}
}
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static inline int tc956xmac_rx_threshold_count(struct tc956xmac_rx_queue *rx_q)
{
	if (rx_q->rx_zeroc_thresh < TC956XMAC_RX_THRESH)
		return 0;

	return 1;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/**
 * tc956xmac_rx_refill - refill used skb preallocated buffers
 * @priv: driver private structure
 * @queue: RX queue index
 * Description : this is to reallocate the skb for the reception process
 * that is based on zero-copy.
 */
static inline void tc956xmac_rx_refill(struct tc956xmac_priv *priv, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int len, dirty = tc956xmac_rx_dirty(priv, queue);
	unsigned int entry = rx_q->dirty_rx;

	len = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE) * PAGE_SIZE;

	while (dirty-- > 0) {
		struct tc956xmac_rx_buffer *buf = &rx_q->buf_pool[entry];
		struct dma_desc *p;
		bool use_rx_wd;

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		if (!buf->page) {
			buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->page)
				break;
		}

		if (priv->sph && !buf->sec_page) {
			buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->sec_page)
				break;

			buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);

			dma_sync_single_for_device(priv->device, buf->sec_addr,
						   len, DMA_FROM_DEVICE);
		}

		buf->addr = page_pool_get_dma_addr(buf->page);

		/* Sync whole allocation to device. This will invalidate old
		 * data.
		 */
		dma_sync_single_for_device(priv->device, buf->addr, len,
					   DMA_FROM_DEVICE);

		tc956xmac_set_desc_addr(priv, p, buf->addr);
		tc956xmac_set_desc_sec_addr(priv, p, buf->sec_addr);
		tc956xmac_refill_desc3(priv, rx_q, p);

		rx_q->rx_count_frames++;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
		rx_q->rx_count_frames += priv->rx_coal_frames[queue];
		if (rx_q->rx_count_frames > priv->rx_coal_frames[queue])
			rx_q->rx_count_frames = 0;

		use_rx_wd = !priv->rx_coal_frames[queue];
#else
		rx_q->rx_count_frames += priv->rx_coal_frames;
		if (rx_q->rx_count_frames > priv->rx_coal_frames)
			rx_q->rx_count_frames = 0;

		use_rx_wd = !priv->rx_coal_frames;
#endif
		use_rx_wd |= rx_q->rx_count_frames > 0;
		if (!priv->use_riwt)
			use_rx_wd = false;

		dma_wmb();
		tc956xmac_set_rx_owner(priv, p, use_rx_wd);

		entry = TC956XMAC_GET_ENTRY(entry, DMA_RX_SIZE);
	}
	rx_q->dirty_rx = entry;
	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (rx_q->dirty_rx * sizeof(struct dma_desc));
	tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, queue);
}

static unsigned int tc956xmac_rx_buf1_len(struct tc956xmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	int ret, coe = priv->hw->rx_csum;
	unsigned int plen = 0, hlen = 0;

	/* Not first descriptor, buffer is always zero */
	if (priv->sph && len)
		return 0;

	/* First descriptor, get split header length */
	ret = tc956xmac_get_rx_header_len(priv, p, &hlen);
	if (priv->sph && hlen) {
		priv->xstats.rx_split_hdr_pkt_n++;
		return hlen;
	}

	/* First descriptor, not last descriptor and not split header */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = tc956xmac_get_rx_frame_len(priv, p, coe);

	/* First descriptor and last descriptor and not split header */
	return min_t(unsigned int, priv->dma_buf_sz, plen);
}

static unsigned int tc956xmac_rx_buf2_len(struct tc956xmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	int coe = priv->hw->rx_csum;
	unsigned int plen = 0;

	/* Not split header, buffer is not available */
	if (!priv->sph)
		return 0;

	/* Not last descriptor */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = tc956xmac_get_rx_frame_len(priv, p, coe);

	/* Last descriptor */
	return plen - len;
}

/**
 * tc956xmac_rx - manage the receive process
 * @priv: driver private structure
 * @limit: napi bugget
 * @queue: RX queue index.
 * Description :  this the function called by the napi poll method.
 * It gets all the frames inside the ring.
 */
static int tc956xmac_rx(struct tc956xmac_priv *priv, int limit, u32 queue)
{
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct tc956xmac_channel *ch = &priv->channel[queue];
	unsigned int count = 0, error = 0, len = 0, per_queue_count = 0;
	int status = 0, coe = priv->hw->rx_csum;
	unsigned int next_entry = rx_q->cur_rx;
	struct sk_buff *skb = NULL;
	unsigned int proto;

	if (netif_msg_rx_status(priv)) {
		void *rx_head;

		netdev_dbg(priv->dev, "%s: descriptor ring:\n", __func__);
		if (priv->extend_desc)
			rx_head = (void *)rx_q->dma_erx;
		else
			rx_head = (void *)rx_q->dma_rx;

		tc956xmac_display_ring(priv, rx_head, DMA_RX_SIZE, true);
	}
	while (count < limit) {
		unsigned int buf1_len = 0, buf2_len = 0;
		enum pkt_hash_types hash_type;
		struct tc956xmac_rx_buffer *buf;
		struct dma_desc *np, *p;
		int entry;
		u32 hash;

		if (!count && rx_q->state_saved) {
			skb = rx_q->state.skb;
			error = rx_q->state.error;
			len = rx_q->state.len;
		} else {
			rx_q->state_saved = false;
			skb = NULL;
			error = 0;
			len = 0;
		}

		if (count >= limit)
			break;

read_again:
		buf1_len = 0;
		buf2_len = 0;
		entry = next_entry;
		buf = &rx_q->buf_pool[entry];

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		/* read the status of the incoming frame */
		status = tc956xmac_rx_status(priv, &priv->dev->stats,
				&priv->xstats, p);
		/* check if managed by the DMA otherwise go ahead */
		if (unlikely(status & dma_own))
			break;

		rx_q->cur_rx = TC956XMAC_GET_ENTRY(rx_q->cur_rx, DMA_RX_SIZE);
		next_entry = rx_q->cur_rx;

		if (priv->extend_desc)
			np = (struct dma_desc *)(rx_q->dma_erx + next_entry);
		else
			np = rx_q->dma_rx + next_entry;

		prefetch(np);

		if (priv->extend_desc)
			tc956xmac_rx_extended_status(priv, &priv->dev->stats,
					&priv->xstats, rx_q->dma_erx + entry);
		if (unlikely(status == discard_frame)) {
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
			error = 1;
			if (!priv->hwts_rx_en)
				priv->dev->stats.rx_errors++;
		}

		if (unlikely(error && (status & rx_not_ls)))
			goto read_again;
		if (unlikely(error)) {
			dev_kfree_skb(skb);
			skb = NULL;
			count++;
			continue;
		}

		/* Buffer is good. Go on. */

		prefetch(page_address(buf->page));
		if (buf->sec_page)
			prefetch(page_address(buf->sec_page));

		buf1_len = tc956xmac_rx_buf1_len(priv, p, status, len);
		len += buf1_len;
		buf2_len = tc956xmac_rx_buf2_len(priv, p, status, len);
		len += buf2_len;

		/* CRC stripping is done in MAC (CST &ACS bits) based on ethtool state*/
		if (!skb) {
			skb = napi_alloc_skb(&ch->rx_napi, buf1_len);
			if (!skb) {
				priv->dev->stats.rx_dropped++;
				count++;
				goto drain_data;
			}

			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_copy_to_linear_data(skb, page_address(buf->page),
						buf1_len);
			skb_put(skb, buf1_len);

			/* Data payload copied into SKB, page ready for recycle */
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
		} else if (buf1_len) {
			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->page, 0, buf1_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
			page_pool_release_page(rx_q->page_pool, buf->page);
#else
			skb_mark_for_recycle(skb);
#endif
			buf->page = NULL;
		}

		if (buf2_len) {
			dma_sync_single_for_cpu(priv->device, buf->sec_addr,
						buf2_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->sec_page, 0, buf2_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
			page_pool_release_page(rx_q->page_pool, buf->sec_page);
#else
			skb_mark_for_recycle(skb);
#endif

			buf->sec_page = NULL;
		}

drain_data:
		if (likely(status & rx_not_ls))
			goto read_again;
		if (!skb)
			continue;

		/* Got entire packet into SKB. Finish it. */
#ifdef RX_LOGGING_TRACE
		tc956xmac_get_rx_hwtstamp(priv, p, np, skb, queue);
#else
		/* Pause frame counter to count link partner pause frames */
		if ((mac0_en_lp_pause_frame_cnt == ENABLE && priv->port_num == RM_PF0_ID) ||
			(mac1_en_lp_pause_frame_cnt == ENABLE && priv->port_num == RM_PF1_ID)) {
			proto = htons(((skb->data[13]<<8) | skb->data[12]));
			if (proto == ETH_P_PAUSE) {
				if (!(skb->data[6] == phy_sa_addr[priv->port_num][0] && skb->data[7] == phy_sa_addr[priv->port_num][1]
					&& skb->data[8] == phy_sa_addr[priv->port_num][2] && skb->data[9] == phy_sa_addr[priv->port_num][3]
					&& skb->data[10] == phy_sa_addr[priv->port_num][4] && skb->data[11] == phy_sa_addr[priv->port_num][5])) {
					priv->xstats.link_partner_pause_frame_cnt++;
				}
			}
		}

		tc956xmac_get_rx_hwtstamp(priv, p, np, skb);
#endif
#ifndef TC956X
		tc956xmac_rx_vlan(priv->dev, skb);
#else
		tc956xmac_rx_vlan(priv->dev, p, skb);
#endif
		skb->protocol = eth_type_trans(skb, priv->dev);

		if (unlikely(!coe))
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (!tc956xmac_get_rx_hash(priv, p, &hash, &hash_type))
			skb_set_hash(skb, hash, hash_type);

		skb_record_rx_queue(skb, queue);
		napi_gro_receive(&ch->rx_napi, skb);
		skb = NULL;

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += len;
		count++;
		per_queue_count++;
	}

	if (status & rx_not_ls || skb) {
		rx_q->state_saved = true;
		rx_q->state.skb = skb;
		rx_q->state.error = error;
		rx_q->state.len = len;
	}

	tc956xmac_rx_refill(priv, queue);

	/* priv->xstats.rx_pkt_n[queue]+= count; */
	/* Count only Acceptd packet */
	priv->xstats.rx_pkt_n[queue] += per_queue_count;


	return count;
}

static int tc956xmac_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct tc956xmac_channel *ch =
		container_of(napi, struct tc956xmac_channel, rx_napi);
	struct tc956xmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll_rx[chan]++;

	work_done = tc956xmac_rx(priv, budget, chan);
	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_enable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

static int tc956xmac_napi_poll_tx(struct napi_struct *napi, int budget)
{
	struct tc956xmac_channel *ch =
		container_of(napi, struct tc956xmac_channel, tx_napi);
	struct tc956xmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll_tx[chan]++;

	work_done = tc956xmac_tx_clean(priv, DMA_TX_SIZE, chan);
	work_done = min(work_done, budget);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		tc956xmac_enable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

/**
 *  tc956xmac_tx_timeout
 *  @dev : Pointer to net device structure
 *  Description: this function is called when a packet transmission fails to
 *   complete within a reasonable time. The driver will mark the error in the
 *   netdev structure and arrange for the device to be reset to a sane state
 *   in order to transmit a new packet.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static void tc956xmac_tx_timeout(struct net_device *dev, unsigned int txqueue)
#else
static void tc956xmac_tx_timeout(struct net_device *dev)
#endif
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956xmac_global_err(priv);
}

#ifdef TC956X_SRIOV_VF
static int tc956x_vf_add_mac_addr(struct net_device *dev, const unsigned char *mac)
{
	int ret_value;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	ret_value = tc956xmac_add_mac(priv, mac);

	return ret_value;
}

static int tc956x_vf_delete_mac_addr(struct net_device *dev,
				const unsigned char *mac)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956xmac_delete_mac(priv, mac);
	return 0;
}
static void tc956x_vf_set_filter(struct mac_device_info *hw,
				struct net_device *dev)
{
	__dev_uc_sync(dev, tc956x_vf_add_mac_addr, tc956x_vf_delete_mac_addr);

	__dev_mc_sync(dev, tc956x_vf_add_mac_addr, tc956x_vf_delete_mac_addr);
}
#endif

/**
 *  tc956xmac_set_rx_mode - entry point for multicast addressing
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled.
 *  Return value:
 *  void.
 */
static void tc956xmac_set_rx_mode(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifndef TC956X_SRIOV_VF
	tc956xmac_set_filter(priv, priv->hw, dev);
#else
	tc956x_vf_set_filter(priv->hw, dev);
#endif
}

/**
 *  tc956xmac_change_mtu - entry point to change MTU size for the device.
 *  @dev : device pointer.
 *  @new_mtu : the new MTU size for the device.
 *  Description: the Maximum Transfer Unit (MTU) is used by the network layer
 *  to drive packet transmission. Ethernet has an MTU of 1500 octets
 *  (ETH_DATA_LEN). This value can be changed with ifconfig.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int tc956xmac_change_mtu(struct net_device *dev, int new_mtu)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int txfifosz = priv->plat->tx_fifo_size;


	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	txfifosz /= priv->plat->tx_queues_to_use;

	if (netif_running(dev)) {
		netdev_err(priv->dev, "must be stopped to change its MTU\n");
		return -EBUSY;
	}

	new_mtu = TC956XMAC_ALIGN(new_mtu);
#ifdef TC956X
	/* Supported frame sizes */
	if ((new_mtu < MIN_SUPPORTED_MTU) || (new_mtu > JUMBO_LEN)) {
		NMSGPR_ALERT(priv->device,
		       "%s: invalid MTU, min %d and max %d MTU are supported\n",
		       dev->name, MIN_SUPPORTED_MTU, JUMBO_LEN);
		return -EINVAL;
	}

#else
	/* If condition true, FIFO is too small or MTU too large */
	if ((txfifosz < new_mtu) || (new_mtu > BUF_SIZE_16KiB))
		return -EINVAL;
#endif
	dev->mtu = new_mtu;

	netdev_update_features(dev);

	return 0;
}

static netdev_features_t tc956xmac_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);


#ifdef TC956X_SRIOV_VF
	/* Update dev->feature RX Checksum and RX FCS flags based on states
	 * received from PF via mailbox.
	 */
	if (priv->rx_csum_state)
		features |= NETIF_F_RXCSUM;
	else
		features &= ~NETIF_F_RXCSUM;

	if (priv->rx_crc_pad_state)
		features &= ~NETIF_F_RXFCS;
	else
		features |= NETIF_F_RXFCS;
#endif
	if (priv->plat->rx_coe == TC956XMAC_RX_COE_NONE)
		features &= ~NETIF_F_RXCSUM;

	if (!priv->plat->tx_coe)
		features &= ~NETIF_F_CSUM_MASK;

	/* Some GMAC devices have a bugged Jumbo frame support that
	 * needs to have the Tx COE disabled for oversized frames
	 * (due to limited buffer sizes). In this case we disable
	 * the TX csum insertion in the TDES and not use SF.
	 */
	if (priv->plat->bugged_jumbo && (dev->mtu > ETH_DATA_LEN))
		features &= ~NETIF_F_CSUM_MASK;

#ifdef TC956X_SRIOV_PF
	/* Disable tso if asked by ethtool */
	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		if (features & NETIF_F_TSO) {
			priv->tso = true;
			/* Update driver cap to let VF know about
			 * feature enable/disable
			 */
			priv->pf_drv_cap.tso_en = true;
		} else {
			priv->tso = false;
			/* Update driver cap to let VF know about
			 * feature enable/disable
			 */
			priv->pf_drv_cap.tso_en = false;
		}
	}
#elif defined TC956X_SRIOV_VF
	if (priv->pf_drv_cap.tso_en) {
		/* Disable tso if asked by ethtool */
		if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
			if (features & NETIF_F_TSO)
				priv->tso = true;
			else
				priv->tso = false;
		}
	}
#endif

	return features;
}

static int tc956xmac_set_features(struct net_device *netdev,
			       netdev_features_t features)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);
	bool sph_en;
	u32 chan;
#ifdef TC956X
	netdev_features_t txvlan, rxvlan, rxvlan_filter;

	rxvlan = (netdev->features & NETIF_F_HW_VLAN_CTAG_RX);
	txvlan = (netdev->features & NETIF_F_HW_VLAN_CTAG_TX);
	rxvlan_filter = (netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER);
#endif

#ifdef TC956X_SRIOV_VF
	/* RX CSUM is handled in PF as it impacts all drivers */
	if (priv->pf_drv_cap.csum_en)
		priv->hw->rx_csum = priv->plat->rx_coe;
	else
		priv->hw->rx_csum = 0;
#else
	/* Keep the COE Type in case of csum is supporting */
	if (features & NETIF_F_RXCSUM) {
		if (priv->hw->rx_csum != priv->plat->rx_coe) {
		priv->hw->rx_csum = priv->plat->rx_coe;
		priv->rx_csum_state = priv->hw->rx_csum;
#ifdef TC956X_SRIOV_PF
	tc956x_mbx_wrap_set_rx_csum(priv);
#endif
		}
	} else {
		if (priv->hw->rx_csum != 0) {
			priv->hw->rx_csum = 0;
			priv->rx_csum_state = priv->hw->rx_csum;
#ifdef TC956X_SRIOV_PF
			tc956x_mbx_wrap_set_rx_csum(priv);
#endif
		}
	}

	/* No check needed because rx_coe has been set before and it will be
	 * fixed in case of issue.
	 */
	tc956xmac_rx_ipc(priv, priv->hw);
#endif
	/* Tx Checksum Configuration via Ethtool */
	if ((features & NETIF_F_IP_CSUM) || (features & NETIF_F_IPV6_CSUM))
		priv->csum_insertion = 1;
	else
		priv->csum_insertion = 0;

	KPRINT_DEBUG1("priv->csum_insertion = %d\n", priv->csum_insertion);
#ifdef TC956X_SRIOV_PF /* Clean up needed */
	/* Rx fcs Configuration via Ethtool */
	if (features & NETIF_F_RXFCS) {
		if (priv->rx_crc_pad_state != (priv->rx_crc_pad_state & (~TC956X_RX_CRC_DEFAULT))) {
			priv->rx_crc_pad_state &= (~TC956X_RX_CRC_DEFAULT);
			tc956x_mbx_wrap_set_rx_crc(priv);
		}
	} else {
		if (priv->rx_crc_pad_state != TC956X_RX_CRC_DEFAULT) {
			priv->rx_crc_pad_state = TC956X_RX_CRC_DEFAULT;
			tc956x_mbx_wrap_set_rx_crc(priv);
		}
	}

	tc956x_rx_crc_pad_config(priv, priv->rx_crc_pad_state);
	KPRINT_DEBUG1("priv->rx_crc_pad_state = %d\n", priv->rx_crc_pad_state);

	sph_en = (priv->hw->rx_csum > 0) && priv->sph;
	for (chan = 0; chan < priv->plat->rx_queues_to_use; chan++)
		tc956xmac_enable_sph(priv, priv->ioaddr, sph_en, chan);

#elif defined TC956X_SRIOV_VF
	/* Rx fcs Configuration via Ethtool */
	if (features & NETIF_F_RXFCS) {
		priv->rx_crc_pad_state &= (~TC956X_RX_CRC_DEFAULT);
	} else {
#ifdef TC956X
		priv->rx_crc_pad_state = TC956X_RX_CRC_DEFAULT;
#endif
	}

	sph_en = (priv->hw->rx_csum > 0) && priv->sph;
	for (chan = 0; chan < priv->plat->rx_queues_to_use; chan++) {
		if (priv->plat->ch_in_use[chan] == 0)
			continue;
		tc956xmac_enable_sph(priv, priv->ioaddr, sph_en, chan);
	}
#endif
#ifdef TC956X
	if ((features & NETIF_F_HW_VLAN_CTAG_RX) && !rxvlan)
		tc956xmac_enable_rx_vlan_stripping(priv, priv->hw);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_RX) && rxvlan)
		tc956xmac_disable_rx_vlan_stripping(priv, priv->hw);

	if ((features & NETIF_F_HW_VLAN_CTAG_TX) && !txvlan)
		tc956xmac_enable_vlan(priv, priv->hw, TC956XMAC_VLAN_INSERT);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_TX) && txvlan)
		tc956xmac_disable_tx_vlan(priv, priv->hw);

	if ((features & NETIF_F_HW_VLAN_CTAG_FILTER) && !rxvlan_filter)
		tc956xmac_enable_rx_vlan_filtering(priv, priv->hw);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER) && rxvlan_filter)
		tc956xmac_disable_rx_vlan_filtering(priv, priv->hw);
#endif
	return 0;
}

#ifdef TC956X_SRIOV_PF
/**
 * tc956xmac_rx_dma_error_recovery
 *
 * \brief API to handle and recover from the dma err
 *
 * \details This function is used to handle and recover from the dma error
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return None
 */
static void tc956xmac_rx_dma_error_recovery(struct tc956xmac_priv *priv)
{
	u8 ch;
	u32 intr_status = 0;
	u32 reg_val = 0;
#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	unsigned long flags;
#endif
	for (ch = 0; ch < TC956XMAC_CH_MAX; ch++) {
		intr_status = 0;
		reg_val = 0;

		intr_status = readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(ch));

		if ((intr_status & TC956X_DMA_RX_FBE) && (intr_status & TC956X_DMA_RX_RPS)) {
			reg_val = readl(priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL(ch));
			reg_val |= 0x80000000;  //set bit 31 to flush ch
			writel(reg_val, priv->ioaddr + XGMAC_DMA_CH_RX_CONTROL(ch));
			writel(intr_status, priv->ioaddr + XGMAC_DMA_CH_STATUS(ch));
#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
			spin_lock_irqsave(&priv->wq_lock, flags);
			priv->mbx_wq_param.fn_id = SCH_WQ_RX_DMA_ERR;
			tc956xmac_service_mbx_event_schedule(priv);
			spin_unlock_irqrestore(&priv->wq_lock, flags);
#endif
		}
	}
}
#endif

/**
 *  tc956xmac_interrupt_v0 - main ISR
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 *  Description: this is the main driver interrupt service routine.
 *  It can call:
 *  o DMA service routine (to manage incoming frame reception and transmission
 *    status)
 *  o Core interrupts to manage: remote wake-up, management counter, LPI
 *    interrupts.
 */
static irqreturn_t tc956xmac_interrupt_v0(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifndef TC956X_SRIOV_VF
	u32 val = 0;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_AUTOMOTIVE_CONFIG)
	u32 queue, value;
	uint32_t uiIntSts, uiIntclr = 0;
#endif
	u32 queues_count;
#ifdef TC956X_SRIOV_VF
	enum mbx_msg_fns msg_src;
	u8 i;
#endif
#ifdef TC956X
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;

	queues_count = (rx_cnt > tx_cnt) ? rx_cnt : tx_cnt;
#endif
#ifndef TC956X_SRIOV_VF
	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	priv->xstats.total_interrupts++;

	if (val & (0xFF << 3))
		priv->xstats.tx_intr_n++;

	if (val & (0xFF << 11))
		priv->xstats.rx_intr_n++;
	/* Checking if any RBUs occurred and updating the statistics corresponding to channel */
#endif

#if defined(TC956X_SRIOV_PF) && defined(TC956X_AUTOMOTIVE_CONFIG)
	for (queue = 0; queue < queues_count; queue++) {
		uiIntSts = readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(queue));

		/* Assuming DMA Tx and Rx channels are used as pairs */
		if ((priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW) ||
		    (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)) {
			uiIntSts = readl(priv->ioaddr + XGMAC_DMA_CH_STATUS(queue));
			/* Handling Abnormal interrupts of NON S/W path, TI & RI are handled in FW */
			if (unlikely(uiIntSts & XGMAC_AIS)) {
				if (unlikely(uiIntSts & XGMAC_RBU)) {
					priv->xstats.rx_buf_unav_irq[queue]++;
					uiIntclr |= XGMAC_RBU;
				}
				if (unlikely(uiIntSts & XGMAC_TPS)) {
					priv->xstats.tx_process_stopped_irq[queue]++;
					uiIntclr |= XGMAC_TPS;
				}
				if (unlikely(uiIntSts & XGMAC_FBE)) {
					priv->xstats.fatal_bus_error_irq[queue]++;
					uiIntclr |= XGMAC_FBE;
				}
				if (unlikely(uiIntSts & XGMAC_RPS)) {
					uiIntclr |= XGMAC_RPS;
				}
				uiIntclr |= XGMAC_AIS;
				writel(uiIntclr, (priv->ioaddr + XGMAC_DMA_CH_STATUS(queue)));
			}

			/* Disable RBU interrupt on RBU interrupt occurance. IPA SW should enable it back */
			value = readl(priv->ioaddr + XGMAC_DMA_CH_INT_EN(queue));
			if ( ((uiIntclr & XGMAC_RBU) == XGMAC_RBU) && (value & XGMAC_RBUE)) {
				value = readl(priv->ioaddr + XGMAC_DMA_CH_INT_EN(queue));
				value &= ~XGMAC_RBUE;
				writel(value, priv->ioaddr + XGMAC_DMA_CH_INT_EN(queue));
				printk("***RBU INT disabled***XGMAC_DMA_CH_INT_EN[%d]***** :0x%x\n", queue, readl(priv->ioaddr + XGMAC_DMA_CH_INT_EN(queue)));
			}
		}
	}
#endif
	/* To handle DMA interrupts */
	tc956xmac_dma_interrupt(priv); /* TODO: To be compared sequence with Beta-2 */


#ifdef TC956X_SRIOV_PF
#if defined(TC956X_SRIOV_PF) && (defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE))
	tc956xmac_interrupt_v1(irq, dev_id);
#endif
	/* unmask MSI vector 0 */
	tc956x_msi_intr_clr(priv, dev, TC956X_MSI_VECTOR_0);
#elif defined TC956X_SRIOV_VF
		/* Run the through the loop PF and MCU */

	for (i = 0; i < PFS_MAX; i++) {
		msg_src = tc956x_vf_get_fn_idx_from_int_sts(priv,
							    &priv->fn_id_info);
		if (msg_src >= 0 && msg_src <= 2) {
			/* Read and ack the mail and call respective
			 * message type functions to perform the action
			 */
			tc956x_vf_parse_mbx(priv, msg_src);
		} else {
			/* No valid Interrupt*/
		}
	}

	/* unmask MSI vector 0 */
	tc956x_msi_intr_clr(priv, dev, TC956X_MSI_VECTOR_0, &priv->fn_id_info);
#endif
	return IRQ_HANDLED;
}

#ifdef TC956X_SRIOV_PF
static irqreturn_t tc956xmac_interrupt_v1(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct tc956xmac_priv *priv = netdev_priv(dev);

#ifdef TC956X
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
#endif

#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	u32 i;
	enum mbx_msg_fns msg_src;
	unsigned long flags;
#endif

	u32 queues_count;
	u32 queue;
	bool xmac;
	u32 val = 0;
	//uint32_t uiIntSts, uiIntclr = 0;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;
#ifdef TC956X
	queues_count = (rx_cnt > tx_cnt) ? rx_cnt : tx_cnt;
#endif
	if (priv->irq_wake)
		pm_wakeup_event(priv->device, 0);

	if (unlikely(!dev)) {
		netdev_err(priv->dev, "%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}

	/* Check if adapter is up */
	if (test_bit(TC956XMAC_DOWN, &priv->state))
		return IRQ_HANDLED;
	/* Check if a fatal error happened */
	if (tc956xmac_safety_feat_interrupt(priv))
		return IRQ_HANDLED;

	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	priv->xstats.total_interrupts++;

	if (val & (1 << 0))
		priv->xstats.lpi_intr_n++;

	if (val & (1 << 1))
		priv->xstats.pmt_intr_n++;

	if (val & (1 << 2))
		priv->xstats.event_intr_n++;

	if (val & (1 << 19))
		priv->xstats.xpcs_intr_n++;

	if (val & (1 << 20))
		priv->xstats.phy_intr_n++;

	if (val & (1 << 24))
		priv->xstats.sw_msi_n++;

#ifdef TC956X_SRIOV_PF
	tc956xmac_rx_dma_error_recovery(priv);
#endif

	/* To handle GMAC own interrupts */
	if ((priv->plat->has_gmac) || xmac) {
		int status = tc956xmac_host_irq_status(priv, priv->hw, &priv->xstats);
		int mtl_status;

		if (unlikely(status)) {
			/* For LPI we need to save the tx status */
			if (status & CORE_IRQ_TX_PATH_IN_LPI_MODE)
				priv->tx_path_in_lpi_mode = true;
			if (status & CORE_IRQ_TX_PATH_EXIT_LPI_MODE)
				priv->tx_path_in_lpi_mode = false;
		}

		for (queue = 0; queue < queues_count; queue++) {

			struct tc956xmac_rx_queue *rx_q;

#ifdef TC956X_SRIOV_PF
			if (priv->plat->rx_q_in_use[queue] == TC956X_DISABLE_QUEUE)
				continue;

#endif
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
			if ((queue == TC956X_ONE) || (queue == TC956X_TWO))
				continue;
#endif
			mtl_status = tc956xmac_host_mtl_irq_status(priv, priv->hw,
								queue);

			if (mtl_status != -EINVAL)
				status |= mtl_status;

			if (status & CORE_IRQ_MTL_RX_OVERFLOW) {
				u8 pf_dma_ch;

				status = status &  (~CORE_IRQ_MTL_RX_OVERFLOW);
				priv->mbx_wq_param.queue_no = queue;
				KPRINT_INFO("CORE_IRQ_MTL_RX_OVERFLOW for queue = %d\n", queue);
				if (queue == 0 || queue == 1 || queue == 7) {
					pf_dma_ch = priv->pf_queue_dma_map[queue];
					rx_q = &priv->rx_queue[pf_dma_ch];
#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
					spin_lock_irqsave(&priv->wq_lock, flags);
					tc956xmac_service_mbx_event_schedule(priv);
					spin_unlock_irqrestore(&priv->wq_lock, flags);
#endif
					tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr,
										rx_q->rx_tail_addr, pf_dma_ch);
				} else if (queue == 2 || queue == 3) {
					pf_dma_ch = priv->pf_queue_dma_map[queue];
					rx_q = &priv->rx_queue[pf_dma_ch];
					tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr,
									rx_q->rx_tail_addr, pf_dma_ch);
#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
				} else {
					spin_lock_irqsave(&priv->wq_lock, flags);
					tc956xmac_service_mbx_event_schedule(priv);
					spin_unlock_irqrestore(&priv->wq_lock, flags);
#endif
				}
			}
		}

		/* PCS link status */
		if (priv->hw->pcs) {
			if (priv->xstats.pcs_link)
				netif_carrier_on(dev);
			else
				netif_carrier_off(dev);
		}
	}


#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	/* Mailbox Events */
	/* Run the through the loop of VF, PF (other PF) and MCU */
	for (i = 0; i < FNS_MAX; i++) {
		msg_src = tc956x_pf_get_fn_idx_from_int_sts(priv,
							    &priv->fn_id_info);

		if (msg_src >= 0 && msg_src <= 5) {
			/* Read and ack the mail and call respective
			 * message type functions to perform the action
			 */
			tc956x_pf_parse_mbx(priv, msg_src);
		} else {
			/* No valid Interrupt*/
		}
	}
#endif

	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	if (val & TC956X_EXT_PHY_ETH_INT) {
		KPRINT_INFO("PHY Interrupt %s\n", __func__);
#ifndef TC956X_SRIOV_VF
		if (priv->port_link_down == true)
			tc956xmac_link_change_set_power(priv, LINK_UP); /* Restore, De-assert and Enable Reset and Clock */
#endif
		/* Queue the work in system_wq */
		if (priv->tc956x_port_pm_suspend == true) {
			KPRINT_INFO("%s : (Do not queue PHY Work during suspend. Set WOL Interrupt flag)\n", __func__);
			priv->tc956xmac_pm_wol_interrupt = true;
		} else {
			KPRINT_INFO("%s : (Queue PHY Work.)\n", __func__);
			queue_work(system_wq, &priv->emac_phy_work);
		}
		/* phy_mac_interrupt(priv->dev->phydev); */
		/* MSI_OUT_EN: Reading */
		val = readl(priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
		val &= (~(1 << MSI_INT_EXT_PHY));
		/* MSI_OUT_EN: Writing to disable MAC Ext Interrupt*/
		writel(val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	}

#ifdef TC956X_SW_MSI
	val = readl(priv->ioaddr + TC956X_MSI_INT_STS_OFFSET(priv->port_num));
	if (val & TC956X_SW_MSI_INT) {
		/*Clear SW MSI*/
		writel(1, priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

		val = readl(priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	}
#endif

#ifdef TC956X
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	/* unmask MSI vector 1 */
	tc956x_msi_intr_clr(priv, dev, TC956X_MSI_VECTOR_1);
#endif
#endif

	return IRQ_HANDLED;
}
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled.
 */
static void tc956xmac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
#ifdef TC956X_SRIOV_PF
	tc956xmac_interrupt_v0(dev->irq, dev);
	tc956xmac_interrupt_v1(dev->irq, dev);
#elif defined TC956X_SRIOV_VF
	tc956xmac_interrupt_v0(dev->irq, dev);
	//tc956xmac_interrupt_v1(dev->irq, dev);
#endif
	enable_irq(dev->irq);
}
#endif
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

int tc956xmac_rx_parser_configuration(struct tc956xmac_priv *priv)
{
	int ret = -EINVAL, re_init_eee = 0, dly_cnt = 0, ret_val;
	struct ethtool_eee edata;

#ifndef TC956X_SRIOV_VF
	/* Disable EEE before configuring FRP */
	if (priv->eee_enabled) {
		if (priv->hw->xpcs) {
			tc956x_xpcs_ctrl0_lrx(priv, false);
			re_init_eee = 1;

		} else if (priv->dev->phydev && priv->dev->phydev->link) {
			/* Disable EEE only if PHY link is Up */

			ret_val = phylink_ethtool_get_eee(priv->phylink, &edata);
			if (ret_val)
				KPRINT_INFO("Phylink get EEE error \n\r");

			KPRINT_INFO("Disabling EEE \n\r");
			priv->eee_enabled = 0;
			edata.tx_lpi_enabled = priv->eee_enabled;
			edata.tx_lpi_timer = priv->tx_lpi_timer;
			edata.eee_enabled = priv->eee_enabled;

			set_bit(TC956XMAC_DOWN, &priv->link_state);

			tc956xmac_disable_eee_mode(priv);
			ret_val = phylink_ethtool_set_eee(priv->phylink, &edata);
			ret_val |= phy_ethtool_set_eee_2p5(priv->dev->phydev, &edata);
			if (ret_val)
				KPRINT_INFO("Phylink EEE config error \n\r");

			/* Wait for AN - link down, link up sequence */
			while (test_bit(TC956XMAC_DOWN, &priv->link_state)) {
				msleep(10);
				if (dly_cnt++ >= TC956X_MAX_LINK_DELAY) {
					KPRINT_INFO("Link Up Timeout \n\r");
					break;
				}
			}
			KPRINT_INFO("AN duration : %d \n\r", dly_cnt*10);

			re_init_eee = 1;
		}
	}
#endif
	if (priv->hw->mac->rx_parser_init && priv->plat->rxp_cfg.enable)
		ret = tc956xmac_rx_parser_init(priv,
			priv->dev, priv->hw, priv->dma_cap.spram,
			priv->dma_cap.frpsel, priv->dma_cap.frpes,
			&priv->plat->rxp_cfg);

#ifndef TC956X_SRIOV_VF
	/* Restore EEE state */
	if (re_init_eee) {
		re_init_eee = 0;
		if (priv->hw->xpcs)
			tc956x_xpcs_ctrl0_lrx(priv, true);
		else {
			ret_val = phylink_ethtool_get_eee(priv->phylink, &edata);
			if (ret_val)
				KPRINT_INFO("Phylink get EEE error \n\r");

			KPRINT_INFO("Enabling EEE \n\r");
			edata.tx_lpi_enabled = priv->eee_enabled;
			edata.tx_lpi_timer = priv->tx_lpi_timer;

			edata.eee_enabled = 1;

			set_bit(TC956XMAC_DOWN, &priv->link_state);

			edata.eee_enabled = tc956xmac_eee_init(priv);
			priv->eee_enabled = edata.eee_enabled;
			if (!edata.eee_enabled)
				KPRINT_INFO("Error in init_eee \n\r");

			ret_val = phylink_ethtool_set_eee(priv->phylink, &edata);
			ret_val |= phy_ethtool_set_eee_2p5(priv->dev->phydev, &edata);
			if (ret_val)
				KPRINT_INFO("Phylink EEE config error \n\r");

			/* Wait for AN - link down, link up sequence */
			dly_cnt = 0;
			while (test_bit(TC956XMAC_DOWN, &priv->link_state)) {
				msleep(10);
				if (dly_cnt++ >= TC956X_MAX_LINK_DELAY) {
					KPRINT_INFO("Link Up Timeout \n\r");
					break;
				}
			}
			KPRINT_INFO("AN duration : %d \n\r", dly_cnt*10);
		}
	}

#endif
		/* spram feautre is not present in TC956X */
	if (ret)
		priv->rxp_enabled = false;
	else
		priv->rxp_enabled = true;

	return ret;
}

#ifndef TC956X_SRIOV_VF
static int tc956xmac_est_configuration_ioctl(struct tc956xmac_priv *priv)
{
	int ret = -EINVAL;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X
	ret = tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
				   priv->plat->clk_ptp_rate);
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return ret;
}

/**
 * tc956xmac_ioctl_get_cbs - gets the cbs parameter
 * @priv: driver private structure
 * @data: tc956xmac_ioctl_cbs_cfg strcuture passed by user
 * Note: percentage element is dummy parameter and is not used in code.
 * Description :  this function gets called when ioctl TC956XMAC_IOTL_GET_CBS
 * is invoked.
 * As pre-requisite for calling this function, CBS Set must be done before get.
 *
 */
#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_get_cbs(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_get_cbs(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;
	u8 qmode;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	memcpy(&cbs, data, sizeof(cbs));
#else
	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;
#endif

	/* queue 0 is reserved for legacy traffic; cbs configuration not allowed (registers also not available for Q0)*/
	if ((cbs.queue_idx >= tx_qcount) || (cbs.queue_idx == 0))
		return -EINVAL;

	qmode = priv->plat->tx_queues_cfg[cbs.queue_idx].mode_to_use;

	/* Only AVB queue supported for cbs */
	if (qmode != MTL_QUEUE_AVB)
		return -EINVAL;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
	cbs.speed100cfg.send_slope = priv->cbs_speed100_cfg[cbs.queue_idx].send_slope;
	cbs.speed100cfg.idle_slope = priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope;
	cbs.speed100cfg.high_credit = priv->cbs_speed100_cfg[cbs.queue_idx].high_credit;
	cbs.speed100cfg.low_credit = priv->cbs_speed100_cfg[cbs.queue_idx].low_credit;

	cbs.speed1000cfg.send_slope = priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope;
	cbs.speed1000cfg.idle_slope = priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed1000cfg.high_credit = priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit;
	cbs.speed1000cfg.low_credit = priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit;

	cbs.speed10000cfg.send_slope = priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope;
	cbs.speed10000cfg.idle_slope = priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed10000cfg.high_credit = priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit;
	cbs.speed10000cfg.low_credit = priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit;

	cbs.speed5000cfg.send_slope = priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope;
	cbs.speed5000cfg.idle_slope = priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope;
	cbs.speed5000cfg.high_credit = priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit;
	cbs.speed5000cfg.low_credit = priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit;

	cbs.speed2500cfg.send_slope = priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope;
	cbs.speed2500cfg.idle_slope = priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope;
	cbs.speed2500cfg.high_credit = priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit;
	cbs.speed2500cfg.low_credit = priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit;
#ifdef TC956X_SRIOV_PF
	memcpy(data, &cbs, sizeof(cbs));
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
#else
	if (copy_to_user(data, &cbs, sizeof(cbs)))
		return -EFAULT;
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_set_cbs(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_set_cbs(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;
	u8 qmode;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	memcpy(&cbs, data, sizeof(cbs));
#else
	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;
#endif

	/* queue 0 is reserved for legacy traffic; cbs configuration not allowed (registers also not available for Q0)*/
	if ((cbs.queue_idx >= tx_qcount) || (cbs.queue_idx == 0))
		return -EINVAL;

	if (!priv->hw->mac->config_cbs)
		return -EINVAL;

#ifdef TC956X_SRIOV_VF
	/* skip configuring for unallocated queue */
	if (priv->plat->tx_q_in_use[cbs.queue_idx] == 0)
		return -EINVAL;
#endif

	qmode = priv->plat->tx_queues_cfg[cbs.queue_idx].mode_to_use;

	if (qmode != MTL_QUEUE_AVB)
		return -EINVAL;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
	priv->cbs_speed100_cfg[cbs.queue_idx].send_slope = cbs.speed100cfg.send_slope;
	priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope = cbs.speed100cfg.idle_slope;
	priv->cbs_speed100_cfg[cbs.queue_idx].high_credit = cbs.speed100cfg.high_credit;
	priv->cbs_speed100_cfg[cbs.queue_idx].low_credit = cbs.speed100cfg.low_credit;

	priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope = cbs.speed1000cfg.send_slope;
	priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope = cbs.speed1000cfg.idle_slope;
	priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit = cbs.speed1000cfg.high_credit;
	priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit = cbs.speed1000cfg.low_credit;

	priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope = cbs.speed10000cfg.send_slope;
	priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope = cbs.speed10000cfg.idle_slope;
	priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit = cbs.speed10000cfg.high_credit;
	priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit = cbs.speed10000cfg.low_credit;

	priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope = cbs.speed2500cfg.send_slope;
	priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope = cbs.speed2500cfg.idle_slope;
	priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit = cbs.speed2500cfg.high_credit;
	priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit = cbs.speed2500cfg.low_credit;


	priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope = cbs.speed5000cfg.send_slope;
	priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope = cbs.speed5000cfg.idle_slope;
	priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit = cbs.speed5000cfg.high_credit;
	priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit = cbs.speed5000cfg.low_credit;

	if (priv->speed == SPEED_100) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed100_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed100_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed100_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed100_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_1000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed1000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed1000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed1000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed1000_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_10000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed10000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed10000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed10000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed10000_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_2500) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed2500_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed2500_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed2500_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed2500_cfg[cbs.queue_idx].low_credit;
	} else if (priv->speed == SPEED_5000) {
		priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope =
								priv->cbs_speed5000_cfg[cbs.queue_idx].send_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope =
								priv->cbs_speed5000_cfg[cbs.queue_idx].idle_slope;
		priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit =
								priv->cbs_speed5000_cfg[cbs.queue_idx].high_credit;
		priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit =
								priv->cbs_speed5000_cfg[cbs.queue_idx].low_credit;
	}
#ifdef TC956X_SRIOV_PF
	tc956xmac_config_cbs(priv, priv->hw, priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope,
				priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope,
				priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit,
				priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit,
				cbs.queue_idx);
#elif defined TC956X_SRIOV_VF
			tc956xmac_config_cbs(priv,
			priv->plat->tx_queues_cfg[cbs.queue_idx].send_slope,
			priv->plat->tx_queues_cfg[cbs.queue_idx].idle_slope,
			priv->plat->tx_queues_cfg[cbs.queue_idx].high_credit,
			priv->plat->tx_queues_cfg[cbs.queue_idx].low_credit,
			cbs.queue_idx);
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_get_est(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_get_est(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_ioctl_est_cfg *est;
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
#ifdef TC956X_SRIOV_PF
	est = data;
#else
	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.est, flags);
#endif
	est->enabled = priv->plat->est->enable;
	est->estwid = priv->dma_cap.estwid;
	est->estdep = priv->dma_cap.estdep;
	est->btr_offset[0] = priv->plat->est->btr_offset[0];
	est->btr_offset[1] = priv->plat->est->btr_offset[1];
	est->ctr[0] = priv->plat->est->ctr[0];
	est->ctr[1] = priv->plat->est->ctr[1];
	est->ter = priv->plat->est->ter;
	est->gcl_size = priv->plat->est->gcl_size;
	memcpy(est->gcl, priv->plat->est->gcl, est->gcl_size * sizeof(*est->gcl));

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.est, flags);
#endif

#ifndef TC956X_SRIOV_PF
	if (copy_to_user(data, est, sizeof(*est))) {
		ret = -EFAULT;
		goto out_free;
	}
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
#ifndef TC956X_SRIOV_PF
out_free:
	kfree(est);
#endif
	return ret;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_set_est(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_set_est(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_est *cfg = priv->plat->est;
	struct tc956xmac_ioctl_est_cfg *est;
	int ret = 0;
	u64 system_time;
	u32 system_time_s;
	u32 system_time_ns;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u64 quotient;
	u32 reminder;

#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	est = data;
#else
	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;

	if (copy_from_user(est, data, sizeof(*est))) {
		ret = -EFAULT;
		goto out_free;
	}
#endif
	if (est->gcl_size > TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES) {
		ret = -EINVAL;
		goto out_free;
	}

	if (cfg->gcl_size > priv->dma_cap.estdep) {
		ret = -EINVAL;
		goto out_free;
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.est, flags);
#endif
	if (est->enabled) {
		cfg->btr_offset[0] = est->btr_offset[0];
		cfg->btr_offset[1] = est->btr_offset[1];
		cfg->ctr[0] = est->ctr[0];
		cfg->ctr[1] = est->ctr[1];
		cfg->ter = est->ter;
		cfg->gcl_size = est->gcl_size;

		/* BTR Offset */
		tc956xmac_get_systime(priv, priv->ptpaddr, &system_time);
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
		quotient = div_u64_rem(system_time, 1000000000ULL, &reminder);
		system_time_s = (u32)quotient;
		system_time_ns = reminder;
#else
		system_time_s = system_time / 1000000000;
		system_time_ns = system_time % 1000000000;
#endif
		cfg->btr[0] = cfg->btr_offset[0] + (u32)system_time_ns;
		cfg->btr[1] = cfg->btr_offset[1] + (u32)system_time_s;
		memcpy(cfg->gcl, est->gcl, cfg->gcl_size * sizeof(*cfg->gcl));
	} else {
		cfg->btr_offset[0] = 0;
		cfg->btr_offset[1] = 0;
		cfg->ctr[0] = 0;
		cfg->ctr[1] = 0;
		cfg->ter = 0;
		cfg->gcl_size = 0;
		memset(cfg->gcl, 0, sizeof(cfg->gcl));
	}

	cfg->enable = est->enabled;
	tc956xmac_est_configuration_ioctl(priv);

	if (!est->enabled)
		ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.est, flags);
#endif
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

out_free:
	kfree(est);
	return ret;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_get_fpe(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_get_fpe(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_ioctl_fpe_cfg *fpe;
	int ret = 0;
	unsigned int control = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	fpe = data;
#else
	fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);

	if (!fpe)
		return -ENOMEM;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.fpe, flags);
#endif
#ifdef TC956X
	control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
#endif

	if (control & XGMAC_EFPE) {
		fpe->enabled = 1;
#ifdef TC956X
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		fpe->pec = (control & XGMAC_MTL_FPE_PEC_MASK) >> XGMAC_MTL_FPE_PEC_SHIFT;
		fpe->afsz = control & XGMAC_MTL_FPE_AFSZ_MASK;
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		fpe->HA_time =  (control & XGMAC_MTL_FPE_HOLD_ADVANCE_MASK);
		fpe->RA_time =  (control & XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK) >> XGMAC_MTL_FPE_ADVANCE_SHIFT;
#endif
	} else {
		fpe->enabled = 0;
		fpe->pec = 0;
		fpe->afsz = 0;
		fpe->HA_time = 0;
		fpe->RA_time = 0;
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.fpe, flags);
#endif
#ifndef TC956X_SRIOV_PF
	if (copy_to_user(data, fpe, sizeof(*fpe))) {
		ret = -EFAULT;
		goto out_free;
	}
out_free:
		kfree(fpe);
#endif
		return ret;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_set_fpe(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_set_fpe(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_ioctl_fpe_cfg *fpe;
	int ret = 0;
	unsigned int control = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	fpe = data;
#else
	fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);
	if (!fpe)
		return -ENOMEM;
	if (copy_from_user(fpe, data, sizeof(*fpe))) {
		ret = -EFAULT;
		goto out_free;
	}
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.fpe, flags);
#endif
#ifdef TC956X
	if (fpe->enabled) {
		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		control &= ~(XGMAC_MTL_FPE_PEC_MASK | XGMAC_MTL_FPE_AFSZ_MASK);
		control |= ((fpe->pec << XGMAC_MTL_FPE_PEC_SHIFT) & XGMAC_MTL_FPE_PEC_MASK) |
				(fpe->afsz & XGMAC_MTL_FPE_AFSZ_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		control &= ~(XGMAC_MTL_FPE_HOLD_ADVANCE_MASK | XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK);
		control |= ((fpe->RA_time << XGMAC_MTL_FPE_ADVANCE_SHIFT) & XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK) |
				(fpe->HA_time & XGMAC_MTL_FPE_HOLD_ADVANCE_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);

		control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
		control |= XGMAC_EFPE;
		writel(control, priv->ioaddr + XGMAC_FPE_CTRL_STS);
	} else {
		control = readl(priv->ioaddr + XGMAC_FPE_CTRL_STS);
		control &= ~XGMAC_EFPE;
		writel(control, priv->ioaddr + XGMAC_FPE_CTRL_STS);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);
		control &= ~(XGMAC_MTL_FPE_HOLD_ADVANCE_MASK | XGMAC_MTL_FPE_RELEASE_ADVANCE_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_ADVANCE);

		control = readl(priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
		control &= ~(XGMAC_MTL_FPE_PEC_MASK | XGMAC_MTL_FPE_AFSZ_MASK);
		writel(control, priv->ioaddr + XGMAC_MTL_FPE_CTRL_STS);
	}
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.fpe, flags);
#endif
#ifndef TC956X_SRIOV_PF
out_free:
	kfree(fpe);
#endif
	return ret;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_get_rxp(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_get_rxp(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_rx_parser_cfg *cfg = &priv->plat->rxp_cfg;
	struct tc956xmac_ioctl_rxp_cfg *rxp;
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif


	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	rxp = data;
#else
	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;
#endif
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.frp, flags);
#endif

	rxp->enabled = priv->rxp_enabled;
	rxp->frpes = priv->dma_cap.frpes;
	rxp->nve = cfg->nve;
	rxp->npe = cfg->npe;
	memcpy(rxp->entries, cfg->entries, rxp->nve * sizeof(*cfg->entries));

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif

#ifndef TC956X_SRIOV_PF
	if (copy_to_user(data, rxp, sizeof(*rxp))) {
		ret = -EFAULT;
		goto out_free;
	}

out_free:
	kfree(rxp);
#endif
	return ret;
}

#ifdef TC956X_SRIOV_PF
int tc956xmac_ioctl_set_rxp(struct tc956xmac_priv *priv, void *data)
#else
static int tc956xmac_ioctl_set_rxp(struct tc956xmac_priv *priv, void __user *data)
#endif
{
	struct tc956xmac_rx_parser_cfg *cfg = &priv->plat->rxp_cfg;
	struct tc956xmac_ioctl_rxp_cfg *rxp;
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

#ifdef TC956X_SRIOV_PF
	rxp = data;
#else
	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;

	if (copy_from_user(rxp, data, sizeof(*rxp))) {
		ret = -EFAULT;
		goto out_free;
	}
#endif
	if (rxp->nve > TC956XMAC_RX_PARSER_MAX_ENTRIES) {
		ret = -EINVAL;
		goto out_free;
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.frp, flags);
#endif
	if (rxp->enabled) {
		cfg->nve = rxp->nve;
		cfg->npe = rxp->npe;
		memcpy(cfg->entries, rxp->entries,
		       cfg->nve * sizeof(*cfg->entries));
	} else {
		cfg->nve = 0;
		cfg->npe = 0;
		memset(cfg->entries, 0, sizeof(cfg->entries));
	}

	ret = tc956xmac_rx_parser_configuration(priv);
	if (!rxp->enabled)
		ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif
out_free:
	kfree(rxp);
	return ret;
}
#endif

static int tc956xmac_ioctl_get_tx_free_desc(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_free_desc;
	struct tc956xmac_ioctl_free_desc ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

#ifdef TC956X_SRIOV_PF
	if ((ioctl_data.queue_idx < priv->plat->tx_queues_to_use) &&
		(priv->plat->tx_ch_in_use[ioctl_data.queue_idx] == TC956X_ENABLE_CHNL)) {
#else
	if ((ioctl_data.queue_idx < priv->plat->tx_queues_to_use) &&
		(priv->plat->ch_in_use[ioctl_data.queue_idx] == 1)) {
#endif
		tx_free_desc = tc956xmac_tx_avail(priv, ioctl_data.queue_idx);
		if (copy_to_user((void __user *)ioctl_data.ptr, &tx_free_desc, sizeof(unsigned int)))
			return -EFAULT;
	} else {
		NMSGPR_ALERT(priv->device, "Channel no %d is invalid\n", ioctl_data.queue_idx);
		return -EFAULT;
	}
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

#ifndef TC956X_SRIOV_VF
static int tc956xmac_ioctl_get_connected_speed(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_speed ioctl_data;

	memset(&ioctl_data, 0, sizeof(ioctl_data));

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	ioctl_data.connected_speed = priv->speed;

	if (copy_to_user(data, &ioctl_data, sizeof(ioctl_data)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}
#endif /* TC956X_SRIOV_VF */
#ifdef TC956X_IOCTL_REG_RD_WR_ENABLE

/*!
 * \brief API to read register
 * \param[in] address offset as per tc956x data-sheet
 * \param[in] bar number
 * \return register value
 */
static int tc956xmac_reg_rd(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

#ifdef TC956X
		if (ioctl_data.bar_num == 4)
			val = readl((void __iomem *)(priv->dev->base_addr + ioctl_data.addr));
		else if (ioctl_data.bar_num == 2)/*SRAM bar number 2*/
			val = readl((void __iomem *)(priv->tc956x_SRAM_pci_base_addr + ioctl_data.addr));
		else if (ioctl_data.bar_num == 0)/*PCI bridge bar number 0*/
			val = readl((void __iomem *)(priv->tc956x_BRIDGE_CFG_pci_base_addr + ioctl_data.addr));
#endif

	if (copy_to_user(ioctl_data.ptr, &val, sizeof(unsigned int)))
		return -EFAULT;
	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;

}

#ifndef TC956X_SRIOV_VF
/*!
 * \brief API to write register
 * \param[in] address offset as per tc956x data-sheet
 * \return register
 */
static int tc956xmac_reg_wr(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (copy_from_user(&val, ioctl_data.ptr, sizeof(unsigned int)))
		return -EFAULT;

#ifdef TC956X
	if (ioctl_data.bar_num == 4)
		writel(val, (void __iomem *)(priv->dev->base_addr + ioctl_data.addr));
	else if (ioctl_data.bar_num == 2)/*SRAM bar number 2*/
		writel(val, (void __iomem *)(priv->tc956x_SRAM_pci_base_addr + ioctl_data.addr));
	else if (ioctl_data.bar_num == 0)/*PCI bridge bar number 0*/
		writel(val, (void __iomem *)(priv->tc956x_BRIDGE_CFG_pci_base_addr + ioctl_data.addr));
#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}
#endif
#endif

#ifndef TC956X_SRIOV_VF
static int tc956xmac_ioctl_set_mac_loopback(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_loopback ioctl_data;
	u32 value = 0;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->mac_loopback_mode == ioctl_data.flags) {
		NDBGPR_L1(priv->device, " MAC loopback mode is already configured for the intended configuration\n");
		return 0;
	}
#ifdef TC956X
	value = readl((void __iomem *)priv->dev->base_addr + XGMAC_RX_CONFIG);
	if (ioctl_data.flags)
		value |= XGMAC_CONFIG_LM;
	else
		value &= ~XGMAC_CONFIG_LM;

	writel(value, (void __iomem *)priv->dev->base_addr + XGMAC_RX_CONFIG);
#endif

	priv->mac_loopback_mode = ioctl_data.flags;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

static int tc956xmac_ioctl_set_phy_loopback(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_phy_loopback ioctl_data;
#ifdef TC956X
	int ret;
#endif

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->phy_loopback_mode == ioctl_data.flags) {
		NDBGPR_L1(priv->device, " PHY loopback mode is already configured for the intended configuration\n");
		return 0;
	}

	priv->phy_loopback_mode = ioctl_data.flags;

#ifdef TC956X
	if (priv->phy_loopback_mode)
		ret = phy_loopback(priv->dev->phydev, true);
	else
		ret = phy_loopback(priv->dev->phydev, false);

	if (ret)
		return ret;

#endif

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

static int tc956xmac_config_l2_da_filter(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_l2_da_filter ioctl_data;
	int ret = 0;
	unsigned int reg_val = 0;

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (ioctl_data.perfect_hash) {
		if (HASH_TABLE_SIZE > 0)
			priv->l2_filtering_mode = 1;
		else
			ret = -1;
	} else
		priv->l2_filtering_mode = 0;

	/* configure the l2 filter matching mode */
	reg_val = readl(priv->ioaddr + XGMAC_PACKET_FILTER);
	reg_val &= ~XGMAC_PACKET_FILTER_DAIF;
	reg_val |= ((ioctl_data.perfect_inverse_match & 0x1)
		    << XGMAC_PACKET_FILTER_DAIF_LPOS);
	writel(reg_val, priv->ioaddr + XGMAC_PACKET_FILTER);
	return ret;
}

static int tc956xmac_config_vlan_filter(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_vlan_filter ioctl_data;
	u32 reg_val;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	if (!(priv->dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		return -EPERM;
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;
	/* Disabling VLAN is not supported */
	if (ioctl_data.filter_enb_dis == 0)
		return -EINVAL;

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif
#ifndef TC956X
	/* configure the vlan filter */
	reg_val = readl(priv->ioaddr + XGMAC_PACKET_FILTER);
	reg_val =  (ioctl_data.filter_enb_dis << XGMAC_VLAN_VLC_SHIFT);
	writel(reg_val, priv->ioaddr + XGMAC_PACKET_FILTER);
#endif
	reg_val = readl(priv->ioaddr + XGMAC_VLAN_TAG);
	reg_val = (reg_val & (~XGMAC_VLANTR_VTIM)) |
		  (ioctl_data.perfect_inverse_match << XGMAC_VLANTR_VTIM_LPOS);
	reg_val = (reg_val & (~XGMAC_VLAN_VTHM)) |
		  (ioctl_data.perfect_hash << XGMAC_VLANTR_VTHM_LPOS);

	/* When VLAN filtering is enabled, then VL/VID
	 * should be > zero, if VLAN is not configured. Otherwise all VLAN
	 * packets will be accepted. Hence we are writting 1 into VL. It also
	 * means that MAC will always receive VLAN pkt with VID = 1 if inverse
	 * march is not set.
	 */
	/* If hash filtering is enabled,
	 * By default enable MAC to calculate vlan hash on only 12-bits of
	 * received VLAN tag (ie only on VLAN id and ignore priority and other
	 * fields)
	 *
	 */

		reg_val = (reg_val & (~XGMAC_VLAN_ETV)) |
			  (1 << XGMAC_VLAN_VLC_SHIFT);

	writel(reg_val, priv->ioaddr + XGMAC_VLAN_TAG);
	priv->vlan_hash_filtering = ioctl_data.perfect_hash;

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
	KPRINT_INFO("Successfully %s VLAN %s filtering and %s matching\n",
		  (ioctl_data.filter_enb_dis ? "ENABLED" : "DISABLED"),
		  (ioctl_data.perfect_hash ? "HASH" : "PERFECT"),
		  (ioctl_data.perfect_inverse_match ? "INVERSE" : "PERFECT"));

	return 0;
}

/**
 *  tc956xmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently it supports the phy_mii_ioctl(...) and HW time stamping.
 */
/*!
 * \brief API to Check the FW status
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956x_xgmac_get_fw_status(struct tc956xmac_priv *priv,
					void __user *data)
{
	struct tc956x_ioctl_fwstatus ioctl_data;
	u32 wdt_count1, wdt_count2;
	u32 systick_count1, systick_count2;
	u32 fw_status;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;


	/* Read the WDT counter and Systik count value */
	wdt_count1 = readl(priv->ioaddr + INTC_INTINTWDMON);
#ifdef TC956X
	systick_count1 = readl(priv->tc956x_SRAM_pci_base_addr
				+ SYSTCIK_SRAM_OFFSET);
#endif

	mdelay(1);

	/* Read the WDT counter and Systick count value */
	wdt_count2 = readl(priv->ioaddr + INTC_INTINTWDMON);
#ifdef TC956X
	systick_count2 = readl(priv->tc956x_SRAM_pci_base_addr
				+ SYSTCIK_SRAM_OFFSET);
#endif
	fw_status  = 1; /* running */

	/*
	 * 1. Firmware running case
	 * wdt_count1 and  wdt_count2  have different values ( second value
	 * read after 1ms)
	 * systick_count1 and, systick_count2  have different vlaue ( second
	 * value read after 1ms)
	 * 2. Firmware not running case
	 * wdt_count1 and  wdt_count2  have same values ( 0 or FULL value)
	 * systick_count1 and, systick_count2 will have same values
	 */
	if ((wdt_count1 == wdt_count2) || (systick_count1 == systick_count2))
		fw_status  = -1; /* Not running */


	ioctl_data.wdt_count = wdt_count2;
	ioctl_data.systick_count = systick_count2;
	ioctl_data.fw_status = fw_status;

	if (copy_to_user(data, &ioctl_data, sizeof(ioctl_data)))
		return -EFAULT;


	DBGPR_FUNC(priv->device, "<-- %s\n", __func__);
	return 0;
}


/**
 * tc956x_ptp_configuration - Configure PTP
 * @priv: driver private structure
 * Description: It is used for configuring the PTP
 * Return value:
 * 0 on success or negative error number.
 */
static void tc956x_ptp_configuration(struct tc956xmac_priv *priv, u32 tcr_config)
{
	struct timespec64 now;
	u32 control, sec_inc;
	u64 temp;

	if (tcr_config == 0) {
		control = PTP_TCR_TSENA | PTP_TCR_TSCTRLSSR
				| PTP_TCR_TSCFUPDT | PTP_TCR_TSENALL;
		control |= PTP_TCR_PTGE;
		control |= 0x10013e03;
	} else {
		control = tcr_config;
	}

	tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, control);

	/* program Sub Second Increment reg */
#ifdef TC956X
	tc956xmac_config_sub_second_increment(priv,
		priv->ptpaddr, priv->plat->clk_ptp_rate,
		priv->plat->has_xgmac,
		&sec_inc);
#endif

	temp = div_u64(1000000000ULL, sec_inc);

	/*
	 * calculate default added value: formula is :
	 * addend = (2^32)/freq_div_ratio; where, freq_div_ratio = 1e9ns/sec_inc
	 */
	temp = (u64)(temp << 32);

	priv->default_addend = div_u64(temp, TC956X_PTP_SYSCLOCK);

	tc956xmac_config_addend(priv, priv->ptpaddr, priv->default_addend);

	ktime_get_real_ts64(&now);
	tc956xmac_init_systime(priv, priv->ptpaddr, (u32)now.tv_sec, now.tv_nsec);

	priv->hwts_tx_en = 1;
	priv->hwts_rx_en = 1;

}

static int pps_configuration(struct tc956xmac_priv *priv,
			 struct tc956xmac_PPS_Config *tc956x_pps_cfg)
{
	unsigned int sec, subsec, val, align_ns = 0;
	int value, interval, width, ppscmd, trgtmodsel = 0x3;

	value = readl(priv->ptpaddr + PTP_TCR);
	if (!(value & 0x00000001)) {
		tc956x_ptp_configuration(priv, 0);
		DBGPR_FUNC(priv->device, "--> %s\n", __func__);
	}

	interval = (tc956x_pps_cfg->ptpclk_freq + tc956x_pps_cfg->ppsout_freq/2)
			/ tc956x_pps_cfg->ppsout_freq;

	width = ((interval * tc956x_pps_cfg->ppsout_duty) + 50)/100 - 1;
	if (width >= interval)
		width = interval - 1;
	if (width < 0)
		width = 0;

	ppscmd = 0x3;	/* cancel start */
	DBGPR_FUNC(priv->device, "interval: %d, width: %d\n", interval, width);

	if (tc956x_pps_cfg->ppsout_align == 1) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: freq=%dHz, ch=%d, duty=%d, align=%d\n",
			tc956x_pps_cfg->ppsout_freq,
			tc956x_pps_cfg->ppsout_ch,
			tc956x_pps_cfg->ppsout_duty,
			tc956x_pps_cfg->ppsout_align_ns);
	} else {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: freq=%dHz, ch=%d, duty=%d, No alignment\n",
			tc956x_pps_cfg->ppsout_freq,
			tc956x_pps_cfg->ppsout_ch,
			tc956x_pps_cfg->ppsout_duty);
	}

	DBGPR_FUNC(priv->device, ": with PTP Clock freq=%dHz\n", tc956x_pps_cfg->ptpclk_freq);

	if (tc956x_pps_cfg->ppsout_align == 1) {
		align_ns = tc956x_pps_cfg->ppsout_align_ns;
		DBGPR_FUNC(priv->device, "(1000000000/tc956x_pps_cfg->ppsout_freq) : %d, tc956x_pps_cfg->ppsout_align_ns: %d\n",
			(1000000000/tc956x_pps_cfg->ppsout_freq), tc956x_pps_cfg->ppsout_align_ns);
		/* (1000000000/tc956x_pps_cfg->ppsout_freq))  adjust 32ns sync */
		if (align_ns < 32) {
			align_ns += (1000000000 - 32);	/* (1000000000/tc956x_pps_cfg->ppsout_freq)); */
			DBGPR_FUNC(priv->device, "align_ns : %x\n", align_ns);
		} else {
			align_ns -= 32;	/* (1000000000/tc956x_pps_cfg->ppsout_freq) */
		}
	}

#ifdef TC956X
	writel((interval-1), priv->ioaddr + XGMAC_PPSx_INTERVAL(tc956x_pps_cfg->ppsout_ch));
	writel(width, priv->ioaddr + XGMAC_PPSx_WIDTH(tc956x_pps_cfg->ppsout_ch));
#endif

	if (priv->port_num == RM_PF0_ID) {
		if (tc956x_pps_cfg->ppsout_ch == 0) {  /* PPO00 */
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= (~NFUNCEN4_GPIO2_MASK);	/* GPIO2 */
			val |= ((FUNCTION1 << NFUNCEN4_GPIO2_SHIFT) & NFUNCEN4_GPIO2_MASK);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
		} else if (tc956x_pps_cfg->ppsout_ch == 1) { /* PPO01 */
			val = readl(priv->ioaddr + NFUNCEN0_OFFSET);
			val &= (~NFUNCEN0_JTAGEN_MASK); /* Disable JTAGEN */
			val &= (~NFUNCEN0_JTAG_MASK); /* Clear JTAG Function */
			val |= ((FUNCTION2 << NFUNCEN0_JTAG_SHIFT) & NFUNCEN0_JTAG_MASK);
			writel(val, priv->ioaddr + NFUNCEN0_OFFSET);
		}
	} else if (priv->port_num == RM_PF1_ID) {
		if (tc956x_pps_cfg->ppsout_ch == 0) {  /* PPO10 */
			val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
			val &= (~NFUNCEN4_GPIO4_MASK); /* GPIO4 */
			val |= ((FUNCTION1 << NFUNCEN4_GPIO4_SHIFT) & NFUNCEN4_GPIO4_MASK);
			writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
		} else if (tc956x_pps_cfg->ppsout_ch == 1) {  /* PPO11 */
			val = readl(priv->ioaddr + NFUNCEN0_OFFSET);
			val &= (~NFUNCEN0_JTAGEN_MASK); /* Disable JTAGEN */
			val &= (~NFUNCEN0_JTAG_MASK); /* Clear JTAG Function */
			val |= ((FUNCTION2 << NFUNCEN0_JTAG_SHIFT) & NFUNCEN0_JTAG_MASK);
			writel(val, priv->ioaddr + NFUNCEN0_OFFSET);
		}
	}

#ifdef TC956X
	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);
	val |= XGMAC_PPSCMDx(tc956x_pps_cfg->ppsout_ch, ppscmd);
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);
	val &= ~(0x7 << (tc956x_pps_cfg->ppsout_ch * 8));
	val |= (XGMAC_PPSCMD_STOP << (tc956x_pps_cfg->ppsout_ch * 8)); /* stop pulse train immediately */
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);

	sec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR);		/* PTP seconds */
	subsec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STNSR);	/* PTP subseconds */

	/* second roll over */
	if (readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR) != sec) {
		sec  = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STSR);	/* PTP seconds */
		subsec = readl(priv->ioaddr + PTP_XGMAC_OFFSET + PTP_STNSR);/* PTP subseconds */
	}
	DBGPR_FUNC(priv->device, "sec: %x\n", sec);
	DBGPR_FUNC(priv->device, "subsec: %x\n", subsec);
	if (tc956x_pps_cfg->ppsout_align == 1) {
		subsec += PPS_START_DELAY;
		if (subsec >= align_ns) {
			/* s  += 1; */
			DBGPR_FUNC(priv->device, "sec: %x\n", sec);
		}
		sec += 2;
		DBGPR_FUNC(priv->device, "align_ns: %x\n", align_ns);
		/*  PPS target sec */
		writel(sec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_SEC(tc956x_pps_cfg->ppsout_ch));
		/* PPS target nsec */
		writel(align_ns, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(tc956x_pps_cfg->ppsout_ch));
	} else {
		subsec += PPS_START_DELAY;
		if (subsec >= 1000000000) {
			subsec -= 1000000000;
			sec += 1;
		}
		/* set subsecond */
		/* PPS target sec */
		writel(sec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_SEC(tc956x_pps_cfg->ppsout_ch));
		/* PPS target nsec */
		writel(subsec, priv->ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(tc956x_pps_cfg->ppsout_ch));
	}

	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);
	val &= ~GENMASK(((tc956x_pps_cfg->ppsout_ch + 1) * 8) - 1, tc956x_pps_cfg->ppsout_ch * 8);
	val |= (XGMAC_PPSCMD_START << (tc956x_pps_cfg->ppsout_ch * 8)) | 0x10;	/* PPSEN0 */
	val |= XGMAC_TRGTMODSELx(tc956x_pps_cfg->ppsout_ch, trgtmodsel);/* pulse output only */
	writel(val, priv->ioaddr + XGMAC_PPS_CONTROL);

	val = readl(priv->ioaddr + XGMAC_PPS_CONTROL);

#ifdef VERIFY_PPO_USING_AUX
	/* Enable Aux_control (bit 4) */
	val = readl(priv->ioaddr + XGMAC_MAC_AUX_CTRL);
	val |= (0x1 << 4);	/* set bit 4 */
	val |= (0x1 << 0);	/* set bit 0 */
	writel(val, priv->ioaddr + XGMAC_MAC_AUX_CTRL);

	val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
	val |= (0x1 << 4);	/* set bit 4 GPIO1 TRIG00 */
	writel(val, priv->ioaddr + NFUNCEN4_OFFSET);

	/*  enable the trig interrupt */
	val = readl(priv->ioaddr + XGMAC_INT_EN);
	val &= ~(0x1 << 5);	/* clear bit 5 to disable LPI interrupt */
	val |= (0x1 << 12);	/* set bit 12req=50000000Hz */
	writel(val, priv->ioaddr + XGMAC_INT_EN);
#endif
#endif /* TC956X */

	return 0;
}

/*!
 * \brief API to configure pps
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_set_ppsout(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_PPS_Config *tc956x_pps_cfg, tc956x_pps_cfg_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&tc956x_pps_cfg_data, data, sizeof(struct tc956xmac_PPS_Config)))
		return -EFAULT;

	tc956x_pps_cfg = (struct tc956xmac_PPS_Config *)(&tc956x_pps_cfg_data);

	if (tc956x_pps_cfg->ppsout_duty <= 0) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: duty cycle is invalid. Using duty=1\n");
		tc956x_pps_cfg->ppsout_duty = 1;
		return -EFAULT;
	} else if (tc956x_pps_cfg->ppsout_duty >= 100) {
		DBGPR_FUNC(priv->device, "PPS: PPSOut_Config: duty cycle is invalid. Using duty=99\n");
		tc956x_pps_cfg->ppsout_duty = 99;
		return -EFAULT;
	}

	if ((tc956x_pps_cfg->ppsout_ch != 0) && (tc956x_pps_cfg->ppsout_ch != 1)) {
		DBGPR_FUNC(priv->device, "pps channel %d not supported by GPIO\n", tc956x_pps_cfg->ppsout_ch);
		return -EFAULT;
	}

	return(pps_configuration(priv, tc956x_pps_cfg));
}

/*!
 * \brief API to configure PTP clock
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * ptp_clock should be > 3.921568 MHz
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_ptp_clk_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_PPS_Config *tc956x_pps_cfg, tc956x_pps_cfg_data;
	int ret = 0;
	u32 value = 0;
	__u64 temp = 0;
	__u32 sec_inc;
	struct timespec64 now;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&tc956x_pps_cfg_data, data, sizeof(struct tc956xmac_PPS_Config))) {
		DBGPR_FUNC(priv->device, "copy_from_user error: ifr data structure\n");
		return -EFAULT;
	}

	tc956x_pps_cfg = (struct tc956xmac_PPS_Config *)(&tc956x_pps_cfg_data);

	/*  Update minimum functional ptpclk_freq */
	if ((tc956x_pps_cfg->ptpclk_freq == 0)
	|| (tc956x_pps_cfg->ptpclk_freq > 250000000)) {
		DBGPR_FUNC(priv->device, "PPS: Invalid PTPCLK_Config: freq=%dHz\n",
			tc956x_pps_cfg->ptpclk_freq);
		return -1;
	}

#ifdef TC956X
	tc956xmac_config_sub_second_increment(priv,
				priv->ptpaddr, tc956x_pps_cfg->ptpclk_freq,
				priv->plat->has_xgmac,
				&sec_inc);
#endif

	DBGPR_FUNC(priv->device, "sec_inc : %x , tc956x_pps_cfg->ptpclk_freq :%d\n", sec_inc, tc956x_pps_cfg->ptpclk_freq);
	temp = div_u64(1000000000ULL, sec_inc);
	temp = (u64)(temp << 32);
	priv->default_addend = div_u64(temp, TC956X_PTP_SYSCLOCK);

	DBGPR_FUNC(priv->device, "priv->default_addend : %x\n", priv->default_addend);
	/*  Add handling of config_addend return */
	tc956xmac_config_addend(priv, priv->ptpaddr, priv->default_addend);

	value = readl(priv->ptpaddr + PTP_TCR);
	/* Note : TSENA never disabled. */
	if (!(value & 0x00000001)) {
		DBGPR_FUNC(priv->device, "tc956x_xgmac_ptp_clk_config : init systime\n");
		/* initialize system time */
		ktime_get_real_ts64(&now);
		tc956xmac_init_systime(priv, priv->ptpaddr, (u32)now.tv_sec, now.tv_nsec);
		value |= PTP_TCR_TSINIT | PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSCTRLSSR;
		tc956xmac_config_hw_tstamping(priv, priv->ptpaddr, value);
	}

	return ret;
}

/*!
 * \brief API to configure ptp offloading feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_config_ptpoffload(struct tc956xmac_priv *priv, void __user *data)
{
	u32 pto_cntrl;
	u32 varMAC_TCR;
	struct tc956x_config_ptpoffloading ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if ((ioctl_data.domain_num < 0U)
	|| (ioctl_data.domain_num > 0xFFU)) {
		return -EINVAL;
	}

	pto_cntrl = XGMAC_PTOEN;	/* enable ptp offloading */

	varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
	varMAC_TCR &= ~PTP_TCR_MODE_MASK;

	if (ioctl_data.mode == TC956X_PTP_ORDINARY_SLAVE) {
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		priv->ptp_offloading_mode = TC956X_PTP_ORDINARY_SLAVE;
	} else if (ioctl_data.mode == TC956X_PTP_TRASPARENT_SLAVE) {
		pto_cntrl |= XGMAC_APDREQEN;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR = varMAC_TCR & (~PTP_GMAC4_TCR_SNAPTYPSEL_1);
		varMAC_TCR |= PTP_TCR_SNAPTYPSEL_1;
		priv->ptp_offloading_mode = TC956X_PTP_TRASPARENT_SLAVE;
	} else if (ioctl_data.mode == TC956X_PTP_ORDINARY_MASTER) {
		pto_cntrl |= XGMAC_ASYNCEN;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR |= PTP_TCR_TSMSTRENA;
		priv->ptp_offloading_mode = TC956X_PTP_ORDINARY_MASTER;
	} else if (ioctl_data.mode == TC956X_PTP_TRASPARENT_MASTER) {
		pto_cntrl |= XGMAC_ASYNCEN | XGMAC_APDREQEN;
		varMAC_TCR = varMAC_TCR & (~PTP_GMAC4_TCR_SNAPTYPSEL_1);
		varMAC_TCR |= PTP_TCR_SNAPTYPSEL_1;
		varMAC_TCR |= PTP_TCR_TSEVNTENA;
		varMAC_TCR |= PTP_TCR_TSMSTRENA;
		priv->ptp_offloading_mode = TC956X_PTP_TRASPARENT_MASTER;
	} else if (ioctl_data.mode == TC956X_PTP_PEER_TO_PEER_TRANSPARENT) {
		pto_cntrl |= XGMAC_APDREQEN;
		varMAC_TCR |= (3 << PTP_TCR_SNAPTYPSEL_1_LPOS);
		priv->ptp_offloading_mode = TC956X_PTP_PEER_TO_PEER_TRANSPARENT;
	} else
		return -EINVAL;

	priv->ptp_offload = 1;
	pto_cntrl |= (ioctl_data.domain_num << 8);

	if ((ioctl_data.en_dis != TC956X_PTP_OFFLOADING_DISABLE) && (ioctl_data.mc_uc == 1))
		tc956x_add_mac_addr(priv->dev, ioctl_data.mc_uc_addr);

	if (ioctl_data.en_dis == TC956X_PTP_OFFLOADING_DISABLE) {
		pto_cntrl = 0;
		varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
		priv->ptp_offload = 0;
	}

	writel(varMAC_TCR, priv->ptpaddr + PTP_TCR);


#ifdef TC956X
	writel(pto_cntrl, priv->ioaddr + XGMAC_PTO_CTRL);
#endif
	varMAC_TCR = readl(priv->ptpaddr + PTP_TCR);
	varMAC_TCR &= (~PTP_TCR_TSENMACADDR);
	varMAC_TCR |= ((ioctl_data.mc_uc & 0x1) << PTP_TCR_MC_UC_LPOS);
	writel(varMAC_TCR, priv->ptpaddr + PTP_TCR);

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	return 0;
}
#endif

#ifndef TC956X_SRIOV_VF

/*!
 * \brief API to configure to enable auxiliary timestamp feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specific structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_aux_timestamp_enable(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_aux_snapshot ioctl_data;
	u32 aux_cntrl_en, val;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	aux_cntrl_en = readl(priv->ioaddr + XGMAC_MAC_AUX_CTRL);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (ioctl_data.aux_snapshot_ctrl > 1)
		return -EFAULT;

	if ((ioctl_data.aux_snapshot_ctrl & TC956X_AUX_SNAPSHOT_0))
		aux_cntrl_en |= XGMAC_ATSEN0;
	else
		aux_cntrl_en &= ~XGMAC_ATSEN0;

	/* Auxiliary timestamp FIFO clear */
	aux_cntrl_en |= XGMAC_ATSFC;

	writel(aux_cntrl_en, priv->ioaddr + XGMAC_MAC_AUX_CTRL);

	if (priv->port_num == RM_PF0_ID) {
		val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
		val &= ~TRIG00_MASK;
		val |= (0x1 << TRIG00_SHIFT);	/* set bit 4 GPIO1 TRIG00 */
		writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
	}
	if (priv->port_num == RM_PF1_ID) {
		val = readl(priv->ioaddr + NFUNCEN4_OFFSET);
		val &= ~TRIG10_MASK;
		val |= (0x1 << TRIG10_SHIFT);	/* set bit 12 GPIO3 TRIG10 */
		writel(val, priv->ioaddr + NFUNCEN4_OFFSET);
	}

	/* Enable the Trig interrupt */
	val = readl(priv->ioaddr + XGMAC_INT_EN);
	val |= (0x1 << TSIE_SHIFT); /* set bit 12req=50000000Hz */
	writel(val, priv->ioaddr + XGMAC_INT_EN);

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	return 0;

}

/*!
 * \brief API to configure to enable/disable onestep timestamp feature
 * \param[in] tc956xmac priv structure
 * \param[in] An IOCTL specefic structure, that can contain a data pointer
 * \return 0 (success) or Error value (fail)
 */
static int tc956xmac_config_onestep_timestamp(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_config_ost ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if ((priv->dma_cap.osten) && (ioctl_data.en_dis == 1))
		priv->ost_en = 1;
	else
		priv->ost_en = 0;

	return 0;

}

static int tc956xmac_sa_vlan_ins_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_sa_ins_cfg ioctl_data;
	u32 reg_data;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (priv->dma_cap.vlins) {
		switch (ioctl_data.cmd) {
		case TC956XMAC_SA0_VLAN_INS_REP_REG:
			priv->sa_vlan_ins_via_reg = ioctl_data.control_flag;
			if (ioctl_data.control_flag == TC956XMAC_SA0_NONE)
				memcpy(priv->ins_mac_addr, priv->dev->dev_addr, ETH_ALEN);
			else
				memcpy(priv->ins_mac_addr, ioctl_data.mac_addr, ETH_ALEN);

#ifdef TC956X
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
			spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

			tc956xmac_set_umac_addr(priv, priv->hw, priv->ins_mac_addr, 0, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
			spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

			reg_data = readl(priv->ioaddr + XGMAC_TX_CONFIG);
			reg_data = (reg_data & (~XGMAC_CONFIG_SARC)) |
				   (priv->sa_vlan_ins_via_reg << XGMAC_CONFIG_SARC_SHIFT);
			writel(reg_data, priv->ioaddr + XGMAC_TX_CONFIG);
#endif

			NMSGPR_ALERT(priv->device, "SA will use MAC0 with register for configuration %d\n", priv->sa_vlan_ins_via_reg);
			break;
		case TC956XMAC_SA1_VLAN_INS_REP_REG:
			priv->sa_vlan_ins_via_reg = ioctl_data.control_flag;
			if (ioctl_data.control_flag == TC956XMAC_SA1_NONE)
				memcpy(priv->ins_mac_addr, priv->dev->dev_addr, ETH_ALEN);
			else
				memcpy(priv->ins_mac_addr, ioctl_data.mac_addr, ETH_ALEN);

#ifdef TC956X
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
			spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
			tc956xmac_set_umac_addr(priv, priv->hw, priv->ins_mac_addr, 1, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
			spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

			reg_data = readl(priv->ioaddr + XGMAC_TX_CONFIG);
			reg_data = (reg_data & (~XGMAC_CONFIG_SARC)) |
				   (priv->sa_vlan_ins_via_reg << XGMAC_CONFIG_SARC_SHIFT);
			writel(reg_data, priv->ioaddr + XGMAC_TX_CONFIG);
#endif
			NMSGPR_ALERT(priv->device, "SA will use MAC1 with register for configuration %d\n", priv->sa_vlan_ins_via_reg);
			break;
		default:
			return -EINVAL;
		}
	} else {
		NMSGPR_ALERT(priv->device, "Device doesn't supports SA Insertion/Replacement\n");
		return -EINVAL;
	}

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

#endif /* TC956X_SRIOV_VF */
static int tc956xmac_get_tx_qcnt(struct tc956xmac_priv *priv, void __user *data)
{
	u32 tx_qcnt = 0;
	struct tc956xmac_ioctl_tx_qcnt ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	tx_qcnt = priv->plat->tx_queues_to_use;
	if (copy_to_user((void __user *)ioctl_data.ptr, &tx_qcnt, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

static int tc956xmac_get_rx_qcnt(struct tc956xmac_priv *priv, void __user *data)
{
	u32 rx_qcnt = 0;
	struct tc956xmac_ioctl_rx_qcnt ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	rx_qcnt = priv->plat->rx_queues_to_use;
	if (copy_to_user((void __user *)ioctl_data.ptr, &rx_qcnt, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/*!
 * \brief API to read pcie conf register
 * \param[in] address offset as per tc956x data-sheet
 * \return -EFAULT in case of error, otherwise 0
 * \description register can be read for pcie conf between 0 to 0xFFF.
 */
static int tc956xmac_pcie_config_reg_rd(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_pcie_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	pci_read_config_dword(priv->plat->pdev, ioctl_data.addr, &val);

	if (copy_to_user((void __user *)ioctl_data.ptr, &val, sizeof(unsigned int)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/*!
 * \brief API to write pcie conf register
 * \param[in] address offset as per tc956x data-sheet
 * \return -EFAULT in case of error, otherwise 0
 * \description register can be written for pcie conf between 0 to 0xFFF.
 */
static int tc956xmac_pcie_config_reg_wr(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_pcie_reg_rd_wr ioctl_data;
	u32 val;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (copy_from_user(&val, (const void __user *) ioctl_data.ptr, sizeof(unsigned int)))
		return -EFAULT;

	pci_write_config_dword(priv->plat->pdev, ioctl_data.addr, val);

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

#ifndef TC956X_SRIOV_VF
/*!
 * \brief IOCTL to enable/disable VLAN Rx Stripping
 * \param[in] priv driver private structure
 * \param[in] data user data
 * \return -EFAULT in case of error, otherwise 0
 * \description enable/disable stripping.
 * Enable this function only if NETIF_F_HW_VLAN_CTAG_RX is user-configureable.
 */
static int tc956xmac_vlan_strip_config(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956xmac_ioctl_vlan_strip_cfg ioctl_data;
	u32 reg_data;
	struct net_device *dev = priv->dev;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (ioctl_data.enabled == 1) {
		/* Enable VLAN Stripping */
#ifdef TC956X
		reg_data = readl(priv->ioaddr + XGMAC_VLAN_TAG);
		reg_data &= ~XGMAC_VLAN_EVLS;
		reg_data |= 0x3 << XGMAC_VLAN_EVLS_SHIFT;
		dev->features |= NETIF_F_HW_VLAN_CTAG_RX;
		writel(reg_data, priv->ioaddr + XGMAC_VLAN_TAG);
#endif
	} else {
		/* Disable VLAN Stripping */
#ifdef TC956X
		reg_data = readl(priv->ioaddr + XGMAC_VLAN_TAG);
		reg_data &= ~XGMAC_VLAN_EVLS;
		dev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
		writel(reg_data, priv->ioaddr + XGMAC_VLAN_TAG);
#endif
	}
	netdev_features_change(dev);
	return 0;
}

#ifdef TC956X
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_mode1_usp_lane_change_4_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x4 --> x1) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_4_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set phy_core_reg_access_enable Register in Glue Logic Register to select
	 * PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);


	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE0 Lane 1 PMA Lane Register
	 * to select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_POWER_CTL);


	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 1 PMA Lane Register
	 * to select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register
	 * to select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register
	 * to select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}


static int tc956xmac_mode1_usp_lane_change_4_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x4 --> x2) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_4_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode1_usp_lane_change_1_2_to_4(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 2/3 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register to
	 * select Lane 2/3 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 2/3 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1/x2 --> x4) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_1_2_TO_4, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int tc956xmac_mode2_usp_lane_change_2_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x2 --> x1) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_2_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE0 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode2_usp_lane_change_1_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 0 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_0_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 0 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE0_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_USP_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1 --> x2) to PCIe Switch USP
	 */
	writel(USP_LINK_WIDTH_CHANGE_1_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int tc956xmac_mode2_dsp1_lane_change_2_to_1(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Clear SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x2 --> x1) to PCIe Switch DSP1
	 */
	writel(DSP1_LINK_WIDTH_CHANGE_2_TO_1, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

	/*Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= (LANE_1_ENABLE & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/*Set pcs2pma_power_ctrl Register in PHY PHY_CORE 1 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_POWER_CTL);
	reg_data &= ~POWER_CTL_MASK;
	reg_data |= (POWER_CTL_LOW_POWER_ENABLE & POWER_CTL_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_POWER_CTL);

	/* Set pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 1 PMA Lane Register to
	 * select Lane 1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	writel((reg_data | POWER_CTL_OVER_ENABLE), ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	return 0;
}

static int tc956xmac_mode2_dsp1_lane_change_1_to_2(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);


	/* Set phy_core_reg_access_enable Register in Glue Logic Register to
	 * select PHY_CORE 1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
	reg_data &= ~PHY_CORE_ENABLE_MASK;
	reg_data |= (PHY_CORE_1_ENABLE & PHY_CORE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


	/* Set lane_access_enable Register in PHY PHY_CORE 1 PCS Global Register to
	 * select Lane 0/1 to access by APB I/F
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);
	reg_data &= ~LANE_ENABLE_MASK;
	reg_data |= ((LANE_1_ENABLE | LANE_0_ENABLE) & LANE_ENABLE_MASK);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_GL_LANE_ACCESS);

	/* Clear pcs2pma_power_ctrl_ovren Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register to
	 * select Lane 0/1 to set Low Power mode
	 */
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);
	reg_data &= (~POWER_CTL_OVER_ENABLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_OVEREN_POWER_CTL);

	/* Clear Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/*Clear Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/*Clear Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data &= (~PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Set Rate 0 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R0);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 1 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R1);

	/* Wait for 100us */
	udelay(100);

	/*Set Rate 2 pc_debug_p1_toggle Register in PHY PHY_CORE 1 Lane 0/1 PMA Lane Register*/
	reg_data = readl(ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);
	reg_data |= (PC_DEBUG_P1_TOGGLE);
	writel(reg_data, ioaddr + TC956X_PHY_CORE1_MISC_RW0_R2);

	/* Wait for 100us */
	udelay(100);

	/* Clear SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 */
	writel(0, ioaddr + TC956X_GLUE_RSVD_RW0);

	/* Set SW_DSP1_TL_PM_BWCHANGE Register in Glue Logic Register rsvd_rw0 to
	 * direct Link Width Change (x1 --> x2) to PCIe Switch DSP1
	 */
	writel(DSP1_LINK_WIDTH_CHANGE_1_TO_2, ioaddr + TC956X_GLUE_RSVD_RW0);


	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int tc956xmac_pcie_lane_change(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_lane_change ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	enum lane_width usp_curr_lane_width, dsp1_curr_lane_width;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Lane change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	/* Get the current lane setting from glue lane monitor register */
	reg_data = readl(priv->ioaddr + TC956X_GLUE_TL_NUM_LANES_MON);
	usp_curr_lane_width = (reg_data & USP_LANE_WIDTH_MASK);
	dsp1_curr_lane_width = ((reg_data & DSP1_LANE_WIDTH_MASK) >> DSP1_LANE_WIDTH_SHIFT);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		/* Only USP port lane can be changed in this setting, others return error */
		if (ioctl_data.port != UPSTREAM_PORT) {
			KPRINT_INFO("Lane change requested for unsupported port\n\r");
			return -EINVAL;
		}

		/* If target link width is same as current width, return 0*/
		if (usp_curr_lane_width == ioctl_data.target_lane_width) {
			KPRINT_INFO("Already port is in requested lane. No change required\n\r");
			return 0;
		}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
		/* All lane width change from x4 to x1, x4 to x2, x1/x2 to x4 are allowed */
		if (usp_curr_lane_width == LANE_4 && ioctl_data.target_lane_width == LANE_1)
			return tc956xmac_mode1_usp_lane_change_4_to_1(priv, ioaddr);
		else if (usp_curr_lane_width == LANE_4 && ioctl_data.target_lane_width == LANE_2)
			return tc956xmac_mode1_usp_lane_change_4_to_2(priv, ioaddr);
		else if (ioctl_data.target_lane_width == LANE_4) /* x1/x2 to x4*/
			return tc956xmac_mode1_usp_lane_change_1_2_to_4(priv, ioaddr);
		else
			return -EINVAL;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		/* Only USP and DSP1 port lane can be changed in this setting, others return error */
		if ((ioctl_data.port != UPSTREAM_PORT) &&
			(ioctl_data.port != DOWNSTREAM_PORT1)) {
			KPRINT_INFO("Lane change requested for unsupported port\n\r");
			return -EINVAL;
		}

		/* If target link width is same as current width, return 0*/
		if (ioctl_data.port == UPSTREAM_PORT) {
			if (usp_curr_lane_width == ioctl_data.target_lane_width) {
				KPRINT_INFO("Already port is in requested lane. No change required\n\r");
				return 0;
			}

			if (usp_curr_lane_width == LANE_2 && ioctl_data.target_lane_width == LANE_1)
				return tc956xmac_mode2_usp_lane_change_2_to_1(priv, ioaddr);
			else if (usp_curr_lane_width == LANE_1 && ioctl_data.target_lane_width == LANE_2)
				return tc956xmac_mode2_usp_lane_change_1_to_2(priv, ioaddr);
			else
				return -EINVAL;
		}

		if (ioctl_data.port == DOWNSTREAM_PORT1) {
			if (dsp1_curr_lane_width == ioctl_data.target_lane_width)
				return 0;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
			if (dsp1_curr_lane_width == LANE_2 && ioctl_data.target_lane_width == LANE_1)
				return tc956xmac_mode2_dsp1_lane_change_2_to_1(priv, ioaddr);
			else if (dsp1_curr_lane_width == LANE_1 && ioctl_data.target_lane_width == LANE_2)
				return tc956xmac_mode2_dsp1_lane_change_1_to_2(priv, ioaddr);
			else
				return -EINVAL;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
		}

	break;

	default:
	break;

	}

	return 0;
}


static int tc956xmac_pcie_set_tx_margin(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_tx_margin ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		default:
			return -EINVAL;
		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}
	writel((ioctl_data.txmargin & 0x1FF),
			ioaddr + TC956X_PHY_COREX_PCS_GL_MD_CFG_TXMARGIN0);

	return 0;
}



static int tc956xmac_set_gen3_deemphasis(struct tc956xmac_priv *priv, void __iomem *ioaddr,
						__u8 txpreset, __u8 enable)
{
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (enable) {
		/*Write Gen1 De-emphasis setting (as of now no configuration through this IOCTL) */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R0);
		writel(0x0E, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R0);

		/*Write Gen2 De-emphasis setting (as of now no configuration through this IOCTL) */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R1);
		writel(0x14, ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R1);

		/*Write Gen3 De-emphasis setting */
		writel(tx_demphasis_setting[txpreset][0], ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R2);
		writel(tx_demphasis_setting[txpreset][1], ioaddr + TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R2);

		/*Set override enable setting */
		writel(0x1, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_ADV);
		writel(0x1, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_DLY);
	} else {
		/*Clear override enable setting */
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_ADV);
		writel(0x0, ioaddr + TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_DLY);
	}


	return 0;
}

static int tc956xmac_dfe_read_and_write_ctle_optimised(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 reg_data;
	u8 eqc_force, eq_res, vga_ctrl;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Read the optimized CTLE values from correct phy core
	 *(selection should be done before calling this function)
	 */
	reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_RESULT_BEST_MON0);
	eqc_force = ((reg_data & 0x780) >> 0x7);
	eq_res = ((reg_data & 0x78) >> 0x3);
	vga_ctrl = (reg_data & 0x7);

	/* Write back the optimized CTLE values from correct phy core
	 * ((selection should be done before calling this function)
	 */
	reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
	reg_data &= ~0xFF7;
	reg_data |= (eqc_force << 0x8 | eq_res << 0x4 | vga_ctrl);
	writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

	return 0;
}

static int tc956xmac_dfe_disable_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 phy_core)
{

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Set DFE Disable settings (1)*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG0_R2);

	/*Set DFE Disable settings (2)*/
	writel(0x411, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG1_R2);

	/*Set DFE Disable settings (3)*/
	writel(0x11, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ1_CFG0_R2);

	/*Set DFE Disable settings (4)*/
	writel(0x11, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ2_CFG0_R2);

	/*Set DFE Disable settings (5)*/
	if (phy_core == PHY_CORE_1_ENABLE)
		writel(0x7, ioaddr + TC956X_PHY_CORE1_PMACNT_GL_PM_DFE_PD_CTRL);
	else /*For all other phy core write at offset 0x254*/
		writel(0x7, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_DFE_PD_CTRL);

	return 0;
}

static int tc956xmac_dfe_enable_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 phy_core)
{

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Set DFE Enable settings (1)*/
	writel(0x3, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG0_R2);

	/*Set DFE Enable settings (2)*/
	writel(0x2111, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ1_CFG0_R2);

	/*Set DFE Enable settings (3)*/
	writel(0x2111, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ2_CFG0_R2);

	/*Set DFE Enable settings (4)*/
	if (phy_core == PHY_CORE_1_ENABLE)
		writel(0x3, ioaddr + TC956X_PHY_CORE1_PMACNT_GL_PM_DFE_PD_CTRL);
	else /*For all other phy core write at offset 0x254*/
		writel(0x3, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_DFE_PD_CTRL);

	return 0;
}

static int tc956xmac_dfe_pll_reset_settings(struct tc956xmac_priv *priv, void __iomem *ioaddr, u8 dfe_enable_flag)
{
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/*Enable PHY Rate Change Override mode*/
	writel(0x10, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT0);

	if (dfe_enable_flag)
		/*Set DFE Enable settings (5)*/
		writel(0x211, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG1_R2);

	/*Execute PHY Rate Change*/
	writel(0x21, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT4);

	/*Wait for PHY Rate Change done*/
	mdelay(1);

	/*Clear PHY Rate Change reqeust*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT4);

	/*Disable PHY Rate Change Override mode*/
	writel(0x0, ioaddr + TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT0);

	return 0;
}

static int tc956xmac_pcie_set_tx_deemphasis(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_tx_deemphasis ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);


		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);


		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		case DOWNSTREAM_PORT1:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/* Set lane_access_enable Register in PHY PHY_CORE 0 PCS Global Register to
			 * select Lane 0/1 to access by APB I/F
			 */
			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		case DOWNSTREAM_PORT2:
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_set_gen3_deemphasis(priv, ioaddr, ioctl_data.txpreset, ioctl_data.enable);

		break;
		default:
			return -EINVAL;
		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_set_dfe(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_dfe ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) { /*DFE Disable settings*/

				/*Phy core 0, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 0, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 0 CTLE copysettings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core0 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*Disable DFE settings for Phy core1 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings for phy core 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/

				/*Phy core 0/1, Lane 0/1 CTLE initial value register settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


				/*Enable DFE settings for Phy core0 */
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);


				/*Enable DFE settings for Phy core1 */
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);


				/*PLL reset settings for Phy core0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			if (ioctl_data.enable == 0) { /*DFE Disable settings*/
				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core2*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				/*Enable DFE settings for Phy core2*/
				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			}
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			if (ioctl_data.enable == 0) { /*DFE Disable settings*/
				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);

				/*Disable DFE settings for Phy core3*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core3*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else {  /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				/*Enable DFE settings for Phy core2*/
				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core2*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			}

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) {//Disable setting
				/*Phy core 0, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 0, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core0 and Lane 0/1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*Enable settings*/

				/*Phy core 0, Lane 0/1 CTLE initial value register settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			if (ioctl_data.enable == 0) {//Disable setting
				/*Phy core 1, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_0_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Phy core 1, Lane 1 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= LANE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core1*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings for Phy core1*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				/*Phy core 1, Lane 0 CTLE copy settings*/
				reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
				reg_data &= ~PHY_CORE_ENABLE_MASK;
				reg_data |= PHY_CORE_1_ENABLE;
				writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

				reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
				reg_data &= ~LANE_ENABLE_MASK;
				reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
				writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			if (ioctl_data.enable == 0) {//Disable setting

				tc956xmac_dfe_read_and_write_ctle_optimised(priv, ioaddr);


				/*Disable DFE settings for Phy core3*/
				tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings for Phy core3*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);
			} else { /*DFE Enable settings*/
				writel(0x000008A5, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

				tc956xmac_dfe_enable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

				/*PLL reset settings*/
				tc956xmac_dfe_pll_reset_settings(priv, ioaddr, ioctl_data.enable);

			}
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_set_ctle_fixed(struct tc956xmac_priv *priv, void __user *data)
{
	struct tc956x_ioctl_pcie_set_ctle_fixed_mode ioctl_data;
	u32 reg_data;
	u32 pcie_mode;
	void __iomem *ioaddr;

	ioaddr = priv->ioaddr;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	/* Return error if the interface where lane change request sent is not Port 0 */
	if (priv->port_num != RM_PF0_ID) {
		KPRINT_INFO("Tx Margin change requested through wrong interface\n\r");
		return -EINVAL;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	/* Read mode setting register
	 * Mode settings values 0:Setting A: x4x1x1, 1:Setting B: x2x2x1
	 */
	reg_data = readl(priv->ioaddr + NMODESTS_OFFSET);
	pcie_mode = (reg_data & NMODESTS_MODE2) >> NMODESTS_MODE2_SHIFT;

	KPRINT_INFO("Pcie mode: %d\n\r", pcie_mode);

	switch (pcie_mode) {
	case TC956X_PCIE_SETTING_A: /* 0:Setting A: x4x1x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 0, Lane 0/1 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


			/*Phy core 1, Lane 0/1 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);


			/*Disable DFE settings for Phy core0 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

			/*Disable DFE settings for Phy core1 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

			/*PLL reset settings for phy core 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= (PHY_CORE_0_ENABLE | PHY_CORE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_2_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);


			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core2*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_2_ENABLE);

			/*PLL reset settings for Phy core2*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core3*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

			/*PLL reset settings for Phy core3*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		default:
			return -EINVAL;

		}

	break;
	case TC956X_PCIE_SETTING_B: /* 1:Setting B: x2x2x1 mode */
		switch (ioctl_data.port) {
		case UPSTREAM_PORT:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF) != 0x3) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 0, Lane 0 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);


			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core0 and Lane 0/1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_0_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_0_ENABLE);

			/*PLL reset settings done with disable flag*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);

		break;
		case DOWNSTREAM_PORT1:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF00) != 0x300) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			/*Phy core 1, Lane 0 */
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core1*/
			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_1_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			reg_data = readl(ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);
			reg_data &= ~LANE_ENABLE_MASK;
			reg_data |= (LANE_0_ENABLE | LANE_1_ENABLE);
			writel(reg_data, ioaddr + TC956X_PHY_CORE0_GL_LANE_ACCESS);

			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_1_ENABLE);

			/*PLL reset settings for Phy core1*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		case DOWNSTREAM_PORT2:
			/*Device should be in Gen3 when we execute this command*/
			reg_data = readl(ioaddr + TC956X_GLUE_TL_LINK_SPEED_MON);
			if ((reg_data & 0xF0000) != 0x30000) {
				KPRINT_INFO("Device not in Gen3 to execute DFE disable\n\r");
				return -EINVAL;
			}

			reg_data = readl(ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);
			reg_data &= ~PHY_CORE_ENABLE_MASK;
			reg_data |= PHY_CORE_3_ENABLE;
			writel(reg_data, ioaddr + TC956X_GLUE_PHY_REG_ACCESS_CTRL);

			/*Set CTLE initial value registers*/
			reg_data = readl(ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);
			reg_data &= ~0xFF7;
			reg_data |= ((ioctl_data.eqc_force & 0xF) << 0x8 |
				     (ioctl_data.eq_res & 0xF) << 0x4 |
				     (ioctl_data.vga_ctrl & 0x7));
			writel(reg_data, ioaddr + TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2);

			/*Disable DFE settings for Phy core3*/
			tc956xmac_dfe_disable_settings(priv, ioaddr, PHY_CORE_3_ENABLE);

			/*PLL reset settings for Phy core3*/
			tc956xmac_dfe_pll_reset_settings(priv, ioaddr, 0);
		break;
		default:
			return -EINVAL;

		}
	break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int tc956xmac_pcie_speed_change(struct tc956xmac_priv *priv, void __user *data)
{
	struct pci_dev *pdev = to_pci_dev(priv->device);
	struct tc956x_ioctl_pcie_set_speed ioctl_data;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	return tc956x_set_pci_speed(pdev, (u32)ioctl_data.speed);
}

#endif /*#define TC956X*/
#endif /*#ifdef TC956X_SRIOV_VF*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static int tc956xmac_siocdevprivate(struct net_device *dev, struct ifreq *rq,
			       void __user *data, int cmd)
#else
static int tc956xmac_extension_ioctl(struct tc956xmac_priv *priv,
				     void __user *data)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	struct tc956xmac_priv *priv = netdev_priv(dev);
#else
	u32 cmd;
#endif
	DBGPR_FUNC(priv->device, "-->%s\n", __func__);
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (copy_from_user(&cmd, data, sizeof(cmd)))
		return -EFAULT;

	switch (cmd) {
	case TC956XMAC_GET_CBS:
#ifdef TC956X_SRIOV_PF
	{
		int ret;
		struct tc956xmac_ioctl_cbs_cfg cbs;

		if (copy_from_user(&cbs, data, sizeof(cbs)))
			return -EFAULT;

		ret = tc956xmac_ioctl_get_cbs(priv, &cbs);

		if (ret == 0) {
			if (copy_to_user(data, &cbs, sizeof(cbs)))
				return -EFAULT;
		}

		return ret;
	}
#elif (defined TC956X_SRIOV_VF)
		return tc956xmac_get_cbs(priv, data);
#endif
	case TC956XMAC_SET_CBS:
#ifdef TC956X_SRIOV_PF
	{
		struct tc956xmac_ioctl_cbs_cfg cbs;

		if (copy_from_user(&cbs, data, sizeof(cbs)))
			return -EFAULT;

		return tc956xmac_ioctl_set_cbs(priv, &cbs);
	}
#elif (defined TC956X_SRIOV_VF)
		return tc956xmac_set_cbs(priv, data);
#endif
	case TC956XMAC_GET_EST:
#ifdef TC956X_SRIOV_PF
	{
		int ret;
		struct tc956xmac_ioctl_est_cfg *est;

		est = kzalloc(sizeof(*est), GFP_KERNEL);
		if (!est)
		return -ENOMEM;

		ret = tc956xmac_ioctl_get_est(priv, est);
		if (ret == 0) {
			if (copy_to_user(data, est, sizeof(*est))) {
				kfree(est);
				return -EFAULT;
			}
		}
		kfree(est);
		return ret;
	}
#elif (defined TC956X_SRIOV_VF)
	return tc956xmac_get_est(priv, data);
#endif
	case TC956XMAC_SET_EST:
#ifdef TC956X_SRIOV_PF
	{
		struct tc956xmac_ioctl_est_cfg *est;

		est = kzalloc(sizeof(*est), GFP_KERNEL);
		if (!est)
			return -ENOMEM;

		if (copy_from_user(est, data, sizeof(*est))) {
			kfree(est);
			return -EFAULT;
		}
		return tc956xmac_ioctl_set_est(priv, est);
	}
#elif (defined TC956X_SRIOV_VF)
	return tc956xmac_set_est(priv, data);
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED
	case TC956XMAC_GET_FPE: /*Function to Get FPE related configurations*/
#ifdef TC956X_SRIOV_PF
	{
		int ret;
		struct tc956xmac_ioctl_fpe_cfg *fpe;

		fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);
		if (!fpe)
			return -ENOMEM;

		ret = tc956xmac_ioctl_get_fpe(priv, fpe);
		if (ret == 0) {
			if (copy_to_user(data, fpe, sizeof(*fpe))) {
				kfree(fpe);
				return -EFAULT;
			}
		}
		kfree(fpe);
		return ret;
	}
#elif (defined TC956X_SRIOV_VF)
	return tc956xmac_get_fpe(priv, data);
#endif
	case TC956XMAC_SET_FPE: /*Function to Set FPE related configurations*/
#ifdef TC956X_SRIOV_PF
	{
		struct tc956xmac_ioctl_fpe_cfg *fpe;

		fpe = kzalloc(sizeof(*fpe), GFP_KERNEL);
		if (!fpe)
			return -ENOMEM;

		if (copy_from_user(fpe, data, sizeof(*fpe))) {
			kfree(fpe);
			return -EFAULT;
		}
		return tc956xmac_ioctl_set_fpe(priv, fpe);
	}
#elif (defined TC956X_SRIOV_VF)
		return tc956xmac_set_fpe(priv, data);
#endif
#endif /* TC956X_UNSUPPORTED_UNTESTED */

#ifdef TC956X_SRIOV_PF
	case TC956X_GET_FW_STATUS:
		return tc956x_xgmac_get_fw_status(priv, data);
	case TC956XMAC_VLAN_FILTERING:
		return tc956xmac_config_vlan_filter(priv, data);
#endif
	case TC956XMAC_GET_RXP:
#ifdef TC956X_SRIOV_PF
	{
		int ret;
		struct tc956xmac_ioctl_rxp_cfg *rxp;

		rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
		if (!rxp)
			return -ENOMEM;

		ret = tc956xmac_ioctl_get_rxp(priv, rxp);
		if (ret == 0) {
			if (copy_to_user(data, rxp, sizeof(*rxp))) {
				kfree(rxp);
				return -EFAULT;
			}
		}
		kfree(rxp);
		return ret;
	}
#elif (defined TC956X_SRIOV_VF)
		return tc956xmac_get_rxp(priv, data);
#endif
	case TC956XMAC_SET_RXP:
#ifdef TC956X_SRIOV_PF
	{
		struct tc956xmac_ioctl_rxp_cfg *rxp;

		rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
		if (!rxp)
			return -ENOMEM;

		if (copy_from_user(rxp, data, sizeof(*rxp))) {
			kfree(rxp);
			return -EFAULT;
		}
		return tc956xmac_ioctl_set_rxp(priv, rxp);
	}
#elif (defined TC956X_SRIOV_VF)
		return tc956xmac_set_rxp(priv, data);
#endif
	case TC956XMAC_GET_SPEED:
#ifdef TC956X_SRIOV_PF
		return tc956xmac_ioctl_get_connected_speed(priv, data);
#elif defined TC956X_SRIOV_VF
		return tc956xmac_get_speed(priv, data);
#endif
	case TC956XMAC_GET_TX_FREE_DESC:
		return tc956xmac_ioctl_get_tx_free_desc(priv, data);
#ifdef TC956X_IOCTL_REG_RD_WR_ENABLE
	case TC956XMAC_REG_RD:
		return tc956xmac_reg_rd(priv, data);
	case TC956XMAC_REG_WR:
#ifdef TC956X_SRIOV_PF
		return tc956xmac_reg_wr(priv, data);
#elif defined TC956X_SRIOV_VF
		return tc956xmac_reg_wr(priv, data);
#endif
#endif /* TC956X_IOCTL_REG_RD_WR_ENABLE */
#ifndef TC956X_SRIOV_VF
	case TC956XMAC_SET_MAC_LOOPBACK:
		return tc956xmac_ioctl_set_mac_loopback(priv, data);
	case TC956XMAC_SET_PHY_LOOPBACK:
		return tc956xmac_ioctl_set_phy_loopback(priv, data);
	case TC956XMAC_L2_DA_FILTERING_CMD:
		return tc956xmac_config_l2_da_filter(priv, data);
	case TC956XMAC_SET_PPS_OUT:
		return tc956xmac_set_ppsout(priv, data);
	case TC956XMAC_PTPCLK_CONFIG:
		return tc956xmac_ptp_clk_config(priv, data);
	case TC956XMAC_SA0_VLAN_INS_REP_REG:
		return tc956xmac_sa_vlan_ins_config(priv, data);
	case TC956XMAC_SA1_VLAN_INS_REP_REG:
		return tc956xmac_sa_vlan_ins_config(priv, data);
#endif /* TC956X_SRIOV_VF */
	case TC956XMAC_GET_TX_QCNT:
		return tc956xmac_get_tx_qcnt(priv, data);
	case TC956XMAC_GET_RX_QCNT:
		return tc956xmac_get_rx_qcnt(priv, data);
	case TC956XMAC_PCIE_CONFIG_REG_RD:
		return tc956xmac_pcie_config_reg_rd(priv, data);
	case TC956XMAC_PCIE_CONFIG_REG_WR:
		return tc956xmac_pcie_config_reg_wr(priv, data);
#ifdef TC956X_SRIOV_PF
	case TC956XMAC_PTPOFFLOADING:
		return tc956xmac_config_ptpoffload(priv, data);
#endif /* TC956X_SRIOV_PF */
	case TC956XMAC_ENABLE_AUX_TIMESTAMP:
#ifdef TC956X_SRIOV_PF
		return tc956xmac_aux_timestamp_enable(priv, data);
#endif
#ifdef TC956X_SRIOV_PF
	case TC956XMAC_ENABLE_ONESTEP_TIMESTAMP:
		return tc956xmac_config_onestep_timestamp(priv, data);
#endif /* TC956X_SRIOV_PF */

#ifndef TC956X_SRIOV_VF
#ifdef TC956X_PCIE_LOGSTAT
	case TC956X_PCIE_STATE_LOG_SUMMARY:
		return tc956x_pcie_ioctl_state_log_summary(priv, data);
	case TC956X_PCIE_GET_PCIE_LINK_PARAMS:
		return tc956x_pcie_ioctl_get_pcie_link_params(priv, data);
	case TC956X_PCIE_STATE_LOG_ENABLE:
		return tc956x_pcie_ioctl_state_log_enable(priv, data);
#endif /* #ifdef TC956X_PCIE_LOGSTAT */
	case TC956XMAC_VLAN_STRIP_CONFIG:
		return tc956xmac_vlan_strip_config(priv, data);
#ifdef TC956X
	case TC956XMAC_PCIE_LANE_CHANGE:
		return tc956xmac_pcie_lane_change(priv, data);
	case TC956XMAC_PCIE_SET_TX_MARGIN:
		return tc956xmac_pcie_set_tx_margin(priv, data);
	case TC956XMAC_PCIE_SET_TX_DEEMPHASIS:
		return tc956xmac_pcie_set_tx_deemphasis(priv, data);
	case TC956XMAC_PCIE_SET_DFE:
		return tc956xmac_pcie_set_dfe(priv, data);
	case TC956XMAC_PCIE_SET_CTLE:
		return tc956xmac_pcie_set_ctle_fixed(priv, data);
	case TC956XMAC_PCIE_SPEED_CHANGE:
		return tc956xmac_pcie_speed_change(priv, data);
#endif /* TC956X */
#endif

	default:
		return -EINVAL;
	}

	return 0;
}

#ifndef TC956X_SRIOV_VF
static int tc956xmac_phy_fw_flash_mdio_ioctl(struct net_device *ndev,
					     struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *mii = if_mii(ifr);
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	int ret = -EINVAL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0))
	int prtad, devad;

	if (mdio_phy_id_is_c45(mii->phy_id)) {
		prtad = mdio_phy_id_prtad(mii->phy_id);
		devad = mdio_phy_id_devad(mii->phy_id);
		devad = MII_ADDR_C45 | devad << 16 | mii->reg_num;
	} else {
		prtad = mii->phy_id;
		devad = mii->reg_num;
	}
#endif
	switch (cmd) {
	case SIOCGMIIPHY:
		mii->phy_id = 0;
		fallthrough;

	case SIOCGMIIREG:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0))
		ret = priv->mii->read(priv->mii, prtad, devad);
#else
		ret = phylink_mii_ioctl(priv->phylink, ifr, cmd);
#endif
		if (ret >= 0) {
			mii->val_out = ret;
			ret = 0;
		}
		break;

	case SIOCSMIIREG:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0))
		ret = priv->mii->write(priv->mii, prtad, devad,
					mii->val_in);
#else
		ret = phylink_mii_ioctl(priv->phylink, ifr, cmd);
#endif
		break;

	default:
		break;
	}

	return ret;
}
#endif

/**
 *  tc956xmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently it supports the phy_mii_ioctl(...) and HW time stamping.
 */
static int tc956xmac_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

#ifdef TC956X_SRIOV_VF
	u8 mbx[MBX_TOT_SIZE];

	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
#endif
	struct mii_ioctl_data *data = if_mii(rq);

	if (!netif_running(dev))
#ifndef TC956X_SRIOV_VF
		return tc956xmac_phy_fw_flash_mdio_ioctl(dev, rq, cmd);
#else
		return -EINVAL;
#endif

	switch (cmd) {
	case SIOCGMIIPHY:
#ifndef TC956X_SRIOV_VF
		data->phy_id = priv->plat->phy_addr;
		NMSGPR_ALERT(priv->device, "PHY ID: SIOCGMIIPHY\n");
#elif (defined TC956X_SRIOV_VF)
		mbx[0] = OPCODE_MBX_VF_IOCTL; //opcode for ioctl
		mbx[1] = 1; //size
		mbx[2] = OPCODE_MBX_VF_GET_MII_PHY;//cmd for SIOCGMIIPHY

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFE)
		 */
		if (ret > 0) {
			if (ret == ACK) {
				/* Check the acknowledgement message for opcode and size,
				 * then read the data from the ACK message
				 */
				if ((mbx[4] == OPCODE_MBX_ACK_MSG && mbx[5] == 4)) {
					memcpy(&data->phy_id, &mbx[6], 4);
					NMSGPR_ALERT(priv->device, "PHY ID: SIOCGMIIPHY\n");
				}
			}
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d %x %x", ret, mbx[4], mbx[5]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
		}
#endif
		ret = 0;
		break;
#ifndef TC956X_SRIOV_VF
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		ret = phylink_mii_ioctl(priv->phylink, rq, cmd);
		break;
#elif defined TC956X_SRIOV_VF
	case SIOCGMIIREG:
		mbx[0] = OPCODE_MBX_VF_IOCTL; //opcode for ioctl
		mbx[1] = SIZE_MBX_VF_GET_MII_REG; //size
		mbx[2] = OPCODE_MBX_VF_GET_MII_REG_1;//cmd for SIOCGMIIREG

		memcpy(&mbx[3], &(data->phy_id), 2);
		memcpy(&mbx[5], &(data->reg_num), 2);
		memcpy(&mbx[7], &(data->val_in), 2);

		memset(&mbx[9], 0, sizeof(rq->ifr_ifrn.ifrn_name));
		memcpy(&mbx[9], &rq->ifr_ifrn.ifrn_name, sizeof(rq->ifr_ifrn.ifrn_name));
		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFF)
		 */
		if (ret > 0) {
			if (ret == ACK) {
				KPRINT_DEBUG2("mailbox write with ACK or NACK %d %x %x", ret, mbx[4], mbx[5]);

				mbx[0] = OPCODE_MBX_VF_IOCTL; //opcode for ioctl
				mbx[1] = 1; //size
				mbx[2] = OPCODE_MBX_VF_GET_MII_REG_2;

				ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
				if (ret > 0) {
					KPRINT_DEBUG2("mailbox write with ACK or NACK %d %x %x", ret, mbx[4], mbx[5]);

					if (ret == ACK) {
						/* Check the acknowledgement message for opcode and size,
						 * then read the data from the ACK message
						 */
						if ((mbx[4] == OPCODE_MBX_ACK_MSG && mbx[5] == 2))
							memcpy(&(data->val_out), &mbx[6], 2);
					}
				} else
					KPRINT_DEBUG2("mailbox write failed");
			}
		} else {
			KPRINT_DEBUG2("mailbox write failed");
		}
		ret = 0;
		break;
#endif
#ifndef TC956X_SRIOV_VF
	case SIOCSHWTSTAMP:
		ret = tc956xmac_hwtstamp_set(dev, rq);
		break;
#endif
	case SIOCGHWTSTAMP:
#ifndef TC956X_SRIOV_VF
		ret = tc956xmac_hwtstamp_get(dev, rq);
#else
		mbx[0] = OPCODE_MBX_VF_IOCTL; //opcode for ioctl
		mbx[1] = 1; //size
		mbx[2] = OPCODE_MBX_VF_GET_HW_STMP;//cmd for SIOCGHWTSTAMP

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFF)
		 */
		if (ret > 0) {
			if (ret == ACK) {
				/* Check the acknowledgement message for opcode and size,
				 * then read the data from the ACK message
				 */
				if ((mbx[4] == OPCODE_MBX_ACK_MSG && mbx[5] == SIZE_MBX_GET_HW_STMP)) {
					if (copy_to_user(rq->ifr_data, &mbx[6], SIZE_MBX_GET_HW_STMP))
						ret = -EFAULT;
				}
			}
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d %x %x", ret, mbx[4], mbx[5]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
		}
#endif
		break;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	case SIOCSTIOCTL:
		if (!priv || !rq)
			return -EINVAL;
		ret = tc956xmac_extension_ioctl(priv, rq->ifr_data);
		break;
#endif
	default:
		break;
	}

	return ret;
}

static int tc956xmac_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				    void *cb_priv)
{
	struct tc956xmac_priv *priv = cb_priv;
	int ret = -EOPNOTSUPP;

	if (!tc_cls_can_offload_and_chain0(priv->dev, type_data))
		return ret;

	tc956xmac_disable_all_queues(priv);

	switch (type) {
	case TC_SETUP_CLSU32:
		ret = tc956xmac_tc_setup_cls_u32(priv, type_data);
		break;
	case TC_SETUP_CLSFLOWER:
		ret = tc956xmac_tc_setup_cls(priv, type_data);
		break;
	default:
		break;
	}

	tc956xmac_enable_all_queues(priv);
	return ret;
}

static LIST_HEAD(tc956xmac_block_cb_list);

static int tc956xmac_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			   void *type_data)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &tc956xmac_block_cb_list,
						  tc956xmac_setup_tc_block_cb,
						  priv, priv, true);
	case TC_SETUP_QDISC_CBS:
		return tc956xmac_tc_setup_cbs(priv, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return tc956xmac_tc_setup_taprio(priv, type_data);
	case TC_SETUP_QDISC_ETF:
		return tc956xmac_tc_setup_etf(priv, type_data);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16))
	case TC_QUERY_CAPS:
		return tc956xmac_tc_setup_query_cap(priv, type_data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

static u16 tc956xmac_select_queue(struct net_device *dev, struct sk_buff *skb,
			       struct net_device *sb_dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifdef TC956X
	u16 txqueue_select;
	unsigned int eth_or_vlan_tag;
	unsigned int eth_type;
	unsigned int avb_priority;

	/* TX Channel assignment based on Vlan tag and protocol type */
	eth_or_vlan_tag = htons(((skb->data[13]<<8) | skb->data[12]));
	if (eth_or_vlan_tag == ETH_P_8021Q) {
		eth_type = htons(((skb->data[17]<<8) | skb->data[16]));
		if (eth_type == ETH_P_TSN) {
			/* Extract VLAN priority field from the tx data */
			avb_priority = htons((skb->data[15]<<8) | skb->data[14]);

			avb_priority >>= 13;
#ifndef TC956X_SRIOV_VF
			if (avb_priority == TC956X_AVB_PRIORITY_CLASS_A)
				txqueue_select = AVB_CLASS_A_TX_CH;
			else if (avb_priority == TC956X_AVB_PRIORITY_CLASS_B)
				txqueue_select = AVB_CLASS_B_TX_CH;
			else if (avb_priority == TC956X_PRIORITY_CLASS_CDT)
				txqueue_select = TSN_CLASS_CDT_TX_CH;
			else
				txqueue_select = HOST_BEST_EFF_CH;
#elif (defined TC956X_SRIOV_VF)
			if (avb_priority == TC956X_AVB_PRIORITY_CLASS_A)
				txqueue_select = priv->plat->avb_class_a_ch_no;
			else if (avb_priority == TC956X_AVB_PRIORITY_CLASS_B)
				txqueue_select = priv->plat->avb_class_b_ch_no;
			else if (avb_priority == TC956X_PRIORITY_CLASS_CDT)
				txqueue_select = priv->plat->tsn_ch_no;
			else
				txqueue_select = priv->plat->best_effort_ch_no;
#endif
		} else {
#ifndef TC956X_SRIOV_VF
			txqueue_select = LEGACY_VLAN_TAGGED_CH;
#elif (defined TC956X_SRIOV_VF)
			txqueue_select = priv->plat->best_effort_ch_no;
#endif
		}
	} else {
#ifndef TC956X_SRIOV_VF
		switch (eth_or_vlan_tag) {
		case ETH_P_1588:
			txqueue_select = TC956X_GPTP_TX_CH;
			break;
		default:
			txqueue_select = HOST_BEST_EFF_CH;
			break;
		}
#elif (defined TC956X_SRIOV_VF)
		switch (eth_or_vlan_tag) {
		case ETH_P_1588:
			txqueue_select = priv->plat->gptp_ch_no;
			break;
		default:
		txqueue_select = priv->plat->best_effort_ch_no;
			break;
		}
#endif
	}
	netdev_dbg(priv->dev, "%s: Tx Queue select = %d", __func__, txqueue_select);
	return txqueue_select;
#else
	int gso = skb_shinfo(skb)->gso_type;

	if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6 | SKB_GSO_UDP_L4)) {
		/*
		 * There is no way to determine the number of TSO/USO
		 * capable Queues. Let's use always the Queue 0
		 * because if TSO/USO is supported then at least this
		 * one will be capable.
		 */
		return 0;
	}

	return netdev_pick_tx(dev, skb, NULL) % dev->real_num_tx_queues;
#endif
}

static int tc956xmac_set_mac_address(struct net_device *ndev, void *addr)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;

#ifdef TC956X_SRIOV_VF
	tc956xmac_set_umac_addr(priv, ndev->dev_addr, HOST_MAC_ADDR_OFFSET + priv->fn_id_info.vf_no);
#else
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956xmac_set_umac_addr(priv, priv->hw, (unsigned char *)ndev->dev_addr, HOST_MAC_ADDR_OFFSET, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

#endif

	return ret;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *tc956xmac_fs_dir;

#ifdef TC956X_UNSUPPORTED_UNTESETD_FEATURE
static void sysfs_display_ring(void *head, int size, int extend_desc,
			       struct seq_file *seq)
{
	int i;
	struct dma_extended_desc *ep = (struct dma_extended_desc *)head;
	struct dma_desc *p = (struct dma_desc *)head;

	for (i = 0; i < size; i++) {
		if (extend_desc) {
			seq_printf(seq, "%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, (unsigned int)virt_to_phys(ep),
				   le32_to_cpu(ep->basic.des0),
				   le32_to_cpu(ep->basic.des1),
				   le32_to_cpu(ep->basic.des2),
				   le32_to_cpu(ep->basic.des3));
			ep++;
		} else {
			seq_printf(seq, "%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, (unsigned int)virt_to_phys(p),
				   le32_to_cpu(p->des0), le32_to_cpu(p->des1),
				   le32_to_cpu(p->des2), le32_to_cpu(p->des3));
			p++;
		}
		seq_printf(seq, "\n");
	}
}

static int tc956xmac_rings_status_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	if ((dev->flags & IFF_UP) == 0)
		return 0;

	for (queue = 0; queue < rx_count; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];

		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		seq_printf(seq, "RX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_erx,
					   DMA_RX_SIZE, 1, seq);
		} else {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_rx,
					   DMA_RX_SIZE, 0, seq);
		}
	}

	for (queue = 0; queue < tx_count; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;

		seq_printf(seq, "TX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_etx,
					   DMA_TX_SIZE, 1, seq);
		} else if (!(tx_q->tbs & TC956XMAC_TBS_AVAIL)) {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_tx,
					   DMA_TX_SIZE, 0, seq);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tc956xmac_rings_status);

static int tc956xmac_dma_cap_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (!priv->hw_cap_support) {
		seq_printf(seq, "DMA HW features not supported\n");
		return 0;
	}

	seq_printf(seq, "==============================\n");
	seq_printf(seq, "\tDMA HW features\n");
	seq_printf(seq, "==============================\n");

	seq_printf(seq, "\t10/100 Mbps: %s\n",
		   (priv->dma_cap.mbps_10_100) ? "Y" : "N");
	seq_printf(seq, "\t1000 Mbps: %s\n",
		   (priv->dma_cap.mbps_1000) ? "Y" : "N");
	seq_printf(seq, "\tHalf duplex: %s\n",
		   (priv->dma_cap.half_duplex) ? "Y" : "N");
	seq_printf(seq, "\tHash Filter: %s\n",
		   (priv->dma_cap.hash_filter) ? "Y" : "N");
	seq_printf(seq, "\tMultiple MAC address registers: %s\n",
		   (priv->dma_cap.multi_addr) ? "Y" : "N");
	seq_printf(seq, "\tPCS (TBI/SGMII/RTBI PHY interfaces): %s\n",
		   (priv->dma_cap.pcs) ? "Y" : "N");
	seq_printf(seq, "\tSMA (MDIO) Interface: %s\n",
		   (priv->dma_cap.sma_mdio) ? "Y" : "N");
	seq_printf(seq, "\tPMT Remote wake up: %s\n",
		   (priv->dma_cap.pmt_remote_wake_up) ? "Y" : "N");
	seq_printf(seq, "\tPMT Magic Frame: %s\n",
		   (priv->dma_cap.pmt_magic_frame) ? "Y" : "N");
	seq_printf(seq, "\tRMON module: %s\n",
		   (priv->dma_cap.rmon) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2002 Time Stamp: %s\n",
		   (priv->dma_cap.time_stamp) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2008 Advanced Time Stamp: %s\n",
		   (priv->dma_cap.atime_stamp) ? "Y" : "N");
	seq_printf(seq, "\t802.3az - Energy-Efficient Ethernet (EEE): %s\n",
		   (priv->dma_cap.eee) ? "Y" : "N");
	seq_printf(seq, "\tAV features: %s\n", (priv->dma_cap.av) ? "Y" : "N");
	seq_printf(seq, "\tChecksum Offload in TX: %s\n",
		   (priv->dma_cap.tx_coe) ? "Y" : "N");
	if (priv->synopsys_id >= DWMAC_CORE_4_00) {
		seq_printf(seq, "\tIP Checksum Offload in RX: %s\n",
			   (priv->dma_cap.rx_coe) ? "Y" : "N");
	} else {
		seq_printf(seq, "\tIP Checksum Offload (type1) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type1) ? "Y" : "N");
		seq_printf(seq, "\tIP Checksum Offload (type2) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type2) ? "Y" : "N");
	}
	seq_printf(seq, "\tRXFIFO > 2048bytes: %s\n",
		   (priv->dma_cap.rxfifo_over_2048) ? "Y" : "N");
	seq_printf(seq, "\tNumber of Additional RX channel: %d\n",
		   priv->dma_cap.number_rx_channel);
	seq_printf(seq, "\tNumber of Additional TX channel: %d\n",
		   priv->dma_cap.number_tx_channel);
	seq_printf(seq, "\tNumber of Additional RX queues: %d\n",
		   priv->dma_cap.number_rx_queues);
	seq_printf(seq, "\tNumber of Additional TX queues: %d\n",
		   priv->dma_cap.number_tx_queues);
	seq_printf(seq, "\tEnhanced descriptors: %s\n",
		   (priv->dma_cap.enh_desc) ? "Y" : "N");
	seq_printf(seq, "\tTX Fifo Size: %d\n", priv->dma_cap.tx_fifo_size);
	seq_printf(seq, "\tRX Fifo Size: %d\n", priv->dma_cap.rx_fifo_size);
	seq_printf(seq, "\tHash Table Size: %d\n", priv->dma_cap.hash_tb_sz);
	seq_printf(seq, "\tTSO: %s\n", priv->dma_cap.tsoen ? "Y" : "N");
	seq_printf(seq, "\tNumber of PPS Outputs: %d\n",
		   priv->dma_cap.pps_out_num);
	seq_printf(seq, "\tSafety Features: %s\n",
		   priv->dma_cap.asp ? "Y" : "N");
	seq_printf(seq, "\tFlexible RX Parser: %s\n",
		   priv->dma_cap.frpsel ? "Y" : "N");
	seq_printf(seq, "\tEnhanced Addressing: %d\n",
		   priv->dma_cap.addr64);
	seq_printf(seq, "\tReceive Side Scaling: %s\n",
		   priv->dma_cap.rssen ? "Y" : "N");
	seq_printf(seq, "\tVLAN Hash Filtering: %s\n",
		   priv->dma_cap.vlhash ? "Y" : "N");
	seq_printf(seq, "\tSplit Header: %s\n",
		   priv->dma_cap.sphen ? "Y" : "N");
	seq_printf(seq, "\tVLAN TX Insertion: %s\n",
		   priv->dma_cap.vlins ? "Y" : "N");
	seq_printf(seq, "\tDouble VLAN: %s\n",
		   priv->dma_cap.dvlan ? "Y" : "N");
	seq_printf(seq, "\tNumber of L3/L4 Filters: %d\n",
		   priv->dma_cap.l3l4fnum);
	seq_printf(seq, "\tARP Offloading: %s\n",
		   priv->dma_cap.arpoffsel ? "Y" : "N");
	seq_printf(seq, "\tEnhancements to Scheduled Traffic (EST): %s\n",
		   priv->dma_cap.estsel ? "Y" : "N");
	seq_printf(seq, "\tFrame Preemption (FPE): %s\n",
		   priv->dma_cap.fpesel ? "Y" : "N");
	seq_printf(seq, "\tTime-Based Scheduling (TBS): %s\n",
		   priv->dma_cap.tbssel ? "Y" : "N");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tc956xmac_dma_cap);

/* Use network device events to rename debugfs file entries.
 */
static int tc956xmac_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tc956xmac_priv *priv = netdev_priv(dev);

#ifndef TC956X_SRIOV_VF
	if (dev->netdev_ops != &tc956xmac_netdev_ops)
#else
	if (dev->netdev_ops != &tc956xmac_vf_netdev_ops)
#endif
		goto done;

	switch (event) {
	case NETDEV_CHANGENAME:
		if (priv->dbgfs_dir)
			priv->dbgfs_dir = debugfs_rename(tc956xmac_fs_dir,
							 priv->dbgfs_dir,
							 tc956xmac_fs_dir,
							 dev->name);
		break;
	}
done:
	return NOTIFY_DONE;
}

static struct notifier_block tc956xmac_notifier = {
	.notifier_call = tc956xmac_device_event,
};
#endif
static void tc956xmac_init_fs(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	/* Create per netdev entries */
	priv->dbgfs_dir = debugfs_create_dir(dev->name, tc956xmac_fs_dir);

#ifndef TC956X
	/* Entry to report DMA RX/TX rings */
	debugfs_create_file("descriptors_status", 0444, priv->dbgfs_dir, dev,
			    &tc956xmac_rings_status_fops);

	/* Entry to report the DMA HW features */
	debugfs_create_file("dma_cap", 0444, priv->dbgfs_dir, dev,
			    &tc956xmac_dma_cap_fops);

	register_netdevice_notifier(&tc956xmac_notifier);
#endif
}

static void tc956xmac_exit_fs(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifndef TC956X
	unregister_netdevice_notifier(&tc956xmac_notifier);
#endif
	debugfs_remove_recursive(priv->dbgfs_dir);
}
#endif /* CONFIG_DEBUG_FS */

#ifndef TC956X
static u32 tc956xmac_vid_crc32_le(__le16 vid_le)
{
	unsigned char *data = (unsigned char *)&vid_le;
	unsigned char data_byte = 0;
	u32 crc = ~0x0;
	u32 temp = 0;
	int i, bits;

	bits = get_bitmask_order(VLAN_VID_MASK);
	for (i = 0; i < bits; i++) {
		if ((i % 8) == 0)
			data_byte = data[i / 8];

		temp = ((crc & 1) ^ data_byte) & 1;
		crc >>= 1;
		data_byte >>= 1;

		if (temp)
			crc ^= 0xedb88320;
	}

	return crc;
}
#endif

static int tc956xmac_vlan_rx_add_vid(struct net_device *ndev, __be16 proto,
				  u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
#ifndef TC956X_SRIOV_VF
	bool is_double = false;
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	if (be16_to_cpu(proto) == ETH_P_8021AD)
		is_double = true;

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif

	tc956xmac_update_vlan_hash(priv, ndev, is_double, vid, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
	return ret;
#elif (defined TC956X_SRIOV_VF)
	tc956xmac_add_vlan(priv, vid);
	return 0;
#endif
}

static int tc956xmac_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto,
				   u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
#ifndef TC956X_SRIOV_VF
	int ret = 0;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;

	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif
	tc956xmac_delete_vlan(priv, ndev, vid, PF_DRIVER);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
	return ret;
#elif (defined TC956X_SRIOV_VF)
	tc956xmac_delete_vlan(priv, vid);
	return 0;
#endif
}

#ifndef TC956X_SRIOV_VF
static const struct net_device_ops tc956xmac_netdev_ops = {
#elif defined TC956X_SRIOV_VF
static const struct net_device_ops tc956xmac_vf_netdev_ops = {
#endif
	.ndo_open = tc956xmac_open,
	.ndo_start_xmit = tc956xmac_xmit,
	.ndo_stop = tc956xmac_release,
	.ndo_change_mtu = tc956xmac_change_mtu,
	.ndo_fix_features = tc956xmac_fix_features,
	.ndo_set_features = tc956xmac_set_features,
	.ndo_set_rx_mode = tc956xmac_set_rx_mode,
	.ndo_tx_timeout = tc956xmac_tx_timeout,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_eth_ioctl = tc956xmac_ioctl,
	.ndo_siocdevprivate = tc956xmac_siocdevprivate,
#else
	.ndo_do_ioctl = tc956xmac_ioctl,
#endif
	.ndo_setup_tc = tc956xmac_setup_tc,
	.ndo_select_queue = tc956xmac_select_queue,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = tc956xmac_poll_controller,
#endif
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.ndo_set_mac_address = tc956xmac_set_mac_address,
	.ndo_vlan_rx_add_vid = tc956xmac_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = tc956xmac_vlan_rx_kill_vid,
};

#ifdef TC956X_UNSUPPORTED_UNTESTED
static void tc956xmac_reset_subtask(struct tc956xmac_priv *priv)
{
	if (!test_and_clear_bit(TC956XMAC_RESET_REQUESTED, &priv->state))
		return;
	if (test_bit(TC956XMAC_DOWN, &priv->state))
		return;

	netdev_err(priv->dev, "Reset adapter.\n");

	rtnl_lock();
	netif_trans_update(priv->dev);
	while (test_and_set_bit(TC956XMAC_RESETING, &priv->state))
		usleep_range(1000, 2000);

	set_bit(TC956XMAC_DOWN, &priv->state);
	dev_close(priv->dev);
	dev_open(priv->dev, NULL);
	clear_bit(TC956XMAC_DOWN, &priv->state);
	clear_bit(TC956XMAC_RESETING, &priv->state);
	rtnl_unlock();
}

static void tc956xmac_service_task(struct work_struct *work)
{
	struct tc956xmac_priv *priv = container_of(work, struct tc956xmac_priv,
			service_task);

	tc956xmac_reset_subtask(priv);
	clear_bit(TC956XMAC_SERVICE_SCHED, &priv->state);
}
#endif
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
/**
 * tc956xmac_service_mbx_task - Service mailbox task
 * @work: work structure
 */
static void tc956xmac_service_mbx_task(struct work_struct *work)
{
	struct tc956xmac_priv *priv = container_of(work, struct tc956xmac_priv,
			service_mbx_task);
	u32 dma_ch = 0;
	u8 vf;
	int ret = -EBUSY;
	struct mii_ioctl_data *mii_ptr;

	if (priv->mbx_wq_param.fn_id == SCH_WQ_PHY_REG_RD) {
		mii_ptr = if_mii(&priv->mbx_wq_param.rq);
		rtnl_lock();
		ret = phylink_mii_ioctl(priv->phylink, &priv->mbx_wq_param.rq, SIOCGMIIREG);
		rtnl_unlock();
		priv->mbx_wq_param.val_out[priv->mbx_wq_param.vf_no] = mii_ptr->val_out;
		priv->mbx_wq_param.fn_id = 0;
	} else if (priv->mbx_wq_param.fn_id == SCH_WQ_RX_DMA_ERR) {
		for (vf = 0; vf < MAX_NO_OF_VFS; vf++) {
			while (ret == -EBUSY)
				ret = tc956x_mbx_wrap_rx_dma_err(priv, vf);

			if (ret == -1) {
				priv->clear_to_send[vf] = VF_DOWN;
				for (dma_ch = 0; dma_ch < TC956XMAC_CH_MAX; dma_ch++) {
					if (priv->dma_vf_map[dma_ch] == vf)
						tc956xmac_stop_rx_dma(priv, dma_ch);
				}
			}
			ret = -EBUSY;  //to continue with next vf
		}
		/* Invoke device driver close */
		if (netif_running(priv->dev)) {
			rtnl_lock();
			dev_close(priv->dev);
			rtnl_unlock();
		}
		/* Invoke device driver open */
		if (!netif_running(priv->dev)) {
			rtnl_lock();
			dev_open(priv->dev, NULL);
			rtnl_unlock();
		}
		priv->mbx_wq_param.fn_id = 0;
	} else if (priv->mbx_wq_param.fn_id == SCH_WQ_GET_PAUSE_PARAM) {
		rtnl_lock();
		tc956xmac_get_pauseparam(priv->dev, &priv->mbx_wq_param.pause);
		rtnl_unlock();
		priv->mbx_wq_param.fn_id = 0;
	} else {
		if (priv->mbx_wq_param.queue_no == 0 || priv->mbx_wq_param.queue_no == 1 || priv->mbx_wq_param.queue_no == 7) {
			for (dma_ch = 0; dma_ch < 3; dma_ch++) {
				vf = priv->dma_vf_map[dma_ch];
				while (ret == -EBUSY)
					ret = tc956x_mbx_wrap_rx_dma_ch_tlptr(priv, dma_ch, vf);
				ret = -EBUSY;  //to continue with next vf
			}
		} else {
			dma_ch = priv->plat->rx_queues_cfg[priv->mbx_wq_param.queue_no].chan;
			vf = priv->dma_vf_map[dma_ch];
			while (ret == -EBUSY)
				ret = tc956x_mbx_wrap_rx_dma_ch_tlptr(priv, dma_ch, vf);
		}
	}
	clear_bit(TC956XMAC_SERVICE_SCHED, &priv->state);
}
#endif

#ifdef TC956X_SRIOV_VF
static void tc956xmac_mailbox_service_task(struct work_struct *work)
{
	struct tc956xmac_priv *priv = container_of(work, struct tc956xmac_priv, mbx_service_task);
	struct pci_dev *pdev = to_pci_dev(priv->device);

	if (priv->flag == SCH_WQ_PF_FLR) {
		pci_reset_function(pdev);
		priv->flag = 0;
	} else if (priv->flag == SCH_WQ_RX_DMA_ERR) {
		tc956xmac_stop_all_dma(priv);
		priv->flag = 0;
	} else if (priv->flag == SCH_WQ_LINK_STATE_UP) {
		int ret = 0;

		ret = init_dma_desc_rings(priv->dev, GFP_KERNEL);
		if (ret < 0) {
			netdev_err(priv->dev, "%s: DMA descriptors initialization failed\n",
				__func__);
		}

		ret = tc956xmac_hw_setup(priv->dev, true);
		if (ret < 0)
			netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		priv->flag = 0;
	} else {
		/* Indicate change of features required, which invokes ndo_fix_features
		 * where dev->features are updated.
		 */
		rtnl_lock();
		netdev_change_features(priv->dev);
		rtnl_unlock();
	}

	clear_bit(TC956XMAC_SERVICE_SCHED, &priv->state);
}
#endif

/**
 *  tc956xmac_hw_init - Init the MAC device
 *  @priv: driver private structure
 *  Description: this function is to configure the MAC device according to
 *  some platform parameters or the HW capability register. It prepares the
 *  driver to use either ring or chain modes and to setup either enhanced or
 *  normal descriptors.
 */
static int tc956xmac_hw_init(struct tc956xmac_priv *priv)
{
	int ret;

	/* dwmac-sun8i only work in chain mode */
	if (priv->plat->has_sun8i)
		chain_mode = 1;
#ifndef TC956X
	priv->chain_mode = chain_mode;
#endif

	/* Initialize HW Interface */
	ret = tc956xmac_hwif_init(priv);
	if (ret)
		return ret;

	/* Get the HW capability (new GMAC newer than 3.50a) */
	priv->hw_cap_support = tc956xmac_get_hw_features(priv);
	if (priv->hw_cap_support) {
		dev_info(priv->device, "DMA HW capability register supported\n");

		/* We can override some gmac/dma configuration fields: e.g.
		 * enh_desc, tx_coe (e.g. that are passed through the
		 * platform) with the values from the HW capability
		 * register (if supported).
		 */
		priv->plat->enh_desc = priv->dma_cap.enh_desc;
		priv->plat->pmt = priv->dma_cap.pmt_remote_wake_up;
		priv->hw->pmt = priv->plat->pmt;
		if (priv->dma_cap.hash_tb_sz) {
			priv->hw->multicast_filter_bins =
					(BIT(priv->dma_cap.hash_tb_sz) << 5);
			priv->hw->mcast_bits_log2 =
					ilog2(priv->hw->multicast_filter_bins);
		}

		/* TXCOE doesn't work in thresh DMA mode */
		if (priv->plat->force_thresh_dma_mode)
			priv->plat->tx_coe = 0;
		else
			priv->plat->tx_coe = priv->dma_cap.tx_coe;

		/* In case of GMAC4 rx_coe is from HW cap register. */

		priv->plat->rx_coe = priv->dma_cap.rx_coe;

		if (priv->dma_cap.rx_coe_type2)
			priv->plat->rx_coe = TC956XMAC_RX_COE_TYPE2;
		else if (priv->dma_cap.rx_coe_type1)
			priv->plat->rx_coe = TC956XMAC_RX_COE_TYPE1;

	} else {
		dev_info(priv->device, "No HW DMA feature register supported\n");
	}

	if (priv->plat->rx_coe) {
		priv->hw->rx_csum = priv->plat->rx_coe;
		dev_info(priv->device, "RX Checksum Offload Engine supported\n");
		if (priv->synopsys_id < DWMAC_CORE_4_00)
			dev_info(priv->device, "COE Type %d\n", priv->hw->rx_csum);
	}
	if (priv->plat->tx_coe) {
		dev_info(priv->device, "TX Checksum insertion supported\n");
		priv->csum_insertion = 1;	/* Enable Tx Csum Offload */
	}

	if (priv->plat->pmt) {
		dev_info(priv->device, "Wake-Up On Lan supported\n");
		device_set_wakeup_capable(priv->device, 1);
	}

	if (priv->dma_cap.tsoen)
		dev_info(priv->device, "TSO supported\n");

	/* Run HW quirks, if any */
	if (priv->hwif_quirks) {
		ret = priv->hwif_quirks(priv);
		if (ret)
			return ret;
	}

	/* Rx Watchdog is available in the COREs newer than the 3.40.
	 * In some case, for example on bugged HW this feature
	 * has to be disable and this can be done by passing the
	 * riwt_off field from the platform.
	 */
	if (((priv->synopsys_id >= DWMAC_CORE_3_50) ||
	    (priv->plat->has_xgmac)) && (!priv->plat->riwt_off)) {
		priv->use_riwt = 1;
		dev_info(priv->device,
			 "Enable RX Mitigation via HW Watchdog Timer\n");
	}

	return 0;
}

#ifdef TC956X
/*
 * brief This sequence is used to get Tx channel count
 * param[in] count
 * return Success or Failure
 * retval  0 Success
 * retval -1 Failure
 */

static unsigned char tc956x_get_tx_channel_count(struct tc956xmac_resources *res)
{
	unsigned char count;
	unsigned long varMAC_HFR2;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
					MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

	KPRINT_INFO("-->%s\n", __func__);
	varMAC_HFR2 = readl(res->addr + mac_offset_base + XGMAC_HW_FEATURE2_BASE);
	count = ((varMAC_HFR2 & XGMAC_HWFEAT_TXCHCNT) >> XGMAC_HWFEAT_TXCHCNT_SHIFT) + 1;
	KPRINT_INFO("<-- %s\n", __func__);
	return count;
}

/*
 * brief This sequence is used to get Rx channel count
 * param[in] count
 * return Success or Failure
 * retval  0 Success
 * retval -1 Failure
 */

static unsigned char tc956x_get_rx_channel_count(struct tc956xmac_resources *res)
{
	unsigned char count;
	unsigned long varMAC_HFR2;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
						MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

	KPRINT_INFO("--> %s\n", __func__);

	varMAC_HFR2 = readl(res->addr + mac_offset_base + XGMAC_HW_FEATURE2_BASE);
	count = ((varMAC_HFR2 & XGMAC_HWFEAT_RXCHCNT) >> XGMAC_HWFEAT_RXCHCNT_SHIFT) + 1;
	KPRINT_INFO("<-- %s\n", __func__);
	return count;
}
#endif
#ifndef EEPROM_MAC_ADDR

/*!
 * \brief API to validate MAC ID
 *
 * \param[in] char *s - pointer to MAC ID string
 *
 * \return boolean
 *
 * \retval true on success and false on failure.
 */
static bool isMAC(char *s)
{
	int i = 0;

	if (s == NULL)
		return false;

	for (i = 0; i < 17; i++) {
		if (i % 3 != 2 && !isxdigit(s[i]))
			return false;
		if (i % 3 == 2 && s[i] != ':')
			return false;
	}

	return true;
}

/*!
 * \brief API to extract MAC ID from given string
 *
 * \param[in] char *string - pointer to MAC ID string
 *
 * \return None
 */
static void extract_macid(char *string, uint8_t vf_id)
{
	char *token_m = NULL;
	int j = 0;
	int mac_id = 0;

#ifdef TC956X_SRIOV_PF
	static int addr_found;

	/* Extract MAC ID byte by byte */
	token_m = strsep(&string, ":");

	while (token_m != NULL) {
		sscanf(token_m, "%x", &mac_id);
		if (addr_found < TC956X_MAC_ADDR_CNT) {
			dev_addr[addr_found][j++] = mac_id;
			token_m = strsep(&string, ":");
		} else
			break;
	}
	KPRINT_DEBUG1("MAC Addr : %pM\n", &dev_addr[addr_found][0]);
#elif defined TC956X_SRIOV_VF
	static int k;
	int addr_found = 0;

	/* Extract MAC ID byte by byte */
	token_m = strsep(&string, ":");

	while (token_m != NULL) {
		sscanf(token_m, "%x", &mac_id);

		if (addr_found < 2) {
			dev_addr[k][addr_found + vf_id][j++] = mac_id;
			token_m = strsep(&string, ":");
		} else
			break;
	}
	KPRINT_INFO("MAC Addr : %pM\n", &dev_addr[k][addr_found + vf_id][0]);

	k++;
#endif
	addr_found++;
}

/*!
 * \brief API to parse and extract the user configured MAC ID
 *
 * \param[in] file_buf - Pointer to file data buffer
 *
 * \return boolean
 *
 * \return - True on Success and False in failure
 */
static bool lookfor_macid(char *file_buf, uint8_t port_id, uint8_t dev_id)
{
	char *string = NULL, *token_n = NULL, *token_s = NULL, *token_m = NULL;
	char *dev_no = NULL, *port_no = NULL;
	bool status = false;
	int tc956x_device_no = 0;
	int total_valid_addr = 0;

	string = file_buf;

	/* Parse Line-0 */
	token_n = strsep(&string, "\n");
	while (token_n != NULL) {
		/* Check if line is enabled */
		if (token_n[0] != '#') {
			/* Extract the token based space character */
			token_s = strsep(&token_n, " ");
			if (token_s != NULL) {
				if (strncmp(token_s, config_param_list[0]
					.mdio_key, 9) == 0) {
					token_s = strsep(&token_n, " ");
					dev_no = &token_n[6];
					port_no = &token_n[7];
					token_m = strsep(&token_s, ":");
					sscanf(token_m, "%d",
						&tc956x_device_no);
#ifdef TC956X_SRIOV_VF
					if (((tc956x_device_no != mdio_bus_id) &&
						(*port_no != (char)(port_id + 49))) ||
						(*dev_no != (char)(dev_id + 49))) {
#else
					if (((tc956x_device_no != mdio_bus_id) &&
						(*port_no != (char)(port_id + 49))) ||
						(*dev_no != (char)(dev_id + 48))) {
#endif
						token_n = strsep(&string, "\n");
						if (token_n == NULL)
							break;
						continue;
					}
				}
			}

			/* Extract the token based space character */
			token_s = strsep(&token_n, " ");
			if (token_s != NULL) {
				/* Compare if parsed string matches with
				 * key listed in configuration table
				 */
				if (strncmp(token_s, config_param_list[0]
					.mac_key, 6) == 0) {

					KPRINT_INFO("MAC_ID Key is found\n");
					/* Read next word */
					token_s = strsep(&token_n, "\n");
					if (token_s != NULL) {

						/* Check if MAC ID length
						 * and MAC ID is valid
						 */
						if ((isMAC(token_s) == true)
							&& (strlen(token_s) ==
							config_param_list[0]
							.mac_str_len)) {
							/* If user configured
							 * MAC ID is valid,
							 * assign default MAC ID
							 */
							extract_macid(token_s, dev_id);
							total_valid_addr++;

							if (total_valid_addr
								> 1)
								status = true;
						} else {
							KPRINT_INFO
							("Valid Mac not found");
						}
					}
				}
			}
		}
		/* Read next lile */
		token_n = strsep(&string, "\n");

	if (token_n == NULL)
		break;

	}
	return status;
}

/*!
 * \brief Parse the user configuration file for various config
 *
 * \param[in] None
 *
 * \return None
 *
 */
static void parse_config_file(uint8_t port_id, uint8_t dev_id)
{
	void *data = NULL;
	char *cdata;
	int ret, i;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	ret = kernel_read_file_from_path("config.ini", 0, &data, INT_MAX, NULL, READING_POLICY);
#else
	loff_t size;
	ret = kernel_read_file_from_path("config.ini", &data, &size, 1000, READING_POLICY);
#endif
	if (ret < 0) {
		KPRINT_ERR("Mac configuration file not found\n");
		KPRINT_INFO("Using Default MAC Address\n");
		return;
	} else {

		cdata = data;
		/* Parse the file */
		for (i = 0; i < CONFIG_PARAM_NUM; i++) {
			if (strstr((const char *)cdata, config_param_list[i].mdio_key)) {
				KPRINT_DEBUG1("Pattern Match\n");
				if (strncmp(config_param_list[i].mdio_key, "MDIOBUSID", 9) == 0) {
					/* MAC ID Configuration */
					KPRINT_DEBUG1("MAC_ID Configuration\n");
					lookfor_macid(data, port_id, dev_id);
				}
			}
		}
	}

	vfree(data);
	KPRINT_INFO("<--%s", __func__);
}
#endif


#ifdef TC956X_SRIOV_VF
static int validate_rsc_mgr_alloc(struct tc956xmac_priv *priv, struct net_device *ndev)
{
	u8 rsc, i;
	u8 ch_cnt = priv->plat->tx_queues_to_use;
	u8 ret = 0;

	/* Validate Resource allocation for VF */

	tc956xmac_rsc_mng_get_rscs(priv, ndev, &rsc);

	dev_info(priv->device, "VF %d Resource allocation %x\n", priv->plat->vf_id+1, rsc);

	for (i = 0; i < ch_cnt; i++) {
		if (priv->plat->ch_in_use[i])
			if (!((rsc >> i) & TC956X_DMA_CH0_MASK))
				ret = -1;
	}

	return ret;
}
#endif

/**
 * tc956xmac_dvr_probe
 * @device: device pointer
 * @plat_dat: platform data pointer
 * @res: tc956xmac resource pointer
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 * Return:
 * returns 0 on success, otherwise errno.
 */
#ifdef TC956X_SRIOV_PF
int tc956xmac_dvr_probe(struct device *device,
#elif defined TC956X_SRIOV_VF
int tc956xmac_vf_dvr_probe(struct device *device,
#endif
		     struct plat_tc956xmacenet_data *plat_dat,
		     struct tc956xmac_resources *res)
{
	struct net_device *ndev = NULL;
	struct tc956xmac_priv *priv;
	u32 queue, rxq, maxq;
	int i, ret = 0;
	u8 tx_ch_count, rx_ch_count;
	u32 mac_offset_base = res->port_num == RM_PF0_ID ?
					MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

#ifdef EEPROM_MAC_ADDR
	u32 mac_addr;
#endif
#ifndef TC956X_WITHOUT_MDIO
	void *nrst_reg = NULL, *nclk_reg = NULL;
	u32 nrst_val = 0, nclk_val = 0;
#endif
#ifdef TC956X
	KPRINT_INFO("HFR0 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE0_BASE));
	KPRINT_INFO("HFR1 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE1_BASE));
	KPRINT_INFO("HFR2 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE2_BASE));
	KPRINT_INFO("HFR3 Val = 0x%08x", readl(res->addr + mac_offset_base +
							XGMAC_HW_FEATURE3_BASE));


	tx_ch_count = tc956x_get_tx_channel_count(res);
	rx_ch_count = tc956x_get_rx_channel_count(res);

	ndev = devm_alloc_etherdev_mqs(device, sizeof(struct tc956xmac_priv),
				       tx_ch_count, rx_ch_count);
#endif

	if (!ndev) {
		NMSGPR_ALERT(device, "%s:Unable to alloc new net device\n",
				TC956X_RESOURCE_NAME);
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, device);

	priv = netdev_priv(ndev);
	priv->device = device;
	priv->dev = ndev;
	priv->ioaddr = res->addr;

	memset(priv->saved_gpio_config, 0, sizeof(struct tc956x_gpio_config) * (GPIO_12 + 1));

	ret = tc956x_platform_probe(priv, res);
	if (ret) {
		dev_err(priv->device, "Platform probe error %d\n", ret);
		return -EPERM;
	}
#ifdef TC956X
	priv->mac_loopback_mode = 0; /* Disable MAC loopback by default */
	priv->phy_loopback_mode = 0; /* Disable PHY loopback by default */
	priv->tc956x_port_pm_suspend = false; /* By Default Port suspend state set to false */
	priv->link_down_rst= false; /* Disable MAC reset during link down by default */
#endif
	/* ToDo : Firwmware load code here */

	tc956xmac_set_ethtool_ops(ndev);
	priv->pause = pause;
	priv->plat = plat_dat;
#ifdef TC956X_SRIOV_PF
	priv->sriov_enabled = res->sriov_enabled;
#endif
#ifdef TC956X
	priv->tc956x_SFR_pci_base_addr = res->tc956x_SFR_pci_base_addr;
	priv->tc956x_SRAM_pci_base_addr = res->tc956x_SRAM_pci_base_addr;
	priv->tc956x_BRIDGE_CFG_pci_base_addr = res->tc956x_BRIDGE_CFG_pci_base_addr;
#endif
	priv->port_num = res->port_num;
	priv->dev->base_addr = (unsigned long)res->addr;
	if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII)
		priv->is_sgmii_2p5g = true;
	else
		priv->is_sgmii_2p5g = false;
#ifdef TC956X_SRIOV_PF
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	priv->wol_config_enabled = false; /* Disable WOL SGMII 1G, configuration by default */
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
#endif
	priv->dev->irq = res->irq;
	priv->wol_irq = res->wol_irq;
	priv->lpi_irq = res->lpi_irq;
	priv->port_interface = res->port_interface;
	priv->eee_enabled = res->eee_enabled;
	priv->tx_lpi_timer = res->tx_lpi_timer;

#ifdef TC956X_SRIOV_PF
	priv->pm_saved_linkdown_rst = 0;
	priv->pm_saved_linkdown_clk = 0;
	priv->port_link_down = false;
	priv->port_release = false;


	/* DMA Ch allocation for PF & VF */
	priv->rsc_dma_ch_alloc[0] = TC956X_PF_CH_ALLOC;
	priv->rsc_dma_ch_alloc[1] = TC956X_VF0_CH_ALLOC;
	priv->rsc_dma_ch_alloc[2] = TC956X_VF1_CH_ALLOC;
	priv->rsc_dma_ch_alloc[3] = TC956X_VF2_CH_ALLOC;

#if !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	/* SRIOV PF 0/1 Queue configuration
	 * Note:
	 *	1. Channels used in PF/VF configurations are enabled
	 *		in respective drivers.
	 *	2. Rx Queues are configured in PF only.
	 *	3. Tx Queues are configured in respective devices. In case of
	 *		VF, the Queue configuraion happens via mailbox.
	 *	4. Traffic class configuraions are done in respective drivers except for TC0.
	 *		TC0 is configured in PF only as its common for Tx 0,1,2,3
	 *		as per current design.
	 */

	/* Tx & Rx Channel Configuration */
	priv->plat->tx_ch_in_use[0] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[1] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[2] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[3] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[4] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[5] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[6] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[7] = TC956X_DISABLE_CHNL;

	priv->plat->rx_ch_in_use[0] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[1] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[2] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[3] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[4] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[5] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[6] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[7] = TC956X_DISABLE_CHNL;

	/* Tx & Rx Queue Configuration */
	priv->plat->tx_q_in_use[0] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[1] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[2] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[3] = TC956X_ENABLE_QUEUE;
	priv->plat->tx_q_in_use[4] = TC956X_ENABLE_QUEUE;
	priv->plat->tx_q_in_use[5] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[6] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[7] = TC956X_DISABLE_QUEUE;

	priv->plat->rx_q_in_use[0] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[1] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[2] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[3] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[4] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[5] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[6] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[7] = TC956X_ENABLE_QUEUE;
#else
	/* Tx & Rx Channel Configuration */
	priv->plat->tx_ch_in_use[0] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[1] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[2] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[3] = TC956X_DISABLE_CHNL;
	priv->plat->tx_ch_in_use[4] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[5] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[6] = TC956X_ENABLE_CHNL;
	priv->plat->tx_ch_in_use[7] = TC956X_ENABLE_CHNL;

	priv->plat->rx_ch_in_use[0] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[1] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[2] = TC956X_DISABLE_CHNL;
	priv->plat->rx_ch_in_use[3] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[4] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[5] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[6] = TC956X_ENABLE_CHNL;
	priv->plat->rx_ch_in_use[7] = TC956X_ENABLE_CHNL;

	/* Tx & Rx Queue Configuration */
	priv->plat->tx_q_in_use[0] = TC956X_ENABLE_QUEUE;
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	priv->plat->tx_q_in_use[1] = TC956X_ENABLE_QUEUE;
#else
	priv->plat->tx_q_in_use[1] = TC956X_DISABLE_QUEUE;
#endif
#if defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	priv->plat->tx_q_in_use[2] = TC956X_ENABLE_QUEUE;
#else
	priv->plat->tx_q_in_use[2] = TC956X_DISABLE_QUEUE;
#endif
	priv->plat->tx_q_in_use[3] = TC956X_DISABLE_QUEUE;
	priv->plat->tx_q_in_use[4] = TC956X_ENABLE_QUEUE;
	priv->plat->tx_q_in_use[5] = TC956X_ENABLE_QUEUE;
	priv->plat->tx_q_in_use[6] = TC956X_ENABLE_QUEUE;
	priv->plat->tx_q_in_use[7] = TC956X_ENABLE_QUEUE;

	priv->plat->rx_q_in_use[0] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[1] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[2] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[3] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[4] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[5] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[6] = TC956X_ENABLE_QUEUE;
	priv->plat->rx_q_in_use[7] = TC956X_ENABLE_QUEUE;
#endif /* TC956X_AUTOMOTIVE_CONFIG */
#endif /* TC956X_SRIOV_PF */

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	dev_info(priv->device, "Locks enabled for shared resources\n");
	spin_lock_init(&priv->spn_lock.mac_filter);
	spin_lock_init(&priv->spn_lock.vlan_filter);
	spin_lock_init(&priv->spn_lock.est);
	spin_lock_init(&priv->spn_lock.fpe);
	spin_lock_init(&priv->spn_lock.frp);
	spin_lock_init(&priv->spn_lock.cbs);
#endif
#ifndef TC956X_SRIOV_PF
#ifdef TC956X_DMA_OFFLOAD_ENABLE
	priv->client_priv = NULL;
	memset(priv->cm3_tamap, 0, sizeof(struct tc956xmac_cm3_tamap) * MAX_CM3_TAMAP_ENTRIES);
#endif
#endif
#ifdef TC956X
	/* Read mac address from config.ini file */
	++mdio_bus_id;

#ifdef EEPROM_MAC_ADDR

#ifdef TC956X
#ifdef TC956X_SRIOV_VF

	mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
		TC956X_M3_SRAM_EEPROM_MAC_ADDR + (EEPROM_PORT_OFFSET * TC956X_EIGHT) +
		(priv->plat->vf_id * TC956X_SIXTEEN));

	if (mac_addr != 0) {

		dev_addr[priv->port_num][priv->plat->vf_id][0] = (mac_addr & EEPROM_MAC_ADDR_MASK1);
		dev_addr[priv->port_num][priv->plat->vf_id][1] =
						(mac_addr & EEPROM_MAC_ADDR_MASK2) >> TC956X_EIGHT;
		dev_addr[priv->port_num][priv->plat->vf_id][2] =
						(mac_addr & EEPROM_MAC_ADDR_MASK3) >> TC956X_SIXTEEN;
		dev_addr[priv->port_num][priv->plat->vf_id][3] =
						(mac_addr & EEPROM_MAC_ADDR_MASK4) >> TC956X_TWENTY_FOUR;

		mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
			TC956X_M3_SRAM_EEPROM_MAC_ADDR + (EEPROM_PORT_OFFSET * TC956X_EIGHT) +
			(priv->plat->vf_id * TC956X_SIXTEEN) + TC956X_FOUR);

		dev_addr[priv->port_num][priv->plat->vf_id][4] = (mac_addr & EEPROM_MAC_ADDR_MASK1);
		dev_addr[priv->port_num][priv->plat->vf_id][5] =
						(mac_addr & EEPROM_MAC_ADDR_MASK2) >> TC956X_EIGHT;
	}

#else
	mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
			TC956X_M3_SRAM_EEPROM_MAC_ADDR + (priv->port_num * TC956X_EIGHT));

	if (mac_addr != 0) {
		dev_addr[priv->port_num][0] = (mac_addr & EEPROM_MAC_ADDR_MASK1);
		dev_addr[priv->port_num][1] = (mac_addr & EEPROM_MAC_ADDR_MASK2) >> TC956X_EIGHT;
		dev_addr[priv->port_num][2] = (mac_addr & EEPROM_MAC_ADDR_MASK3) >> TC956X_SIXTEEN;
		dev_addr[priv->port_num][3] = (mac_addr & EEPROM_MAC_ADDR_MASK4) >> TC956X_TWENTY_FOUR;

		mac_addr = readl(priv->tc956x_SRAM_pci_base_addr +
				TC956X_M3_SRAM_EEPROM_MAC_ADDR + TC956X_FOUR + (priv->port_num * TC956X_EIGHT));
		dev_addr[priv->port_num][4] = (mac_addr & EEPROM_MAC_ADDR_MASK1);
		dev_addr[priv->port_num][5] = (mac_addr & EEPROM_MAC_ADDR_MASK2) >> TC956X_EIGHT;
	}
#endif /* TC956X_SRIOV_VF */
#endif /* TC956X */

#else
#ifdef TC956X_SRIOV_VF
	/* To be enabled for config.ini parsing */
	parse_config_file(priv->port_num, priv->plat->vf_id);
#else
	/* To be enabled for config.ini parsing */
	parse_config_file(priv->port_num, 0);
#endif

#endif /* EEPROM_MAC_ADDR */
#ifndef TC956X_SRIOV_VF
	res->mac = &dev_addr[tc956xmac_pm_usage_counter][0];

	NMSGPR_INFO(device, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		dev_addr[tc956xmac_pm_usage_counter][0], dev_addr[tc956xmac_pm_usage_counter][1],
		dev_addr[tc956xmac_pm_usage_counter][2], dev_addr[tc956xmac_pm_usage_counter][3],
		dev_addr[tc956xmac_pm_usage_counter][4], dev_addr[tc956xmac_pm_usage_counter][5]);

#elif (defined TC956X_SRIOV_VF)

	res->mac = &dev_addr[priv->port_num][priv->plat->vf_id][0];
	NMSGPR_INFO(device, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		dev_addr[priv->port_num][priv->plat->vf_id][0], dev_addr[priv->port_num][priv->plat->vf_id][1],
		dev_addr[priv->port_num][priv->plat->vf_id][2], dev_addr[priv->port_num][priv->plat->vf_id][3],
		dev_addr[priv->port_num][priv->plat->vf_id][4], dev_addr[priv->port_num][priv->plat->vf_id][5]);
#endif

#endif
	if (!IS_ERR_OR_NULL(res->mac))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
		eth_hw_addr_set(priv->dev, res->mac);
#else
		memcpy(priv->dev->dev_addr, res->mac, ETH_ALEN);
#endif
	dev_set_drvdata(device, priv->dev);

	/* Verify driver arguments */
	tc956xmac_verify_args();

#ifdef TC956X_UNSUPPORTED_UNTESTED
	/* Allocate workqueue */
	priv->wq = create_singlethread_workqueue("tc956xmac_wq");
	if (!priv->wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->service_task, tc956xmac_service_task);
#endif

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	/* Allocate workqueue */
	priv->mbx_wq = create_singlethread_workqueue("tc956xmac_mbx_wq");
	if (!priv->mbx_wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->service_mbx_task, tc956xmac_service_mbx_task);
#endif
#ifdef TC956X_SRIOV_VF
	/* Allocate workqueue */
	priv->mbx_wq = create_singlethread_workqueue("tc956xmac_mailbox_wq");
	if (!priv->mbx_wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->mbx_service_task, tc956xmac_mailbox_service_task);
#endif
	/* Override with kernel parameters if supplied XXX CRS XXX
	 * this needs to have multiple instances
	 */
	if ((phyaddr >= 0) && (phyaddr <= 31))
		priv->plat->phy_addr = phyaddr;

	if (priv->plat->tc956xmac_rst) {
		ret = reset_control_assert(priv->plat->tc956xmac_rst);
		reset_control_deassert(priv->plat->tc956xmac_rst);
		/* Some reset controllers have only reset callback instead of
		 * assert + deassert callbacks pair.
		 */
		if (ret == -ENOTSUPP)
			reset_control_reset(priv->plat->tc956xmac_rst);
	}

	/* Init MAC and get the capabilities */
	ret = tc956xmac_hw_init(priv);
	if (ret)
		goto error_hw_init;
#ifdef TC956X_SRIOV_VF
	ret = validate_rsc_mgr_alloc(priv, ndev);

	if (ret) {
		dev_err(priv->device, "VF %d Channel Resource allocation mismatch\n", priv->plat->vf_id + 1);
		goto error_hw_init;
	}

	tc956xmac_check_ether_addr(priv);
#endif
	/* Configure real RX and TX queues */
#ifndef TC956X_SRIOV_VF
	netif_set_real_num_rx_queues(ndev, priv->plat->rx_queues_to_use);
	netif_set_real_num_tx_queues(ndev, priv->plat->tx_queues_to_use);
	ndev->netdev_ops = &tc956xmac_netdev_ops;
#elif (defined TC956X_SRIOV_VF)
	netif_set_real_num_rx_queues(ndev, priv->plat->rx_queues_to_use_actual);
	netif_set_real_num_tx_queues(ndev, priv->plat->tx_queues_to_use_actual);
	ndev->netdev_ops = &tc956xmac_vf_netdev_ops;
#endif

#ifdef TC956X_SRIOV_VF
	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
#else //CPE_DRV
	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				NETIF_F_RXCSUM;
#endif

	ret = tc956xmac_tc_init(priv, NULL);
	if (!ret)
		ndev->hw_features |= NETIF_F_HW_TC;

	/* Enable TSO module if any Queue TSO is Enabled */
	for (queue = 0; queue < MTL_MAX_TX_QUEUES; queue++) {
#ifdef TC956X_SRIOV_VF
		if (priv->plat->tx_queues_cfg[0].tso_en == TC956X_ENABLE &&
								priv->tso)
#else
		if (priv->plat->tx_queues_cfg[0].tso_en == TC956X_ENABLE)
#endif
			priv->plat->tso_en = 1;
	}

#ifndef TC956X_SRIOV_VF
	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		if (priv->plat->has_gmac4)
			ndev->hw_features |= NETIF_F_GSO_UDP_L4;
		priv->tso = true;
		/* Update driver cap to let VF know about feature
		 * enable/disable
		 */
		priv->pf_drv_cap.tso_en = true;
		dev_info(priv->device, "TSO feature enabled\n");
	} else {
		priv->tso = false;
		/* Update driver cap to let VF know about feature
		 * enable/disable
		 */
		priv->pf_drv_cap.tso_en = false;
	}
#endif

	if (priv->dma_cap.sphen && priv->plat->sph_en) {
		ndev->hw_features |= NETIF_F_GRO;
		priv->sph = true;
		dev_info(priv->device, "SPH feature enabled\n");
	}

	if (priv->dma_cap.addr64) {
		ret = dma_set_mask_and_coherent(device,
				DMA_BIT_MASK(priv->dma_cap.addr64));
		if (!ret) {
			dev_info(priv->device, "Using %d bits DMA width\n",
				 priv->dma_cap.addr64);
		} else {
			ret = dma_set_mask_and_coherent(device,
					DMA_BIT_MASK(32));
			if (ret) {
				dev_err(priv->device, "Failed to set\
						DMA Mask\n");
				goto error_hw_init;
			}

			priv->dma_cap.addr64 = 32;
		}
	}
	/*
	 * Enable enhanced addressing mode in all configurations to support
	 * mapping 0x10 XXXX XXXX TC956X address space to host.
	 */
	priv->plat->dma_cfg->eame = true;

	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;
#ifdef TC956X_SRIOV_VF
	/* Get the VF function id information */

	if (tc956xmac_rsc_mng_get_fn_id(priv, priv->tc956x_BRIDGE_CFG_pci_base_addr,
					&priv->fn_id_info)) {
		netdev_err(priv->dev,
				"%s: Wrong function id for the driver (error: %d)\n",
				__func__, -ENODEV);
		return -ENODEV;
	}

	tc956xmac_mbx_init(priv, NULL);

	/* Get PF driver capabilities */
	tc956x_get_drv_cap(priv, priv);
	KPRINT_DEBUG1("priv->pf_drv_cap.jumbo_en = %d\n", priv->pf_drv_cap.jumbo_en);

	/* These features will be modified based on PF configuration and mailbox
	 * request from PF */
	if (priv->pf_drv_cap.csum_en) {
		ndev->features |= NETIF_F_RXCSUM;
		ndev->hw_features |= NETIF_F_RXCSUM;
	}

	if (priv->pf_drv_cap.crc_en) {
		ndev->features |= NETIF_F_RXFCS;
		ndev->hw_features |= NETIF_F_RXFCS;
	}

	if (priv->pf_drv_cap.tso_en) {
		if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
			ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
			ndev->features |= NETIF_F_TSO | NETIF_F_TSO6;
			if (priv->plat->has_gmac4) {
				ndev->hw_features |= NETIF_F_GSO_UDP_L4;
				ndev->features |= NETIF_F_GSO_UDP_L4;
			}
			priv->tso = true;
			dev_info(priv->device, "TSO feature enabled\n");
		} else {
			priv->tso = false;
		}
	}
#else
	/* Ethtool rx-fcs state is Off by default */
	ndev->hw_features |= NETIF_F_RXFCS;
#endif
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
#ifdef TC956XMAC_VLAN_TAG_USED
	/* Both mac100 and gmac support receive VLAN tag detection */
	/* Driver only supports CTAG */
	ndev->features &= ~NETIF_F_HW_VLAN_CTAG_RX; /* Disable rx-vlan-offload by default */
	ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX; /* Support as User-changeable features */
#ifndef TC956X
	ndev->features |= NETIF_F_HW_VLAN_STAG_RX;
#endif
	if (priv->dma_cap.vlhash) {
		/* Driver only supports CTAG */
		ndev->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER; /* Disable rx-vlan-filter by default */
		ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER; /* Support as User-changeable features */
#ifndef TC956X
		ndev->features |= NETIF_F_HW_VLAN_STAG_FILTER;
#endif
	}
	if (priv->dma_cap.vlins) {
		ndev->features |= NETIF_F_HW_VLAN_CTAG_TX; /* Enable tx-vlan-offload by default */
		ndev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX; /* Support as User-changeable features */
#ifndef TC956X
		if (priv->dma_cap.dvlan)
			ndev->features |= NETIF_F_HW_VLAN_STAG_TX;
#endif
	}
#endif
	priv->mac_table =
		kcalloc(TC956X_MAX_PERFECT_ADDRESSES - XGMAC_ADDR_ADD_SKIP_OFST,
			sizeof(struct tc956x_mac_addr), GFP_KERNEL);

	if (!priv->mac_table)
		goto error_hw_init;

	priv->vlan_table =
		kcalloc(TC956X_MAX_PERFECT_VLAN, sizeof(struct tc956x_vlan_id), GFP_KERNEL);

	if (!priv->vlan_table) {
		kfree(priv->mac_table);
		goto error_hw_init;
	}
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	/* Initialize RSS */
	rxq = priv->plat->rx_queues_to_use;
	netdev_rss_key_fill(priv->rss.key, sizeof(priv->rss.key));
	for (i = 0; i < ARRAY_SIZE(priv->rss.table); i++)
		priv->rss.table[i] = ethtool_rxfh_indir_default(i, rxq);

	if (priv->dma_cap.rssen && priv->plat->rss_en)
		ndev->features |= NETIF_F_RXHASH;

	/* MTU range: 46 - hw-specific max */
	ndev->min_mtu = ETH_ZLEN - ETH_HLEN;
#ifdef TC956X_SRIOV_VF
	/* Set MTU size based on Jumbo Frame feature enable */
	if (priv->pf_drv_cap.jumbo_en) {
#endif
	if (priv->plat->has_xgmac)
		ndev->max_mtu = XGMAC_JUMBO_LEN;
	else if ((priv->plat->enh_desc) || (priv->synopsys_id >= DWMAC_CORE_4_00))
		ndev->max_mtu = JUMBO_LEN;
	else
		ndev->max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);
#ifdef TC956X_SRIOV_VF
	} else
		ndev->max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);
#endif
	/* Will not overwrite ndev->max_mtu if plat->maxmtu > ndev->max_mtu
	 * as well as plat->maxmtu < ndev->min_mtu which is a invalid range.
	 */
	if ((priv->plat->maxmtu < ndev->max_mtu) &&
	    (priv->plat->maxmtu >= ndev->min_mtu))
		ndev->max_mtu = priv->plat->maxmtu;
	else if (priv->plat->maxmtu < ndev->min_mtu)
		dev_warn(priv->device,
			 "%s: warning: maxmtu having invalid value (%d)\n",
			 __func__, priv->plat->maxmtu);

	netdev_dbg(priv->dev, "%s: MAX MTU size = %d", __func__, ndev->max_mtu);

	if (flow_ctrl)
		priv->flow_ctrl = FLOW_AUTO;/* RX/TX pause on */

	/* Setup channels NAPI */
	maxq = max(priv->plat->rx_queues_to_use, priv->plat->tx_queues_to_use);

	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];

		spin_lock_init(&ch->lock);
		ch->priv_data = priv;
		ch->index = queue;

#ifdef TC956X_SRIOV_PF

		if (queue < priv->plat->rx_queues_to_use) {

			if (priv->plat->rx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;

			/* Add napi only for applicable channels */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
			netif_napi_add(ndev, &ch->rx_napi, tc956xmac_napi_poll_rx);
#else
			netif_napi_add(ndev, &ch->rx_napi, tc956xmac_napi_poll_rx,
				       NAPI_POLL_WEIGHT);
#endif
		}
		if (queue < priv->plat->tx_queues_to_use) {


			if (priv->plat->tx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;

			/* Add napi only for applicable channels */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0))
			netif_napi_add_tx(ndev, &ch->tx_napi,
					  tc956xmac_napi_poll_tx);
#else
			netif_tx_napi_add(ndev, &ch->tx_napi,
					  tc956xmac_napi_poll_tx,
					  NAPI_POLL_WEIGHT);
#endif
		}
#elif defined TC956X_SRIOV_VF
		/* DMA Tx or Rx channel number and counts are same for VF, so
		 * use any one variable to skip the non-used channel
		 */

		if ((queue < priv->plat->rx_queues_to_use) &&
				(priv->plat->ch_in_use[queue] == 1)) {
			netif_napi_add(ndev, &ch->rx_napi, tc956xmac_napi_poll_rx,
				       NAPI_POLL_WEIGHT);
		}
		if ((queue < priv->plat->tx_queues_to_use) &&
				(priv->plat->ch_in_use[queue] == 1)) {
			netif_tx_napi_add(ndev, &ch->tx_napi,
					  tc956xmac_napi_poll_tx,
					  NAPI_POLL_WEIGHT);
		}
#else /* TC956X_SRIOV_PF */
		if ((queue < priv->plat->rx_queues_to_use) &&
			(priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)) {

			netif_napi_add(ndev, &ch->rx_napi, tc956xmac_napi_poll_rx,
				       NAPI_POLL_WEIGHT);
		}
		if ((queue < priv->plat->tx_queues_to_use) &&
			(priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)) {
			netif_tx_napi_add(ndev, &ch->tx_napi,
					  tc956xmac_napi_poll_tx,
					  NAPI_POLL_WEIGHT);
		}
#endif /* TC956X_SRIOV_PF */
	}

	mutex_init(&priv->lock);
	mutex_init(&priv->port_ld_release_lock);

#ifndef TC956X_SRIOV_VF

	/* If a specific clk_csr value is passed from the platform
	 * this means that the CSR Clock Range selection cannot be
	 * changed at run-time and it is fixed. Viceversa the driver'll try to
	 * set the MDC clock dynamically according to the csr actual
	 * clock input.
	 */
	if (priv->plat->clk_csr >= 0)
		priv->clk_csr = priv->plat->clk_csr;
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	else
		tc956xmac_clk_csr_set(priv);
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	tc956xmac_check_pcs_mode(priv);

#ifdef TC956X
	/*PMA module init*/
	if (priv->hw->xpcs) {

		if (priv->port_num == RM_PF0_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret |= (NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}

		if (priv->port_num == RM_PF1_ID) {
			/* Assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret |= (NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		ret = tc956x_pma_setup(priv, priv->pmaaddr);
		if (ret < 0)
			KPRINT_INFO("PMA switching to internal clock Failed\n");

		if (priv->port_num == RM_PF0_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
			ret &= ~(NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST);
			writel(ret, priv->ioaddr + NRSTCTRL0_OFFSET);
		}

		if (priv->port_num == RM_PF1_ID) {
			/* De-assertion of PMA &  XPCS reset  software Reset*/
			ret = readl(priv->ioaddr + NRSTCTRL1_OFFSET);
			ret &= ~(NRSTCTRL1_MAC1PMARST1 | NRSTCTRL1_MAC1PONRST1);
			writel(ret, priv->ioaddr + NRSTCTRL1_OFFSET);
		}

		if (priv->port_num == RM_PF0_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC0CTL_OFFSET);
			} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}

		if (priv->port_num == RM_PF1_ID) {
			do {
				ret = readl(priv->ioaddr + NEMAC1CTL_OFFSET);
			} while ((NEMACCTL_INIT_DONE & ret) != NEMACCTL_INIT_DONE);
		}

		ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
		if (ret < 0)
			KPRINT_INFO("XPCS initialization error\n");
	}

#endif /* TC956X */
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII  &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI) {
		/* MDIO bus Registration */
#ifdef TC956X_WITHOUT_MDIO
		if (priv->dma_cap.sma_mdio == 1) {
#endif
			ret = tc956xmac_mdio_register(ndev);
			if (ret < 0) {
				/* tc956xmac_mdio_register() will return -ENODEV when No PHY is found */
				if (ret == -ENODEV) {
					dev_info(priv->device, "Port%d will not be registered as ethernet controller", priv->port_num);
					goto error_mdio_register;
				} else {
					dev_err(priv->device,
					"%s: MDIO bus (id: %d) registration failed",
					__func__, priv->plat->bus_id);
					goto error_mdio_register;
				}
		}
	}
#ifdef TC956X_WITHOUT_MDIO
	}
#endif
	ret = tc956xmac_phy_setup(priv);
	if (ret) {
		netdev_err(ndev, "failed to setup phy (%d)\n", ret);
		goto error_phy_setup;
	}
#endif /* TC956X_SRIOV_VF */
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->device, "%s: ERROR %i registering the device\n",
			__func__, ret);
		goto error_netdev_register;
	}

#ifdef CONFIG_DEBUG_FS
	tc956xmac_init_fs(ndev);
#endif

#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
	priv->pbridge_buffsize = DEFAULT_BUFSIZE * DMA_RX_SIZE;
	priv->pbridge_buffaddr = dma_alloc_coherent(priv->device,
		priv->pbridge_buffsize, &priv->pbridge_handle, GFP_DMA);
	if (priv->pbridge_buffaddr == NULL)
		ret = -ENOSPC;
#endif
	return ret;

error_netdev_register:
#ifndef TC956X_SRIOV_VF
	phylink_destroy(priv->phylink);
error_phy_setup:
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI)
		tc956xmac_mdio_unregister(ndev);
error_mdio_register:
	for (queue = 0; queue < maxq; queue++) {
		struct tc956xmac_channel *ch = &priv->channel[queue];
#ifdef TC956X_SRIOV_PF
		if (queue < priv->plat->rx_queues_to_use) {
			if (priv->plat->rx_ch_in_use[queue] ==
					TC956X_DISABLE_CHNL)
				continue;
#else
		if (queue < priv->plat->rx_queues_to_use &&
				priv->plat->rx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
#endif
			netif_napi_del(&ch->rx_napi);
}
#ifdef TC956X_SRIOV_PF
		if (queue < priv->plat->tx_queues_to_use) {
			if (priv->plat->tx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;
#else
		if (queue < priv->plat->tx_queues_to_use &&
				priv->plat->tx_dma_ch_owner[queue] == USE_IN_TC956X_SW)
#endif
			netif_napi_del(&ch->tx_napi);
		}
	}
#endif /* TC956X_SRIOV_VF */
#ifndef TC956X_WITHOUT_MDIO
	if (priv->port_num == 0) {
		nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
		nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
	} else {
		nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET;
		nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET;
	}
	nrst_val = readl(nrst_reg);
	nclk_val = readl(nclk_reg);
	KPRINT_INFO("%s : Port %d Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
		nrst_val, nclk_val);
	/* Assert reset and Disable Clock for EMAC */
	nrst_val = nrst_val | NRSTCTRL_EMAC_MASK;
	nclk_val = nclk_val & ~NCLKCTRL_EMAC_MASK;
	writel(nrst_val, nrst_reg);
	writel(nclk_val, nclk_reg);
	KPRINT_INFO("%s : Port %d Wr RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
		readl(nrst_reg), readl(nclk_reg));
#endif
error_hw_init:
#ifndef TC956X
	destroy_workqueue(priv->wq);
#endif
#if (defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)) | defined(TC956X_SRIOV_VF)
	destroy_workqueue(priv->mbx_wq);
#endif

	return ret;
}
#ifdef TC956X_SRIOV_PF
EXPORT_SYMBOL_GPL(tc956xmac_dvr_probe);
#elif defined TC956X_SRIOV_VF
EXPORT_SYMBOL_GPL(tc956xmac_vf_dvr_probe);
#endif
/**
 * tc956xmac_dvr_remove
 * @dev: device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
#ifdef TC956X_SRIOV_PF
int tc956xmac_dvr_remove(struct device *dev)
#elif defined TC956X_SRIOV_VF
int tc956xmac_vf_dvr_remove(struct device *dev)
#endif
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
#ifndef TC956X_SRIOV_VF
	u32 val;
	struct phy_device *phydev;
	int addr = priv->plat->phy_addr;

	phydev = mdiobus_get_phy(priv->mii, addr);

#endif

	netdev_info(priv->dev, "%s: removing driver", __func__);

#ifdef TC956X_SRIOV_VF
	tc956xmac_vf_reset(priv, VF_DOWN);
#else
	if (phydev->drv != NULL) {
		if ((true == priv->plat->phy_interrupt_mode) && (phydev->drv->config_intr))
			cancel_work_sync(&priv->emac_phy_work);
	}

#endif

#ifdef CONFIG_DEBUG_FS
	tc956xmac_exit_fs(ndev);
#endif
	tc956xmac_stop_all_dma(priv);

#ifndef TC956X_SRIOV_VF
	tc956xmac_mac_set(priv, priv->ioaddr, false);
#endif

	netif_carrier_off(ndev);
	unregister_netdev(ndev);
#ifndef TC956X_SRIOV_VF
	phylink_destroy(priv->phylink);
#endif
	kfree(priv->mac_table);
	kfree(priv->vlan_table);

	if (priv->plat->tc956xmac_rst)
		reset_control_assert(priv->plat->tc956xmac_rst);
#ifndef TC956X_SRIOV_VF
	clk_disable_unprepare(priv->plat->pclk);
	clk_disable_unprepare(priv->plat->tc956xmac_clk);
	if (priv->hw->pcs != TC956XMAC_PCS_RGMII &&
		priv->hw->pcs != TC956XMAC_PCS_TBI &&
		priv->hw->pcs != TC956XMAC_PCS_RTBI)
		tc956xmac_mdio_unregister(ndev);

	if (tc956x_platform_remove(priv)) {
		dev_err(priv->device, "Platform remove error\n");
	}

#endif
#ifndef TC956X
	destroy_workqueue(priv->wq);
#endif
#if (defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)) | defined(TC956X_SRIOV_VF)
	destroy_workqueue(priv->mbx_wq);
#endif
	mutex_destroy(&priv->lock);
	mutex_destroy(&priv->port_ld_release_lock);

#ifndef TC956X_SRIOV_VF
#ifdef TC956X
	val = ioread32((void __iomem *)(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT));
	val += 1;
#ifdef DISABLE_EMAC_PORT1
	val = TC956X_M3_FW_EXIT_VALUE;
#endif
	iowrite32(val, (void __iomem *)(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_FW_EXIT));
#endif

#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
	dma_free_coherent(priv->device, priv->pbridge_buffsize, priv->pbridge_buffaddr,
			priv->pbridge_handle);
#endif
#endif

	return 0;
}

#ifdef TC956X_SRIOV_PF
EXPORT_SYMBOL_GPL(tc956xmac_dvr_remove);
#elif defined TC956X_SRIOV_VF
EXPORT_SYMBOL_GPL(tc956xmac_vf_dvr_remove);
#endif

/**
 * tc956xmac_suspend - suspend callback
 * @dev: device pointer
 * Description: this is the function to suspend the device and it is called
 * by the platform driver to stop the network queue, release the resources,
 * program the PMT register (for WoL), clean and release driver resources.
 */
#ifdef TC956X_SRIOV_PF
int tc956xmac_suspend(struct device *dev)
#elif defined TC956X_SRIOV_VF
int tc956xmac_vf_suspend(struct device *dev)
#endif
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
#ifndef TC956X_SRIOV_VF
	struct phy_device *phydev = NULL; /* For cancelling Work queue */
	int addr = priv->plat->phy_addr;
#endif
	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);
#ifndef TC956X_SRIOV_VF
	if ((priv->plat->phy_addr != -1) && (priv->mii != NULL))
		phydev = mdiobus_get_phy(priv->mii, addr);
#endif
	if (!ndev)
		return 0;
#ifndef TC956X_SRIOV_VF
	if (!phydev) {
		DBGPR_FUNC(priv->device, "%s Error : No phy at Addr %d or MDIO Unavailable\n",
			__func__, addr);
		return 0;
	}
#endif

#ifdef TC956X_SRIOV_VF
	tc956xmac_vf_reset(priv, VF_SUSPEND);
#endif

#ifndef TC956X_SRIOV_VF
	/* Disabling EEE for issue in TC9560/62, to be tested for TC956X */
	if (priv->eee_enabled)
		tc956xmac_disable_eee_mode(priv);

	//if (priv->wolopts) {
	//	KPRINT_INFO("%s : Port %d - Phy Speed Down", __func__, priv->port_num);
	//	phy_speed_down(phydev, true);
	//}
#endif

	if (!netif_running(ndev))
		goto clean_exit;

#ifndef TC956X_SRIOV_VF
	/* Cancel all work-queues before suspend start only when net interface is up and running */
	if (phydev->drv != NULL) {
		if ((true == priv->plat->phy_interrupt_mode) &&
		(phydev->drv->config_intr)) {
			DBGPR_FUNC(priv->device, "%s : (Flush All PHY work-queues)\n", __func__);
			cancel_work_sync(&priv->emac_phy_work);
		}
	}
#endif
	/* Invoke device driver close only when net inteface is up and running. */
	rtnl_lock();
	tc956xmac_release(ndev);
	rtnl_unlock();

clean_exit:
	/* Detach network device */
	netif_device_detach(ndev);

	priv->oldlink = false;
	priv->speed = SPEED_UNKNOWN;
	priv->oldduplex = DUPLEX_UNKNOWN;

	KPRINT_INFO("<--- %s : Port %d", __func__, priv->port_num);
	return 0;
}
#ifdef TC956X_SRIOV_PF
EXPORT_SYMBOL_GPL(tc956xmac_suspend);
#elif defined TC956X_SRIOV_VF
EXPORT_SYMBOL_GPL(tc956xmac_vf_suspend);
#endif
#ifndef TC956X
/**
 * tc956xmac_reset_queues_param - reset queue parameters
 * @dev: device pointer
 */
static void tc956xmac_reset_queues_param(struct tc956xmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < rx_cnt; queue++) {
		struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[queue];
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->rx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		rx_q->cur_rx = 0;
		rx_q->dirty_rx = 0;
	}

	for (queue = 0; queue < tx_cnt; queue++) {
		struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[queue];
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[queue] ==
						TC956X_DISABLE_CHNL)
				continue;
#elif defined TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[queue] == 0)
			continue;
#else
		if (priv->plat->tx_dma_ch_owner[queue] != USE_IN_TC956X_SW)
			continue;
#endif
		tx_q->cur_tx = 0;
		tx_q->dirty_tx = 0;
		tx_q->mss = 0;
	}
}
#endif /* TC956X */
/**
 * tc956xmac_resume - resume callback
 * @dev: device pointer
 * Description: when resume this function is invoked to setup the DMA and CORE
 * in a usable state.
 */
#ifdef TC956X_SRIOV_PF
int tc956xmac_resume(struct device *dev)
#elif defined TC956X_SRIOV_VF
int tc956xmac_vf_resume(struct device *dev)
#endif
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_resources res;
#ifndef TC956X_SRIOV_VF
	u32 cm3_reset_status = 0;
	s32 fw_load_status = 0;
#ifndef TC956X_WITHOUT_MDIO
	void *nrst_reg = NULL, *nclk_reg = NULL;
	u32 nrst_val = 0, nclk_val = 0;
#endif
#endif /* TC956X_SRIOV_VF */

	KPRINT_INFO("---> %s : Port %d", __func__, priv->port_num);
#ifdef TC956X
	memset(&res, 0, sizeof(res));
#ifndef TC956X_SRIOV_VF
	cm3_reset_status = readl((priv->ioaddr + NRSTCTRL0_OFFSET));
	if ((cm3_reset_status & NRSTCTRL0_MCURST) == NRSTCTRL0_MCURST) {
	res.tc956x_SFR_pci_base_addr = priv->tc956x_SFR_pci_base_addr;
	res.addr = priv->ioaddr;
	res.tc956x_SRAM_pci_base_addr = priv->tc956x_SRAM_pci_base_addr;
	res.irq = priv->dev->irq;
		fw_load_status = tc956x_load_firmware(dev, &res);
		if (fw_load_status < 0) {
			KPRINT_ERR("Firmware load failed\n");
			return -EINVAL;
		}
	}

	//if (priv->wolopts) {
	//	KPRINT_INFO("%s : Port %d - Phy Speed Up", __func__, priv->port_num);
	//	phy_speed_up(phydev);
	//}
#elif defined(TC956X_SRIOV_VF)
	res.tc956x_SFR_pci_base_addr = priv->tc956x_SFR_pci_base_addr;
	res.addr = priv->ioaddr;
	res.tc956x_SRAM_pci_base_addr = priv->tc956x_SRAM_pci_base_addr;
	res.irq = priv->dev->irq;
#endif
#endif /* TC956X */


#ifndef TC956X
	/* Reset Parameters. */
	tc956xmac_reset_queues_param(priv);
#endif
	if (!netif_running(ndev))
		goto clean_exit;

	/* Invoke device driver open */
	rtnl_lock();
	tc956xmac_open(ndev);
	rtnl_unlock();

clean_exit:
	/* Attach network device */
	netif_device_attach(ndev);



#ifdef TC956X_SRIOV_VF
	tc956xmac_vf_reset(priv, VF_UP);
#endif  /* TC956X_SRIOV_VF */

#ifndef TC956X_SRIOV_VF
	/*  Reset eMAC when Port unavailable */
	if ((priv->plat->phy_addr == -1) || (priv->mii == NULL)) {
		KPRINT_ERR("%s : Port %d : Invalid PHY Address (%d)\n", __func__, priv->port_num,
			priv->plat->phy_addr);
#ifndef TC956X_WITHOUT_MDIO
		/* Set Clocks same as before suspend */
		if (priv->port_num == 0) {
			nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
			nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
		} else {
			nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET;
			nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET;
		}
		nrst_val = readl(nrst_reg);
		nclk_val = readl(nclk_reg);
		KPRINT_INFO("%s : Port %d Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
			nrst_val, nclk_val);
		/* Assert reset and Disable Clock for EMAC */
		nrst_val = nrst_val | NRSTCTRL_EMAC_MASK;
		nclk_val = nclk_val & ~NCLKCTRL_EMAC_MASK;
		writel(nrst_val, nrst_reg);
		writel(nclk_val, nclk_reg);
		KPRINT_INFO("%s : Port %d Wr RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
			readl(nrst_reg), readl(nclk_reg));
#endif
	}
#endif
	KPRINT_INFO("<--- %s : Port %d", __func__, priv->port_num);
	return 0;
}
#ifdef TC956X_SRIOV_PF
EXPORT_SYMBOL_GPL(tc956xmac_resume);
#elif defined TC956X_SRIOV_VF
EXPORT_SYMBOL_GPL(tc956xmac_vf_resume);
#endif

#ifndef TC956X_SRIOV_VF
/*!
 * \brief API to save and restore clock and reset during link down and link up.
 *
 * \details This fucntion saves the EMAC clock and reset bits before
 * link down. And restores the same settings after link up.
 *
 * \param[in] priv - pointer to device private structure.
 * \param[in] state - identify LINK DOWN and LINK DOWN operation.
 *
 * \return None
 */
void tc956xmac_link_change_set_power(struct tc956xmac_priv *priv, enum TC956X_PORT_LINK_CHANGE_STATE state)
{
	void *nrst_reg = NULL, *nclk_reg = NULL, *commonrst_reg = NULL, *commonclk_reg = NULL;
	u32 nrst_val = 0, nclk_val = 0, commonrst_val = 0, commonclk_val = 0;
	static u32 pm_saved_cmn_linkdown_rst = 0, pm_saved_cmn_linkdown_clk = 0;
	int ret;
	bool enable_en = true;

	if (mac_power_save_at_link_down == ENABLE) {
		KPRINT_INFO("-->%s : Port %d", __func__, priv->port_num);
		/* Select register address by port */
		if (priv->port_num == 0) {
			nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
			nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
		} else {
			nrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL1_OFFSET;
			nclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL1_OFFSET;
		}
		if (state == LINK_DOWN) {
			KPRINT_INFO("%s : Port %d Set Power for Link Down", __func__, priv->port_num);
			nrst_val = readl(nrst_reg);
			nclk_val = readl(nclk_reg);
			KPRINT_INFO("%s : Port %d Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
				nrst_val, nclk_val);
			/* Save register values before Asserting reset and Clock Disable */
			priv->pm_saved_linkdown_rst = ((~nrst_val) & NRSTCTRL_LINK_DOWN_SAVE); /* Save Non-Common De-Asserted Resets */
			priv->pm_saved_linkdown_clk = (nclk_val & NCLKCTRL_LINK_DOWN_SAVE); /* Save Non-Common Enabled Clocks */
			KPRINT_INFO("%s : Port %d priv->pm_saved_linkdown_rst %x priv->pm_saved_linkdown_clk %x", __func__,
				priv->port_num, priv->pm_saved_linkdown_rst, priv->pm_saved_linkdown_clk);
			/* Assert Reset and Disable Clock */
			nrst_val = nrst_val | NRSTCTRL_LINK_DOWN;
			nclk_val = nclk_val & ~NCLKCTRL_LINK_DOWN;
			KPRINT_INFO("%s : Port %d Wr RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
				nrst_val, nclk_val);
			writel(nrst_val, nrst_reg);
			writel(nclk_val, nclk_reg);
			tc956xmac_link_down_counter++; /* Increment counter for Link Down */
			if (tc956xmac_link_down_counter == TC956X_ALL_MAC_PORT_LINK_DOWN) {
				/* Save Common register values */
				commonrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
				commonclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
				commonrst_val = readl(commonrst_reg);
				commonclk_val = readl(commonclk_reg);
				KPRINT_INFO("%s : Port %d Common Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
					commonrst_val, commonclk_val);
				pm_saved_cmn_linkdown_rst = ((~commonrst_val) & NRSTCTRL_LINK_DOWN_CMN_SAVE); /* Save Common De-Asserted Resets */
				pm_saved_cmn_linkdown_clk = commonclk_val & NCLKCTRL_LINK_DOWN_CMN_SAVE; /* Save Common Enabled Clocks */
				KPRINT_INFO("%s : Port %d pm_saved_cmn_linkdown_rst %x pm_saved_cmn_linkdown_clk %x", __func__,
					priv->port_num, pm_saved_cmn_linkdown_rst, pm_saved_cmn_linkdown_clk);
			}
			priv->port_link_down = true; /* Set per port flag to true */
		} else if (state == LINK_UP) {
			KPRINT_INFO("%s : Port %d Set Power for Link Up", __func__, priv->port_num);
			if (tc956xmac_link_down_counter == TC956X_ALL_MAC_PORT_LINK_DOWN) {
				/* Restore Common register values */
				KPRINT_INFO("%s : Port %d pm_saved_cmn_linkdown_clk %x, pm_saved_cmn_linkdown_rst %x", __func__,
					priv->port_num, pm_saved_cmn_linkdown_clk, pm_saved_cmn_linkdown_rst);
				/* Enable Common Clock and De-Assert Common Resets */
				commonclk_reg = priv->tc956x_SFR_pci_base_addr + NCLKCTRL0_OFFSET;
				commonrst_reg = priv->tc956x_SFR_pci_base_addr + NRSTCTRL0_OFFSET;
				commonclk_val = readl(commonclk_reg);
				commonrst_val = readl(commonrst_reg);
				KPRINT_INFO("%s : Port %d Common Rd CLK Reg:%x, RST Reg:%x ", __func__, priv->port_num,
					commonclk_val, commonrst_val);
				/* Clear Common Clocks only when both port suspends */
				commonclk_val = (commonclk_val | pm_saved_cmn_linkdown_clk); /* Enable Common Saved Clock */
				commonrst_val = (commonrst_val & (~pm_saved_cmn_linkdown_rst)); /* De-assert Common Saved Reset */
				writel(commonclk_val, commonclk_reg);
				writel(commonrst_val, commonrst_reg);
				KPRINT_INFO("%s : Port %d Common Wr CLK Reg:%x, RST Reg:%x ", __func__, priv->port_num,
					commonclk_val, commonrst_val);
				KPRINT_INFO("%s : Port %d Common Rd CLK Reg:%x, RST Reg:%x", __func__, priv->port_num,
					readl(commonclk_reg), readl(commonrst_reg));
			}
			/* Restore register values */
			KPRINT_INFO("%s : Port %d pm_saved_linkdown_clk %x, pm_saved_linkdown_rst %x", __func__,
				priv->port_num, priv->pm_saved_linkdown_clk, priv->pm_saved_linkdown_rst);
			nclk_val = readl(nclk_reg);
			nrst_val = readl(nrst_reg);
			KPRINT_INFO("%s : Port %d Rd CLK Reg:%x, RST Reg:%x", __func__, priv->port_num,
				nrst_val, nclk_val);
			/* Restore values same as before link down */
			nclk_val = (nclk_val | priv->pm_saved_linkdown_clk); /* Enable Saved Clock */
			nrst_val = (nrst_val & (~(priv->pm_saved_linkdown_rst))); /* De-assert Saved Reset */
			writel(nclk_val, nclk_reg);
			writel(nrst_val, nrst_reg);
			KPRINT_INFO("%s : Port %d Wr CLK Reg:%x, RST Reg:%x ", __func__, priv->port_num,
				nclk_val, nrst_val);

			tc956xmac_link_down_counter--; /* Decrement Counter Only when this api called */
			priv->port_link_down = false;

			/* Re-Init XPCS module as MACxPONRST is asserted during link-down */
			ret = tc956x_xpcs_init(priv, priv->xpcsaddr);
			if (ret < 0)
				KPRINT_INFO("XPCS initialization error\n");

			/*C37 AN enable*/
				if ((priv->plat->interface == PHY_INTERFACE_MODE_10GKR) ||
					(priv->plat->interface == ENABLE_2500BASE_X_INTERFACE))
				enable_en = false;
			else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
				if (priv->is_sgmii_2p5g == true)
					enable_en = false;
				else
					enable_en = true;
			} else
				enable_en = true;

			tc956x_xpcs_ctrl_ane(priv, enable_en);
		}
		KPRINT_INFO("%s : Port %d Rd RST Reg:%x, CLK Reg:%x", __func__, priv->port_num,
			readl(nrst_reg), readl(nclk_reg));
		KPRINT_INFO("<--%s : Port %d", __func__, priv->port_num);
	} else {
		KPRINT_INFO("-->%s : status of Power saving at Link down %d", __func__, mac_power_save_at_link_down);
	}

}
#endif

#ifndef MODULE
static int __init tc956xmac_cmdline_opt(char *str)
{
	char *opt;

	if (!str || !*str)
		return -EINVAL;
	while ((opt = strsep(&str, ",")) != NULL) {
		if (!strncmp(opt, "debug:", 6)) {
			if (kstrtoint(opt + 6, 0, &debug))
				goto err;
		} else if (!strncmp(opt, "phyaddr:", 8)) {
			if (kstrtoint(opt + 8, 0, &phyaddr))
				goto err;
		} else if (!strncmp(opt, "buf_sz:", 7)) {
			if (kstrtoint(opt + 7, 0, &buf_sz))
				goto err;
		} else if (!strncmp(opt, "tc:", 3)) {
			if (kstrtoint(opt + 3, 0, &tc))
				goto err;
		} else if (!strncmp(opt, "watchdog:", 9)) {
			if (kstrtoint(opt + 9, 0, &watchdog))
				goto err;
		} else if (!strncmp(opt, "flow_ctrl:", 10)) {
			if (kstrtoint(opt + 10, 0, &flow_ctrl))
				goto err;
		} else if (!strncmp(opt, "pause:", 6)) {
			if (kstrtoint(opt + 6, 0, &pause))
				goto err;
		} else if (!strncmp(opt, "eee_timer:", 10)) {
			if (kstrtoint(opt + 10, 0, &eee_timer))
				goto err;
		} else if (!strncmp(opt, "chain_mode:", 11)) {
			if (kstrtoint(opt + 11, 0, &chain_mode))
				goto err;
		}
	}
	return 0;

err:
	pr_err("%s: ERROR broken module parameter conversion", __func__);
	return -EINVAL;
}

__setup("tc956xmaceth=", tc956xmac_cmdline_opt);
#endif /* MODULE */

#ifdef TC956X
int tc956xmac_init(void)
{
#ifdef CONFIG_DEBUG_FS
	/* Create debugfs main directory if it doesn't exist yet */
	if (!tc956xmac_fs_dir)
		tc956xmac_fs_dir = debugfs_create_dir(TC956X_RESOURCE_NAME, NULL);
#endif

	return 0;
}

void tc956xmac_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(tc956xmac_fs_dir);
#endif
}
#endif

#ifndef TC956X
MODULE_DESCRIPTION("TC956X PCI Express Ethernet Network Driver");
MODULE_AUTHOR("Toshiba Electronic Devices & Storage Corporation");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_MODULE_VERSION);
#endif
