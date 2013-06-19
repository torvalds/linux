/*
 *  SuperH Ethernet device driver
 *
 *  Copyright (C) 2006-2012 Nobuhiro Iwamatsu
 *  Copyright (C) 2008-2013 Renesas Solutions Corp.
 *  Copyright (C) 2013 Cogent Embedded, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 */

#include <linux/init.h>
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
#include <linux/phy.h>
#include <linux/cache.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/clk.h>
#include <linux/sh_eth.h>

#include "sh_eth.h"

#define SH_ETH_DEF_MSG_ENABLE \
		(NETIF_MSG_LINK	| \
		NETIF_MSG_TIMER	| \
		NETIF_MSG_RX_ERR| \
		NETIF_MSG_TX_ERR)

static const u16 sh_eth_offset_gigabit[SH_ETH_MAX_REGISTER_OFFSET] = {
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
	[TSU_QTAG0]	= 0x0040,
	[TSU_QTAG1]	= 0x0044,
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
	[TSU_ADRL0]	= 0x0104,
	[TSU_ADRH31]	= 0x01f8,
	[TSU_ADRL31]	= 0x01fc,

	[TXNLCR0]	= 0x0080,
	[TXALCR0]	= 0x0084,
	[RXNLCR0]	= 0x0088,
	[RXALCR0]	= 0x008c,
	[FWNLCR0]	= 0x0090,
	[FWALCR0]	= 0x0094,
	[TXNLCR1]	= 0x00a0,
	[TXALCR1]	= 0x00a0,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,
};

static const u16 sh_eth_offset_fast_rcar[SH_ETH_MAX_REGISTER_OFFSET] = {
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
	[FCFTR]		= 0x0270,
	[TRIMD]		= 0x027c,
};

static const u16 sh_eth_offset_fast_sh4[SH_ETH_MAX_REGISTER_OFFSET] = {
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
	[TXALCR1]	= 0x00a0,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,

	[TSU_ADRH0]	= 0x0100,
	[TSU_ADRL0]	= 0x0104,
	[TSU_ADRL31]	= 0x01fc,
};

static int sh_eth_is_gether(struct sh_eth_private *mdp)
{
	if (mdp->reg_offset == sh_eth_offset_gigabit)
		return 1;
	else
		return 0;
}

static void sh_eth_select_mii(struct net_device *ndev)
{
	u32 value = 0x0;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->phy_interface) {
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
		pr_warn("PHY interface mode was not setup. Set to MII.\n");
		value = 0x1;
		break;
	}

	sh_eth_write(ndev, value, RMII_MII);
}

static void sh_eth_set_duplex(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	if (mdp->duplex) /* Full */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) | ECMR_DM, ECMR);
	else		/* Half */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) & ~ECMR_DM, ECMR);
}

/* There is CPU dependent code */
static void sh_eth_set_rate_r8a777x(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) & ~ECMR_ELB, ECMR);
		break;
	case 100:/* 100BASE */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) | ECMR_ELB, ECMR);
		break;
	default:
		break;
	}
}

/* R8A7778/9 */
static struct sh_eth_cpu_data r8a777x_data = {
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_r8a777x,

	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP,
	.eesipr_value	= 0x01ff009f,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_RTO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RDE |
			  EESR_RFRMER | EESR_TFE | EESR_TDE | EESR_ECI,
	.tx_error_check	= EESR_TWB | EESR_TABT | EESR_TDE | EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
};

static void sh_eth_set_rate_sh7724(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	switch (mdp->speed) {
	case 10: /* 10BASE */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) & ~ECMR_RTM, ECMR);
		break;
	case 100:/* 100BASE */
		sh_eth_write(ndev, sh_eth_read(ndev, ECMR) | ECMR_RTM, ECMR);
		break;
	default:
		break;
	}
}

/* SH7724 */
static struct sh_eth_cpu_data sh7724_data = {
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_sh7724,

	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x01ff009f,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_RTO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RDE |
			  EESR_RFRMER | EESR_TFE | EESR_TDE | EESR_ECI,
	.tx_error_check	= EESR_TWB | EESR_TABT | EESR_TDE | EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.rpadir_value	= 0x00020000, /* NET_IP_ALIGN assumed to be 2 */
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
	default:
		break;
	}
}

/* SH7757 */
static struct sh_eth_cpu_data sh7757_data = {
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_sh7757,

	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,
	.rmcr_value	= 0x00000001,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_RTO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RDE |
			  EESR_RFRMER | EESR_TFE | EESR_TDE | EESR_ECI,
	.tx_error_check	= EESR_TWB | EESR_TABT | EESR_TDE | EESR_TFE,

	.irq_flags	= IRQF_SHARED,
	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
	.no_ade		= 1,
	.rpadir		= 1,
	.rpadir_value   = 2 << 16,
};

#define SH_GIGA_ETH_BASE	0xfee00000UL
#define GIGA_MALR(port)		(SH_GIGA_ETH_BASE + 0x800 * (port) + 0x05c8)
#define GIGA_MAHR(port)		(SH_GIGA_ETH_BASE + 0x800 * (port) + 0x05c0)
static void sh_eth_chip_reset_giga(struct net_device *ndev)
{
	int i;
	unsigned long mahr[2], malr[2];

	/* save MAHR and MALR */
	for (i = 0; i < 2; i++) {
		malr[i] = ioread32((void *)GIGA_MALR(i));
		mahr[i] = ioread32((void *)GIGA_MAHR(i));
	}

	/* reset device */
	iowrite32(ARSTR_ARSTR, (void *)(SH_GIGA_ETH_BASE + 0x1800));
	mdelay(1);

	/* restore MAHR and MALR */
	for (i = 0; i < 2; i++) {
		iowrite32(malr[i], (void *)GIGA_MALR(i));
		iowrite32(mahr[i], (void *)GIGA_MAHR(i));
	}
}

static void sh_eth_set_rate_giga(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

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
	default:
		break;
	}
}

/* SH7757(GETHERC) */
static struct sh_eth_cpu_data sh7757_data_giga = {
	.chip_reset	= sh_eth_chip_reset_giga,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_giga,

	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT | \
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE | \
			  EESR_ECI,
	.tx_error_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_TDE | \
			  EESR_TFE,
	.fdr_value	= 0x0000072f,
	.rmcr_value	= 0x00000001,

	.irq_flags	= IRQF_SHARED,
	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.rpadir_value   = 2 << 16,
	.no_trimd	= 1,
	.no_ade		= 1,
	.tsu		= 1,
};

static void sh_eth_chip_reset(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* reset device */
	sh_eth_tsu_write(mdp, ARSTR_ARSTR, ARSTR);
	mdelay(1);
}

static void sh_eth_set_rate_gether(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

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
	default:
		break;
	}
}

/* SH7734 */
static struct sh_eth_cpu_data sh7734_data = {
	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT | \
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE | \
			  EESR_ECI,
	.tx_error_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_TDE | \
			  EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.tsu		= 1,
	.hw_crc		= 1,
	.select_mii	= 1,
};

/* SH7763 */
static struct sh_eth_cpu_data sh7763_data = {
	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT | \
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE | \
			  EESR_ECI,
	.tx_error_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_TDE | \
			  EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.tsu		= 1,
	.irq_flags	= IRQF_SHARED,
};

static void sh_eth_chip_reset_r8a7740(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* reset device */
	sh_eth_tsu_write(mdp, ARSTR_ARSTR, ARSTR);
	mdelay(1);

	sh_eth_select_mii(ndev);
}

