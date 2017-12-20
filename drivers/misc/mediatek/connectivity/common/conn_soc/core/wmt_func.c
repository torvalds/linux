/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-FUNC]"

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"

#include "wmt_func.h"
#include "wmt_lib.h"
#include "wmt_core.h"
#include "wmt_exp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
#if CFG_FUNC_BT_SUPPORT

static INT32 wmt_func_bt_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);
static INT32 wmt_func_bt_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);

WMT_FUNC_OPS wmt_func_bt_ops = {
	/* BT subsystem function on/off */
	.func_on = wmt_func_bt_on,
	.func_off = wmt_func_bt_off
};
#endif

#if CFG_FUNC_FM_SUPPORT

static INT32 wmt_func_fm_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);
static INT32 wmt_func_fm_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);

WMT_FUNC_OPS wmt_func_fm_ops = {
	/* FM subsystem function on/off */
	.func_on = wmt_func_fm_on,
	.func_off = wmt_func_fm_off
};
#endif

#if CFG_FUNC_GPS_SUPPORT

static INT32 wmt_func_gps_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);
static INT32 wmt_func_gps_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);

WMT_FUNC_OPS wmt_func_gps_ops = {
	/* GPS subsystem function on/off */
	.func_on = wmt_func_gps_on,
	.func_off = wmt_func_gps_off
};

#endif

#if CFG_FUNC_WIFI_SUPPORT
static INT32 wmt_func_wifi_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);
static INT32 wmt_func_wifi_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);

WMT_FUNC_OPS wmt_func_wifi_ops = {
	/* Wi-Fi subsystem function on/off */
	.func_on = wmt_func_wifi_on,
	.func_off = wmt_func_wifi_off
};
#endif

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
#if CFG_FUNC_GPS_SUPPORT
CMB_PIN_CTRL_REG eediPinOhRegs[] = {
	{
	 /* pull down ctrl register */
	 .regAddr = 0x80050020,
	 .regValue = ~(0x1 << 5),
	 .regMask = 0x00000020,
	 },
	{
	 /* pull up ctrl register */
	 .regAddr = 0x80050000,
	 .regValue = 0x1 << 5,
	 .regMask = 0x00000020,
	 },
	{
	 /* iomode ctrl register */
	 .regAddr = 0x80050110,
	 .regValue = 0x1 << 0,
	 .regMask = 0x00000007,
	 },
	{
	 /* output high/low ctrl register */
	 .regAddr = 0x80050040,
	 .regValue = 0x1 << 5,
	 .regMask = 0x00000020,
	 }

};

CMB_PIN_CTRL_REG eediPinOlRegs[] = {
	{
	 .regAddr = 0x80050020,
	 .regValue = 0x1 << 5,
	 .regMask = 0x00000020UL,
	 },
	{
	 .regAddr = 0x80050000,
	 .regValue = ~(0x1 << 5),
	 .regMask = 0x00000020,
	 },
	{
	 .regAddr = 0x80050110,
	 .regValue = 0x1 << 0,
	 .regMask = 0x00000007,
	 },
	{
	 .regAddr = 0x80050040,
	 .regValue = ~(0x1 << 5),
	 .regMask = 0x00000020,
	 }
};

CMB_PIN_CTRL_REG eedoPinOhRegs[] = {
	{
	 .regAddr = 0x80050020,
	 .regValue = ~(0x1 << 7),
	 .regMask = 0x00000080UL,
	 },
	{
	 .regAddr = 0x80050000,
	 .regValue = 0x1 << 7,
	 .regMask = 0x00000080UL,
	 },
	{
	 .regAddr = 0x80050110,
	 .regValue = 0x1 << 12,
	 .regMask = 0x00007000UL,
	 },
	{
	 .regAddr = 0x80050040,
	 .regValue = 0x1 << 7,
	 .regMask = 0x00000080,
	 }
};

