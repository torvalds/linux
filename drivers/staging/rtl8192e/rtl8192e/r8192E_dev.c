/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h"
#include "r8192E_cmdpkt.h"
#include "rtl_dm.h"
#include "rtl_wx.h"

static int WDCAPARA_ADD[] = {EDCAPARA_BE, EDCAPARA_BK, EDCAPARA_VI,
			     EDCAPARA_VO};

void rtl92e_start_beacon(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	struct rtllib_network *net = &priv->rtllib->current_network;
	u16 BcnTimeCfg = 0;
	u16 BcnCW = 6;
	u16 BcnIFS = 0xf;

	rtl92e_irq_disable(dev);

	rtl92e_writew(dev, ATIMWND, 2);

	rtl92e_writew(dev, BCN_INTERVAL, net->beacon_interval);
	rtl92e_writew(dev, BCN_DRV_EARLY_INT, 10);
	rtl92e_writew(dev, BCN_DMATIME, 256);

	rtl92e_writeb(dev, BCN_ERR_THRESH, 100);

	BcnTimeCfg |= BcnCW<<BCN_TCFG_CW_SHIFT;
	BcnTimeCfg |= BcnIFS<<BCN_TCFG_IFS;
	rtl92e_writew(dev, BCN_TCFG, BcnTimeCfg);
	rtl92e_irq_enable(dev);
}

static void _rtl92e_update_msr(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 msr;
	enum led_ctl_mode LedAction = LED_CTL_NO_LINK;

	msr  = rtl92e_readb(dev, MSR);
	msr &= ~MSR_LINK_MASK;

	switch (priv->rtllib->iw_mode) {
	case IW_MODE_INFRA:
		if (priv->rtllib->state == RTLLIB_LINKED)
			msr |= (MSR_LINK_MANAGED << MSR_LINK_SHIFT);
		else
			msr |= (MSR_LINK_NONE << MSR_LINK_SHIFT);
		LedAction = LED_CTL_LINK;
		break;
	case IW_MODE_ADHOC:
		if (priv->rtllib->state == RTLLIB_LINKED)
			msr |= (MSR_LINK_ADHOC << MSR_LINK_SHIFT);
		else
			msr |= (MSR_LINK_NONE << MSR_LINK_SHIFT);
		break;
	case IW_MODE_MASTER:
		if (priv->rtllib->state == RTLLIB_LINKED)
			msr |= (MSR_LINK_MASTER << MSR_LINK_SHIFT);
		else
			msr |= (MSR_LINK_NONE << MSR_LINK_SHIFT);
		break;
	default:
		break;
	}

	rtl92e_writeb(dev, MSR, msr);
	if (priv->rtllib->LedControlHandler)
		priv->rtllib->LedControlHandler(dev, LedAction);
}

void rtl92e_set_reg(struct net_device *dev, u8 variable, u8 *val)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	switch (variable) {
	case HW_VAR_BSSID:
		rtl92e_writel(dev, BSSIDR, ((u32 *)(val))[0]);
		rtl92e_writew(dev, BSSIDR+2, ((u16 *)(val+2))[0]);
		break;

	case HW_VAR_MEDIA_STATUS:
	{
		enum rt_op_mode OpMode = *((enum rt_op_mode *)(val));
		u8 btMsr = rtl92e_readb(dev, MSR);

		btMsr &= 0xfc;

		switch (OpMode) {
		case RT_OP_MODE_INFRASTRUCTURE:
			btMsr |= MSR_INFRA;
			break;

		case RT_OP_MODE_IBSS:
			btMsr |= MSR_ADHOC;
			break;

		case RT_OP_MODE_AP:
			btMsr |= MSR_AP;
			break;

		default:
			btMsr |= MSR_NOLINK;
			break;
		}

		rtl92e_writeb(dev, MSR, btMsr);

	}
	break;

	case HW_VAR_CECHK_BSSID:
	{
		u32	RegRCR, Type;

		Type = ((u8 *)(val))[0];
		RegRCR = rtl92e_readl(dev, RCR);
		priv->ReceiveConfig = RegRCR;

		if (Type == true)
			RegRCR |= (RCR_CBSSID);
		else if (Type == false)
			RegRCR &= (~RCR_CBSSID);

		rtl92e_writel(dev, RCR, RegRCR);
		priv->ReceiveConfig = RegRCR;

	}
	break;

	case HW_VAR_SLOT_TIME:

		priv->slot_time = val[0];
		rtl92e_writeb(dev, SLOT_TIME, val[0]);

		break;

	case HW_VAR_ACK_PREAMBLE:
	{
		u32 regTmp;

		priv->short_preamble = (bool)(*(u8 *)val);
		regTmp = priv->basic_rate;
		if (priv->short_preamble)
			regTmp |= BRSR_AckShortPmb;
		rtl92e_writel(dev, RRSR, regTmp);
		break;
	}

	case HW_VAR_CPU_RST:
		rtl92e_writel(dev, CPU_GEN, ((u32 *)(val))[0]);
		break;

	case HW_VAR_AC_PARAM:
	{
		u8	pAcParam = *((u8 *)val);
		u32	eACI = pAcParam;
		u8		u1bAIFS;
		u32		u4bAcParam;
		u8 mode = priv->rtllib->mode;
		struct rtllib_qos_parameters *qop =
			 &priv->rtllib->current_network.qos_data.parameters;

		u1bAIFS = qop->aifs[pAcParam] *
			  ((mode&(IEEE_G|IEEE_N_24G)) ? 9 : 20) + aSifsTime;

		rtl92e_dm_init_edca_turbo(dev);

		u4bAcParam = (le16_to_cpu(qop->tx_op_limit[pAcParam]) <<
			      AC_PARAM_TXOP_LIMIT_OFFSET) |
				((le16_to_cpu(qop->cw_max[pAcParam])) <<
				 AC_PARAM_ECW_MAX_OFFSET) |
				((le16_to_cpu(qop->cw_min[pAcParam])) <<
				 AC_PARAM_ECW_MIN_OFFSET) |
				(((u32)u1bAIFS) << AC_PARAM_AIFS_OFFSET);

		RT_TRACE(COMP_DBG, "%s():HW_VAR_AC_PARAM eACI:%x:%x\n",
			 __func__, eACI, u4bAcParam);
		switch (eACI) {
		case AC1_BK:
			rtl92e_writel(dev, EDCAPARA_BK, u4bAcParam);
			break;

		case AC0_BE:
			rtl92e_writel(dev, EDCAPARA_BE, u4bAcParam);
			break;

		case AC2_VI:
			rtl92e_writel(dev, EDCAPARA_VI, u4bAcParam);
			break;

		case AC3_VO:
			rtl92e_writel(dev, EDCAPARA_VO, u4bAcParam);
			break;

		default:
			netdev_info(dev, "SetHwReg8185(): invalid ACI: %d !\n",
				    eACI);
			break;
		}
		priv->rtllib->SetHwRegHandler(dev, HW_VAR_ACM_CTRL,
					      (u8 *)(&pAcParam));
		break;
	}

	case HW_VAR_ACM_CTRL:
	{
		struct rtllib_qos_parameters *qos_parameters =
			 &priv->rtllib->current_network.qos_data.parameters;
		u8 pAcParam = *((u8 *)val);
		u32 eACI = pAcParam;
		union aci_aifsn *pAciAifsn = (union aci_aifsn *) &
					      (qos_parameters->aifs[0]);
		u8 acm = pAciAifsn->f.acm;
		u8 AcmCtrl = rtl92e_readb(dev, AcmHwCtrl);

		RT_TRACE(COMP_DBG, "===========>%s():HW_VAR_ACM_CTRL:%x\n",
			 __func__, eACI);
		AcmCtrl = AcmCtrl | ((priv->AcmMethod == 2) ? 0x0 : 0x1);

		if (acm) {
			switch (eACI) {
			case AC0_BE:
				AcmCtrl |= AcmHw_BeqEn;
				break;

			case AC2_VI:
				AcmCtrl |= AcmHw_ViqEn;
				break;

			case AC3_VO:
				AcmCtrl |= AcmHw_VoqEn;
				break;

			default:
				RT_TRACE(COMP_QOS,
					 "SetHwReg8185(): [HW_VAR_ACM_CTRL] acm set failed: eACI is %d\n",
					 eACI);
				break;
			}
		} else {
			switch (eACI) {
			case AC0_BE:
				AcmCtrl &= (~AcmHw_BeqEn);
				break;

			case AC2_VI:
				AcmCtrl &= (~AcmHw_ViqEn);
				break;

			case AC3_VO:
				AcmCtrl &= (~AcmHw_BeqEn);
				break;

			default:
				break;
			}
		}

		RT_TRACE(COMP_QOS,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			 AcmCtrl);
		rtl92e_writeb(dev, AcmHwCtrl, AcmCtrl);
		break;
	}

	case HW_VAR_SIFS:
		rtl92e_writeb(dev, SIFS, val[0]);
		rtl92e_writeb(dev, SIFS+1, val[0]);
		break;

	case HW_VAR_RF_TIMING:
	{
		u8 Rf_Timing = *((u8 *)val);

		rtl92e_writeb(dev, rFPGA0_RFTiming1, Rf_Timing);
		break;
	}

	default:
		break;
	}

}

