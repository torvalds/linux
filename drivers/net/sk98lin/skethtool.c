/******************************************************************************
 *
 * Name:        skethtool.c
 * Project:     GEnesis, PCI Gigabit Ethernet Adapter
 * Version:     $Revision: 1.7 $
 * Date:        $Date: 2004/09/29 13:32:07 $
 * Purpose:     All functions regarding ethtool handling
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2004 Marvell.
 *
 *	Driver for Marvell Yukon/2 chipset and SysKonnect Gigabit Ethernet 
 *      Server Adapters.
 *
 *	Author: Ralph Roesler (rroesler@syskonnect.de)
 *	        Mirko Lindner (mlindner@syskonnect.de)
 *
 *	Address all question to: linux@syskonnect.de
 *
 *	The technical manual for the adapters is available from SysKonnect's
 *	web pages: www.syskonnect.com
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 *****************************************************************************/

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"
#include "h/skversion.h"

#include <linux/ethtool.h>
#include <linux/timer.h>
#include <linux/delay.h>

/******************************************************************************
 *
 * Defines
 *
 *****************************************************************************/

#define SUPP_COPPER_ALL (SUPPORTED_10baseT_Half  | SUPPORTED_10baseT_Full  | \
                         SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
                         SUPPORTED_1000baseT_Half| SUPPORTED_1000baseT_Full| \
                         SUPPORTED_TP)

#define ADV_COPPER_ALL  (ADVERTISED_10baseT_Half  | ADVERTISED_10baseT_Full  | \
                         ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full | \
                         ADVERTISED_1000baseT_Half| ADVERTISED_1000baseT_Full| \
                         ADVERTISED_TP)

#define SUPP_FIBRE_ALL  (SUPPORTED_1000baseT_Full | \
                         SUPPORTED_FIBRE          | \
                         SUPPORTED_Autoneg)

#define ADV_FIBRE_ALL   (ADVERTISED_1000baseT_Full | \
                         ADVERTISED_FIBRE          | \
                         ADVERTISED_Autoneg)


/******************************************************************************
 *
 * Local Functions
 *
 *****************************************************************************/

/*****************************************************************************
 *
 * 	getSettings - retrieves the current settings of the selected adapter
 *
 * Description:
 *	The current configuration of the selected adapter is returned.
 *	This configuration involves a)speed, b)duplex and c)autoneg plus
 *	a number of other variables.
 *
 * Returns:    always 0
 *
 */
static int getSettings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	const DEV_NET *pNet = netdev_priv(dev);
	int port = pNet->PortNr;
	const SK_AC *pAC = pNet->pAC;
	const SK_GEPORT *pPort = &pAC->GIni.GP[port];

	static int DuplexAutoNegConfMap[9][3]= {
		{ -1                     , -1         , -1              },
		{ 0                      , -1         , -1              },
		{ SK_LMODE_HALF          , DUPLEX_HALF, AUTONEG_DISABLE },
		{ SK_LMODE_FULL          , DUPLEX_FULL, AUTONEG_DISABLE },
		{ SK_LMODE_AUTOHALF      , DUPLEX_HALF, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOFULL      , DUPLEX_FULL, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOBOTH      , DUPLEX_FULL, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOSENSE     , -1         , -1              },
		{ SK_LMODE_INDETERMINATED, -1         , -1              }
	};
	static int SpeedConfMap[6][2] = {
		{ 0                       , -1         },
		{ SK_LSPEED_AUTO          , -1         },
		{ SK_LSPEED_10MBPS        , SPEED_10   },
		{ SK_LSPEED_100MBPS       , SPEED_100  },
		{ SK_LSPEED_1000MBPS      , SPEED_1000 },
		{ SK_LSPEED_INDETERMINATED, -1         }
	};
	static int AdvSpeedMap[6][2] = {
		{ 0                       , -1         },
		{ SK_LSPEED_AUTO          , -1         },
		{ SK_LSPEED_10MBPS        , ADVERTISED_10baseT_Half   | ADVERTISED_10baseT_Full },
		{ SK_LSPEED_100MBPS       , ADVERTISED_100baseT_Half  | ADVERTISED_100baseT_Full },
		{ SK_LSPEED_1000MBPS      , ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full},
		{ SK_LSPEED_INDETERMINATED, -1         }
	};

	ecmd->phy_address = port;
	ecmd->speed       = SpeedConfMap[pPort->PLinkSpeedUsed][1];
	ecmd->duplex      = DuplexAutoNegConfMap[pPort->PLinkModeStatus][1];
	ecmd->autoneg     = DuplexAutoNegConfMap[pPort->PLinkModeStatus][2];
	ecmd->transceiver = XCVR_INTERNAL;

	if (pAC->GIni.GICopperType) {
		ecmd->port        = PORT_TP;
		ecmd->supported   = (SUPP_COPPER_ALL|SUPPORTED_Autoneg);
		if (pAC->GIni.GIGenesis) {
			ecmd->supported &= ~(SUPPORTED_10baseT_Half);
			ecmd->supported &= ~(SUPPORTED_10baseT_Full);
			ecmd->supported &= ~(SUPPORTED_100baseT_Half);
			ecmd->supported &= ~(SUPPORTED_100baseT_Full);
		} else {
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON) {
				ecmd->supported &= ~(SUPPORTED_1000baseT_Half);
			} 
#ifdef CHIP_ID_YUKON_FE
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON_FE) {
				ecmd->supported &= ~(SUPPORTED_1000baseT_Half);
				ecmd->supported &= ~(SUPPORTED_1000baseT_Full);
			}