CMB_PIN_CTRL_REG eedoPinOlRegs[] = {
	{
	 .regAddr = 0x80050020,
	 .regValue = 0x1 << 7,
	 .regMask = 0x00000080,
	 },
	{
	 .regAddr = 0x80050000,
	 .regValue = ~(0x1 << 7),
	 .regMask = 0x00000080,
	 },
	{
	 .regAddr = 0x80050110,
	 .regValue = 0x1 << 12,
	 .regMask = 0x00007000,
	 },
	{
	 .regAddr = 0x80050040,
	 .regValue = ~(0x1 << 7),
	 .regMask = 0x00000080,
	 }

};

CMB_PIN_CTRL_REG gsyncPinOnRegs[] = {
	{
	 .regAddr = 0x80050110,
	 .regValue = 0x3 << 20,
	 .regMask = 0x7 << 20,
	 }

};

CMB_PIN_CTRL_REG gsyncPinOffRegs[] = {
	{
	 .regAddr = 0x80050110,
	 .regValue = 0x0 << 20,
	 .regMask = 0x7 << 20,
	 }
};

/* templete usage for GPIO control */
CMB_PIN_CTRL gCmbPinCtrl[3] = {
	{
	 .pinId = CMB_PIN_EEDI_ID,
	 .regNum = 4,
	 .pFuncOnArray = eediPinOhRegs,
	 .pFuncOffArray = eediPinOlRegs,
	 },
	{
	 .pinId = CMB_PIN_EEDO_ID,
	 .regNum = 4,
	 .pFuncOnArray = eedoPinOhRegs,
	 .pFuncOffArray = eedoPinOlRegs,
	 },
	{
	 .pinId = CMB_PIN_GSYNC_ID,
	 .regNum = 1,
	 .pFuncOnArray = gsyncPinOnRegs,
	 .pFuncOffArray = gsyncPinOffRegs,
	 }
};
#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#if CFG_FUNC_BT_SUPPORT

INT32 _osal_inline_ wmt_func_bt_ctrl(ENUM_FUNC_STATE funcState)
{
	/*only need to send turn BT subsystem wmt command */
	return wmt_core_func_ctrl_cmd(WMTDRV_TYPE_BT, (FUNC_ON == funcState) ? MTK_WCN_BOOL_TRUE : MTK_WCN_BOOL_FALSE);
}

INT32 wmt_func_bt_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	/* return wmt_func_bt_ctrl(FUNC_ON); */
	INT32 iRet = -1;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;

	ctrlPa1 = BT_PALDO;
	ctrlPa2 = PALDO_ON;
	iRet = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
	if (iRet) {
		WMT_ERR_FUNC("wmt-func: wmt_ctrl_soc_paldo_ctrl failed(%d)(%d)(%d)\n", iRet, ctrlPa1, ctrlPa2);
		return -1;
	}
	iRet = wmt_core_func_ctrl_cmd(WMTDRV_TYPE_BT, MTK_WCN_BOOL_TRUE);
	if (iRet) {
		WMT_ERR_FUNC("wmt-func: wmt_core_func_ctrl_cmd(bt_on) failed(%d)\n", iRet);
		ctrlPa1 = BT_PALDO;
		ctrlPa2 = PALDO_OFF;
		wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);

		/*do coredump when bt on fail */
		wmt_core_set_coredump_state(DRV_STS_FUNC_ON);
		ctrlPa1 = WMTDRV_TYPE_BT;
		ctrlPa2 = 32;
		wmt_core_ctrl(WMT_CTRL_EVT_ERR_TRG_ASSERT, &ctrlPa1, &ctrlPa2);
		return -2;
	}
	osal_set_bit(WMT_BT_ON, &gBtWifiGpsState);
	if (osal_test_bit(WMT_GPS_ON, &gBtWifiGpsState)) {
		/* send msg to GPS native for sending de-sense CMD */
		ctrlPa1 = 1;
		ctrlPa2 = 0;
		wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
	}
		return 0;
}

