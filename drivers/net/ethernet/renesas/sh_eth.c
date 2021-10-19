// SPDX-License-Identifier: GPL-2.0
/*  SuperH Ethernet device driver
 *
 *  Copyright (C) 2014 Renesas Electronics Corporation
 *  Copyright (C) 2006-2012 Nobuhiro Iwamatsu
 *  Copyright (C) 2008-2014 Renesas Solutions Corp.
 *  Copyright (C) 2013-2017 Cogent Embedded, Inc.
 *  Copyright (C) 2014 Codethink Limited
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mdio-bitbang.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/cache.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/sh_eth.h>
#include <linux/of_mdio.h>

#include "sh_eth.h"

#define SH_ETH_DEF_MSG_ENABLE \
		(NETIF_MSG_LINK	| \
		NETIF_MSG_TIMER	| \
		NETIF_MSG_RX_ERR| \
		NETIF_MSG_TX_ERR)

#define SH_ETH_OFFSET_INVALID	((u16)~0)

#define SH_ETH_OFFSET_DEFAULTS			\
	[0 ... SH_ETH_MAX_REGISTER_OFFSET - 1] = SH_ETH_OFFSET_INVALID

/* use some intentionally tricky logic here to initialize the whole struct to
 * 0xffff, but then override certain fields, requiring us to indicate that we
 * "know" that there are overrides in this structure, and we'll need to disable
 * that warning from W=1 builds. GCC has supported this option since 4.2.X, but
 * the macros available to do this only define GCC 8.
 */
__diag_push();
__diag_ignore(GCC, 8, "-Woverride-init",
	      "logic to initialize all and then override some is OK");
static const u16 sh_eth_offset_gigabit[SH_ETH_MAX_REGISTER_OFFSET] = {
	SH_ETH_OFFSET_DEFAULTS,

	[EDSR]		= 0x0000,
	[EDMR]		= 0x0400,
	[EDTRR]		= 0x0408,
	[EDRRR]		= 0x0410,
	[EESR]		= 0x0428,
	[EESIPR]	= 0x0430,
	[TDLAR]		= 0x0010,
	[TDFAR]		= 0x0014,
	[TDFXR]		= 0x0018,
	[TDFFR]		= 0x001c,
	[RDLAR]		= 0x0030,
	[RDFAR]		= 0x0034,
	[RDFXR]		= 0x0038,
	[RDFFR]		= 0x003c,
	[TRSCER]	= 0x0438,
	[RMFCR]		= 0x0440,
	[TFTR]		= 0x0448,
	[FDR]		= 0x0450,
	[RMCR]		= 0x0458,
	[RPADIR]	= 0x0460,
	[FCFTR]		= 0x0468,
	[CSMR]		= 0x04E4,

	[ECMR]		= 0x0500,
	[ECSR]		= 0x0510,
	[ECSIPR]	= 0x0518,
	[PIR]		= 0x0520,
	[PSR]		= 0x0528,
	[PIPR]		= 0x052c,
	[RFLR]		= 0x0508,
	[APR]		= 0x0554,
	[MPR]		= 0x0558,
	[PFTCR]		= 0x055c,
	[PFRCR]		= 0x0560,
	[TPAUSER]	= 0x0564,
	[GECMR]		= 0x05b0,
	[BCULR]		= 0x05b4,
	[MAHR]		= 0x05c0,
	[MALR]		= 0x05c8,
	[TROCR]		= 0x0700,
	[CDCR]		= 0x0708,
	[LCCR]		= 0x0710,
	[CEFCR]		= 0x0740,
	[FRECR]		= 0x0748,
	[TSFRCR]	= 0x0750,
	[TLFRCR]	= 0x0758,
	[RFCR]		= 0x0760,
	[CERCR]		= 0x0768,
	[CEECR]		= 0x0770,
	[MAFCR]		= 0x0778,
	[RMII_MII]	= 0x0790,

	[ARSTR]		= 0x0000,
	[TSU_CTRST]	= 0x0004,
	[TSU_FWEN0]	= 0x0010,
	[TSU_FWEN1]	= 0x0014,
	[TSU_FCM]	= 0x0018,
	[TSU_BSYSL0]	= 0x0020,
	[TSU_BSYSL1]	= 0x0024,
	[TSU_PRISL0]	= 0x0028,
	[TSU_PRISL1]	= 0x002c,
	[TSU_FWSL0]	= 0x0030,
	[TSU_FWSL1]	= 0x0034,
	[TSU_FWSLC]	= 0x0038,
	[TSU_QTAGM0]	= 0x0040,
	[TSU_QTAGM1]	= 0x0044,
	[TSU_FWSR]	= 0x0050,
	[TSU_FWINMK]	= 0x0054,
	[TSU_ADQT0]	= 0x0048,
	[TSU_ADQT1]	= 0x004c,
	[TSU_VTAG0]	= 0x0058,
	[TSU_VTAG1]	= 0x005c,
	[TSU_ADSBSY]	= 0x0060,
	[TSU_TEN]	= 0x0064,
	[TSU_POST1]	= 0x0070,
	[TSU_POST2]	= 0x0074,
	[TSU_POST3]	= 0x0078,
	[TSU_POST4]	= 0x007c,
	[TSU_ADRH0]	= 0x0100,

	[TXNLCR0]	= 0x0080,
	[TXALCR0]	= 0x0084,
	[RXNLCR0]	= 0x0088,
	[RXALCR0]	= 0x008c,
	[FWNLCR0]	= 0x0090,
	[FWALCR0]	= 0x0094,
	[TXNLCR1]	= 0x00a0,
	[TXALCR1]	= 0x00a4,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,
};

static const u16 sh_eth_offset_fast_rcar[SH_ETH_MAX_REGISTER_OFFSET] = {
	SH_ETH_OFFSET_DEFAULTS,

	[ECMR]		= 0x0300,
	[RFLR]		= 0x0308,
	[ECSR]		= 0x0310,
	[ECSIPR]	= 0x0318,
	[PIR]		= 0x0320,
	[PSR]		= 0x0328,
	[RDMLR]		= 0x0340,
	[IPGR]		= 0x0350,
	[APR]		= 0x0354,
	[MPR]		= 0x0358,
	[RFCF]		= 0x0360,
	[TPAUSER]	= 0x0364,
	[TPAUSECR]	= 0x0368,
	[MAHR]		= 0x03c0,
	[MALR]		= 0x03c8,
	[TROCR]		= 0x03d0,
	[CDCR]		= 0x03d4,
	[LCCR]		= 0x03d8,
	[CNDCR]		= 0x03dc,
	[CEFCR]		= 0x03e4,
	[FRECR]		= 0x03e8,
	[TSFRCR]	= 0x03ec,
	[TLFRCR]	= 0x03f0,
	[RFCR]		= 0x03f4,
	[MAFCR]		= 0x03f8,

	[EDMR]		= 0x0200,
	[EDTRR]		= 0x0208,
	[EDRRR]		= 0x0210,
	[TDLAR]		= 0x0218,
	[RDLAR]		= 0x0220,
	[EESR]		= 0x0228,
	[EESIPR]	= 0x0230,
	[TRSCER]	= 0x0238,
	[RMFCR]		= 0x0240,
	[TFTR]		= 0x0248,
	[FDR]		= 0x0250,
	[RMCR]		= 0x0258,
	[TFUCR]		= 0x0264,
	[RFOCR]		= 0x0268,
	[RMIIMODE]      = 0x026c,
	[FCFTR]		= 0x0270,
	[TRIMD]		= 0x027c,
};

static const u16 sh_eth_offset_fast_sh4[SH_ETH_MAX_REGISTER_OFFSET] = {
	SH_ETH_OFFSET_DEFAULTS,

	[ECMR]		= 0x0100,
	[RFLR]		= 0x0108,
	[ECSR]		= 0x0110,
	[ECSIPR]	= 0x0118,
	[PIR]		= 0x0120,
	[PSR]		= 0x0128,
	[RDMLR]		= 0x0140,
	[IPGR]		= 0x0150,
	[APR]		= 0x0154,
	[MPR]		= 0x0158,
	[TPAUSER]	= 0x0164,
	[RFCF]		= 0x0160,
	[TPAUSECR]	= 0x0168,
	[BCFRR]		= 0x016c,
	[MAHR]		= 0x01c0,
	[MALR]		= 0x01c8,
	[TROCR]		= 0x01d0,
	[CDCR]		= 0x01d4,
	[LCCR]		= 0x01d8,
	[CNDCR]		= 0x01dc,
	[CEFCR]		= 0x01e4,
	[FRECR]		= 0x01e8,
	[TSFRCR]	= 0x01ec,
	[TLFRCR]	= 0x01f0,
	[RFCR]		= 0x01f4,
	[MAFCR]		= 0x01f8,
	[RTRATE]	= 0x01fc,

	[EDMR]		= 0x0000,
	[EDTRR]		= 0x0008,
	[EDRRR]		= 0x0010,
	[TDLAR]		= 0x0018,
	[RDLAR]		= 0x0020,
	[EESR]		= 0x0028,
	[EESIPR]	= 0x0030,
	[TRSCER]	= 0x0038,
	[RMFCR]		= 0x0040,
	[TFTR]		= 0x0048,
	[FDR]		= 0x0050,
	[RMCR]		= 0x0058,
	[TFUCR]		= 0x0064,
	[RFOCR]		= 0x0068,
	[FCFTR]		= 0x0070,
	[RPADIR]	= 0x0078,
	[TRIMD]		= 0x007c,
	[RBWAR]		= 0x00c8,
	[RDFAR]		= 0x00cc,
	[TBRAR]		= 0x00d4,
	[TDFAR]		= 0x00d8,
};

static const u16 sh_eth_offset_fast_sh3_sh2[SH_ETH_MAX_REGISTER_OFFSET] = {
	SH_ETH_OFFSET_DEFAULTS,

	[EDMR]		= 0x0000,
	[EDTRR]		= 0x0004,
	[EDRRR]		= 0x0008,
	[TDLAR]		= 0x000c,
	[RDLAR]		= 0x0010,
	[EESR]		= 0x0014,
	[EESIPR]	= 0x0018,
	[TRSCER]	= 0x001c,
	[RMFCR]		= 0x0020,
	[TFTR]		= 0x0024,
	[FDR]		= 0x0028,
	[RMCR]		= 0x002c,
	[EDOCR]		= 0x0030,
	[FCFTR]		= 0x0034,
	[RPADIR]	= 0x0038,
	[TRIMD]		= 0x003c,
	[RBWAR]		= 0x0040,
	[RDFAR]		= 0x0044,
	[TBRAR]		= 0x004c,
	[TDFAR]		= 0x0050,

	[ECMR]		= 0x0160,
	[ECSR]		= 0x0164,
	[ECSIPR]	= 0x0168,
	[PIR]		= 0x016c,
	[MAHR]		= 0x0170,
	[MALR]		= 0x0174,
	[RFLR]		= 0x0178,
	[PSR]		= 0x017c,
	[TROCR]		= 0x0180,
	[CDCR]		= 0x0184,
	[LCCR]		= 0x0188,
	[CNDCR]		= 0x018c,
	[CEFCR]		= 0x0194,
	[FRECR]		= 0x0198,
	[TSFRCR]	= 0x019c,
	[TLFRCR]	= 0x01a0,
	[RFCR]		= 0x01a4,
	[MAFCR]		= 0x01a8,
	[IPGR]		= 0x01b4,
	[APR]		= 0x01b8,
	[MPR]		= 0x01bc,
	[TPAUSER]	= 0x01c4,
	[BCFR]		= 0x01cc,

	[ARSTR]		= 0x0000,
	[TSU_CTRST]	= 0x0004,
	[TSU_FWEN0]	= 0x0010,
	[TSU_FWEN1]	= 0x0014,
	[TSU_FCM]	= 0x0018,
	[TSU_BSYSL0]	= 0x0020,
	[TSU_BSYSL1]	= 0x0024,
	[TSU_PRISL0]	= 0x0028,
	[TSU_PRISL1]	= 0x002c,
	[TSU_FWSL0]	= 0x0030,
	[TSU_FWSL1]	= 0x0034,
	[TSU_FWSLC]	= 0x0038,
	[TSU_QTAGM0]	= 0x0040,
	[TSU_QTAGM1]	= 0x0044,
	[TSU_ADQT0]	= 0x0048,
	[TSU_ADQT1]	= 0x004c,
	[TSU_FWSR]	= 0x0050,
	[TSU_FWINMK]	= 0x0054,
	[TSU_ADSBSY]	= 0x0060,
	[TSU_TEN]	= 0x0064,
	[TSU_POST1]	= 0x0070,
	[TSU_POST2]	= 0x0074,
	[TSU_POST3]	= 0x0078,
	[TSU_POST4]	= 0x007c,

	[TXNLCR0]	= 0x0080,
	[TXALCR0]	= 0x0084,
	[RXNLCR0]	= 0x0088,
	[RXALCR0]	= 0x008c,
	[FWNLCR0]	= 0x0090,
	[FWALCR0]	= 0x0094,
	[TXNLCR1]	= 0x00a0,
	[TXALCR1]	= 0x00a4,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,

	[TSU_ADRH0]	= 0x0100,
};
__diag_pop();

static void sh_eth_rcv_snd_disable(struct net_device *ndev);
static struct net_device_stats *sh_eth_get_stats(struct net_device *ndev);

