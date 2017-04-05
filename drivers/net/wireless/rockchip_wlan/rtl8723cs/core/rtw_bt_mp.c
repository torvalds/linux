/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/


#include <drv_types.h>
#include <rtw_bt_mp.h>

#if defined(CONFIG_RTL8723B)
	#include <rtl8723b_hal.h>
#endif

#if defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8821A)
void MPh2c_timeout_handle(void *FunctionContext)
{
	PADAPTER pAdapter;
	PMPT_CONTEXT pMptCtx;


	RTW_INFO("[MPT], MPh2c_timeout_handle\n");

	pAdapter = (PADAPTER)FunctionContext;
	pMptCtx = &pAdapter->mppriv.mpt_ctx;

	pMptCtx->bMPh2c_timeout = _TRUE;

	if ((_FALSE == pMptCtx->MptH2cRspEvent)
	    || ((_TRUE == pMptCtx->MptH2cRspEvent)
		&& (_FALSE == pMptCtx->MptBtC2hEvent)))
		_rtw_up_sema(&pMptCtx->MPh2c_Sema);
}

u32 WaitC2Hevent(PADAPTER pAdapter, u8 *C2H_event, u32 delay_time)
{
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	pMptCtx->bMPh2c_timeout = _FALSE;

	if (pAdapter->registrypriv.mp_mode == 0) {
		RTW_INFO("[MPT], Error!! WaitC2Hevent mp_mode == 0!!\n");
		return _FALSE;
	}

	_set_timer(&pMptCtx->MPh2c_timeout_timer, delay_time);

	_rtw_down_sema(&pMptCtx->MPh2c_Sema);

	if (pMptCtx->bMPh2c_timeout == _TRUE) {
		*C2H_event = _FALSE;

		return _FALSE;
	}

	/* for safty, cancel timer here again */
	_cancel_timer_ex(&pMptCtx->MPh2c_timeout_timer);

	return _TRUE;
}

BT_CTRL_STATUS
mptbt_CheckC2hFrame(
	PADAPTER		Adapter,
	PBT_H2C			pH2c,
	PBT_EXT_C2H		pExtC2h
)
{
	BT_CTRL_STATUS	c2hStatus = BT_STATUS_C2H_SUCCESS;

	/* RTW_INFO("[MPT], MPT rsp C2H hex: %x %x %x  %x %x %x\n"), pExtC2h , pExtC2h+1 ,pExtC2h+2 ,pExtC2h+3 ,pExtC2h+4 ,pExtC2h+5); */

	RTW_INFO("[MPT], statusCode = 0x%x\n", pExtC2h->statusCode);
	RTW_INFO("[MPT], retLen = %d\n", pExtC2h->retLen);
	RTW_INFO("[MPT], opCodeVer : req/rsp=%d/%d\n", pH2c->opCodeVer, pExtC2h->opCodeVer);
	RTW_INFO("[MPT], reqNum : req/rsp=%d/%d\n", pH2c->reqNum, pExtC2h->reqNum);
	if (pExtC2h->reqNum != pH2c->reqNum) {
		c2hStatus = BT_STATUS_C2H_REQNUM_MISMATCH;
		RTW_INFO("[MPT], Error!! C2H reqNum Mismatch!!\n");
	} else if (pExtC2h->opCodeVer != pH2c->opCodeVer) {
		c2hStatus = BT_STATUS_OPCODE_L_VERSION_MISMATCH;
		RTW_INFO("[MPT], Error!! OPCode version L mismatch!!\n");
	}

	return c2hStatus;
}

BT_CTRL_STATUS
mptbt_SendH2c(
	PADAPTER	Adapter,
	PBT_H2C	pH2c,
	u2Byte		h2cCmdLen
)
{
	/* KIRQL				OldIrql = KeGetCurrentIrql(); */
	BT_CTRL_STATUS	h2cStatus = BT_STATUS_H2C_SUCCESS;
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.mpt_ctx);
	u1Byte				i;

	RTW_INFO("[MPT], mptbt_SendH2c()=========>\n");

	/* PlatformResetEvent(&pMptCtx->MptH2cRspEvent); */
	/* PlatformResetEvent(&pMptCtx->MptBtC2hEvent); */

	/*	if(OldIrql == PASSIVE_LEVEL)
	 *	{ */
	/* RTPRINT_DATA(FMPBT, FMPBT_H2C_CONTENT, ("[MPT], MPT H2C hex:\n"), pH2c, h2cCmdLen); */

	for (i = 0; i < BT_H2C_MAX_RETRY; i++) {
		RTW_INFO("[MPT], Send H2C command to wifi!!!\n");

		pMptCtx->MptH2cRspEvent = _FALSE;
		pMptCtx->MptBtC2hEvent = _FALSE;

#if defined(CONFIG_RTL8723B)
		rtl8723b_set_FwBtMpOper_cmd(Adapter, pH2c->opCode, pH2c->opCodeVer, pH2c->reqNum, pH2c->buf);
#endif
		pMptCtx->h2cReqNum++;
		pMptCtx->h2cReqNum %= 16;

		if (WaitC2Hevent(Adapter, &pMptCtx->MptH2cRspEvent, 100)) {
			RTW_INFO("[MPT], Received WiFi MptH2cRspEvent!!!\n");
			if (WaitC2Hevent(Adapter, &pMptCtx->MptBtC2hEvent, 400)) {
				RTW_INFO("[MPT], Received MptBtC2hEvent!!!\n");
				break;
			} else {
				RTW_INFO("[MPT], Error!!BT MptBtC2hEvent timeout!!\n");
				h2cStatus = BT_STATUS_H2C_BT_NO_RSP;
			}
		} else {
			RTW_INFO("[MPT], Error!!WiFi  MptH2cRspEvent timeout!!\n");
			h2cStatus = BT_STATUS_H2C_TIMTOUT;
		}
	}
	/*	}
	 *	else
	 *	{
	 * 		RT_ASSERT(FALSE, ("[MPT],  mptbt_SendH2c() can only run under PASSIVE_LEVEL!!\n"));
	 *		h2cStatus = BT_STATUS_WRONG_LEVEL;
	 *	} */

	RTW_INFO("[MPT], mptbt_SendH2c()<=========\n");
	return h2cStatus;
}