INT32 wmt_func_bt_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	/* return wmt_func_bt_ctrl(FUNC_OFF); */
	INT32 iRet1 = -1;
	INT32 iRet2 = -1;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;

	iRet1 = wmt_core_func_ctrl_cmd(WMTDRV_TYPE_BT, MTK_WCN_BOOL_FALSE);
	if (iRet1)
		WMT_ERR_FUNC("wmt-func: wmt_core_func_ctrl_cmd(bt_off) failed(%d)\n", iRet1);

	ctrlPa1 = BT_PALDO;
	ctrlPa2 = PALDO_OFF;
	iRet2 = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
	if (iRet2)
		WMT_ERR_FUNC("wmt-func: wmt_ctrl_soc_paldo_ctrl(bt_off) failed(%d)\n", iRet2);

	if (iRet1 + iRet2) {
		/*do coredump when bt off fail */
		wmt_core_set_coredump_state(DRV_STS_FUNC_ON);
		ctrlPa1 = WMTDRV_TYPE_BT;
		ctrlPa2 = 32;
		wmt_core_ctrl(WMT_CTRL_EVT_ERR_TRG_ASSERT, &ctrlPa1, &ctrlPa2);
		return -1;
	}

	osal_clear_bit(WMT_BT_ON, &gBtWifiGpsState);
	if ((!osal_test_bit(WMT_WIFI_ON, &gBtWifiGpsState)) && (osal_test_bit(WMT_GPS_ON, &gBtWifiGpsState))) {
		/* send msg to GPS native for stopping send de-sense CMD */
		ctrlPa1 = 0;
		ctrlPa2 = 0;
		wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
	}
	return 0;
}

#endif

#if CFG_FUNC_GPS_SUPPORT

INT32 _osal_inline_ wmt_func_gps_ctrl(ENUM_FUNC_STATE funcState)
{
	/*send turn GPS subsystem wmt command */
	return wmt_core_func_ctrl_cmd(WMTDRV_TYPE_GPS, (FUNC_ON == funcState) ? MTK_WCN_BOOL_TRUE : MTK_WCN_BOOL_FALSE);
}