/* R8A7740 */
static struct sh_eth_cpu_data r8a7740_data = {
	.chip_reset	= sh_eth_chip_reset_r8a7740,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate_gether,

	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT | \
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE | \
			  EESR_ECI,
	.tx_error_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_TDE | \
			  EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
	.tsu		= 1,
	.select_mii	= 1,
};

static struct sh_eth_cpu_data sh7619_data = {
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
};

static struct sh_eth_cpu_data sh771x_data = {
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,
	.tsu		= 1,
};

static void sh_eth_set_default_cpu_data(struct sh_eth_cpu_data *cd)
{
	if (!cd->ecsr_value)
		cd->ecsr_value = DEFAULT_ECSR_INIT;

	if (!cd->ecsipr_value)
		cd->ecsipr_value = DEFAULT_ECSIPR_INIT;

	if (!cd->fcftr_value)
		cd->fcftr_value = DEFAULT_FIFO_F_D_RFF | \
				  DEFAULT_FIFO_F_D_RFD;

	if (!cd->fdr_value)
		cd->fdr_value = DEFAULT_FDR_INIT;

	if (!cd->rmcr_value)
		cd->rmcr_value = DEFAULT_RMCR_VALUE;

	if (!cd->tx_check)
		cd->tx_check = DEFAULT_TX_CHECK;

	if (!cd->eesr_err_check)
		cd->eesr_err_check = DEFAULT_EESR_ERR_CHECK;

	if (!cd->tx_error_check)
		cd->tx_error_check = DEFAULT_TX_ERROR_CHECK;
}

static int sh_eth_check_reset(struct net_device *ndev)
{
	int ret = 0;
	int cnt = 100;

	while (cnt > 0) {
		if (!(sh_eth_read(ndev, EDMR) & 0x3))
			break;
		mdelay(1);
		cnt--;
	}
	if (cnt <= 0) {
		pr_err("Device reset failed\n");
		ret = -ETIMEDOUT;
	}
	return ret;
}

static int sh_eth_reset(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret = 0;

	if (sh_eth_is_gether(mdp)) {
		sh_eth_write(ndev, EDSR_ENALL, EDSR);
		sh_eth_write(ndev, sh_eth_read(ndev, EDMR) | EDMR_SRST_GETHER,
			     EDMR);

		ret = sh_eth_check_reset(ndev);
		if (ret)
			goto out;

		/* Table Init */
		sh_eth_write(ndev, 0x0, TDLAR);
		sh_eth_write(ndev, 0x0, TDFAR);
		sh_eth_write(ndev, 0x0, TDFXR);
		sh_eth_write(ndev, 0x0, TDFFR);
		sh_eth_write(ndev, 0x0, RDLAR);
		sh_eth_write(ndev, 0x0, RDFAR);
		sh_eth_write(ndev, 0x0, RDFXR);
		sh_eth_write(ndev, 0x0, RDFFR);

		/* Reset HW CRC register */
		if (mdp->cd->hw_crc)
			sh_eth_write(ndev, 0x0, CSMR);

		/* Select MII mode */
		if (mdp->cd->select_mii)
			sh_eth_select_mii(ndev);
	} else {
		sh_eth_write(ndev, sh_eth_read(ndev, EDMR) | EDMR_SRST_ETHER,
			     EDMR);
		mdelay(3);
		sh_eth_write(ndev, sh_eth_read(ndev, EDMR) & ~EDMR_SRST_ETHER,
			     EDMR);
	}

out:
	return ret;
}

#if defined(CONFIG_CPU_SH4) || defined(CONFIG_ARCH_SHMOBILE)
static void sh_eth_set_receive_align(struct sk_buff *skb)
{
	int reserve;

	reserve = SH4_SKB_RX_ALIGN - ((u32)skb->data & (SH4_SKB_RX_ALIGN - 1));
	if (reserve)
		skb_reserve(skb, reserve);
}
#else
static void sh_eth_set_receive_align(struct sk_buff *skb)
{
	skb_reserve(skb, SH2_SH3_SKB_RX_ALIGN);
}
#endif


/* CPU <-> EDMAC endian convert */
static inline __u32 cpu_to_edmac(struct sh_eth_private *mdp, u32 x)
{
	switch (mdp->edmac_endian) {
	case EDMAC_LITTLE_ENDIAN:
		return cpu_to_le32(x);
	case EDMAC_BIG_ENDIAN:
		return cpu_to_be32(x);
	}
	return x;
}

static inline __u32 edmac_to_cpu(struct sh_eth_private *mdp, u32 x)
{
	switch (mdp->edmac_endian) {
	case EDMAC_LITTLE_ENDIAN:
		return le32_to_cpu(x);
	case EDMAC_BIG_ENDIAN:
		return be32_to_cpu(x);
	}
	return x;
}

/*
 * Program the hardware MAC address from dev->dev_addr.
 */
static void update_mac_address(struct net_device *ndev)
{
	sh_eth_write(ndev,
		(ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		(ndev->dev_addr[2] << 8) | (ndev->dev_addr[3]), MAHR);
	sh_eth_write(ndev,
		(ndev->dev_addr[4] << 8) | (ndev->dev_addr[5]), MALR);
}

/*
 * Get MAC address from SuperH MAC address register
 *
 * SuperH's Ethernet device doesn't have 'ROM' to MAC address.
 * This driver get MAC address that use by bootloader(U-boot or sh-ipl+g).
 * When you want use this device, you must set MAC address in bootloader.
 *
 */
static void read_mac_address(struct net_device *ndev, unsigned char *mac)
{
	if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
		memcpy(ndev->dev_addr, mac, 6);
	} else {
		ndev->dev_addr[0] = (sh_eth_read(ndev, MAHR) >> 24);
		ndev->dev_addr[1] = (sh_eth_read(ndev, MAHR) >> 16) & 0xFF;
		ndev->dev_addr[2] = (sh_eth_read(ndev, MAHR) >> 8) & 0xFF;
		ndev->dev_addr[3] = (sh_eth_read(ndev, MAHR) & 0xFF);
		ndev->dev_addr[4] = (sh_eth_read(ndev, MALR) >> 8) & 0xFF;
		ndev->dev_addr[5] = (sh_eth_read(ndev, MALR) & 0xFF);
	}
}

static unsigned long sh_eth_get_edtrr_trns(struct sh_eth_private *mdp)
{
	if (sh_eth_is_gether(mdp))
		return EDTRR_TRNS_GETHER;
	else
		return EDTRR_TRNS_ETHER;
}

struct bb_info {
	void (*set_gate)(void *addr);
	struct mdiobb_ctrl ctrl;
	void *addr;
	u32 mmd_msk;/* MMD */
	u32 mdo_msk;
	u32 mdi_msk;
	u32 mdc_msk;
};

/* PHY bit set */
static void bb_set(void *addr, u32 msk)
{
	iowrite32(ioread32(addr) | msk, addr);
}

/* PHY bit clear */
static void bb_clr(void *addr, u32 msk)
{
	iowrite32((ioread32(addr) & ~msk), addr);
}

/* PHY bit read */
static int bb_read(void *addr, u32 msk)
{
	return (ioread32(addr) & msk) != 0;
}

/* Data I/O pin control */
static void sh_mmd_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	if (bit)
		bb_set(bitbang->addr, bitbang->mmd_msk);
	else
		bb_clr(bitbang->addr, bitbang->mmd_msk);
}

/* Set bit data*/
static void sh_set_mdio(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	if (bit)
		bb_set(bitbang->addr, bitbang->mdo_msk);
	else
		bb_clr(bitbang->addr, bitbang->mdo_msk);
}