static void _rtl92e_read_eeprom_info(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	const u8 bMac_Tmp_Addr[ETH_ALEN] = {0x00, 0xe0, 0x4c, 0x00, 0x00, 0x01};
	u8 tempval;
	u8 ICVer8192, ICVer8256;
	u16 i, usValue, IC_Version;
	u16 EEPROMId;

	RT_TRACE(COMP_INIT, "====> _rtl92e_read_eeprom_info\n");

	EEPROMId = rtl92e_eeprom_read(dev, 0);
	if (EEPROMId != RTL8190_EEPROM_ID) {
		netdev_err(dev, "%s(): Invalid EEPROM ID: %x\n", __func__,
			   EEPROMId);
		priv->AutoloadFailFlag = true;
	} else {
		priv->AutoloadFailFlag = false;
	}

	if (!priv->AutoloadFailFlag) {
		priv->eeprom_vid = rtl92e_eeprom_read(dev, EEPROM_VID >> 1);
		priv->eeprom_did = rtl92e_eeprom_read(dev, EEPROM_DID >> 1);

		usValue = rtl92e_eeprom_read(dev,
					     (u16)(EEPROM_Customer_ID>>1)) >> 8;
		priv->eeprom_CustomerID = (u8)(usValue & 0xff);
		usValue = rtl92e_eeprom_read(dev,
					     EEPROM_ICVersion_ChannelPlan>>1);
		priv->eeprom_ChannelPlan = usValue&0xff;
		IC_Version = (usValue & 0xff00)>>8;

		ICVer8192 = (IC_Version&0xf);
		ICVer8256 = (IC_Version & 0xf0)>>4;
		RT_TRACE(COMP_INIT, "\nICVer8192 = 0x%x\n", ICVer8192);
		RT_TRACE(COMP_INIT, "\nICVer8256 = 0x%x\n", ICVer8256);
		if (ICVer8192 == 0x2) {
			if (ICVer8256 == 0x5)
				priv->card_8192_version = VERSION_8190_BE;
		}
		switch (priv->card_8192_version) {
		case VERSION_8190_BD:
		case VERSION_8190_BE:
			break;
		default:
			priv->card_8192_version = VERSION_8190_BD;
			break;
		}
		RT_TRACE(COMP_INIT, "\nIC Version = 0x%x\n",
			  priv->card_8192_version);
	} else {
		priv->card_8192_version = VERSION_8190_BD;
		priv->eeprom_vid = 0;
		priv->eeprom_did = 0;
		priv->eeprom_CustomerID = 0;
		priv->eeprom_ChannelPlan = 0;
		RT_TRACE(COMP_INIT, "\nIC Version = 0x%x\n", 0xff);
	}

	RT_TRACE(COMP_INIT, "EEPROM VID = 0x%4x\n", priv->eeprom_vid);
	RT_TRACE(COMP_INIT, "EEPROM DID = 0x%4x\n", priv->eeprom_did);
	RT_TRACE(COMP_INIT, "EEPROM Customer ID: 0x%2x\n",
		 priv->eeprom_CustomerID);

	if (!priv->AutoloadFailFlag) {
		for (i = 0; i < 6; i += 2) {
			usValue = rtl92e_eeprom_read(dev,
				 (u16)((EEPROM_NODE_ADDRESS_BYTE_0 + i) >> 1));
			*(u16 *)(&dev->dev_addr[i]) = usValue;
		}
	} else {
		ether_addr_copy(dev->dev_addr, bMac_Tmp_Addr);
	}

	RT_TRACE(COMP_INIT, "Permanent Address = %pM\n",
		 dev->dev_addr);

	if (priv->card_8192_version > VERSION_8190_BD)
		priv->bTXPowerDataReadFromEEPORM = true;
	else
		priv->bTXPowerDataReadFromEEPORM = false;

	priv->rf_type = RTL819X_DEFAULT_RF_TYPE;

	if (priv->card_8192_version > VERSION_8190_BD) {
		if (!priv->AutoloadFailFlag) {
			tempval = (rtl92e_eeprom_read(dev,
						      (EEPROM_RFInd_PowerDiff >> 1))) & 0xff;
			priv->EEPROMLegacyHTTxPowerDiff = tempval & 0xf;

			if (tempval&0x80)
				priv->rf_type = RF_1T2R;
			else
				priv->rf_type = RF_2T4R;
		} else {
			priv->EEPROMLegacyHTTxPowerDiff = 0x04;
		}
		RT_TRACE(COMP_INIT, "EEPROMLegacyHTTxPowerDiff = %d\n",
			priv->EEPROMLegacyHTTxPowerDiff);

		if (!priv->AutoloadFailFlag)
			priv->EEPROMThermalMeter = (u8)(((rtl92e_eeprom_read(dev,
						   (EEPROM_ThermalMeter>>1))) &
						   0xff00)>>8);
		else
			priv->EEPROMThermalMeter = EEPROM_Default_ThermalMeter;
		RT_TRACE(COMP_INIT, "ThermalMeter = %d\n",
			 priv->EEPROMThermalMeter);
		priv->TSSI_13dBm = priv->EEPROMThermalMeter * 100;

		if (priv->epromtype == EEPROM_93C46) {
			if (!priv->AutoloadFailFlag) {
				usValue = rtl92e_eeprom_read(dev,
					  EEPROM_TxPwDiff_CrystalCap >> 1);
				priv->EEPROMAntPwDiff = (usValue&0x0fff);
				priv->EEPROMCrystalCap = (u8)((usValue & 0xf000)
							 >> 12);
			} else {
				priv->EEPROMAntPwDiff =
					 EEPROM_Default_AntTxPowerDiff;
				priv->EEPROMCrystalCap =
					 EEPROM_Default_TxPwDiff_CrystalCap;
			}
			RT_TRACE(COMP_INIT, "EEPROMAntPwDiff = %d\n",
				 priv->EEPROMAntPwDiff);
			RT_TRACE(COMP_INIT, "EEPROMCrystalCap = %d\n",
				 priv->EEPROMCrystalCap);

			for (i = 0; i < 14; i += 2) {
				if (!priv->AutoloadFailFlag)
					usValue = rtl92e_eeprom_read(dev,
						  (u16)((EEPROM_TxPwIndex_CCK +
						  i) >> 1));
				else
					usValue = EEPROM_Default_TxPower;
				*((u16 *)(&priv->EEPROMTxPowerLevelCCK[i])) =
								 usValue;
				RT_TRACE(COMP_INIT,
					 "CCK Tx Power Level, Index %d = 0x%02x\n",
					 i, priv->EEPROMTxPowerLevelCCK[i]);
				RT_TRACE(COMP_INIT,
					 "CCK Tx Power Level, Index %d = 0x%02x\n",
					 i+1, priv->EEPROMTxPowerLevelCCK[i+1]);
			}
			for (i = 0; i < 14; i += 2) {
				if (!priv->AutoloadFailFlag)
					usValue = rtl92e_eeprom_read(dev,
						(u16)((EEPROM_TxPwIndex_OFDM_24G
						+ i) >> 1));
				else
					usValue = EEPROM_Default_TxPower;
				*((u16 *)(&priv->EEPROMTxPowerLevelOFDM24G[i]))
							 = usValue;
				RT_TRACE(COMP_INIT,
					 "OFDM 2.4G Tx Power Level, Index %d = 0x%02x\n",
					 i, priv->EEPROMTxPowerLevelOFDM24G[i]);
				RT_TRACE(COMP_INIT,
					 "OFDM 2.4G Tx Power Level, Index %d = 0x%02x\n",
					 i + 1,
					 priv->EEPROMTxPowerLevelOFDM24G[i+1]);
			}
		}
		if (priv->epromtype == EEPROM_93C46) {
			for (i = 0; i < 14; i++) {
				priv->TxPowerLevelCCK[i] =
					 priv->EEPROMTxPowerLevelCCK[i];
				priv->TxPowerLevelOFDM24G[i] =
					 priv->EEPROMTxPowerLevelOFDM24G[i];
			}
			priv->LegacyHTTxPowerDiff =
					 priv->EEPROMLegacyHTTxPowerDiff;
			priv->AntennaTxPwDiff[0] = (priv->EEPROMAntPwDiff &
						    0xf);
			priv->AntennaTxPwDiff[1] = (priv->EEPROMAntPwDiff &
							0xf0) >> 4;
			priv->AntennaTxPwDiff[2] = (priv->EEPROMAntPwDiff &
							0xf00) >> 8;
			priv->CrystalCap = priv->EEPROMCrystalCap;
			priv->ThermalMeter[0] = (priv->EEPROMThermalMeter &
						 0xf);
			priv->ThermalMeter[1] = (priv->EEPROMThermalMeter &
						     0xf0) >> 4;
		} else if (priv->epromtype == EEPROM_93C56) {

			for (i = 0; i < 3; i++) {
				priv->TxPowerLevelCCK_A[i] =
					 priv->EEPROMRfACCKChnl1TxPwLevel[0];
				priv->TxPowerLevelOFDM24G_A[i] =
					 priv->EEPROMRfAOfdmChnlTxPwLevel[0];
				priv->TxPowerLevelCCK_C[i] =
					 priv->EEPROMRfCCCKChnl1TxPwLevel[0];
				priv->TxPowerLevelOFDM24G_C[i] =
					 priv->EEPROMRfCOfdmChnlTxPwLevel[0];
			}
			for (i = 3; i < 9; i++) {
				priv->TxPowerLevelCCK_A[i]  =
					 priv->EEPROMRfACCKChnl1TxPwLevel[1];
				priv->TxPowerLevelOFDM24G_A[i] =
					 priv->EEPROMRfAOfdmChnlTxPwLevel[1];
				priv->TxPowerLevelCCK_C[i] =
					 priv->EEPROMRfCCCKChnl1TxPwLevel[1];
				priv->TxPowerLevelOFDM24G_C[i] =
					 priv->EEPROMRfCOfdmChnlTxPwLevel[1];
			}
			for (i = 9; i < 14; i++) {
				priv->TxPowerLevelCCK_A[i]  =
					 priv->EEPROMRfACCKChnl1TxPwLevel[2];
				priv->TxPowerLevelOFDM24G_A[i] =
					 priv->EEPROMRfAOfdmChnlTxPwLevel[2];
				priv->TxPowerLevelCCK_C[i] =
					 priv->EEPROMRfCCCKChnl1TxPwLevel[2];
				priv->TxPowerLevelOFDM24G_C[i] =
					 priv->EEPROMRfCOfdmChnlTxPwLevel[2];
			}
			for (i = 0; i < 14; i++)
				RT_TRACE(COMP_INIT,
					 "priv->TxPowerLevelCCK_A[%d] = 0x%x\n",
					 i, priv->TxPowerLevelCCK_A[i]);
			for (i = 0; i < 14; i++)
				RT_TRACE(COMP_INIT,
					 "priv->TxPowerLevelOFDM24G_A[%d] = 0x%x\n",
					 i, priv->TxPowerLevelOFDM24G_A[i]);
			for (i = 0; i < 14; i++)
				RT_TRACE(COMP_INIT,
					 "priv->TxPowerLevelCCK_C[%d] = 0x%x\n",
					 i, priv->TxPowerLevelCCK_C[i]);
			for (i = 0; i < 14; i++)
				RT_TRACE(COMP_INIT,
					 "priv->TxPowerLevelOFDM24G_C[%d] = 0x%x\n",
					 i, priv->TxPowerLevelOFDM24G_C[i]);
			priv->LegacyHTTxPowerDiff =
				 priv->EEPROMLegacyHTTxPowerDiff;
			priv->AntennaTxPwDiff[0] = 0;
			priv->AntennaTxPwDiff[1] = 0;
			priv->AntennaTxPwDiff[2] = 0;
			priv->CrystalCap = priv->EEPROMCrystalCap;
			priv->ThermalMeter[0] = (priv->EEPROMThermalMeter &
						 0xf);
			priv->ThermalMeter[1] = (priv->EEPROMThermalMeter &
						     0xf0) >> 4;
		}
	}

	if (priv->rf_type == RF_1T2R) {
		/* no matter what checkpatch says, the braces are needed */
		RT_TRACE(COMP_INIT, "\n1T2R config\n");
	} else if (priv->rf_type == RF_2T4R) {
		RT_TRACE(COMP_INIT, "\n2T4R config\n");
	}

	rtl92e_init_adaptive_rate(dev);

	priv->rf_chip = RF_8256;

	if (priv->RegChannelPlan == 0xf)
		priv->ChannelPlan = priv->eeprom_ChannelPlan;
	else
		priv->ChannelPlan = priv->RegChannelPlan;

	if (priv->eeprom_vid == 0x1186 &&  priv->eeprom_did == 0x3304)
		priv->CustomerID =  RT_CID_DLINK;

	switch (priv->eeprom_CustomerID) {
	case EEPROM_CID_DEFAULT:
		priv->CustomerID = RT_CID_DEFAULT;
		break;
	case EEPROM_CID_CAMEO:
		priv->CustomerID = RT_CID_819x_CAMEO;
		break;
	case  EEPROM_CID_RUNTOP:
		priv->CustomerID = RT_CID_819x_RUNTOP;
		break;
	case EEPROM_CID_NetCore:
		priv->CustomerID = RT_CID_819x_Netcore;
		break;
	case EEPROM_CID_TOSHIBA:
		priv->CustomerID = RT_CID_TOSHIBA;
		if (priv->eeprom_ChannelPlan&0x80)
			priv->ChannelPlan = priv->eeprom_ChannelPlan&0x7f;
		else
			priv->ChannelPlan = 0x0;
		RT_TRACE(COMP_INIT, "Toshiba ChannelPlan = 0x%x\n",
			priv->ChannelPlan);
		break;
	case EEPROM_CID_Nettronix:
		priv->ScanDelay = 100;
		priv->CustomerID = RT_CID_Nettronix;
		break;
	case EEPROM_CID_Pronet:
		priv->CustomerID = RT_CID_PRONET;
		break;
	case EEPROM_CID_DLINK:
		priv->CustomerID = RT_CID_DLINK;
		break;

	case EEPROM_CID_WHQL:
		break;
	default:
		break;
	}

	if (priv->ChannelPlan > CHANNEL_PLAN_LEN - 1)
		priv->ChannelPlan = 0;
	priv->ChannelPlan = COUNTRY_CODE_WORLD_WIDE_13;

	if (priv->eeprom_vid == 0x1186 &&  priv->eeprom_did == 0x3304)
		priv->rtllib->bSupportRemoteWakeUp = true;
	else
		priv->rtllib->bSupportRemoteWakeUp = false;

	RT_TRACE(COMP_INIT, "RegChannelPlan(%d)\n", priv->RegChannelPlan);
	RT_TRACE(COMP_INIT, "ChannelPlan = %d\n", priv->ChannelPlan);
	RT_TRACE(COMP_TRACE, "<==== ReadAdapterInfo\n");
}

