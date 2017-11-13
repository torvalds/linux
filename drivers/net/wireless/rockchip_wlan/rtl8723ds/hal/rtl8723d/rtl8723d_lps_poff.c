/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#include <rtl8723d_hal.h>

#ifdef CONFIG_LPS_POFF
/****************************************************************************
 * Function: constuct Register Setting for HW to Backup Before LPS
 *		    page 2/4/6/7/8~F and page 0x24(for NAN)
*****************************************************************************/
static bool hal_construct_poff_static_file(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *lps_poff_info = NULL;
	u8	*staticFile = NULL;
	u8	*start_at = NULL;
	u8	page_count = 0, page_offset = 0, round = 0;
	/*There are 256 bytes in each register bank, and  64 dwords*/
	u8	total = 256 / 4;
	u16	offset = 0, addr_value = 0;

	if (pwrpriv->plps_poff_info == NULL) {
		RTW_INFO("%s: please alloc plps_poff_info first!!\n", __func__);
		return _FALSE;
	}

	lps_poff_info = pwrpriv->plps_poff_info;

	if (lps_poff_info->pStaticFile == NULL) {
		RTW_INFO("%s: please alloc static configure file first!!\n",
			 __func__);
		return _FALSE;
	}
	staticFile = lps_poff_info->pStaticFile + TXDESC_SIZE;

	for (page_count = 2 ; page_count < 16 ; page_count++) {
		page_offset = 0;
		if (page_count == 3 || page_count == 5)
			continue;

		offset = (round << 9);

		for (page_offset = 0 ; page_offset < total ; page_offset++) {
			start_at = staticFile + offset + (page_offset << 3);
			addr_value =
				((page_count << 8) + (page_offset << 2)) >> 2;

			SET_HOIE_ENTRY_LOW_DATA(start_at, 0);
			SET_HOIE_ENTRY_HIGH_DATA(start_at, 0);
			SET_HOIE_ENTRY_MODE_SELECT(start_at, 0);
			SET_HOIE_ENTRY_ADDRESS(start_at, addr_value);
			SET_HOIE_ENTRY_BYTE_MASK(start_at, 0xF);
			SET_HOIE_ENTRY_IO_LOCK(start_at, 0);
			SET_HOIE_ENTRY_WR_EN(start_at, 1);
			SET_HOIE_ENTRY_RD_EN(start_at, 1);
			SET_HOIE_ENTRY_RAW_RW(start_at, 0);
			SET_HOIE_ENTRY_RAW(start_at, 0);
			SET_HOIE_ENTRY_IO_DELAY(start_at, 0);
		}

		round++;
	}

	/*construct page 24*/
	offset = (round << 9);
	for (page_offset = 0 ; page_offset < total ; page_offset++) {
		start_at = staticFile + offset + (page_offset << 3);
		addr_value = ((0x24 << 8) + (page_offset << 2)) >> 2;

		SET_HOIE_ENTRY_LOW_DATA(start_at, 0);
		SET_HOIE_ENTRY_HIGH_DATA(start_at, 0);
		SET_HOIE_ENTRY_MODE_SELECT(start_at, 0);
		SET_HOIE_ENTRY_ADDRESS(start_at, addr_value);
		SET_HOIE_ENTRY_BYTE_MASK(start_at, 0xF);
		SET_HOIE_ENTRY_IO_LOCK(start_at, 0);
		SET_HOIE_ENTRY_WR_EN(start_at, 1);
		SET_HOIE_ENTRY_RD_EN(start_at, 1);
		SET_HOIE_ENTRY_RAW_RW(start_at, 0);
		SET_HOIE_ENTRY_RAW(start_at, 0);
		SET_HOIE_ENTRY_IO_DELAY(start_at, 0);
	}
	RTW_INFO("%s: (round << 9) + (PageOffset << 3) = %#08x\n",
		 __func__, offset + (page_offset << 3));

	start_at = staticFile + offset + (page_offset << 3);
	/* add last command: 00 00 00 00 00 40 30 00,suggested by DD */
	*(start_at) = 0;
	*(start_at + 1) = 0;
	*(start_at + 2) = 0;
	*(start_at + 3) = 0;
	*(start_at + 4) = 0;
	*(start_at + 5) = 0x40;
	*(start_at + 6) = 0x30;
	*(start_at + 7) = 0;

	return _TRUE;
}

