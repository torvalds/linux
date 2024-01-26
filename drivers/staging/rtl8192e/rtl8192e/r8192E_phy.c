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

#include "table.h"

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

void rtl92e_set_bb_reg(struct net_device *dev, u32 dwRegAddr, u32 dwBitMask,
		       u32 dwData)
{
	u32 OriginalValue, BitShift, NewValue;

	if (dwBitMask != bMaskDWord) {
		OriginalValue = rtl92e_readl(dev, dwRegAddr);
		BitShift = _rtl92e_calculate_bit_shift(dwBitMask);
		NewValue = (OriginalValue & ~dwBitMask) | (dwData << BitShift);
		rtl92e_writel(dev, dwRegAddr, NewValue);
	} else {
		rtl92e_writel(dev, dwRegAddr, dwData);
	}
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
	struct bb_reg_definition *pPhyReg = &priv->phy_reg_def[eRFPath];

	Offset &= 0x3f;

	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);
	if (Offset >= 31) {
		priv->rf_reg_0value[eRFPath] |= 0x140;
		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
				  bMaskDWord,
				  (priv->rf_reg_0value[eRFPath] << 16));
		NewOffset = Offset - 30;
	} else if (Offset >= 16) {
		priv->rf_reg_0value[eRFPath] |= 0x100;
		priv->rf_reg_0value[eRFPath] &= (~0x40);
		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
				  bMaskDWord,
				  (priv->rf_reg_0value[eRFPath] << 16));
		NewOffset = Offset - 15;
	} else {
		NewOffset = Offset;
	}
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2, bLSSIReadAddress,
			  NewOffset);
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x0);
	rtl92e_set_bb_reg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x1);

	mdelay(1);

	ret = rtl92e_get_bb_reg(dev, pPhyReg->rfLSSIReadBack,
				bLSSIReadBackData);

	priv->rf_reg_0value[eRFPath] &= 0xebf;

	rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset, bMaskDWord,
			  (priv->rf_reg_0value[eRFPath] << 16));

	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);

	return ret;
}

static void _rtl92e_phy_rf_write(struct net_device *dev,
				 enum rf90_radio_path eRFPath, u32 Offset,
				 u32 Data)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 DataAndAddr = 0, NewOffset = 0;
	struct bb_reg_definition *pPhyReg = &priv->phy_reg_def[eRFPath];

	Offset &= 0x3f;

	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0xf00, 0x0);

	if (Offset >= 31) {
		priv->rf_reg_0value[eRFPath] |= 0x140;
		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
				  bMaskDWord,
				  (priv->rf_reg_0value[eRFPath] << 16));
		NewOffset = Offset - 30;
	} else if (Offset >= 16) {
		priv->rf_reg_0value[eRFPath] |= 0x100;
		priv->rf_reg_0value[eRFPath] &= (~0x40);
		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
				  bMaskDWord,
				  (priv->rf_reg_0value[eRFPath] << 16));
		NewOffset = Offset - 15;
	} else {
		NewOffset = Offset;
	}

	DataAndAddr = (NewOffset & 0x3f) | (Data << 16);

	rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);

	if (Offset == 0x0)
		priv->rf_reg_0value[eRFPath] = Data;

	if (Offset != 0) {
		priv->rf_reg_0value[eRFPath] &= 0xebf;
		rtl92e_set_bb_reg(dev, pPhyReg->rf3wireOffset,
				  bMaskDWord,
				  (priv->rf_reg_0value[eRFPath] << 16));
	}
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);
}