INT32 wmt_func_gps_pre_ctrl(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf, ENUM_FUNC_STATE funcStatus)
{
	UINT32 i = 0;
	INT32 iRet = 0;
	UINT32 regAddr = 0;
	UINT32 regValue = 0;
	UINT32 regMask = 0;
	UINT32 regNum = 0;
	P_CMB_PIN_CTRL_REG pReg;
	P_CMB_PIN_CTRL pCmbPinCtrl = &gCmbPinCtrl[CMB_PIN_GSYNC_ID];
	WMT_CTRL_DATA ctrlData;
	WMT_IC_PIN_ID wmtIcPinId = WMT_IC_PIN_MAX;
	/* sanity check */
	if (FUNC_ON != funcStatus && FUNC_OFF != funcStatus) {
		WMT_ERR_FUNC("invalid funcStatus(%d)\n", funcStatus);
		return -1;
	}
	/* turn on GPS sync function on both side */
	ctrlData.ctrlId = WMT_CTRL_GPS_SYNC_SET;
	ctrlData.au4CtrlData[0] = (FUNC_ON == funcStatus) ? 1 : 0;
	iRet = wmt_ctrl(&ctrlData);
	if (iRet) {
		/*we suppose this would never print */
		WMT_ERR_FUNC("ctrl GPS_SYNC_SET(%d) fail, ret(%d)\n", funcStatus, iRet);
		/* TODO:[FixMe][George] error handling? */
		return -2;
	}
	WMT_INFO_FUNC("ctrl GPS_SYNC_SET(%d) ok\n", funcStatus);


	if ((NULL == pOps->ic_pin_ctrl) ||
		(0 > pOps->ic_pin_ctrl(
				WMT_IC_PIN_GSYNC,
				FUNC_ON == funcStatus ? WMT_IC_PIN_MUX : WMT_IC_PIN_GPIO,
				1))) {	/*WMT_IC_PIN_GSYNC */
		pCmbPinCtrl = &gCmbPinCtrl[CMB_PIN_GSYNC_ID];
		regNum = pCmbPinCtrl->regNum;
		for (i = 0; i < regNum; i++) {
			if (FUNC_ON == funcStatus)
				pReg = &pCmbPinCtrl->pFuncOnArray[i];
			else
				pReg = &pCmbPinCtrl->pFuncOffArray[i];

			regAddr = pReg->regAddr;
			regValue = pReg->regValue;
			regMask = pReg->regMask;

			iRet = wmt_core_reg_rw_raw(1, regAddr, &regValue, regMask);
			if (iRet) {
				WMT_ERR_FUNC("set reg for GPS_SYNC function fail(%d)\n", iRet);
				/* TODO:[FixMe][Chaozhong] error handling? */
				return -2;
			}

		}
	} else {
		WMT_INFO_FUNC("set reg for GPS_SYNC function okay by chip ic_pin_ctrl\n");
	}
	WMT_INFO_FUNC("ctrl combo chip gps sync function succeed\n");
	/* turn on GPS lna ctrl function */
	if (NULL != pConf) {
		if (0 == pConf->wmt_gps_lna_enable) {

			WMT_INFO_FUNC("host pin used for gps lna\n");
			/* host LNA ctrl pin needed */
			ctrlData.ctrlId = WMT_CTRL_GPS_LNA_SET;
			ctrlData.au4CtrlData[0] = FUNC_ON == funcStatus ? 1 : 0;
			iRet = wmt_ctrl(&ctrlData);
			if (iRet) {
				/*we suppose this would never print */
				WMT_ERR_FUNC("ctrl host GPS_LNA output high fail, ret(%d)\n", iRet);
				/* TODO:[FixMe][Chaozhong] error handling? */
				return -3;
			}
			WMT_INFO_FUNC("ctrl host gps lna function succeed\n");
		} else {
			WMT_INFO_FUNC("combo chip pin(%s) used for gps lna\n",
				      0 == pConf->wmt_gps_lna_pin ? "EEDI" : "EEDO");
			wmtIcPinId = 0 == pConf->wmt_gps_lna_pin ? WMT_IC_PIN_EEDI : WMT_IC_PIN_EEDO;
			if ((NULL == pOps->ic_pin_ctrl) ||
				(0 > pOps->ic_pin_ctrl(
					wmtIcPinId,
					FUNC_ON == funcStatus ? WMT_IC_PIN_GPIO_HIGH : WMT_IC_PIN_GPIO_LOW,
					1))) {	/*WMT_IC_PIN_GSYNC */
				if (0 == pConf->wmt_gps_lna_pin) {
					/* EEDI needed */
					pCmbPinCtrl = &gCmbPinCtrl[CMB_PIN_EEDI_ID];
				} else if (1 == pConf->wmt_gps_lna_pin) {
					/* EEDO needed */
					pCmbPinCtrl = &gCmbPinCtrl[CMB_PIN_EEDO_ID];
				}
				regNum = pCmbPinCtrl->regNum;
				for (i = 0; i < regNum; i++) {
					if (FUNC_ON == funcStatus)
						pReg = &pCmbPinCtrl->pFuncOnArray[i];
					else
						pReg = &pCmbPinCtrl->pFuncOffArray[i];
					regAddr = pReg->regAddr;
					regValue = pReg->regValue;
					regMask = pReg->regMask;

					iRet = wmt_core_reg_rw_raw(1, regAddr, &regValue, regMask);
					if (iRet) {
						WMT_ERR_FUNC("set reg for GPS_LNA function fail(%d)\n", iRet);
						/* TODO:[FixMe][Chaozhong] error handling? */
						return -3;
					}
				}
				WMT_INFO_FUNC("ctrl combo chip gps lna succeed\n");
			} else {
				WMT_INFO_FUNC("set reg for GPS_LNA function okay by chip ic_pin_ctrl\n");
			}
		}
	}
	return 0;

}

INT32 wmt_func_gps_pre_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	return wmt_func_gps_pre_ctrl(pOps, pConf, FUNC_ON);
}

INT32 wmt_func_gps_pre_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{

	return wmt_func_gps_pre_ctrl(pOps, pConf, FUNC_OFF);
}