/****************************************************************************
Function: send Location of configuration file and other info for FW
*****************************************************************************/
static void rtl8723d_lps_poff_h2c_param(PADAPTER padapter, u8 tx_bndy, u16 len,
					bool isDynamic)
{
	u8 lps_poff_param[H2C_LPS_POFF_PARAM_LEN] = {0};
	u8 start_addr_l = 0, start_addr_h = 0;
	u8 end_addr_l = 0, end_addr_h = 0;
	u16 start_addr = 0, end_addr = 0;

	if (len < 8)
		return;
	/*
	set start address, The parameter is entrys. every page has 16 entrys
	The Tx Descriptor is 40Byte which locate 5 entries
	*/

	start_addr = (tx_bndy << 4) + 5;
	end_addr = start_addr + (len >> 3) - 1;

	RTW_INFO("%s: start_addr = %#02X, end_addr= %#02X\n",
		 __func__, start_addr, end_addr);

	start_addr_l = (u8)start_addr;
	start_addr_h = (u8)(start_addr >> 8);

	end_addr_l = (u8)end_addr;
	end_addr_h = (u8)(end_addr >> 8);

	/*construct H2C Cmd*/
	if (isDynamic)
		SET_H2CCMD_LPS_POFF_PARAM_RDVLD(lps_poff_param, 0);
	else
		SET_H2CCMD_LPS_POFF_PARAM_RDVLD(lps_poff_param, 1);

	SET_H2CCMD_LPS_POFF_PARAM_WRVLD(lps_poff_param, 1);
	SET_H2CCMD_LPS_POFF_PARAM_STARTADDL(lps_poff_param, start_addr_l);
	SET_H2CCMD_LPS_POFF_PARAM_STARTADDH(lps_poff_param, start_addr_h);
	SET_H2CCMD_LPS_POFF_PARAM_ENDADDL(lps_poff_param, end_addr_l);
	SET_H2CCMD_LPS_POFF_PARAM_ENDADDH(lps_poff_param, end_addr_h);

	rtw_hal_fill_h2c_cmd(padapter, H2C_LPS_POFF_PARAM,
			     H2C_LPS_POFF_PARAM_LEN, lps_poff_param);
}

/*************************************************************************
Function: SET Location of Configuration File to FW
**************************************************************************/
static void rtl8723d_lps_poff_set_param(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;

	u8 static_tx_bndy = 0;
	u8 dynamic_tx_bndy = 0;
	u16 static_len = 0, dynamic_len = 0;

	static_tx_bndy = plps_poff_info->tx_bndy_static;
	dynamic_tx_bndy = plps_poff_info->tx_bndy_dynamic;

	dynamic_len = plps_poff_info->ConfLenForPTK +
		      plps_poff_info->ConfLenForGTK;

	if (ATOMIC_READ(&plps_poff_info->bSetPOFFParm) == _TRUE)
		return;

	/* download static configuration */
	static_len = LPS_POFF_STATIC_FILE_LEN - TXDESC_SIZE;
	rtl8723d_lps_poff_h2c_param(padapter, static_tx_bndy,
				    static_len, _FALSE);

	/* download dynamic configuration */
	/* the length must be add more 8 Byte due to hard bug */
	if (dynamic_len != 0) {
		dynamic_len += 8;
		rtl8723d_lps_poff_h2c_param(padapter, dynamic_tx_bndy,
					    dynamic_len, _TRUE);
	}

	ATOMIC_SET(&plps_poff_info->bSetPOFFParm, _TRUE);
}