void rtl92e_get_eeprom_size(struct net_device *dev)
{
	u16 curCR;
	struct r8192_priv *priv = rtllib_priv(dev);

	RT_TRACE(COMP_INIT, "===========>%s()\n", __func__);
	curCR = rtl92e_readl(dev, EPROM_CMD);
	RT_TRACE(COMP_INIT, "read from Reg Cmd9346CR(%x):%x\n", EPROM_CMD,
		 curCR);
	priv->epromtype = (curCR & EPROM_CMD_9356SEL) ? EEPROM_93C56 :
			  EEPROM_93C46;
	RT_TRACE(COMP_INIT, "<===========%s(), epromtype:%d\n", __func__,
		 priv->epromtype);
	_rtl92e_read_eeprom_info(dev);
}

static void _rtl92e_hwconfig(struct net_device *dev)
{
	u32 regRATR = 0, regRRSR = 0;
	u8 regBwOpMode = 0, regTmp = 0;
	struct r8192_priv *priv = rtllib_priv(dev);

	switch (priv->rtllib->mode) {
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK;
		regRRSR = RATE_ALL_CCK;
		break;
	case WIRELESS_MODE_A:
		regBwOpMode = BW_OPMODE_5G | BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_AUTO:
	case WIRELESS_MODE_N_24G:
		regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG |
				  RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_N_5G:
		regBwOpMode = BW_OPMODE_5G;
		regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS |
			  RATE_ALL_OFDM_2SS;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	default:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	}

	rtl92e_writeb(dev, BW_OPMODE, regBwOpMode);
	{
		u32 ratr_value;

		ratr_value = regRATR;
		if (priv->rf_type == RF_1T2R)
			ratr_value &= ~(RATE_ALL_OFDM_2SS);
		rtl92e_writel(dev, RATR0, ratr_value);
		rtl92e_writeb(dev, UFWP, 1);
	}
	regTmp = rtl92e_readb(dev, 0x313);
	regRRSR = ((regTmp) << 24) | (regRRSR & 0x00ffffff);
	rtl92e_writel(dev, RRSR, regRRSR);

	rtl92e_writew(dev, RETRY_LIMIT,
		      priv->ShortRetryLimit << RETRY_LIMIT_SHORT_SHIFT |
		      priv->LongRetryLimit << RETRY_LIMIT_LONG_SHIFT);
}

bool rtl92e_start_adapter(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 ulRegRead;
	bool rtStatus = true;
	u8 tmpvalue;
	u8 ICVersion, SwitchingRegulatorOutput;
	bool bfirmwareok = true;
	u32 tmpRegA, tmpRegC, TempCCk;
	int i = 0;
	u32 retry_times = 0;

	RT_TRACE(COMP_INIT, "====>%s()\n", __func__);
	priv->being_init_adapter = true;

start:
	rtl92e_reset_desc_ring(dev);
	priv->Rf_Mode = RF_OP_By_SW_3wire;
	if (priv->ResetProgress == RESET_TYPE_NORESET) {
		rtl92e_writeb(dev, ANAPAR, 0x37);
		mdelay(500);
	}
	priv->pFirmware->status = FW_STATUS_0_INIT;

	if (priv->RegRfOff)
		priv->rtllib->eRFPowerState = eRfOff;

	ulRegRead = rtl92e_readl(dev, CPU_GEN);
	if (priv->pFirmware->status == FW_STATUS_0_INIT)
		ulRegRead |= CPU_GEN_SYSTEM_RESET;
	else if (priv->pFirmware->status == FW_STATUS_5_READY)
		ulRegRead |= CPU_GEN_FIRMWARE_RESET;
	else
		netdev_err(dev, "%s(): undefined firmware state: %d.\n",
			   __func__, priv->pFirmware->status);

	rtl92e_writel(dev, CPU_GEN, ulRegRead);

	ICVersion = rtl92e_readb(dev, IC_VERRSION);
	if (ICVersion >= 0x4) {
		SwitchingRegulatorOutput = rtl92e_readb(dev, SWREGULATOR);
		if (SwitchingRegulatorOutput  != 0xb8) {
			rtl92e_writeb(dev, SWREGULATOR, 0xa8);
			mdelay(1);
			rtl92e_writeb(dev, SWREGULATOR, 0xb8);
		}
	}
	RT_TRACE(COMP_INIT, "BB Config Start!\n");
	rtStatus = rtl92e_config_bb(dev);
	if (!rtStatus) {
		netdev_warn(dev, "%s(): Failed to configure BB\n", __func__);
		return rtStatus;
	}
	RT_TRACE(COMP_INIT, "BB Config Finished!\n");

	priv->LoopbackMode = RTL819X_NO_LOOPBACK;
	if (priv->ResetProgress == RESET_TYPE_NORESET) {
		ulRegRead = rtl92e_readl(dev, CPU_GEN);
		if (priv->LoopbackMode == RTL819X_NO_LOOPBACK)
			ulRegRead = ((ulRegRead & CPU_GEN_NO_LOOPBACK_MSK) |
				     CPU_GEN_NO_LOOPBACK_SET);
		else if (priv->LoopbackMode == RTL819X_MAC_LOOPBACK)
			ulRegRead |= CPU_CCK_LOOPBACK;
		else
			netdev_err(dev, "%s: Invalid loopback mode setting.\n",
				   __func__);

		rtl92e_writel(dev, CPU_GEN, ulRegRead);

		udelay(500);
	}
	_rtl92e_hwconfig(dev);
	rtl92e_writeb(dev, CMDR, CR_RE | CR_TE);

	rtl92e_writeb(dev, PCIF, ((MXDMA2_NoLimit<<MXDMA2_RX_SHIFT) |
				  (MXDMA2_NoLimit<<MXDMA2_TX_SHIFT)));
	rtl92e_writel(dev, MAC0, ((u32 *)dev->dev_addr)[0]);
	rtl92e_writew(dev, MAC4, ((u16 *)(dev->dev_addr + 4))[0]);
	rtl92e_writel(dev, RCR, priv->ReceiveConfig);

	rtl92e_writel(dev, RQPN1, NUM_OF_PAGE_IN_FW_QUEUE_BK <<
		      RSVD_FW_QUEUE_PAGE_BK_SHIFT |
		      NUM_OF_PAGE_IN_FW_QUEUE_BE <<
		      RSVD_FW_QUEUE_PAGE_BE_SHIFT |
		      NUM_OF_PAGE_IN_FW_QUEUE_VI <<
		      RSVD_FW_QUEUE_PAGE_VI_SHIFT |
		      NUM_OF_PAGE_IN_FW_QUEUE_VO <<
		      RSVD_FW_QUEUE_PAGE_VO_SHIFT);
	rtl92e_writel(dev, RQPN2, NUM_OF_PAGE_IN_FW_QUEUE_MGNT <<
		      RSVD_FW_QUEUE_PAGE_MGNT_SHIFT);
	rtl92e_writel(dev, RQPN3, APPLIED_RESERVED_QUEUE_IN_FW |
		      NUM_OF_PAGE_IN_FW_QUEUE_BCN <<
		      RSVD_FW_QUEUE_PAGE_BCN_SHIFT|
		      NUM_OF_PAGE_IN_FW_QUEUE_PUB <<
		      RSVD_FW_QUEUE_PAGE_PUB_SHIFT);

	rtl92e_tx_enable(dev);
	rtl92e_rx_enable(dev);
	ulRegRead = (0xFFF00000 & rtl92e_readl(dev, RRSR))  |
		     RATE_ALL_OFDM_AG | RATE_ALL_CCK;
	rtl92e_writel(dev, RRSR, ulRegRead);
	rtl92e_writel(dev, RATR0+4*7, (RATE_ALL_OFDM_AG | RATE_ALL_CCK));

	rtl92e_writeb(dev, ACK_TIMEOUT, 0x30);

	if (priv->ResetProgress == RESET_TYPE_NORESET)
		rtl92e_set_wireless_mode(dev, priv->rtllib->mode);
	rtl92e_cam_reset(dev);
	{
		u8 SECR_value = 0x0;

		SECR_value |= SCR_TxEncEnable;
		SECR_value |= SCR_RxDecEnable;
		SECR_value |= SCR_NoSKMC;
		rtl92e_writeb(dev, SECR, SECR_value);
	}
	rtl92e_writew(dev, ATIMWND, 2);
	rtl92e_writew(dev, BCN_INTERVAL, 100);
	{
		int i;

		for (i = 0; i < QOS_QUEUE_NUM; i++)
			rtl92e_writel(dev, WDCAPARA_ADD[i], 0x005e4332);
	}
	rtl92e_writeb(dev, 0xbe, 0xc0);

	rtl92e_config_mac(dev);

	if (priv->card_8192_version > (u8) VERSION_8190_BD) {
		rtl92e_get_tx_power(dev);
		rtl92e_set_tx_power(dev, priv->chan);
	}

	tmpvalue = rtl92e_readb(dev, IC_VERRSION);
	priv->IC_Cut = tmpvalue;
	RT_TRACE(COMP_INIT, "priv->IC_Cut= 0x%x\n", priv->IC_Cut);
	if (priv->IC_Cut >= IC_VersionCut_D) {
		if (priv->IC_Cut == IC_VersionCut_D) {
			/* no matter what checkpatch says, braces are needed */
			RT_TRACE(COMP_INIT, "D-cut\n");
		} else if (priv->IC_Cut == IC_VersionCut_E) {
			RT_TRACE(COMP_INIT, "E-cut\n");
		}
	} else {
		RT_TRACE(COMP_INIT, "Before C-cut\n");
	}

	RT_TRACE(COMP_INIT, "Load Firmware!\n");
	bfirmwareok = rtl92e_init_fw(dev);
	if (!bfirmwareok) {
		if (retry_times < 10) {
			retry_times++;
			goto start;
		} else {
			rtStatus = false;
			goto end;
		}
	}
	RT_TRACE(COMP_INIT, "Load Firmware finished!\n");
	if (priv->ResetProgress == RESET_TYPE_NORESET) {
		RT_TRACE(COMP_INIT, "RF Config Started!\n");
		rtStatus = rtl92e_config_phy(dev);
		if (!rtStatus) {
			netdev_info(dev, "RF Config failed\n");
			return rtStatus;
		}
		RT_TRACE(COMP_INIT, "RF Config Finished!\n");
	}

	rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bCCKEn, 0x1);
	rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bOFDMEn, 0x1);

	rtl92e_writeb(dev, 0x87, 0x0);

	if (priv->RegRfOff) {
		RT_TRACE((COMP_INIT | COMP_RF | COMP_POWER),
			  "%s(): Turn off RF for RegRfOff ----------\n",
			  __func__);
		rtl92e_set_rf_state(dev, eRfOff, RF_CHANGE_BY_SW);
	} else if (priv->rtllib->RfOffReason > RF_CHANGE_BY_PS) {
		RT_TRACE((COMP_INIT|COMP_RF|COMP_POWER),
			 "%s(): Turn off RF for RfOffReason(%d) ----------\n",
			 __func__, priv->rtllib->RfOffReason);
		rtl92e_set_rf_state(dev, eRfOff, priv->rtllib->RfOffReason);
	} else if (priv->rtllib->RfOffReason >= RF_CHANGE_BY_IPS) {
		RT_TRACE((COMP_INIT|COMP_RF|COMP_POWER),
			 "%s(): Turn off RF for RfOffReason(%d) ----------\n",
			 __func__, priv->rtllib->RfOffReason);
		rtl92e_set_rf_state(dev, eRfOff, priv->rtllib->RfOffReason);
	} else {
		RT_TRACE((COMP_INIT|COMP_RF|COMP_POWER), "%s(): RF-ON\n",
			  __func__);
		priv->rtllib->eRFPowerState = eRfOn;
		priv->rtllib->RfOffReason = 0;
	}

	if (priv->rtllib->FwRWRF)
		priv->Rf_Mode = RF_OP_By_FW;
	else
		priv->Rf_Mode = RF_OP_By_SW_3wire;

	if (priv->ResetProgress == RESET_TYPE_NORESET) {
		rtl92e_dm_init_txpower_tracking(dev);

		if (priv->IC_Cut >= IC_VersionCut_D) {
			tmpRegA = rtl92e_get_bb_reg(dev, rOFDM0_XATxIQImbalance,
						    bMaskDWord);
			tmpRegC = rtl92e_get_bb_reg(dev, rOFDM0_XCTxIQImbalance,
						    bMaskDWord);
			for (i = 0; i < TxBBGainTableLength; i++) {
				if (tmpRegA == dm_tx_bb_gain[i]) {
					priv->rfa_txpowertrackingindex = (u8)i;
					priv->rfa_txpowertrackingindex_real =
						 (u8)i;
					priv->rfa_txpowertracking_default =
						 priv->rfa_txpowertrackingindex;
					break;
				}
			}

			TempCCk = rtl92e_get_bb_reg(dev, rCCK0_TxFilter1,
						    bMaskByte2);

			for (i = 0; i < CCKTxBBGainTableLength; i++) {
				if (TempCCk == dm_cck_tx_bb_gain[i][0]) {
					priv->CCKPresentAttentuation_20Mdefault = (u8)i;
					break;
				}
			}
			priv->CCKPresentAttentuation_40Mdefault = 0;
			priv->CCKPresentAttentuation_difference = 0;
			priv->CCKPresentAttentuation =
				  priv->CCKPresentAttentuation_20Mdefault;
			RT_TRACE(COMP_POWER_TRACKING,
				 "priv->rfa_txpowertrackingindex_initial = %d\n",
				 priv->rfa_txpowertrackingindex);
			RT_TRACE(COMP_POWER_TRACKING,
				 "priv->rfa_txpowertrackingindex_real__initial = %d\n",
				 priv->rfa_txpowertrackingindex_real);
			RT_TRACE(COMP_POWER_TRACKING,
				 "priv->CCKPresentAttentuation_difference_initial = %d\n",
				  priv->CCKPresentAttentuation_difference);
			RT_TRACE(COMP_POWER_TRACKING,
				 "priv->CCKPresentAttentuation_initial = %d\n",
				 priv->CCKPresentAttentuation);
			priv->btxpower_tracking = false;
		}
	}
	rtl92e_irq_enable(dev);