static void sh_eth_write(struct net_device *ndev, u32 data, int enum_index)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 offset = mdp->reg_offset[enum_index];

	if (WARN_ON(offset == SH_ETH_OFFSET_INVALID))
		return;

	iowrite32(data, mdp->addr + offset);
}

static u32 sh_eth_read(struct net_device *ndev, int enum_index)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 offset = mdp->reg_offset[enum_index];

	if (WARN_ON(offset == SH_ETH_OFFSET_INVALID))
		return ~0U;

	return ioread32(mdp->addr + offset);
}

static void sh_eth_modify(struct net_device *ndev, int enum_index, u32 clear,
			  u32 set)
{
	sh_eth_write(ndev, (sh_eth_read(ndev, enum_index) & ~clear) | set,
		     enum_index);
}

static u16 sh_eth_tsu_get_offset(struct sh_eth_private *mdp, int enum_index)
{
	return mdp->reg_offset[enum_index];
}

static void sh_eth_tsu_write(struct sh_eth_private *mdp, u32 data,
			     int enum_index)
{
	u16 offset = sh_eth_tsu_get_offset(mdp, enum_index);

	if (WARN_ON(offset == SH_ETH_OFFSET_INVALID))
		return;

	iowrite32(data, mdp->tsu_addr + offset);
}

static u32 sh_eth_tsu_read(struct sh_eth_private *mdp, int enum_index)
{
	u16 offset = sh_eth_tsu_get_offset(mdp, enum_index);

	if (WARN_ON(offset == SH_ETH_OFFSET_INVALID))
		return ~0U;

	return ioread32(mdp->tsu_addr + offset);
}

static void sh_eth_soft_swap(char *src, int len)
{
#ifdef __LITTLE_ENDIAN
	u32 *p = (u32 *)src;
	u32 *maxp = p + DIV_ROUND_UP(len, sizeof(u32));

	for (; p < maxp; p++)
		*p = swab32(*p);
#endif
}

static void sh_eth_select_mii(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 value;

	switch (mdp->phy_interface) {
	case PHY_INTERFACE_MODE_RGMII ... PHY_INTERFACE_MODE_RGMII_TXID:
		value = 0x3;
		break;
	case PHY_INTERFACE_MODE_GMII:
		value = 0x2;
		break;
	case PHY_INTERFACE_MODE_MII:
		value = 0x1;
		break;
	case PHY_INTERFACE_MODE_RMII:
		value = 0x0;
		break;
	default:
		netdev_warn(ndev,
			    "PHY interface mode was not setup. Set to MII.\n");
		value = 0x1;
		break;
	}

	sh_eth_write(ndev, value, RMII_MII);
}

static void sh_eth_set_duplex(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	sh_eth_modify(ndev, ECMR, ECMR_DM, mdp->duplex ? ECMR_DM : 0);
}

static void sh_eth_chip_reset(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* reset device */
	sh_eth_tsu_write(mdp, ARSTR_ARST, ARSTR);
	mdelay(1);
}

static int sh_eth_soft_reset(struct net_device *ndev)
{
	sh_eth_modify(ndev, EDMR, EDMR_SRST_ETHER, EDMR_SRST_ETHER);
	mdelay(3);
	sh_eth_modify(ndev, EDMR, EDMR_SRST_ETHER, 0);

	return 0;
}

static int sh_eth_check_soft_reset(struct net_device *ndev)
{
	int cnt;

	for (cnt = 100; cnt > 0; cnt--) {
		if (!(sh_eth_read(ndev, EDMR) & EDMR_SRST_GETHER))
			return 0;
		mdelay(1);
	}

	netdev_err(ndev, "Device reset failed\n");
	return -ETIMEDOUT;
}

static int sh_eth_soft_reset_gether(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	sh_eth_write(ndev, EDSR_ENALL, EDSR);
	sh_eth_modify(ndev, EDMR, EDMR_SRST_GETHER, EDMR_SRST_GETHER);

	ret = sh_eth_check_soft_reset(ndev);
	if (ret)
		return ret;

	/* Table Init */
	sh_eth_write(ndev, 0, TDLAR);
	sh_eth_write(ndev, 0, TDFAR);
	sh_eth_write(ndev, 0, TDFXR);
	sh_eth_write(ndev, 0, TDFFR);
	sh_eth_write(ndev, 0, RDLAR);
	sh_eth_write(ndev, 0, RDFAR);
	sh_eth_write(ndev, 0, RDFXR);
	sh_eth_write(ndev, 0, RDFFR);

	/* Reset HW CRC register */
	if (mdp->cd->csmr)
		sh_eth_write(ndev, 0, CSMR);

	/* Select MII mode */
	if (mdp->cd->select_mii)
		sh_eth_select_mii(ndev);

	return ret;
}

static void sh_eth_set_rate_gether(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (WARN_ON(!mdp->cd->gecmr))
		return;

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_write(ndev, GECMR_10, GECMR);
		break;
	case 100:/* 100BASE */
		sh_eth_write(ndev, GECMR_100, GECMR);
		break;
	case 1000: /* 1000BASE */
		sh_eth_write(ndev, GECMR_1000, GECMR);
		break;
	}
}

#ifdef CONFIG_OF
/* R7S72100 */
static struct sh_eth_cpu_data r7s72100_data = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,

	.register_type	= SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_ICD,
	.ecsipr_value	= ECSIPR_ICDIP,
	.eesipr_value	= EESIPR_TWB1IP | EESIPR_TWBIP | EESIPR_TC1IP |
			  EESIPR_TABTIP | EESIPR_RABTIP | EESIPR_RFCOFIP |
			  EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP |
			  EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RFE | EESR_RDE | EESR_RFRMER | EESR_TFE |
			  EESR_TDE,
	.fdr_value	= 0x0000070f,

	.trscer_err_mask = TRSCER_RMAFCE | TRSCER_RRFCE,

	.no_psr		= 1,
	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.csmr		= 1,
	.rx_csum	= 1,
	.tsu		= 1,
	.no_tx_cntrs	= 1,
};

static void sh_eth_chip_reset_r8a7740(struct net_device *ndev)
{
	sh_eth_chip_reset(ndev);

	sh_eth_select_mii(ndev);
}

/* R8A7740 */
static struct sh_eth_cpu_data r8a7740_data = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.chip_reset	= sh_eth_chip_reset_r8a7740,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.register_type	= SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  0x0000f000 | EESIPR_CNDIP | EESIPR_DLCIP |
			  EESIPR_CDIP | EESIPR_TROIP | EESIPR_RMAFIP |
			  EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RFE | EESR_RDE | EESR_RFRMER | EESR_TFE |
			  EESR_TDE,
	.fdr_value	= 0x0000070f,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.gecmr		= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.csmr		= 1,
	.rx_csum	= 1,
	.tsu		= 1,
	.select_mii	= 1,
	.magic		= 1,
	.cexcr		= 1,
};

/* There is CPU dependent code */
static void sh_eth_set_rate_rcar(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_modify(ndev, ECMR, ECMR_ELB, 0);
		break;
	case 100:/* 100BASE */
		sh_eth_modify(ndev, ECMR, ECMR_ELB, ECMR_ELB);
		break;
	}
}

/* R-Car Gen1 */
static struct sh_eth_cpu_data rcar_gen1_data = {
	.soft_reset	= sh_eth_soft_reset,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_rcar,

	.register_type	= SH_ETH_REG_FAST_RCAR,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ADEIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP |
			  EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_TRO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,
	.fdr_value	= 0x00000f0f,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.no_xdfar	= 1,
};

/* R-Car Gen2 and RZ/G1 */
static struct sh_eth_cpu_data rcar_gen2_data = {
	.soft_reset	= sh_eth_soft_reset,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_rcar,

	.register_type	= SH_ETH_REG_FAST_RCAR,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP |
			  ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ADEIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP |
			  EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_TRO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,
	.fdr_value	= 0x00000f0f,

	.trscer_err_mask = TRSCER_RMAFCE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.no_xdfar	= 1,
	.rmiimode	= 1,
	.magic		= 1,
};

/* R8A77980 */
static struct sh_eth_cpu_data r8a77980_data = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.register_type  = SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP |
			  ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP |
			  EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check       = EESR_FTC | EESR_CD | EESR_TRO,
	.eesr_err_check = EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RFE | EESR_RDE | EESR_RFRMER |
			  EESR_TFE | EESR_TDE | EESR_ECI,
	.fdr_value	= 0x0000070f,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.gecmr		= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.nbst		= 1,
	.rpadir		= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.csmr		= 1,
	.rx_csum	= 1,
	.select_mii	= 1,
	.magic		= 1,
	.cexcr		= 1,
};

/* R7S9210 */
static struct sh_eth_cpu_data r7s9210_data = {
	.soft_reset	= sh_eth_soft_reset,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_rcar,

	.register_type	= SH_ETH_REG_FAST_SH4,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.ecsr_value	= ECSR_ICD,
	.ecsipr_value	= ECSIPR_ICDIP,
	.eesipr_value	= EESIPR_TWBIP | EESIPR_TABTIP | EESIPR_RABTIP |
			  EESIPR_RFCOFIP | EESIPR_ECIIP | EESIPR_FTCIP |
			  EESIPR_TDEIP | EESIPR_TFUFIP | EESIPR_FRIP |
			  EESIPR_RDEIP | EESIPR_RFOFIP | EESIPR_CNDIP |
			  EESIPR_DLCIP | EESIPR_CDIP | EESIPR_TROIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP | EESIPR_RTLFIP |
			  EESIPR_RTSFIP | EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_TRO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,

	.fdr_value	= 0x0000070f,

	.trscer_err_mask = TRSCER_RMAFCE | TRSCER_RRFCE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
};
#endif /* CONFIG_OF */

static void sh_eth_set_rate_sh7724(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_modify(ndev, ECMR, ECMR_RTM, 0);
		break;
	case 100:/* 100BASE */
		sh_eth_modify(ndev, ECMR, ECMR_RTM, ECMR_RTM);
		break;
	}
}

/* SH7724 */
static struct sh_eth_cpu_data sh7724_data = {
	.soft_reset	= sh_eth_soft_reset,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_sh7724,

	.register_type	= SH_ETH_REG_FAST_SH4,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ADEIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_RMAFIP | EESIPR_RRFIP |
			  EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_TRO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
};

static void sh_eth_set_rate_sh7757(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_write(ndev, 0, RTRATE);
		break;
	case 100:/* 100BASE */
		sh_eth_write(ndev, 1, RTRATE);
		break;
	}
}

/* SH7757 */
static struct sh_eth_cpu_data sh7757_data = {
	.soft_reset	= sh_eth_soft_reset,

	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_sh7757,

	.register_type	= SH_ETH_REG_FAST_SH4,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  0x0000f000 | EESIPR_CNDIP | EESIPR_DLCIP |
			  EESIPR_CDIP | EESIPR_TROIP | EESIPR_RMAFIP |
			  EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_TRO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,

	.irq_flags	= IRQF_SHARED,
	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.no_ade		= 1,
	.rpadir		= 1,
	.rtrate		= 1,
	.dual_port	= 1,
};

#define SH_GIGA_ETH_BASE	0xfee00000UL
#define GIGA_MALR(port)		(SH_GIGA_ETH_BASE + 0x800 * (port) + 0x05c8)
#define GIGA_MAHR(port)		(SH_GIGA_ETH_BASE + 0x800 * (port) + 0x05c0)
static void sh_eth_chip_reset_giga(struct net_device *ndev)
{
	u32 mahr[2], malr[2];
	int i;

	/* save MAHR and MALR */
	for (i = 0; i < 2; i++) {
		malr[i] = ioread32((void *)GIGA_MALR(i));
		mahr[i] = ioread32((void *)GIGA_MAHR(i));
	}

	sh_eth_chip_reset(ndev);

	/* restore MAHR and MALR */
	for (i = 0; i < 2; i++) {
		iowrite32(malr[i], (void *)GIGA_MALR(i));
		iowrite32(mahr[i], (void *)GIGA_MAHR(i));
	}
}

static void sh_eth_set_rate_giga(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (WARN_ON(!mdp->cd->gecmr))
		return;

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_write(ndev, 0x00000000, GECMR);
		break;
	case 100:/* 100BASE */
		sh_eth_write(ndev, 0x00000010, GECMR);
		break;
	case 1000: /* 1000BASE */
		sh_eth_write(ndev, 0x00000020, GECMR);
		break;
	}
}

/* SH7757(GETHERC) */
static struct sh_eth_cpu_data sh7757_data_giga = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.chip_reset	= sh_eth_chip_reset_giga,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_giga,

	.register_type	= SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  0x0000f000 | EESIPR_CNDIP | EESIPR_DLCIP |
			  EESIPR_CDIP | EESIPR_TROIP | EESIPR_RMAFIP |
			  EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RFE | EESR_RDE | EESR_RFRMER | EESR_TFE |
			  EESR_TDE,
	.fdr_value	= 0x0000072f,

	.irq_flags	= IRQF_SHARED,
	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.gecmr		= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.tsu		= 1,
	.cexcr		= 1,
	.dual_port	= 1,
};