/****************************************************************************
Function: change tx boundary
*****************************************************************************/
static void rtl8723d_lps_poff_set_tx_bndy(PADAPTER padapter, u8 tx_bndy)
{
	u32	numHQ = 0x10;
	u32	numLQ = 0x10;
	u32	numPubQ = 0;
	u8	numNQ = 0;

	u32	val32 = 0;
	u8	val8  = 0;

#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	numHQ = 0x8;
	numLQ = 0x8;
#endif
	numPubQ = tx_bndy - numHQ - numLQ - numNQ - 1;
	val8 = _NPQ(numNQ);
	val32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;

	rtw_write8(padapter, REG_RQPN_NPQ, val8);
	rtw_write32(padapter, REG_RQPN, val32);
}

/****************************************************************************
Function: change tx boundary flow
*****************************************************************************/
static bool rtl8723d_lps_poff_tx_bndy_flow(PADAPTER padapter, bool enable)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct xmit_priv *pxmitpriv;
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	u8	tx_bndy = 0, tx_bndy_new = 0, count = 0, queue_pending = _FALSE;
	u8	val8 = 0;
	u16	val16 = 0;
	u32	val32 = 0;

	pxmitpriv = &padapter->xmitpriv;
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&tx_bndy);

	RTW_INFO("%s: tx_bndy: %#X, tx_bndy_static: %#X\n",
		 __func__, tx_bndy, plps_poff_info->tx_bndy_static);

	if (enable)
		tx_bndy_new = plps_poff_info->tx_bndy_static + 1;
	else
		tx_bndy_new = tx_bndy;

	ATOMIC_SET(&plps_poff_info->bTxBoundInProgress, _TRUE);

	/* stop os layer TX*/
	rtw_mi_netif_stop_queue(padapter);

	val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);

	/* stop tx process and wait tx empty */
	while ((val16 & 0x05FF) != 0x05FF) {
		rtw_mdelay_os(10);
		val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);

		count++;
		if (count >= 100) {
			val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
			RTW_INFO("%s, txpkt_empty: %#04x\n", __func__, val16);
			val8 = rtw_read8(padapter, REG_CPU_MGQ_INFORMATION);
			RTW_INFO("%s, REG_CPU_MGQ_INFORMATION: %#02x\n",
				 __func__, val8);
			RTW_INFO("%s, wait for tx empty timeout!!\n", __func__);
			ATOMIC_SET(&plps_poff_info->bTxBoundInProgress, _FALSE);
			rtw_mi_netif_wake_queue(padapter);
			return _FALSE;
		}
	}

	/* change tx boundary*/
	rtl8723d_lps_poff_set_tx_bndy(padapter, tx_bndy_new);

	/* set free tail */
	val32 = rtw_read32(padapter, REG_FWHW_TXQ_CTRL);
	val32 |= BIT20;
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, val32);
	rtw_write8(padapter, REG_BCNQ_BDNY, tx_bndy_new);

	RTW_INFO("%s: free tail = %#x after\n", __func__,
		 rtw_read8(padapter, REG_FW_FREE_TAIL_8723D));

	/* set bcn head */
	val32 = rtw_read32(padapter, REG_FWHW_TXQ_CTRL);
	val32 &= ~BIT20;
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, val32);
	rtw_write8(padapter, REG_BCNQ_BDNY, tx_bndy);

	/* reinit LLT */
	rtl8723d_InitLLTTable(padapter);

	/* clear 0x210 ??*/
	val32 = rtw_read32(padapter, REG_TXDMA_STATUS);

	if (val32 != 0) {
		RTW_INFO("%s: REG_TXDMA_STATUS: %#08x\n", __func__, val32);
		rtw_write32(padapter, REG_TXDMA_STATUS, val32);
	}

	ATOMIC_SET(&plps_poff_info->bTxBoundInProgress, _FALSE);


	/* restart tx */

	rtw_mi_netif_wake_queue(padapter);

	return _TRUE;
}