end:
	priv->being_init_adapter = false;
	return rtStatus;
}

static void _rtl92e_net_update(struct net_device *dev)
{

	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_network *net;
	u16 BcnTimeCfg = 0, BcnCW = 6, BcnIFS = 0xf;
	u16 rate_config = 0;

	net = &priv->rtllib->current_network;
	rtl92e_config_rate(dev, &rate_config);
	priv->dot11CurrentPreambleMode = PREAMBLE_AUTO;
	 priv->basic_rate = rate_config &= 0x15f;
	rtl92e_writel(dev, BSSIDR, ((u32 *)net->bssid)[0]);
	rtl92e_writew(dev, BSSIDR+4, ((u16 *)net->bssid)[2]);

	if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
		rtl92e_writew(dev, ATIMWND, 2);
		rtl92e_writew(dev, BCN_DMATIME, 256);
		rtl92e_writew(dev, BCN_INTERVAL, net->beacon_interval);
		rtl92e_writew(dev, BCN_DRV_EARLY_INT, 10);
		rtl92e_writeb(dev, BCN_ERR_THRESH, 100);

		BcnTimeCfg |= (BcnCW<<BCN_TCFG_CW_SHIFT);
		BcnTimeCfg |= BcnIFS<<BCN_TCFG_IFS;

		rtl92e_writew(dev, BCN_TCFG, BcnTimeCfg);
	}
}

void rtl92e_link_change(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	if (!priv->up)
		return;

	if (ieee->state == RTLLIB_LINKED) {
		_rtl92e_net_update(dev);
		priv->ops->update_ratr_table(dev);
		if ((ieee->pairwise_key_type == KEY_TYPE_WEP40) ||
		    (ieee->pairwise_key_type == KEY_TYPE_WEP104))
			rtl92e_enable_hw_security_config(dev);
	} else {
		rtl92e_writeb(dev, 0x173, 0);
	}
	_rtl92e_update_msr(dev);

	if (ieee->iw_mode == IW_MODE_INFRA || ieee->iw_mode == IW_MODE_ADHOC) {
		u32 reg;

		reg = rtl92e_readl(dev, RCR);
		if (priv->rtllib->state == RTLLIB_LINKED) {
			if (ieee->IntelPromiscuousModeInfo.bPromiscuousOn)
				;
			else
				priv->ReceiveConfig = reg |= RCR_CBSSID;
		} else
			priv->ReceiveConfig = reg &= ~RCR_CBSSID;

		rtl92e_writel(dev, RCR, reg);
	}
}

void rtl92e_set_monitor_mode(struct net_device *dev, bool bAllowAllDA,
			     bool WriteIntoReg)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (bAllowAllDA)
		priv->ReceiveConfig |= RCR_AAP;
	else
		priv->ReceiveConfig &= ~RCR_AAP;

	if (WriteIntoReg)
		rtl92e_writel(dev, RCR, priv->ReceiveConfig);
}

static u8 _rtl92e_rate_mgn_to_hw(u8 rate)
{
	u8  ret = DESC90_RATE1M;

	switch (rate) {
	case MGN_1M:
		ret = DESC90_RATE1M;
		break;
	case MGN_2M:
		ret = DESC90_RATE2M;
		break;
	case MGN_5_5M:
		ret = DESC90_RATE5_5M;
		break;
	case MGN_11M:
		ret = DESC90_RATE11M;
		break;
	case MGN_6M:
		ret = DESC90_RATE6M;
		break;
	case MGN_9M:
		ret = DESC90_RATE9M;
		break;
	case MGN_12M:
		ret = DESC90_RATE12M;
		break;
	case MGN_18M:
		ret = DESC90_RATE18M;
		break;
	case MGN_24M:
		ret = DESC90_RATE24M;
		break;
	case MGN_36M:
		ret = DESC90_RATE36M;
		break;
	case MGN_48M:
		ret = DESC90_RATE48M;
		break;
	case MGN_54M:
		ret = DESC90_RATE54M;
		break;
	case MGN_MCS0:
		ret = DESC90_RATEMCS0;
		break;
	case MGN_MCS1:
		ret = DESC90_RATEMCS1;
		break;
	case MGN_MCS2:
		ret = DESC90_RATEMCS2;
		break;
	case MGN_MCS3:
		ret = DESC90_RATEMCS3;
		break;
	case MGN_MCS4:
		ret = DESC90_RATEMCS4;
		break;
	case MGN_MCS5:
		ret = DESC90_RATEMCS5;
		break;
	case MGN_MCS6:
		ret = DESC90_RATEMCS6;
		break;
	case MGN_MCS7:
		ret = DESC90_RATEMCS7;
		break;
	case MGN_MCS8:
		ret = DESC90_RATEMCS8;
		break;
	case MGN_MCS9:
		ret = DESC90_RATEMCS9;
		break;
	case MGN_MCS10:
		ret = DESC90_RATEMCS10;
		break;
	case MGN_MCS11:
		ret = DESC90_RATEMCS11;
		break;
	case MGN_MCS12:
		ret = DESC90_RATEMCS12;
		break;
	case MGN_MCS13:
		ret = DESC90_RATEMCS13;
		break;
	case MGN_MCS14:
		ret = DESC90_RATEMCS14;
		break;
	case MGN_MCS15:
		ret = DESC90_RATEMCS15;
		break;
	case (0x80|0x20):
		ret = DESC90_RATEMCS32;
		break;
	default:
		break;
	}
	return ret;
}