/* SH7734 */
static struct sh_eth_cpu_data sh7734_data = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.register_type	= SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_DLCIP | EESIPR_CDIP | EESIPR_TROIP |
			  EESIPR_RMAFIP | EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RFE | EESR_RDE | EESR_RFRMER | EESR_TFE |
			  EESR_TDE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.gecmr		= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.tsu		= 1,
	.csmr		= 1,
	.rx_csum	= 1,
	.select_mii	= 1,
	.magic		= 1,
	.cexcr		= 1,
};

/* SH7763 */
static struct sh_eth_cpu_data sh7763_data = {
	.soft_reset	= sh_eth_soft_reset_gether,

	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.register_type	= SH_ETH_REG_GIGABIT,

	.edtrr_trns	= EDTRR_TRNS_GETHER,
	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  EESIPR_DLCIP | EESIPR_CDIP | EESIPR_TROIP |
			  EESIPR_RMAFIP | EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT |
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.gecmr		= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.xdfar_rw	= 1,
	.tsu		= 1,
	.irq_flags	= IRQF_SHARED,
	.magic		= 1,
	.cexcr		= 1,
	.rx_csum	= 1,
	.dual_port	= 1,
};

static struct sh_eth_cpu_data sh7619_data = {
	.soft_reset	= sh_eth_soft_reset,

	.register_type	= SH_ETH_REG_FAST_SH3_SH2,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  0x0000f000 | EESIPR_CNDIP | EESIPR_DLCIP |
			  EESIPR_CDIP | EESIPR_TROIP | EESIPR_RMAFIP |
			  EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
};

static struct sh_eth_cpu_data sh771x_data = {
	.soft_reset	= sh_eth_soft_reset,

	.register_type	= SH_ETH_REG_FAST_SH3_SH2,

	.edtrr_trns	= EDTRR_TRNS_ETHER,
	.eesipr_value	= EESIPR_RFCOFIP | EESIPR_ECIIP |
			  EESIPR_FTCIP | EESIPR_TDEIP | EESIPR_TFUFIP |
			  EESIPR_FRIP | EESIPR_RDEIP | EESIPR_RFOFIP |
			  0x0000f000 | EESIPR_CNDIP | EESIPR_DLCIP |
			  EESIPR_CDIP | EESIPR_TROIP | EESIPR_RMAFIP |
			  EESIPR_CEEFIP | EESIPR_CELFIP |
			  EESIPR_RRFIP | EESIPR_RTLFIP | EESIPR_RTSFIP |
			  EESIPR_PREIP | EESIPR_CERFIP,

	.trscer_err_mask = TRSCER_RMAFCE,

	.tsu		= 1,
	.dual_port	= 1,
};

static void sh_eth_set_default_cpu_data(struct sh_eth_cpu_data *cd)
{
	if (!cd->ecsr_value)
		cd->ecsr_value = DEFAULT_ECSR_INIT;

	if (!cd->ecsipr_value)
		cd->ecsipr_value = DEFAULT_ECSIPR_INIT;

	if (!cd->fcftr_value)
		cd->fcftr_value = DEFAULT_FIFO_F_D_RFF |
				  DEFAULT_FIFO_F_D_RFD;

	if (!cd->fdr_value)
		cd->fdr_value = DEFAULT_FDR_INIT;

	if (!cd->tx_check)
		cd->tx_check = DEFAULT_TX_CHECK;

	if (!cd->eesr_err_check)
		cd->eesr_err_check = DEFAULT_EESR_ERR_CHECK;

	if (!cd->trscer_err_mask)
		cd->trscer_err_mask = DEFAULT_TRSCER_ERR_MASK;
}

static void sh_eth_set_receive_align(struct sk_buff *skb)
{
	uintptr_t reserve = (uintptr_t)skb->data & (SH_ETH_RX_ALIGN - 1);

	if (reserve)
		skb_reserve(skb, SH_ETH_RX_ALIGN - reserve);
}

/* Program the hardware MAC address from dev->dev_addr. */
static void update_mac_address(struct net_device *ndev)
{
	sh_eth_write(ndev,
		     (ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		     (ndev->dev_addr[2] << 8) | (ndev->dev_addr[3]), MAHR);
	sh_eth_write(ndev,
		     (ndev->dev_addr[4] << 8) | (ndev->dev_addr[5]), MALR);
}

/* Get MAC address from SuperH MAC address register
 *
 * SuperH's Ethernet device doesn't have 'ROM' to MAC address.
 * This driver get MAC address that use by bootloader(U-boot or sh-ipl+g).
 * When you want use this device, you must set MAC address in bootloader.
 *
 */
static void read_mac_address(struct net_device *ndev, unsigned char *mac)
{
	if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
		eth_hw_addr_set(ndev, mac);
	} else {
		u32 mahr = sh_eth_read(ndev, MAHR);
		u32 malr = sh_eth_read(ndev, MALR);
		u8 addr[ETH_ALEN];

		addr[0] = (mahr >> 24) & 0xFF;
		addr[1] = (mahr >> 16) & 0xFF;
		addr[2] = (mahr >>  8) & 0xFF;
		addr[3] = (mahr >>  0) & 0xFF;
		addr[4] = (malr >>  8) & 0xFF;
		addr[5] = (malr >>  0) & 0xFF;
		eth_hw_addr_set(ndev, addr);
	}
}

struct bb_info {
	void (*set_gate)(void *addr);
	struct mdiobb_ctrl ctrl;
	void *addr;
};

static void sh_mdio_ctrl(struct mdiobb_ctrl *ctrl, u32 mask, int set)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);
	u32 pir;

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	pir = ioread32(bitbang->addr);
	if (set)
		pir |=  mask;
	else
		pir &= ~mask;
	iowrite32(pir, bitbang->addr);
}

/* Data I/O pin control */
static void sh_mmd_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	sh_mdio_ctrl(ctrl, PIR_MMD, bit);
}

/* Set bit data*/
static void sh_set_mdio(struct mdiobb_ctrl *ctrl, int bit)
{
	sh_mdio_ctrl(ctrl, PIR_MDO, bit);
}

/* Get bit data*/
static int sh_get_mdio(struct mdiobb_ctrl *ctrl)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	return (ioread32(bitbang->addr) & PIR_MDI) != 0;
}

/* MDC pin control */
static void sh_mdc_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	sh_mdio_ctrl(ctrl, PIR_MDC, bit);
}

/* mdio bus control struct */
static const struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = sh_mdc_ctrl,
	.set_mdio_dir = sh_mmd_ctrl,
	.set_mdio_data = sh_set_mdio,
	.get_mdio_data = sh_get_mdio,
};

/* free Tx skb function */
static int sh_eth_tx_free(struct net_device *ndev, bool sent_only)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	int free_num = 0;
	int entry;
	bool sent;

	for (; mdp->cur_tx - mdp->dirty_tx > 0; mdp->dirty_tx++) {
		entry = mdp->dirty_tx % mdp->num_tx_ring;
		txdesc = &mdp->tx_ring[entry];
		sent = !(txdesc->status & cpu_to_le32(TD_TACT));
		if (sent_only && !sent)
			break;
		/* TACT bit must be checked before all the following reads */
		dma_rmb();
		netif_info(mdp, tx_done, ndev,
			   "tx entry %d status 0x%08x\n",
			   entry, le32_to_cpu(txdesc->status));
		/* Free the original skb. */
		if (mdp->tx_skbuff[entry]) {
			dma_unmap_single(&mdp->pdev->dev,
					 le32_to_cpu(txdesc->addr),
					 le32_to_cpu(txdesc->len) >> 16,
					 DMA_TO_DEVICE);
			dev_kfree_skb_irq(mdp->tx_skbuff[entry]);
			mdp->tx_skbuff[entry] = NULL;
			free_num++;
		}
		txdesc->status = cpu_to_le32(TD_TFP);
		if (entry >= mdp->num_tx_ring - 1)
			txdesc->status |= cpu_to_le32(TD_TDLE);

		if (sent) {
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += le32_to_cpu(txdesc->len) >> 16;
		}
	}
	return free_num;
}

/* free skb and descriptor buffer */
static void sh_eth_ring_free(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ringsize, i;

	if (mdp->rx_ring) {
		for (i = 0; i < mdp->num_rx_ring; i++) {
			if (mdp->rx_skbuff[i]) {
				struct sh_eth_rxdesc *rxdesc = &mdp->rx_ring[i];

				dma_unmap_single(&mdp->pdev->dev,
						 le32_to_cpu(rxdesc->addr),
						 ALIGN(mdp->rx_buf_sz, 32),
						 DMA_FROM_DEVICE);
			}
		}
		ringsize = sizeof(struct sh_eth_rxdesc) * mdp->num_rx_ring;
		dma_free_coherent(&mdp->pdev->dev, ringsize, mdp->rx_ring,
				  mdp->rx_desc_dma);
		mdp->rx_ring = NULL;
	}

	/* Free Rx skb ringbuffer */
	if (mdp->rx_skbuff) {
		for (i = 0; i < mdp->num_rx_ring; i++)
			dev_kfree_skb(mdp->rx_skbuff[i]);
	}
	kfree(mdp->rx_skbuff);
	mdp->rx_skbuff = NULL;

	if (mdp->tx_ring) {
		sh_eth_tx_free(ndev, false);

		ringsize = sizeof(struct sh_eth_txdesc) * mdp->num_tx_ring;
		dma_free_coherent(&mdp->pdev->dev, ringsize, mdp->tx_ring,
				  mdp->tx_desc_dma);
		mdp->tx_ring = NULL;
	}

	/* Free Tx skb ringbuffer */
	kfree(mdp->tx_skbuff);
	mdp->tx_skbuff = NULL;
}

/* format skb and descriptor buffer */
static void sh_eth_ring_format(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i;
	struct sk_buff *skb;
	struct sh_eth_rxdesc *rxdesc = NULL;
	struct sh_eth_txdesc *txdesc = NULL;
	int rx_ringsize = sizeof(*rxdesc) * mdp->num_rx_ring;
	int tx_ringsize = sizeof(*txdesc) * mdp->num_tx_ring;
	int skbuff_size = mdp->rx_buf_sz + SH_ETH_RX_ALIGN + 32 - 1;
	dma_addr_t dma_addr;
	u32 buf_len;

	mdp->cur_rx = 0;
	mdp->cur_tx = 0;
	mdp->dirty_rx = 0;
	mdp->dirty_tx = 0;

	memset(mdp->rx_ring, 0, rx_ringsize);

	/* build Rx ring buffer */
	for (i = 0; i < mdp->num_rx_ring; i++) {
		/* skb */
		mdp->rx_skbuff[i] = NULL;
		skb = netdev_alloc_skb(ndev, skbuff_size);
		if (skb == NULL)
			break;
		sh_eth_set_receive_align(skb);

		/* The size of the buffer is a multiple of 32 bytes. */
		buf_len = ALIGN(mdp->rx_buf_sz, 32);
		dma_addr = dma_map_single(&mdp->pdev->dev, skb->data, buf_len,
					  DMA_FROM_DEVICE);
		if (dma_mapping_error(&mdp->pdev->dev, dma_addr)) {
			kfree_skb(skb);
			break;
		}
		mdp->rx_skbuff[i] = skb;

		/* RX descriptor */
		rxdesc = &mdp->rx_ring[i];
		rxdesc->len = cpu_to_le32(buf_len << 16);
		rxdesc->addr = cpu_to_le32(dma_addr);
		rxdesc->status = cpu_to_le32(RD_RACT | RD_RFP);

		/* Rx descriptor address set */
		if (i == 0) {
			sh_eth_write(ndev, mdp->rx_desc_dma, RDLAR);
			if (mdp->cd->xdfar_rw)
				sh_eth_write(ndev, mdp->rx_desc_dma, RDFAR);
		}
	}

	mdp->dirty_rx = (u32) (i - mdp->num_rx_ring);

	/* Mark the last entry as wrapping the ring. */
	if (rxdesc)
		rxdesc->status |= cpu_to_le32(RD_RDLE);

	memset(mdp->tx_ring, 0, tx_ringsize);

	/* build Tx ring buffer */
	for (i = 0; i < mdp->num_tx_ring; i++) {
		mdp->tx_skbuff[i] = NULL;
		txdesc = &mdp->tx_ring[i];
		txdesc->status = cpu_to_le32(TD_TFP);
		txdesc->len = cpu_to_le32(0);
		if (i == 0) {
			/* Tx descriptor address set */
			sh_eth_write(ndev, mdp->tx_desc_dma, TDLAR);
			if (mdp->cd->xdfar_rw)
				sh_eth_write(ndev, mdp->tx_desc_dma, TDFAR);
		}
	}

	txdesc->status |= cpu_to_le32(TD_TDLE);
}