#endif
		}
		if (pAC->GIni.GP[0].PLinkSpeed != SK_LSPEED_AUTO) {
			ecmd->advertising = AdvSpeedMap[pPort->PLinkSpeed][1];
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON) {
				ecmd->advertising &= ~(SUPPORTED_1000baseT_Half);
			} 
		} else {
			ecmd->advertising = ecmd->supported;
		}

		if (ecmd->autoneg == AUTONEG_ENABLE) 
			ecmd->advertising |= ADVERTISED_Autoneg;
	} else {
		ecmd->port        = PORT_FIBRE;
		ecmd->supported   = SUPP_FIBRE_ALL;
		ecmd->advertising = ADV_FIBRE_ALL;
	}
	return 0;
}

/*
 * MIB infrastructure uses instance value starting at 1
 * based on board and port.
 */
static inline u32 pnmiInstance(const DEV_NET *pNet)
{
	return 1 + (pNet->pAC->RlmtNets == 2) + pNet->PortNr;
}

/*****************************************************************************
 *
 *	setSettings - configures the settings of a selected adapter
 *
 * Description:
 *	Possible settings that may be altered are a)speed, b)duplex or 
 *	c)autonegotiation.
 *
 * Returns:
 *	0:	everything fine, no error
 *	<0:	the return value is the error code of the failure 
 */
static int setSettings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	u32 instance;
	char buf[4];
	int len = 1;

	if (ecmd->speed != SPEED_10 && ecmd->speed != SPEED_100 
	    && ecmd->speed != SPEED_1000)
		return -EINVAL;

	if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
		return -EINVAL;

	if (ecmd->autoneg != AUTONEG_DISABLE && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	if (ecmd->autoneg == AUTONEG_DISABLE)
		*buf = (ecmd->duplex == DUPLEX_FULL) 
			? SK_LMODE_FULL : SK_LMODE_HALF;
	else
		*buf = (ecmd->duplex == DUPLEX_FULL) 
			? SK_LMODE_AUTOFULL : SK_LMODE_AUTOHALF;
	
	instance = pnmiInstance(pNet);
	if (SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_LINK_MODE, 
			   &buf, &len, instance, pNet->NetNr) != SK_PNMI_ERR_OK)
		return -EINVAL;

	switch(ecmd->speed) {
	case SPEED_1000:
		*buf = SK_LSPEED_1000MBPS;
		break;
	case SPEED_100:
		*buf = SK_LSPEED_100MBPS;
		break;
	case SPEED_10:
		*buf = SK_LSPEED_10MBPS;
	}

	if (SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_SPEED_MODE, 
			 &buf, &len, instance, pNet->NetNr) != SK_PNMI_ERR_OK)
		return -EINVAL;

	return 0;
}

/*****************************************************************************
 *
 * 	getDriverInfo - returns generic driver and adapter information
 *
 * Description:
 *	Generic driver information is returned via this function, such as
 *	the name of the driver, its version and and firmware version.
 *	In addition to this, the location of the selected adapter is 
 *	returned as a bus info string (e.g. '01:05.0').
 *	
 * Returns:	N/A
 *
 */
