/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

#include "rtl_core.h"
#ifdef RTL8192SE
#include "rtl8192s/r8192S_phyreg.h"
#include "rtl8192s/r8192S_phy.h"
#else
#include "r8192E_phyreg.h"
#include "r8192E_phy.h"
#endif
#include "r8190P_rtl8256.h"

void PHY_SetRF8256Bandwidth(struct net_device* dev , HT_CHANNEL_WIDTH Bandwidth)
{
	u8	eRFPath;
	struct r8192_priv *priv = rtllib_priv(dev);

	for (eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++) {
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		switch (Bandwidth) {
		case HT_CHANNEL_WIDTH_20:
			if (priv->card_8192_version == VERSION_8190_BD || priv->card_8192_version == VERSION_8190_BE) {
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0b, bMask12Bits, 0x100);
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x2c, bMask12Bits, 0x3d7);
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0e, bMask12Bits, 0x021);

			} else {
				RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown hardware version\n");
			}

			break;
		case HT_CHANNEL_WIDTH_20_40:
			if (priv->card_8192_version == VERSION_8190_BD ||priv->card_8192_version == VERSION_8190_BE) {
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0b, bMask12Bits, 0x300);
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x2c, bMask12Bits, 0x3ff);
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0e, bMask12Bits, 0x0e1);

			} else {
				RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown hardware version\n");
			}


			break;
		default:
			RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth );
			break;

		}
	}
	return;
}

bool PHY_RF8256_Config(struct net_device* dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool rtStatus = true;
	priv->NumTotalRFPath = RTL819X_TOTAL_RF_PATH;
	rtStatus = phy_RF8256_Config_ParaFile(dev);

	return rtStatus;
}