static u8 _rtl92e_hw_queue_to_fw_queue(struct net_device *dev, u8 QueueID,
				       u8 priority)
{
	u8 QueueSelect = 0x0;

	switch (QueueID) {
	case BE_QUEUE:
		QueueSelect = QSLT_BE;
		break;

	case BK_QUEUE:
		QueueSelect = QSLT_BK;
		break;

	case VO_QUEUE:
		QueueSelect = QSLT_VO;
		break;

	case VI_QUEUE:
		QueueSelect = QSLT_VI;
		break;
	case MGNT_QUEUE:
		QueueSelect = QSLT_MGNT;
		break;
	case BEACON_QUEUE:
		QueueSelect = QSLT_BEACON;
		break;
	case TXCMD_QUEUE:
		QueueSelect = QSLT_CMD;
		break;
	case HIGH_QUEUE:
		QueueSelect = QSLT_HIGH;
		break;
	default:
		netdev_warn(dev, "%s(): Impossible Queue Selection: %d\n",
			    __func__, QueueID);
		break;
	}
	return QueueSelect;
}

static u8 _rtl92e_query_is_short(u8 TxHT, u8 TxRate, struct cb_desc *tcb_desc)
{
	u8   tmp_Short;

	tmp_Short = (TxHT == 1) ? ((tcb_desc->bUseShortGI) ? 1 : 0) :
			((tcb_desc->bUseShortPreamble) ? 1 : 0);
	if (TxHT == 1 && TxRate != DESC90_RATEMCS15)
		tmp_Short = 0;

	return tmp_Short;
}

void  rtl92e_fill_tx_desc(struct net_device *dev, struct tx_desc *pdesc,
			  struct cb_desc *cb_desc, struct sk_buff *skb)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	dma_addr_t mapping;
	struct tx_fwinfo_8190pci *pTxFwInfo;

	pTxFwInfo = (struct tx_fwinfo_8190pci *)skb->data;
	memset(pTxFwInfo, 0, sizeof(struct tx_fwinfo_8190pci));
	pTxFwInfo->TxHT = (cb_desc->data_rate & 0x80) ? 1 : 0;
	pTxFwInfo->TxRate = _rtl92e_rate_mgn_to_hw((u8)cb_desc->data_rate);
	pTxFwInfo->EnableCPUDur = cb_desc->bTxEnableFwCalcDur;
	pTxFwInfo->Short = _rtl92e_query_is_short(pTxFwInfo->TxHT,
						  pTxFwInfo->TxRate, cb_desc);

	if (cb_desc->bAMPDUEnable) {
		pTxFwInfo->AllowAggregation = 1;
		pTxFwInfo->RxMF = cb_desc->ampdu_factor;
		pTxFwInfo->RxAMD = cb_desc->ampdu_density;
	} else {
		pTxFwInfo->AllowAggregation = 0;
		pTxFwInfo->RxMF = 0;
		pTxFwInfo->RxAMD = 0;
	}

	pTxFwInfo->RtsEnable =	(cb_desc->bRTSEnable) ? 1 : 0;
	pTxFwInfo->CtsEnable = (cb_desc->bCTSEnable) ? 1 : 0;
	pTxFwInfo->RtsSTBC = (cb_desc->bRTSSTBC) ? 1 : 0;
	pTxFwInfo->RtsHT = (cb_desc->rts_rate&0x80) ? 1 : 0;
	pTxFwInfo->RtsRate = _rtl92e_rate_mgn_to_hw((u8)cb_desc->rts_rate);
	pTxFwInfo->RtsBandwidth = 0;
	pTxFwInfo->RtsSubcarrier = cb_desc->RTSSC;
	pTxFwInfo->RtsShort = (pTxFwInfo->RtsHT == 0) ?
			  (cb_desc->bRTSUseShortPreamble ? 1 : 0) :
			  (cb_desc->bRTSUseShortGI ? 1 : 0);
	if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20_40) {
		if (cb_desc->bPacketBW) {
			pTxFwInfo->TxBandwidth = 1;
			pTxFwInfo->TxSubCarrier = 0;
		} else {
			pTxFwInfo->TxBandwidth = 0;
			pTxFwInfo->TxSubCarrier = priv->nCur40MhzPrimeSC;
		}
	} else {
		pTxFwInfo->TxBandwidth = 0;
		pTxFwInfo->TxSubCarrier = 0;
	}

	memset((u8 *)pdesc, 0, 12);

	mapping = pci_map_single(priv->pdev, skb->data, skb->len,
				 PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(priv->pdev, mapping)) {
		netdev_err(dev, "%s(): DMA Mapping error\n", __func__);
		return;
	}

	pdesc->LINIP = 0;
	pdesc->CmdInit = 1;
	pdesc->Offset = sizeof(struct tx_fwinfo_8190pci) + 8;
	pdesc->PktSize = (u16)skb->len-sizeof(struct tx_fwinfo_8190pci);

	pdesc->SecCAMID = 0;
	pdesc->RATid = cb_desc->RATRIndex;


	pdesc->NoEnc = 1;
	pdesc->SecType = 0x0;
	if (cb_desc->bHwSec) {
		static u8 tmp;

		if (!tmp) {
			RT_TRACE(COMP_DBG, "==>================hw sec\n");
			tmp = 1;
		}
		switch (priv->rtllib->pairwise_key_type) {
		case KEY_TYPE_WEP40:
		case KEY_TYPE_WEP104:
			pdesc->SecType = 0x1;
			pdesc->NoEnc = 0;
			break;
		case KEY_TYPE_TKIP:
			pdesc->SecType = 0x2;
			pdesc->NoEnc = 0;
			break;
		case KEY_TYPE_CCMP:
			pdesc->SecType = 0x3;
			pdesc->NoEnc = 0;
			break;
		case KEY_TYPE_NA:
			pdesc->SecType = 0x0;
			pdesc->NoEnc = 1;
			break;
		}
	}

	pdesc->PktId = 0x0;

	pdesc->QueueSelect = _rtl92e_hw_queue_to_fw_queue(dev,
							  cb_desc->queue_index,
							  cb_desc->priority);
	pdesc->TxFWInfoSize = sizeof(struct tx_fwinfo_8190pci);

	pdesc->DISFB = cb_desc->bTxDisableRateFallBack;
	pdesc->USERATE = cb_desc->bTxUseDriverAssingedRate;

	pdesc->FirstSeg = 1;
	pdesc->LastSeg = 1;
	pdesc->TxBufferSize = skb->len;

	pdesc->TxBuffAddr = mapping;
}

void  rtl92e_fill_tx_cmd_desc(struct net_device *dev, struct tx_desc_cmd *entry,
			      struct cb_desc *cb_desc, struct sk_buff *skb)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	dma_addr_t mapping = pci_map_single(priv->pdev, skb->data, skb->len,
			 PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(priv->pdev, mapping))
		netdev_err(dev, "%s(): DMA Mapping error\n", __func__);
	memset(entry, 0, 12);
	entry->LINIP = cb_desc->bLastIniPkt;
	entry->FirstSeg = 1;
	entry->LastSeg = 1;
	if (cb_desc->bCmdOrInit == DESC_PACKET_TYPE_INIT) {
		entry->CmdInit = DESC_PACKET_TYPE_INIT;
	} else {
		struct tx_desc *entry_tmp = (struct tx_desc *)entry;

		entry_tmp->CmdInit = DESC_PACKET_TYPE_NORMAL;
		entry_tmp->Offset = sizeof(struct tx_fwinfo_8190pci) + 8;
		entry_tmp->PktSize = (u16)(cb_desc->pkt_size +
				      entry_tmp->Offset);
		entry_tmp->QueueSelect = QSLT_CMD;
		entry_tmp->TxFWInfoSize = 0x08;
		entry_tmp->RATid = (u8)DESC_PACKET_TYPE_INIT;
	}
	entry->TxBufferSize = skb->len;
	entry->TxBuffAddr = mapping;
	entry->OWN = 1;
}

static u8 _rtl92e_rate_hw_to_mgn(bool bIsHT, u8 rate)
{
	u8  ret_rate = 0x02;

	if (!bIsHT) {
		switch (rate) {
		case DESC90_RATE1M:
			ret_rate = MGN_1M;
			break;
		case DESC90_RATE2M:
			ret_rate = MGN_2M;
			break;
		case DESC90_RATE5_5M:
			ret_rate = MGN_5_5M;
			break;
		case DESC90_RATE11M:
			ret_rate = MGN_11M;
			break;
		case DESC90_RATE6M:
			ret_rate = MGN_6M;
			break;
		case DESC90_RATE9M:
			ret_rate = MGN_9M;
			break;
		case DESC90_RATE12M:
			ret_rate = MGN_12M;
			break;
		case DESC90_RATE18M:
			ret_rate = MGN_18M;
			break;
		case DESC90_RATE24M:
			ret_rate = MGN_24M;
			break;
		case DESC90_RATE36M:
			ret_rate = MGN_36M;
			break;
		case DESC90_RATE48M:
			ret_rate = MGN_48M;
			break;
		case DESC90_RATE54M:
			ret_rate = MGN_54M;
			break;

		default:
			RT_TRACE(COMP_RECV,
				 "_rtl92e_rate_hw_to_mgn(): Non supportedRate [%x], bIsHT = %d!!!\n",
				 rate, bIsHT);
			break;
		}

	} else {
		switch (rate) {
		case DESC90_RATEMCS0:
			ret_rate = MGN_MCS0;
			break;
		case DESC90_RATEMCS1:
			ret_rate = MGN_MCS1;
			break;
		case DESC90_RATEMCS2:
			ret_rate = MGN_MCS2;
			break;
		case DESC90_RATEMCS3:
			ret_rate = MGN_MCS3;
			break;
		case DESC90_RATEMCS4:
			ret_rate = MGN_MCS4;
			break;
		case DESC90_RATEMCS5:
			ret_rate = MGN_MCS5;
			break;
		case DESC90_RATEMCS6:
			ret_rate = MGN_MCS6;
			break;
		case DESC90_RATEMCS7:
			ret_rate = MGN_MCS7;
			break;
		case DESC90_RATEMCS8:
			ret_rate = MGN_MCS8;
			break;
		case DESC90_RATEMCS9:
			ret_rate = MGN_MCS9;
			break;
		case DESC90_RATEMCS10:
			ret_rate = MGN_MCS10;
			break;
		case DESC90_RATEMCS11:
			ret_rate = MGN_MCS11;
			break;
		case DESC90_RATEMCS12:
			ret_rate = MGN_MCS12;
			break;
		case DESC90_RATEMCS13:
			ret_rate = MGN_MCS13;
			break;
		case DESC90_RATEMCS14:
			ret_rate = MGN_MCS14;
			break;
		case DESC90_RATEMCS15:
			ret_rate = MGN_MCS15;
			break;
		case DESC90_RATEMCS32:
			ret_rate = (0x80|0x20);
			break;

		default:
			RT_TRACE(COMP_RECV,
				 "_rtl92e_rate_hw_to_mgn(): Non supported Rate [%x], bIsHT = %d!!!\n",
				 rate, bIsHT);
			break;
		}
	}

	return ret_rate;
}

