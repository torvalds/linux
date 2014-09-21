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
#include "r8192E_phyreg.h"
#include "r8192E_phy.h"
#include "r8190P_rtl8256.h"

void PHY_SetRF8256Bandwidth(struct net_device *dev,
			    enum ht_channel_width Bandwidth)
{
	u8	eRFPath;
	struct r8192_priv *priv = rtllib_priv(dev);

	for (eRFPath = 0; eRFPath < priv->NumTotalRFPath; eRFPath++) {
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		switch (Bandwidth) {
		case HT_CHANNEL_WIDTH_20:
			if (priv->card_8192_version == VERSION_8190_BD ||
			    priv->card_8192_version == VERSION_8190_BE) {
				rtl8192_phy_SetRFReg(dev,
						(enum rf90_radio_path)eRFPath,
						0x0b, bMask12Bits, 0x100);
				rtl8192_phy_SetRFReg(dev,
						(enum rf90_radio_path)eRFPath,
						0x2c, bMask12Bits, 0x3d7);
				rtl8192_phy_SetRFReg(dev,
						(enum rf90_radio_path)eRFPath,
						0x0e, bMask12Bits, 0x021);

			} else {
				RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): "
					 "unknown hardware version\n");
			}

			break;
		case HT_CHANNEL_WIDTH_20_40:
			if (priv->card_8192_version == VERSION_8190_BD ||
			    priv->card_8192_version == VERSION_8190_BE) {
				rtl8192_phy_SetRFReg(dev,
						 (enum rf90_radio_path)eRFPath,
						 0x0b, bMask12Bits, 0x300);
				rtl8192_phy_SetRFReg(dev,
						 (enum rf90_radio_path)eRFPath,
						 0x2c, bMask12Bits, 0x3ff);
				rtl8192_phy_SetRFReg(dev,
						 (enum rf90_radio_path)eRFPath,
						 0x0e, bMask12Bits, 0x0e1);

			} else {
				RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): "
					 "unknown hardware version\n");
			}


			break;
		default:
			RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown "
				 "Bandwidth: %#X\n", Bandwidth);
			break;

		}
	}
	return;
}

bool PHY_RF8256_Config(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->NumTotalRFPath = RTL819X_TOTAL_RF_PATH;
	return phy_RF8256_Config_ParaFile(dev);
}