BT_CTRL_STATUS
mptbt_CheckBtRspStatus(
	PADAPTER			Adapter,
	PBT_EXT_C2H			pExtC2h
)
{
	BT_CTRL_STATUS	retStatus = BT_OP_STATUS_SUCCESS;

	switch (pExtC2h->statusCode) {
	case BT_OP_STATUS_SUCCESS:
		retStatus = BT_STATUS_BT_OP_SUCCESS;
		RTW_INFO("[MPT], BT status : BT_STATUS_SUCCESS\n");
		break;
	case BT_OP_STATUS_VERSION_MISMATCH:
		retStatus = BT_STATUS_OPCODE_L_VERSION_MISMATCH;
		RTW_INFO("[MPT], BT status : BT_STATUS_OPCODE_L_VERSION_MISMATCH\n");
		break;
	case BT_OP_STATUS_UNKNOWN_OPCODE:
		retStatus = BT_STATUS_UNKNOWN_OPCODE_L;
		RTW_INFO("[MPT], BT status : BT_STATUS_UNKNOWN_OPCODE_L\n");
		break;
	case BT_OP_STATUS_ERROR_PARAMETER:
		retStatus = BT_STATUS_PARAMETER_FORMAT_ERROR_L;
		RTW_INFO("[MPT], BT status : BT_STATUS_PARAMETER_FORMAT_ERROR_L\n");
		break;
	default:
		retStatus = BT_STATUS_UNKNOWN_STATUS_L;
		RTW_INFO("[MPT], BT status : BT_STATUS_UNKNOWN_STATUS_L\n");
		break;
	}

	return retStatus;
}



BT_CTRL_STATUS
mptbt_BtFwOpCodeProcess(
	PADAPTER		Adapter,
	u1Byte			btFwOpCode,
	u1Byte			opCodeVer,
	pu1Byte			pH2cPar,
	u1Byte			h2cParaLen
)
{
	u1Byte				H2C_Parameter[6] = {0};
	PBT_H2C				pH2c = (PBT_H2C)&H2C_Parameter[0];
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.mpt_ctx);
	PBT_EXT_C2H			pExtC2h = (PBT_EXT_C2H)&pMptCtx->c2hBuf[0];
	u2Byte				paraLen = 0, i;
	BT_CTRL_STATUS	h2cStatus = BT_STATUS_H2C_SUCCESS, c2hStatus = BT_STATUS_C2H_SUCCESS;
	BT_CTRL_STATUS	retStatus = BT_STATUS_H2C_BT_NO_RSP;

	if (Adapter->registrypriv.mp_mode == 0) {
		RTW_INFO("[MPT], Error!! mptbt_BtFwOpCodeProces mp_mode == 0!!\n");
		return _FALSE;
	}

	pH2c->opCode = btFwOpCode;
	pH2c->opCodeVer = opCodeVer;
	pH2c->reqNum = pMptCtx->h2cReqNum;
	/* PlatformMoveMemory(&pH2c->buf[0], pH2cPar, h2cParaLen); */
	/* _rtw_memcpy(&pH2c->buf[0], pH2cPar, h2cParaLen); */
	_rtw_memcpy(pH2c->buf, pH2cPar, h2cParaLen);

	RTW_INFO("[MPT], pH2c->opCode=%d\n", pH2c->opCode);
	RTW_INFO("[MPT], pH2c->opCodeVer=%d\n", pH2c->opCodeVer);
	RTW_INFO("[MPT], pH2c->reqNum=%d\n", pH2c->reqNum);
	RTW_INFO("[MPT], h2c parameter length=%d\n", h2cParaLen);
	for (i = 0; i < h2cParaLen; i++)
		RTW_INFO("[MPT], parameter[%d]=0x%02x\n", i, pH2c->buf[i]);

	h2cStatus = mptbt_SendH2c(Adapter, pH2c, h2cParaLen + 2);
	if (BT_STATUS_H2C_SUCCESS == h2cStatus) {
		/* if reach here, it means H2C get the correct c2h response, */
		c2hStatus = mptbt_CheckC2hFrame(Adapter, pH2c, pExtC2h);
		if (BT_STATUS_C2H_SUCCESS == c2hStatus)
			retStatus = mptbt_CheckBtRspStatus(Adapter, pExtC2h);
		else {
			RTW_INFO("[MPT], Error!! C2H failed for pH2c->opCode=%d\n", pH2c->opCode);
			/* check c2h status error, return error status code to upper layer. */
			retStatus = c2hStatus;
		}
	} else {
		RTW_INFO("[MPT], Error!! H2C failed for pH2c->opCode=%d\n", pH2c->opCode);
		/* check h2c status error, return error status code to upper layer. */
		retStatus = h2cStatus;
	}

	return retStatus;
}




u2Byte
mptbt_BtReady(
	PADAPTER		Adapter,
	PBT_REQ_CMD	pBtReq,
	PBT_RSP_CMD	pBtRsp
)
{
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.mpt_ctx);
	PBT_EXT_C2H			pExtC2h = (PBT_EXT_C2H)&pMptCtx->c2hBuf[0];
	u1Byte				i;
	u1Byte				btFwVer = 0, bdAddr[6] = {0};
	u2Byte				btRealFwVer = 0;
	pu2Byte			pu2Tmp = NULL;

	/*  */
	/* check upper layer parameters */
	/*  */

	/* 1. check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}

	pBtRsp->pParamStart[0] = MP_BT_NOT_READY;
	paraLen = 10;
	/*  */
	/* execute lower layer opcodes */
	/*  */

	/* Get BT FW version */
	/* fill h2c parameters */
	btOpcode = BT_LO_OP_GET_BT_VERSION;
	/* execute h2c and check respond c2h from bt fw is correct or not */
	retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	} else {
		pu2Tmp = (pu2Byte)&pExtC2h->buf[0];
		btRealFwVer = *pu2Tmp;
		btFwVer = pExtC2h->buf[1];
		RTW_INFO("[MPT], btRealFwVer=0x%x, btFwVer=0x%x\n", btRealFwVer, btFwVer);
	}

	/* Get BD Address */
	/* fill h2c parameters */
	btOpcode = BT_LO_OP_GET_BD_ADDR_L;
	/* execute h2c and check respond c2h from bt fw is correct or not */
	retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	} else {
		bdAddr[5] = pExtC2h->buf[0];
		bdAddr[4] = pExtC2h->buf[1];
		bdAddr[3] = pExtC2h->buf[2];
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_GET_BD_ADDR_H;
	/* execute h2c and check respond c2h from bt fw is correct or not */
	retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	} else {
		bdAddr[2] = pExtC2h->buf[0];
		bdAddr[1] = pExtC2h->buf[1];
		bdAddr[0] = pExtC2h->buf[2];
	}
	RTW_INFO("[MPT], Local BDAddr:");
	for (i = 0; i < 6; i++)
		RTW_INFO(" 0x%x ", bdAddr[i]);
	pBtRsp->status = BT_STATUS_SUCCESS;
	pBtRsp->pParamStart[0] = MP_BT_READY;
	pu2Tmp = (pu2Byte)&pBtRsp->pParamStart[1];
	*pu2Tmp = btRealFwVer;
	pBtRsp->pParamStart[3] = btFwVer;
	for (i = 0; i < 6; i++)
		pBtRsp->pParamStart[4 + i] = bdAddr[5 - i];

	return paraLen;
}