/* Get bit data*/
static int sh_get_mdio(struct mdiobb_ctrl *ctrl)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	return bb_read(bitbang->addr, bitbang->mdi_msk);
}

/* MDC pin control */
static void sh_mdc_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bitbang->set_gate)
		bitbang->set_gate(bitbang->addr);

	if (bit)
		bb_set(bitbang->addr, bitbang->mdc_msk);
	else
		bb_clr(bitbang->addr, bitbang->mdc_msk);
}

/* mdio bus control struct */
static struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = sh_mdc_ctrl,
	.set_mdio_dir = sh_mmd_ctrl,
	.set_mdio_data = sh_set_mdio,
	.get_mdio_data = sh_get_mdio,
};

/* free skb and descriptor buffer */
static void sh_eth_ring_free(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i;

	/* Free Rx skb ringbuffer */
	if (mdp->rx_skbuff) {
		for (i = 0; i < mdp->num_rx_ring; i++) {
			if (mdp->rx_skbuff[i])
				dev_kfree_skb(mdp->rx_skbuff[i]);
		}
	}
	kfree(mdp->rx_skbuff);
	mdp->rx_skbuff = NULL;

	/* Free Tx skb ringbuffer */
	if (mdp->tx_skbuff) {
		for (i = 0; i < mdp->num_tx_ring; i++) {
			if (mdp->tx_skbuff[i])
				dev_kfree_skb(mdp->tx_skbuff[i]);
		}
	}
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

	mdp->cur_rx = mdp->cur_tx = 0;
	mdp->dirty_rx = mdp->dirty_tx = 0;

	memset(mdp->rx_ring, 0, rx_ringsize);

	/* build Rx ring buffer */
	for (i = 0; i < mdp->num_rx_ring; i++) {
		/* skb */
		mdp->rx_skbuff[i] = NULL;
		skb = netdev_alloc_skb(ndev, mdp->rx_buf_sz);
		mdp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		dma_map_single(&ndev->dev, skb->data, mdp->rx_buf_sz,
				DMA_FROM_DEVICE);
		sh_eth_set_receive_align(skb);

		/* RX descriptor */
		rxdesc = &mdp->rx_ring[i];
		rxdesc->addr = virt_to_phys(PTR_ALIGN(skb->data, 4));
		rxdesc->status = cpu_to_edmac(mdp, RD_RACT | RD_RFP);

		/* The size of the buffer is 16 byte boundary. */
		rxdesc->buffer_length = ALIGN(mdp->rx_buf_sz, 16);
		/* Rx descriptor address set */
		if (i == 0) {
			sh_eth_write(ndev, mdp->rx_desc_dma, RDLAR);
			if (sh_eth_is_gether(mdp))
				sh_eth_write(ndev, mdp->rx_desc_dma, RDFAR);
		}
	}

	mdp->dirty_rx = (u32) (i - mdp->num_rx_ring);

	/* Mark the last entry as wrapping the ring. */
	rxdesc->status |= cpu_to_edmac(mdp, RD_RDEL);

	memset(mdp->tx_ring, 0, tx_ringsize);

	/* build Tx ring buffer */
	for (i = 0; i < mdp->num_tx_ring; i++) {
		mdp->tx_skbuff[i] = NULL;
		txdesc = &mdp->tx_ring[i];
		txdesc->status = cpu_to_edmac(mdp, TD_TFP);
		txdesc->buffer_length = 0;
		if (i == 0) {
			/* Tx descriptor address set */
			sh_eth_write(ndev, mdp->tx_desc_dma, TDLAR);
			if (sh_eth_is_gether(mdp))
				sh_eth_write(ndev, mdp->tx_desc_dma, TDFAR);
		}
	}

	txdesc->status |= cpu_to_edmac(mdp, TD_TDLE);
}

/* Get skb and descriptor buffer */
static int sh_eth_ring_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int rx_ringsize, tx_ringsize, ret = 0;

	/*
	 * +26 gets the maximum ethernet encapsulation, +7 & ~7 because the
	 * card needs room to do 8 byte alignment, +2 so we can reserve
	 * the first 2 bytes, and +16 gets room for the status word from the
	 * card.
	 */
	mdp->rx_buf_sz = (ndev->mtu <= 1492 ? PKT_BUF_SZ :
			  (((ndev->mtu + 26 + 7) & ~7) + 2 + 16));
	if (mdp->cd->rpadir)
		mdp->rx_buf_sz += NET_IP_ALIGN;

	/* Allocate RX and TX skb rings */
	mdp->rx_skbuff = kmalloc_array(mdp->num_rx_ring,
				       sizeof(*mdp->rx_skbuff), GFP_KERNEL);
	if (!mdp->rx_skbuff) {
		ret = -ENOMEM;
		return ret;
	}

	mdp->tx_skbuff = kmalloc_array(mdp->num_tx_ring,
				       sizeof(*mdp->tx_skbuff), GFP_KERNEL);
	if (!mdp->tx_skbuff) {
		ret = -ENOMEM;
		goto skb_ring_free;
	}

	/* Allocate all Rx descriptors. */
	rx_ringsize = sizeof(struct sh_eth_rxdesc) * mdp->num_rx_ring;
	mdp->rx_ring = dma_alloc_coherent(NULL, rx_ringsize, &mdp->rx_desc_dma,
					  GFP_KERNEL);
	if (!mdp->rx_ring) {
		ret = -ENOMEM;
		goto desc_ring_free;
	}

	mdp->dirty_rx = 0;

	/* Allocate all Tx descriptors. */
	tx_ringsize = sizeof(struct sh_eth_txdesc) * mdp->num_tx_ring;
	mdp->tx_ring = dma_alloc_coherent(NULL, tx_ringsize, &mdp->tx_desc_dma,
					  GFP_KERNEL);
	if (!mdp->tx_ring) {
		ret = -ENOMEM;
		goto desc_ring_free;
	}
	return ret;

desc_ring_free:
	/* free DMA buffer */
	dma_free_coherent(NULL, rx_ringsize, mdp->rx_ring, mdp->rx_desc_dma);

skb_ring_free:
	/* Free Rx and Tx skb ring buffer */
	sh_eth_ring_free(ndev);
	mdp->tx_ring = NULL;
	mdp->rx_ring = NULL;

	return ret;
}

static void sh_eth_free_dma_buffer(struct sh_eth_private *mdp)
{
	int ringsize;

	if (mdp->rx_ring) {
		ringsize = sizeof(struct sh_eth_rxdesc) * mdp->num_rx_ring;
		dma_free_coherent(NULL, ringsize, mdp->rx_ring,
				  mdp->rx_desc_dma);
		mdp->rx_ring = NULL;
	}

	if (mdp->tx_ring) {
		ringsize = sizeof(struct sh_eth_txdesc) * mdp->num_tx_ring;
		dma_free_coherent(NULL, ringsize, mdp->tx_ring,
				  mdp->tx_desc_dma);
		mdp->tx_ring = NULL;
	}
}

