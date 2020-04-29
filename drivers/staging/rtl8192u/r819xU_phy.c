// SPDX-License-Identifier: GPL-2.0
#include "r8192U.h"
#include "r8192U_hw.h"
#include "r819xU_phy.h"
#include "r819xU_phyreg.h"
#include "r8190_rtl8256.h"
#include "r8192U_dm.h"
#include "r819xU_firmware_img.h"

#include "ieee80211/dot11d.h"
#include <linux/bitops.h>

static u32 RF_CHANNEL_TABLE_ZEBRA[] = {
	0,
	0x085c, /* 2412 1  */
	0x08dc, /* 2417 2  */
	0x095c, /* 2422 3  */
	0x09dc, /* 2427 4  */
	0x0a5c, /* 2432 5  */
	0x0adc, /* 2437 6  */
	0x0b5c, /* 2442 7  */
	0x0bdc, /* 2447 8  */
	0x0c5c, /* 2452 9  */
	0x0cdc, /* 2457 10 */
	0x0d5c, /* 2462 11 */
	0x0ddc, /* 2467 12 */
	0x0e5c, /* 2472 13 */
	0x0f72, /* 2484    */
};

#define rtl819XMACPHY_Array Rtl8192UsbMACPHY_Array

/******************************************************************************
 * function:  This function checks different RF type to execute legal judgement.
 *            If RF Path is illegal, we will return false.
 * input:     net_device	 *dev
 *            u32		 e_rfpath
 * output:    none
 * return:    0(illegal, false), 1(legal, true)
 *****************************************************************************/
u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device *dev, u32 e_rfpath)
{
	u8 ret = 1;
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (priv->rf_type == RF_2T4R) {
		ret = 0;
	} else if (priv->rf_type == RF_1T2R) {
		if (e_rfpath == RF90_PATH_A || e_rfpath == RF90_PATH_B)
			ret = 1;
		else if (e_rfpath == RF90_PATH_C || e_rfpath == RF90_PATH_D)
			ret = 0;
	}
	return ret;
}

/******************************************************************************
 * function:  This function sets specific bits to BB register
 * input:     net_device *dev
 *            u32        reg_addr   //target addr to be modified
 *            u32        bitmask    //taget bit pos to be modified
 *            u32        data       //value to be write
 * output:    none
 * return:    none
 * notice:
 ******************************************************************************/
void rtl8192_setBBreg(struct net_device *dev, u32 reg_addr, u32 bitmask,
		      u32 data)
{

	u32 reg, bitshift;

	if (bitmask != bMaskDWord) {
		read_nic_dword(dev, reg_addr, &reg);
		bitshift = ffs(bitmask) - 1;
		reg &= ~bitmask;
		reg |= data << bitshift;
		write_nic_dword(dev, reg_addr, reg);
	} else {
		write_nic_dword(dev, reg_addr, data);
	}
}

/******************************************************************************
 * function:  This function reads specific bits from BB register
 * input:     net_device	*dev
 *            u32		reg_addr   //target addr to be readback
 *            u32		bitmask    //taget bit pos to be readback
 * output:    none
 * return:    u32		data       //the readback register value
 * notice:
 ******************************************************************************/
u32 rtl8192_QueryBBReg(struct net_device *dev, u32 reg_addr, u32 bitmask)
{
	u32 reg, bitshift;

	read_nic_dword(dev, reg_addr, &reg);
	bitshift = ffs(bitmask) - 1;

	return (reg & bitmask) >> bitshift;
}

static u32 phy_FwRFSerialRead(struct net_device *dev,
			      enum rf90_radio_path_e e_rfpath,
			      u32 offset);

static void phy_FwRFSerialWrite(struct net_device *dev,
				enum rf90_radio_path_e e_rfpath,
				u32  offset,
				u32  data);

/******************************************************************************
 * function:  This function reads register from RF chip
 * input:     net_device        *dev
 *            rf90_radio_path_e e_rfpath    //radio path of A/B/C/D
 *            u32               offset     //target address to be read
 * output:    none
 * return:    u32               readback value
 * notice:    There are three types of serial operations:
 *            (1) Software serial write.
 *            (2)Hardware LSSI-Low Speed Serial Interface.
 *            (3)Hardware HSSI-High speed serial write.
 *            Driver here need to implement (1) and (2)
 *            ---need more spec for this information.
 ******************************************************************************/
static u32 rtl8192_phy_RFSerialRead(struct net_device *dev,
				    enum rf90_radio_path_e e_rfpath, u32 offset)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 ret = 0;
	u32 new_offset = 0;
	BB_REGISTER_DEFINITION_T *pPhyReg = &priv->PHYRegDef[e_rfpath];

	rtl8192_setBBreg(dev, pPhyReg->rfLSSIReadBack, bLSSIReadBackData, 0);
	/* Make sure RF register offset is correct */
	offset &= 0x3f;

	/* Switch page for 8256 RF IC */
	if (priv->rf_chip == RF_8256) {
		if (offset >= 31) {
			priv->RfReg0Value[e_rfpath] |= 0x140;
			/* Switch to Reg_Mode2 for Reg 31-45 */
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset,
					 bMaskDWord,
					 priv->RfReg0Value[e_rfpath]<<16);
			/* Modify offset */
			new_offset = offset - 30;
		} else if (offset >= 16) {
			priv->RfReg0Value[e_rfpath] |= 0x100;
			priv->RfReg0Value[e_rfpath] &= (~0x40);
			/* Switch to Reg_Mode1 for Reg16-30 */
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset,
					 bMaskDWord,
					 priv->RfReg0Value[e_rfpath]<<16);

			new_offset = offset - 15;
		} else {
			new_offset = offset;
		}
	} else {
		RT_TRACE((COMP_PHY|COMP_ERR),
			 "check RF type here, need to be 8256\n");
		new_offset = offset;
	}
	/* Put desired read addr to LSSI control Register */
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, bLSSIReadAddress,
			 new_offset);
	/* Issue a posedge trigger */
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x0);
	rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2,  bLSSIReadEdge, 0x1);


	/* TODO: we should not delay such a long time. Ask for help from SD3 */
	usleep_range(1000, 1000);

	ret = rtl8192_QueryBBReg(dev, pPhyReg->rfLSSIReadBack,
				 bLSSIReadBackData);


	/* Switch back to Reg_Mode0 */
	if (priv->rf_chip == RF_8256) {
		priv->RfReg0Value[e_rfpath] &= 0xebf;

		rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord,
				 priv->RfReg0Value[e_rfpath] << 16);
	}

	return ret;
}

/******************************************************************************
 * function:  This function writes data to RF register
 * input:     net_device        *dev
 *            rf90_radio_path_e e_rfpath  //radio path of A/B/C/D
 *            u32               offset   //target address to be written
 *            u32               data	 //the new register data to be written
 * output:    none
 * return:    none
 * notice:    For RF8256 only.
 * ===========================================================================
 * Reg Mode	RegCTL[1]	RegCTL[0]		Note
 *		(Reg00[12])	(Reg00[10])
 * ===========================================================================
 * Reg_Mode0	0		x			Reg 0 ~ 15(0x0 ~ 0xf)
 * ---------------------------------------------------------------------------
 * Reg_Mode1	1		0			Reg 16 ~ 30(0x1 ~ 0xf)
 * ---------------------------------------------------------------------------
 * Reg_Mode2	1		1			Reg 31 ~ 45(0x1 ~ 0xf)
 * ---------------------------------------------------------------------------
 *****************************************************************************/