void mptbt_close_WiFiRF(PADAPTER Adapter)
{
	phy_set_bb_reg(Adapter, 0x824, 0xF, 0x0);
	phy_set_bb_reg(Adapter, 0x824, 0x700000, 0x0);
	phy_set_rf_reg(Adapter, RF_PATH_A, 0x0, 0xF0000, 0x0);
}

void mptbt_open_WiFiRF(PADAPTER	Adapter)
{
	phy_set_bb_reg(Adapter, 0x824, 0x700000, 0x3);
	phy_set_bb_reg(Adapter, 0x824, 0xF, 0x2);
	phy_set_rf_reg(Adapter, RF_PATH_A, 0x0, 0xF0000, 0x3);
}

u4Byte mptbt_switch_RF(PADAPTER	Adapter, u1Byte	Enter)
{
	u2Byte	tmp_2byte = 0;

	/* Enter test mode */
	if (Enter) {
		/* 1>. close WiFi RF */
		mptbt_close_WiFiRF(Adapter);

		/* 2>. change ant switch to BT */
		tmp_2byte = rtw_read16(Adapter, 0x860);
		tmp_2byte = tmp_2byte | BIT(9);
		tmp_2byte = tmp_2byte & (~BIT(8));
		rtw_write16(Adapter, 0x860, tmp_2byte);
		rtw_write16(Adapter, 0x870, 0x300);
	} else {
		/* 1>. Open WiFi RF */
		mptbt_open_WiFiRF(Adapter);

		/* 2>. change ant switch back */
		tmp_2byte = rtw_read16(Adapter, 0x860);
		tmp_2byte = tmp_2byte | BIT(8);
		tmp_2byte = tmp_2byte & (~BIT(9));
		rtw_write16(Adapter, 0x860, tmp_2byte);
		rtw_write16(Adapter, 0x870, 0x300);
	}

	return 0;
}