static int sh_eth_dev_init(struct net_device *ndev, bool start)
{
	int ret = 0;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 val;

	/* Soft Reset */
	ret = sh_eth_reset(ndev);
	if (ret)
		goto out;

	/* Descriptor format */
	sh_eth_ring_format(ndev);
	if (mdp->cd->rpadir)
		sh_eth_write(ndev, mdp->cd->rpadir_value, RPADIR);

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

	/* Frame recv control */
	sh_eth_write(ndev, mdp->cd->rmcr_value, RMCR);

	sh_eth_write(ndev, DESC_I_RINT8 | DESC_I_RINT5 | DESC_I_TINT2, TRSCER);

	if (mdp->cd->bculr)
		sh_eth_write(ndev, 0x800, BCULR);	/* Burst sycle set */

	sh_eth_write(ndev, mdp->cd->fcftr_value, FCFTR);

	if (!mdp->cd->no_trimd)
		sh_eth_write(ndev, 0, TRIMD);

	/* Recv frame limit set register */
	sh_eth_write(ndev, ndev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN,
		     RFLR);

	sh_eth_write(ndev, sh_eth_read(ndev, EESR), EESR);
	if (start)
		sh_eth_write(ndev, mdp->cd->eesipr_value, EESIPR);

	/* PAUSE Prohibition */
	val = (sh_eth_read(ndev, ECMR) & ECMR_DM) |
		ECMR_ZPF | (mdp->duplex ? ECMR_DM : 0) | ECMR_TE | ECMR_RE;

	sh_eth_write(ndev, val, ECMR);

	if (mdp->cd->set_rate)
		mdp->cd->set_rate(ndev);

	/* E-MAC Status Register clear */
	sh_eth_write(ndev, mdp->cd->ecsr_value, ECSR);

	/* E-MAC Interrupt Enable register */
	if (start)
		sh_eth_write(ndev, mdp->cd->ecsipr_value, ECSIPR);

	/* Set MAC address */
	update_mac_address(ndev);

	/* mask reset */
	if (mdp->cd->apr)
		sh_eth_write(ndev, APR_AP, APR);
	if (mdp->cd->mpr)
		sh_eth_write(ndev, MPR_MP, MPR);
	if (mdp->cd->tpauser)
		sh_eth_write(ndev, TPAUSER_UNLIMITED, TPAUSER);

	if (start) {
		/* Setting the Rx mode will start the Rx process. */
		sh_eth_write(ndev, EDRRR_R, EDRRR);

		netif_start_queue(ndev);
	}

out:
	return ret;
}

/* free Tx skb function */
static int sh_eth_txfree(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	int freeNum = 0;
	int entry = 0;

	for (; mdp->cur_tx - mdp->dirty_tx > 0; mdp->dirty_tx++) {
		entry = mdp->dirty_tx % mdp->num_tx_ring;
		txdesc = &mdp->tx_ring[entry];
		if (txdesc->status & cpu_to_edmac(mdp, TD_TACT))
			break;
		/* Free the original skb. */
		if (mdp->tx_skbuff[entry]) {
			dma_unmap_single(&ndev->dev, txdesc->addr,
					 txdesc->buffer_length, DMA_TO_DEVICE);
			dev_kfree_skb_irq(mdp->tx_skbuff[entry]);
			mdp->tx_skbuff[entry] = NULL;
			freeNum++;
		}
		txdesc->status = cpu_to_edmac(mdp, TD_TFP);
		if (entry >= mdp->num_tx_ring - 1)
			txdesc->status |= cpu_to_edmac(mdp, TD_TDLE);

		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += txdesc->buffer_length;
	}
	return freeNum;
}

/* Packet receive function */
static int sh_eth_rx(struct net_device *ndev, u32 intr_status)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_rxdesc *rxdesc;

	int entry = mdp->cur_rx % mdp->num_rx_ring;
	int boguscnt = (mdp->dirty_rx + mdp->num_rx_ring) - mdp->cur_rx;
	struct sk_buff *skb;
	u16 pkt_len = 0;
	u32 desc_status;

	rxdesc = &mdp->rx_ring[entry];
	while (!(rxdesc->status & cpu_to_edmac(mdp, RD_RACT))) {
		desc_status = edmac_to_cpu(mdp, rxdesc->status);
		pkt_len = rxdesc->frame_length;

		if (--boguscnt < 0)
			break;

		if (!(desc_status & RDFEND))
			ndev->stats.rx_length_errors++;

#if defined(CONFIG_ARCH_R8A7740)
		/*
		 * In case of almost all GETHER/ETHERs, the Receive Frame State
		 * (RFS) bits in the Receive Descriptor 0 are from bit 9 to
		 * bit 0. However, in case of the R8A7740's GETHER, the RFS
		 * bits are from bit 25 to bit 16. So, the driver needs right
		 * shifting by 16.
		 */
		desc_status >>= 16;
#endif

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
		} else {
			if (!mdp->cd->hw_swap)
				sh_eth_soft_swap(
					phys_to_virt(ALIGN(rxdesc->addr, 4)),
					pkt_len + 2);
			skb = mdp->rx_skbuff[entry];
			mdp->rx_skbuff[entry] = NULL;
			if (mdp->cd->rpadir)
				skb_reserve(skb, NET_IP_ALIGN);
			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, ndev);
			netif_rx(skb);
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += pkt_len;
		}
		rxdesc->status |= cpu_to_edmac(mdp, RD_RACT);
		entry = (++mdp->cur_rx) % mdp->num_rx_ring;
		rxdesc = &mdp->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; mdp->cur_rx - mdp->dirty_rx > 0; mdp->dirty_rx++) {
		entry = mdp->dirty_rx % mdp->num_rx_ring;
		rxdesc = &mdp->rx_ring[entry];
		/* The size of the buffer is 16 byte boundary. */
		rxdesc->buffer_length = ALIGN(mdp->rx_buf_sz, 16);

		if (mdp->rx_skbuff[entry] == NULL) {
			skb = netdev_alloc_skb(ndev, mdp->rx_buf_sz);
			mdp->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;	/* Better luck next round. */
			dma_map_single(&ndev->dev, skb->data, mdp->rx_buf_sz,
					DMA_FROM_DEVICE);
			sh_eth_set_receive_align(skb);

			skb_checksum_none_assert(skb);
			rxdesc->addr = virt_to_phys(PTR_ALIGN(skb->data, 4));
		}
		if (entry >= mdp->num_rx_ring - 1)
			rxdesc->status |=
				cpu_to_edmac(mdp, RD_RACT | RD_RFP | RD_RDEL);
		else
			rxdesc->status |=
				cpu_to_edmac(mdp, RD_RACT | RD_RFP);
	}

	/* Restart Rx engine if stopped. */
	/* If we don't need to check status, don't. -KDU */
	if (!(sh_eth_read(ndev, EDRRR) & EDRRR_R)) {
		/* fix the values for the next receiving if RDE is set */
		if (intr_status & EESR_RDE)
			mdp->cur_rx = mdp->dirty_rx =
				(sh_eth_read(ndev, RDFAR) -
				 sh_eth_read(ndev, RDLAR)) >> 4;
		sh_eth_write(ndev, EDRRR_R, EDRRR);
	}

	return 0;
}

static void sh_eth_rcv_snd_disable(struct net_device *ndev)
{
	/* disable tx and rx */
	sh_eth_write(ndev, sh_eth_read(ndev, ECMR) &
		~(ECMR_RE | ECMR_TE), ECMR);
}

static void sh_eth_rcv_snd_enable(struct net_device *ndev)
{
	/* enable tx and rx */
	sh_eth_write(ndev, sh_eth_read(ndev, ECMR) |
		(ECMR_RE | ECMR_TE), ECMR);
}

