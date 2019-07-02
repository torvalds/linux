// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include <linux/bitops.h>
#include "rtl_core.h"
#include "r8192E_hw.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h"
#include "r8192E_phy.h"
#include "rtl_dm.h"

#include "r8192E_hwimg.h"

static u32 RF_CHANNEL_TABLE_ZEBRA[] = {
	0,
	0x085c,
	0x08dc,
	0x095c,
	0x09dc,
	0x0a5c,
	0x0adc,
	0x0b5c,
	0x0bdc,
	0x0c5c,
	0x0cdc,
	0x0d5c,
	0x0ddc,
	0x0e5c,
	0x0f72,
};

/*************************Define local function prototype**********************/

static u32 _rtl92e_phy_rf_fw_read(struct net_device *dev,
				  enum rf90_radio_path eRFPath, u32 Offset);
static void _rtl92e_phy_rf_fw_write(struct net_device *dev,
				    enum rf90_radio_path eRFPath, u32 Offset,
				    u32 Data);

static u32 _rtl92e_calculate_bit_shift(u32 dwBitMask)
{
	if (!dwBitMask)
		return 32;
	return ffs(dwBitMask) - 1;
}

u8 rtl92e_is_legal_rf_path(struct net_device *dev, u32 eRFPath)
{
	u8 ret = 1;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rf_type == RF_2T4R)
		ret = 0;
	else if (priv->rf_type == RF_1T2R) {
		if (eRFPath == RF90_PATH_A || eRFPath == RF90_PATH_B)
			ret = 1;
		else if (eRFPath == RF90_PATH_C || eRFPath == RF90_PATH_D)
			ret = 0;
	}
	return ret;
}

void rtl92e_set_bb_reg(struct net_device *dev, u32 dwRegAddr, u32 dwBitMask,
		       u32 dwData)
{

	u32 OriginalValue, BitShift, NewValue;

	if (dwBitMask != bMaskDWord) {
		OriginalValue = rtl92e_readl(dev, dwRegAddr);
		BitShift = _rtl92e_calculate_bit_shift(dwBitMask);
		NewValue = (OriginalValue & ~dwBitMask) | (dwData << BitShift);
		rtl92e_writel(dev, dwRegAddr, NewValue);
	} else
		rtl92e_writel(dev, dwRegAddr, dwData);
}

u32 rtl92e_get_bb_reg(struct net_device *dev, u32 dwRegAddr, u32 dwBitMask)
{
	u32 OriginalValue, BitShift;

	OriginalValue = rtl92e_readl(dev, dwRegAddr);
	BitShift = _rtl92e_calculate_bit_shift(dwBitMask);

	return (OriginalValue & dwBitMask) >> BitShift;
}

static u32 _rtl92e_phy_rf_read(struct net_device *dev,
			       enum rf90_radio_path eRFPath, u32 Offset)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 ret = 0;
	u32 NewOffset = 0;
	struct bb_reg_definition *pPhyReg = &priv->PHYRegDef[eRFPath];

	Offset &= 0x3f;

	if (priv->rf_chip == RF_8256) {
		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);
		if (Offset >= 31) {
			priv->RfReg0Value[eRFPath] |= 0x140;
			rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
					  bMaskDWord,
					  (priv->RfReg0Value[eRFPath]<<16));
			NewOffset = Offset - 30;
		} else if (Offset >= 16) {
			priv->RfReg0Value[eRFPath] |= 0x100;
			priv->RfReg0Value[eRFPath] &= (~0x40);
			rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
					  bMaskDWord,
					  (priv->RfReg0Value[eRFPath]<<16));

			NewOffset = Offset - 15;
		} else
			NewOffset = Offset;
	} else {
		RT_TRACE((COMP_PHY|COMP_ERR),
			 "check RF type here, need to be 8256\n");
		NewOffset = Offset;
	}
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2, bLSSIReadAddress,
			  NewOffset);
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x0);
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x1);

	mdelay(1);

	ret = rtl92e_get_bb_reg(dev, pPhyReg->rfLSSIReadBack,
				bLSSIReadBackData);

	if (priv->rf_chip == RF_8256) {
		priv->RfReg0Value[eRFPath] &= 0xebf;

		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset, bMaskDWord,
				  (priv->RfReg0Value[eRFPath] << 16));

		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);
	}


	return ret;

}

static void _rtl92e_phy_rf_write(struct net_device *dev,
				 enum rf90_radio_path eRFPath, u32 Offset,
				 u32 Data)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 DataAndAddr = 0, NewOffset = 0;
	struct bb_reg_definition *pPhyReg = &priv->PHYRegDef[eRFPath];

	Offset &= 0x3f;
	if (priv->rf_chip == RF_8256) {
		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);

		if (Offset >= 31) {
			priv->RfReg0Value[eRFPath] |= 0x140;
			rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
					  bMaskDWord,
					  (priv->RfReg0Value[eRFPath] << 16));
			NewOffset = Offset - 30;
		} else if (Offset >= 16) {
			priv->RfReg0Value[eRFPath] |= 0x100;
			priv->RfReg0Value[eRFPath] &= (~0x40);
			rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
					  bMaskDWord,
					  (priv->RfReg0Value[eRFPath] << 16));
			NewOffset = Offset - 15;
		} else
			NewOffset = Offset;
	} else {
		RT_TRACE((COMP_PHY|COMP_ERR),
			 "check RF type here, need to be 8256\n");
		NewOffset = Offset;
	}

	DataAndAddr = (NewOffset & 0x3f) | (Data << 16);

	rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);

	if (Offset == 0x0)
		priv->RfReg0Value[eRFPath] = Data;

	if (priv->rf_chip == RF_8256) {
		if (Offset != 0) {
			priv->RfReg0Value[eRFPath] &= 0xebf;
			rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
					  bMaskDWord,
					  (priv->RfReg0Value[eRFPath] << 16));
		}
		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);
	}
}