u2Byte
mptbt_BtSetMode(
	PADAPTER		Adapter,
	PBT_REQ_CMD	pBtReq,
	PBT_RSP_CMD	pBtRsp
)
{
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	u1Byte				btModeToSet = 0;

	/*  */
	/* check upper layer parameters */
	/*  */
	/* 1. check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}
	/* 2. check upper layer parameter length */
	if (1 == pBtReq->paraLength) {
		btModeToSet = pBtReq->pParamStart[0];
		RTW_INFO("[MPT], BtTestMode=%d\n", btModeToSet);
	} else {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d (should be 1)\n", pBtReq->paraLength);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}

	/*  */
	/* execute lower layer opcodes */
	/*  */

	/* 1. fill h2c parameters	 */
	/* check bt mode */
	btOpcode = BT_LO_OP_SET_BT_MODE;
	if (btModeToSet >= MP_BT_MODE_MAX) {
		pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		mptbt_switch_RF(Adapter, 1);

		h2cParaBuf[0] = btModeToSet;
		h2cParaLen = 1;
		/* 2. execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* 3. construct respond status code and data. */
	if (BT_STATUS_BT_OP_SUCCESS == retStatus)
		pBtRsp->status = BT_STATUS_SUCCESS;
	else {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
	}

	return paraLen;
}


VOID
MPTBT_FwC2hBtMpCtrl(
	PADAPTER	Adapter,
	pu1Byte	tmpBuf,
	u1Byte		length
)
{
	u32 i;
	PMPT_CONTEXT	pMptCtx = &(Adapter->mppriv.mpt_ctx);
	PBT_EXT_C2H pExtC2h = (PBT_EXT_C2H)tmpBuf;

	if (Adapter->bBTFWReady == _FALSE || Adapter->registrypriv.mp_mode == 0) {
		/* RTW_INFO("Ignore C2H BT MP Info since not in MP mode\n"); */
		return;
	}
	if (length > 32 || length < 3) {
		RTW_INFO("\n [MPT], pExtC2h->buf hex: length=%d > 32 || < 3\n", length);
		return;
	}

	/* cancel_timeout for h2c handle */
	_cancel_timer_ex(&pMptCtx->MPh2c_timeout_timer);

	for (i = 0; i < length; i++)
		RTW_INFO("[MPT], %s, buf[%d]=0x%02x ", __FUNCTION__, i, tmpBuf[i]);
	RTW_INFO("[MPT], pExtC2h->extendId=0x%x\n", pExtC2h->extendId);

	switch (pExtC2h->extendId) {
	case EXT_C2H_WIFI_FW_ACTIVE_RSP:
		RTW_INFO("[MPT], EXT_C2H_WIFI_FW_ACTIVE_RSP\n");
#if 0
		RTW_INFO("[MPT], pExtC2h->buf hex:\n");
		for (i = 0; i < (length - 3); i++)
			RTW_INFO(" 0x%x ", pExtC2h->buf[i]);
#endif
		if ((_FALSE == pMptCtx->bMPh2c_timeout)
		    && (_FALSE == pMptCtx->MptH2cRspEvent)) {
			pMptCtx->MptH2cRspEvent = _TRUE;
			_rtw_up_sema(&pMptCtx->MPh2c_Sema);
		}
		break;

	case EXT_C2H_TRIG_BY_BT_FW:
		RTW_INFO("[MPT], EXT_C2H_TRIG_BY_BT_FW\n");
		_rtw_memcpy(&pMptCtx->c2hBuf[0], tmpBuf, length);
		RTW_INFO("[MPT], pExtC2h->statusCode=0x%x\n", pExtC2h->statusCode);
		RTW_INFO("[MPT], pExtC2h->retLen=0x%x\n", pExtC2h->retLen);
		RTW_INFO("[MPT], pExtC2h->opCodeVer=0x%x\n", pExtC2h->opCodeVer);
		RTW_INFO("[MPT], pExtC2h->reqNum=0x%x\n", pExtC2h->reqNum);
		for (i = 0; i < (length - 3); i++)
			RTW_INFO("[MPT], pExtC2h->buf[%d]=0x%02x\n", i, pExtC2h->buf[i]);

		if ((_FALSE == pMptCtx->bMPh2c_timeout)
		    && (_TRUE == pMptCtx->MptH2cRspEvent)
		    && (_FALSE == pMptCtx->MptBtC2hEvent)) {
			pMptCtx->MptBtC2hEvent = _TRUE;
			_rtw_up_sema(&pMptCtx->MPh2c_Sema);
		}
		break;

	default:
		RTW_INFO("[MPT], EXT_C2H Target not found,pExtC2h->extendId =%d ,pExtC2h->reqNum=%d\n", pExtC2h->extendId, pExtC2h->reqNum);
		break;
	}



}


u2Byte
mptbt_BtGetGeneral(
	IN	PADAPTER		Adapter,
	IN	PBT_REQ_CMD	pBtReq,
	IN	PBT_RSP_CMD	pBtRsp
)
{
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.mpt_ctx);
	PBT_EXT_C2H		pExtC2h = (PBT_EXT_C2H)&pMptCtx->c2hBuf[0];
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode, bdAddr[6] = {0};
	u1Byte				btOpcodeVer = 0;
	u1Byte				getType = 0, i;
	u2Byte				getParaLen = 0, validParaLen = 0;
	u1Byte				regType = 0, reportType = 0;
	u4Byte				regAddr = 0, regValue = 0;
	pu4Byte			pu4Tmp;
	pu2Byte			pu2Tmp;
	pu1Byte			pu1Tmp;

	/*  */
	/* check upper layer parameters */
	/*  */

	/* check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}
	/* check upper layer parameter length */
	if (pBtReq->paraLength < 1) {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d (should larger than 1)\n", pBtReq->paraLength);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}
	getParaLen = pBtReq->paraLength - 1;
	getType = pBtReq->pParamStart[0];

	RTW_INFO("[MPT], getType=%d, getParaLen=%d\n", getType, getParaLen);

	/* check parameter first */
	switch (getType) {
	case BT_GGET_REG:
		RTW_INFO("[MPT], [BT_GGET_REG]\n");
		validParaLen = 5;
		if (getParaLen == validParaLen) {
			btOpcode = BT_LO_OP_READ_REG;
			regType = pBtReq->pParamStart[1];
			pu4Tmp = (pu4Byte)&pBtReq->pParamStart[2];
			regAddr = *pu4Tmp;
			RTW_INFO("[MPT], BT_GGET_REG regType=0x%02x, regAddr=0x%08x!!\n",
				 regType, regAddr);
			if (regType >= BT_REG_MAX) {
				pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			} else {
				if (((BT_REG_RF == regType) && (regAddr > 0x7f)) ||
				    ((BT_REG_MODEM == regType) && (regAddr > 0x1ff)) ||
				    ((BT_REG_BLUEWIZE == regType) && (regAddr > 0xfff)) ||
				    ((BT_REG_VENDOR == regType) && (regAddr > 0xfff)) ||
				    ((BT_REG_LE == regType) && (regAddr > 0xfff))) {
					pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
					return paraLen;
				}
			}
		}
		break;
	case BT_GGET_STATUS:
		RTW_INFO("[MPT], [BT_GGET_STATUS]\n");
		validParaLen = 0;
		break;
	case BT_GGET_REPORT:
		RTW_INFO("[MPT], [BT_GGET_REPORT]\n");
		validParaLen = 1;
		if (getParaLen == validParaLen) {
			reportType = pBtReq->pParamStart[1];
			RTW_INFO("[MPT], BT_GGET_REPORT reportType=0x%x!!\n", reportType);
			if (reportType >= BT_REPORT_MAX) {
				pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			}
		}
		break;
	default: {
		RTW_INFO("[MPT], Error!! getType=%d, out of range\n", getType);
		pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	}
	break;
	}
	if (getParaLen != validParaLen) {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d for BT_GET_GEN_CMD cmd id=0x%x, paraLen should=0x%x\n",
			 getParaLen, getType, validParaLen);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}

	/*  */
	/* execute lower layer opcodes */
	/*  */
	if (BT_GGET_REG == getType) {
		/* fill h2c parameters */
		/* here we should write reg value first then write the address, adviced by Austin */
		btOpcode = BT_LO_OP_READ_REG;
		h2cParaBuf[0] = regType;
		h2cParaBuf[1] = pBtReq->pParamStart[2];
		h2cParaBuf[2] = pBtReq->pParamStart[3];
		h2cParaLen = 3;
		/* execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* construct respond status code and data. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}

		pu2Tmp = (pu2Byte)&pExtC2h->buf[0];
		regValue = *pu2Tmp;
		RTW_INFO("[MPT], read reg regType=0x%02x, regAddr=0x%08x, regValue=0x%04x\n",
			 regType, regAddr, regValue);

		pu4Tmp = (pu4Byte)&pBtRsp->pParamStart[0];
		*pu4Tmp = regValue;
		paraLen = 4;
	} else if (BT_GGET_STATUS == getType) {
		btOpcode = BT_LO_OP_GET_BT_STATUS;
		h2cParaLen = 0;
		/* execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* construct respond status code and data. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}

		pBtRsp->pParamStart[0] = pExtC2h->buf[0];
		pBtRsp->pParamStart[1] = pExtC2h->buf[1];
		RTW_INFO("[MPT], read bt status, testMode=0x%x, testStatus=0x%x\n",
			 pBtRsp->pParamStart[0], pBtRsp->pParamStart[1]);
		paraLen = 2;
	} else if (BT_GGET_REPORT == getType) {
		switch (reportType) {
		case BT_REPORT_RX_PACKET_CNT: {
			RTW_INFO("[MPT], [Rx Packet Counts]\n");
			btOpcode = BT_LO_OP_GET_RX_PKT_CNT_L;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[0] = pExtC2h->buf[0];
			pBtRsp->pParamStart[1] = pExtC2h->buf[1];

			btOpcode = BT_LO_OP_GET_RX_PKT_CNT_H;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[2] = pExtC2h->buf[0];
			pBtRsp->pParamStart[3] = pExtC2h->buf[1];
			paraLen = 4;
		}
		break;
		case BT_REPORT_RX_ERROR_BITS: {
			RTW_INFO("[MPT], [Rx Error Bits]\n");
			btOpcode = BT_LO_OP_GET_RX_ERROR_BITS_L;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[0] = pExtC2h->buf[0];
			pBtRsp->pParamStart[1] = pExtC2h->buf[1];

			btOpcode = BT_LO_OP_GET_RX_ERROR_BITS_H;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[2] = pExtC2h->buf[0];
			pBtRsp->pParamStart[3] = pExtC2h->buf[1];
			paraLen = 4;
		}
		break;
		case BT_REPORT_RSSI: {
			RTW_INFO("[MPT], [RSSI]\n");
			btOpcode = BT_LO_OP_GET_RSSI;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[0] = pExtC2h->buf[0];
			pBtRsp->pParamStart[1] = pExtC2h->buf[1];
			paraLen = 2;
		}
		break;
		case BT_REPORT_CFO_HDR_QUALITY: {
			RTW_INFO("[MPT], [CFO & Header Quality]\n");
			btOpcode = BT_LO_OP_GET_CFO_HDR_QUALITY_L;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[0] = pExtC2h->buf[0];
			pBtRsp->pParamStart[1] = pExtC2h->buf[1];

			btOpcode = BT_LO_OP_GET_CFO_HDR_QUALITY_H;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			pBtRsp->pParamStart[2] = pExtC2h->buf[0];
			pBtRsp->pParamStart[3] = pExtC2h->buf[1];
			paraLen = 4;
		}
		break;
		case BT_REPORT_CONNECT_TARGET_BD_ADDR: {
			RTW_INFO("[MPT], [Connected Target BD ADDR]\n");
			btOpcode = BT_LO_OP_GET_TARGET_BD_ADDR_L;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			bdAddr[5] = pExtC2h->buf[0];
			bdAddr[4] = pExtC2h->buf[1];
			bdAddr[3] = pExtC2h->buf[2];

			btOpcode = BT_LO_OP_GET_TARGET_BD_ADDR_H;
			h2cParaLen = 0;
			/* execute h2c and check respond c2h from bt fw is correct or not */
			retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
			/* construct respond status code and data. */
			if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
				pBtRsp->status = ((btOpcode << 8) | retStatus);
				RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
				return paraLen;
			}
			bdAddr[2] = pExtC2h->buf[0];
			bdAddr[1] = pExtC2h->buf[1];
			bdAddr[0] = pExtC2h->buf[2];

			RTW_INFO("[MPT], Connected Target BDAddr:%s", bdAddr);
			for (i = 0; i < 6; i++)
				pBtRsp->pParamStart[i] = bdAddr[5 - i];
			paraLen = 6;
		}
		break;
		default:
			pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
			return paraLen;
			break;
		}
	}

	pBtRsp->status = BT_STATUS_SUCCESS;
	return paraLen;
}