/* error control function */
static void sh_eth_error(struct net_device *ndev, int intr_status)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 felic_stat;
	u32 link_stat;
	u32 mask;

	if (intr_status & EESR_ECI) {
		felic_stat = sh_eth_read(ndev, ECSR);
		sh_eth_write(ndev, felic_stat, ECSR);	/* clear int */
		if (felic_stat & ECSR_ICD)
			ndev->stats.tx_carrier_errors++;
		if (felic_stat & ECSR_LCHNG) {
			/* Link Changed */
			if (mdp->cd->no_psr || mdp->no_ether_link) {
				goto ignore_link;
			} else {
				link_stat = (sh_eth_read(ndev, PSR));
				if (mdp->ether_link_active_low)
					link_stat = ~link_stat;
			}
			if (!(link_stat & PHY_ST_LINK))
				sh_eth_rcv_snd_disable(ndev);
			else {
				/* Link Up */
				sh_eth_write(ndev, sh_eth_read(ndev, EESIPR) &
					  ~DMAC_M_ECI, EESIPR);
				/*clear int */
				sh_eth_write(ndev, sh_eth_read(ndev, ECSR),
					  ECSR);
				sh_eth_write(ndev, sh_eth_read(ndev, EESIPR) |
					  DMAC_M_ECI, EESIPR);
				/* enable tx and rx */
				sh_eth_rcv_snd_enable(ndev);
			}
		}
	}

ignore_link:
	if (intr_status & EESR_TWB) {
		/* Write buck end. unused write back interrupt */
		if (intr_status & EESR_TABT)	/* Transmit Abort int */
			ndev->stats.tx_aborted_errors++;
			if (netif_msg_tx_err(mdp))
				dev_err(&ndev->dev, "Transmit Abort\n");
	}

	if (intr_status & EESR_RABT) {
		/* Receive Abort int */
		if (intr_status & EESR_RFRMER) {
			/* Receive Frame Overflow int */
			ndev->stats.rx_frame_errors++;
			if (netif_msg_rx_err(mdp))
				dev_err(&ndev->dev, "Receive Abort\n");
		}
	}

	if (intr_status & EESR_TDE) {
		/* Transmit Descriptor Empty int */
		ndev->stats.tx_fifo_errors++;
		if (netif_msg_tx_err(mdp))
			dev_err(&ndev->dev, "Transmit Descriptor Empty\n");
	}

	if (intr_status & EESR_TFE) {
		/* FIFO under flow */
		ndev->stats.tx_fifo_errors++;
		if (netif_msg_tx_err(mdp))
			dev_err(&ndev->dev, "Transmit FIFO Under flow\n");
	}

	if (intr_status & EESR_RDE) {
		/* Receive Descriptor Empty int */
		ndev->stats.rx_over_errors++;

		if (netif_msg_rx_err(mdp))
			dev_err(&ndev->dev, "Receive Descriptor Empty\n");
	}

	if (intr_status & EESR_RFE) {
		/* Receive FIFO Overflow int */
		ndev->stats.rx_fifo_errors++;
		if (netif_msg_rx_err(mdp))
			dev_err(&ndev->dev, "Receive FIFO Overflow\n");
	}

	if (!mdp->cd->no_ade && (intr_status & EESR_ADE)) {
		/* Address Error */
		ndev->stats.tx_fifo_errors++;
		if (netif_msg_tx_err(mdp))
			dev_err(&ndev->dev, "Address Error\n");
	}

	mask = EESR_TWB | EESR_TABT | EESR_ADE | EESR_TDE | EESR_TFE;
	if (mdp->cd->no_ade)
		mask &= ~EESR_ADE;
	if (intr_status & mask) {
		/* Tx error */
		u32 edtrr = sh_eth_read(ndev, EDTRR);
		/* dmesg */
		dev_err(&ndev->dev, "TX error. status=%8.8x cur_tx=%8.8x ",
				intr_status, mdp->cur_tx);
		dev_err(&ndev->dev, "dirty_tx=%8.8x state=%8.8x EDTRR=%8.8x.\n",
				mdp->dirty_tx, (u32) ndev->state, edtrr);
		/* dirty buffer free */
		sh_eth_txfree(ndev);

		/* SH7712 BUG */
		if (edtrr ^ sh_eth_get_edtrr_trns(mdp)) {
			/* tx dma start */
			sh_eth_write(ndev, sh_eth_get_edtrr_trns(mdp), EDTRR);
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
	unsigned long intr_status;

	spin_lock(&mdp->lock);

	/* Get interrupt status */
	intr_status = sh_eth_read(ndev, EESR);
	/* Mask it with the interrupt mask, forcing ECI interrupt to be always
	 * enabled since it's the one that  comes thru regardless of the mask,
	 * and we need to fully handle it in sh_eth_error() in order to quench
	 * it as it doesn't get cleared by just writing 1 to the ECI bit...
	 */
	intr_status &= sh_eth_read(ndev, EESIPR) | DMAC_M_ECI;
	/* Clear interrupt */
	if (intr_status & (EESR_RX_CHECK | cd->tx_check | cd->eesr_err_check)) {
		sh_eth_write(ndev, intr_status, EESR);
		ret = IRQ_HANDLED;
	} else
		goto other_irq;

	if (intr_status & EESR_RX_CHECK)
		sh_eth_rx(ndev, intr_status);

	/* Tx Check */
	if (intr_status & cd->tx_check) {
		sh_eth_txfree(ndev);
		netif_wake_queue(ndev);
	}

	if (intr_status & cd->eesr_err_check)
		sh_eth_error(ndev, intr_status);

other_irq:
	spin_unlock(&mdp->lock);

	return ret;
}

/* PHY state control function */
static void sh_eth_adjust_link(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev = mdp->phydev;
	int new_state = 0;

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
			sh_eth_write(ndev,
				(sh_eth_read(ndev, ECMR) & ~ECMR_TXF), ECMR);
			new_state = 1;
			mdp->link = phydev->link;
			if (mdp->cd->no_psr || mdp->no_ether_link)
				sh_eth_rcv_snd_enable(ndev);
		}
	} else if (mdp->link) {
		new_state = 1;
		mdp->link = 0;
		mdp->speed = 0;
		mdp->duplex = -1;
		if (mdp->cd->no_psr || mdp->no_ether_link)
			sh_eth_rcv_snd_disable(ndev);
	}

	if (new_state && netif_msg_link(mdp))
		phy_print_status(phydev);
}

/* PHY init function */
static int sh_eth_phy_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	char phy_id[MII_BUS_ID_SIZE + 3];
	struct phy_device *phydev = NULL;

	snprintf(phy_id, sizeof(phy_id), PHY_ID_FMT,
		mdp->mii_bus->id , mdp->phy_id);

	mdp->link = 0;
	mdp->speed = 0;
	mdp->duplex = -1;

	/* Try connect to PHY */
	phydev = phy_connect(ndev, phy_id, sh_eth_adjust_link,
			     mdp->phy_interface);
	if (IS_ERR(phydev)) {
		dev_err(&ndev->dev, "phy_connect failed\n");
		return PTR_ERR(phydev);
	}

	dev_info(&ndev->dev, "attached phy %i to driver %s\n",
		phydev->addr, phydev->drv->name);

	mdp->phydev = phydev;

	return 0;
}

/* PHY control start function */
static int sh_eth_phy_start(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	ret = sh_eth_phy_init(ndev);
	if (ret)
		return ret;

	/* reset phy - this also wakes it from PDOWN */
	phy_write(mdp->phydev, MII_BMCR, BMCR_RESET);
	phy_start(mdp->phydev);

	return 0;
}

static int sh_eth_get_settings(struct net_device *ndev,
			struct ethtool_cmd *ecmd)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mdp->lock, flags);
	ret = phy_ethtool_gset(mdp->phydev, ecmd);
	spin_unlock_irqrestore(&mdp->lock, flags);

	return ret;
}