static void getDriverInfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	const DEV_NET	*pNet = netdev_priv(dev);
	const SK_AC *pAC = pNet->pAC;
	char vers[32];

	snprintf(vers, sizeof(vers)-1, VER_STRING "(v%d.%d)",
		(pAC->GIni.GIPciHwRev >> 4) & 0xf, pAC->GIni.GIPciHwRev & 0xf);

	strlcpy(info->driver, DRIVER_FILE_NAME, sizeof(info->driver));
	strcpy(info->version, vers);
	strcpy(info->fw_version, "N/A");
	strlcpy(info->bus_info, pci_name(pAC->PciDev), ETHTOOL_BUSINFO_LEN);
}

/*
 * Ethtool statistics support.
 */
static const char StringsStats[][ETH_GSTRING_LEN] = {
	"rx_packets",	"tx_packets",
	"rx_bytes",	"tx_bytes",
	"rx_errors",	"tx_errors",	
	"rx_dropped",	"tx_dropped",
	"multicasts",	"collisions",	
	"rx_length_errors",		"rx_buffer_overflow_errors",
	"rx_crc_errors",		"rx_frame_errors",
	"rx_too_short_errors",		"rx_too_long_errors",
	"rx_carrier_extension_errors",	"rx_symbol_errors",
	"rx_llc_mac_size_errors",	"rx_carrier_errors",	
	"rx_jabber_errors",		"rx_missed_errors",
	"tx_abort_collision_errors",	"tx_carrier_errors",
	"tx_buffer_underrun_errors",	"tx_heartbeat_errors",
	"tx_window_errors",
};

static int getStatsCount(struct net_device *dev)
{
	return ARRAY_SIZE(StringsStats);
}

static void getStrings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *StringsStats, sizeof(StringsStats));
		break;
	}
}

static void getEthtoolStats(struct net_device *dev,
			    struct ethtool_stats *stats, u64 *data)
{
	const DEV_NET	*pNet = netdev_priv(dev);
	const SK_AC *pAC = pNet->pAC;
	const SK_PNMI_STRUCT_DATA *pPnmiStruct = &pAC->PnmiStruct;

	*data++ = pPnmiStruct->Stat[0].StatRxOkCts;
	*data++ = pPnmiStruct->Stat[0].StatTxOkCts;
	*data++ = pPnmiStruct->Stat[0].StatRxOctetsOkCts;
	*data++ = pPnmiStruct->Stat[0].StatTxOctetsOkCts;
	*data++ = pPnmiStruct->InErrorsCts;
	*data++ = pPnmiStruct->Stat[0].StatTxSingleCollisionCts;
	*data++ = pPnmiStruct->RxNoBufCts;
	*data++ = pPnmiStruct->TxNoBufCts;
	*data++ = pPnmiStruct->Stat[0].StatRxMulticastOkCts;
	*data++ = pPnmiStruct->Stat[0].StatTxSingleCollisionCts;
	*data++ = pPnmiStruct->Stat[0].StatRxRuntCts;
	*data++ = pPnmiStruct->Stat[0].StatRxFifoOverflowCts;
	*data++ = pPnmiStruct->Stat[0].StatRxFcsCts;
	*data++ = pPnmiStruct->Stat[0].StatRxFramingCts;
	*data++ = pPnmiStruct->Stat[0].StatRxShortsCts;
	*data++ = pPnmiStruct->Stat[0].StatRxTooLongCts;
	*data++ = pPnmiStruct->Stat[0].StatRxCextCts;
	*data++ = pPnmiStruct->Stat[0].StatRxSymbolCts;
	*data++ = pPnmiStruct->Stat[0].StatRxIRLengthCts;
	*data++ = pPnmiStruct->Stat[0].StatRxCarrierCts;
	*data++ = pPnmiStruct->Stat[0].StatRxJabberCts;
	*data++ = pPnmiStruct->Stat[0].StatRxMissedCts;
	*data++ = pAC->stats.tx_aborted_errors;
	*data++ = pPnmiStruct->Stat[0].StatTxCarrierCts;
	*data++ = pPnmiStruct->Stat[0].StatTxFifoUnderrunCts;
	*data++ = pPnmiStruct->Stat[0].StatTxCarrierCts;
	*data++ = pAC->stats.tx_window_errors;
}


/*****************************************************************************
 *
 * 	toggleLeds - Changes the LED state of an adapter
 *
 * Description:
 *	This function changes the current state of all LEDs of an adapter so
 *	that it can be located by a user. 
 *
 * Returns:	N/A
 *
 */