/****************************************************************************
Function: Append extra bytes for dynamic confiure file
	  Add one entry to fix hw bug,
	  list command: 00 00 00 00 00 40 30 00, suggested by DD
*****************************************************************************/
static u8 rtl8723d_lps_poff_append_extra_info(PADAPTER padapter, u32 len)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	u32 data_offset = len + plps_poff_info->ConfFileOffset;

	*(plps_poff_info->pDynamicFile + (data_offset)) = 0x00;
	*(plps_poff_info->pDynamicFile + (data_offset + 1)) = 0x00;
	*(plps_poff_info->pDynamicFile + (data_offset + 2)) = 0x00;
	*(plps_poff_info->pDynamicFile + (data_offset + 3)) = 0x00;
	*(plps_poff_info->pDynamicFile + (data_offset + 4)) = 0x00;
	*(plps_poff_info->pDynamicFile + (data_offset + 5)) = 0x40;
	*(plps_poff_info->pDynamicFile + (data_offset + 6)) = 0x30;
	*(plps_poff_info->pDynamicFile + (data_offset + 7)) = 0x00;
	return 8;
}

/****************************************************************************
Function: xmit config file to write port.
*****************************************************************************/
static void rtl8723d_lps_poff_send_config_frame(PADAPTER padapter,
		u8 *pFile, u32 len)
{
	struct xmit_frame	*pcmdframe = NULL;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	int i = 0;

	pxmitpriv = &padapter->xmitpriv;
	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);

	if (pcmdframe == NULL)
		RTW_INFO("%s: alloc cmdframe fail\n", __func__);

	_rtw_memcpy(pcmdframe->buf_addr, pFile, len);

	pattrib = &pcmdframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;
	pattrib->pktlen = len - TXDESC_OFFSET;
	pattrib->last_txcmdsz = len - TXDESC_OFFSET;

	RTW_INFO("%s, len: %d, MAX_CMDBUF_SZ: %d\n", __func__, len,
		 MAX_CMDBUF_SZ);
#ifdef CONFIG_PCI_HCI
	dump_mgntframe(padapter, pcmdframe);
#else
	dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif

}

/****************************************************************************
Function: download config file flow
*****************************************************************************/
static void rtl8723d_lps_poff_send_config_file(PADAPTER padapter,
		u8 *pFile, u8 loc, u32 len)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	bool bRecover = _FALSE, bcn_valid = _FALSE;
	u8 DLBcnCount = 0, val8 = 0, tx_bndy = 0;
	u32 poll = 0;
	u8 RegFwHwTxQCtrl;
	rtw_hal_get_def_var(padapter,
			    HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&tx_bndy);

	/* set 0x100[8]=1 for SW beacon */
	val8 = rtw_read8(padapter, REG_CR + 1);
	val8 |= BIT(0);
	rtw_write8(padapter,  REG_CR + 1, val8);

	/*set 0x422[6]=0 to disable beacon DMA pass to MACTx*/
	RegFwHwTxQCtrl = rtw_read8(padapter, REG_FWHW_TXQ_CTRL + 2);
	if (RegFwHwTxQCtrl & BIT(6))
		bRecover = _TRUE;

	RegFwHwTxQCtrl &= ~BIT(6);

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, RegFwHwTxQCtrl);

	/* Clear beacon valid check bit. */
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

	/* set 0x209[7:0] beacon queue head page to start download location */
	rtw_write8(padapter, REG_TDECTRL_8723D + 1, loc);

	DLBcnCount = 0;
	poll = 0;

	do {
		rtl8723d_lps_poff_send_config_frame(padapter, pFile, len);
		DLBcnCount++;
		do {
			rtw_mdelay_os(10);
			/*check rsvd page download OK.*/
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID,
					  (u8 *)(&bcn_valid));
			poll++;
		} while (!bcn_valid && (poll % 10) != 0
			 && !RTW_CANNOT_RUN(padapter));
	} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(padapter));

	if (!bcn_valid)
		RTW_INFO(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
			 ADPT_ARG(padapter), DLBcnCount, poll);
	else
		RTW_INFO(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
			 ADPT_ARG(padapter), DLBcnCount, poll);

	/*restore bcn operation*/
	rtw_write8(padapter, REG_TDECTRL_8723D + 1, tx_bndy);

	/*restore 0x422[6]=1 for normal bcn*/
	if (bRecover) {
		RegFwHwTxQCtrl |= BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, RegFwHwTxQCtrl);
	}

	/*restore 0x100[8]=0 for SW beacon*/
	/* Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
#ifndef CONFIG_PCI_HCI
	val8 = rtw_read8(padapter, REG_CR + 1);
	val8 &= ~BIT(0);
	rtw_write8(padapter, REG_CR + 1, val8);
#endif
}

/****************************************************************************
Function: Prepare Confiure File
*****************************************************************************/
static void rtl8723d_lps_poff_dl_config_file(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	u8 count = 0, append_len = 0;
	u32 offset = 0, static_file_len = 0, dynamic_file_len = 0;
	int i = 0, j = 0;

	offset = plps_poff_info->ConfFileOffset;

	static_file_len = LPS_POFF_STATIC_FILE_LEN - offset;

	dynamic_file_len =
		plps_poff_info->ConfLenForPTK + plps_poff_info->ConfLenForGTK;

	RTW_INFO("%s: static_file_len: %d dynamic_file_len: %d, offset: %d\n",
		 __func__, static_file_len, dynamic_file_len, offset);

	/*static file*/
	rtl8723d_lps_poff_send_config_file(padapter,
					   plps_poff_info->pStaticFile + offset,
					   plps_poff_info->tx_bndy_static,
					   static_file_len);

	/*dynamic file*/
	if (dynamic_file_len != 0) {
		dynamic_file_len += TXDESC_SIZE - offset;

		append_len =
			rtl8723d_lps_poff_append_extra_info(padapter,
					dynamic_file_len);
		dynamic_file_len += append_len;
		rtl8723d_lps_poff_send_config_file(padapter,
				   plps_poff_info->pDynamicFile + offset,
					   plps_poff_info->tx_bndy_dynamic,
						   dynamic_file_len);
	}
}