static int sh_eth_set_settings(struct net_device *ndev,
		struct ethtool_cmd *ecmd)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mdp->lock, flags);

	/* disable tx and rx */
	sh_eth_rcv_snd_disable(ndev);

	ret = phy_ethtool_sset(mdp->phydev, ecmd);
	if (ret)
		goto error_exit;

	if (ecmd->duplex == DUPLEX_FULL)
		mdp->duplex = 1;
	else
		mdp->duplex = 0;

	if (mdp->cd->set_duplex)
		mdp->cd->set_duplex(ndev);

error_exit:
	mdelay(1);

	/* enable tx and rx */
	sh_eth_rcv_snd_enable(ndev);

	spin_unlock_irqrestore(&mdp->lock, flags);

	return ret;
}

static int sh_eth_nway_reset(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mdp->lock, flags);
	ret = phy_start_aneg(mdp->phydev);
	spin_unlock_irqrestore(&mdp->lock, flags);

	return ret;
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
		memcpy(data, *sh_eth_gstrings_stats,
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
		netif_tx_disable(ndev);
		/* Disable interrupts by clearing the interrupt mask. */
		sh_eth_write(ndev, 0x0000, EESIPR);
		/* Stop the chip's Tx and Rx processes. */
		sh_eth_write(ndev, 0, EDTRR);
		sh_eth_write(ndev, 0, EDRRR);
		synchronize_irq(ndev->irq);
	}

	/* Free all the skbuffs in the Rx queue. */
	sh_eth_ring_free(ndev);
	/* Free DMA buffer */
	sh_eth_free_dma_buffer(mdp);

	/* Set new parameters */
	mdp->num_rx_ring = ring->rx_pending;
	mdp->num_tx_ring = ring->tx_pending;

	ret = sh_eth_ring_init(ndev);
	if (ret < 0) {
		dev_err(&ndev->dev, "%s: sh_eth_ring_init failed.\n", __func__);
		return ret;
	}
	ret = sh_eth_dev_init(ndev, false);
	if (ret < 0) {
		dev_err(&ndev->dev, "%s: sh_eth_dev_init failed.\n", __func__);
		return ret;
	}

	if (netif_running(ndev)) {
		sh_eth_write(ndev, mdp->cd->eesipr_value, EESIPR);
		/* Setting the Rx mode will start the Rx process. */
		sh_eth_write(ndev, EDRRR_R, EDRRR);
		netif_wake_queue(ndev);
	}

	return 0;
}

static const struct ethtool_ops sh_eth_ethtool_ops = {
	.get_settings	= sh_eth_get_settings,
	.set_settings	= sh_eth_set_settings,
	.nway_reset	= sh_eth_nway_reset,
	.get_msglevel	= sh_eth_get_msglevel,
	.set_msglevel	= sh_eth_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_strings	= sh_eth_get_strings,
	.get_ethtool_stats  = sh_eth_get_ethtool_stats,
	.get_sset_count     = sh_eth_get_sset_count,
	.get_ringparam	= sh_eth_get_ringparam,
	.set_ringparam	= sh_eth_set_ringparam,
};

/* network device open function */
static int sh_eth_open(struct net_device *ndev)
{
	int ret = 0;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	pm_runtime_get_sync(&mdp->pdev->dev);

	ret = request_irq(ndev->irq, sh_eth_interrupt,
			  mdp->cd->irq_flags, ndev->name, ndev);
	if (ret) {
		dev_err(&ndev->dev, "Can not assign IRQ number\n");
		return ret;
	}

	/* Descriptor set */
	ret = sh_eth_ring_init(ndev);
	if (ret)
		goto out_free_irq;

	/* device init */
	ret = sh_eth_dev_init(ndev, true);
	if (ret)
		goto out_free_irq;

	/* PHY control start*/
	ret = sh_eth_phy_start(ndev);
	if (ret)
		goto out_free_irq;

	return ret;

out_free_irq:
	free_irq(ndev->irq, ndev);
	pm_runtime_put_sync(&mdp->pdev->dev);
	return ret;
}

/* Timeout function */
static void sh_eth_tx_timeout(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_rxdesc *rxdesc;
	int i;

	netif_stop_queue(ndev);

	if (netif_msg_timer(mdp))
		dev_err(&ndev->dev, "%s: transmit timed out, status %8.8x,"
	       " resetting...\n", ndev->name, (int)sh_eth_read(ndev, EESR));

	/* tx_errors count up */
	ndev->stats.tx_errors++;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < mdp->num_rx_ring; i++) {
		rxdesc = &mdp->rx_ring[i];
		rxdesc->status = 0;
		rxdesc->addr = 0xBADF00D0;
		if (mdp->rx_skbuff[i])
			dev_kfree_skb(mdp->rx_skbuff[i]);
		mdp->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < mdp->num_tx_ring; i++) {
		if (mdp->tx_skbuff[i])
			dev_kfree_skb(mdp->tx_skbuff[i]);
		mdp->tx_skbuff[i] = NULL;
	}

	/* device init */
	sh_eth_dev_init(ndev, true);
}

/* Packet transmit function */
static int sh_eth_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	u32 entry;
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);
	if ((mdp->cur_tx - mdp->dirty_tx) >= (mdp->num_tx_ring - 4)) {
		if (!sh_eth_txfree(ndev)) {
			if (netif_msg_tx_queued(mdp))
				dev_warn(&ndev->dev, "TxFD exhausted.\n");
			netif_stop_queue(ndev);
			spin_unlock_irqrestore(&mdp->lock, flags);
			return NETDEV_TX_BUSY;
		}
	}
	spin_unlock_irqrestore(&mdp->lock, flags);

	entry = mdp->cur_tx % mdp->num_tx_ring;
	mdp->tx_skbuff[entry] = skb;
	txdesc = &mdp->tx_ring[entry];
	/* soft swap. */
	if (!mdp->cd->hw_swap)
		sh_eth_soft_swap(phys_to_virt(ALIGN(txdesc->addr, 4)),
				 skb->len + 2);
	txdesc->addr = dma_map_single(&ndev->dev, skb->data, skb->len,
				      DMA_TO_DEVICE);
	if (skb->len < ETHERSMALL)
		txdesc->buffer_length = ETHERSMALL;
	else
		txdesc->buffer_length = skb->len;

	if (entry >= mdp->num_tx_ring - 1)
		txdesc->status |= cpu_to_edmac(mdp, TD_TACT | TD_TDLE);
	else
		txdesc->status |= cpu_to_edmac(mdp, TD_TACT);

	mdp->cur_tx++;

	if (!(sh_eth_read(ndev, EDTRR) & sh_eth_get_edtrr_trns(mdp)))
		sh_eth_write(ndev, sh_eth_get_edtrr_trns(mdp), EDTRR);

	return NETDEV_TX_OK;
}

/* device close function */
static int sh_eth_close(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	netif_stop_queue(ndev);

	/* Disable interrupts by clearing the interrupt mask. */
	sh_eth_write(ndev, 0x0000, EESIPR);

	/* Stop the chip's Tx and Rx processes. */
	sh_eth_write(ndev, 0, EDTRR);
	sh_eth_write(ndev, 0, EDRRR);

	/* PHY Disconnect */
	if (mdp->phydev) {
		phy_stop(mdp->phydev);
		phy_disconnect(mdp->phydev);
	}

	free_irq(ndev->irq, ndev);

	/* Free all the skbuffs in the Rx queue. */
	sh_eth_ring_free(ndev);

	/* free DMA buffer */
	sh_eth_free_dma_buffer(mdp);

	pm_runtime_put_sync(&mdp->pdev->dev);

	return 0;
}