/* Get skb and descriptor buffer */
static int sh_eth_ring_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int rx_ringsize, tx_ringsize;

	/* +26 gets the maximum ethernet encapsulation, +7 & ~7 because the
	 * card needs room to do 8 byte alignment, +2 so we can reserve
	 * the first 2 bytes, and +16 gets room for the status word from the
	 * card.
	 */
	mdp->rx_buf_sz = (ndev->mtu <= 1492 ? PKT_BUF_SZ :
			  (((ndev->mtu + 26 + 7) & ~7) + 2 + 16));
	if (mdp->cd->rpadir)
		mdp->rx_buf_sz += NET_IP_ALIGN;

	/* Allocate RX and TX skb rings */
	mdp->rx_skbuff = kcalloc(mdp->num_rx_ring, sizeof(*mdp->rx_skbuff),
				 GFP_KERNEL);
	if (!mdp->rx_skbuff)
		return -ENOMEM;

	mdp->tx_skbuff = kcalloc(mdp->num_tx_ring, sizeof(*mdp->tx_skbuff),
				 GFP_KERNEL);
	if (!mdp->tx_skbuff)
		goto ring_free;

	/* Allocate all Rx descriptors. */
	rx_ringsize = sizeof(struct sh_eth_rxdesc) * mdp->num_rx_ring;
	mdp->rx_ring = dma_alloc_coherent(&mdp->pdev->dev, rx_ringsize,
					  &mdp->rx_desc_dma, GFP_KERNEL);
	if (!mdp->rx_ring)
		goto ring_free;

	mdp->dirty_rx = 0;

	/* Allocate all Tx descriptors. */
	tx_ringsize = sizeof(struct sh_eth_txdesc) * mdp->num_tx_ring;
	mdp->tx_ring = dma_alloc_coherent(&mdp->pdev->dev, tx_ringsize,
					  &mdp->tx_desc_dma, GFP_KERNEL);
	if (!mdp->tx_ring)
		goto ring_free;
	return 0;

ring_free:
	/* Free Rx and Tx skb ring buffer and DMA buffer */
	sh_eth_ring_free(ndev);

	return -ENOMEM;
}

static int sh_eth_dev_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	/* Soft Reset */
	ret = mdp->cd->soft_reset(ndev);
	if (ret)
		return ret;

	if (mdp->cd->rmiimode)
		sh_eth_write(ndev, 0x1, RMIIMODE);

	/* Descriptor format */
	sh_eth_ring_format(ndev);
	if (mdp->cd->rpadir)
		sh_eth_write(ndev, NET_IP_ALIGN << 16, RPADIR);

	/* all sh_eth int mask */
	sh_eth_write(ndev, 0, EESIPR);

#if defined(__LITTLE_ENDIAN)
	if (mdp->cd->hw_swap)
		sh_eth_write(ndev, EDMR_EL, EDMR);
	else
#endif
		sh_eth_write(ndev, 0, EDMR);

	/* FIFO size set */
	sh_eth_write(ndev, mdp->cd->fdr_value, FDR);
	sh_eth_write(ndev, 0, TFTR);

	/* Frame recv control (enable multiple-packets per rx irq) */
	sh_eth_write(ndev, RMCR_RNC, RMCR);

	sh_eth_write(ndev, mdp->cd->trscer_err_mask, TRSCER);

	/* DMA transfer burst mode */
	if (mdp->cd->nbst)
		sh_eth_modify(ndev, EDMR, EDMR_NBST, EDMR_NBST);

	/* Burst cycle count upper-limit */
	if (mdp->cd->bculr)
		sh_eth_write(ndev, 0x800, BCULR);

	sh_eth_write(ndev, mdp->cd->fcftr_value, FCFTR);

	if (!mdp->cd->no_trimd)
		sh_eth_write(ndev, 0, TRIMD);

	/* Recv frame limit set register */
	sh_eth_write(ndev, ndev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN,
		     RFLR);

	sh_eth_modify(ndev, EESR, 0, 0);
	mdp->irq_enabled = true;
	sh_eth_write(ndev, mdp->cd->eesipr_value, EESIPR);

	/* EMAC Mode: PAUSE prohibition; Duplex; RX Checksum; TX; RX */
	sh_eth_write(ndev, ECMR_ZPF | (mdp->duplex ? ECMR_DM : 0) |
		     (ndev->features & NETIF_F_RXCSUM ? ECMR_RCSC : 0) |
		     ECMR_TE | ECMR_RE, ECMR);

	if (mdp->cd->set_rate)
		mdp->cd->set_rate(ndev);

	/* E-MAC Status Register clear */
	sh_eth_write(ndev, mdp->cd->ecsr_value, ECSR);

	/* E-MAC Interrupt Enable register */
	sh_eth_write(ndev, mdp->cd->ecsipr_value, ECSIPR);

	/* Set MAC address */
	update_mac_address(ndev);

	/* mask reset */
	if (mdp->cd->apr)
		sh_eth_write(ndev, 1, APR);
	if (mdp->cd->mpr)
		sh_eth_write(ndev, 1, MPR);
	if (mdp->cd->tpauser)
		sh_eth_write(ndev, TPAUSER_UNLIMITED, TPAUSER);

	/* Setting the Rx mode will start the Rx process. */
	sh_eth_write(ndev, EDRRR_R, EDRRR);

	return ret;
}

static void sh_eth_dev_exit(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i;

	/* Deactivate all TX descriptors, so DMA should stop at next
	 * packet boundary if it's currently running
	 */
	for (i = 0; i < mdp->num_tx_ring; i++)
		mdp->tx_ring[i].status &= ~cpu_to_le32(TD_TACT);

	/* Disable TX FIFO egress to MAC */
	sh_eth_rcv_snd_disable(ndev);

	/* Stop RX DMA at next packet boundary */
	sh_eth_write(ndev, 0, EDRRR);

	/* Aside from TX DMA, we can't tell when the hardware is
	 * really stopped, so we need to reset to make sure.
	 * Before doing that, wait for long enough to *probably*
	 * finish transmitting the last packet and poll stats.
	 */
	msleep(2); /* max frame time at 10 Mbps < 1250 us */
	sh_eth_get_stats(ndev);
	mdp->cd->soft_reset(ndev);

	/* Set the RMII mode again if required */
	if (mdp->cd->rmiimode)
		sh_eth_write(ndev, 0x1, RMIIMODE);

	/* Set MAC address again */
	update_mac_address(ndev);
}

static void sh_eth_rx_csum(struct sk_buff *skb)
{
	u8 *hw_csum;

	/* The hardware checksum is 2 bytes appended to packet data */
	if (unlikely(skb->len < sizeof(__sum16)))
		return;
	hw_csum = skb_tail_pointer(skb) - sizeof(__sum16);
	skb->csum = csum_unfold((__force __sum16)get_unaligned_le16(hw_csum));
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb_trim(skb, skb->len - sizeof(__sum16));
}

/* Packet receive function */
static int sh_eth_rx(struct net_device *ndev, u32 intr_status, int *quota)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_rxdesc *rxdesc;

	int entry = mdp->cur_rx % mdp->num_rx_ring;
	int boguscnt = (mdp->dirty_rx + mdp->num_rx_ring) - mdp->cur_rx;
	int limit;
	struct sk_buff *skb;
	u32 desc_status;
	int skbuff_size = mdp->rx_buf_sz + SH_ETH_RX_ALIGN + 32 - 1;
	dma_addr_t dma_addr;
	u16 pkt_len;
	u32 buf_len;

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;
	rxdesc = &mdp->rx_ring[entry];
	while (!(rxdesc->status & cpu_to_le32(RD_RACT))) {
		/* RACT bit must be checked before all the following reads */
		dma_rmb();
		desc_status = le32_to_cpu(rxdesc->status);
		pkt_len = le32_to_cpu(rxdesc->len) & RD_RFL;

		if (--boguscnt < 0)
			break;

		netif_info(mdp, rx_status, ndev,
			   "rx entry %d status 0x%08x len %d\n",
			   entry, desc_status, pkt_len);

		if (!(desc_status & RDFEND))
			ndev->stats.rx_length_errors++;

		/* In case of almost all GETHER/ETHERs, the Receive Frame State
		 * (RFS) bits in the Receive Descriptor 0 are from bit 9 to
		 * bit 0. However, in case of the R8A7740 and R7S72100
		 * the RFS bits are from bit 25 to bit 16. So, the
		 * driver needs right shifting by 16.
		 */
		if (mdp->cd->csmr)
			desc_status >>= 16;

		skb = mdp->rx_skbuff[entry];
		if (desc_status & (RD_RFS1 | RD_RFS2 | RD_RFS3 | RD_RFS4 |
				   RD_RFS5 | RD_RFS6 | RD_RFS10)) {
			ndev->stats.rx_errors++;
			if (desc_status & RD_RFS1)
				ndev->stats.rx_crc_errors++;
			if (desc_status & RD_RFS2)
				ndev->stats.rx_frame_errors++;
			if (desc_status & RD_RFS3)
				ndev->stats.rx_length_errors++;
			if (desc_status & RD_RFS4)
				ndev->stats.rx_length_errors++;
			if (desc_status & RD_RFS6)
				ndev->stats.rx_missed_errors++;
			if (desc_status & RD_RFS10)
				ndev->stats.rx_over_errors++;
		} else	if (skb) {
			dma_addr = le32_to_cpu(rxdesc->addr);
			if (!mdp->cd->hw_swap)
				sh_eth_soft_swap(
					phys_to_virt(ALIGN(dma_addr, 4)),
					pkt_len + 2);
			mdp->rx_skbuff[entry] = NULL;
			if (mdp->cd->rpadir)
				skb_reserve(skb, NET_IP_ALIGN);
			dma_unmap_single(&mdp->pdev->dev, dma_addr,
					 ALIGN(mdp->rx_buf_sz, 32),
					 DMA_FROM_DEVICE);
			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, ndev);
			if (ndev->features & NETIF_F_RXCSUM)
				sh_eth_rx_csum(skb);
			netif_receive_skb(skb);
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += pkt_len;
			if (desc_status & RD_RFS8)
				ndev->stats.multicast++;
		}
		entry = (++mdp->cur_rx) % mdp->num_rx_ring;
		rxdesc = &mdp->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; mdp->cur_rx - mdp->dirty_rx > 0; mdp->dirty_rx++) {
		entry = mdp->dirty_rx % mdp->num_rx_ring;
		rxdesc = &mdp->rx_ring[entry];
		/* The size of the buffer is 32 byte boundary. */
		buf_len = ALIGN(mdp->rx_buf_sz, 32);
		rxdesc->len = cpu_to_le32(buf_len << 16);

		if (mdp->rx_skbuff[entry] == NULL) {
			skb = netdev_alloc_skb(ndev, skbuff_size);
			if (skb == NULL)
				break;	/* Better luck next round. */
			sh_eth_set_receive_align(skb);
			dma_addr = dma_map_single(&mdp->pdev->dev, skb->data,
						  buf_len, DMA_FROM_DEVICE);
			if (dma_mapping_error(&mdp->pdev->dev, dma_addr)) {
				kfree_skb(skb);
				break;
			}
			mdp->rx_skbuff[entry] = skb;

			skb_checksum_none_assert(skb);
			rxdesc->addr = cpu_to_le32(dma_addr);
		}
		dma_wmb(); /* RACT bit must be set after all the above writes */
		if (entry >= mdp->num_rx_ring - 1)
			rxdesc->status |=
				cpu_to_le32(RD_RACT | RD_RFP | RD_RDLE);
		else
			rxdesc->status |= cpu_to_le32(RD_RACT | RD_RFP);
	}

	/* Restart Rx engine if stopped. */
	/* If we don't need to check status, don't. -KDU */
	if (!(sh_eth_read(ndev, EDRRR) & EDRRR_R)) {
		/* fix the values for the next receiving if RDE is set */
		if (intr_status & EESR_RDE && !mdp->cd->no_xdfar) {
			u32 count = (sh_eth_read(ndev, RDFAR) -
				     sh_eth_read(ndev, RDLAR)) >> 4;

			mdp->cur_rx = count;
			mdp->dirty_rx = count;
		}
		sh_eth_write(ndev, EDRRR_R, EDRRR);
	}

	*quota -= limit - boguscnt - 1;

	return *quota <= 0;
}

static void sh_eth_rcv_snd_disable(struct net_device *ndev)
{
	/* disable tx and rx */
	sh_eth_modify(ndev, ECMR, ECMR_RE | ECMR_TE, 0);
}

static void sh_eth_rcv_snd_enable(struct net_device *ndev)
{
	/* enable tx and rx */
	sh_eth_modify(ndev, ECMR, ECMR_RE | ECMR_TE, ECMR_RE | ECMR_TE);
}

/* E-MAC interrupt handler */
static void sh_eth_emac_interrupt(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 felic_stat;
	u32 link_stat;

	felic_stat = sh_eth_read(ndev, ECSR) & sh_eth_read(ndev, ECSIPR);
	sh_eth_write(ndev, felic_stat, ECSR);	/* clear int */
	if (felic_stat & ECSR_ICD)
		ndev->stats.tx_carrier_errors++;
	if (felic_stat & ECSR_MPD)
		pm_wakeup_event(&mdp->pdev->dev, 0);
	if (felic_stat & ECSR_LCHNG) {
		/* Link Changed */
		if (mdp->cd->no_psr || mdp->no_ether_link)
			return;
		link_stat = sh_eth_read(ndev, PSR);
		if (mdp->ether_link_active_low)
			link_stat = ~link_stat;
		if (!(link_stat & PSR_LMON)) {
			sh_eth_rcv_snd_disable(ndev);
		} else {
			/* Link Up */
			sh_eth_modify(ndev, EESIPR, EESIPR_ECIIP, 0);
			/* clear int */
			sh_eth_modify(ndev, ECSR, 0, 0);
			sh_eth_modify(ndev, EESIPR, EESIPR_ECIIP, EESIPR_ECIIP);
			/* enable tx and rx */
			sh_eth_rcv_snd_enable(ndev);
		}
	}
}