INT32 wmt_func_gps_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	INT32 iRet = 0;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;
	UINT8 co_clock_type = (pConf->co_clock_flag & 0x0f);

	if ((co_clock_type) && (0 == pConf->wmt_gps_lna_enable)) {	/* use SOC external LNA */
		if (!osal_test_bit(WMT_FM_ON, &gGpsFmState)) {
			ctrlPa1 = GPS_PALDO;
			ctrlPa2 = PALDO_ON;
			wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
		} else {
			WMT_INFO_FUNC("LDO VCN28 has been turn on by FM\n");
		}
	}

	iRet = wmt_func_gps_pre_on(pOps, pConf);
	if (0 == iRet) {
		iRet = wmt_func_gps_ctrl(FUNC_ON);
		if (!iRet) {
			osal_set_bit(WMT_GPS_ON, &gBtWifiGpsState);
			if ((osal_test_bit(WMT_BT_ON, &gBtWifiGpsState))
			    || (osal_test_bit(WMT_WIFI_ON, &gBtWifiGpsState))) {
				/* send msg to GPS native for sending de-sense CMD */
				ctrlPa1 = 1;
				ctrlPa2 = 0;
				wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
			}

			if ((co_clock_type) && (0 == pConf->wmt_gps_lna_enable))	/* use SOC external LNA */
				osal_set_bit(WMT_GPS_ON, &gGpsFmState);
		}
	}
	return iRet;
}

INT32 wmt_func_gps_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	INT32 iRet = 0;
	unsigned long ctrlPa1 = 0;
	unsigned long ctrlPa2 = 0;
	UINT8 co_clock_type = (pConf->co_clock_flag & 0x0f);

	iRet = wmt_func_gps_pre_off(pOps, pConf);
	if (0 == iRet) {
		iRet = wmt_func_gps_ctrl(FUNC_OFF);
		if (!iRet) {
			osal_clear_bit(WMT_GPS_ON, &gBtWifiGpsState);
			if ((osal_test_bit(WMT_BT_ON, &gBtWifiGpsState))
			    || (osal_test_bit(WMT_WIFI_ON, &gBtWifiGpsState))) {
				/* send msg to GPS native for stop sending de-sense CMD */
				ctrlPa1 = 0;
				ctrlPa2 = 0;
				wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
			}
		}
	}

	if ((co_clock_type) && (0 == pConf->wmt_gps_lna_enable)) {	/* use SOC external LNA */
		if (osal_test_bit(WMT_FM_ON, &gGpsFmState))
			WMT_INFO_FUNC("FM is still on, do not turn off LDO VCN28\n");
		else {
			ctrlPa1 = GPS_PALDO;
			ctrlPa2 = PALDO_OFF;
			wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
		}

		osal_clear_bit(WMT_GPS_ON, &gGpsFmState);
	}

	return iRet;

}
#endif

#if CFG_FUNC_FM_SUPPORT

INT32 _osal_inline_ wmt_func_fm_ctrl(ENUM_FUNC_STATE funcState)
{
	/*only need to send turn FM subsystem wmt command */
	return wmt_core_func_ctrl_cmd(WMTDRV_TYPE_FM, (FUNC_ON == funcState) ? MTK_WCN_BOOL_TRUE : MTK_WCN_BOOL_FALSE);
}

INT32 wmt_func_fm_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	/* return wmt_func_fm_ctrl(FUNC_ON); */
	unsigned long ctrlPa1 = 0;
	unsigned long ctrlPa2 = 0;
	INT32 iRet = -1;
	UINT8 co_clock_type = (pConf->co_clock_flag & 0x0f);

	if (co_clock_type) {
		if (!osal_test_bit(WMT_GPS_ON, &gGpsFmState)) {
			ctrlPa1 = FM_PALDO;
			ctrlPa2 = PALDO_ON;
			wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
		} else {
			WMT_INFO_FUNC("LDO VCN28 has been turn on by GPS\n");
		}
	}

	iRet = wmt_core_func_ctrl_cmd(WMTDRV_TYPE_FM, MTK_WCN_BOOL_TRUE);
	if (!iRet) {
		if (co_clock_type)
			osal_set_bit(WMT_FM_ON, &gGpsFmState);
	}

	return iRet;
}