static struct net_device_stats *sh_eth_get_stats(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	pm_runtime_get_sync(&mdp->pdev->dev);

	ndev->stats.tx_dropped += sh_eth_read(ndev, TROCR);
	sh_eth_write(ndev, 0, TROCR);	/* (write clear) */
	ndev->stats.collisions += sh_eth_read(ndev, CDCR);
	sh_eth_write(ndev, 0, CDCR);	/* (write clear) */
	ndev->stats.tx_carrier_errors += sh_eth_read(ndev, LCCR);
	sh_eth_write(ndev, 0, LCCR);	/* (write clear) */
	if (sh_eth_is_gether(mdp)) {
		ndev->stats.tx_carrier_errors += sh_eth_read(ndev, CERCR);
		sh_eth_write(ndev, 0, CERCR);	/* (write clear) */
		ndev->stats.tx_carrier_errors += sh_eth_read(ndev, CEECR);
		sh_eth_write(ndev, 0, CEECR);	/* (write clear) */
	} else {
		ndev->stats.tx_carrier_errors += sh_eth_read(ndev, CNDCR);
		sh_eth_write(ndev, 0, CNDCR);	/* (write clear) */
	}
	pm_runtime_put_sync(&mdp->pdev->dev);

	return &ndev->stats;
}

/* ioctl to device function */
static int sh_eth_do_ioctl(struct net_device *ndev, struct ifreq *rq,
				int cmd)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev = mdp->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, rq, cmd);
}

/* For TSU_POSTn. Please refer to the manual about this (strange) bitfields */
static void *sh_eth_tsu_get_post_reg_offset(struct sh_eth_private *mdp,
					    int entry)
{
	return sh_eth_tsu_get_offset(mdp, TSU_POST1) + (entry / 8 * 4);
}

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
	u32 tmp;
	void *reg_offset;

	reg_offset = sh_eth_tsu_get_post_reg_offset(mdp, entry);
	tmp = ioread32(reg_offset);
	iowrite32(tmp | sh_eth_tsu_get_post_bit(mdp, entry), reg_offset);
}

static bool sh_eth_tsu_disable_cam_entry_post(struct net_device *ndev,
					      int entry)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 post_mask, ref_mask, tmp;
	void *reg_offset;

	reg_offset = sh_eth_tsu_get_post_reg_offset(mdp, entry);
	post_mask = sh_eth_tsu_get_post_mask(entry);
	ref_mask = sh_eth_tsu_get_post_bit(mdp, entry) & ~post_mask;

	tmp = ioread32(reg_offset);
	iowrite32(tmp & ~post_mask, reg_offset);

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
			dev_err(&ndev->dev, "%s: timeout\n", __func__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int sh_eth_tsu_write_entry(struct net_device *ndev, void *reg,
				  const u8 *addr)
{
	u32 val;

	val = addr[0] << 24 | addr[1] << 16 | addr[2] << 8 | addr[3];
	iowrite32(val, reg);
	if (sh_eth_tsu_busy(ndev) < 0)
		return -EBUSY;

	val = addr[4] << 8 | addr[5];
	iowrite32(val, reg + 4);
	if (sh_eth_tsu_busy(ndev) < 0)
		return -EBUSY;

	return 0;
}

static void sh_eth_tsu_read_entry(void *reg, u8 *addr)
{
	u32 val;

	val = ioread32(reg);
	addr[0] = (val >> 24) & 0xff;
	addr[1] = (val >> 16) & 0xff;
	addr[2] = (val >> 8) & 0xff;
	addr[3] = val & 0xff;
	val = ioread32(reg + 4);
	addr[4] = (val >> 8) & 0xff;
	addr[5] = val & 0xff;
}


static int sh_eth_tsu_find_entry(struct net_device *ndev, const u8 *addr)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	void *reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	int i;
	u8 c_addr[ETH_ALEN];

	for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES; i++, reg_offset += 8) {
		sh_eth_tsu_read_entry(reg_offset, c_addr);
		if (memcmp(addr, c_addr, ETH_ALEN) == 0)
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
	void *reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
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
	void *reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
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

	if (unlikely(!mdp->cd->tsu))
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
	u8 addr[ETH_ALEN];
	void *reg_offset = sh_eth_tsu_get_offset(mdp, TSU_ADRH0);
	int i;

	if (unlikely(!mdp->cd->tsu))
		return;

	for (i = 0; i < SH_ETH_TSU_CAM_ENTRIES; i++, reg_offset += 8) {
		sh_eth_tsu_read_entry(reg_offset, addr);
		if (is_multicast_ether_addr(addr))
			sh_eth_tsu_del_entry(ndev, addr);
	}
}

/* Multicast reception directions set */
static void sh_eth_set_multicast_list(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ecmr_bits;
	int mcast_all = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);
	/*
	 * Initial condition is MCT = 1, PRM = 0.
	 * Depending on ndev->flags, set PRM or clear MCT
	 */
	ecmr_bits = (sh_eth_read(ndev, ECMR) & ~ECMR_PRM) | ECMR_MCT;

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
	} else {
		/* Normal, unicast/broadcast-only mode. */
		ecmr_bits = (ecmr_bits & ~ECMR_PRM) | ECMR_MCT;
	}

	/* update the ethernet mode */
	sh_eth_write(ndev, ecmr_bits, ECMR);

	spin_unlock_irqrestore(&mdp->lock, flags);
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

	/*
	 * The controller has one VLAN tag HW filter. So, if the filter is
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
	if (sh_eth_is_gether(mdp)) {
		sh_eth_tsu_write(mdp, 0, TSU_QTAG0);	/* Disable QTAG(0->1) */
		sh_eth_tsu_write(mdp, 0, TSU_QTAG1);	/* Disable QTAG(1->0) */
	} else {
		sh_eth_tsu_write(mdp, 0, TSU_QTAGM0);	/* Disable QTAG(0->1) */
		sh_eth_tsu_write(mdp, 0, TSU_QTAGM1);	/* Disable QTAG(1->0) */
	}
	sh_eth_tsu_write(mdp, 0, TSU_FWSR);	/* all interrupt status clear */
	sh_eth_tsu_write(mdp, 0, TSU_FWINMK);	/* Disable all interrupt */
	sh_eth_tsu_write(mdp, 0, TSU_TEN);	/* Disable all CAM entry */
	sh_eth_tsu_write(mdp, 0, TSU_POST1);	/* Disable CAM entry [ 0- 7] */
	sh_eth_tsu_write(mdp, 0, TSU_POST2);	/* Disable CAM entry [ 8-15] */
	sh_eth_tsu_write(mdp, 0, TSU_POST3);	/* Disable CAM entry [16-23] */
	sh_eth_tsu_write(mdp, 0, TSU_POST4);	/* Disable CAM entry [24-31] */
}

/* MDIO bus release function */
static int sh_mdio_release(struct net_device *ndev)
{
	struct mii_bus *bus = dev_get_drvdata(&ndev->dev);

	/* unregister mdio bus */
	mdiobus_unregister(bus);

	/* remove mdio bus info from net_device */
	dev_set_drvdata(&ndev->dev, NULL);

	/* free bitbang info */
	free_mdio_bitbang(bus);

	return 0;
}

/* MDIO bus init function */
static int sh_mdio_init(struct net_device *ndev, int id,
			struct sh_eth_plat_data *pd)
{
	int ret, i;
	struct bb_info *bitbang;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* create bit control struct for PHY */
	bitbang = devm_kzalloc(&ndev->dev, sizeof(struct bb_info),
			       GFP_KERNEL);
	if (!bitbang) {
		ret = -ENOMEM;
		goto out;
	}