void rtl92e_set_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		       u32 RegAddr, u32 BitMask, u32 Data)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 Original_Value, BitShift, New_Value;

	if (!rtl92e_is_legal_rf_path(dev, eRFPath))
		return;
	if (priv->rtllib->eRFPowerState != eRfOn && !priv->being_init_adapter)
		return;

	RT_TRACE(COMP_PHY, "FW RF CTRL is not ready now\n");
	if (priv->Rf_Mode == RF_OP_By_FW) {
		if (BitMask != bMask12Bits) {
			Original_Value = _rtl92e_phy_rf_fw_read(dev, eRFPath,
								RegAddr);
			BitShift =  _rtl92e_calculate_bit_shift(BitMask);
			New_Value = (Original_Value & ~BitMask) | (Data << BitShift);

			_rtl92e_phy_rf_fw_write(dev, eRFPath, RegAddr,
						New_Value);
		} else
			_rtl92e_phy_rf_fw_write(dev, eRFPath, RegAddr, Data);
		udelay(200);

	} else {
		if (BitMask != bMask12Bits) {
			Original_Value = _rtl92e_phy_rf_read(dev, eRFPath,
							     RegAddr);
			BitShift =  _rtl92e_calculate_bit_shift(BitMask);
			New_Value = (Original_Value & ~BitMask) | (Data << BitShift);

			_rtl92e_phy_rf_write(dev, eRFPath, RegAddr, New_Value);
		} else
			_rtl92e_phy_rf_write(dev, eRFPath, RegAddr, Data);
	}
}

u32 rtl92e_get_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		      u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (!rtl92e_is_legal_rf_path(dev, eRFPath))
		return 0;
	if (priv->rtllib->eRFPowerState != eRfOn && !priv->being_init_adapter)
		return	0;
	mutex_lock(&priv->rf_mutex);
	if (priv->Rf_Mode == RF_OP_By_FW) {
		Original_Value = _rtl92e_phy_rf_fw_read(dev, eRFPath, RegAddr);
		udelay(200);
	} else {
		Original_Value = _rtl92e_phy_rf_read(dev, eRFPath, RegAddr);
	}
	BitShift =  _rtl92e_calculate_bit_shift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;
	mutex_unlock(&priv->rf_mutex);
	return Readback_Value;
}

static u32 _rtl92e_phy_rf_fw_read(struct net_device *dev,
				  enum rf90_radio_path eRFPath, u32 Offset)
{
	u32		Data = 0;
	u8		time = 0;

	Data |= ((Offset & 0xFF) << 12);
	Data |= ((eRFPath & 0x3) << 20);
	Data |= 0x80000000;
	while (rtl92e_readl(dev, QPNR) & 0x80000000) {
		if (time++ < 100)
			udelay(10);
		else
			break;
	}
	rtl92e_writel(dev, QPNR, Data);
	while (rtl92e_readl(dev, QPNR) & 0x80000000) {
		if (time++ < 100)
			udelay(10);
		else
			return 0;
	}
	return rtl92e_readl(dev, RF_DATA);

}

static void _rtl92e_phy_rf_fw_write(struct net_device *dev,
				    enum rf90_radio_path eRFPath, u32 Offset,
				    u32 Data)
{
	u8	time = 0;

	Data |= ((Offset & 0xFF) << 12);
	Data |= ((eRFPath & 0x3) << 20);
	Data |= 0x400000;
	Data |= 0x80000000;

	while (rtl92e_readl(dev, QPNR) & 0x80000000) {
		if (time++ < 100)
			udelay(10);
		else
			break;
	}
	rtl92e_writel(dev, QPNR, Data);

}


void rtl92e_config_mac(struct net_device *dev)
{
	u32 dwArrayLen = 0, i = 0;
	u32 *pdwArray = NULL;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->bTXPowerDataReadFromEEPORM) {
		RT_TRACE(COMP_PHY, "Rtl819XMACPHY_Array_PG\n");
		dwArrayLen = MACPHY_Array_PGLength;
		pdwArray = Rtl819XMACPHY_Array_PG;

	} else {
		RT_TRACE(COMP_PHY, "Read rtl819XMACPHY_Array\n");
		dwArrayLen = MACPHY_ArrayLength;
		pdwArray = Rtl819XMACPHY_Array;
	}
	for (i = 0; i < dwArrayLen; i += 3) {
		RT_TRACE(COMP_DBG,
			 "The Rtl8190MACPHY_Array[0] is %x Rtl8190MACPHY_Array[1] is %x Rtl8190MACPHY_Array[2] is %x\n",
			 pdwArray[i], pdwArray[i+1], pdwArray[i+2]);
		if (pdwArray[i] == 0x318)
			pdwArray[i+2] = 0x00000800;
		rtl92e_set_bb_reg(dev, pdwArray[i], pdwArray[i+1],
				  pdwArray[i+2]);
	}
	return;

}

static void _rtl92e_phy_config_bb(struct net_device *dev, u8 ConfigType)
{
	int i;
	u32 *Rtl819XPHY_REGArray_Table = NULL;
	u32 *Rtl819XAGCTAB_Array_Table = NULL;
	u16 AGCTAB_ArrayLen, PHY_REGArrayLen = 0;
	struct r8192_priv *priv = rtllib_priv(dev);

	AGCTAB_ArrayLen = AGCTAB_ArrayLength;
	Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_Array;
	if (priv->rf_type == RF_2T4R) {
		PHY_REGArrayLen = PHY_REGArrayLength;
		Rtl819XPHY_REGArray_Table = Rtl819XPHY_REGArray;
	} else if (priv->rf_type == RF_1T2R) {
		PHY_REGArrayLen = PHY_REG_1T2RArrayLength;
		Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T2RArray;
	}

	if (ConfigType == BaseBand_Config_PHY_REG) {
		for (i = 0; i < PHY_REGArrayLen; i += 2) {
			rtl92e_set_bb_reg(dev, Rtl819XPHY_REGArray_Table[i],
					  bMaskDWord,
					  Rtl819XPHY_REGArray_Table[i+1]);
			RT_TRACE(COMP_DBG,
				 "i: %x, The Rtl819xUsbPHY_REGArray[0] is %x Rtl819xUsbPHY_REGArray[1] is %x\n",
				 i, Rtl819XPHY_REGArray_Table[i],
				 Rtl819XPHY_REGArray_Table[i+1]);
		}
	} else if (ConfigType == BaseBand_Config_AGC_TAB) {
		for (i = 0; i < AGCTAB_ArrayLen; i += 2) {
			rtl92e_set_bb_reg(dev, Rtl819XAGCTAB_Array_Table[i],
					  bMaskDWord,
					  Rtl819XAGCTAB_Array_Table[i+1]);
			RT_TRACE(COMP_DBG,
				 "i:%x, The rtl819XAGCTAB_Array[0] is %x rtl819XAGCTAB_Array[1] is %x\n",
				 i, Rtl819XAGCTAB_Array_Table[i],
				 Rtl819XAGCTAB_Array_Table[i+1]);
		}
	}
}