static void rtl8192_phy_RFSerialWrite(struct net_device *dev,
				      enum rf90_radio_path_e e_rfpath,
				      u32 offset,
				      u32 data)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 DataAndAddr = 0, new_offset = 0;
	BB_REGISTER_DEFINITION_T	*pPhyReg = &priv->PHYRegDef[e_rfpath];

	offset &= 0x3f;
	if (priv->rf_chip == RF_8256) {

		if (offset >= 31) {
			priv->RfReg0Value[e_rfpath] |= 0x140;
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset,
					 bMaskDWord,
					 priv->RfReg0Value[e_rfpath] << 16);
			new_offset = offset - 30;
		} else if (offset >= 16) {
			priv->RfReg0Value[e_rfpath] |= 0x100;
			priv->RfReg0Value[e_rfpath] &= (~0x40);
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset,
					 bMaskDWord,
					 priv->RfReg0Value[e_rfpath]<<16);
			new_offset = offset - 15;
		} else {
			new_offset = offset;
		}
	} else {
		RT_TRACE((COMP_PHY|COMP_ERR),
			 "check RF type here, need to be 8256\n");
		new_offset = offset;
	}

	/* Put write addr in [5:0] and write data in [31:16] */
	DataAndAddr = (data<<16) | (new_offset&0x3f);

	/* Write operation */
	rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);


	if (offset == 0x0)
		priv->RfReg0Value[e_rfpath] = data;

	/* Switch back to Reg_Mode0 */
	if (priv->rf_chip == RF_8256) {
		if (offset != 0) {
			priv->RfReg0Value[e_rfpath] &= 0xebf;
			rtl8192_setBBreg(dev, pPhyReg->rf3wireOffset,
					 bMaskDWord,
					 priv->RfReg0Value[e_rfpath] << 16);
		}
	}
}

/******************************************************************************
 * function:  This function set specific bits to RF register
 * input:     net_device        dev
 *            rf90_radio_path_e e_rfpath  //radio path of A/B/C/D
 *            u32               reg_addr //target addr to be modified
 *            u32               bitmask  //taget bit pos to be modified
 *            u32               data     //value to be written
 * output:    none
 * return:    none
 * notice:
 *****************************************************************************/
void rtl8192_phy_SetRFReg(struct net_device *dev,
			  enum rf90_radio_path_e e_rfpath,
			  u32 reg_addr, u32 bitmask, u32 data)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 reg, bitshift;

	if (!rtl8192_phy_CheckIsLegalRFPath(dev, e_rfpath))
		return;

	if (priv->Rf_Mode == RF_OP_By_FW) {
		if (bitmask != bMask12Bits) {
			/* RF data is 12 bits only */
			reg = phy_FwRFSerialRead(dev, e_rfpath, reg_addr);
			bitshift =  ffs(bitmask) - 1;
			reg &= ~bitmask;
			reg |= data << bitshift;

			phy_FwRFSerialWrite(dev, e_rfpath, reg_addr, reg);
		} else {
			phy_FwRFSerialWrite(dev, e_rfpath, reg_addr, data);
		}

		udelay(200);

	} else {
		if (bitmask != bMask12Bits) {
			/* RF data is 12 bits only */
			reg = rtl8192_phy_RFSerialRead(dev, e_rfpath, reg_addr);
			bitshift =  ffs(bitmask) - 1;
			reg &= ~bitmask;
			reg |= data << bitshift;

			rtl8192_phy_RFSerialWrite(dev, e_rfpath, reg_addr, reg);
		} else {
			rtl8192_phy_RFSerialWrite(dev, e_rfpath, reg_addr, data);
		}
	}
}

/******************************************************************************
 * function:  This function reads specific bits from RF register
 * input:     net_device        *dev
 *            u32               reg_addr //target addr to be readback
 *            u32               bitmask  //taget bit pos to be readback
 * output:    none
 * return:    u32               data     //the readback register value
 * notice:
 *****************************************************************************/
u32 rtl8192_phy_QueryRFReg(struct net_device *dev,
			   enum rf90_radio_path_e e_rfpath,
			   u32 reg_addr, u32 bitmask)
{
	u32 reg, bitshift;
	struct r8192_priv *priv = ieee80211_priv(dev);


	if (!rtl8192_phy_CheckIsLegalRFPath(dev, e_rfpath))
		return 0;
	if (priv->Rf_Mode == RF_OP_By_FW) {
		reg = phy_FwRFSerialRead(dev, e_rfpath, reg_addr);
		udelay(200);
	} else {
		reg = rtl8192_phy_RFSerialRead(dev, e_rfpath, reg_addr);
	}
	bitshift =  ffs(bitmask) - 1;
	reg = (reg & bitmask) >> bitshift;
	return reg;

}

/******************************************************************************
 * function:  We support firmware to execute RF-R/W.
 * input:     net_device        *dev
 *            rf90_radio_path_e e_rfpath
 *            u32               offset
 * output:    none
 * return:    u32
 * notice:
 ****************************************************************************/
static u32 phy_FwRFSerialRead(struct net_device *dev,
			      enum rf90_radio_path_e e_rfpath,
			      u32 offset)
{
	u32		reg = 0;
	u32		data = 0;
	u8		time = 0;
	u32		tmp;

	/* Firmware RF Write control.
	 * We can not execute the scheme in the initial step.
	 * Otherwise, RF-R/W will waste much time.
	 * This is only for site survey.
	 */
	/* 1. Read operation need not insert data. bit 0-11 */
	/* 2. Write RF register address. bit 12-19 */
	data |= ((offset&0xFF)<<12);
	/* 3. Write RF path.  bit 20-21 */
	data |= ((e_rfpath&0x3)<<20);
	/* 4. Set RF read indicator. bit 22=0 */
	/* 5. Trigger Fw to operate the command. bit 31 */
	data |= 0x80000000;
	/* 6. We can not execute read operation if bit 31 is 1. */
	read_nic_dword(dev, QPNR, &tmp);
	while (tmp & 0x80000000) {
		/* If FW can not finish RF-R/W for more than ?? times.
		 * We must reset FW.
		 */
		if (time++ < 100) {
			udelay(10);
			read_nic_dword(dev, QPNR, &tmp);
		} else {
			break;
		}
	}
	/* 7. Execute read operation. */
	write_nic_dword(dev, QPNR, data);
	/* 8. Check if firmware send back RF content. */
	read_nic_dword(dev, QPNR, &tmp);
	while (tmp & 0x80000000) {
		/* If FW can not finish RF-R/W for more than ?? times.
		 * We must reset FW.
		 */
		if (time++ < 100) {
			udelay(10);
			read_nic_dword(dev, QPNR, &tmp);
		} else {
			return 0;
		}
	}
	read_nic_dword(dev, RF_DATA, &reg);

	return reg;
}

/******************************************************************************
 * function:  We support firmware to execute RF-R/W.
 * input:     net_device        *dev
 *            rf90_radio_path_e e_rfpath
 *            u32               offset
 *            u32               data
 * output:    none
 * return:    none
 * notice:
 ****************************************************************************/