u2Byte
mptbt_BtSetGeneral(
	IN	PADAPTER		Adapter,
	IN	PBT_REQ_CMD	pBtReq,
	IN	PBT_RSP_CMD	pBtRsp
)
{
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	u1Byte				setType = 0;
	u2Byte				setParaLen = 0, validParaLen = 0;
	u1Byte				regType = 0, bdAddr[6] = {0}, calVal = 0;
	u4Byte				regAddr = 0, regValue = 0;
	pu4Byte			pu4Tmp;
	pu2Byte			pu2Tmp;
	pu1Byte			pu1Tmp;

	/*  */
	/* check upper layer parameters */
	/*  */

	/* check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}
	/* check upper layer parameter length */
	if (pBtReq->paraLength < 1) {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d (should larger than 1)\n", pBtReq->paraLength);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}
	setParaLen = pBtReq->paraLength - 1;
	setType = pBtReq->pParamStart[0];

	RTW_INFO("[MPT], setType=%d, setParaLen=%d\n", setType, setParaLen);

	/* check parameter first */
	switch (setType) {
	case BT_GSET_REG:
		RTW_INFO("[MPT], [BT_GSET_REG]\n");
		validParaLen = 9;
		if (setParaLen == validParaLen) {
			btOpcode = BT_LO_OP_WRITE_REG_VALUE;
			regType = pBtReq->pParamStart[1];
			pu4Tmp = (pu4Byte)&pBtReq->pParamStart[2];
			regAddr = *pu4Tmp;
			pu4Tmp = (pu4Byte)&pBtReq->pParamStart[6];
			regValue = *pu4Tmp;
			RTW_INFO("[MPT], BT_GSET_REG regType=0x%x, regAddr=0x%x, regValue=0x%x!!\n",
				 regType, regAddr, regValue);
			if (regType >= BT_REG_MAX) {
				pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			} else {
				if (((BT_REG_RF == regType) && (regAddr > 0x7f)) ||
				    ((BT_REG_MODEM == regType) && (regAddr > 0x1ff)) ||
				    ((BT_REG_BLUEWIZE == regType) && (regAddr > 0xfff)) ||
				    ((BT_REG_VENDOR == regType) && (regAddr > 0xfff)) ||
				    ((BT_REG_LE == regType) && (regAddr > 0xfff))) {
					pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
					return paraLen;
				}
			}
		}
		break;
	case BT_GSET_RESET:
		RTW_INFO("[MPT], [BT_GSET_RESET]\n");
		validParaLen = 0;
		break;
	case BT_GSET_TARGET_BD_ADDR:
		RTW_INFO("[MPT], [BT_GSET_TARGET_BD_ADDR]\n");
		validParaLen = 6;
		if (setParaLen == validParaLen) {
			btOpcode = BT_LO_OP_SET_TARGET_BD_ADDR_H;
			if ((pBtReq->pParamStart[1] == 0) &&
			    (pBtReq->pParamStart[2] == 0) &&
			    (pBtReq->pParamStart[3] == 0) &&
			    (pBtReq->pParamStart[4] == 0) &&
			    (pBtReq->pParamStart[5] == 0) &&
			    (pBtReq->pParamStart[6] == 0)) {
				RTW_INFO("[MPT], Error!! targetBDAddr=all zero\n");
				pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			}
			if ((pBtReq->pParamStart[1] == 0xff) &&
			    (pBtReq->pParamStart[2] == 0xff) &&
			    (pBtReq->pParamStart[3] == 0xff) &&
			    (pBtReq->pParamStart[4] == 0xff) &&
			    (pBtReq->pParamStart[5] == 0xff) &&
			    (pBtReq->pParamStart[6] == 0xff)) {
				RTW_INFO("[MPT], Error!! targetBDAddr=all 0xf\n");
				pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			}
			bdAddr[0] = pBtReq->pParamStart[6];
			bdAddr[1] = pBtReq->pParamStart[5];
			bdAddr[2] = pBtReq->pParamStart[4];
			bdAddr[3] = pBtReq->pParamStart[3];
			bdAddr[4] = pBtReq->pParamStart[2];
			bdAddr[5] = pBtReq->pParamStart[1];
			RTW_INFO("[MPT], target BDAddr:%x,%x,%x,%x,%x,%x\n",
				bdAddr[0], bdAddr[1], bdAddr[2], bdAddr[3], bdAddr[4], bdAddr[5]);
		}
		break;
	case BT_GSET_TX_PWR_FINETUNE:
		RTW_INFO("[MPT], [BT_GSET_TX_PWR_FINETUNE]\n");
		validParaLen = 1;
		if (setParaLen == validParaLen) {
			btOpcode = BT_LO_OP_SET_TX_POWER_CALIBRATION;
			calVal = pBtReq->pParamStart[1];
			if ((calVal < 1) || (calVal > 9)) {
				pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
				return paraLen;
			}
			RTW_INFO("[MPT], calVal=%d\n", calVal);
		}
		break;
	case BT_SET_TRACKING_INTERVAL:
		RTW_INFO("[MPT], [BT_SET_TRACKING_INTERVAL] setParaLen =%d\n", setParaLen);

		validParaLen = 1;
		if (setParaLen == validParaLen)
			calVal = pBtReq->pParamStart[1];
		break;
	case BT_SET_THERMAL_METER:
		RTW_INFO("[MPT], [BT_SET_THERMAL_METER] setParaLen =%d\n", setParaLen);
		validParaLen = 1;
		if (setParaLen == validParaLen)
			calVal = pBtReq->pParamStart[1];
		break;
	case BT_ENABLE_CFO_TRACKING:
		RTW_INFO("[MPT], [BT_ENABLE_CFO_TRACKING] setParaLen =%d\n", setParaLen);
		validParaLen = 1;
		if (setParaLen == validParaLen)
			calVal = pBtReq->pParamStart[1];
		break;
	case BT_GSET_UPDATE_BT_PATCH:

		break;
	default: {
		RTW_INFO("[MPT], Error!! setType=%d, out of range\n", setType);
		pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	}
	break;
	}
	if (setParaLen != validParaLen) {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d for BT_SET_GEN_CMD cmd id=0x%x, paraLen should=0x%x\n",
			 setParaLen, setType, validParaLen);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}

	/*  */
	/* execute lower layer opcodes */
	/*  */
	if (BT_GSET_REG == setType) {
		/* fill h2c parameters */
		/* here we should write reg value first then write the address, adviced by Austin */
		btOpcode = BT_LO_OP_WRITE_REG_VALUE;
		h2cParaBuf[0] = pBtReq->pParamStart[6];
		h2cParaBuf[1] = pBtReq->pParamStart[7];
		h2cParaBuf[2] = pBtReq->pParamStart[8];
		h2cParaLen = 3;
		/* execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* construct respond status code and data. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}

		/* write reg address */
		btOpcode = BT_LO_OP_WRITE_REG_ADDR;
		h2cParaBuf[0] = regType;
		h2cParaBuf[1] = pBtReq->pParamStart[2];
		h2cParaBuf[2] = pBtReq->pParamStart[3];
		h2cParaLen = 3;
		/* execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* construct respond status code and data. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_GSET_RESET == setType) {
		btOpcode = BT_LO_OP_RESET;
		h2cParaLen = 0;
		/* execute h2c and check respond c2h from bt fw is correct or not */
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* construct respond status code and data. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_GSET_TARGET_BD_ADDR == setType) {
		/* fill h2c parameters */
		btOpcode = BT_LO_OP_SET_TARGET_BD_ADDR_L;
		h2cParaBuf[0] = pBtReq->pParamStart[1];
		h2cParaBuf[1] = pBtReq->pParamStart[2];
		h2cParaBuf[2] = pBtReq->pParamStart[3];
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}

		btOpcode = BT_LO_OP_SET_TARGET_BD_ADDR_H;
		h2cParaBuf[0] = pBtReq->pParamStart[4];
		h2cParaBuf[1] = pBtReq->pParamStart[5];
		h2cParaBuf[2] = pBtReq->pParamStart[6];
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_GSET_TX_PWR_FINETUNE == setType) {
		/* fill h2c parameters */
		btOpcode = BT_LO_OP_SET_TX_POWER_CALIBRATION;
		h2cParaBuf[0] = calVal;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_SET_TRACKING_INTERVAL == setType) {
		/*	BT_LO_OP_SET_TRACKING_INTERVAL								= 0x22, */
		/*	BT_LO_OP_SET_THERMAL_METER									= 0x23, */
		/*	BT_LO_OP_ENABLE_CFO_TRACKING									= 0x24, */
		btOpcode = BT_LO_OP_SET_TRACKING_INTERVAL;
		h2cParaBuf[0] = calVal;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_SET_THERMAL_METER == setType) {
		btOpcode = BT_LO_OP_SET_THERMAL_METER;
		h2cParaBuf[0] = calVal;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	} else if (BT_ENABLE_CFO_TRACKING == setType) {
		btOpcode = BT_LO_OP_ENABLE_CFO_TRACKING;
		h2cParaBuf[0] = calVal;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
		/* ckeck bt return status. */
		if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
			pBtRsp->status = ((btOpcode << 8) | retStatus);
			RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
			return paraLen;
		}
	}

	pBtRsp->status = BT_STATUS_SUCCESS;
	return paraLen;
}



