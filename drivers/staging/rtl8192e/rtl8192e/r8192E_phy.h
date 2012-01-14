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
#ifndef _R819XU_PHY_H
#define _R819XU_PHY_H

#define MAX_DOZE_WAITING_TIMES_9x 64

#define AGCTAB_ArrayLength			AGCTAB_ArrayLengthPciE
#define MACPHY_ArrayLength			MACPHY_ArrayLengthPciE
#define RadioA_ArrayLength			RadioA_ArrayLengthPciE
#define RadioB_ArrayLength			RadioB_ArrayLengthPciE
#define MACPHY_Array_PGLength			MACPHY_Array_PGLengthPciE
#define RadioC_ArrayLength			RadioC_ArrayLengthPciE
#define RadioD_ArrayLength			RadioD_ArrayLengthPciE
#define PHY_REGArrayLength			PHY_REGArrayLengthPciE
#define PHY_REG_1T2RArrayLength			PHY_REG_1T2RArrayLengthPciE

#define Rtl819XMACPHY_Array_PG			Rtl8192PciEMACPHY_Array_PG
#define Rtl819XMACPHY_Array			Rtl8192PciEMACPHY_Array
#define Rtl819XRadioA_Array			Rtl8192PciERadioA_Array
#define Rtl819XRadioB_Array			Rtl8192PciERadioB_Array
#define Rtl819XRadioC_Array			Rtl8192PciERadioC_Array
#define Rtl819XRadioD_Array			Rtl8192PciERadioD_Array
#define Rtl819XAGCTAB_Array			Rtl8192PciEAGCTAB_Array
#define Rtl819XPHY_REGArray			Rtl8192PciEPHY_REGArray
#define Rtl819XPHY_REG_1T2RArray		Rtl8192PciEPHY_REG_1T2RArray

extern u32 rtl819XMACPHY_Array_PG[];
extern u32 rtl819XPHY_REG_1T2RArray[];
extern u32 rtl819XAGCTAB_Array[];
extern u32 rtl819XRadioA_Array[];
extern u32 rtl819XRadioB_Array[];
extern u32 rtl819XRadioC_Array[];
extern u32 rtl819XRadioD_Array[];

enum hw90_block {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4,
};

enum rf90_radio_path {
	RF90_PATH_A = 0,
	RF90_PATH_B = 1,
	RF90_PATH_C = 2,
	RF90_PATH_D = 3,
	RF90_PATH_MAX
};

#define bMaskByte0                0xff
#define bMaskByte1                0xff00
#define bMaskByte2                0xff0000
#define bMaskByte3                0xff000000
#define bMaskHWord                0xffff0000
#define bMaskLWord                0x0000ffff
#define bMaskDWord                0xffffffff

extern u8 rtl8192_phy_CheckIsLegalRFPath(struct net_device *dev,
					 u32 eRFPath);
extern void rtl8192_setBBreg(struct net_device *dev, u32 dwRegAddr,
			     u32 dwBitMask, u32 dwData);
extern u32 rtl8192_QueryBBReg(struct net_device *dev, u32 dwRegAddr,
			      u32 dwBitMask);
extern void rtl8192_phy_SetRFReg(struct net_device *dev,
				 enum rf90_radio_path eRFPath,
				 u32 RegAddr, u32 BitMask, u32 Data);
extern u32 rtl8192_phy_QueryRFReg(struct net_device *dev,
				  enum rf90_radio_path eRFPath,
				  u32 RegAddr, u32 BitMask);
extern void rtl8192_phy_configmac(struct net_device *dev);
extern void rtl8192_phyConfigBB(struct net_device *dev, u8 ConfigType);
extern bool rtl8192_phy_checkBBAndRF(struct net_device *dev,
				     enum hw90_block CheckBlock,
				     enum rf90_radio_path eRFPath);
extern bool rtl8192_BBConfig(struct net_device *dev);
extern void rtl8192_phy_getTxPower(struct net_device *dev);
extern void rtl8192_phy_setTxPower(struct net_device *dev, u8 channel);
extern bool rtl8192_phy_RFConfig(struct net_device *dev);
extern void rtl8192_phy_updateInitGain(struct net_device *dev);
extern u8 rtl8192_phy_ConfigRFWithHeaderFile(struct net_device *dev,
					     enum rf90_radio_path eRFPath);

extern u8 rtl8192_phy_SwChnl(struct net_device *dev, u8 channel);
extern void rtl8192_SetBWMode(struct net_device *dev,
			      enum ht_channel_width Bandwidth,
			      enum ht_extchnl_offset Offset);
extern void rtl8192_SwChnl_WorkItem(struct net_device *dev);
extern void rtl8192_SetBWModeWorkItem(struct net_device *dev);
extern void InitialGain819xPci(struct net_device *dev, u8 Operation);

extern	void PHY_SetRtl8192eRfOff(struct net_device *dev);

bool
SetRFPowerState(
	struct net_device *dev,
	enum rt_rf_power_state eRFPowerState
	);
#define PHY_SetRFPowerState SetRFPowerState

extern void PHY_ScanOperationBackup8192(struct net_device *dev, u8 Operation);

#endif