	/* bitbang init */
	bitbang->addr = mdp->addr + mdp->reg_offset[PIR];
	bitbang->set_gate = pd->set_mdio_gate;
	bitbang->mdi_msk = PIR_MDI;
	bitbang->mdo_msk = PIR_MDO;
	bitbang->mmd_msk = PIR_MMD;
	bitbang->mdc_msk = PIR_MDC;
	bitbang->ctrl.ops = &bb_ops;

	/* MII controller setting */
	mdp->mii_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!mdp->mii_bus) {
		ret = -ENOMEM;
		goto out;
	}

	/* Hook up MII support for ethtool */
	mdp->mii_bus->name = "sh_mii";
	mdp->mii_bus->parent = &ndev->dev;
	snprintf(mdp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		mdp->pdev->name, id);

	/* PHY IRQ */
	mdp->mii_bus->irq = devm_kzalloc(&ndev->dev,
					 sizeof(int) * PHY_MAX_ADDR,
					 GFP_KERNEL);
	if (!mdp->mii_bus->irq) {
		ret = -ENOMEM;
		goto out_free_bus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		mdp->mii_bus->irq[i] = PHY_POLL;

	/* register mdio bus */
	ret = mdiobus_register(mdp->mii_bus);
	if (ret)
		goto out_free_bus;

	dev_set_drvdata(&ndev->dev, mdp->mii_bus);

	return 0;

out_free_bus:
	free_mdio_bitbang(mdp->mii_bus);

out:
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
	default:
		pr_err("Unknown register type (%d)\n", register_type);
		break;
	}

	return reg_offset;
}

static const struct net_device_ops sh_eth_netdev_ops = {
	.ndo_open		= sh_eth_open,
	.ndo_stop		= sh_eth_close,
	.ndo_start_xmit		= sh_eth_start_xmit,
	.ndo_get_stats		= sh_eth_get_stats,
	.ndo_tx_timeout		= sh_eth_tx_timeout,
	.ndo_do_ioctl		= sh_eth_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static const struct net_device_ops sh_eth_netdev_ops_tsu = {
	.ndo_open		= sh_eth_open,
	.ndo_stop		= sh_eth_close,
	.ndo_start_xmit		= sh_eth_start_xmit,
	.ndo_get_stats		= sh_eth_get_stats,
	.ndo_set_rx_mode	= sh_eth_set_multicast_list,
	.ndo_vlan_rx_add_vid	= sh_eth_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= sh_eth_vlan_rx_kill_vid,
	.ndo_tx_timeout		= sh_eth_tx_timeout,
	.ndo_do_ioctl		= sh_eth_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int sh_eth_drv_probe(struct platform_device *pdev)
{
	int ret, devno = 0;
	struct resource *res;
	struct net_device *ndev = NULL;
	struct sh_eth_private *mdp = NULL;
	struct sh_eth_plat_data *pd = pdev->dev.platform_data;
	const struct platform_device_id *id = platform_get_device_id(pdev);

	/* get base addr */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "invalid resource\n");
		ret = -EINVAL;
		goto out;
	}

	ndev = alloc_etherdev(sizeof(struct sh_eth_private));
	if (!ndev) {
		ret = -ENOMEM;
		goto out;
	}

	/* The sh Ether-specific entries in the device structure. */
	ndev->base_addr = res->start;
	devno = pdev->id;
	if (devno < 0)
		devno = 0;

	ndev->dma = -1;
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		ret = -ENODEV;
		goto out_release;
	}
	ndev->irq = ret;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(ndev);

	mdp = netdev_priv(ndev);
	mdp->num_tx_ring = TX_RING_SIZE;
	mdp->num_rx_ring = RX_RING_SIZE;
	mdp->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdp->addr)) {
		ret = PTR_ERR(mdp->addr);
		goto out_release;
	}

	spin_lock_init(&mdp->lock);
	mdp->pdev = pdev;
	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	/* get PHY ID */
	mdp->phy_id = pd->phy;
	mdp->phy_interface = pd->phy_interface;
	/* EDMAC endian */
	mdp->edmac_endian = pd->edmac_endian;
	mdp->no_ether_link = pd->no_ether_link;
	mdp->ether_link_active_low = pd->ether_link_active_low;
	mdp->reg_offset = sh_eth_get_register_offset(pd->register_type);

	/* set cpu data */
	mdp->cd = (struct sh_eth_cpu_data *)id->driver_data;
	sh_eth_set_default_cpu_data(mdp->cd);

	/* set function */
	if (mdp->cd->tsu)
		ndev->netdev_ops = &sh_eth_netdev_ops_tsu;
	else
		ndev->netdev_ops = &sh_eth_netdev_ops;
	SET_ETHTOOL_OPS(ndev, &sh_eth_ethtool_ops);
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

	/* ioremap the TSU registers */
	if (mdp->cd->tsu) {
		struct resource *rtsu;
		rtsu = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		mdp->tsu_addr = devm_ioremap_resource(&pdev->dev, rtsu);
		if (IS_ERR(mdp->tsu_addr)) {
			ret = PTR_ERR(mdp->tsu_addr);
			goto out_release;
		}
		mdp->port = devno % 2;
		ndev->features = NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	/* initialize first or needed device */
	if (!devno || pd->needs_init) {
		if (mdp->cd->chip_reset)
			mdp->cd->chip_reset(ndev);

		if (mdp->cd->tsu) {
			/* TSU init (Init only)*/
			sh_eth_tsu_init(mdp);
		}
	}

	/* network device register */
	ret = register_netdev(ndev);
	if (ret)
		goto out_release;

	/* mdio bus init */
	ret = sh_mdio_init(ndev, pdev->id, pd);
	if (ret)
		goto out_unregister;

	/* print device information */
	pr_info("Base address at 0x%x, %pM, IRQ %d.\n",
	       (u32)ndev->base_addr, ndev->dev_addr, ndev->irq);

	platform_set_drvdata(pdev, ndev);

	return ret;

out_unregister:
	unregister_netdev(ndev);

out_release:
	/* net_dev free */
	if (ndev)
		free_netdev(ndev);

out:
	return ret;
}

static int sh_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	sh_mdio_release(ndev);
	unregister_netdev(ndev);
	pm_runtime_disable(&pdev->dev);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int sh_eth_runtime_nop(struct device *dev)
{
	/*
	 * Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops sh_eth_dev_pm_ops = {
	.runtime_suspend = sh_eth_runtime_nop,
	.runtime_resume = sh_eth_runtime_nop,
};
#define SH_ETH_PM_OPS (&sh_eth_dev_pm_ops)
#else
#define SH_ETH_PM_OPS NULL
#endif

static struct platform_device_id sh_eth_id_table[] = {
	{ "sh7619-ether", (kernel_ulong_t)&sh7619_data },
	{ "sh771x-ether", (kernel_ulong_t)&sh771x_data },
	{ "sh7724-ether", (kernel_ulong_t)&sh7724_data },
	{ "sh7734-gether", (kernel_ulong_t)&sh7734_data },
	{ "sh7757-ether", (kernel_ulong_t)&sh7757_data },
	{ "sh7757-gether", (kernel_ulong_t)&sh7757_data_giga },
	{ "sh7763-gether", (kernel_ulong_t)&sh7763_data },
	{ "r8a7740-gether", (kernel_ulong_t)&r8a7740_data },
	{ "r8a777x-ether", (kernel_ulong_t)&r8a777x_data },
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
	},
};

module_platform_driver(sh_eth_driver);

MODULE_AUTHOR("Nobuhiro Iwamatsu, Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas SuperH Ethernet driver");
MODULE_LICENSE("GPL v2");