u2Byte
mptbt_BtSetTxRxPars(
	IN	PADAPTER		Adapter,
	IN	PBT_REQ_CMD	pBtReq,
	IN	PBT_RSP_CMD	pBtRsp
)
{
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	PBT_TXRX_PARAMETERS pTxRxPars = (PBT_TXRX_PARAMETERS)&pBtReq->pParamStart[0];
	u2Byte				lenTxRx = sizeof(BT_TXRX_PARAMETERS);
	u1Byte				i;
	u1Byte				bdAddr[6] = {0};

	/*  */
	/* check upper layer parameters */
	/*  */

	/* 1. check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}
	/* 2. check upper layer parameter length */
	if (pBtReq->paraLength == sizeof(BT_TXRX_PARAMETERS)) {
		RTW_INFO("[MPT], pTxRxPars->txrxChannel=0x%x\n", pTxRxPars->txrxChannel);
		RTW_INFO("[MPT], pTxRxPars->txrxTxPktCnt=0x%8x\n", pTxRxPars->txrxTxPktCnt);
		RTW_INFO("[MPT], pTxRxPars->txrxTxPktInterval=0x%x\n", pTxRxPars->txrxTxPktInterval);
		RTW_INFO("[MPT], pTxRxPars->txrxPayloadType=0x%x\n", pTxRxPars->txrxPayloadType);
		RTW_INFO("[MPT], pTxRxPars->txrxPktType=0x%x\n", pTxRxPars->txrxPktType);
		RTW_INFO("[MPT], pTxRxPars->txrxPayloadLen=0x%x\n", pTxRxPars->txrxPayloadLen);
		RTW_INFO("[MPT], pTxRxPars->txrxPktHeader=0x%x\n", pTxRxPars->txrxPktHeader);
		RTW_INFO("[MPT], pTxRxPars->txrxWhitenCoeff=0x%x\n", pTxRxPars->txrxWhitenCoeff);
		bdAddr[0] = pTxRxPars->txrxBdaddr[5];
		bdAddr[1] = pTxRxPars->txrxBdaddr[4];
		bdAddr[2] = pTxRxPars->txrxBdaddr[3];
		bdAddr[3] = pTxRxPars->txrxBdaddr[2];
		bdAddr[4] = pTxRxPars->txrxBdaddr[1];
		bdAddr[5] = pTxRxPars->txrxBdaddr[0];
		RTW_INFO("[MPT], pTxRxPars->txrxBdaddr: %s", &bdAddr[0]);
		RTW_INFO("[MPT], pTxRxPars->txrxTxGainIndex=0x%x\n", pTxRxPars->txrxTxGainIndex);
	} else {
		RTW_INFO("[MPT], Error!! pBtReq->paraLength=%d, correct Len=%d\n", pBtReq->paraLength, lenTxRx);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}

	/*  */
	/* execute lower layer opcodes */
	/*  */

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_PKT_HEADER;
	if (pTxRxPars->txrxPktHeader > 0x3ffff) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxPktHeader=0x%x is out of range, (should be between 0x0~0x3ffff)\n", pTxRxPars->txrxPktHeader);
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		h2cParaBuf[0] = (u1Byte)(pTxRxPars->txrxPktHeader & 0xff);
		h2cParaBuf[1] = (u1Byte)((pTxRxPars->txrxPktHeader & 0xff00) >> 8);
		h2cParaBuf[2] = (u1Byte)((pTxRxPars->txrxPktHeader & 0xff0000) >> 16);
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_PKT_TYPE_LEN;
	{
		u2Byte	payloadLenLimit = 0;
		switch (pTxRxPars->txrxPktType) {
		case MP_BT_PKT_DH1:
			payloadLenLimit = 27 * 8;
			break;
		case MP_BT_PKT_DH3:
			payloadLenLimit = 183 * 8;
			break;
		case MP_BT_PKT_DH5:
			payloadLenLimit = 339 * 8;
			break;
		case MP_BT_PKT_2DH1:
			payloadLenLimit = 54 * 8;
			break;
		case MP_BT_PKT_2DH3:
			payloadLenLimit = 367 * 8;
			break;
		case MP_BT_PKT_2DH5:
			payloadLenLimit = 679 * 8;
			break;
		case MP_BT_PKT_3DH1:
			payloadLenLimit = 83 * 8;
			break;
		case MP_BT_PKT_3DH3:
			payloadLenLimit = 552 * 8;
			break;
		case MP_BT_PKT_3DH5:
			payloadLenLimit = 1021 * 8;
			break;
		case MP_BT_PKT_LE:
			payloadLenLimit = 39 * 8;
			break;
		default: {
			RTW_INFO("[MPT], Error!! Unknown pTxRxPars->txrxPktType=0x%x\n", pTxRxPars->txrxPktType);
			pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
			return paraLen;
		}
		break;
		}

		if (pTxRxPars->txrxPayloadLen > payloadLenLimit) {
			RTW_INFO("[MPT], Error!! pTxRxPars->txrxPayloadLen=0x%x, (should smaller than %d)\n",
				 pTxRxPars->txrxPayloadLen, payloadLenLimit);
			pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
			return paraLen;
		}

		h2cParaBuf[0] = pTxRxPars->txrxPktType;
		h2cParaBuf[1] = (u1Byte)((pTxRxPars->txrxPayloadLen & 0xff));
		h2cParaBuf[2] = (u1Byte)((pTxRxPars->txrxPayloadLen & 0xff00) >> 8);
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_PKT_CNT_L_PL_TYPE;
	if (pTxRxPars->txrxPayloadType > MP_BT_PAYLOAD_MAX) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxPayloadType=0x%x, (should be between 0~4)\n", pTxRxPars->txrxPayloadType);
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		h2cParaBuf[0] = (u1Byte)((pTxRxPars->txrxTxPktCnt & 0xff));
		h2cParaBuf[1] = (u1Byte)((pTxRxPars->txrxTxPktCnt & 0xff00) >> 8);
		h2cParaBuf[2] = pTxRxPars->txrxPayloadType;
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_PKT_CNT_H_PKT_INTV;
	if (pTxRxPars->txrxTxPktInterval > 15) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxTxPktInterval=0x%x, (should be between 0~15)\n", pTxRxPars->txrxTxPktInterval);
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		h2cParaBuf[0] = (u1Byte)((pTxRxPars->txrxTxPktCnt & 0xff0000) >> 16);
		h2cParaBuf[1] = (u1Byte)((pTxRxPars->txrxTxPktCnt & 0xff000000) >> 24);
		h2cParaBuf[2] = pTxRxPars->txrxTxPktInterval;
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_WHITENCOEFF;
	{
		h2cParaBuf[0] = pTxRxPars->txrxWhitenCoeff;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}


	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_CHNL_TX_GAIN;
	if ((pTxRxPars->txrxChannel > 78) ||
	    (pTxRxPars->txrxTxGainIndex > 7)) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxChannel=0x%x, (should be between 0~78)\n", pTxRxPars->txrxChannel);
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxTxGainIndex=0x%x, (should be between 0~7)\n", pTxRxPars->txrxTxGainIndex);
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		h2cParaBuf[0] = pTxRxPars->txrxChannel;
		h2cParaBuf[1] = pTxRxPars->txrxTxGainIndex;
		h2cParaLen = 2;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	/* fill h2c parameters */
	btOpcode = BT_LO_OP_SET_BD_ADDR_L;
	if ((pTxRxPars->txrxBdaddr[0] == 0) &&
	    (pTxRxPars->txrxBdaddr[1] == 0) &&
	    (pTxRxPars->txrxBdaddr[2] == 0) &&
	    (pTxRxPars->txrxBdaddr[3] == 0) &&
	    (pTxRxPars->txrxBdaddr[4] == 0) &&
	    (pTxRxPars->txrxBdaddr[5] == 0)) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxBdaddr=all zero\n");
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	}
	if ((pTxRxPars->txrxBdaddr[0] == 0xff) &&
	    (pTxRxPars->txrxBdaddr[1] == 0xff) &&
	    (pTxRxPars->txrxBdaddr[2] == 0xff) &&
	    (pTxRxPars->txrxBdaddr[3] == 0xff) &&
	    (pTxRxPars->txrxBdaddr[4] == 0xff) &&
	    (pTxRxPars->txrxBdaddr[5] == 0xff)) {
		RTW_INFO("[MPT], Error!! pTxRxPars->txrxBdaddr=all 0xf\n");
		pBtRsp->status = (btOpcode << 8) | BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	}

	{
		h2cParaBuf[0] = pTxRxPars->txrxBdaddr[0];
		h2cParaBuf[1] = pTxRxPars->txrxBdaddr[1];
		h2cParaBuf[2] = pTxRxPars->txrxBdaddr[2];
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}
	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	btOpcode = BT_LO_OP_SET_BD_ADDR_H;
	{
		h2cParaBuf[0] = pTxRxPars->txrxBdaddr[3];
		h2cParaBuf[1] = pTxRxPars->txrxBdaddr[4];
		h2cParaBuf[2] = pTxRxPars->txrxBdaddr[5];
		h2cParaLen = 3;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}
	/* ckeck bt return status. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	pBtRsp->status = BT_STATUS_SUCCESS;
	return paraLen;
}



u2Byte
mptbt_BtTestCtrl(
	IN	PADAPTER		Adapter,
	IN	PBT_REQ_CMD	pBtReq,
	IN	PBT_RSP_CMD	pBtRsp
)
{
	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	u1Byte				testCtrl = 0;

	/*  */
	/* check upper layer parameters */
	/*  */

	/* 1. check upper layer opcode version */
	if (pBtReq->opCodeVer != 1) {
		RTW_INFO("[MPT], Error!! Upper OP code version not match!!!\n");
		pBtRsp->status = BT_STATUS_OPCODE_U_VERSION_MISMATCH;
		return paraLen;
	}
	/* 2. check upper layer parameter length */
	if (1 == pBtReq->paraLength) {
		testCtrl = pBtReq->pParamStart[0];
		RTW_INFO("[MPT], testCtrl=%d\n", testCtrl);
	} else {
		RTW_INFO("[MPT], Error!! wrong parameter length=%d (should be 1)\n", pBtReq->paraLength);
		pBtRsp->status = BT_STATUS_PARAMETER_FORMAT_ERROR_U;
		return paraLen;
	}

	/*  */
	/* execute lower layer opcodes */
	/*  */

	/* 1. fill h2c parameters	 */
	/* check bt mode */
	btOpcode = BT_LO_OP_TEST_CTRL;
	if (testCtrl >= MP_BT_TEST_MAX) {
		RTW_INFO("[MPT], Error!! testCtrl=0x%x, (should be between smaller or equal to 0x%x)\n",
			 testCtrl, MP_BT_TEST_MAX - 1);
		pBtRsp->status = BT_STATUS_PARAMETER_OUT_OF_RANGE_U;
		return paraLen;
	} else {
		h2cParaBuf[0] = testCtrl;
		h2cParaLen = 1;
		retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen);
	}

	/* 3. construct respond status code and data. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	pBtRsp->status = BT_STATUS_SUCCESS;
	return paraLen;
}


u2Byte
mptbt_TestBT(
	IN	PADAPTER		Adapter,
	IN	PBT_REQ_CMD	pBtReq,
	IN	PBT_RSP_CMD	pBtRsp
)
{

	u1Byte				h2cParaBuf[6] = {0};
	u1Byte				h2cParaLen = 0;
	u2Byte				paraLen = 0;
	u1Byte				retStatus = BT_STATUS_BT_OP_SUCCESS;
	u1Byte				btOpcode;
	u1Byte				btOpcodeVer = 0;
	u1Byte				testCtrl = 0;

	/* 1. fill h2c parameters	 */
	btOpcode =  0x11;
	h2cParaBuf[0] = 0x11;
	h2cParaBuf[1] = 0x0;
	h2cParaBuf[2] = 0x0;
	h2cParaBuf[3] = 0x0;
	h2cParaBuf[4] = 0x0;
	h2cParaLen = 1;
	/*	retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, &h2cParaBuf[0], h2cParaLen); */
	retStatus = mptbt_BtFwOpCodeProcess(Adapter, btOpcode, btOpcodeVer, h2cParaBuf, h2cParaLen);


	/* 3. construct respond status code and data. */
	if (BT_STATUS_BT_OP_SUCCESS != retStatus) {
		pBtRsp->status = ((btOpcode << 8) | retStatus);
		RTW_INFO("[MPT], Error!! status code=0x%x\n", pBtRsp->status);
		return paraLen;
	}

	pBtRsp->status = BT_STATUS_SUCCESS;
	return paraLen;
}