static void toggleLeds(DEV_NET *pNet, int on)
{
	SK_AC *pAC = pNet->pAC;
	int port = pNet->PortNr;
	void __iomem *io = pAC->IoBase;

	if (pAC->GIni.GIGenesis) {
		SK_OUT8(io, MR_ADDR(port,LNK_LED_REG), 
			on ? SK_LNK_ON : SK_LNK_OFF);
		SkGeYellowLED(pAC, io, 
			      on ? (LED_ON >> 1) : (LED_OFF >> 1));
		SkGeXmitLED(pAC, io, MR_ADDR(port,RX_LED_INI),
			    on ? SK_LED_TST : SK_LED_DIS);

		if (pAC->GIni.GP[port].PhyType == SK_PHY_BCOM)
			SkXmPhyWrite(pAC, io, port, PHY_BCOM_P_EXT_CTRL, 
				     on ? PHY_B_PEC_LED_ON : PHY_B_PEC_LED_OFF);
		else if (pAC->GIni.GP[port].PhyType == SK_PHY_LONE)
			SkXmPhyWrite(pAC, io, port, PHY_LONE_LED_CFG,
				     on ? 0x0800 : PHY_L_LC_LEDT);
		else
			SkGeXmitLED(pAC, io, MR_ADDR(port,TX_LED_INI),
				    on ? SK_LED_TST : SK_LED_DIS);
	} else {
		const u16 YukLedOn = (PHY_M_LED_MO_DUP(MO_LED_ON)  |
				      PHY_M_LED_MO_10(MO_LED_ON)   |
				      PHY_M_LED_MO_100(MO_LED_ON)  |
				      PHY_M_LED_MO_1000(MO_LED_ON) | 
				      PHY_M_LED_MO_RX(MO_LED_ON));
		const u16  YukLedOff = (PHY_M_LED_MO_DUP(MO_LED_OFF)  |
					PHY_M_LED_MO_10(MO_LED_OFF)   |
					PHY_M_LED_MO_100(MO_LED_OFF)  |
					PHY_M_LED_MO_1000(MO_LED_OFF) | 
					PHY_M_LED_MO_RX(MO_LED_OFF));
	

		SkGmPhyWrite(pAC,io,port,PHY_MARV_LED_CTRL,0);
		SkGmPhyWrite(pAC,io,port,PHY_MARV_LED_OVER, 
			     on ? YukLedOn : YukLedOff);
	}
}

/*****************************************************************************
 *
 * 	skGeBlinkTimer - Changes the LED state of an adapter
 *
 * Description:
 *	This function changes the current state of all LEDs of an adapter so
 *	that it can be located by a user. If the requested time interval for
 *	this test has elapsed, this function cleans up everything that was 
 *	temporarily setup during the locate NIC test. This involves of course
 *	also closing or opening any adapter so that the initial board state 
 *	is recovered.
 *
 * Returns:	N/A
 *
 */
void SkGeBlinkTimer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	toggleLeds(pNet, pAC->LedsOn);

	pAC->LedsOn = !pAC->LedsOn;
	mod_timer(&pAC->BlinkTimer, jiffies + HZ/4);
}

/*****************************************************************************
 *
 * 	locateDevice - start the locate NIC feature of the elected adapter 
 *
 * Description:
 *	This function is used if the user want to locate a particular NIC.
 *	All LEDs are regularly switched on and off, so the NIC can easily
 *	be identified.
 *
 * Returns:	
 *	==0:	everything fine, no error, locateNIC test was started
 *	!=0:	one locateNIC test runs already
 *
 */
static int locateDevice(struct net_device *dev, u32 data)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	if(!data || data > (u32)(MAX_SCHEDULE_TIMEOUT / HZ))
		data = (u32)(MAX_SCHEDULE_TIMEOUT / HZ);

	/* start blinking */
	pAC->LedsOn = 0;
	mod_timer(&pAC->BlinkTimer, jiffies);
	msleep_interruptible(data * 1000);
	del_timer_sync(&pAC->BlinkTimer);
	toggleLeds(pNet, 0);

	return 0;
}

/*****************************************************************************
 *
 * 	getPauseParams - retrieves the pause parameters
 *
 * Description:
 *	All current pause parameters of a selected adapter are placed 
 *	in the passed ethtool_pauseparam structure and are returned.
 *
 * Returns:	N/A
 *
 */