void rtl92e_set_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		       u32 RegAddr, u32 BitMask, u32 Data)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 Original_Value, BitShift, New_Value;

	if (priv->rtllib->rf_power_state != rf_on && !priv->being_init_adapter)
		return;

	if (priv->rf_mode == RF_OP_By_FW) {
		if (BitMask != bMask12Bits) {
			Original_Value = _rtl92e_phy_rf_fw_read(dev, eRFPath,
								RegAddr);
			BitShift =  _rtl92e_calculate_bit_shift(BitMask);
			New_Value = (Original_Value & ~BitMask) | (Data << BitShift);

			_rtl92e_phy_rf_fw_write(dev, eRFPath, RegAddr,
						New_Value);
		} else {
			_rtl92e_phy_rf_fw_write(dev, eRFPath, RegAddr, Data);
		}
		udelay(200);
	} else {
		if (BitMask != bMask12Bits) {
			Original_Value = _rtl92e_phy_rf_read(dev, eRFPath,
							     RegAddr);
			BitShift =  _rtl92e_calculate_bit_shift(BitMask);
			New_Value = (Original_Value & ~BitMask) | (Data << BitShift);

			_rtl92e_phy_rf_write(dev, eRFPath, RegAddr, New_Value);
		} else {
			_rtl92e_phy_rf_write(dev, eRFPath, RegAddr, Data);
		}
	}
}

u32 rtl92e_get_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		      u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rtllib->rf_power_state != rf_on && !priv->being_init_adapter)
		return	0;
	mutex_lock(&priv->rf_mutex);
	if (priv->rf_mode == RF_OP_By_FW) {
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

	if (priv->tx_pwr_data_read_from_eeprom) {
		dwArrayLen = RTL8192E_MACPHY_ARR_PG_LEN;
		pdwArray = RTL8192E_MACPHY_ARR_PG;

	} else {
		dwArrayLen = RTL8192E_MACPHY_ARR_LEN;
		pdwArray = RTL8192E_MACPHY_ARR;
	}
	for (i = 0; i < dwArrayLen; i += 3) {
		if (pdwArray[i] == 0x318)
			pdwArray[i + 2] = 0x00000800;
		rtl92e_set_bb_reg(dev, pdwArray[i], pdwArray[i + 1],
				  pdwArray[i + 2]);
	}
}

static void _rtl92e_phy_config_bb(struct net_device *dev, u8 ConfigType)
{
	int i;
	u32 *Rtl819XPHY_REGArray_Table = NULL;
	u32 *Rtl819XAGCTAB_Array_Table = NULL;
	u16 AGCTAB_ArrayLen, PHY_REGArrayLen = 0;

	AGCTAB_ArrayLen = RTL8192E_AGCTAB_ARR_LEN;
	Rtl819XAGCTAB_Array_Table = RTL8192E_AGCTAB_ARR;
	PHY_REGArrayLen = RTL8192E_PHY_REG_1T2R_ARR_LEN;
	Rtl819XPHY_REGArray_Table = RTL8192E_PHY_REG_1T2R_ARR;

	if (ConfigType == BB_CONFIG_PHY_REG) {
		for (i = 0; i < PHY_REGArrayLen; i += 2) {
			rtl92e_set_bb_reg(dev, Rtl819XPHY_REGArray_Table[i],
					  bMaskDWord,
					  Rtl819XPHY_REGArray_Table[i + 1]);
		}
	} else if (ConfigType == BB_CONFIG_AGC_TAB) {
		for (i = 0; i < AGCTAB_ArrayLen; i += 2) {
			rtl92e_set_bb_reg(dev, Rtl819XAGCTAB_Array_Table[i],
					  bMaskDWord,
					  Rtl819XAGCTAB_Array_Table[i + 1]);
		}
	}
}