static u8 rtl8723d_lps_poff_set_dynamic_file(u8 *pFile, u32 type, u32 wdata)
{
	SET_HOIE_ENTRY_LOW_DATA(pFile, (u16)wdata);
	SET_HOIE_ENTRY_HIGH_DATA(pFile, (u16)(wdata >> 16));
	SET_HOIE_ENTRY_MODE_SELECT(pFile, 0);
	SET_HOIE_ENTRY_ADDRESS(pFile, (type >> 2));
	SET_HOIE_ENTRY_BYTE_MASK(pFile, 0xF);
	SET_HOIE_ENTRY_IO_LOCK(pFile, 0);
	SET_HOIE_ENTRY_WR_EN(pFile, 1);
	SET_HOIE_ENTRY_RD_EN(pFile, 0);
	SET_HOIE_ENTRY_RAW_RW(pFile, 0);
	SET_HOIE_ENTRY_RAW(pFile, 0);
	SET_HOIE_ENTRY_IO_DELAY(pFile, 0);

	return 8;
}

static void rtl8723d_lps_poff_dynamic_file(PADAPTER padapter, u8 index, u8 isGK)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	u8 *ptkfile = NULL;
	u8 ret = 0;
	u32 tgt_cmd = 0, tgt_wdata = 0;
	int i = 0, j = 0;

	for (i = 0 ; i < CAM_CONTENT_COUNT; i++) {
		if (!isGK)
			ptkfile = plps_poff_info->pDynamicFile +
				  plps_poff_info->ConfLenForPTK + TXDESC_SIZE;
		else
			ptkfile = plps_poff_info->pDynamicFile +
				  plps_poff_info->ConfLenForPTK +
				  plps_poff_info->ConfLenForGTK + TXDESC_SIZE;

		tgt_cmd = i + (CAM_CONTENT_COUNT * index);
		tgt_cmd = tgt_cmd | CAM_POLLINIG | CAM_WRITE;

		switch (i) {
		case 0:
			tgt_wdata = dvobj->cam_cache[index].ctrl |
				    dvobj->cam_cache[index].mac[0] << 16 |
				    dvobj->cam_cache[index].mac[1] << 24;
			break;
		case 1:
			tgt_wdata = dvobj->cam_cache[index].mac[2] |
				    dvobj->cam_cache[index].mac[3] << 8 |
				    dvobj->cam_cache[index].mac[4] << 16 |
				    dvobj->cam_cache[index].mac[5] << 24;
			break;
		default:
			j = (i - 2) << 2;
			tgt_wdata =
				dvobj->cam_cache[index].key[j + 3] << 24 |
				dvobj->cam_cache[index].key[j + 2] << 16 |
				dvobj->cam_cache[index].key[j + 1] << 8 |
				dvobj->cam_cache[index].key[j];
			break;
		}

		ret = rtl8723d_lps_poff_set_dynamic_file(ptkfile,
				WCAMI, tgt_wdata);

		if (!isGK) {
			plps_poff_info->ConfLenForPTK += ret;

			ptkfile = plps_poff_info->pDynamicFile +
				  plps_poff_info->ConfLenForPTK + TXDESC_SIZE;
		} else {
			plps_poff_info->ConfLenForGTK += ret;
			ptkfile = plps_poff_info->pDynamicFile +
				  plps_poff_info->ConfLenForPTK +
				  plps_poff_info->ConfLenForGTK + TXDESC_SIZE;
		}

		ret = rtl8723d_lps_poff_set_dynamic_file(ptkfile,
				RWCAM, tgt_cmd);

		if (!isGK)
			plps_poff_info->ConfLenForPTK += ret;
		else
			plps_poff_info->ConfLenForGTK += ret;

#if 0
		RTW_INFO("%s: tgt_wdata: %#08x\n", __func__, tgt_wdata);
#endif
	}
}