INT32 wmt_func_fm_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	/* return wmt_func_fm_ctrl(FUNC_OFF); */
	unsigned long ctrlPa1 = 0;
	unsigned long ctrlPa2 = 0;
	INT32 iRet = -1;
	UINT8 co_clock_type = (pConf->co_clock_flag & 0x0f);

	iRet = wmt_core_func_ctrl_cmd(WMTDRV_TYPE_FM, MTK_WCN_BOOL_FALSE);

	if (co_clock_type) {
		if (osal_test_bit(WMT_GPS_ON, &gGpsFmState)) {
			WMT_INFO_FUNC("GPS is still on, do not turn off LDO VCN28\n");
		} else {
			ctrlPa1 = FM_PALDO;
			ctrlPa2 = PALDO_OFF;
			wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
		}

		osal_clear_bit(WMT_FM_ON, &gGpsFmState);
	}

	return iRet;
}

#endif

#if CFG_FUNC_WIFI_SUPPORT

/*in soc, wmt turn on wifi directly, no not need operate SDIO*/
#if 0
INT32 wmt_func_wifi_ctrl(ENUM_FUNC_STATE funcState)
{
	INT32 iRet = 0;
	unsigned long ctrlPa1 = WMT_SDIO_FUNC_WIFI;
	unsigned long ctrlPa2 = (FUNC_ON == funcState) ? 1 : 0;	/* turn on Wi-Fi driver */

	iRet = wmt_core_ctrl(WMT_CTRL_SDIO_FUNC, &ctrlPa1, &ctrlPa2);
	if (iRet) {
		WMT_ERR_FUNC("WMT-FUNC: turn on WIFI function fail (%d)", iRet);
		return -1;
	}
	return 0;
}
#endif

INT32 wmt_func_wifi_on(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	int iRet = 0;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;

	if (NULL != mtk_wcn_wlan_probe) {

		WMT_WARN_FUNC("WMT-FUNC: wmt wlan func on before wlan probe\n");
		iRet = (*mtk_wcn_wlan_probe) ();
		if (iRet) {
			WMT_ERR_FUNC("WMT-FUNC: wmt call wlan probe fail(%d)\n", iRet);
			iRet = -1;
		} else {
			WMT_WARN_FUNC("WMT-FUNC: wmt call wlan probe ok\n");
		}
	} else {
		WMT_ERR_FUNC("WMT-FUNC: null pointer mtk_wcn_wlan_probe\n");
		gWifiProbed = 1;
		iRet = -2;
	}

	if (!iRet) {
		osal_set_bit(WMT_WIFI_ON, &gBtWifiGpsState);
		if (osal_test_bit(WMT_GPS_ON, &gBtWifiGpsState)) {
			/* send msg to GPS native for sending de-sense CMD */
			ctrlPa1 = 1;
			ctrlPa2 = 0;
			wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
		}
	}
	return iRet;
#if 0
	return wmt_func_wifi_ctrl(FUNC_ON);
#endif
}

INT32 wmt_func_wifi_off(P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf)
{
	int iRet = 0;

	unsigned long ctrlPa1 = 0;
	unsigned long ctrlPa2 = 0;

	if (NULL != mtk_wcn_wlan_remove) {

		WMT_WARN_FUNC("WMT-FUNC: wmt wlan func on before wlan remove\n");
		iRet = (*mtk_wcn_wlan_remove) ();
		if (iRet) {
			WMT_ERR_FUNC("WMT-FUNC: wmt call wlan remove fail(%d)\n", iRet);
			iRet = -1;
		} else {
			WMT_WARN_FUNC("WMT-FUNC: wmt call wlan remove ok\n");
		}
	} else {
		WMT_ERR_FUNC("WMT-FUNC: null pointer mtk_wcn_wlan_remove\n");
		iRet = -2;
	}

	if (!iRet) {
		osal_clear_bit(WMT_WIFI_ON, &gBtWifiGpsState);
		if ((!osal_test_bit(WMT_BT_ON, &gBtWifiGpsState)) && (osal_test_bit(WMT_GPS_ON, &gBtWifiGpsState))) {
			/* send msg to GPS native for stopping send de-sense CMD */
			ctrlPa1 = 0;
			ctrlPa2 = 0;
			wmt_core_ctrl(WMT_CTRL_BGW_DESENSE_CTRL, &ctrlPa1, &ctrlPa2);
		}
	}
	return iRet;
#if 0
	return wmt_func_wifi_ctrl(FUNC_OFF);
#endif
}
#endif