static void getPauseParams(struct net_device *dev, struct ethtool_pauseparam *epause) 
{
	DEV_NET	*pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	SK_GEPORT *pPort = &pAC->GIni.GP[pNet->PortNr];

	epause->rx_pause = (pPort->PFlowCtrlMode == SK_FLOW_MODE_SYMMETRIC) ||
		  (pPort->PFlowCtrlMode == SK_FLOW_MODE_SYM_OR_REM);

	epause->tx_pause = epause->rx_pause || (pPort->PFlowCtrlMode == SK_FLOW_MODE_LOC_SEND);
	epause->autoneg = epause->rx_pause || epause->tx_pause;
}

/*****************************************************************************
 *
 *	setPauseParams - configures the pause parameters of an adapter
 *
 * Description:
 *	This function sets the Rx or Tx pause parameters 
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 */
static int setPauseParams(struct net_device *dev , struct ethtool_pauseparam *epause)
{
	DEV_NET	*pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	SK_GEPORT *pPort = &pAC->GIni.GP[pNet->PortNr];
	u32	instance = pnmiInstance(pNet);
	struct ethtool_pauseparam old;
	u8	oldspeed = pPort->PLinkSpeedUsed;
	char	buf[4];
	int	len = 1;
	int ret;

	/*
	** we have to determine the current settings to see if 
	** the operator requested any modification of the flow 
	** control parameters...
	*/
	getPauseParams(dev, &old);

	/*
	** perform modifications regarding the changes 
	** requested by the operator
	*/
	if (epause->autoneg != old.autoneg) 
		*buf = epause->autoneg ? SK_FLOW_MODE_NONE : SK_FLOW_MODE_SYMMETRIC;
	else {
		if (epause->rx_pause && epause->tx_pause) 
			*buf = SK_FLOW_MODE_SYMMETRIC;
		else if (epause->rx_pause && !epause->tx_pause)
			*buf =  SK_FLOW_MODE_SYM_OR_REM;
		else if (!epause->rx_pause && epause->tx_pause)
			*buf =  SK_FLOW_MODE_LOC_SEND;
		else
			*buf = SK_FLOW_MODE_NONE;
	}

	ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_FLOWCTRL_MODE,
			 &buf, &len, instance, pNet->NetNr);

	if (ret != SK_PNMI_ERR_OK) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_CTRL,
			   ("ethtool (sk98lin): error changing rx/tx pause (%i)\n", ret));
		goto err;
	}

	/*
	** It may be that autoneg has been disabled! Therefore
	** set the speed to the previously used value...
	*/
	if (!epause->autoneg) {
		len = 1;
		ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_SPEED_MODE, 
				   &oldspeed, &len, instance, pNet->NetNr);
		if (ret != SK_PNMI_ERR_OK) 
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_CTRL,
				   ("ethtool (sk98lin): error setting speed (%i)\n", ret));
	}
 err:
        return ret ? -EIO : 0;
}

/* Only Yukon supports checksum offload. */
static int setScatterGather(struct net_device *dev, u32 data)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	if (pAC->GIni.GIChipId == CHIP_ID_GENESIS)
		return -EOPNOTSUPP;
	return ethtool_op_set_sg(dev, data);
}

static int setTxCsum(struct net_device *dev, u32 data)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	if (pAC->GIni.GIChipId == CHIP_ID_GENESIS)
		return -EOPNOTSUPP;

	return ethtool_op_set_tx_csum(dev, data);
}

static u32 getRxCsum(struct net_device *dev)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	return pAC->RxPort[pNet->PortNr].RxCsum;
}

static int setRxCsum(struct net_device *dev, u32 data)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;

	if (pAC->GIni.GIChipId == CHIP_ID_GENESIS)
		return -EOPNOTSUPP;

	pAC->RxPort[pNet->PortNr].RxCsum = data != 0;
	return 0;
}

struct ethtool_ops SkGeEthtoolOps = {
	.get_settings		= getSettings,
	.set_settings		= setSettings,
	.get_drvinfo		= getDriverInfo,
	.get_strings		= getStrings,
	.get_stats_count	= getStatsCount,
	.get_ethtool_stats	= getEthtoolStats,
	.phys_id		= locateDevice,
	.get_pauseparam		= getPauseParams,
	.set_pauseparam		= setPauseParams,
	.get_link		= ethtool_op_get_link,
	.get_perm_addr		= ethtool_op_get_perm_addr,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= setScatterGather,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= setTxCsum,
	.get_rx_csum		= getRxCsum,
	.set_rx_csum		= setRxCsum,
};