/* error control function */
static void sh_eth_error(struct net_device *ndev, u32 intr_status)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 mask;

	if (intr_status & EESR_TWB) {
		/* Unused write back interrupt */
		if (intr_status & EESR_TABT) {	/* Transmit Abort int */
			ndev->stats.tx_aborted_errors++;
			netif_err(mdp, tx_err, ndev, "Transmit Abort\n");
		}
	}

	if (intr_status & EESR_RABT) {
		/* Receive Abort int */
		if (intr_status & EESR_RFRMER) {
			/* Receive Frame Overflow int */
			ndev->stats.rx_frame_errors++;
		}
	}

	if (intr_status & EESR_TDE) {
		/* Transmit Descriptor Empty int */
		ndev->stats.tx_fifo_errors++;
		netif_err(mdp, tx_err, ndev, "Transmit Descriptor Empty\n");
	}

	if (intr_status & EESR_TFE) {
		/* FIFO under flow */
		ndev->stats.tx_fifo_errors++;
		netif_err(mdp, tx_err, ndev, "Transmit FIFO Under flow\n");
	}

	if (intr_status & EESR_RDE) {
		/* Receive Descriptor Empty int */
		ndev->stats.rx_over_errors++;
	}

	if (intr_status & EESR_RFE) {
		/* Receive FIFO Overflow int */
		ndev->stats.rx_fifo_errors++;
	}

	if (!mdp->cd->no_ade && (intr_status & EESR_ADE)) {
		/* Address Error */
		ndev->stats.tx_fifo_errors++;
		netif_err(mdp, tx_err, ndev, "Address Error\n");
	}

	mask = EESR_TWB | EESR_TABT | EESR_ADE | EESR_TDE | EESR_TFE;
	if (mdp->cd->no_ade)
		mask &= ~EESR_ADE;
	if (intr_status & mask) {
		/* Tx error */
		u32 edtrr = sh_eth_read(ndev, EDTRR);

		/* dmesg */
		netdev_err(ndev, "TX error. status=%8.8x cur_tx=%8.8x dirty_tx=%8.8x state=%8.8x EDTRR=%8.8x.\n",
			   intr_status, mdp->cur_tx, mdp->dirty_tx,
			   (u32)ndev->state, edtrr);
		/* dirty buffer free */
		sh_eth_tx_free(ndev, true);

		/* SH7712 BUG */
		if (edtrr ^ mdp->cd->edtrr_trns) {
			/* tx dma start */
			sh_eth_write(ndev, mdp->cd->edtrr_trns, EDTRR);
		}
		/* wakeup */
		netif_wake_queue(ndev);
	}
}

static irqreturn_t sh_eth_interrupt(int irq, void *netdev)
{
	struct net_device *ndev = netdev;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_cpu_data *cd = mdp->cd;
	irqreturn_t ret = IRQ_NONE;
	u32 intr_status, intr_enable;

	spin_lock(&mdp->lock);

	/* Get interrupt status */
	intr_status = sh_eth_read(ndev, EESR);
	/* Mask it with the interrupt mask, forcing ECI interrupt  to be always
	 * enabled since it's the one that  comes  thru regardless of the mask,
	 * and  we need to fully handle it  in sh_eth_emac_interrupt() in order
	 * to quench it as it doesn't get cleared by just writing 1 to the  ECI
	 * bit...
	 */
	intr_enable = sh_eth_read(ndev, EESIPR);
	intr_status &= intr_enable | EESIPR_ECIIP;
	if (intr_status & (EESR_RX_CHECK | cd->tx_check | EESR_ECI |
			   cd->eesr_err_check))
		ret = IRQ_HANDLED;
	else
		goto out;

	if (unlikely(!mdp->irq_enabled)) {
		sh_eth_write(ndev, 0, EESIPR);
		goto out;
	}

	if (intr_status & EESR_RX_CHECK) {
		if (napi_schedule_prep(&mdp->napi)) {
			/* Mask Rx interrupts */
			sh_eth_write(ndev, intr_enable & ~EESR_RX_CHECK,
				     EESIPR);
			__napi_schedule(&mdp->napi);
		} else {
			netdev_warn(ndev,
				    "ignoring interrupt, status 0x%08x, mask 0x%08x.\n",
				    intr_status, intr_enable);
		}
	}

	/* Tx Check */
	if (intr_status & cd->tx_check) {
		/* Clear Tx interrupts */
		sh_eth_write(ndev, intr_status & cd->tx_check, EESR);

		sh_eth_tx_free(ndev, true);
		netif_wake_queue(ndev);
	}

	/* E-MAC interrupt */
	if (intr_status & EESR_ECI)
		sh_eth_emac_interrupt(ndev);

	if (intr_status & cd->eesr_err_check) {
		/* Clear error interrupts */
		sh_eth_write(ndev, intr_status & cd->eesr_err_check, EESR);

		sh_eth_error(ndev, intr_status);
	}

out:
	spin_unlock(&mdp->lock);

	return ret;
}

static int sh_eth_poll(struct napi_struct *napi, int budget)
{
	struct sh_eth_private *mdp = container_of(napi, struct sh_eth_private,
						  napi);
	struct net_device *ndev = napi->dev;
	int quota = budget;
	u32 intr_status;

	for (;;) {
		intr_status = sh_eth_read(ndev, EESR);
		if (!(intr_status & EESR_RX_CHECK))
			break;
		/* Clear Rx interrupts */
		sh_eth_write(ndev, intr_status & EESR_RX_CHECK, EESR);

		if (sh_eth_rx(ndev, intr_status, &quota))
			goto out;
	}

	napi_complete(napi);

	/* Reenable Rx interrupts */
	if (mdp->irq_enabled)
		sh_eth_write(ndev, mdp->cd->eesipr_value, EESIPR);
out:
	return budget - quota;
}

/* PHY state control function */
static void sh_eth_adjust_link(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	unsigned long flags;
	int new_state = 0;

	spin_lock_irqsave(&mdp->lock, flags);

	/* Disable TX and RX right over here, if E-MAC change is ignored */
	if (mdp->cd->no_psr || mdp->no_ether_link)
		sh_eth_rcv_snd_disable(ndev);

	if (phydev->link) {
		if (phydev->duplex != mdp->duplex) {
			new_state = 1;
			mdp->duplex = phydev->duplex;
			if (mdp->cd->set_duplex)
				mdp->cd->set_duplex(ndev);
		}

		if (phydev->speed != mdp->speed) {
			new_state = 1;
			mdp->speed = phydev->speed;
			if (mdp->cd->set_rate)
				mdp->cd->set_rate(ndev);
		}
		if (!mdp->link) {
			sh_eth_modify(ndev, ECMR, ECMR_TXF, 0);
			new_state = 1;
			mdp->link = phydev->link;
		}
	} else if (mdp->link) {
		new_state = 1;
		mdp->link = 0;
		mdp->speed = 0;
		mdp->duplex = -1;
	}

	/* Enable TX and RX right over here, if E-MAC change is ignored */
	if ((mdp->cd->no_psr || mdp->no_ether_link) && phydev->link)
		sh_eth_rcv_snd_enable(ndev);

	spin_unlock_irqrestore(&mdp->lock, flags);

	if (new_state && netif_msg_link(mdp))
		phy_print_status(phydev);
}

/* PHY init function */
static int sh_eth_phy_init(struct net_device *ndev)
{
	struct device_node *np = ndev->dev.parent->of_node;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev;

	mdp->link = 0;
	mdp->speed = 0;
	mdp->duplex = -1;

	/* Try connect to PHY */
	if (np) {
		struct device_node *pn;

		pn = of_parse_phandle(np, "phy-handle", 0);
		phydev = of_phy_connect(ndev, pn,
					sh_eth_adjust_link, 0,
					mdp->phy_interface);

		of_node_put(pn);
		if (!phydev)
			phydev = ERR_PTR(-ENOENT);
	} else {
		char phy_id[MII_BUS_ID_SIZE + 3];

		snprintf(phy_id, sizeof(phy_id), PHY_ID_FMT,
			 mdp->mii_bus->id, mdp->phy_id);

		phydev = phy_connect(ndev, phy_id, sh_eth_adjust_link,
				     mdp->phy_interface);
	}

	if (IS_ERR(phydev)) {
		netdev_err(ndev, "failed to connect PHY\n");
		return PTR_ERR(phydev);
	}

	/* mask with MAC supported features */
	if (mdp->cd->register_type != SH_ETH_REG_GIGABIT) {
		int err = phy_set_max_speed(phydev, SPEED_100);
		if (err) {
			netdev_err(ndev, "failed to limit PHY to 100 Mbit/s\n");
			phy_disconnect(phydev);
			return err;
		}
	}

	phy_attached_info(phydev);

	return 0;
}

/* PHY control start function */
static int sh_eth_phy_start(struct net_device *ndev)
{
	int ret;

	ret = sh_eth_phy_init(ndev);
	if (ret)
		return ret;

	phy_start(ndev->phydev);

	return 0;
}

/* If it is ever necessary to increase SH_ETH_REG_DUMP_MAX_REGS, the
 * version must be bumped as well.  Just adding registers up to that
 * limit is fine, as long as the existing register indices don't
 * change.
 */
#define SH_ETH_REG_DUMP_VERSION		1
#define SH_ETH_REG_DUMP_MAX_REGS	256

static size_t __sh_eth_get_regs(struct net_device *ndev, u32 *buf)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_cpu_data *cd = mdp->cd;
	u32 *valid_map;
	size_t len;

	BUILD_BUG_ON(SH_ETH_MAX_REGISTER_OFFSET > SH_ETH_REG_DUMP_MAX_REGS);

	/* Dump starts with a bitmap that tells ethtool which
	 * registers are defined for this chip.
	 */
	len = DIV_ROUND_UP(SH_ETH_REG_DUMP_MAX_REGS, 32);
	if (buf) {
		valid_map = buf;
		buf += len;
	} else {
		valid_map = NULL;
	}

	/* Add a register to the dump, if it has a defined offset.
	 * This automatically skips most undefined registers, but for
	 * some it is also necessary to check a capability flag in
	 * struct sh_eth_cpu_data.
	 */
#define mark_reg_valid(reg) valid_map[reg / 32] |= 1U << (reg % 32)
#define add_reg_from(reg, read_expr) do {				\
		if (mdp->reg_offset[reg] != SH_ETH_OFFSET_INVALID) {	\
			if (buf) {					\
				mark_reg_valid(reg);			\
				*buf++ = read_expr;			\
			}						\
			++len;						\
		}							\
	} while (0)
#define add_reg(reg) add_reg_from(reg, sh_eth_read(ndev, reg))
#define add_tsu_reg(reg) add_reg_from(reg, sh_eth_tsu_read(mdp, reg))

	add_reg(EDSR);
	add_reg(EDMR);
	add_reg(EDTRR);
	add_reg(EDRRR);
	add_reg(EESR);
	add_reg(EESIPR);
	add_reg(TDLAR);
	if (!cd->no_xdfar)
		add_reg(TDFAR);
	add_reg(TDFXR);
	add_reg(TDFFR);
	add_reg(RDLAR);
	if (!cd->no_xdfar)
		add_reg(RDFAR);
	add_reg(RDFXR);
	add_reg(RDFFR);
	add_reg(TRSCER);
	add_reg(RMFCR);
	add_reg(TFTR);
	add_reg(FDR);
	add_reg(RMCR);
	add_reg(TFUCR);
	add_reg(RFOCR);
	if (cd->rmiimode)
		add_reg(RMIIMODE);
	add_reg(FCFTR);
	if (cd->rpadir)
		add_reg(RPADIR);
	if (!cd->no_trimd)
		add_reg(TRIMD);
	add_reg(ECMR);
	add_reg(ECSR);
	add_reg(ECSIPR);
	add_reg(PIR);
	if (!cd->no_psr)
		add_reg(PSR);
	add_reg(RDMLR);
	add_reg(RFLR);
	add_reg(IPGR);
	if (cd->apr)
		add_reg(APR);
	if (cd->mpr)
		add_reg(MPR);
	add_reg(RFCR);
	add_reg(RFCF);
	if (cd->tpauser)
		add_reg(TPAUSER);
	add_reg(TPAUSECR);
	if (cd->gecmr)
		add_reg(GECMR);
	if (cd->bculr)
		add_reg(BCULR);
	add_reg(MAHR);
	add_reg(MALR);
	if (!cd->no_tx_cntrs) {
		add_reg(TROCR);
		add_reg(CDCR);
		add_reg(LCCR);
		add_reg(CNDCR);
	}
	add_reg(CEFCR);
	add_reg(FRECR);
	add_reg(TSFRCR);
	add_reg(TLFRCR);
	if (cd->cexcr) {
		add_reg(CERCR);
		add_reg(CEECR);
	}
	add_reg(MAFCR);
	if (cd->rtrate)
		add_reg(RTRATE);
	if (cd->csmr)
		add_reg(CSMR);
	if (cd->select_mii)
		add_reg(RMII_MII);
	if (cd->tsu) {
		add_tsu_reg(ARSTR);
		add_tsu_reg(TSU_CTRST);
		if (cd->dual_port) {
			add_tsu_reg(TSU_FWEN0);
			add_tsu_reg(TSU_FWEN1);
			add_tsu_reg(TSU_FCM);
			add_tsu_reg(TSU_BSYSL0);
			add_tsu_reg(TSU_BSYSL1);
			add_tsu_reg(TSU_PRISL0);
			add_tsu_reg(TSU_PRISL1);
			add_tsu_reg(TSU_FWSL0);
			add_tsu_reg(TSU_FWSL1);
		}
		add_tsu_reg(TSU_FWSLC);
		if (cd->dual_port) {
			add_tsu_reg(TSU_QTAGM0);
			add_tsu_reg(TSU_QTAGM1);
			add_tsu_reg(TSU_FWSR);
			add_tsu_reg(TSU_FWINMK);
			add_tsu_reg(TSU_ADQT0);
			add_tsu_reg(TSU_ADQT1);
			add_tsu_reg(TSU_VTAG0);
			add_tsu_reg(TSU_VTAG1);
		}
		add_tsu_reg(TSU_ADSBSY);
		add_tsu_reg(TSU_TEN);
		add_tsu_reg(TSU_POST1);
		add_tsu_reg(TSU_POST2);
		add_tsu_reg(TSU_POST3);
		add_tsu_reg(TSU_POST4);
		/* This is the start of a table, not just a single register. */
		if (buf) {
			unsigned int i;

			mark_reg_valid(TSU_ADRH0);
			for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES * 2; i++)
				*buf++ = ioread32(mdp->tsu_addr +
						  mdp->reg_offset[TSU_ADRH0] +
						  i * 4);
		}
		len += SH_ETH_TSU_CAM_ENTRIES * 2;
	}