static void phy_FwRFSerialWrite(struct net_device *dev,
				enum rf90_radio_path_e e_rfpath,
				u32 offset, u32 data)
{
	u8	time = 0;
	u32	tmp;

	/* Firmware RF Write control.
	 * We can not execute the scheme in the initial step.
	 * Otherwise, RF-R/W will waste much time.
	 * This is only for site survey.
	 */

	/* 1. Set driver write bit and 12 bit data. bit 0-11 */
	/* 2. Write RF register address. bit 12-19 */
	data |= ((offset&0xFF)<<12);
	/* 3. Write RF path.  bit 20-21 */
	data |= ((e_rfpath&0x3)<<20);
	/* 4. Set RF write indicator. bit 22=1 */
	data |= 0x400000;
	/* 5. Trigger Fw to operate the command. bit 31=1 */
	data |= 0x80000000;

	/* 6. Write operation. We can not write if bit 31 is 1. */
	read_nic_dword(dev, QPNR, &tmp);
	while (tmp & 0x80000000) {
		/* If FW can not finish RF-R/W for more than ?? times.
		 * We must reset FW.
		 */
		if (time++ < 100) {
			udelay(10);
			read_nic_dword(dev, QPNR, &tmp);
		} else {
			break;
		}
	}
	/* 7. No matter check bit. We always force the write.
	 * Because FW will not accept the command.
	 */
	write_nic_dword(dev, QPNR, data);
	/* According to test, we must delay 20us to wait firmware
	 * to finish RF write operation.
	 */
	/* We support delay in firmware side now. */
}

/******************************************************************************
 * function:  This function reads BB parameters from header file we generate,
 *            and do register read/write
 * input:     net_device	*dev
 * output:    none
 * return:    none
 * notice:    BB parameters may change all the time, so please make
 *            sure it has been synced with the newest.
 *****************************************************************************/
void rtl8192_phy_configmac(struct net_device *dev)
{
	u32 dwArrayLen = 0, i;
	u32 *pdwArray = NULL;
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (priv->btxpowerdata_readfromEEPORM) {
		RT_TRACE(COMP_PHY, "Rtl819XMACPHY_Array_PG\n");
		dwArrayLen = MACPHY_Array_PGLength;
		pdwArray = Rtl8192UsbMACPHY_Array_PG;

	} else {
		RT_TRACE(COMP_PHY, "Rtl819XMACPHY_Array\n");
		dwArrayLen = MACPHY_ArrayLength;
		pdwArray = rtl819XMACPHY_Array;
	}
	for (i = 0; i < dwArrayLen; i = i+3) {
		if (pdwArray[i] == 0x318)
			pdwArray[i+2] = 0x00000800;

		RT_TRACE(COMP_DBG,
			 "Rtl8190MACPHY_Array[0]=%x Rtl8190MACPHY_Array[1]=%x Rtl8190MACPHY_Array[2]=%x\n",
			 pdwArray[i], pdwArray[i+1], pdwArray[i+2]);
		rtl8192_setBBreg(dev, pdwArray[i], pdwArray[i+1],
				 pdwArray[i+2]);
	}
}

/******************************************************************************
 * function:  This function does dirty work
 * input:     net_device	*dev
 *            u8                ConfigType
 * output:    none
 * return:    none
 * notice:    BB parameters may change all the time, so please make
 *            sure it has been synced with the newest.
 *****************************************************************************/
static void rtl8192_phyConfigBB(struct net_device *dev,
				enum baseband_config_type ConfigType)
{
	u32 i;

	if (ConfigType == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < PHY_REG_1T2RArrayLength; i += 2) {
			rtl8192_setBBreg(dev, Rtl8192UsbPHY_REG_1T2RArray[i],
					 bMaskDWord,
					 Rtl8192UsbPHY_REG_1T2RArray[i+1]);
			RT_TRACE(COMP_DBG,
				 "i: %x, Rtl819xUsbPHY_REGArray[0]=%x Rtl819xUsbPHY_REGArray[1]=%x\n",
				 i, Rtl8192UsbPHY_REG_1T2RArray[i],
				 Rtl8192UsbPHY_REG_1T2RArray[i+1]);
		}
	} else if (ConfigType == BASEBAND_CONFIG_AGC_TAB) {
		for (i = 0; i < AGCTAB_ArrayLength; i += 2) {
			rtl8192_setBBreg(dev, Rtl8192UsbAGCTAB_Array[i],
					 bMaskDWord, Rtl8192UsbAGCTAB_Array[i+1]);
			RT_TRACE(COMP_DBG,
				 "i: %x, Rtl8192UsbAGCTAB_Array[0]=%x Rtl8192UsbAGCTAB_Array[1]=%x\n",
				 i, Rtl8192UsbAGCTAB_Array[i],
				 Rtl8192UsbAGCTAB_Array[i+1]);
		}
	}
}

/******************************************************************************
 * function:  This function initializes Register definition offset for
 *            Radio Path A/B/C/D
 * input:     net_device	*dev
 * output:    none
 * return:    none
 * notice:    Initialization value here is constant and it should never
 *            be changed
 *****************************************************************************/