bool phy_RF8256_Config_ParaFile(struct net_device* dev)
{
	u32	u4RegValue = 0;
	u8	eRFPath;
	bool rtStatus = true;
	BB_REGISTER_DEFINITION_T	*pPhyReg;
	struct r8192_priv *priv = rtllib_priv(dev);
	u32	RegOffSetToBeCheck = 0x3;
	u32	RegValueToBeCheck = 0x7f1;
	u32	RF3_Final_Value = 0;
	u8	ConstRetryTimes = 5, RetryTimes = 5;
	u8 ret = 0;

	for (eRFPath = (RF90_RADIO_PATH_E)RF90_PATH_A; eRFPath <priv->NumTotalRFPath; eRFPath++) {
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		pPhyReg = &priv->PHYRegDef[eRFPath];


		switch (eRFPath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs, bRFSI_RFENV);
			break;
		case RF90_PATH_B :
		case RF90_PATH_D:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs, bRFSI_RFENV<<16);
			break;
		}

		rtl8192_setBBreg(dev, pPhyReg->rfintfe, bRFSI_RFENV<<16, 0x1);

		rtl8192_setBBreg(dev, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);

		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0);
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);

		rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E) eRFPath, 0x0, bMask12Bits, 0xbf);

		rtStatus = rtl8192_phy_checkBBAndRF(dev, HW90_BLOCK_RF, (RF90_RADIO_PATH_E)eRFPath);
		if (rtStatus!= true) {
			RT_TRACE(COMP_ERR, "PHY_RF8256_Config():Check Radio[%d] Fail!!\n", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

		RetryTimes = ConstRetryTimes;
		RF3_Final_Value = 0;
		switch (eRFPath) {
		case RF90_PATH_A:
			while (RF3_Final_Value!=RegValueToBeCheck && RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_B:
			while (RF3_Final_Value!=RegValueToBeCheck && RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_C:
			while (RF3_Final_Value!=RegValueToBeCheck && RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_D:
			while (RF3_Final_Value!=RegValueToBeCheck && RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		}

		switch (eRFPath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV, u4RegValue);
			break;
		case RF90_PATH_B :
		case RF90_PATH_D:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV<<16, u4RegValue);
			break;
		}

		if (ret) {
			RT_TRACE(COMP_ERR, "phy_RF8256_Config_ParaFile():Radio[%d] Fail!!", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

	}

	RT_TRACE(COMP_PHY, "PHY Initialization Success\n") ;
	return true;

phy_RF8256_Config_ParaFile_Fail:
	RT_TRACE(COMP_ERR, "PHY Initialization failed\n") ;
	return false;
}

#ifndef RTL8192SE
void PHY_SetRF8256CCKTxPower(struct net_device*	dev, u8	powerlevel)
{
	u32	TxAGC=0;
	struct r8192_priv *priv = rtllib_priv(dev);
#ifdef RTL8190P
	u8				byte0, byte1;

	TxAGC |= ((powerlevel<<8)|powerlevel);
	TxAGC += priv->CCKTxPowerLevelOriginalOffset;

	if (priv->bDynamicTxLowPower == true
		/*pMgntInfo->bScanInProgress == true*/ )
	{
		if (priv->CustomerID == RT_CID_819x_Netcore)
			TxAGC = 0x2222;
		else
		TxAGC += ((priv->CckPwEnl<<8)|priv->CckPwEnl);
	}

	byte0 = (u8)(TxAGC & 0xff);
	byte1 = (u8)((TxAGC & 0xff00)>>8);
	if (byte0 > 0x24)
		byte0 = 0x24;
	if (byte1 > 0x24)
		byte1 = 0x24;
	if (priv->rf_type == RF_2T4R)
	{
			if (priv->RF_C_TxPwDiff > 0)
			{
				if ( (byte0 + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byte0 = 0x24 - priv->RF_C_TxPwDiff;
				if ( (byte1 + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byte1 = 0x24 - priv->RF_C_TxPwDiff;
			}
		}
	TxAGC = (byte1<<8) |byte0;
	write_nic_dword(dev, CCK_TXAGC, TxAGC);
#else
	#ifdef RTL8192E

	TxAGC = powerlevel;
	if (priv->bDynamicTxLowPower == true)
	{
		if (priv->CustomerID == RT_CID_819x_Netcore)
		TxAGC = 0x22;
	else
		TxAGC += priv->CckPwEnl;
	}
	if (TxAGC > 0x24)
		TxAGC = 0x24;
	rtl8192_setBBreg(dev, rTxAGC_CCK_Mcs32, bTxAGCRateCCK, TxAGC);
	#endif
#endif
}


void PHY_SetRF8256OFDMTxPower(struct net_device* dev, u8 powerlevel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
#ifdef RTL8190P
	u32				TxAGC1=0, TxAGC2=0, TxAGC2_tmp = 0;
	u8				i, byteVal1[4], byteVal2[4], byteVal3[4];

	if (priv->bDynamicTxHighPower == true)
	{
		TxAGC1 |= ((powerlevel<<24)|(powerlevel<<16)|(powerlevel<<8)|powerlevel);
		TxAGC2_tmp = TxAGC1;

		TxAGC1 += priv->MCSTxPowerLevelOriginalOffset[0];
		TxAGC2 =0x03030303;

		TxAGC2_tmp += priv->MCSTxPowerLevelOriginalOffset[1];
	}
	else
	{
		TxAGC1 |= ((powerlevel<<24)|(powerlevel<<16)|(powerlevel<<8)|powerlevel);
		TxAGC2 = TxAGC1;

		TxAGC1 += priv->MCSTxPowerLevelOriginalOffset[0];
		TxAGC2 += priv->MCSTxPowerLevelOriginalOffset[1];

		TxAGC2_tmp = TxAGC2;

	}
	for (i=0; i<4; i++)
	{
		byteVal1[i] = (u8)(  (TxAGC1 & (0xff<<(i*8))) >>(i*8) );
		if (byteVal1[i] > 0x24)
			byteVal1[i] = 0x24;
		byteVal2[i] = (u8)(  (TxAGC2 & (0xff<<(i*8))) >>(i*8) );
		if (byteVal2[i] > 0x24)
			byteVal2[i] = 0x24;

		byteVal3[i] = (u8)(  (TxAGC2_tmp & (0xff<<(i*8))) >>(i*8) );
		if (byteVal3[i] > 0x24)
			byteVal3[i] = 0x24;
	}

	if (priv->rf_type == RF_2T4R)
	{
		if (priv->RF_C_TxPwDiff > 0)
		{
			for (i=0; i<4; i++)
			{
				if ( (byteVal1[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal1[i] = 0x24 - priv->RF_C_TxPwDiff;
				if ( (byteVal2[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal2[i] = 0x24 - priv->RF_C_TxPwDiff;
				if ( (byteVal3[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal3[i] = 0x24 - priv->RF_C_TxPwDiff;
			}
		}
	}

	TxAGC1 = (byteVal1[3]<<24) | (byteVal1[2]<<16) |(byteVal1[1]<<8) |byteVal1[0];
	TxAGC2 = (byteVal2[3]<<24) | (byteVal2[2]<<16) |(byteVal2[1]<<8) |byteVal2[0];

	TxAGC2_tmp = (byteVal3[3]<<24) | (byteVal3[2]<<16) |(byteVal3[1]<<8) |byteVal3[0];
	priv->Pwr_Track = TxAGC2_tmp;

	write_nic_dword(dev, MCS_TXAGC, TxAGC1);
	write_nic_dword(dev, MCS_TXAGC+4, TxAGC2);
#else
#ifdef RTL8192E
	u32 writeVal, powerBase0, powerBase1, writeVal_tmp;
	u8 index = 0;
	u16 RegOffset[6] = {0xe00, 0xe04, 0xe10, 0xe14, 0xe18, 0xe1c};
	u8 byte0, byte1, byte2, byte3;

	powerBase0 = powerlevel + priv->LegacyHTTxPowerDiff;
	powerBase0 = (powerBase0<<24) | (powerBase0<<16) |(powerBase0<<8) |powerBase0;
	powerBase1 = powerlevel;
	powerBase1 = (powerBase1<<24) | (powerBase1<<16) |(powerBase1<<8) |powerBase1;

	for (index=0; index<6; index++)
	{
		writeVal = (u32)(priv->MCSTxPowerLevelOriginalOffset[index] + ((index<2)?powerBase0:powerBase1));
		byte0 = (u8)(writeVal & 0x7f);
		byte1 = (u8)((writeVal & 0x7f00)>>8);
		byte2 = (u8)((writeVal & 0x7f0000)>>16);
		byte3 = (u8)((writeVal & 0x7f000000)>>24);
		if (byte0 > 0x24)
			byte0 = 0x24;
		if (byte1 > 0x24)
			byte1 = 0x24;
		if (byte2 > 0x24)
			byte2 = 0x24;
		if (byte3 > 0x24)
			byte3 = 0x24;

		if (index == 3)
		{
			writeVal_tmp = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
			priv->Pwr_Track = writeVal_tmp;
		}

		if (priv->bDynamicTxHighPower == true)
		{
			writeVal = 0x03030303;
		}
		else
		{
			writeVal = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
		}
		rtl8192_setBBreg(dev, RegOffset[index], 0x7f7f7f7f, writeVal);
	}

#endif
#endif
	return;
}



#endif