static long _rtl92e_signal_scale_mapping(struct r8192_priv *priv, long currsig)
{
	long retsig;

	if (currsig >= 61 && currsig <= 100)
		retsig = 90 + ((currsig - 60) / 4);
	else if (currsig >= 41 && currsig <= 60)
		retsig = 78 + ((currsig - 40) / 2);
	else if (currsig >= 31 && currsig <= 40)
		retsig = 66 + (currsig - 30);
	else if (currsig >= 21 && currsig <= 30)
		retsig = 54 + (currsig - 20);
	else if (currsig >= 5 && currsig <= 20)
		retsig = 42 + (((currsig - 5) * 2) / 3);
	else if (currsig == 4)
		retsig = 36;
	else if (currsig == 3)
		retsig = 27;
	else if (currsig == 2)
		retsig = 18;
	else if (currsig == 1)
		retsig = 9;
	else
		retsig = currsig;

	return retsig;
}


#define	 rx_hal_is_cck_rate(_pdrvinfo)\
			((_pdrvinfo->RxRate == DESC90_RATE1M ||\
			_pdrvinfo->RxRate == DESC90_RATE2M ||\
			_pdrvinfo->RxRate == DESC90_RATE5_5M ||\
			_pdrvinfo->RxRate == DESC90_RATE11M) &&\
			!_pdrvinfo->RxHT)