#undef mark_reg_valid
#undef add_reg_from
#undef add_reg
#undef add_tsu_reg

	return len * 4;
}

static int sh_eth_get_regs_len(struct net_device *ndev)
{
	return __sh_eth_get_regs(ndev, NULL);
}

static void sh_eth_get_regs(struct net_device *ndev, struct ethtool_regs *regs,
			    void *buf)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	regs->version = SH_ETH_REG_DUMP_VERSION;

	pm_runtime_get_sync(&mdp->pdev->dev);
	__sh_eth_get_regs(ndev, buf);
	pm_runtime_put_sync(&mdp->pdev->dev);
}

static u32 sh_eth_get_msglevel(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	return mdp->msg_enable;
}

static void sh_eth_set_msglevel(struct net_device *ndev, u32 value)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	mdp->msg_enable = value;
}

static const char sh_eth_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_current", "tx_current",
	"rx_dirty", "tx_dirty",
};
#define SH_ETH_STATS_LEN  ARRAY_SIZE(sh_eth_gstrings_stats)

static int sh_eth_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return SH_ETH_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void sh_eth_get_ethtool_stats(struct net_device *ndev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i = 0;

	/* device-specific stats */
	data[i++] = mdp->cur_rx;
	data[i++] = mdp->cur_tx;
	data[i++] = mdp->dirty_rx;
	data[i++] = mdp->dirty_tx;
}

static void sh_eth_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, sh_eth_gstrings_stats,
		       sizeof(sh_eth_gstrings_stats));
		break;
	}
}

static void sh_eth_get_ringparam(struct net_device *ndev,
				 struct ethtool_ringparam *ring)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	ring->rx_max_pending = RX_RING_MAX;
	ring->tx_max_pending = TX_RING_MAX;
	ring->rx_pending = mdp->num_rx_ring;
	ring->tx_pending = mdp->num_tx_ring;
}

static int sh_eth_set_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	if (ring->tx_pending > TX_RING_MAX ||
	    ring->rx_pending > RX_RING_MAX ||
	    ring->tx_pending < TX_RING_MIN ||
	    ring->rx_pending < RX_RING_MIN)
		return -EINVAL;
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	if (netif_running(ndev)) {
		netif_device_detach(ndev);
		netif_tx_disable(ndev);

		/* Serialise with the interrupt handler and NAPI, then
		 * disable interrupts.  We have to clear the
		 * irq_enabled flag first to ensure that interrupts
		 * won't be re-enabled.
		 */
		mdp->irq_enabled = false;
		synchronize_irq(ndev->irq);
		napi_synchronize(&mdp->napi);
		sh_eth_write(ndev, 0x0000, EESIPR);

		sh_eth_dev_exit(ndev);

		/* Free all the skbuffs in the Rx queue and the DMA buffers. */
		sh_eth_ring_free(ndev);
	}

	/* Set new parameters */
	mdp->num_rx_ring = ring->rx_pending;
	mdp->num_tx_ring = ring->tx_pending;

	if (netif_running(ndev)) {
		ret = sh_eth_ring_init(ndev);
		if (ret < 0) {
			netdev_err(ndev, "%s: sh_eth_ring_init failed.\n",
				   __func__);
			return ret;
		}
		ret = sh_eth_dev_init(ndev);
		if (ret < 0) {
			netdev_err(ndev, "%s: sh_eth_dev_init failed.\n",
				   __func__);
			return ret;
		}

		netif_device_attach(ndev);
	}

	return 0;
}

static void sh_eth_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	wol->supported = 0;
	wol->wolopts = 0;

	if (mdp->cd->magic) {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = mdp->wol_enabled ? WAKE_MAGIC : 0;
	}
}

static int sh_eth_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (!mdp->cd->magic || wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	mdp->wol_enabled = !!(wol->wolopts & WAKE_MAGIC);

	device_set_wakeup_enable(&mdp->pdev->dev, mdp->wol_enabled);

	return 0;
}

static const struct ethtool_ops sh_eth_ethtool_ops = {
	.get_regs_len	= sh_eth_get_regs_len,
	.get_regs	= sh_eth_get_regs,
	.nway_reset	= phy_ethtool_nway_reset,
	.get_msglevel	= sh_eth_get_msglevel,
	.set_msglevel	= sh_eth_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_strings	= sh_eth_get_strings,
	.get_ethtool_stats  = sh_eth_get_ethtool_stats,
	.get_sset_count     = sh_eth_get_sset_count,
	.get_ringparam	= sh_eth_get_ringparam,
	.set_ringparam	= sh_eth_set_ringparam,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.get_wol	= sh_eth_get_wol,
	.set_wol	= sh_eth_set_wol,
};

/* network device open function */
static int sh_eth_open(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	pm_runtime_get_sync(&mdp->pdev->dev);

	napi_enable(&mdp->napi);

	ret = request_irq(ndev->irq, sh_eth_interrupt,
			  mdp->cd->irq_flags, ndev->name, ndev);
	if (ret) {
		netdev_err(ndev, "Can not assign IRQ number\n");
		goto out_napi_off;
	}

	/* Descriptor set */
	ret = sh_eth_ring_init(ndev);
	if (ret)
		goto out_free_irq;

	/* device init */
	ret = sh_eth_dev_init(ndev);
	if (ret)
		goto out_free_irq;

	/* PHY control start*/
	ret = sh_eth_phy_start(ndev);
	if (ret)
		goto out_free_irq;

	netif_start_queue(ndev);

	mdp->is_opened = 1;

	return ret;

out_free_irq:
	free_irq(ndev->irq, ndev);
out_napi_off:
	napi_disable(&mdp->napi);
	pm_runtime_put_sync(&mdp->pdev->dev);
	return ret;
}

/* Timeout function */
static void sh_eth_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_rxdesc *rxdesc;
	int i;

	netif_stop_queue(ndev);

	netif_err(mdp, timer, ndev,
		  "transmit timed out, status %8.8x, resetting...\n",
		  sh_eth_read(ndev, EESR));

	/* tx_errors count up */
	ndev->stats.tx_errors++;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < mdp->num_rx_ring; i++) {
		rxdesc = &mdp->rx_ring[i];
		rxdesc->status = cpu_to_le32(0);
		rxdesc->addr = cpu_to_le32(0xBADF00D0);
		dev_kfree_skb(mdp->rx_skbuff[i]);
		mdp->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < mdp->num_tx_ring; i++) {
		dev_kfree_skb(mdp->tx_skbuff[i]);
		mdp->tx_skbuff[i] = NULL;
	}

	/* device init */
	sh_eth_dev_init(ndev);

	netif_start_queue(ndev);
}

/* Packet transmit function */
static netdev_tx_t sh_eth_start_xmit(struct sk_buff *skb,
				     struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	dma_addr_t dma_addr;
	u32 entry;
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);
	if ((mdp->cur_tx - mdp->dirty_tx) >= (mdp->num_tx_ring - 4)) {
		if (!sh_eth_tx_free(ndev, true)) {
			netif_warn(mdp, tx_queued, ndev, "TxFD exhausted.\n");
			netif_stop_queue(ndev);
			spin_unlock_irqrestore(&mdp->lock, flags);
			return NETDEV_TX_BUSY;
		}
	}
	spin_unlock_irqrestore(&mdp->lock, flags);

	if (skb_put_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	entry = mdp->cur_tx % mdp->num_tx_ring;
	mdp->tx_skbuff[entry] = skb;
	txdesc = &mdp->tx_ring[entry];
	/* soft swap. */
	if (!mdp->cd->hw_swap)
		sh_eth_soft_swap(PTR_ALIGN(skb->data, 4), skb->len + 2);
	dma_addr = dma_map_single(&mdp->pdev->dev, skb->data, skb->len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(&mdp->pdev->dev, dma_addr)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	txdesc->addr = cpu_to_le32(dma_addr);
	txdesc->len  = cpu_to_le32(skb->len << 16);

	dma_wmb(); /* TACT bit must be set after all the above writes */
	if (entry >= mdp->num_tx_ring - 1)
		txdesc->status |= cpu_to_le32(TD_TACT | TD_TDLE);
	else
		txdesc->status |= cpu_to_le32(TD_TACT);

	wmb(); /* cur_tx must be incremented after TACT bit was set */
	mdp->cur_tx++;

	if (!(sh_eth_read(ndev, EDTRR) & mdp->cd->edtrr_trns))
		sh_eth_write(ndev, mdp->cd->edtrr_trns, EDTRR);

	return NETDEV_TX_OK;
}

/* The statistics registers have write-clear behaviour, which means we
 * will lose any increment between the read and write.  We mitigate
 * this by only clearing when we read a non-zero value, so we will
 * never falsely report a total of zero.
 */
static void
sh_eth_update_stat(struct net_device *ndev, unsigned long *stat, int reg)
{
	u32 delta = sh_eth_read(ndev, reg);

	if (delta) {
		*stat += delta;
		sh_eth_write(ndev, 0, reg);
	}
}

static struct net_device_stats *sh_eth_get_stats(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (mdp->cd->no_tx_cntrs)
		return &ndev->stats;

	if (!mdp->is_opened)
		return &ndev->stats;

	sh_eth_update_stat(ndev, &ndev->stats.tx_dropped, TROCR);
	sh_eth_update_stat(ndev, &ndev->stats.collisions, CDCR);
	sh_eth_update_stat(ndev, &ndev->stats.tx_carrier_errors, LCCR);

	if (mdp->cd->cexcr) {
		sh_eth_update_stat(ndev, &ndev->stats.tx_carrier_errors,
				   CERCR);
		sh_eth_update_stat(ndev, &ndev->stats.tx_carrier_errors,
				   CEECR);
	} else {
		sh_eth_update_stat(ndev, &ndev->stats.tx_carrier_errors,
				   CNDCR);
	}

	return &ndev->stats;
}

/* device close function */
static int sh_eth_close(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	netif_stop_queue(ndev);

	/* Serialise with the interrupt handler and NAPI, then disable
	 * interrupts.  We have to clear the irq_enabled flag first to
	 * ensure that interrupts won't be re-enabled.
	 */
	mdp->irq_enabled = false;
	synchronize_irq(ndev->irq);
	napi_disable(&mdp->napi);
	sh_eth_write(ndev, 0x0000, EESIPR);

	sh_eth_dev_exit(ndev);

	/* PHY Disconnect */
	if (ndev->phydev) {
		phy_stop(ndev->phydev);
		phy_disconnect(ndev->phydev);
	}

	free_irq(ndev->irq, ndev);

	/* Free all the skbuffs in the Rx queue and the DMA buffer. */
	sh_eth_ring_free(ndev);

	mdp->is_opened = 0;

	pm_runtime_put(&mdp->pdev->dev);

	return 0;
}

static int sh_eth_change_mtu(struct net_device *ndev, int new_mtu)
{
	if (netif_running(ndev))
		return -EBUSY;

	ndev->mtu = new_mtu;
	netdev_update_features(ndev);

	return 0;
}

/* For TSU_POSTn. Please refer to the manual about this (strange) bitfields */
static u32 sh_eth_tsu_get_post_mask(int entry)
{
	return 0x0f << (28 - ((entry % 8) * 4));
}

static u32 sh_eth_tsu_get_post_bit(struct sh_eth_private *mdp, int entry)
{
	return (0x08 >> (mdp->port << 1)) << (28 - ((entry % 8) * 4));
}

static void sh_eth_tsu_enable_cam_entry_post(struct net_device *ndev,
					     int entry)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int reg = TSU_POST1 + entry / 8;
	u32 tmp;

	tmp = sh_eth_tsu_read(mdp, reg);
	sh_eth_tsu_write(mdp, tmp | sh_eth_tsu_get_post_bit(mdp, entry), reg);
}

