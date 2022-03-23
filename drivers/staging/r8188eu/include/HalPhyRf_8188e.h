/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __HAL_PHY_RF_8188E_H__
#define __HAL_PHY_RF_8188E_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_88E		10		/* ms */
#define	index_mapping_NUM_88E	15
#define AVG_THERMAL_NUM_88E	4

void ODM_TxPwrTrackAdjust88E(struct odm_dm_struct *pDM_Odm,
			     u8 Type,	/* 0 = OFDM, 1 = CCK */
			     u8 *pDirection,/* 1 = +(incr) 2 = -(decr) */
			     u32 *pOutWriteVal); /* Tx tracking CCK/OFDM BB
						     * swing index adjust */

void odm_TXPowerTrackingCallback_ThermalMeter_8188E(struct adapter *Adapter);

/* 1 7.	IQK */

void PHY_IQCalibrate_8188E(struct adapter *Adapter, bool ReCovery);

/*  LC calibrate */
void PHY_LCCalibrate_8188E(struct adapter *pAdapter);

/*  AP calibrate */
void PHY_DigitalPredistortion_8188E(struct adapter *pAdapter);

void _PHY_SaveADDARegisters(struct adapter *pAdapter, u32 *ADDAReg,
			    u32 *ADDABackup, u32 RegisterNum);

void _PHY_MACSettingCalibration(struct adapter *pAdapter, u32 *MACReg,
				u32 *MACBackup);

#endif	/*  #ifndef __HAL_PHY_RF_8188E_H__ */