static void rtl8723d_lps_poff_sec_cam_opt(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int i = 0;
	u8 isValid = 0, isGK = 0, index = 0, key_id = 0xff, gk_start = 0xff;
	u16 val16 = 0, len = 0;

	/*Using cam cache to create config file for FW use.*/
	/*1- deail with pairwise key*/

	for (i = 0 ; i < cam_ctl->num ; i++) {

		val16 = dvobj->cam_cache[i].ctrl;
		isValid = (val16 >> 15) & 0x01;
		isGK = (val16 >> 6) & 0x01;

		if (isValid && !isGK) {
			rtl8723d_lps_poff_dynamic_file(padapter, i, _FALSE);
			RTW_INFO("%s: id: %2u, kid: %3u, ctrl: %#04x, "MAC_FMT""KEY_FMT"\n",
				 __func__, i, (dvobj->cam_cache[i].ctrl) & 0x03,
				 dvobj->cam_cache[i].ctrl,
				 MAC_ARG(dvobj->cam_cache[i].mac),
				 KEY_ARG(dvobj->cam_cache[i].key));
		} else if (isGK) {
			key_id = dvobj->cam_cache[i].ctrl & 0x03;
			if (key_id == pmlmeinfo->key_index)
				gk_start = i;
			RTW_INFO("%s: GK_start at %d\n", __func__, gk_start);
			RTW_INFO("%s: id: %2u, kid: %3u, ctrl: %#04x, "MAC_FMT""KEY_FMT"\n",
				 __func__, i, key_id, dvobj->cam_cache[i].ctrl,
				 MAC_ARG(dvobj->cam_cache[i].mac),
				 KEY_ARG(dvobj->cam_cache[i].key));
		}
	}

	/*2- deail with group key*/
	rtl8723d_lps_poff_dynamic_file(padapter, gk_start, _TRUE);

	RTW_INFO("%s: ConfLenForPTK: %d, ConfLenForGTK: %d\n", __func__,
		 plps_poff_info->ConfLenForPTK, plps_poff_info->ConfLenForGTK);
}

