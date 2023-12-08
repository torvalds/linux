/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

 #ifndef __HAL_PHY_RF_H__
 #define __HAL_PHY_RF_H__

enum pwrtrack_method {
	BBSWING,
	TXAGC,
	MIX_MODE
};

typedef void (*FuncSetPwr)(struct dm_odm_t *, enum pwrtrack_method, u8, u8);
typedef void (*FuncIQK)(struct dm_odm_t *, u8, u8, u8);
typedef void (*FuncLCK)(struct dm_odm_t *);
typedef void (*FuncSwing)(struct dm_odm_t *, u8 **, u8 **, u8 **, u8 **);

struct txpwrtrack_cfg {
	u8 SwingTableSize_CCK;
	u8 SwingTableSize_OFDM;
	u8 Threshold_IQK;
	u8 AverageThermalNum;
	u8 RfPathCount;
	u32 ThermalRegAddr;
	FuncSetPwr ODM_TxPwrTrackSetPwr;
	FuncIQK DoIQK;
	FuncLCK PHY_LCCalibrate;
	FuncSwing GetDeltaSwingTable;
};

void ConfigureTxpowerTrack(struct dm_odm_t *pDM_Odm, struct txpwrtrack_cfg *pConfig);


void ODM_ClearTxPowerTrackingState(struct dm_odm_t *pDM_Odm);

void ODM_TXPowerTrackingCallback_ThermalMeter(struct adapter *Adapter);

#endif	/*  #ifndef __HAL_PHY_RF_H__ */