static bool sh_eth_tsu_disable_cam_entry_post(struct net_device *ndev,
					      int entry)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int reg = TSU_POST1 + entry / 8;
	u32 post_mask, ref_mask, tmp;

	post_mask = sh_eth_tsu_get_post_mask(entry);
	ref_mask = sh_eth_tsu_get_post_bit(mdp, entry) & ~post_mask;

	tmp = sh_eth_tsu_read(mdp, reg);
	sh_eth_tsu_write(mdp, tmp & ~post_mask, reg);

	/* If other port enables, the function returns "true" */
	return tmp & ref_mask;
}

static int sh_eth_tsu_busy(struct net_device *ndev)
{
	int timeout = SH_ETH_TSU_TIMEOUT_MS * 100;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	while ((sh_eth_tsu_read(mdp, TSU_ADSBSY) & TSU_ADSBSY_0)) {
		udelay(10);
		timeout--;
		if (timeout <= 0) {
			netdev_err(ndev, "%s: timeout\n", __func__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int sh_eth_tsu_write_entry(struct net_device *ndev, u16 offset,
				  const u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 val;

	val = addr[0] << 24 | addr[1] << 16 | addr[2] << 8 | addr[3];
	iowrite32(val, mdp->tsu_addr + offset);
	if (sh_eth_tsu_busy(ndev) < 0)
		return -EBUSY;

	val = addr[4] << 8 | addr[5];
	iowrite32(val, mdp->tsu_addr + offset + 4);
	if (sh_eth_tsu_busy(ndev) < 0)
		return -EBUSY;

	return 0;
}

static void sh_eth_tsu_read_entry(struct net_device *ndev, u16 offset, u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 val;

	val = ioread32(mdp->tsu_addr + offset);
	addr[0] = (val >> 24) & 0xff;
	addr[1] = (val >> 16) & 0xff;
	addr[2] = (val >> 8) & 0xff;
	addr[3] = val & 0xff;
	val = ioread32(mdp->tsu_addr + offset + 4);
	addr[4] = (val >> 8) & 0xff;
	addr[5] = val & 0xff;
}


static int sh_eth_tsu_find_entry(struct net_device *ndev, const u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	int i;
	u8 c_addr[ETH_ALEN];

	for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES; i++, reg_offset += 8) {
		sh_eth_tsu_read_entry(ndev, reg_offset, c_addr);
		if (ether_addr_equal(addr, c_addr))
			return i;
	}

	return -ENOENT;
}

static int sh_eth_tsu_find_empty(struct net_device *ndev)
{
	u8 blank[ETH_ALEN];
	int entry;

	memset(blank, 0, sizeof(blank));
	entry = sh_eth_tsu_find_entry(ndev, blank);
	return (entry < 0) ? -ENOMEM : entry;
}

static int sh_eth_tsu_disable_cam_entry_table(struct net_device *ndev,
					      int entry)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	int ret;
	u8 blank[ETH_ALEN];

	sh_eth_tsu_write(mdp, sh_eth_tsu_read(mdp, TSU_TEN) &
			 ~(1 << (31 - entry)), TSU_TEN);

	memset(blank, 0, sizeof(blank));
	ret = sh_eth_tsu_write_entry(ndev, reg_offset + entry * 8, blank);
	if (ret < 0)
		return ret;
	return 0;
}

static int sh_eth_tsu_add_entry(struct net_device *ndev, const u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	int i, ret;

	if (!mdp->cd->tsu)
		return 0;

	i = sh_eth_tsu_find_entry(ndev, addr);
	if (i < 0) {
		/* No entry found, create one */
		i = sh_eth_tsu_find_empty(ndev);
		if (i < 0)
			return -ENOMEM;
		ret = sh_eth_tsu_write_entry(ndev, reg_offset + i * 8, addr);
		if (ret < 0)
			return ret;

		/* Enable the entry */
		sh_eth_tsu_write(mdp, sh_eth_tsu_read(mdp, TSU_TEN) |
				 (1 << (31 - i)), TSU_TEN);
	}

	/* Entry found or created, enable POST */
	sh_eth_tsu_enable_cam_entry_post(ndev, i);

	return 0;
}

static int sh_eth_tsu_del_entry(struct net_device *ndev, const u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i, ret;

	if (!mdp->cd->tsu)
		return 0;

	i = sh_eth_tsu_find_entry(ndev, addr);
	if (i) {
		/* Entry found */
		if (sh_eth_tsu_disable_cam_entry_post(ndev, i))
			goto done;

		/* Disable the entry if both ports was disabled */
		ret = sh_eth_tsu_disable_cam_entry_table(ndev, i);
		if (ret < 0)
			return ret;
	}
done:
	return 0;
}

static int sh_eth_tsu_purge_all(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i, ret;

	if (!mdp->cd->tsu)
		return 0;

	for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES; i++) {
		if (sh_eth_tsu_disable_cam_entry_post(ndev, i))
			continue;

		/* Disable the entry if both ports was disabled */
		ret = sh_eth_tsu_disable_cam_entry_table(ndev, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void sh_eth_tsu_purge_mcast(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u16 reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	u8 addr[ETH_ALEN];
	int i;

	if (!mdp->cd->tsu)
		return;

	for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES; i++, reg_offset += 8) {
		sh_eth_tsu_read_entry(ndev, reg_offset, addr);
		if (is_multicast_ether_addr(addr))
			sh_eth_tsu_del_entry(ndev, addr);
	}
}

/* Update promiscuous flag and multicast filter */
static void sh_eth_set_rx_mode(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ecmr_bits;
	int mcast_all = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);
	/* Initial condition is MCT = 1, PRM = 0.
	 * Depending on ndev->flags, set PRM or clear MCT
	 */
	ecmr_bits = sh_eth_read(ndev, ECMR) & ~ECMR_PRM;
	if (mdp->cd->tsu)
		ecmr_bits |= ECMR_MCT;

	if (!(ndev->flags & IFF_MULTICAST)) {
		sh_eth_tsu_purge_mcast(ndev);
		mcast_all = 1;
	}
	if (ndev->flags & IFF_ALLMULTI) {
		sh_eth_tsu_purge_mcast(ndev);
		ecmr_bits &= ~ECMR_MCT;
		mcast_all = 1;
	}

	if (ndev->flags & IFF_PROMISC) {
		sh_eth_tsu_purge_all(ndev);
		ecmr_bits = (ecmr_bits & ~ECMR_MCT) | ECMR_PRM;
	} else if (mdp->cd->tsu) {
		struct netdev_hw_addr *ha;
		netdev_for_each_mc_addr(ha, ndev) {
			if (mcast_all && is_multicast_ether_addr(ha->addr))
				continue;

			if (sh_eth_tsu_add_entry(ndev, ha->addr) < 0) {
				if (!mcast_all) {
					sh_eth_tsu_purge_mcast(ndev);
					ecmr_bits &= ~ECMR_MCT;
					mcast_all = 1;
				}
			}
		}
	}

	/* update the ethernet mode */
	sh_eth_write(ndev, ecmr_bits, ECMR);

	spin_unlock_irqrestore(&mdp->lock, flags);
}

static void sh_eth_set_rx_csum(struct net_device *ndev, bool enable)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);

	/* Disable TX and RX */
	sh_eth_rcv_snd_disable(ndev);

	/* Modify RX Checksum setting */
	sh_eth_modify(ndev, ECMR, ECMR_RCSC, enable ? ECMR_RCSC : 0);

	/* Enable TX and RX */
	sh_eth_rcv_snd_enable(ndev);

	spin_unlock_irqrestore(&mdp->lock, flags);
}

static int sh_eth_set_features(struct net_device *ndev,
			       netdev_features_t features)
{
	netdev_features_t changed = ndev->features ^ features;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (changed & NETIF_F_RXCSUM && mdp->cd->rx_csum)
		sh_eth_set_rx_csum(ndev, features & NETIF_F_RXCSUM);

	ndev->features = features;

	return 0;
}

static int sh_eth_get_vtag_index(struct sh_eth_private *mdp)
{
	if (!mdp->port)
		return TSU_VTAG0;
	else
		return TSU_VTAG1;
}

static int sh_eth_vlan_rx_add_vid(struct net_device *ndev,
				  __be16 proto, u16 vid)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int vtag_reg_index = sh_eth_get_vtag_index(mdp);

	if (unlikely(!mdp->cd->tsu))
		return -EPERM;

	/* No filtering if vid = 0 */
	if (!vid)
		return 0;

	mdp->vlan_num_ids++;

	/* The controller has one VLAN tag HW filter. So, if the filter is
	 * already enabled, the driver disables it and the filte
	 */
	if (mdp->vlan_num_ids > 1) {
		/* disable VLAN filter */
		sh_eth_tsu_write(mdp, 0, vtag_reg_index);
		return 0;
	}

	sh_eth_tsu_write(mdp, TSU_VTAG_ENABLE | (vid & TSU_VTAG_VID_MASK),
			 vtag_reg_index);

	return 0;
}

static int sh_eth_vlan_rx_kill_vid(struct net_device *ndev,
				   __be16 proto, u16 vid)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int vtag_reg_index = sh_eth_get_vtag_index(mdp);

	if (unlikely(!mdp->cd->tsu))
		return -EPERM;

	/* No filtering if vid = 0 */
	if (!vid)
		return 0;

	mdp->vlan_num_ids--;
	sh_eth_tsu_write(mdp, 0, vtag_reg_index);

	return 0;
}

/* SuperH's TSU register init function */
static void sh_eth_tsu_init(struct sh_eth_private *mdp)
{
	if (!mdp->cd->dual_port) {
		sh_eth_tsu_write(mdp, 0, TSU_TEN); /* Disable all CAM entry */
		sh_eth_tsu_write(mdp, TSU_FWSLC_POSTENU | TSU_FWSLC_POSTENL,
				 TSU_FWSLC);	/* Enable POST registers */
		return;
	}

	sh_eth_tsu_write(mdp, 0, TSU_FWEN0);	/* Disable forward(0->1) */
	sh_eth_tsu_write(mdp, 0, TSU_FWEN1);	/* Disable forward(1->0) */
	sh_eth_tsu_write(mdp, 0, TSU_FCM);	/* forward fifo 3k-3k */
	sh_eth_tsu_write(mdp, 0xc, TSU_BSYSL0);
	sh_eth_tsu_write(mdp, 0xc, TSU_BSYSL1);
	sh_eth_tsu_write(mdp, 0, TSU_PRISL0);
	sh_eth_tsu_write(mdp, 0, TSU_PRISL1);
	sh_eth_tsu_write(mdp, 0, TSU_FWSL0);
	sh_eth_tsu_write(mdp, 0, TSU_FWSL1);
	sh_eth_tsu_write(mdp, TSU_FWSLC_POSTENU | TSU_FWSLC_POSTENL, TSU_FWSLC);
	sh_eth_tsu_write(mdp, 0, TSU_QTAGM0);	/* Disable QTAG(0->1) */
	sh_eth_tsu_write(mdp, 0, TSU_QTAGM1);	/* Disable QTAG(1->0) */
	sh_eth_tsu_write(mdp, 0, TSU_FWSR);	/* all interrupt status clear */
	sh_eth_tsu_write(mdp, 0, TSU_FWINMK);	/* Disable all interrupt */
	sh_eth_tsu_write(mdp, 0, TSU_TEN);	/* Disable all CAM entry */
	sh_eth_tsu_write(mdp, 0, TSU_POST1);	/* Disable CAM entry [ 0- 7] */
	sh_eth_tsu_write(mdp, 0, TSU_POST2);	/* Disable CAM entry [ 8-15] */
	sh_eth_tsu_write(mdp, 0, TSU_POST3);	/* Disable CAM entry [16-23] */
	sh_eth_tsu_write(mdp, 0, TSU_POST4);	/* Disable CAM entry [24-31] */
}

/* MDIO bus release function */
static int sh_mdio_release(struct sh_eth_private *mdp)
{
	/* unregister mdio bus */
	mdiobus_unregister(mdp->mii_bus);

	/* free bitbang info */
	free_mdio_bitbang(mdp->mii_bus);

	return 0;
}

static int sh_mdiobb_read(struct mii_bus *bus, int phy, int reg)
{
	int res;

	pm_runtime_get_sync(bus->parent);
	res = mdiobb_read(bus, phy, reg);
	pm_runtime_put(bus->parent);

	return res;
}

static int sh_mdiobb_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	int res;

	pm_runtime_get_sync(bus->parent);
	res = mdiobb_write(bus, phy, reg, val);
	pm_runtime_put(bus->parent);

	return res;
}

/* MDIO bus init function */
static int sh_mdio_init(struct sh_eth_private *mdp,
			struct sh_eth_plat_data *pd)
{
	int ret;
	struct bb_info *bitbang;
	struct platform_device *pdev = mdp->pdev;
	struct device *dev = &mdp->pdev->dev;

	/* create bit control struct for PHY */
	bitbang = devm_kzalloc(dev, sizeof(struct bb_info), GFP_KERNEL);
	if (!bitbang)
		return -ENOMEM;

	/* bitbang init */
	bitbang->addr = mdp->addr + mdp->reg_offset[PIR];
	bitbang->set_gate = pd->set_mdio_gate;
	bitbang->ctrl.ops = &bb_ops;