static void rtl8192_InitBBRFRegDef(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	/* RF Interface Software Control */
	/* 16 LSBs if read 32-bit from 0x870 */
	priv->PHYRegDef[RF90_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	/* 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	priv->PHYRegDef[RF90_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	/* 16 LSBs if read 32-bit from 0x874 */
	priv->PHYRegDef[RF90_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;
	/* 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876) */
	priv->PHYRegDef[RF90_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;

	/* RF Interface Readback Value */
	/* 16 LSBs if read 32-bit from 0x8E0 */
	priv->PHYRegDef[RF90_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB;
	/* 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2) */
	priv->PHYRegDef[RF90_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;
	/* 16 LSBs if read 32-bit from 0x8E4 */
	priv->PHYRegDef[RF90_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;
	/* 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6) */
	priv->PHYRegDef[RF90_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;

	/* RF Interface Output (and Enable) */
	/* 16 LSBs if read 32-bit from 0x860 */
	priv->PHYRegDef[RF90_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	/* 16 LSBs if read 32-bit from 0x864 */
	priv->PHYRegDef[RF90_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;
	/* 16 LSBs if read 32-bit from 0x868 */
	priv->PHYRegDef[RF90_PATH_C].rfintfo = rFPGA0_XC_RFInterfaceOE;
	/* 16 LSBs if read 32-bit from 0x86C */
	priv->PHYRegDef[RF90_PATH_D].rfintfo = rFPGA0_XD_RFInterfaceOE;

	/* RF Interface (Output and) Enable */
	/* 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	priv->PHYRegDef[RF90_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	/* 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */
	priv->PHYRegDef[RF90_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;
	/* 16 MSBs if read 32-bit from 0x86A (16-bit for 0x86A) */
	priv->PHYRegDef[RF90_PATH_C].rfintfe = rFPGA0_XC_RFInterfaceOE;
	/* 16 MSBs if read 32-bit from 0x86C (16-bit for 0x86E) */
	priv->PHYRegDef[RF90_PATH_D].rfintfe = rFPGA0_XD_RFInterfaceOE;

	/* Addr of LSSI. Write RF register by driver */
	priv->PHYRegDef[RF90_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_C].rf3wireOffset = rFPGA0_XC_LSSIParameter;
	priv->PHYRegDef[RF90_PATH_D].rf3wireOffset = rFPGA0_XD_LSSIParameter;

	/* RF parameter */
	/* BB Band Select */
	priv->PHYRegDef[RF90_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	priv->PHYRegDef[RF90_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	priv->PHYRegDef[RF90_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	/* Tx AGC Gain Stage (same for all path. Should we remove this?) */
	priv->PHYRegDef[RF90_PATH_A].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_B].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_C].rfTxGainStage = rFPGA0_TxGainStage;
	priv->PHYRegDef[RF90_PATH_D].rfTxGainStage = rFPGA0_TxGainStage;

	/* Tranceiver A~D HSSI Parameter-1 */
	/* wire control parameter1 */
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara1 = rFPGA0_XC_HSSIParameter1;
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara1 = rFPGA0_XD_HSSIParameter1;

	/* Tranceiver A~D HSSI Parameter-2 */
	/* wire control parameter2 */
	priv->PHYRegDef[RF90_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_C].rfHSSIPara2 = rFPGA0_XC_HSSIParameter2;
	priv->PHYRegDef[RF90_PATH_D].rfHSSIPara2 = rFPGA0_XD_HSSIParameter2;

	/* RF Switch Control */
	/* TR/Ant switch control */
	priv->PHYRegDef[RF90_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	priv->PHYRegDef[RF90_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	priv->PHYRegDef[RF90_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	/* AGC control 1 */
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	/* AGC control 2 */
	priv->PHYRegDef[RF90_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	priv->PHYRegDef[RF90_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	priv->PHYRegDef[RF90_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	priv->PHYRegDef[RF90_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	/* RX AFE control 1 */
	priv->PHYRegDef[RF90_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	/* RX AFE control 1 */
	priv->PHYRegDef[RF90_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	/* Tx AFE control 1 */
	priv->PHYRegDef[RF90_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	priv->PHYRegDef[RF90_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	priv->PHYRegDef[RF90_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	/* Tx AFE control 2 */
	priv->PHYRegDef[RF90_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	priv->PHYRegDef[RF90_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	priv->PHYRegDef[RF90_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	priv->PHYRegDef[RF90_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	/* Tranceiver LSSI Readback */
	priv->PHYRegDef[RF90_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	priv->PHYRegDef[RF90_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;
}

/******************************************************************************
 * function:  This function is to write register and then readback to make
 *            sure whether BB and RF is OK
 * input:     net_device        *dev
 *            hw90_block_e      CheckBlock
 *            rf90_radio_path_e e_rfpath  //only used when checkblock is
 *                                       //HW90_BLOCK_RF
 * output:    none
 * return:    return whether BB and RF is ok (0:OK, 1:Fail)
 * notice:    This function may be removed in the ASIC
 ******************************************************************************/
u8 rtl8192_phy_checkBBAndRF(struct net_device *dev, enum hw90_block_e CheckBlock,
			    enum rf90_radio_path_e e_rfpath)
{
	u8 ret = 0;
	u32 i, CheckTimes = 4, reg = 0;
	u32 WriteAddr[4];
	u32 WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};

	/* Initialize register address offset to be checked */
	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;
	RT_TRACE(COMP_PHY, "%s(), CheckBlock: %d\n", __func__, CheckBlock);
	for (i = 0; i < CheckTimes; i++) {

		/* Write data to register and readback */
		switch (CheckBlock) {
		case HW90_BLOCK_MAC:
			RT_TRACE(COMP_ERR,
				 "PHY_CheckBBRFOK(): Never Write 0x100 here!\n");
			break;

		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			write_nic_dword(dev, WriteAddr[CheckBlock],
					WriteData[i]);
			read_nic_dword(dev, WriteAddr[CheckBlock], &reg);
			break;

		case HW90_BLOCK_RF:
			WriteData[i] &= 0xfff;
			rtl8192_phy_SetRFReg(dev, e_rfpath,
					     WriteAddr[HW90_BLOCK_RF],
					     bMask12Bits, WriteData[i]);
			/* TODO: we should not delay for such a long time.
			 * Ask SD3
			 */
			usleep_range(1000, 1000);
			reg = rtl8192_phy_QueryRFReg(dev, e_rfpath,
						     WriteAddr[HW90_BLOCK_RF],
						     bMask12Bits);
			usleep_range(1000, 1000);
			break;

		default:
			ret = 1;
			break;
		}


		/* Check whether readback data is correct */
		if (reg != WriteData[i]) {
			RT_TRACE((COMP_PHY|COMP_ERR),
				 "error reg: %x, WriteData: %x\n",
				 reg, WriteData[i]);
			ret = 1;
			break;
		}
	}

	return ret;
}

/******************************************************************************
 * function:  This function initializes BB&RF
 * input:     net_device	*dev
 * output:    none
 * return:    none
 * notice:    Initialization value may change all the time, so please make
 *            sure it has been synced with the newest.
 ******************************************************************************/
static void rtl8192_BB_Config_ParaFile(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 reg_u8 = 0, eCheckItem = 0, status = 0;
	u32 reg_u32 = 0;

	/**************************************
	 * <1> Initialize BaseBand
	 *************************************/

	/* --set BB Global Reset-- */
	read_nic_byte(dev, BB_GLOBAL_RESET, &reg_u8);
	write_nic_byte(dev, BB_GLOBAL_RESET, (reg_u8|BB_GLOBAL_RESET_BIT));
	mdelay(50);
	/* ---set BB reset Active--- */
	read_nic_dword(dev, CPU_GEN, &reg_u32);
	write_nic_dword(dev, CPU_GEN, (reg_u32&(~CPU_GEN_BB_RST)));

	/* ----Ckeck FPGAPHY0 and PHY1 board is OK---- */
	/* TODO: this function should be removed on ASIC */
	for (eCheckItem = (enum hw90_block_e)HW90_BLOCK_PHY0;
	     eCheckItem <= HW90_BLOCK_PHY1; eCheckItem++) {
		/* don't care RF path */
		status = rtl8192_phy_checkBBAndRF(dev, (enum hw90_block_e)eCheckItem,
						  (enum rf90_radio_path_e)0);
		if (status != 0) {
			RT_TRACE((COMP_ERR | COMP_PHY),
				 "phy_rf8256_config(): Check PHY%d Fail!!\n",
				 eCheckItem-1);
			return;
		}
	}
	/* ---- Set CCK and OFDM Block "OFF"---- */
	rtl8192_setBBreg(dev, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x0);
	/* ----BB Register Initilazation---- */
	/* ==m==>Set PHY REG From Header<==m== */
	rtl8192_phyConfigBB(dev, BASEBAND_CONFIG_PHY_REG);

	/* ----Set BB reset de-Active---- */
	read_nic_dword(dev, CPU_GEN, &reg_u32);
	write_nic_dword(dev, CPU_GEN, (reg_u32|CPU_GEN_BB_RST));

	/* ----BB AGC table Initialization---- */
	/* ==m==>Set PHY REG From Header<==m== */
	rtl8192_phyConfigBB(dev, BASEBAND_CONFIG_AGC_TAB);

	/* ----Enable XSTAL ---- */
	write_nic_byte_E(dev, 0x5e, 0x00);
	if (priv->card_8192_version == VERSION_819XU_A) {
		/* Antenna gain offset from B/C/D to A */
		reg_u32 = priv->AntennaTxPwDiff[1]<<4 |
			   priv->AntennaTxPwDiff[0];
		rtl8192_setBBreg(dev, rFPGA0_TxGainStage, (bXBTxAGC|bXCTxAGC),
				 reg_u32);

		/* XSTALLCap */
		reg_u32 = priv->CrystalCap & 0xf;
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, bXtalCap,
				 reg_u32);
	}

	/* Check if the CCK HighPower is turned ON.
	 * This is used to calculate PWDB.
	 */
	priv->bCckHighPower = (u8)rtl8192_QueryBBReg(dev,
						     rFPGA0_XA_HSSIParameter2,
						     0x200);
}

/******************************************************************************
 * function:  This function initializes BB&RF
 * input:     net_device	*dev
 * output:    none
 * return:    none
 * notice:    Initialization value may change all the time, so please make
 *            sure it has been synced with the newest.
 *****************************************************************************/
void rtl8192_BBConfig(struct net_device *dev)
{
	rtl8192_InitBBRFRegDef(dev);
	/* config BB&RF. As hardCode based initialization has not been well
	 * implemented, so use file first.
	 * FIXME: should implement it for hardcode?
	 */
	rtl8192_BB_Config_ParaFile(dev);
}


/******************************************************************************
 * function:  This function obtains the initialization value of Tx power Level
 *            offset
 * input:     net_device	*dev
 * output:    none
 * return:    none
 *****************************************************************************/
void rtl8192_phy_getTxPower(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 tmp;

	read_nic_dword(dev, rTxAGC_Rate18_06,
		       &priv->MCSTxPowerLevelOriginalOffset[0]);
	read_nic_dword(dev, rTxAGC_Rate54_24,
		       &priv->MCSTxPowerLevelOriginalOffset[1]);
	read_nic_dword(dev, rTxAGC_Mcs03_Mcs00,
		       &priv->MCSTxPowerLevelOriginalOffset[2]);
	read_nic_dword(dev, rTxAGC_Mcs07_Mcs04,
		       &priv->MCSTxPowerLevelOriginalOffset[3]);
	read_nic_dword(dev, rTxAGC_Mcs11_Mcs08,
		       &priv->MCSTxPowerLevelOriginalOffset[4]);
	read_nic_dword(dev, rTxAGC_Mcs15_Mcs12,
		       &priv->MCSTxPowerLevelOriginalOffset[5]);

	/* Read rx initial gain */
	read_nic_byte(dev, rOFDM0_XAAGCCore1, &priv->DefaultInitialGain[0]);
	read_nic_byte(dev, rOFDM0_XBAGCCore1, &priv->DefaultInitialGain[1]);
	read_nic_byte(dev, rOFDM0_XCAGCCore1, &priv->DefaultInitialGain[2]);
	read_nic_byte(dev, rOFDM0_XDAGCCore1, &priv->DefaultInitialGain[3]);
	RT_TRACE(COMP_INIT,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x)\n",
		 priv->DefaultInitialGain[0], priv->DefaultInitialGain[1],
		 priv->DefaultInitialGain[2], priv->DefaultInitialGain[3]);

	/* Read framesync */
	read_nic_byte(dev, rOFDM0_RxDetector3, &priv->framesync);
	read_nic_byte(dev, rOFDM0_RxDetector2, &tmp);
	priv->framesyncC34 = tmp;
	RT_TRACE(COMP_INIT, "Default framesync (0x%x) = 0x%x\n",
		rOFDM0_RxDetector3, priv->framesync);

	/* Read SIFS (save the value read fome MACPHY_REG.txt) */
	read_nic_word(dev, SIFS, &priv->SifsTime);
}

/******************************************************************************
 * function:  This function sets the initialization value of Tx power Level
 *            offset
 * input:     net_device        *dev
 *            u8                channel
 * output:    none
 * return:    none
 ******************************************************************************/
void rtl8192_phy_setTxPower(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	powerlevel = priv->TxPowerLevelCCK[channel-1];
	u8	powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];

	switch (priv->rf_chip) {
	case RF_8256:
		/* need further implement */
		phy_set_rf8256_cck_tx_power(dev, powerlevel);
		phy_set_rf8256_ofdm_tx_power(dev, powerlevelOFDM24G);
		break;
	default:
		RT_TRACE((COMP_PHY|COMP_ERR),
			 "error RF chipID(8225 or 8258) in function %s()\n",
			 __func__);
		break;
	}
}

/******************************************************************************
 * function:  This function checks Rf chip to do RF config
 * input:     net_device	*dev
 * output:    none
 * return:    only 8256 is supported
 ******************************************************************************/
void rtl8192_phy_RFConfig(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	switch (priv->rf_chip) {
	case RF_8256:
		phy_rf8256_config(dev);
		break;
	default:
		RT_TRACE(COMP_ERR, "error chip id\n");
		break;
	}
}

/******************************************************************************
 * function:  This function updates Initial gain
 * input:     net_device	*dev
 * output:    none
 * return:    As Windows has not implemented this, wait for complement
 ******************************************************************************/
void rtl8192_phy_updateInitGain(struct net_device *dev)
{
}

/******************************************************************************
 * function:  This function read RF parameters from general head file,
 *            and do RF 3-wire
 * input:     net_device	*dev
 *            rf90_radio_path_e e_rfpath
 * output:    none
 * return:    return code show if RF configuration is successful(0:pass, 1:fail)
 * notice:    Delay may be required for RF configuration
 *****************************************************************************/
u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device *dev,
				      enum rf90_radio_path_e	e_rfpath)
{

	int i;

	switch (e_rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < RadioA_ArrayLength; i = i+2) {

			if (Rtl8192UsbRadioA_Array[i] == 0xfe) {
				mdelay(100);
				continue;
			}
			rtl8192_phy_SetRFReg(dev, e_rfpath,
					     Rtl8192UsbRadioA_Array[i],
					     bMask12Bits,
					     Rtl8192UsbRadioA_Array[i+1]);
			mdelay(1);

		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < RadioB_ArrayLength; i = i+2) {

			if (Rtl8192UsbRadioB_Array[i] == 0xfe) {
				mdelay(100);
				continue;
			}
			rtl8192_phy_SetRFReg(dev, e_rfpath,
					     Rtl8192UsbRadioB_Array[i],
					     bMask12Bits,
					     Rtl8192UsbRadioB_Array[i+1]);
			mdelay(1);

		}
		break;
	case RF90_PATH_C:
		for (i = 0; i < RadioC_ArrayLength; i = i+2) {

			if (Rtl8192UsbRadioC_Array[i] == 0xfe) {
				mdelay(100);
				continue;
			}
			rtl8192_phy_SetRFReg(dev, e_rfpath,
					     Rtl8192UsbRadioC_Array[i],
					     bMask12Bits,
					     Rtl8192UsbRadioC_Array[i+1]);
			mdelay(1);

		}
		break;
	case RF90_PATH_D:
		for (i = 0; i < RadioD_ArrayLength; i = i+2) {

			if (Rtl8192UsbRadioD_Array[i] == 0xfe) {
				mdelay(100);
				continue;
			}
			rtl8192_phy_SetRFReg(dev, e_rfpath,
					     Rtl8192UsbRadioD_Array[i],
					     bMask12Bits,
					     Rtl8192UsbRadioD_Array[i+1]);
			mdelay(1);

		}
		break;
	default:
		break;
	}

	return 0;

}

/******************************************************************************
 * function:  This function sets Tx Power of the channel
 * input:     net_device        *dev
 *            u8                channel
 * output:    none
 * return:    none
 * notice:
 ******************************************************************************/
static void rtl8192_SetTxPowerLevel(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	powerlevel = priv->TxPowerLevelCCK[channel-1];
	u8	powerlevelOFDM24G = priv->TxPowerLevelOFDM24G[channel-1];

	switch (priv->rf_chip) {
	case RF_8225:
		break;

	case RF_8256:
		phy_set_rf8256_cck_tx_power(dev, powerlevel);
		phy_set_rf8256_ofdm_tx_power(dev, powerlevelOFDM24G);
		break;

	case RF_8258:
		break;
	default:
		RT_TRACE(COMP_ERR, "unknown rf chip ID in %s()\n", __func__);
		break;
	}
}

/******************************************************************************
 * function:  This function sets RF state on or off
 * input:     net_device         *dev
 *            RT_RF_POWER_STATE  eRFPowerState  //Power State to set
 * output:    none
 * return:    none
 * notice:
 *****************************************************************************/
bool rtl8192_SetRFPowerState(struct net_device *dev,
			     RT_RF_POWER_STATE eRFPowerState)
{
	bool				bResult = true;
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (eRFPowerState == priv->ieee80211->eRFPowerState)
		return false;

	if (priv->SetRFPowerStateInProgress)
		return false;

	priv->SetRFPowerStateInProgress = true;

	switch (priv->rf_chip) {
	case RF_8256:
		switch (eRFPowerState) {
		case eRfOn:
			/* RF-A, RF-B */
			/* enable RF-Chip A/B - 0x860[4] */
			rtl8192_setBBreg(dev, rFPGA0_XA_RFInterfaceOE, BIT(4),
					 0x1);
			/* analog to digital on - 0x88c[9:8] */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0x300,
					 0x3);
			/* digital to analog on - 0x880[4:3] */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x18,
					 0x3);
			/* rx antenna on - 0xc04[1:0] */
			rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0x3, 0x3);
			/* rx antenna on - 0xd04[1:0] */
			rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0x3, 0x3);
			/* analog to digital part2 on - 0x880[6:5] */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x60,
					 0x3);

			break;

		case eRfSleep:

			break;

		case eRfOff:
			/* RF-A, RF-B */
			/* disable RF-Chip A/B - 0x860[4] */
			rtl8192_setBBreg(dev, rFPGA0_XA_RFInterfaceOE, BIT(4),
					 0x0);
			/* analog to digital off, for power save */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00,
					 0x0); /* 0x88c[11:8] */
			/* digital to analog off, for power save - 0x880[4:3] */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x18,
					 0x0);
			/* rx antenna off - 0xc04[3:0] */
			rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0xf, 0x0);
			/* rx antenna off - 0xd04[3:0] */
			rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xf, 0x0);
			/* analog to digital part2 off, for power save */
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x60,
					 0x0); /* 0x880[6:5] */

			break;

		default:
			bResult = false;
			RT_TRACE(COMP_ERR, "%s(): unknown state to set: 0x%X\n",
				 __func__, eRFPowerState);
			break;
		}
		break;
	default:
		RT_TRACE(COMP_ERR, "Not support rf_chip(%x)\n", priv->rf_chip);
		break;
	}
	priv->SetRFPowerStateInProgress = false;

	return bResult;
}

/******************************************************************************
 * function:  This function sets command table variable (struct sw_chnl_cmd).
 * input:     sw_chnl_cmd      *CmdTable    //table to be set
 *            u32            CmdTableIdx  //variable index in table to be set
 *            u32            CmdTableSz   //table size
 *            switch_chan_cmd_id    CmdID        //command ID to set
 *            u32            Para1
 *            u32            Para2
 *            u32            msDelay
 * output:
 * return:    true if finished, false otherwise
 * notice:
 ******************************************************************************/
static u8 rtl8192_phy_SetSwChnlCmdArray(struct sw_chnl_cmd *CmdTable, u32 CmdTableIdx,
					u32 CmdTableSz, enum switch_chan_cmd_id CmdID,
					u32 Para1, u32 Para2, u32 msDelay)
{
	struct sw_chnl_cmd *pCmd;

	if (CmdTable == NULL) {
		RT_TRACE(COMP_ERR, "%s(): CmdTable cannot be NULL\n", __func__);
		return false;
	}
	if (CmdTableIdx >= CmdTableSz) {
		RT_TRACE(COMP_ERR, "%s(): Access invalid index, please check size of the table, CmdTableIdx:%d, CmdTableSz:%d\n",
			 __func__, CmdTableIdx, CmdTableSz);
		return false;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->cmd_id = CmdID;
	pCmd->para_1 = Para1;
	pCmd->para_2 = Para2;
	pCmd->ms_delay = msDelay;

	return true;
}

/******************************************************************************
 * function:  This function sets channel step by step
 * input:     net_device        *dev
 *            u8                channel
 *            u8                *stage   //3 stages
 *            u8                *step
 *            u32               *delay   //whether need to delay
 * output:    store new stage, step and delay for next step
 *            (combine with function above)
 * return:    true if finished, false otherwise
 * notice:    Wait for simpler function to replace it
 *****************************************************************************/
static u8 rtl8192_phy_SwChnlStepByStep(struct net_device *dev, u8 channel,
				       u8 *stage, u8 *step, u32 *delay)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct sw_chnl_cmd   PreCommonCmd[MAX_PRECMD_CNT];
	u32		   PreCommonCmdCnt;
	struct sw_chnl_cmd   PostCommonCmd[MAX_POSTCMD_CNT];
	u32		   PostCommonCmdCnt;
	struct sw_chnl_cmd   RfDependCmd[MAX_RFDEPENDCMD_CNT];
	u32		   RfDependCmdCnt;
	struct sw_chnl_cmd  *CurrentCmd = NULL;
	u8		   e_rfpath;

	RT_TRACE(COMP_CH, "%s() stage: %d, step: %d, channel: %d\n",
		 __func__, *stage, *step, channel);
	if (!is_legal_channel(priv->ieee80211, channel)) {
		RT_TRACE(COMP_ERR, "set to illegal channel: %d\n", channel);
		/* return true to tell upper caller function this channel
		 * setting is finished! Or it will in while loop.
		 */
		return true;
	}
	/* FIXME: need to check whether channel is legal or not here */


	/* <1> Fill up pre common command. */
	PreCommonCmdCnt = 0;
	rtl8192_phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++,
				      MAX_PRECMD_CNT, CMD_ID_SET_TX_PWR_LEVEL,
				      0, 0, 0);
	rtl8192_phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++,
				      MAX_PRECMD_CNT, CMD_ID_END, 0, 0, 0);

	/* <2> Fill up post common command. */
	PostCommonCmdCnt = 0;

	rtl8192_phy_SetSwChnlCmdArray(PostCommonCmd, PostCommonCmdCnt++,
				      MAX_POSTCMD_CNT, CMD_ID_END, 0, 0, 0);

	/* <3> Fill up RF dependent command. */
	RfDependCmdCnt = 0;
	switch (priv->rf_chip) {
	case RF_8225:
		if (!(channel >= 1 && channel <= 14)) {
			RT_TRACE(COMP_ERR,
				 "illegal channel for Zebra 8225: %d\n",
				 channel);
			return true;
		}
		rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++,
					      MAX_RFDEPENDCMD_CNT,
					      CMD_ID_RF_WRITE_REG,
					      rZebra1_Channel,
					      RF_CHANNEL_TABLE_ZEBRA[channel],
					      10);
		rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++,
					      MAX_RFDEPENDCMD_CNT,
					      CMD_ID_END, 0, 0, 0);
		break;

	case RF_8256:
		/* TEST!! This is not the table for 8256!! */
		if (!(channel >= 1 && channel <= 14)) {
			RT_TRACE(COMP_ERR,
				 "illegal channel for Zebra 8256: %d\n",
				 channel);
			return true;
		}
		rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++,
					      MAX_RFDEPENDCMD_CNT,
					      CMD_ID_RF_WRITE_REG,
					      rZebra1_Channel, channel, 10);
		rtl8192_phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++,
					      MAX_RFDEPENDCMD_CNT,
					      CMD_ID_END, 0, 0, 0);
		break;

	case RF_8258:
		break;

	default:
		RT_TRACE(COMP_ERR, "Unknown RFChipID: %d\n", priv->rf_chip);
		return true;
	}


	do {
		switch (*stage) {
		case 0:
			CurrentCmd = &PreCommonCmd[*step];
			break;
		case 1:
			CurrentCmd = &RfDependCmd[*step];
			break;
		case 2:
			CurrentCmd = &PostCommonCmd[*step];
			break;
		}

		if (CurrentCmd->cmd_id == CMD_ID_END) {
			if ((*stage) == 2) {
				(*delay) = CurrentCmd->ms_delay;
				return true;
			}
			(*stage)++;
			(*step) = 0;
			continue;
		}

		switch (CurrentCmd->cmd_id) {
		case CMD_ID_SET_TX_PWR_LEVEL:
			if (priv->card_8192_version == VERSION_819XU_A)
				/* consider it later! */
				rtl8192_SetTxPowerLevel(dev, channel);
			break;
		case CMD_ID_WRITE_PORT_ULONG:
			write_nic_dword(dev, CurrentCmd->para_1,
					CurrentCmd->para_2);
			break;
		case CMD_ID_WRITE_PORT_USHORT:
			write_nic_word(dev, CurrentCmd->para_1,
				       (u16)CurrentCmd->para_2);
			break;
		case CMD_ID_WRITE_PORT_UCHAR:
			write_nic_byte(dev, CurrentCmd->para_1,
				       (u8)CurrentCmd->para_2);
			break;
		case CMD_ID_RF_WRITE_REG:
			for (e_rfpath = 0; e_rfpath < RF90_PATH_MAX; e_rfpath++) {
				rtl8192_phy_SetRFReg(dev,
						     (enum rf90_radio_path_e)e_rfpath,
						     CurrentCmd->para_1,
						     bZebra1_ChannelNum,
						     CurrentCmd->para_2);
			}
			break;
		default:
			break;
		}

		break;
	} while (true);

	(*delay) = CurrentCmd->ms_delay;
	(*step)++;
	return false;
}

/******************************************************************************
 * function:  This function does actually set channel work
 * input:     net_device        *dev
 *            u8                channel
 * output:    none
 * return:    none
 * notice:    We should not call this function directly
 *****************************************************************************/
static void rtl8192_phy_FinishSwChnlNow(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32	delay = 0;

	while (!rtl8192_phy_SwChnlStepByStep(dev, channel, &priv->SwChnlStage,
					     &priv->SwChnlStep, &delay)) {
		if (!priv->up)
			break;
	}
}

/******************************************************************************
 * function:  Callback routine of the work item for switch channel.
 * input:     net_device	*dev
 *
 * output:    none
 * return:    none
 *****************************************************************************/
void rtl8192_SwChnl_WorkItem(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_CH, "==> SwChnlCallback819xUsbWorkItem(), chan:%d\n",
		 priv->chan);


	rtl8192_phy_FinishSwChnlNow(dev, priv->chan);

	RT_TRACE(COMP_CH, "<== SwChnlCallback819xUsbWorkItem()\n");
}

/******************************************************************************
 * function:  This function scheduled actual work item to set channel
 * input:     net_device        *dev
 *            u8                channel   //channel to set
 * output:    none
 * return:    return code show if workitem is scheduled (1:pass, 0:fail)
 * notice:    Delay may be required for RF configuration
 ******************************************************************************/
u8 rtl8192_phy_SwChnl(struct net_device *dev, u8 channel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_CH, "%s(), SwChnlInProgress: %d\n", __func__,
		 priv->SwChnlInProgress);
	if (!priv->up)
		return false;
	if (priv->SwChnlInProgress)
		return false;

	/* -------------------------------------------- */
	switch (priv->ieee80211->mode) {
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		if (channel <= 14) {
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_A but channel<=14\n");
			return false;
		}
		break;
	case WIRELESS_MODE_B:
		if (channel > 14) {
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_B but channel>14\n");
			return false;
		}
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		if (channel > 14) {
			RT_TRACE(COMP_ERR, "WIRELESS_MODE_G but channel>14\n");
			return false;
		}
		break;
	}
	/* -------------------------------------------- */

	priv->SwChnlInProgress = true;
	if (channel == 0)
		channel = 1;

	priv->chan = channel;

	priv->SwChnlStage = 0;
	priv->SwChnlStep = 0;
	if (priv->up)
		rtl8192_SwChnl_WorkItem(dev);

	priv->SwChnlInProgress = false;
	return true;
}

/******************************************************************************
 * function:  Callback routine of the work item for set bandwidth mode.
 * input:     net_device	 *dev
 * output:    none
 * return:    none
 * notice:    I doubt whether SetBWModeInProgress flag is necessary as we can
 *            test whether current work in the queue or not.//do I?
 *****************************************************************************/
void rtl8192_SetBWModeWorkItem(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 regBwOpMode;

	RT_TRACE(COMP_SWBW, "%s()  Switch to %s bandwidth\n", __func__,
		 priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz");


	if (priv->rf_chip == RF_PSEUDO_11N) {
		priv->SetBWModeInProgress = false;
		return;
	}

	/* <1> Set MAC register */
	read_nic_byte(dev, BW_OPMODE, &regBwOpMode);

	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		/* We have not verify whether this register works */
		write_nic_byte(dev, BW_OPMODE, regBwOpMode);
		break;

	case HT_CHANNEL_WIDTH_20_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		/* We have not verify whether this register works */
		write_nic_byte(dev, BW_OPMODE, regBwOpMode);
		break;

	default:
		RT_TRACE(COMP_ERR,
			 "SetChannelBandwidth819xUsb(): unknown Bandwidth: %#X\n",
			 priv->CurrentChannelBW);
		break;
	}

	/* <2> Set PHY related register */
	switch (priv->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x0);
		rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x0);
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1,
				 0x00100000, 1);

		/* Correct the tx power for CCK rate in 20M. */
		priv->cck_present_attenuation =
			priv->cck_present_attenuation_20Mdefault +
			priv->cck_present_attenuation_difference;

		if (priv->cck_present_attenuation > 22)
			priv->cck_present_attenuation = 22;
		if (priv->cck_present_attenuation < 0)
			priv->cck_present_attenuation = 0;
		RT_TRACE(COMP_INIT,
			 "20M, pHalData->CCKPresentAttentuation = %d\n",
			 priv->cck_present_attenuation);

		if (priv->chan == 14 && !priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = true;
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else if (priv->chan != 14 && priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = false;
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else {
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		}

		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl8192_setBBreg(dev, rFPGA0_RFMOD, bRFMOD, 0x1);
		rtl8192_setBBreg(dev, rFPGA1_RFMOD, bRFMOD, 0x1);
		rtl8192_setBBreg(dev, rCCK0_System, bCCKSideBand,
				 priv->nCur40MhzPrimeSC >> 1);
		rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x00100000, 0);
		rtl8192_setBBreg(dev, rOFDM1_LSTF, 0xC00,
				 priv->nCur40MhzPrimeSC);
		priv->cck_present_attenuation =
			priv->cck_present_attenuation_40Mdefault +
			priv->cck_present_attenuation_difference;

		if (priv->cck_present_attenuation > 22)
			priv->cck_present_attenuation = 22;
		if (priv->cck_present_attenuation < 0)
			priv->cck_present_attenuation = 0;

		RT_TRACE(COMP_INIT,
			 "40M, pHalData->CCKPresentAttentuation = %d\n",
			 priv->cck_present_attenuation);
		if (priv->chan == 14 && !priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = true;
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else if (priv->chan != 14 && priv->bcck_in_ch14) {
			priv->bcck_in_ch14 = false;
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		} else {
			dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
		}

		break;
	default:
		RT_TRACE(COMP_ERR,
			 "SetChannelBandwidth819xUsb(): unknown Bandwidth: %#X\n",
			 priv->CurrentChannelBW);
		break;

	}
	/* Skip over setting of J-mode in BB register here.
	 * Default value is "None J mode".
	 */

	/* <3> Set RF related register */
	switch (priv->rf_chip) {
	case RF_8225:
		break;

	case RF_8256:
		phy_set_rf8256_bandwidth(dev, priv->CurrentChannelBW);
		break;

	case RF_8258:
		break;

	case RF_PSEUDO_11N:
		break;

	default:
		RT_TRACE(COMP_ERR, "Unknown RFChipID: %d\n", priv->rf_chip);
		break;
	}
	priv->SetBWModeInProgress = false;

	RT_TRACE(COMP_SWBW, "<==SetBWMode819xUsb(), %d\n",
		 atomic_read(&priv->ieee80211->atm_swbw));
}

/******************************************************************************
 * function:  This function schedules bandwidth switch work.
 * input:     struct net_deviceq   *dev
 *            HT_CHANNEL_WIDTH     bandwidth  //20M or 40M
 *            HT_EXTCHNL_OFFSET    offset     //Upper, Lower, or Don't care
 * output:    none
 * return:    none
 * notice:    I doubt whether SetBWModeInProgress flag is necessary as we can
 *	      test whether current work in the queue or not.//do I?
 *****************************************************************************/
void rtl8192_SetBWMode(struct net_device *dev,
		       enum ht_channel_width bandwidth,
		       enum ht_extension_chan_offset offset)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (priv->SetBWModeInProgress)
		return;
	priv->SetBWModeInProgress = true;

	priv->CurrentChannelBW = bandwidth;

	if (offset == HT_EXTCHNL_OFFSET_LOWER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (offset == HT_EXTCHNL_OFFSET_UPPER)
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		priv->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	rtl8192_SetBWModeWorkItem(dev);

}

void InitialGain819xUsb(struct net_device *dev,	u8 Operation)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->InitialGainOperateType = Operation;

	if (priv->up)
		queue_delayed_work(priv->priv_wq, &priv->initialgain_operate_wq, 0);
}

void InitialGainOperateWorkItemCallBack(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct r8192_priv *priv = container_of(dwork, struct r8192_priv,
					       initialgain_operate_wq);
	struct net_device *dev = priv->ieee80211->dev;
#define SCAN_RX_INITIAL_GAIN	0x17
#define POWER_DETECTION_TH	0x08
	u32	bitmask;
	u8	initial_gain;
	u8	Operation;

	Operation = priv->InitialGainOperateType;

	switch (Operation) {
	case IG_Backup:
		RT_TRACE(COMP_SCAN, "IG_Backup, backup the initial gain.\n");
		initial_gain = SCAN_RX_INITIAL_GAIN;
		bitmask = bMaskByte0;
		if (dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
			/* FW DIG OFF */
			rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);
		priv->initgain_backup.xaagccore1 =
			(u8)rtl8192_QueryBBReg(dev, rOFDM0_XAAGCCore1, bitmask);
		priv->initgain_backup.xbagccore1 =
			(u8)rtl8192_QueryBBReg(dev, rOFDM0_XBAGCCore1, bitmask);
		priv->initgain_backup.xcagccore1 =
			(u8)rtl8192_QueryBBReg(dev, rOFDM0_XCAGCCore1, bitmask);
		priv->initgain_backup.xdagccore1 =
			(u8)rtl8192_QueryBBReg(dev, rOFDM0_XDAGCCore1, bitmask);
		bitmask = bMaskByte2;
		priv->initgain_backup.cca =
			(u8)rtl8192_QueryBBReg(dev, rCCK0_CCA, bitmask);

		RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc50 is %x\n",
			 priv->initgain_backup.xaagccore1);
		RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc58 is %x\n",
			 priv->initgain_backup.xbagccore1);
		RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc60 is %x\n",
			 priv->initgain_backup.xcagccore1);
		RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xc68 is %x\n",
			 priv->initgain_backup.xdagccore1);
		RT_TRACE(COMP_SCAN, "Scan InitialGainBackup 0xa0a is %x\n",
			 priv->initgain_backup.cca);

		RT_TRACE(COMP_SCAN, "Write scan initial gain = 0x%x\n",
			 initial_gain);
		write_nic_byte(dev, rOFDM0_XAAGCCore1, initial_gain);
		write_nic_byte(dev, rOFDM0_XBAGCCore1, initial_gain);
		write_nic_byte(dev, rOFDM0_XCAGCCore1, initial_gain);
		write_nic_byte(dev, rOFDM0_XDAGCCore1, initial_gain);
		RT_TRACE(COMP_SCAN, "Write scan 0xa0a = 0x%x\n",
			 POWER_DETECTION_TH);
		write_nic_byte(dev, 0xa0a, POWER_DETECTION_TH);
		break;
	case IG_Restore:
		RT_TRACE(COMP_SCAN, "IG_Restore, restore the initial gain.\n");
		bitmask = 0x7f; /* Bit0 ~ Bit6 */
		if (dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
			/* FW DIG OFF */
			rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);

		rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, bitmask,
				 (u32)priv->initgain_backup.xaagccore1);
		rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, bitmask,
				 (u32)priv->initgain_backup.xbagccore1);
		rtl8192_setBBreg(dev, rOFDM0_XCAGCCore1, bitmask,
				 (u32)priv->initgain_backup.xcagccore1);
		rtl8192_setBBreg(dev, rOFDM0_XDAGCCore1, bitmask,
				 (u32)priv->initgain_backup.xdagccore1);
		bitmask  = bMaskByte2;
		rtl8192_setBBreg(dev, rCCK0_CCA, bitmask,
				 (u32)priv->initgain_backup.cca);

		RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc50 is %x\n",
			 priv->initgain_backup.xaagccore1);
		RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc58 is %x\n",
			 priv->initgain_backup.xbagccore1);
		RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc60 is %x\n",
			 priv->initgain_backup.xcagccore1);
		RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xc68 is %x\n",
			 priv->initgain_backup.xdagccore1);
		RT_TRACE(COMP_SCAN, "Scan BBInitialGainRestore 0xa0a is %x\n",
			 priv->initgain_backup.cca);

		rtl8192_phy_setTxPower(dev, priv->ieee80211->current_network.channel);

		if (dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
			/* FW DIG ON */
			rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);
		break;
	default:
		RT_TRACE(COMP_SCAN, "Unknown IG Operation.\n");
		break;
	}
}