static void _rtl92e_init_bb_rf_reg_def(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->phy_reg_def[RF90_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	priv->phy_reg_def[RF90_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;

	priv->phy_reg_def[RF90_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	priv->phy_reg_def[RF90_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;

	priv->phy_reg_def[RF90_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	priv->phy_reg_def[RF90_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;

	priv->phy_reg_def[RF90_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter;
	priv->phy_reg_def[RF90_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	priv->phy_reg_def[RF90_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;
	priv->phy_reg_def[RF90_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;

	priv->phy_reg_def[RF90_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	priv->phy_reg_def[RF90_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
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
	rtl92e_writeb(dev, BB_GLOBAL_RESET, (bRegValue | BB_GLOBAL_RESET_BIT));

	dwRegValue = rtl92e_readl(dev, CPU_GEN);
	rtl92e_writel(dev, CPU_GEN, (dwRegValue & (~CPU_GEN_BB_RST)));

	for (eCheckItem = (enum hw90_block)HW90_BLOCK_PHY0;
	     eCheckItem <= HW90_BLOCK_PHY1; eCheckItem++) {
		rtStatus  = rtl92e_check_bb_and_rf(dev,
						   (enum hw90_block)eCheckItem,
						   (enum rf90_radio_path)0);
		if (!rtStatus)
			return rtStatus;
	}
	rtl92e_set_bb_reg(dev, rFPGA0_RFMOD, bCCKEn | bOFDMEn, 0x0);
	_rtl92e_phy_config_bb(dev, BB_CONFIG_PHY_REG);

	dwRegValue = rtl92e_readl(dev, CPU_GEN);
	rtl92e_writel(dev, CPU_GEN, (dwRegValue | CPU_GEN_BB_RST));

	_rtl92e_phy_config_bb(dev, BB_CONFIG_AGC_TAB);

	if (priv->ic_cut  > VERSION_8190_BD) {
		dwRegValue = 0x0;
		rtl92e_set_bb_reg(dev, rFPGA0_TxGainStage,
				  (bXBTxAGC | bXCTxAGC | bXDTxAGC), dwRegValue);

		dwRegValue = priv->crystal_cap;
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

	priv->mcs_tx_pwr_level_org_offset[0] =
		rtl92e_readl(dev, rTxAGC_Rate18_06);
	priv->mcs_tx_pwr_level_org_offset[1] =
		rtl92e_readl(dev, rTxAGC_Rate54_24);
	priv->mcs_tx_pwr_level_org_offset[2] =
		rtl92e_readl(dev, rTxAGC_Mcs03_Mcs00);
	priv->mcs_tx_pwr_level_org_offset[3] =
		rtl92e_readl(dev, rTxAGC_Mcs07_Mcs04);
	priv->mcs_tx_pwr_level_org_offset[4] =
		rtl92e_readl(dev, rTxAGC_Mcs11_Mcs08);
	priv->mcs_tx_pwr_level_org_offset[5] =
		rtl92e_readl(dev, rTxAGC_Mcs15_Mcs12);

	priv->def_initial_gain[0] = rtl92e_readb(dev, rOFDM0_XAAGCCore1);
	priv->def_initial_gain[1] = rtl92e_readb(dev, rOFDM0_XBAGCCore1);
	priv->def_initial_gain[2] = rtl92e_readb(dev, rOFDM0_XCAGCCore1);
	priv->def_initial_gain[3] = rtl92e_readb(dev, rOFDM0_XDAGCCore1);

	priv->framesync = rtl92e_readb(dev, rOFDM0_RxDetector3);
}

void rtl92e_set_tx_power(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8	powerlevel = 0, powerlevelOFDM24G = 0;

	if (priv->epromtype == EEPROM_93C46) {
		powerlevel = priv->tx_pwr_level_cck[channel - 1];
		powerlevelOFDM24G = priv->tx_pwr_level_ofdm_24g[channel - 1];
	}

	rtl92e_set_cck_tx_power(dev, powerlevel);
	rtl92e_set_ofdm_tx_power(dev, powerlevelOFDM24G);
}

u8 rtl92e_config_rf_path(struct net_device *dev, enum rf90_radio_path eRFPath)
{
	int i;

	switch (eRFPath) {
	case RF90_PATH_A:
		for (i = 0; i < RTL8192E_RADIO_A_ARR_LEN; i += 2) {
			if (RTL8192E_RADIO_A_ARR[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, RTL8192E_RADIO_A_ARR[i],
					  bMask12Bits,
					  RTL8192E_RADIO_A_ARR[i + 1]);
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < RTL8192E_RADIO_B_ARR_LEN; i += 2) {
			if (RTL8192E_RADIO_B_ARR[i] == 0xfe) {
				msleep(100);
				continue;
			}
			rtl92e_set_rf_reg(dev, eRFPath, RTL8192E_RADIO_B_ARR[i],
					  bMask12Bits,
					  RTL8192E_RADIO_B_ARR[i + 1]);
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
	u8	powerlevel = priv->tx_pwr_level_cck[channel - 1];
	u8	powerlevelOFDM24G = priv->tx_pwr_level_ofdm_24g[channel - 1];

	rtl92e_set_cck_tx_power(dev, powerlevel);
	rtl92e_set_ofdm_tx_power(dev, powerlevelOFDM24G);
}

static u8 _rtl92e_phy_set_sw_chnl_cmd_array(struct net_device *dev,
					    struct sw_chnl_cmd *CmdTable,
					    u32 CmdTableIdx, u32 CmdTableSz,
					    enum sw_chnl_cmd_id CmdID,
					    u32 Para1, u32 Para2, u32 msDelay)
{
	struct sw_chnl_cmd *pCmd;

	if (!CmdTable) {
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
				if (priv->ic_cut > VERSION_8190_BD)
					_rtl92e_set_tx_power_level(dev,
								   channel);
				break;
			case CmdID_WritePortUlong:
				rtl92e_writel(dev, CurrentCmd->Para1,
					      CurrentCmd->Para2);
				break;
			case CmdID_WritePortUshort:
				rtl92e_writew(dev, CurrentCmd->Para1,
					      CurrentCmd->Para2);
				break;
			case CmdID_WritePortUchar:
				rtl92e_writeb(dev, CurrentCmd->Para1,
					      CurrentCmd->Para2);
				break;
			case CmdID_RF_WriteReg:
				for (eRFPath = 0; eRFPath <
				     priv->num_total_rf_path; eRFPath++)
					rtl92e_set_rf_reg(dev,
						 (enum rf90_radio_path)eRFPath,
						 CurrentCmd->Para1, bMask12Bits,
						 CurrentCmd->Para2 << 7);
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
						&priv->sw_chnl_stage,
						&priv->sw_chnl_step, &delay)) {
		if (delay > 0)
			msleep(delay);
		if (!priv->up)
			break;
	}
}

static void _rtl92e_phy_switch_channel_work_item(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	_rtl92e_phy_switch_channel(dev, priv->chan);
}

void rtl92e_set_channel(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (!priv->up) {
		netdev_err(dev, "%s(): Driver is not initialized\n", __func__);
		return;
	}
	if (priv->sw_chnl_in_progress)
		return;

	switch (priv->rtllib->mode) {
	case WIRELESS_MODE_B:
		if (channel > 14) {
			netdev_warn(dev,
				    "Channel %d not available in 802.11b.\n",
				    channel);
			return;
		}
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		if (channel > 14) {
			netdev_warn(dev,
				    "Channel %d not available in 802.11g.\n",
				    channel);
			return;
		}
		break;
	}

	priv->sw_chnl_in_progress = true;
	if (channel == 0)
		channel = 1;

	priv->chan = channel;

	priv->sw_chnl_stage = 0;
	priv->sw_chnl_step = 0;

	if (priv->up)
		_rtl92e_phy_switch_channel_work_item(dev);
	priv->sw_chnl_in_progress = false;
	return;
}

static void _rtl92e_cck_tx_power_track_bw_switch_tssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	switch (priv->current_chnl_bw) {
	case HT_CHANNEL_WIDTH_20:
		priv->cck_present_attn =
			priv->cck_present_attn_20m_def +
			    priv->cck_present_attn_diff;

		if (priv->cck_present_attn >
		    (CCK_TX_BB_GAIN_TABLE_LEN - 1))
			priv->cck_present_attn =
					 CCK_TX_BB_GAIN_TABLE_LEN - 1;
		if (priv->cck_present_attn < 0)
			priv->cck_present_attn = 0;

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
		priv->cck_present_attn =
			priv->cck_present_attn_40m_def +
			priv->cck_present_attn_diff;

		if (priv->cck_present_attn >
		    (CCK_TX_BB_GAIN_TABLE_LEN - 1))
			priv->cck_present_attn =
					 CCK_TX_BB_GAIN_TABLE_LEN - 1;
		if (priv->cck_present_attn < 0)
			priv->cck_present_attn = 0;

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

	switch (priv->current_chnl_bw) {
	case HT_CHANNEL_WIDTH_20:
		if (priv->rec_cck_20m_idx == 0)
			priv->rec_cck_20m_idx = 6;
		priv->cck_index = priv->rec_cck_20m_idx;
	break;

	case HT_CHANNEL_WIDTH_20_40:
		priv->cck_index = priv->rec_cck_40m_idx;
	break;
	}
	rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
}

static void _rtl92e_cck_tx_power_track_bw_switch(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->ic_cut >= IC_VersionCut_D)
		_rtl92e_cck_tx_power_track_bw_switch_tssi(dev);
	else
		_rtl92e_cck_tx_power_track_bw_switch_thermal(dev);
}

static void _rtl92e_set_bw_mode_work_item(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 regBwOpMode;

	if (!priv->up) {
		netdev_err(dev, "%s(): Driver is not initialized\n", __func__);
		return;
	}
	regBwOpMode = rtl92e_readb(dev, BW_OPMODE);

	switch (priv->current_chnl_bw) {
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
			   priv->current_chnl_bw);
		break;
	}

	switch (priv->current_chnl_bw) {
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
				  (priv->n_cur_40mhz_prime_sc >> 1));
		rtl92e_set_bb_reg(dev, rOFDM1_LSTF, 0xC00,
				  priv->n_cur_40mhz_prime_sc);

		rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x00100000, 0);
		break;
	default:
		netdev_err(dev, "%s(): unknown Bandwidth: %#X\n", __func__,
			   priv->current_chnl_bw);
		break;
	}

	rtl92e_set_bandwidth(dev, priv->current_chnl_bw);

	atomic_dec(&(priv->rtllib->atm_swbw));
	priv->set_bw_mode_in_progress = false;
}

void rtl92e_set_bw_mode(struct net_device *dev, enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->set_bw_mode_in_progress)
		return;

	atomic_inc(&(priv->rtllib->atm_swbw));
	priv->set_bw_mode_in_progress = true;

	priv->current_chnl_bw = bandwidth;

	if (Offset == HT_EXTCHNL_OFFSET_LOWER)
		priv->n_cur_40mhz_prime_sc = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (Offset == HT_EXTCHNL_OFFSET_UPPER)
		priv->n_cur_40mhz_prime_sc = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		priv->n_cur_40mhz_prime_sc = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

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
			initial_gain = SCAN_RX_INITIAL_GAIN;
			BitMask = bMaskByte0;
			priv->initgain_backup.xaagccore1 =
				 rtl92e_get_bb_reg(dev, rOFDM0_XAAGCCore1,
						   BitMask);
			priv->initgain_backup.xbagccore1 =
				 rtl92e_get_bb_reg(dev, rOFDM0_XBAGCCore1,
						   BitMask);
			priv->initgain_backup.xcagccore1 =
				 rtl92e_get_bb_reg(dev, rOFDM0_XCAGCCore1,
						   BitMask);
			priv->initgain_backup.xdagccore1 =
				 rtl92e_get_bb_reg(dev, rOFDM0_XDAGCCore1,
						   BitMask);
			BitMask = bMaskByte2;
			priv->initgain_backup.cca = (u8)rtl92e_get_bb_reg(dev,
						    rCCK0_CCA, BitMask);

			rtl92e_writeb(dev, rOFDM0_XAAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XBAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XCAGCCore1, initial_gain);
			rtl92e_writeb(dev, rOFDM0_XDAGCCore1, initial_gain);
			rtl92e_writeb(dev, 0xa0a, POWER_DETECTION_TH);
			break;
		case IG_Restore:
			BitMask = 0x7f;
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

			rtl92e_set_tx_power(dev,
					 priv->rtllib->current_network.channel);
			break;
		}
	}
}

void rtl92e_set_rf_off(struct net_device *dev)
{
	rtl92e_set_bb_reg(dev, rFPGA0_XA_RFInterfaceOE, BIT(4), 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter4, 0x300, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x18, 0x0);
	rtl92e_set_bb_reg(dev, rOFDM0_TRxPathEnable, 0xf, 0x0);
	rtl92e_set_bb_reg(dev, rOFDM1_TRxPathEnable, 0xf, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x60, 0x0);
	rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1, 0x4, 0x0);
	rtl92e_writeb(dev, ANAPAR_FOR_8192PCIE, 0x07);
}

static bool _rtl92e_set_rf_power_state(struct net_device *dev,
				       enum rt_rf_power_state rf_power_state)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_pwr_save_ctrl *psc = (struct rt_pwr_save_ctrl *)
					(&priv->rtllib->pwr_save_ctrl);
	bool bResult = true;
	u8	i = 0, QueueID = 0;
	struct rtl8192_tx_ring  *ring = NULL;

	if (priv->set_rf_pwr_state_in_progress)
		return false;
	priv->set_rf_pwr_state_in_progress = true;

	switch (rf_power_state) {
	case rf_on:
		if ((priv->rtllib->rf_power_state == rf_off) &&
		     RT_IN_PS_LEVEL(psc, RT_RF_OFF_LEVL_HALT_NIC)) {
			bool rtstatus;
			u32 InitilizeCount = 3;

			do {
				InitilizeCount--;
				rtstatus = rtl92e_enable_nic(dev);
			} while (!rtstatus && (InitilizeCount > 0));
			if (!rtstatus) {
				netdev_err(dev,
					   "%s(): Failed to initialize Adapter.\n",
					   __func__);
				priv->set_rf_pwr_state_in_progress = false;
				return false;
			}
			RT_CLEAR_PS_LEVEL(psc,
					  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			rtl92e_writeb(dev, ANAPAR, 0x37);
			mdelay(1);
			rtl92e_set_bb_reg(dev, rFPGA0_AnalogParameter1,
					 0x4, 0x1);
			priv->hw_rf_off_action = 0;
			rtl92e_set_bb_reg(dev, rFPGA0_XA_RFInterfaceOE,
					  BIT(4), 0x1);
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
	case rf_sleep:
		if (priv->rtllib->rf_power_state == rf_off)
			break;
		for (QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; ) {
			ring = &priv->tx_ring[QueueID];
			if (skb_queue_len(&ring->queue) == 0) {
				QueueID++;
				continue;
			} else {
				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x)
				break;
		}
		rtl92e_set_rf_off(dev);
		break;
	case rf_off:
		for (QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; ) {
			ring = &priv->tx_ring[QueueID];
			if (skb_queue_len(&ring->queue) == 0) {
				QueueID++;
				continue;
			} else {
				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x)
				break;
		}
		rtl92e_set_rf_off(dev);
		break;
	default:
		bResult = false;
		netdev_warn(dev,
			    "%s(): Unknown state requested: 0x%X.\n",
			    __func__, rf_power_state);
		break;
	}

	if (bResult)
		priv->rtllib->rf_power_state = rf_power_state;

	priv->set_rf_pwr_state_in_progress = false;
	return bResult;
}

bool rtl92e_set_rf_power_state(struct net_device *dev,
			       enum rt_rf_power_state rf_power_state)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	bool bResult = false;

	if (rf_power_state == priv->rtllib->rf_power_state &&
	    priv->hw_rf_off_action == 0) {
		return bResult;
	}

	bResult = _rtl92e_set_rf_power_state(dev, rf_power_state);
	return bResult;
}

void rtl92e_scan_op_backup(struct net_device *dev, u8 Operation)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->up) {
		switch (Operation) {
		case SCAN_OPT_BACKUP:
			priv->rtllib->init_gain_handler(dev, IG_Backup);
			break;

		case SCAN_OPT_RESTORE:
			priv->rtllib->init_gain_handler(dev, IG_Restore);
			break;
		}
	}
}