	/* MII controller setting */
	mdp->mii_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!mdp->mii_bus)
		return -ENOMEM;

	/* Wrap accessors with Runtime PM-aware ops */
	mdp->mii_bus->read = sh_mdiobb_read;
	mdp->mii_bus->write = sh_mdiobb_write;

	/* Hook up MII support for ethtool */
	mdp->mii_bus->name = "sh_mii";
	mdp->mii_bus->parent = dev;
	snprintf(mdp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pdev->name, pdev->id);

	/* register MDIO bus */
	if (pd->phy_irq > 0)
		mdp->mii_bus->irq[pd->phy] = pd->phy_irq;

	ret = of_mdiobus_register(mdp->mii_bus, dev->of_node);
	if (ret)
		goto out_free_bus;

	return 0;

out_free_bus:
	free_mdio_bitbang(mdp->mii_bus);
	return ret;
}

static const u16 *sh_eth_get_register_offset(int register_type)
{
	const u16 *reg_offset = NULL;

	switch (register_type) {
	case SH_ETH_REG_GIGABIT:
		reg_offset = sh_eth_offset_gigabit;
		break;
	case SH_ETH_REG_FAST_RCAR:
		reg_offset = sh_eth_offset_fast_rcar;
		break;
	case SH_ETH_REG_FAST_SH4:
		reg_offset = sh_eth_offset_fast_sh4;
		break;
	case SH_ETH_REG_FAST_SH3_SH2:
		reg_offset = sh_eth_offset_fast_sh3_sh2;
		break;
	}

	return reg_offset;
}

static const struct net_device_ops sh_eth_netdev_ops = {
	.ndo_open		= sh_eth_open,
	.ndo_stop		= sh_eth_close,
	.ndo_start_xmit		= sh_eth_start_xmit,
	.ndo_get_stats		= sh_eth_get_stats,
	.ndo_set_rx_mode	= sh_eth_set_rx_mode,
	.ndo_tx_timeout		= sh_eth_tx_timeout,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_change_mtu		= sh_eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_features	= sh_eth_set_features,
};

static const struct net_device_ops sh_eth_netdev_ops_tsu = {
	.ndo_open		= sh_eth_open,
	.ndo_stop		= sh_eth_close,
	.ndo_start_xmit		= sh_eth_start_xmit,
	.ndo_get_stats		= sh_eth_get_stats,
	.ndo_set_rx_mode	= sh_eth_set_rx_mode,
	.ndo_vlan_rx_add_vid	= sh_eth_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= sh_eth_vlan_rx_kill_vid,
	.ndo_tx_timeout		= sh_eth_tx_timeout,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_change_mtu		= sh_eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_features	= sh_eth_set_features,
};

#ifdef CONFIG_OF
static struct sh_eth_plat_data *sh_eth_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct sh_eth_plat_data *pdata;
	phy_interface_t interface;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	ret = of_get_phy_mode(np, &interface);
	if (ret)
		return NULL;
	pdata->phy_interface = interface;

	of_get_mac_address(np, pdata->mac_addr);

	pdata->no_ether_link =
		of_property_read_bool(np, "renesas,no-ether-link");
	pdata->ether_link_active_low =
		of_property_read_bool(np, "renesas,ether-link-active-low");

	return pdata;
}

static const struct of_device_id sh_eth_match_table[] = {
	{ .compatible = "renesas,gether-r8a7740", .data = &r8a7740_data },
	{ .compatible = "renesas,ether-r8a7743", .data = &rcar_gen2_data },
	{ .compatible = "renesas,ether-r8a7745", .data = &rcar_gen2_data },
	{ .compatible = "renesas,ether-r8a7778", .data = &rcar_gen1_data },
	{ .compatible = "renesas,ether-r8a7779", .data = &rcar_gen1_data },
	{ .compatible = "renesas,ether-r8a7790", .data = &rcar_gen2_data },
	{ .compatible = "renesas,ether-r8a7791", .data = &rcar_gen2_data },
	{ .compatible = "renesas,ether-r8a7793", .data = &rcar_gen2_data },
	{ .compatible = "renesas,ether-r8a7794", .data = &rcar_gen2_data },
	{ .compatible = "renesas,gether-r8a77980", .data = &r8a77980_data },
	{ .compatible = "renesas,ether-r7s72100", .data = &r7s72100_data },
	{ .compatible = "renesas,ether-r7s9210", .data = &r7s9210_data },
	{ .compatible = "renesas,rcar-gen1-ether", .data = &rcar_gen1_data },
	{ .compatible = "renesas,rcar-gen2-ether", .data = &rcar_gen2_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sh_eth_match_table);
#else
static inline struct sh_eth_plat_data *sh_eth_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int sh_eth_drv_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sh_eth_plat_data *pd = dev_get_platdata(&pdev->dev);
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct sh_eth_private *mdp;
	struct net_device *ndev;
	int ret;

	ndev = alloc_etherdev(sizeof(struct sh_eth_private));
	if (!ndev)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto out_release;
	ndev->irq = ret;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	mdp = netdev_priv(ndev);
	mdp->num_tx_ring = TX_RING_SIZE;
	mdp->num_rx_ring = RX_RING_SIZE;
	mdp->addr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(mdp->addr)) {
		ret = PTR_ERR(mdp->addr);
		goto out_release;
	}

	ndev->base_addr = res->start;

	spin_lock_init(&mdp->lock);
	mdp->pdev = pdev;

	if (pdev->dev.of_node)
		pd = sh_eth_parse_dt(&pdev->dev);
	if (!pd) {
		dev_err(&pdev->dev, "no platform data\n");
		ret = -EINVAL;
		goto out_release;
	}

	/* get PHY ID */
	mdp->phy_id = pd->phy;
	mdp->phy_interface = pd->phy_interface;
	mdp->no_ether_link = pd->no_ether_link;
	mdp->ether_link_active_low = pd->ether_link_active_low;

	/* set cpu data */
	if (id)
		mdp->cd = (struct sh_eth_cpu_data *)id->driver_data;
	else
		mdp->cd = (struct sh_eth_cpu_data *)of_device_get_match_data(&pdev->dev);

	mdp->reg_offset = sh_eth_get_register_offset(mdp->cd->register_type);
	if (!mdp->reg_offset) {
		dev_err(&pdev->dev, "Unknown register type (%d)\n",
			mdp->cd->register_type);
		ret = -EINVAL;
		goto out_release;
	}
	sh_eth_set_default_cpu_data(mdp->cd);

	/* User's manual states max MTU should be 2048 but due to the
	 * alignment calculations in sh_eth_ring_init() the practical
	 * MTU is a bit less. Maybe this can be optimized some more.
	 */
	ndev->max_mtu = 2000 - (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN);
	ndev->min_mtu = ETH_MIN_MTU;

	if (mdp->cd->rx_csum) {
		ndev->features = NETIF_F_RXCSUM;
		ndev->hw_features = NETIF_F_RXCSUM;
	}

	/* set function */
	if (mdp->cd->tsu)
		ndev->netdev_ops = &sh_eth_netdev_ops_tsu;
	else
		ndev->netdev_ops = &sh_eth_netdev_ops;
	ndev->ethtool_ops = &sh_eth_ethtool_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;

	/* debug message level */
	mdp->msg_enable = SH_ETH_DEF_MSG_ENABLE;

	/* read and set MAC address */
	read_mac_address(ndev, pd->mac_addr);
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		dev_warn(&pdev->dev,
			 "no valid MAC address supplied, using a random one.\n");
		eth_hw_addr_random(ndev);
	}

	if (mdp->cd->tsu) {
		int port = pdev->id < 0 ? 0 : pdev->id % 2;
		struct resource *rtsu;

		rtsu = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!rtsu) {
			dev_err(&pdev->dev, "no TSU resource\n");
			ret = -ENODEV;
			goto out_release;
		}
		/* We can only request the  TSU region  for the first port
		 * of the two  sharing this TSU for the probe to succeed...
		 */
		if (port == 0 &&
		    !devm_request_mem_region(&pdev->dev, rtsu->start,
					     resource_size(rtsu),
					     dev_name(&pdev->dev))) {
			dev_err(&pdev->dev, "can't request TSU resource.\n");
			ret = -EBUSY;
			goto out_release;
		}
		/* ioremap the TSU registers */
		mdp->tsu_addr = devm_ioremap(&pdev->dev, rtsu->start,
					     resource_size(rtsu));
		if (!mdp->tsu_addr) {
			dev_err(&pdev->dev, "TSU region ioremap() failed.\n");
			ret = -ENOMEM;
			goto out_release;
		}
		mdp->port = port;
		ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

		/* Need to init only the first port of the two sharing a TSU */
		if (port == 0) {
			if (mdp->cd->chip_reset)
				mdp->cd->chip_reset(ndev);

			/* TSU init (Init only)*/
			sh_eth_tsu_init(mdp);
		}
	}

	if (mdp->cd->rmiimode)
		sh_eth_write(ndev, 0x1, RMIIMODE);

	/* MDIO bus init */
	ret = sh_mdio_init(mdp, pd);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "MDIO init failed: %d\n", ret);
		goto out_release;
	}

	netif_napi_add(ndev, &mdp->napi, sh_eth_poll, 64);

	/* network device register */
	ret = register_netdev(ndev);
	if (ret)
		goto out_napi_del;

	if (mdp->cd->magic)
		device_set_wakeup_capable(&pdev->dev, 1);

	/* print device information */
	netdev_info(ndev, "Base address at 0x%x, %pM, IRQ %d.\n",
		    (u32)ndev->base_addr, ndev->dev_addr, ndev->irq);

	pm_runtime_put(&pdev->dev);
	platform_set_drvdata(pdev, ndev);

	return ret;

out_napi_del:
	netif_napi_del(&mdp->napi);
	sh_mdio_release(mdp);

out_release:
	/* net_dev free */
	free_netdev(ndev);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int sh_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct sh_eth_private *mdp = netdev_priv(ndev);

	unregister_netdev(ndev);
	netif_napi_del(&mdp->napi);
	sh_mdio_release(mdp);
	pm_runtime_disable(&pdev->dev);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
static int sh_eth_wol_setup(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* Only allow ECI interrupts */
	synchronize_irq(ndev->irq);
	napi_disable(&mdp->napi);
	sh_eth_write(ndev, EESIPR_ECIIP, EESIPR);

	/* Enable MagicPacket */
	sh_eth_modify(ndev, ECMR, ECMR_MPDE, ECMR_MPDE);

	return enable_irq_wake(ndev->irq);
}

static int sh_eth_wol_restore(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	napi_enable(&mdp->napi);

	/* Disable MagicPacket */
	sh_eth_modify(ndev, ECMR, ECMR_MPDE, 0);

	/* The device needs to be reset to restore MagicPacket logic
	 * for next wakeup. If we close and open the device it will
	 * both be reset and all registers restored. This is what
	 * happens during suspend and resume without WoL enabled.
	 */
	ret = sh_eth_close(ndev);
	if (ret < 0)
		return ret;
	ret = sh_eth_open(ndev);
	if (ret < 0)
		return ret;

	return disable_irq_wake(ndev->irq);
}

static int sh_eth_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret = 0;

	if (!netif_running(ndev))
		return 0;

	netif_device_detach(ndev);

	if (mdp->wol_enabled)
		ret = sh_eth_wol_setup(ndev);
	else
		ret = sh_eth_close(ndev);

	return ret;
}

static int sh_eth_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret = 0;

	if (!netif_running(ndev))
		return 0;

	if (mdp->wol_enabled)
		ret = sh_eth_wol_restore(ndev);
	else
		ret = sh_eth_open(ndev);

	if (ret < 0)
		return ret;

	netif_device_attach(ndev);

	return ret;
}
#endif

static int sh_eth_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops sh_eth_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sh_eth_suspend, sh_eth_resume)
	SET_RUNTIME_PM_OPS(sh_eth_runtime_nop, sh_eth_runtime_nop, NULL)
};
#define SH_ETH_PM_OPS (&sh_eth_dev_pm_ops)
#else
#define SH_ETH_PM_OPS NULL
#endif

static const struct platform_device_id sh_eth_id_table[] = {
	{ "sh7619-ether", (kernel_ulong_t)&sh7619_data },
	{ "sh771x-ether", (kernel_ulong_t)&sh771x_data },
	{ "sh7724-ether", (kernel_ulong_t)&sh7724_data },
	{ "sh7734-gether", (kernel_ulong_t)&sh7734_data },
	{ "sh7757-ether", (kernel_ulong_t)&sh7757_data },
	{ "sh7757-gether", (kernel_ulong_t)&sh7757_data_giga },
	{ "sh7763-gether", (kernel_ulong_t)&sh7763_data },
	{ }
};
MODULE_DEVICE_TABLE(platform, sh_eth_id_table);

static struct platform_driver sh_eth_driver = {
	.probe = sh_eth_drv_probe,
	.remove = sh_eth_drv_remove,
	.id_table = sh_eth_id_table,
	.driver = {
		   .name = CARDNAME,
		   .pm = SH_ETH_PM_OPS,
		   .of_match_table = of_match_ptr(sh_eth_match_table),
	},
};

module_platform_driver(sh_eth_driver);

MODULE_AUTHOR("Nobuhiro Iwamatsu, Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas SuperH Ethernet driver");
MODULE_LICENSE("GPL v2");