/****************************************************************************
Function: Prepare enter LPS partial off status.
change tx boundary, download configuration file if necessary and send info to FW
*****************************************************************************/
static bool rtl8723d_prepare_for_enter_poff(PADAPTER padapter, bool bEnterLPS)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	bool ret = _FALSE;
	u8 i = 0, cam_cache_num = 0;

	for (i = 0 ; i < cam_ctl->num; i++) {
		if (dvobj->cam_cache[i].ctrl != 0)
			cam_cache_num++;
	}

	ret = rtl8723d_lps_poff_tx_bndy_flow(padapter, bEnterLPS);

	if (ret == _TRUE) {
		if (cam_cache_num > 0) {
			rtl8723d_lps_poff_sec_cam_opt(padapter);
		} else {
			plps_poff_info->ConfLenForPTK = 0;
			plps_poff_info->ConfLenForGTK = 0;
		}
		rtl8723d_lps_poff_dl_config_file(padapter);
		rtl8723d_lps_poff_set_param(padapter);
	}
	return ret;
}

/*************************************************************************
Function: SET H2C To Enable Partial Off
**************************************************************************/
void rtl8723d_lps_poff_h2c_ctrl(PADAPTER padapter, u8 en)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	u8 param = 0;

	if (pregistrypriv->wifi_spec == 1)
		return;

	if (plps_poff_info->bEn == _FALSE)
		return;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		if (en) {
			RTW_INFO("%s: enable case\n", __func__);
			SET_H2CCMD_LPS_POFF_CTRL_EN(&param, 1);
		} else {
			RTW_INFO("%s: disable case\n", __func__);
			ATOMIC_SET(&plps_poff_info->bSetPOFFParm, _FALSE);
			SET_H2CCMD_LPS_POFF_CTRL_EN(&param, 0);
		}
		rtw_hal_fill_h2c_cmd(padapter, H2C_LPS_POFF_CTRL,
				     H2C_LPS_POFF_CTRL_LEN, &param);
	}
}

/*************************************************************************
Function: The operation to Enter or Leave FWLPS 32K when partial off enable
**************************************************************************/
void rtl8723d_lps_poff_set_ps_mode(PADAPTER padapter, bool bEnterLPS)
{

	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	bool res = _FALSE;

	if (pregistrypriv->wifi_spec == 1)
		return;

	if (plps_poff_info->bEn) {

		plps_poff_info->ConfLenForPTK = 0;
		plps_poff_info->ConfLenForGTK = 0;

		if (bEnterLPS) {
			res = rtl8723d_prepare_for_enter_poff(padapter,
							      bEnterLPS);
			ATOMIC_SET(&plps_poff_info->bEnterPOFF, res);
		} else
			rtl8723d_lps_poff_tx_bndy_flow(padapter, bEnterLPS);
	}
}

/*************************************************************************
Function: Get LPS-POFF Enter Status
**************************************************************************/
bool rtl8723d_lps_poff_get_status(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;

	if (pregistrypriv->wifi_spec == 1) {
		RTW_INFO("%s: wifi_spec is enable\n", __func__);
		return _FALSE;
	}

	if (plps_poff_info->bEn == _FALSE) {
		RTW_INFO("%s: POFF is disable\n", __func__);
		return _FALSE;
	}

	return ATOMIC_READ(&plps_poff_info->bEnterPOFF);
}

/*************************************************************************
Function: Get LPS-POFF change tx bndy status
**************************************************************************/
bool rtl8723d_lps_poff_get_txbndy_status(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;

	return ATOMIC_READ(&plps_poff_info->bTxBoundInProgress);
}