bool phy_RF8256_Config_ParaFile(struct net_device *dev)
{
	u32	u4RegValue = 0;
	u8	eRFPath;
	bool rtStatus = true;
	struct bb_reg_definition *pPhyReg;
	struct r8192_priv *priv = rtllib_priv(dev);
	u32	RegOffSetToBeCheck = 0x3;
	u32	RegValueToBeCheck = 0x7f1;
	u32	RF3_Final_Value = 0;
	u8	ConstRetryTimes = 5, RetryTimes = 5;
	u8 ret = 0;

	for (eRFPath = (enum rf90_radio_path)RF90_PATH_A;
	     eRFPath < priv->NumTotalRFPath; eRFPath++) {
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		pPhyReg = &priv->PHYRegDef[eRFPath];


		switch (eRFPath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs,
							bRFSI_RFENV);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs,
							bRFSI_RFENV<<16);
			break;
		}

		rtl8192_setBBreg(dev, pPhyReg->rfintfe, bRFSI_RFENV<<16, 0x1);

		rtl8192_setBBreg(dev, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);

		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,
				 b3WireAddressLength, 0x0);
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,
				 b3WireDataLength, 0x0);

		rtl8192_phy_SetRFReg(dev, (enum rf90_radio_path) eRFPath, 0x0,
				     bMask12Bits, 0xbf);

		rtStatus = rtl8192_phy_checkBBAndRF(dev, HW90_BLOCK_RF,
						(enum rf90_radio_path)eRFPath);
		if (!rtStatus) {
			RT_TRACE(COMP_ERR, "PHY_RF8256_Config():Check "
				 "Radio[%d] Fail!!\n", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

		RetryTimes = ConstRetryTimes;
		RF3_Final_Value = 0;
		switch (eRFPath) {
		case RF90_PATH_A:
			while (RF3_Final_Value != RegValueToBeCheck &&
			       RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,
						(enum rf90_radio_path)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev,
						 (enum rf90_radio_path)eRFPath,
						 RegOffSetToBeCheck,
						 bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final "
					 "value: %x\n", eRFPath,
					 RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_B:
			while (RF3_Final_Value != RegValueToBeCheck &&
			       RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,
						(enum rf90_radio_path)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev,
						 (enum rf90_radio_path)eRFPath,
						 RegOffSetToBeCheck,
						 bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final "
					 "value: %x\n", eRFPath,
					  RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_C:
			while (RF3_Final_Value != RegValueToBeCheck &&
			       RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,
						(enum rf90_radio_path)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev,
						(enum rf90_radio_path)eRFPath,
						RegOffSetToBeCheck,
						bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final "
					 "value: %x\n", eRFPath,
					 RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_D:
			while (RF3_Final_Value != RegValueToBeCheck &&
			       RetryTimes != 0) {
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,
					       (enum rf90_radio_path)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev,
					       (enum rf90_radio_path)eRFPath,
					       RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final "
					 "value: %x\n", eRFPath,
					  RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		}

		switch (eRFPath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV,
					 u4RegValue);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV<<16,
					 u4RegValue);
			break;
		}

		if (ret) {
			RT_TRACE(COMP_ERR, "phy_RF8256_Config_ParaFile():"
				 "Radio[%d] Fail!!", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

	}

	RT_TRACE(COMP_PHY, "PHY Initialization Success\n") ;
	return true;

phy_RF8256_Config_ParaFile_Fail:
	RT_TRACE(COMP_ERR, "PHY Initialization failed\n") ;
	return false;
}

void PHY_SetRF8256CCKTxPower(struct net_device *dev, u8	powerlevel)
{
	u32	TxAGC = 0;
	struct r8192_priv *priv = rtllib_priv(dev);

	TxAGC = powerlevel;
	if (priv->bDynamicTxLowPower) {
		if (priv->CustomerID == RT_CID_819x_Netcore)
			TxAGC = 0x22;
		else
			TxAGC += priv->CckPwEnl;
	}
	if (TxAGC > 0x24)
		TxAGC = 0x24;
	rtl8192_setBBreg(dev, rTxAGC_CCK_Mcs32, bTxAGCRateCCK, TxAGC);
}


void PHY_SetRF8256OFDMTxPower(struct net_device *dev, u8 powerlevel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 writeVal, powerBase0, powerBase1, writeVal_tmp;
	u8 index = 0;
	u16 RegOffset[6] = {0xe00, 0xe04, 0xe10, 0xe14, 0xe18, 0xe1c};
	u8 byte0, byte1, byte2, byte3;

	powerBase0 = powerlevel + priv->LegacyHTTxPowerDiff;
	powerBase0 = (powerBase0 << 24) | (powerBase0 << 16) |
		     (powerBase0 << 8) | powerBase0;
	powerBase1 = powerlevel;
	powerBase1 = (powerBase1 << 24) | (powerBase1 << 16) |
		     (powerBase1 << 8) | powerBase1;

	for (index = 0; index < 6; index++) {
		writeVal = (u32)(priv->MCSTxPowerLevelOriginalOffset[index] +
			   ((index < 2) ? powerBase0 : powerBase1));
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

		if (index == 3) {
			writeVal_tmp = (byte3 << 24) | (byte2 << 16) |
				       (byte1 << 8) | byte0;
			priv->Pwr_Track = writeVal_tmp;
		}

		if (priv->bDynamicTxHighPower)
			writeVal = 0x03030303;
		else
			writeVal = (byte3 << 24) | (byte2 << 16) |
				   (byte1 << 8) | byte0;
		rtl8192_setBBreg(dev, RegOffset[index], 0x7f7f7f7f, writeVal);
	}

	return;
}