VOID
mptbt_BtControlProcess(
	PADAPTER	Adapter,
	PVOID		pInBuf
)
{
	u1Byte			H2C_Parameter[6] = {0};
	PBT_H2C		pH2c = (PBT_H2C)&H2C_Parameter[0];
	PMPT_CONTEXT	pMptCtx = &(Adapter->mppriv.mpt_ctx);
	PBT_REQ_CMD	pBtReq = (PBT_REQ_CMD)pInBuf;
	PBT_RSP_CMD	pBtRsp;
	u1Byte			i;


	RTW_INFO("[MPT], mptbt_BtControlProcess()=========>\n");

	RTW_INFO("[MPT], input opCodeVer=%d\n", pBtReq->opCodeVer);
	RTW_INFO("[MPT], input OpCode=%d\n", pBtReq->OpCode);
	RTW_INFO("[MPT], paraLength=%d\n", pBtReq->paraLength);
	if (pBtReq->paraLength) {
		/* RTW_INFO("[MPT], parameters(hex):0x%x %d\n",&pBtReq->pParamStart[0], pBtReq->paraLength); */
	}

	_rtw_memset((void *)pMptCtx->mptOutBuf, 0, 100);
	pMptCtx->mptOutLen = 4; /* length of (BT_RSP_CMD.status+BT_RSP_CMD.paraLength) */

	pBtRsp = (PBT_RSP_CMD)pMptCtx->mptOutBuf;
	pBtRsp->status = BT_STATUS_SUCCESS;
	pBtRsp->paraLength = 0x0;

	/* The following we should maintain the User OP codes sent by upper layer */
	switch (pBtReq->OpCode) {
	case BT_UP_OP_BT_READY:
		RTW_INFO("[MPT], OPcode : [BT_READY]\n");
		pBtRsp->paraLength = mptbt_BtReady(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_BT_SET_MODE:
		RTW_INFO("[MPT], OPcode : [BT_SET_MODE]\n");
		pBtRsp->paraLength = mptbt_BtSetMode(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_BT_SET_TX_RX_PARAMETER:
		RTW_INFO("[MPT], OPcode : [BT_SET_TXRX_PARAMETER]\n");
		pBtRsp->paraLength = mptbt_BtSetTxRxPars(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_BT_SET_GENERAL:
		RTW_INFO("[MPT], OPcode : [BT_SET_GENERAL]\n");
		pBtRsp->paraLength = mptbt_BtSetGeneral(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_BT_GET_GENERAL:
		RTW_INFO("[MPT], OPcode : [BT_GET_GENERAL]\n");
		pBtRsp->paraLength = mptbt_BtGetGeneral(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_BT_TEST_CTRL:
		RTW_INFO("[MPT], OPcode : [BT_TEST_CTRL]\n");
		pBtRsp->paraLength = mptbt_BtTestCtrl(Adapter, pBtReq, pBtRsp);
		break;
	case BT_UP_OP_TEST_BT:
		RTW_INFO("[MPT], OPcode : [TEST_BT]\n");
		pBtRsp->paraLength = mptbt_TestBT(Adapter, pBtReq, pBtRsp);
		break;
	default:
		RTW_INFO("[MPT], Error!! OPcode : UNDEFINED!!!!\n");
		pBtRsp->status = BT_STATUS_UNKNOWN_OPCODE_U;
		pBtRsp->paraLength = 0x0;
		break;
	}

	pMptCtx->mptOutLen += pBtRsp->paraLength;

	RTW_INFO("[MPT], pMptCtx->mptOutLen=%d, pBtRsp->paraLength=%d\n", pMptCtx->mptOutLen, pBtRsp->paraLength);
	RTW_INFO("[MPT], mptbt_BtControlProcess()<=========\n");
}

#endif