/*************************************************************************
Function: Get LPS-POFF initial
**************************************************************************/
void rtl8723d_lps_poff_init(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	lps_poff_info_t *lps_poff_info;
	u8 tx_bndy = 0, page_size = 0, total_page = 0, page_num = 0;
	u8 val = 0;

	if (pregistrypriv->wifi_spec == 1)
		return;

	if (is_primary_adapter(padapter)) {

		rtw_hal_get_def_var(padapter,
				    HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&val);
		tx_bndy = val;

		rtw_hal_get_def_var(padapter,
				    HAL_DEF_TX_PAGE_SIZE, (u8 *)&val);
		page_size = val;

		total_page = PageNum(LPS_POFF_STATIC_FILE_LEN, page_size) +
			     PageNum(LPS_POFF_DYNAMIC_FILE_LEN, page_size);

		lps_poff_info =
			(lps_poff_info_t *)rtw_zmalloc(sizeof(lps_poff_info_t));

		if (lps_poff_info != NULL) {
			pwrpriv->plps_poff_info = lps_poff_info;

			lps_poff_info->pStaticFile =
				(u8 *)rtw_zmalloc(LPS_POFF_STATIC_FILE_LEN);
			if (lps_poff_info->pStaticFile == NULL) {
				RTW_INFO("%s: alloc pStaticFile fail\n",
					 __func__);
				goto alloc_static_conf_file_fail;
			} else {
				pwrpriv->plps_poff_info->pStaticFile =
					lps_poff_info->pStaticFile;
			}

			lps_poff_info->pDynamicFile =
				(u8 *)rtw_zmalloc(LPS_POFF_DYNAMIC_FILE_LEN);
			if (lps_poff_info->pDynamicFile == NULL) {
				RTW_INFO("%s: alloc pDynamicFile fail\n",
					 __func__);
				goto alloc_dynamic_conf_file_fail;
			} else {
				pwrpriv->plps_poff_info->pDynamicFile =
					lps_poff_info->pDynamicFile;
			}

			pwrpriv->plps_poff_info->bEn = _TRUE;
			ATOMIC_SET(&pwrpriv->plps_poff_info->bEnterPOFF,
				   _FALSE);
			ATOMIC_SET(&pwrpriv->plps_poff_info->bSetPOFFParm,
				   _FALSE);
			ATOMIC_SET(&pwrpriv->plps_poff_info->bTxBoundInProgress,
				   _FALSE);
#ifdef CONFIG_PCI_HCI
			pwrpriv->plps_poff_info->ConfFileOffset = 40;
#else
			pwrpriv->plps_poff_info->ConfFileOffset = 0;
#endif
			pwrpriv->plps_poff_info->tx_bndy_static =
				tx_bndy - total_page;
			page_num = PageNum(LPS_POFF_DYNAMIC_FILE_LEN,
					   page_size);
			pwrpriv->plps_poff_info->tx_bndy_dynamic =
				tx_bndy - page_num;
			/*
			   construct static DLConfiguration File
			*/
			hal_construct_poff_static_file(padapter);
			goto exit;
		} else {
			RTW_INFO("%s: alloc lps_poff_info fail\n",  __func__);
			goto exit;
		}
	}

alloc_dynamic_conf_file_fail:
	rtw_mfree((u8 *)pwrpriv->plps_poff_info->pStaticFile,
		  LPS_POFF_STATIC_FILE_LEN);
	pwrpriv->plps_poff_info->pStaticFile = NULL;
alloc_static_conf_file_fail:
	rtw_mfree((u8 *)pwrpriv->plps_poff_info, sizeof(lps_poff_info_t));
	pwrpriv->plps_poff_info = NULL;
exit:
	return;
}

/*************************************************************************
Function: Get LPS-POFF de-initial
**************************************************************************/
void rtl8723d_lps_poff_deinit(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	lps_poff_info_t *plps_poff_info = pwrpriv->plps_poff_info;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;

	if (pregistrypriv->wifi_spec == 1)
		return;

	if (is_primary_adapter(padapter)) {
		if (plps_poff_info->pDynamicFile != NULL) {
			rtw_mfree((u8 *)plps_poff_info->pDynamicFile,
				  LPS_POFF_DYNAMIC_FILE_LEN);
			plps_poff_info->pDynamicFile = NULL;
		}
		if (plps_poff_info->pStaticFile != NULL) {
			rtw_mfree((u8 *)plps_poff_info->pStaticFile,
				  LPS_POFF_STATIC_FILE_LEN);
			plps_poff_info->pStaticFile = NULL;
		}
		if (plps_poff_info != NULL) {
			rtw_mfree((u8 *)plps_poff_info,
				  sizeof(lps_poff_info_t));
			plps_poff_info = NULL;
		}
	}

}
#endif