static void _rtl92e_query_rxphystatus(
	struct r8192_priv *priv,
	struct rtllib_rx_stats *pstats,
	struct rx_desc  *pdesc,
	struct rx_fwinfo   *pdrvinfo,
	struct rtllib_rx_stats *precord_stats,
	bool bpacket_match_bssid,
	bool bpacket_toself,
	bool bPacketBeacon,
	bool bToSelfBA
	)
{
	struct phy_sts_ofdm_819xpci *pofdm_buf;
	struct phy_sts_cck_819xpci *pcck_buf;
	struct phy_ofdm_rx_status_rxsc_sgien_exintfflag *prxsc;
	u8 *prxpkt;
	u8 i, max_spatial_stream, tmp_rxsnr, tmp_rxevm, rxsc_sgien_exflg;
	s8 rx_pwr[4], rx_pwr_all = 0;
	s8 rx_snrX, rx_evmX;
	u8 evm, pwdb_all;
	u32 RSSI, total_rssi = 0;
	u8 is_cck_rate = 0;
	u8 rf_rx_num = 0;
	static	u8 check_reg824;
	static	u32 reg824_bit9;

	priv->stats.numqry_phystatus++;

	is_cck_rate = rx_hal_is_cck_rate(pdrvinfo);
	memset(precord_stats, 0, sizeof(struct rtllib_rx_stats));
	pstats->bPacketMatchBSSID = precord_stats->bPacketMatchBSSID =
				    bpacket_match_bssid;
	pstats->bPacketToSelf = precord_stats->bPacketToSelf = bpacket_toself;
	pstats->bIsCCK = precord_stats->bIsCCK = is_cck_rate;
	pstats->bPacketBeacon = precord_stats->bPacketBeacon = bPacketBeacon;
	pstats->bToSelfBA = precord_stats->bToSelfBA = bToSelfBA;
	if (check_reg824 == 0) {
		reg824_bit9 = rtl92e_get_bb_reg(priv->rtllib->dev,
						rFPGA0_XA_HSSIParameter2,
						0x200);
		check_reg824 = 1;
	}


	prxpkt = (u8 *)pdrvinfo;

	prxpkt += sizeof(struct rx_fwinfo);

	pcck_buf = (struct phy_sts_cck_819xpci *)prxpkt;
	pofdm_buf = (struct phy_sts_ofdm_819xpci *)prxpkt;

	pstats->RxMIMOSignalQuality[0] = -1;
	pstats->RxMIMOSignalQuality[1] = -1;
	precord_stats->RxMIMOSignalQuality[0] = -1;
	precord_stats->RxMIMOSignalQuality[1] = -1;

	if (is_cck_rate) {
		u8 report;

		priv->stats.numqry_phystatusCCK++;
		if (!reg824_bit9) {
			report = pcck_buf->cck_agc_rpt & 0xc0;
			report >>= 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -35 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x2:
				rx_pwr_all = -23 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x1:
				rx_pwr_all = -11 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x0:
				rx_pwr_all = 8 - (pcck_buf->cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			report = pcck_buf->cck_agc_rpt & 0x60;
			report >>= 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -35 -
					((pcck_buf->cck_agc_rpt &
					0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -23 -
					((pcck_buf->cck_agc_rpt &
					 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -11 -
					 ((pcck_buf->cck_agc_rpt &
					 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = -8 -
					 ((pcck_buf->cck_agc_rpt &
					 0x1f) << 1);
				break;
			}
		}

		pwdb_all = rtl92e_rx_db_to_percent(rx_pwr_all);
		pstats->RxPWDBAll = precord_stats->RxPWDBAll = pwdb_all;
		pstats->RecvSignalPower = rx_pwr_all;

		if (bpacket_match_bssid) {
			u8	sq;

			if (pstats->RxPWDBAll > 40) {
				sq = 100;
			} else {
				sq = pcck_buf->sq_rpt;

				if (pcck_buf->sq_rpt > 64)
					sq = 0;
				else if (pcck_buf->sq_rpt < 20)
					sq = 100;
				else
					sq = ((64-sq) * 100) / 44;
			}
			pstats->SignalQuality = sq;
			precord_stats->SignalQuality = sq;
			pstats->RxMIMOSignalQuality[0] = sq;
			precord_stats->RxMIMOSignalQuality[0] = sq;
			pstats->RxMIMOSignalQuality[1] = -1;
			precord_stats->RxMIMOSignalQuality[1] = -1;
		}
	} else {
		priv->stats.numqry_phystatusHT++;
		for (i = RF90_PATH_A; i < RF90_PATH_MAX; i++) {
			if (priv->brfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] = ((pofdm_buf->trsw_gain_X[i] & 0x3F) *
				     2) - 110;

			tmp_rxsnr = pofdm_buf->rxsnr_X[i];
			rx_snrX = (s8)(tmp_rxsnr);
			rx_snrX /= 2;
			priv->stats.rxSNRdB[i] = (long)rx_snrX;

			RSSI = rtl92e_rx_db_to_percent(rx_pwr[i]);
			if (priv->brfpath_rxenable[i])
				total_rssi += RSSI;

			if (bpacket_match_bssid) {
				pstats->RxMIMOSignalStrength[i] = (u8) RSSI;
				precord_stats->RxMIMOSignalStrength[i] =
								(u8) RSSI;
			}
		}


		rx_pwr_all = (((pofdm_buf->pwdb_all) >> 1) & 0x7f) - 106;
		pwdb_all = rtl92e_rx_db_to_percent(rx_pwr_all);

		pstats->RxPWDBAll = precord_stats->RxPWDBAll = pwdb_all;
		pstats->RxPower = precord_stats->RxPower =	rx_pwr_all;
		pstats->RecvSignalPower = rx_pwr_all;
		if (pdrvinfo->RxHT && pdrvinfo->RxRate >= DESC90_RATEMCS8 &&
		    pdrvinfo->RxRate <= DESC90_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			tmp_rxevm = pofdm_buf->rxevm_X[i];
			rx_evmX = (s8)(tmp_rxevm);

			rx_evmX /= 2;

			evm = rtl92e_evm_db_to_percent(rx_evmX);
			if (bpacket_match_bssid) {
				if (i == 0) {
					pstats->SignalQuality = (u8)(evm &
								 0xff);
					precord_stats->SignalQuality = (u8)(evm
									& 0xff);
				}
				pstats->RxMIMOSignalQuality[i] = (u8)(evm &
								 0xff);
				precord_stats->RxMIMOSignalQuality[i] = (u8)(evm
									& 0xff);
			}
		}


		rxsc_sgien_exflg = pofdm_buf->rxsc_sgien_exflg;
		prxsc = (struct phy_ofdm_rx_status_rxsc_sgien_exintfflag *)
			&rxsc_sgien_exflg;
		if (pdrvinfo->BW)
			priv->stats.received_bwtype[1+prxsc->rxsc]++;
		else
			priv->stats.received_bwtype[0]++;
	}

	if (is_cck_rate) {
		pstats->SignalStrength = precord_stats->SignalStrength =
					 (u8)(_rtl92e_signal_scale_mapping(priv,
					 (long)pwdb_all));

	} else {
		if (rf_rx_num != 0)
			pstats->SignalStrength = precord_stats->SignalStrength =
					 (u8)(_rtl92e_signal_scale_mapping(priv,
					 (long)(total_rssi /= rf_rx_num)));
	}
}

static void _rtl92e_process_phyinfo(struct r8192_priv *priv, u8 *buffer,
				    struct rtllib_rx_stats *prev_st,
				    struct rtllib_rx_stats *curr_st)
{
	bool bcheck = false;
	u8	rfpath;
	u32 ij, tmp_val;
	static u32 slide_rssi_index, slide_rssi_statistics;
	static u32 slide_evm_index, slide_evm_statistics;
	static u32 last_rssi, last_evm;
	static u32 slide_beacon_adc_pwdb_index;
	static u32 slide_beacon_adc_pwdb_statistics;
	static u32 last_beacon_adc_pwdb;
	struct rtllib_hdr_3addr *hdr;
	u16 sc;
	unsigned int frag, seq;

	hdr = (struct rtllib_hdr_3addr *)buffer;
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	seq = WLAN_GET_SEQ_SEQ(sc);
	curr_st->Seq_Num = seq;
	if (!prev_st->bIsAMPDU)
		bcheck = true;

	if (slide_rssi_statistics++ >= PHY_RSSI_SLID_WIN_MAX) {
		slide_rssi_statistics = PHY_RSSI_SLID_WIN_MAX;
		last_rssi = priv->stats.slide_signal_strength[slide_rssi_index];
		priv->stats.slide_rssi_total -= last_rssi;
	}
	priv->stats.slide_rssi_total += prev_st->SignalStrength;

	priv->stats.slide_signal_strength[slide_rssi_index++] =
					 prev_st->SignalStrength;
	if (slide_rssi_index >= PHY_RSSI_SLID_WIN_MAX)
		slide_rssi_index = 0;

	tmp_val = priv->stats.slide_rssi_total/slide_rssi_statistics;
	priv->stats.signal_strength = rtl92e_translate_to_dbm(priv,
							      (u8)tmp_val);
	curr_st->rssi = priv->stats.signal_strength;
	if (!prev_st->bPacketMatchBSSID) {
		if (!prev_st->bToSelfBA)
			return;
	}

	if (!bcheck)
		return;

	priv->stats.num_process_phyinfo++;
	if (!prev_st->bIsCCK && prev_st->bPacketToSelf) {
		for (rfpath = RF90_PATH_A; rfpath < RF90_PATH_C; rfpath++) {
			if (!rtl92e_is_legal_rf_path(priv->rtllib->dev, rfpath))
				continue;
			RT_TRACE(COMP_DBG,
				 "Jacken -> pPreviousstats->RxMIMOSignalStrength[rfpath]  = %d\n",
				 prev_st->RxMIMOSignalStrength[rfpath]);
			if (priv->stats.rx_rssi_percentage[rfpath] == 0) {
				priv->stats.rx_rssi_percentage[rfpath] =
					 prev_st->RxMIMOSignalStrength[rfpath];
			}
			if (prev_st->RxMIMOSignalStrength[rfpath]  >
			    priv->stats.rx_rssi_percentage[rfpath]) {
				priv->stats.rx_rssi_percentage[rfpath] =
					((priv->stats.rx_rssi_percentage[rfpath]
					* (RX_SMOOTH - 1)) +
					(prev_st->RxMIMOSignalStrength
					[rfpath])) / (RX_SMOOTH);
				priv->stats.rx_rssi_percentage[rfpath] =
					 priv->stats.rx_rssi_percentage[rfpath]
					 + 1;
			} else {
				priv->stats.rx_rssi_percentage[rfpath] =
				   ((priv->stats.rx_rssi_percentage[rfpath] *
				   (RX_SMOOTH-1)) +
				   (prev_st->RxMIMOSignalStrength[rfpath])) /
				   (RX_SMOOTH);
			}
			RT_TRACE(COMP_DBG,
				 "Jacken -> priv->RxStats.RxRSSIPercentage[rfPath]  = %d\n",
				 priv->stats.rx_rssi_percentage[rfpath]);
		}
	}


	if (prev_st->bPacketBeacon) {
		if (slide_beacon_adc_pwdb_statistics++ >=
		    PHY_Beacon_RSSI_SLID_WIN_MAX) {
			slide_beacon_adc_pwdb_statistics =
					 PHY_Beacon_RSSI_SLID_WIN_MAX;
			last_beacon_adc_pwdb = priv->stats.Slide_Beacon_pwdb
					       [slide_beacon_adc_pwdb_index];
			priv->stats.Slide_Beacon_Total -= last_beacon_adc_pwdb;
		}
		priv->stats.Slide_Beacon_Total += prev_st->RxPWDBAll;
		priv->stats.Slide_Beacon_pwdb[slide_beacon_adc_pwdb_index] =
							 prev_st->RxPWDBAll;
		slide_beacon_adc_pwdb_index++;
		if (slide_beacon_adc_pwdb_index >= PHY_Beacon_RSSI_SLID_WIN_MAX)
			slide_beacon_adc_pwdb_index = 0;
		prev_st->RxPWDBAll = priv->stats.Slide_Beacon_Total /
				     slide_beacon_adc_pwdb_statistics;
		if (prev_st->RxPWDBAll >= 3)
			prev_st->RxPWDBAll -= 3;
	}

	RT_TRACE(COMP_RXDESC, "Smooth %s PWDB = %d\n",
				prev_st->bIsCCK ? "CCK" : "OFDM",
				prev_st->RxPWDBAll);

	if (prev_st->bPacketToSelf || prev_st->bPacketBeacon ||
	    prev_st->bToSelfBA) {
		if (priv->undecorated_smoothed_pwdb < 0)
			priv->undecorated_smoothed_pwdb = prev_st->RxPWDBAll;
		if (prev_st->RxPWDBAll > (u32)priv->undecorated_smoothed_pwdb) {
			priv->undecorated_smoothed_pwdb =
					(((priv->undecorated_smoothed_pwdb) *
					(RX_SMOOTH-1)) +
					(prev_st->RxPWDBAll)) / (RX_SMOOTH);
			priv->undecorated_smoothed_pwdb =
					 priv->undecorated_smoothed_pwdb + 1;
		} else {
			priv->undecorated_smoothed_pwdb =
					(((priv->undecorated_smoothed_pwdb) *
					(RX_SMOOTH-1)) +
					(prev_st->RxPWDBAll)) / (RX_SMOOTH);
		}
		rtl92e_update_rx_statistics(priv, prev_st);
	}

	if (prev_st->SignalQuality != 0) {
		if (prev_st->bPacketToSelf || prev_st->bPacketBeacon ||
		    prev_st->bToSelfBA) {
			if (slide_evm_statistics++ >= PHY_RSSI_SLID_WIN_MAX) {
				slide_evm_statistics = PHY_RSSI_SLID_WIN_MAX;
				last_evm =
					 priv->stats.slide_evm[slide_evm_index];
				priv->stats.slide_evm_total -= last_evm;
			}

			priv->stats.slide_evm_total += prev_st->SignalQuality;

			priv->stats.slide_evm[slide_evm_index++] =
						 prev_st->SignalQuality;
			if (slide_evm_index >= PHY_RSSI_SLID_WIN_MAX)
				slide_evm_index = 0;

			tmp_val = priv->stats.slide_evm_total /
				  slide_evm_statistics;
			priv->stats.signal_quality = tmp_val;
			priv->stats.last_signal_strength_inpercent = tmp_val;
		}

		if (prev_st->bPacketToSelf ||
		    prev_st->bPacketBeacon ||
		    prev_st->bToSelfBA) {
			for (ij = 0; ij < 2; ij++) {
				if (prev_st->RxMIMOSignalQuality[ij] != -1) {
					if (priv->stats.rx_evm_percentage[ij] == 0)
						priv->stats.rx_evm_percentage[ij] =
						   prev_st->RxMIMOSignalQuality[ij];
					priv->stats.rx_evm_percentage[ij] =
					  ((priv->stats.rx_evm_percentage[ij] *
					  (RX_SMOOTH - 1)) +
					  (prev_st->RxMIMOSignalQuality[ij])) /
					  (RX_SMOOTH);
				}
			}
		}
	}
}

static void _rtl92e_translate_rx_signal_stats(struct net_device *dev,
					      struct sk_buff *skb,
					      struct rtllib_rx_stats *pstats,
					      struct rx_desc *pdesc,
					      struct rx_fwinfo *pdrvinfo)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	bool bpacket_match_bssid, bpacket_toself;
	bool bPacketBeacon = false;
	struct rtllib_hdr_3addr *hdr;
	bool bToSelfBA = false;
	static struct rtllib_rx_stats  previous_stats;
	u16 fc, type;
	u8 *tmp_buf;
	u8 *praddr;

	tmp_buf = skb->data + pstats->RxDrvInfoSize + pstats->RxBufShift;

	hdr = (struct rtllib_hdr_3addr *)tmp_buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;

	bpacket_match_bssid =
		((type != RTLLIB_FTYPE_CTL) &&
		 ether_addr_equal(priv->rtllib->current_network.bssid,
				  (fc & RTLLIB_FCTL_TODS) ? hdr->addr1 :
				  (fc & RTLLIB_FCTL_FROMDS) ? hdr->addr2 :
				  hdr->addr3) &&
		 (!pstats->bHwError) && (!pstats->bCRC) && (!pstats->bICV));
	bpacket_toself = bpacket_match_bssid &&		/* check this */
			 ether_addr_equal(praddr, priv->rtllib->dev->dev_addr);
	if (WLAN_FC_GET_FRAMETYPE(fc) == RTLLIB_STYPE_BEACON)
		bPacketBeacon = true;
	if (bpacket_match_bssid)
		priv->stats.numpacket_matchbssid++;
	if (bpacket_toself)
		priv->stats.numpacket_toself++;
	_rtl92e_process_phyinfo(priv, tmp_buf, &previous_stats, pstats);
	_rtl92e_query_rxphystatus(priv, pstats, pdesc, pdrvinfo,
				  &previous_stats, bpacket_match_bssid,
				  bpacket_toself, bPacketBeacon, bToSelfBA);
	rtl92e_copy_mpdu_stats(pstats, &previous_stats);
}

static void _rtl92e_update_received_rate_histogram_stats(
					   struct net_device *dev,
					   struct rtllib_rx_stats *pstats)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u32 rcvType = 1;
	u32 rateIndex;
	u32 preamble_guardinterval;

	if (pstats->bCRC)
		rcvType = 2;
	else if (pstats->bICV)
		rcvType = 3;

	if (pstats->bShortPreamble)
		preamble_guardinterval = 1;
	else
		preamble_guardinterval = 0;

	switch (pstats->rate) {
	case MGN_1M:
		rateIndex = 0;
		break;
	case MGN_2M:
		rateIndex = 1;
		 break;
	case MGN_5_5M:
		rateIndex = 2;
		break;
	case MGN_11M:
		rateIndex = 3;
		break;
	case MGN_6M:
		rateIndex = 4;
		break;
	case MGN_9M:
		rateIndex = 5;
		break;
	case MGN_12M:
		rateIndex = 6;
		break;
	case MGN_18M:
		rateIndex = 7;
		 break;
	case MGN_24M:
		rateIndex = 8;
		break;
	case MGN_36M:
		rateIndex = 9;
		break;
	case MGN_48M:
		rateIndex = 10;
		break;
	case MGN_54M:
		rateIndex = 11;
		break;
	case MGN_MCS0:
		rateIndex = 12;
		break;
	case MGN_MCS1:
		rateIndex = 13;
		break;
	case MGN_MCS2:
		rateIndex = 14;
		break;
	case MGN_MCS3:
		rateIndex = 15;
		break;
	case MGN_MCS4:
		rateIndex = 16;
		break;
	case MGN_MCS5:
		rateIndex = 17;
		break;
	case MGN_MCS6:
		rateIndex = 18;
		break;
	case MGN_MCS7:
		rateIndex = 19;
		break;
	case MGN_MCS8:
		rateIndex = 20;
		break;
	case MGN_MCS9:
		rateIndex = 21;
		break;
	case MGN_MCS10:
		rateIndex = 22;
		break;
	case MGN_MCS11:
		rateIndex = 23;
		break;
	case MGN_MCS12:
		rateIndex = 24;
		break;
	case MGN_MCS13:
		rateIndex = 25;
		break;
	case MGN_MCS14:
		rateIndex = 26;
		break;
	case MGN_MCS15:
		rateIndex = 27;
		break;
	default:
		rateIndex = 28;
		break;
	}
	priv->stats.received_preamble_GI[preamble_guardinterval][rateIndex]++;
	priv->stats.received_rate_histogram[0][rateIndex]++;
	priv->stats.received_rate_histogram[rcvType][rateIndex]++;
}

bool rtl92e_get_rx_stats(struct net_device *dev, struct rtllib_rx_stats *stats,
			 struct rx_desc *pdesc, struct sk_buff *skb)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rx_fwinfo *pDrvInfo = NULL;

	stats->bICV = pdesc->ICV;
	stats->bCRC = pdesc->CRC32;
	stats->bHwError = pdesc->CRC32 | pdesc->ICV;

	stats->Length = pdesc->Length;
	if (stats->Length < 24)
		stats->bHwError |= 1;

	if (stats->bHwError) {
		stats->bShift = false;

		if (pdesc->CRC32) {
			if (pdesc->Length < 500)
				priv->stats.rxcrcerrmin++;
			else if (pdesc->Length > 1000)
				priv->stats.rxcrcerrmax++;
			else
				priv->stats.rxcrcerrmid++;
		}
		return false;
	}

	stats->RxDrvInfoSize = pdesc->RxDrvInfoSize;
	stats->RxBufShift = ((pdesc->Shift)&0x03);
	stats->Decrypted = !pdesc->SWDec;

	pDrvInfo = (struct rx_fwinfo *)(skb->data + stats->RxBufShift);

	stats->rate = _rtl92e_rate_hw_to_mgn((bool)pDrvInfo->RxHT,
					     (u8)pDrvInfo->RxRate);
	stats->bShortPreamble = pDrvInfo->SPLCP;

	_rtl92e_update_received_rate_histogram_stats(dev, stats);

	stats->bIsAMPDU = (pDrvInfo->PartAggr == 1);
	stats->bFirstMPDU = (pDrvInfo->PartAggr == 1) &&
			    (pDrvInfo->FirstAGGR == 1);

	stats->TimeStampLow = pDrvInfo->TSFL;
	stats->TimeStampHigh = rtl92e_readl(dev, TSFR+4);

	rtl92e_update_rx_pkt_timestamp(dev, stats);

	if ((stats->RxBufShift + stats->RxDrvInfoSize) > 0)
		stats->bShift = 1;

	stats->RxIs40MHzPacket = pDrvInfo->BW;

	_rtl92e_translate_rx_signal_stats(dev, skb, stats, pdesc, pDrvInfo);

	if (pDrvInfo->FirstAGGR == 1 || pDrvInfo->PartAggr == 1)
		RT_TRACE(COMP_RXDESC,
			 "pDrvInfo->FirstAGGR = %d, pDrvInfo->PartAggr = %d\n",
			 pDrvInfo->FirstAGGR, pDrvInfo->PartAggr);
	skb_trim(skb, skb->len - 4/*sCrcLng*/);


	stats->packetlength = stats->Length-4;
	stats->fraglength = stats->packetlength;
	stats->fragoffset = 0;
	stats->ntotalfrag = 1;
	return true;
}

void rtl92e_stop_adapter(struct net_device *dev, bool reset)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int i;
	u8	OpMode;
	u8	u1bTmp;
	u32	ulRegRead;

	OpMode = RT_OP_MODE_NO_LINK;
	priv->rtllib->SetHwRegHandler(dev, HW_VAR_MEDIA_STATUS, &OpMode);

	if (!priv->rtllib->bSupportRemoteWakeUp) {
		u1bTmp = 0x0;
		rtl92e_writeb(dev, CMDR, u1bTmp);
	}

	mdelay(20);

	if (!reset) {
		mdelay(150);

		priv->bHwRfOffAction = 2;

		if (!priv->rtllib->bSupportRemoteWakeUp) {
			rtl92e_set_rf_off(dev);
			ulRegRead = rtl92e_readl(dev, CPU_GEN);
			ulRegRead |= CPU_GEN_SYSTEM_RESET;
			rtl92e_writel(dev, CPU_GEN, ulRegRead);
		} else {
			rtl92e_writel(dev, WFCRC0, 0xffffffff);
			rtl92e_writel(dev, WFCRC1, 0xffffffff);
			rtl92e_writel(dev, WFCRC2, 0xffffffff);


			rtl92e_writeb(dev, PMR, 0x5);
			rtl92e_writeb(dev, MacBlkCtrl, 0xa);
		}
	}

	for (i = 0; i < MAX_QUEUE_SIZE; i++)
		skb_queue_purge(&priv->rtllib->skb_waitQ[i]);
	for (i = 0; i < MAX_QUEUE_SIZE; i++)
		skb_queue_purge(&priv->rtllib->skb_aggQ[i]);

	skb_queue_purge(&priv->skb_queue);
}

void rtl92e_update_ratr_table(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	u8 *pMcsRate = ieee->dot11HTOperationalRateSet;
	u32 ratr_value = 0;
	u16 rate_config = 0;
	u8 rate_index = 0;

	rtl92e_config_rate(dev, &rate_config);
	ratr_value = rate_config | *pMcsRate << 12;
	switch (ieee->mode) {
	case IEEE_A:
		ratr_value &= 0x00000FF0;
		break;
	case IEEE_B:
		ratr_value &= 0x0000000F;
		break;
	case IEEE_G:
	case IEEE_G|IEEE_B:
		ratr_value &= 0x00000FF7;
		break;
	case IEEE_N_24G:
	case IEEE_N_5G:
		if (ieee->pHTInfo->PeerMimoPs == 0) {
			ratr_value &= 0x0007F007;
		} else {
			if (priv->rf_type == RF_1T2R)
				ratr_value &= 0x000FF007;
			else
				ratr_value &= 0x0F81F007;
		}
		break;
	default:
		break;
	}
	ratr_value &= 0x0FFFFFFF;
	if (ieee->pHTInfo->bCurTxBW40MHz &&
	    ieee->pHTInfo->bCurShortGI40MHz)
		ratr_value |= 0x80000000;
	else if (!ieee->pHTInfo->bCurTxBW40MHz &&
		  ieee->pHTInfo->bCurShortGI20MHz)
		ratr_value |= 0x80000000;
	rtl92e_writel(dev, RATR0+rate_index*4, ratr_value);
	rtl92e_writeb(dev, UFWP, 1);
}

void
rtl92e_init_variables(struct net_device  *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	strcpy(priv->nick, "rtl8192E");

	priv->rtllib->softmac_features  = IEEE_SOFTMAC_SCAN |
		IEEE_SOFTMAC_ASSOCIATE | IEEE_SOFTMAC_PROBERQ |
		IEEE_SOFTMAC_PROBERS | IEEE_SOFTMAC_TX_QUEUE;

	priv->rtllib->tx_headroom = sizeof(struct tx_fwinfo_8190pci);

	priv->ShortRetryLimit = 0x30;
	priv->LongRetryLimit = 0x30;

	priv->ReceiveConfig = RCR_ADD3	|
		RCR_AMF | RCR_ADF |
		RCR_AICV |
		RCR_AB | RCR_AM | RCR_APM |
		RCR_AAP | ((u32)7<<RCR_MXDMA_OFFSET) |
		((u32)7 << RCR_FIFO_OFFSET) | RCR_ONLYERLPKT;

	priv->irq_mask[0] = (u32)(IMR_ROK | IMR_VODOK | IMR_VIDOK |
			    IMR_BEDOK | IMR_BKDOK | IMR_HCCADOK |
			    IMR_MGNTDOK | IMR_COMDOK | IMR_HIGHDOK |
			    IMR_BDOK | IMR_RXCMDOK | IMR_TIMEOUT0 |
			    IMR_RDU | IMR_RXFOVW | IMR_TXFOVW |
			    IMR_BcnInt | IMR_TBDOK | IMR_TBDER);

	priv->PwrDomainProtect = false;

	priv->bfirst_after_down = false;
}

void rtl92e_enable_irq(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	priv->irq_enabled = 1;

	rtl92e_writel(dev, INTA_MASK, priv->irq_mask[0]);

}

void rtl92e_disable_irq(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	rtl92e_writel(dev, INTA_MASK, 0);

	priv->irq_enabled = 0;
}

void rtl92e_clear_irq(struct net_device *dev)
{
	u32 tmp;

	tmp = rtl92e_readl(dev, ISR);
	rtl92e_writel(dev, ISR, tmp);
}


void rtl92e_enable_rx(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	rtl92e_writel(dev, RDQDA, priv->rx_ring_dma[RX_MPDU_QUEUE]);
}

static const u32 TX_DESC_BASE[] = {
	BKQDA, BEQDA, VIQDA, VOQDA, HCCAQDA, CQDA, MQDA, HQDA, BQDA
};

void rtl92e_enable_tx(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u32 i;

	for (i = 0; i < MAX_TX_QUEUE_COUNT; i++)
		rtl92e_writel(dev, TX_DESC_BASE[i], priv->tx_ring[i].dma);
}


void rtl92e_ack_irq(struct net_device *dev, u32 *p_inta, u32 *p_intb)
{
	*p_inta = rtl92e_readl(dev, ISR);
	rtl92e_writel(dev, ISR, *p_inta);
}

bool rtl92e_is_rx_stuck(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u16		  RegRxCounter = rtl92e_readw(dev, 0x130);
	bool		  bStuck = false;
	static u8	  rx_chk_cnt;
	u32		SlotIndex = 0, TotalRxStuckCount = 0;
	u8		i;
	u8		SilentResetRxSoltNum = 4;

	RT_TRACE(COMP_RESET, "%s(): RegRxCounter is %d, RxCounter is %d\n",
		 __func__, RegRxCounter, priv->RxCounter);

	rx_chk_cnt++;
	if (priv->undecorated_smoothed_pwdb >= (RateAdaptiveTH_High+5)) {
		rx_chk_cnt = 0;
	} else if ((priv->undecorated_smoothed_pwdb < (RateAdaptiveTH_High + 5))
	  && (((priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20) &&
	  (priv->undecorated_smoothed_pwdb >= RateAdaptiveTH_Low_40M))
	  || ((priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20) &&
	  (priv->undecorated_smoothed_pwdb >= RateAdaptiveTH_Low_20M)))) {
		if (rx_chk_cnt < 2)
			return bStuck;
		rx_chk_cnt = 0;
	} else if ((((priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20) &&
		  (priv->undecorated_smoothed_pwdb < RateAdaptiveTH_Low_40M)) ||
		((priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20) &&
		 (priv->undecorated_smoothed_pwdb < RateAdaptiveTH_Low_20M))) &&
		priv->undecorated_smoothed_pwdb >= VeryLowRSSI) {
		if (rx_chk_cnt < 4)
			return bStuck;
		rx_chk_cnt = 0;
	} else {
		if (rx_chk_cnt < 8)
			return bStuck;
		rx_chk_cnt = 0;
	}


	SlotIndex = (priv->SilentResetRxSlotIndex++)%SilentResetRxSoltNum;

	if (priv->RxCounter == RegRxCounter) {
		priv->SilentResetRxStuckEvent[SlotIndex] = 1;

		for (i = 0; i < SilentResetRxSoltNum; i++)
			TotalRxStuckCount += priv->SilentResetRxStuckEvent[i];

		if (TotalRxStuckCount == SilentResetRxSoltNum) {
			bStuck = true;
			for (i = 0; i < SilentResetRxSoltNum; i++)
				TotalRxStuckCount +=
					 priv->SilentResetRxStuckEvent[i];
		}


	} else {
		priv->SilentResetRxStuckEvent[SlotIndex] = 0;
	}

	priv->RxCounter = RegRxCounter;

	return bStuck;
}

bool rtl92e_is_tx_stuck(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool	bStuck = false;
	u16	RegTxCounter = rtl92e_readw(dev, 0x128);

	RT_TRACE(COMP_RESET, "%s():RegTxCounter is %d,TxCounter is %d\n",
		 __func__, RegTxCounter, priv->TxCounter);

	if (priv->TxCounter == RegTxCounter)
		bStuck = true;

	priv->TxCounter = RegTxCounter;

	return bStuck;
}

bool rtl92e_get_nmode_support_by_sec(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	if (ieee->rtllib_ap_sec_type &&
	   (ieee->rtllib_ap_sec_type(priv->rtllib)&(SEC_ALG_WEP |
				     SEC_ALG_TKIP))) {
		return false;
	} else {
		return true;
	}
}

bool rtl92e_is_halfn_supported_by_ap(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	return ieee->bHalfWirelessN24GMode;
}