static void _rtl92e_init_bb_rf_reg_def(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->PHYRegDef[RF90_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	priv->PHYRegDef[RF90_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	priv->PHYRegDef[RF90_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;
	priv->PHYRegDef[RF90_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;

	priv->PHYRegDef[RF90_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB;
	priv->PHYRegDef[RF90_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;
	priv->PHYRegDef[RF90_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;
	priv->PHYRegDef[RF90_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;

	priv->PHYRegDef[RF90_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_C].rfintfo = rFPGA0_XC_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_D].rfintfo = rFPGA0_XD_RFInterfaceOE;

	priv->PHYRegDef[RF90_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_C].rfintfe = rFPGA0_XC_RFInterfaceOE;
	priv->PHYRegDef[RF90_PATH_D].rfintfe = rFPGA0_XD_RFInterfaceOE;

	priv->PHYRegDef[RF90_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_C].rf3wireOffset = rFPGA0_XC_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_D].rf3wireOffset = rFPGA0_XD_LSSIParameter;

	priv->PHYRegDef[RF90_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	priv->PHYRegDef[RF90_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	priv->PHYRegDef[RF90_PATH_A].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_B].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_C].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_D].rfTxGainStage = rFPGA0_TxGainStage;

	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara1 = rFPGA0_XC_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara1 = rFPGA0_XD_HSSIParameter1;

	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara2 = rFPGA0_XC_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara2 = rFPGA0_XD_HSSIParameter2;

	priv->PHYRegDef[RF90_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	priv->PHYRegDef[RF90_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	priv->PHYRegDef[RF90_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	priv->PHYRegDef[RF90_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	priv->PHYRegDef[RF90_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	priv->PHYRegDef[RF90_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	priv->PHYRegDef[RF90_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	priv->PHYRegDef[RF90_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	priv->PHYRegDef[RF90_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

}

bool rtl92e_check_bb_and_rf(struct net_device *dev, enum hw90_block CheckBlock,
			    enum rf90_radio_path eRFPath)
{
	bool ret = true;
	u32 i, CheckTimes = 4, dwRegRead = 0;
	u32 WriteAddr[4];
	u32 WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};

	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;
	RT_TRACE(COMP_PHY, "=======>%s(), CheckBlock:%d\n", __func__,
		 CheckBlock);

	if (CheckBlock == HW90_BLOCK_MAC) {
		netdev_warn(dev, "%s(): No checks available for MAC block.\n",
			    __func__);
		return ret;
	}

	for (i = 0; i < CheckTimes; i++) {
		switch (CheckBlock) {
		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			rtl92e_writel(dev, WriteAddr[CheckBlock],
				      WriteData[i]);
			dwRegRead = rtl92e_readl(dev, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			WriteData[i] &= 0xfff;
			rtl92e_set_rf_reg(dev, eRFPath,
					  WriteAddr[HW90_BLOCK_RF],
					  bMask12Bits, WriteData[i]);
			mdelay(10);
			dwRegRead = rtl92e_get_rf_reg(dev, eRFPath,
						      WriteAddr[HW90_BLOCK_RF],
						      bMaskDWord);
			mdelay(10);
			break;

		default:
			ret = false;
			break;
		}


		if (dwRegRead != WriteData[i]) {
			netdev_warn(dev, "%s(): Check failed.\n", __func__);
			ret = false;
			break;
		}
	}

	return ret;
}

static bool _rtl92e_bb_config_para_file(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool rtStatus = true;
	u8 bRegValue = 0, eCheckItem = 0;
	u32 dwRegValue = 0;

	bRegValue = rtl92e_readb(dev, BB_GLOBAL_RESET);
	rtl92e_writeb(dev, BB_GLOBAL_RESET, (bRegValue|BB_GLOBAL_RESET_BIT));

	dwRegValue = rtl92e_readl(dev, CPU_GEN);
	rtl92e_writel(dev, CPU_GEN, (dwRegValue&(~CPU_GEN_BB_RST)));

	for (eCheckItem = (enum hw90_block)HW90_BLOCK_PHY0;
	     eCheckItem <= HW90_BLOCK_PHY1; eCheckItem++) {
		rtStatus  = rtl92e_check_bb_and_rf(dev,
						   (enum hw90_block)eCheckItem,
						   (enum rf90_radio_path)0);
		if (!rtStatus) {
			RT_TRACE((COMP_ERR | COMP_PHY),
				 "rtl92e_config_rf():Check PHY%d Fail!!\n",
				 eCheckItem-1);
			return rtStatus;
		}
	}
	rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x0);
	_rtl92e_phy_config_bb(dev, BaseBand_Config_PHY_REG);

	dwRegValue = rtl92e_readl(dev, CPU_GEN);
	rtl92e_writel(dev, CPU_GEN, (dwRegValue|CPU_GEN_BB_RST));

	_rtl92e_phy_config_bb(dev, BaseBand_Config_AGC_TAB);

	if (priv->IC_Cut  > VERSION_8190_BD) {
		if (priv->rf_type == RF_2T4R)
			dwRegValue = priv->AntennaTxPwDiff[2]<<8 |
				      priv->AntennaTxPwDiff[1]<<4 |
				      priv->AntennaTxPwDiff[0];
		else
			dwRegValue = 0x0;
		rtl92e_set_bb_reg(dev, rFPGA0_TxGainStage,
				  (bXBTxAGC|bXCTxAGC|bXDTxAGC), dwRegValue);


		dwRegValue = priv->CrystalCap;
		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, bXtalCap92x,
				  dwRegValue);
	}

	return rtStatus;
}
bool rtl92e_config_bb(struct net_device *dev)
{
	_rtl92e_init_bb_rf_reg_def(dev);
	return _rtl92e_bb_config_para_file(dev);
}

void rtl92e_get_tx_power(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->MCSTxPowerLevelOriginalOffset[0] =
		rtl92e_readl(dev, rTxAGC_Rate18_06);
	priv->MCSTxPowerLevelOriginalOffset[1] =
		rtl92e_readl(dev, rTxAGC_Rate54_24);
	priv->MCSTxPowerLevelOriginalOffset[2] =
		rtl92e_readl(dev, rTxAGC_Mcs03_Mcs00);
	priv->MCSTxPowerLevelOriginalOffset[3] =
		rtl92e_readl(dev, rTxAGC_Mcs07_Mcs04);
	priv->MCSTxPowerLevelOriginalOffset[4] =
		rtl92e_readl(dev, rTxAGC_Mcs11_Mcs08);
	priv->MCSTxPowerLevelOriginalOffset[5] =
		rtl92e_readl(dev, rTxAGC_Mcs15_Mcs12);

	priv->DefaultInitialGain[0] = rtl92e_readb(dev, rOFDM0_XAAGCCore1);
	priv->DefaultInitialGain[1] = rtl92e_readb(dev, rOFDM0_XBAGCCore1);
	priv->DefaultInitialGain[2] = rtl92e_readb(dev, rOFDM0_XCAGCCore1);
	priv->DefaultInitialGain[3] = rtl92e_readb(dev, rOFDM0_XDAGCCore1);
	RT_TRACE(COMP_INIT,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x)\n",
		 priv->DefaultInitialGain[0], priv->DefaultInitialGain[1],
		 priv->DefaultInitialGain[2], priv->DefaultInitialGain[3]);

	priv->framesync = rtl92e_readb(dev, rOFDM0_RxDetector3);
	priv->framesyncC34 = rtl92e_readl(dev, rOFDM0_RxDetector2);
	RT_TRACE(COMP_INIT, "Default framesync (0x%x) = 0x%x\n",
		rOFDM0_RxDetector3, priv->framesync);
	priv->SifsTime = rtl92e_readw(dev, SIFS);
}

void rtl92e_set_tx_power(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8	powerlevel = 0, powerlevelOFDM24G = 0;
	s8	ant_pwr_diff;
	u32	u4RegValue;

	if (priv->epromtype == EEPROM_93C46) {
		powerlevel = priv->TxPowerLevelCCK[channel-1];
		powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];
	} else if (priv->epromtype == EEPROM_93C56) {
		if (priv->rf_type == RF_1T2R) {
			powerlevel = priv->TxPowerLevelCCK_C[channel-1];
			powerlevelOFDM24G = priv->TxPowerLevelOFDM24G_C[channel-1];
		} else if (priv->rf_type == RF_2T4R) {
			powerlevel = priv->TxPowerLevelCCK_A[channel-1];
			powerlevelOFDM24G = priv->TxPowerLevelOFDM24G_A[channel-1];

			ant_pwr_diff = priv->TxPowerLevelOFDM24G_C[channel-1]
				       - priv->TxPowerLevelOFDM24G_A[channel-1];

			priv->RF_C_TxPwDiff = ant_pwr_diff;

			ant_pwr_diff &= 0xf;

			priv->AntennaTxPwDiff[2] = 0;
			priv->AntennaTxPwDiff[1] = (u8)(ant_pwr_diff);
			priv->AntennaTxPwDiff[0] = 0;

			u4RegValue = priv->AntennaTxPwDiff[2]<<8 |
				      priv->AntennaTxPwDiff[1]<<4 |
				      priv->AntennaTxPwDiff[0];

			rtl92e_set_bb_reg(dev, rFPGA0_TxGainStage,
					  (bXBTxAGC|bXCTxAGC|bXDTxAGC),
					  u4RegValue);
		}
	}
	switch (priv->rf_chip) {
	case RF_8225:
		break;
	case RF_8256:
		rtl92e_set_cck_tx_power(dev, powerlevel);
		rtl92e_set_ofdm_tx_power(dev, powerlevelOFDM24G);
		break;
	case RF_8258:
		break;
	default:
		netdev_err(dev, "Invalid RF Chip ID.\n");
		break;
	}
}

bool rtl92e_config_phy(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool rtStatus = true;

	switch (priv->rf_chip) {
	case RF_8225:
		break;
	case RF_8256:
		rtStatus = rtl92e_config_rf(dev);
		break;

	case RF_8258:
		break;
	case RF_PSEUDO_11N:
		break;

	default:
		netdev_err(dev, "Invalid RF Chip ID.\n");
		break;
	}
	return rtStatus;
}

u8 rtl92e_config_rf_path(struct net_device *dev, enum rf90_radio_path eRFPath)
{

	int i;

	switch (eRFPath) {
	case RF90_PATH_A:
		for (i = 0; i < RadioA_ArrayLength; i += 2) {
			if (Rtl819XRadioA_Array[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, Rtl819XRadioA_Array[i],
					  bMask12Bits,
					  Rtl819XRadioA_Array[i+1]);

		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < RadioB_ArrayLength; i += 2) {
			if (Rtl819XRadioB_Array[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, Rtl819XRadioB_Array[i],
					  bMask12Bits,
					  Rtl819XRadioB_Array[i+1]);

		}
		break;
	case RF90_PATH_C:
		for (i = 0; i < RadioC_ArrayLength; i += 2) {
			if (Rtl819XRadioC_Array[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, Rtl819XRadioC_Array[i],
					  bMask12Bits,
					  Rtl819XRadioC_Array[i+1]);

		}
		break;
	case RF90_PATH_D:
		for (i = 0; i < RadioD_ArrayLength; i += 2) {
			if (Rtl819XRadioD_Array[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, Rtl819XRadioD_Array[i],
					  bMask12Bits,
					  Rtl819XRadioD_Array[i+1]);

		}
		break;
	default:
		break;
	}

	return 0;

}

static void _rtl92e_set_tx_power_level(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8	powerlevel = priv->TxPowerLevelCCK[channel-1];
	u8	powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];

	switch (priv->rf_chip) {
	case RF_8225:
		break;

	case RF_8256:
		rtl92e_set_cck_tx_power(dev, powerlevel);
		rtl92e_set_ofdm_tx_power(dev, powerlevelOFDM24G);
		break;

	case RF_8258:
		break;
	default:
		netdev_warn(dev, "%s(): Invalid RF Chip ID\n", __func__);
		break;
	}
}

static u8 _rtl92e_phy_set_sw_chnl_cmd_array(struct net_device *dev,
					    struct sw_chnl_cmd *CmdTable,
					    u32 CmdTableIdx, u32 CmdTableSz,
					    enum sw_chnl_cmd_id CmdID,
					    u32 Para1, u32 Para2, u32 msDelay)
{
	struct sw_chnl_cmd *pCmd;

	if (CmdTable == NULL) {
		netdev_err(dev, "%s(): CmdTable cannot be NULL.\n", __func__);
		return false;
	}
	if (CmdTableIdx >= CmdTableSz) {
		netdev_err(dev, "%s(): Invalid index requested.\n", __func__);
		return false;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return true;
}

static u8 _rtl92e_phy_switch_channel_step(struct net_device *dev, u8 channel,
					  u8 *stage, u8 *step, u32 *delay)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	u32					PreCommonCmdCnt;
	u32					PostCommonCmdCnt;
	u32					RfDependCmdCnt;
	struct sw_chnl_cmd *CurrentCmd = NULL;
	u8		eRFPath;

	RT_TRACE(COMP_TRACE, "====>%s()====stage:%d, step:%d, channel:%d\n",
		  __func__, *stage, *step, channel);

	if (!rtllib_legal_channel(priv->rtllib, channel)) {
		netdev_err(dev, "Invalid channel requested: %d\n", channel);
		return true;
	}

	{
		PreCommonCmdCnt = 0;
		_rtl92e_phy_set_sw_chnl_cmd_array(dev, ieee->PreCommonCmd,
						  PreCommonCmdCnt++,
						  MAX_PRECMD_CNT,
						  CmdID_SetTxPowerLevel,
						  0, 0, 0);
		_rtl92e_phy_set_sw_chnl_cmd_array(dev, ieee->PreCommonCmd,
						  PreCommonCmdCnt++,
						  MAX_PRECMD_CNT, CmdID_End,
						  0, 0, 0);

		PostCommonCmdCnt = 0;

		_rtl92e_phy_set_sw_chnl_cmd_array(dev, ieee->PostCommonCmd,
						  PostCommonCmdCnt++,
						  MAX_POSTCMD_CNT, CmdID_End,
						  0, 0, 0);

		RfDependCmdCnt = 0;
		switch (priv->rf_chip) {
		case RF_8225:
			if (!(channel >= 1 && channel <= 14)) {
				netdev_err(dev,
					   "Invalid channel requested for 8225: %d\n",
					   channel);
				return false;
			}
			_rtl92e_phy_set_sw_chnl_cmd_array(dev,
							  ieee->RfDependCmd,
							  RfDependCmdCnt++,
							  MAX_RFDEPENDCMD_CNT,
							  CmdID_RF_WriteReg,
							  rZebra1_Channel,
							  RF_CHANNEL_TABLE_ZEBRA[channel],
							  10);
			_rtl92e_phy_set_sw_chnl_cmd_array(dev,
							  ieee->RfDependCmd,
							  RfDependCmdCnt++,
							  MAX_RFDEPENDCMD_CNT,
							  CmdID_End, 0, 0, 0);
			break;

		case RF_8256:
			if (!(channel >= 1 && channel <= 14)) {
				netdev_err(dev,
					   "Invalid channel requested for 8256: %d\n",
					   channel);
				return false;
			}
			_rtl92e_phy_set_sw_chnl_cmd_array(dev,
							  ieee->RfDependCmd,
							  RfDependCmdCnt++,
							  MAX_RFDEPENDCMD_CNT,
							  CmdID_RF_WriteReg,
							  rZebra1_Channel,
							  channel, 10);
			_rtl92e_phy_set_sw_chnl_cmd_array(dev,
							  ieee->RfDependCmd,
							  RfDependCmdCnt++,
							  MAX_RFDEPENDCMD_CNT,
							  CmdID_End, 0, 0, 0);
			break;

		case RF_8258:
			break;

		default:
			netdev_warn(dev, "Unknown RF Chip ID\n");
			return false;
		}


		do {
			switch (*stage) {
			case 0:
				CurrentCmd = &ieee->PreCommonCmd[*step];
				break;
			case 1:
				CurrentCmd = &ieee->RfDependCmd[*step];
				break;
			case 2:
				CurrentCmd = &ieee->PostCommonCmd[*step];
				break;
			}

			if (CurrentCmd && CurrentCmd->CmdID == CmdID_End) {
				if ((*stage) == 2)
					return true;
				(*stage)++;
				(*step) = 0;
				continue;
			}

			if (!CurrentCmd)
				continue;
			switch (CurrentCmd->CmdID) {
			case CmdID_SetTxPowerLevel:
				if (priv->IC_Cut > (u8)VERSION_8190_BD)
					_rtl92e_set_tx_power_level(dev,
								   channel);
				break;
			case CmdID_WritePortUlong:
				rtl92e_writel(dev, CurrentCmd->Para1,
					      CurrentCmd->Para2);
				break;
			case CmdID_WritePortUshort:
				rtl92e_writew(dev, CurrentCmd->Para1,
					      (u16)CurrentCmd->Para2);
				break;
			case CmdID_WritePortUchar:
				rtl92e_writeb(dev, CurrentCmd->Para1,
					      (u8)CurrentCmd->Para2);
				break;
			case CmdID_RF_WriteReg:
				for (eRFPath = 0; eRFPath <
				     priv->NumTotalRFPath; eRFPath++)
					rtl92e_set_rf_reg(dev,
						 (enum rf90_radio_path)eRFPath,
						 CurrentCmd->Para1, bMask12Bits,
						 CurrentCmd->Para2<<7);
				break;
			default:
				break;
			}

			break;
		} while (true);
	} /*for (Number of RF paths)*/

	(*delay) = CurrentCmd->msDelay;
	(*step)++;
	return false;
}

static void _rtl92e_phy_switch_channel(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 delay = 0;

	while (!_rtl92e_phy_switch_channel_step(dev, channel,
						&priv->SwChnlStage,
						&priv->SwChnlStep, &delay)) {
		if (delay > 0)
			msleep(delay);
		if (!priv->up)
			break;
	}
}

static void _rtl92e_phy_switch_channel_work_item(struct net_device *dev)
{

	struct r8192_priv *priv = rtllib_priv(dev);

	RT_TRACE(COMP_TRACE, "==> SwChnlCallback819xUsbWorkItem()\n");

	RT_TRACE(COMP_TRACE, "=====>--%s(), set chan:%d, priv:%p\n", __func__,
		 priv->chan, priv);

	_rtl92e_phy_switch_channel(dev, priv->chan);

	RT_TRACE(COMP_TRACE, "<== SwChnlCallback819xUsbWorkItem()\n");
}

u8 rtl92e_set_channel(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	RT_TRACE(COMP_PHY, "=====>%s()\n", __func__);
	if (!priv->up) {
		netdev_err(dev, "%s(): Driver is not initialized\n", __func__);
		return false;
	}
	if (priv->SwChnlInProgress)
		return false;


	switch (priv->rtllib->mode) {
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		if (channel <= 14) {
			netdev_warn(dev,
				    "Channel %d not available in 802.11a.\n",
				    channel);
			return false;
		}
		break;
	case WIRELESS_MODE_B:
		if (channel > 14) {
			netdev_warn(dev,
				    "Channel %d not available in 802.11b.\n",
				    channel);
			return false;
		}
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		if (channel > 14) {
			netdev_warn(dev,
				    "Channel %d not available in 802.11g.\n",
				    channel);
			return false;
		}
		break;
	}

	priv->SwChnlInProgress = true;
	if (channel == 0)
		channel = 1;

	priv->chan = channel;

	priv->SwChnlStage = 0;
	priv->SwChnlStep = 0;

	if (priv->up)
		_rtl92e_phy_switch_channel_work_item(dev);
	priv->SwChnlInProgress = false;
	return true;
}

static void _rtl92e_cck_tx_power_track_bw_switch_tssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		priv->CCKPresentAttentuation =
			priv->CCKPresentAttentuation_20Mdefault +
			    priv->CCKPresentAttentuation_difference;

		if (priv->CCKPresentAttentuation >
		    (CCKTxBBGainTableLength-1))
			priv->CCKPresentAttentuation =
					 CCKTxBBGainTableLength-1;
		if (priv->CCKPresentAttentuation < 0)
			priv->CCKPresentAttentuation = 0;

		RT_TRACE(COMP_POWER_TRACKING,
			 "20M, priv->CCKPresentAttentuation = %d\n",
			 priv->CCKPresentAttentuation);

		if (priv->rtllib->current_network.channel == 14 &&
		    !priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = true;
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else if (priv->rtllib->current_network.channel !=
			   14 && priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = false;
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else {
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		}
		break;

	case HT_CHANNEL_WIDTH_20_40:
		priv->CCKPresentAttentuation =
			priv->CCKPresentAttentuation_40Mdefault +
			priv->CCKPresentAttentuation_difference;

		RT_TRACE(COMP_POWER_TRACKING,
			 "40M, priv->CCKPresentAttentuation = %d\n",
			 priv->CCKPresentAttentuation);
		if (priv->CCKPresentAttentuation >
		    (CCKTxBBGainTableLength - 1))
			priv->CCKPresentAttentuation =
					 CCKTxBBGainTableLength-1;
		if (priv->CCKPresentAttentuation < 0)
			priv->CCKPresentAttentuation = 0;

		if (priv->rtllib->current_network.channel == 14 &&
		    !priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = true;
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else if (priv->rtllib->current_network.channel != 14
			   && priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = false;
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else {
			rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		}
		break;
	}
}

static void _rtl92e_cck_tx_power_track_bw_switch_thermal(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rtllib->current_network.channel == 14 &&
	    !priv->bcck_in_ch14)
		priv->bcck_in_ch14 = true;
	else if (priv->rtllib->current_network.channel != 14 &&
		 priv->bcck_in_ch14)
		priv->bcck_in_ch14 = false;

	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		if (priv->Record_CCK_20Mindex == 0)
			priv->Record_CCK_20Mindex = 6;
		priv->CCK_index = priv->Record_CCK_20Mindex;
		RT_TRACE(COMP_POWER_TRACKING,
			 "20MHz, _rtl92e_cck_tx_power_track_bw_switch_thermal(),CCK_index = %d\n",
			 priv->CCK_index);
	break;

	case HT_CHANNEL_WIDTH_20_40:
		priv->CCK_index = priv->Record_CCK_40Mindex;
		RT_TRACE(COMP_POWER_TRACKING,
			 "40MHz, _rtl92e_cck_tx_power_track_bw_switch_thermal(), CCK_index = %d\n",
			 priv->CCK_index);
	break;
	}
	rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
}

static void _rtl92e_cck_tx_power_track_bw_switch(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->IC_Cut >= IC_VersionCut_D)
		_rtl92e_cck_tx_power_track_bw_switch_tssi(dev);
	else
		_rtl92e_cck_tx_power_track_bw_switch_thermal(dev);
}

static void _rtl92e_set_bw_mode_work_item(struct net_device *dev)
{

	struct r8192_priv *priv = rtllib_priv(dev);
	u8 regBwOpMode;

	RT_TRACE(COMP_SWBW,
		 "==>_rtl92e_set_bw_mode_work_item()  Switch to %s bandwidth\n",
		 priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20 ?
			 "20MHz" : "40MHz");


	if (priv->rf_chip == RF_PSEUDO_11N) {
		priv->SetBWModeInProgress = false;
		return;
	}
	if (!priv->up) {
		netdev_err(dev, "%s(): Driver is not initialized\n", __func__);
		return;
	}
	regBwOpMode = rtl92e_readb(dev, BW_OPMODE);

	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		rtl92e_writeb(dev, BW_OPMODE, regBwOpMode);
		break;

	case HT_CHANNEL_WIDTH_20_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		rtl92e_writeb(dev, BW_OPMODE, regBwOpMode);
		break;

	default:
		netdev_err(dev, "%s(): unknown Bandwidth: %#X\n", __func__,
			   priv->CurrentChannelBW);
		break;
	}

	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
		rtl92e_set_bb_reg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);

		if (!priv->btxpower_tracking) {
			rtl92e_writel(dev, rCCK0_TxFilter1, 0x1a1b0000);
			rtl92e_writel(dev, rCCK0_TxFilter2, 0x090e1317);
			rtl92e_writel(dev, rCCK0_DebugPort, 0x00000204);
		} else {
			_rtl92e_cck_tx_power_track_bw_switch(dev);
		}

		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x00100000, 1);

		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
		rtl92e_set_bb_reg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);

		if (!priv->btxpower_tracking) {
			rtl92e_writel(dev, rCCK0_TxFilter1, 0x35360000);
			rtl92e_writel(dev, rCCK0_TxFilter2, 0x121c252e);
			rtl92e_writel(dev, rCCK0_DebugPort, 0x00000409);
		} else {
			_rtl92e_cck_tx_power_track_bw_switch(dev);
		}

		rtl92e_set_bb_reg(dev, rCCK0_System, bCCKSideBand,
				  (priv->nCur40MhzPrimeSC>>1));
		rtl92e_set_bb_reg(dev, rOFDM1_LSTF, 0xC00,
				  priv->nCur40MhzPrimeSC);

		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x00100000, 0);
		break;
	default:
		netdev_err(dev, "%s(): unknown Bandwidth: %#X\n", __func__,
			   priv->CurrentChannelBW);
		break;

	}

	switch (priv->rf_chip) {
	case RF_8225:
		break;

	case RF_8256:
		rtl92e_set_bandwidth(dev, priv->CurrentChannelBW);
		break;

	case RF_8258:
		break;

	case RF_PSEUDO_11N:
		break;

	default:
		netdev_info(dev, "%s(): Unknown RFChipID: %d\n", __func__,
			    priv->rf_chip);
		break;
	}

	atomic_dec(&(priv->rtllib->atm_swbw));
	priv->SetBWModeInProgress = false;

	RT_TRACE(COMP_SWBW, "<==SetBWMode819xUsb()");
}

void rtl92e_set_bw_mode(struct net_device *dev, enum ht_channel_width Bandwidth,
			enum ht_extchnl_offset Offset)
{
	struct r8192_priv *priv = rtllib_priv(dev);


	if (priv->SetBWModeInProgress)
		return;

	atomic_inc(&(priv->rtllib->atm_swbw));
	priv->SetBWModeInProgress = true;

	priv->CurrentChannelBW = Bandwidth;

	if (Offset == HT_EXTCHNL_OFFSET_LOWER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (Offset == HT_EXTCHNL_OFFSET_UPPER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	_rtl92e_set_bw_mode_work_item(dev);

}

void rtl92e_init_gain(struct net_device *dev, u8 Operation)
{
#define SCAN_RX_INITIAL_GAIN	0x17
#define POWER_DETECTION_TH	0x08
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 BitMask;
	u8 initial_gain;

	if (priv->up) {
		switch (Operation) {
		case IG_Backup:
			RT_TRACE(COMP_SCAN,
				 "IG_Backup, backup the initial gain.\n");
			initial_gain = SCAN_RX_INITIAL_GAIN;
			BitMask = bMaskByte0;
			if (dm_digtable.dig_algorithm ==
			    DIG_ALGO_BY_FALSE_ALARM)
				rtl92e_set_bb_reg(dev, UFWP, bMaskByte1, 0x8);
			priv->initgain_backup.xaagccore1 =
				 (u8)rtl92e_get_bb_reg(dev, rOFDM0_XAAGCCore1,
						       BitMask);
			priv->initgain_backup.xbagccore1 =
				 (u8)rtl92e_get_bb_reg(dev, rOFDM0_XBAGCCore1,
						       BitMask);
			priv->initgain_backup.xcagccore1 =
				 (u8)rtl92e_get_bb_reg(dev, rOFDM0_XCAGCCore1,
						       BitMask);
			priv->initgain_backup.xdagccore1 =
				 (u8)rtl92e_get_bb_reg(dev, rOFDM0_XDAGCCore1,
						       BitMask);
			BitMask = bMaskByte2;
			priv->initgain_backup.cca = (u8)rtl92e_get_bb_reg(dev,
						    rCCK0_CCA, BitMask);

			RT_TRACE(COMP_SCAN,
				 "Scan InitialGainBackup 0xc50 is %x\n",
				 priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan InitialGainBackup 0xc58 is %x\n",
				 priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan InitialGainBackup 0xc60 is %x\n",
				 priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan InitialGainBackup 0xc68 is %x\n",
				 priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan InitialGainBackup 0xa0a is %x\n",
				 priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN, "Write scan initial gain = 0x%x\n",
				 initial_gain);
			rtl92e_writeb(dev, rOFDM0_XAAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XBAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XCAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XDAGCCore1, initial_gain);
			RT_TRACE(COMP_SCAN, "Write scan 0xa0a = 0x%x\n",
				 POWER_DETECTION_TH);
			rtl92e_writeb(dev, 0xa0a, POWER_DETECTION_TH);
			break;
		case IG_Restore:
			RT_TRACE(COMP_SCAN,
				 "IG_Restore, restore the initial gain.\n");
			BitMask = 0x7f;
			if (dm_digtable.dig_algorithm ==
			    DIG_ALGO_BY_FALSE_ALARM)
				rtl92e_set_bb_reg(dev, UFWP, bMaskByte1, 0x8);

			rtl92e_set_bb_reg(dev, rOFDM0_XAAGCCore1, BitMask,
					 (u32)priv->initgain_backup.xaagccore1);
			rtl92e_set_bb_reg(dev, rOFDM0_XBAGCCore1, BitMask,
					 (u32)priv->initgain_backup.xbagccore1);
			rtl92e_set_bb_reg(dev, rOFDM0_XCAGCCore1, BitMask,
					 (u32)priv->initgain_backup.xcagccore1);
			rtl92e_set_bb_reg(dev, rOFDM0_XDAGCCore1, BitMask,
					 (u32)priv->initgain_backup.xdagccore1);
			BitMask  = bMaskByte2;
			rtl92e_set_bb_reg(dev, rCCK0_CCA, BitMask,
					 (u32)priv->initgain_backup.cca);

			RT_TRACE(COMP_SCAN,
				 "Scan BBInitialGainRestore 0xc50 is %x\n",
				 priv->initgain_backup.xaagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan BBInitialGainRestore 0xc58 is %x\n",
				 priv->initgain_backup.xbagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan BBInitialGainRestore 0xc60 is %x\n",
				 priv->initgain_backup.xcagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan BBInitialGainRestore 0xc68 is %x\n",
				 priv->initgain_backup.xdagccore1);
			RT_TRACE(COMP_SCAN,
				 "Scan BBInitialGainRestore 0xa0a is %x\n",
				 priv->initgain_backup.cca);

			rtl92e_set_tx_power(dev,
					 priv->rtllib->current_network.channel);

			if (dm_digtable.dig_algorithm ==
			    DIG_ALGO_BY_FALSE_ALARM)
				rtl92e_set_bb_reg(dev, UFWP, bMaskByte1, 0x1);
			break;
		default:
			RT_TRACE(COMP_SCAN, "Unknown IG Operation.\n");
			break;
		}
	}
}

void rtl92e_set_rf_off(struct net_device *dev)
{

	rtl92e_set_bb_reg(dev, rFPGA0_XA_RFInterfaceOE, BIT4, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x18, 0x0);
	rtl92e_set_bb_reg(dev, rOFDM0_TRxPathEnable, 0xf, 0x0);
	rtl92e_set_bb_reg(dev, rOFDM1_TRxPathEnable, 0xf, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x60, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x4, 0x0);
	rtl92e_writeb(dev, ANAPAR_FOR_8192PciE, 0x07);

}

static bool _rtl92e_set_rf_power_state(struct net_device *dev,
				       enum rt_rf_power_state eRFPowerState)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *pPSC = (struct rt_pwr_save_ctrl *)
					(&(priv->rtllib->PowerSaveControl));
	bool bResult = true;
	u8	i = 0, QueueID = 0;
	struct rtl8192_tx_ring  *ring = NULL;

	if (priv->SetRFPowerStateInProgress)
		return false;
	RT_TRACE(COMP_PS, "===========> _rtl92e_set_rf_power_state()!\n");
	priv->SetRFPowerStateInProgress = true;

	switch (priv->rf_chip) {
	case RF_8256:
		switch (eRFPowerState) {
		case eRfOn:
			RT_TRACE(COMP_PS,
				 "_rtl92e_set_rf_power_state() eRfOn!\n");
			if ((priv->rtllib->eRFPowerState == eRfOff) &&
			     RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC)) {
				bool rtstatus;
				u32 InitilizeCount = 3;

				do {
					InitilizeCount--;
					priv->RegRfOff = false;
					rtstatus = rtl92e_enable_nic(dev);
				} while (!rtstatus && (InitilizeCount > 0));

				if (!rtstatus) {
					netdev_err(dev,
						   "%s(): Failed to initialize Adapter.\n",
						   __func__);
					priv->SetRFPowerStateInProgress = false;
					return false;
				}

				RT_CLEAR_PS_LEVEL(pPSC,
						  RT_RF_OFF_LEVL_HALT_NIC);
			} else {
				rtl92e_writeb(dev, ANAPAR, 0x37);
				mdelay(1);
				rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1,
						 0x4, 0x1);
				priv->bHwRfOffAction = 0;

				rtl92e_set_bb_reg(dev, rFPGA0_XA_RFInterfaceOE,
						  BIT4, 0x1);
				rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4,
						  0x300, 0x3);
				rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1,
						  0x18, 0x3);
				rtl92e_set_bb_reg(dev, rOFDM0_TRxPathEnable,
						  0x3, 0x3);
				rtl92e_set_bb_reg(dev, rOFDM1_TRxPathEnable,
						  0x3, 0x3);
				rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1,
						  0x60, 0x3);

			}

			break;

		case eRfSleep:
			if (priv->rtllib->eRFPowerState == eRfOff)
				break;


			for (QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; ) {
				ring = &priv->tx_ring[QueueID];

				if (skb_queue_len(&ring->queue) == 0) {
					QueueID++;
					continue;
				} else {
					RT_TRACE((COMP_POWER|COMP_RF),
						 "eRf Off/Sleep: %d times TcbBusyQueue[%d] !=0 before doze!\n",
						 (i+1), QueueID);
					udelay(10);
					i++;
				}

				if (i >= MAX_DOZE_WAITING_TIMES_9x) {
					RT_TRACE(COMP_POWER,
						 "\n\n\n TimeOut!! _rtl92e_set_rf_power_state(): eRfOff: %d times TcbBusyQueue[%d] != 0 !!!\n",
						 MAX_DOZE_WAITING_TIMES_9x,
						 QueueID);
					break;
				}
			}
			rtl92e_set_rf_off(dev);
			break;

		case eRfOff:
			RT_TRACE(COMP_PS,
				 "_rtl92e_set_rf_power_state() eRfOff/Sleep !\n");

			for (QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; ) {
				ring = &priv->tx_ring[QueueID];

				if (skb_queue_len(&ring->queue) == 0) {
					QueueID++;
					continue;
				} else {
					RT_TRACE(COMP_POWER,
						 "eRf Off/Sleep: %d times TcbBusyQueue[%d] !=0 before doze!\n",
						 (i+1), QueueID);
					udelay(10);
					i++;
				}

				if (i >= MAX_DOZE_WAITING_TIMES_9x) {
					RT_TRACE(COMP_POWER,
						 "\n\n\n SetZebra: RFPowerState8185B(): eRfOff: %d times TcbBusyQueue[%d] != 0 !!!\n",
						 MAX_DOZE_WAITING_TIMES_9x,
						 QueueID);
					break;
				}
			}

			if (pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC &&
			    !RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC)) {
				rtl92e_disable_nic(dev);
				RT_SET_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
			} else if (!(pPSC->RegRfPsLevel &
				   RT_RF_OFF_LEVL_HALT_NIC)) {
				rtl92e_set_rf_off(dev);
			}

			break;

		default:
			bResult = false;
			netdev_warn(dev,
				    "%s(): Unknown state requested: 0x%X.\n",
				    __func__, eRFPowerState);
			break;
		}

		break;

	default:
		netdev_warn(dev, "%s(): Unknown RF type\n", __func__);
		break;
	}

	if (bResult) {
		priv->rtllib->eRFPowerState = eRFPowerState;

		switch (priv->rf_chip) {
		case RF_8256:
			break;

		default:
			netdev_warn(dev, "%s(): Unknown RF type\n", __func__);
			break;
		}
	}

	priv->SetRFPowerStateInProgress = false;
	RT_TRACE(COMP_PS,
		 "<=========== _rtl92e_set_rf_power_state() bResult = %d!\n",
		 bResult);
	return bResult;
}

bool rtl92e_set_rf_power_state(struct net_device *dev,
			       enum rt_rf_power_state eRFPowerState)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	bool bResult = false;

	RT_TRACE(COMP_PS,
		 "---------> rtl92e_set_rf_power_state(): eRFPowerState(%d)\n",
		 eRFPowerState);
	if (eRFPowerState == priv->rtllib->eRFPowerState &&
	    priv->bHwRfOffAction == 0) {
		RT_TRACE(COMP_PS,
			 "<--------- rtl92e_set_rf_power_state(): discard the request for eRFPowerState(%d) is the same.\n",
			 eRFPowerState);
		return bResult;
	}

	bResult = _rtl92e_set_rf_power_state(dev, eRFPowerState);

	RT_TRACE(COMP_PS,
		 "<--------- rtl92e_set_rf_power_state(): bResult(%d)\n",
		 bResult);

	return bResult;
}

void rtl92e_scan_op_backup(struct net_device *dev, u8 Operation)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->up) {
		switch (Operation) {
		case SCAN_OPT_BACKUP:
			priv->rtllib->InitialGainHandler(dev, IG_Backup);
			break;

		case SCAN_OPT_RESTORE:
			priv->rtllib->InitialGainHandler(dev, IG_Restore);
			break;

		default:
			RT_TRACE(COMP_SCAN, "Unknown Scan Backup Operation.\n");
			break;
		}
	}
}
