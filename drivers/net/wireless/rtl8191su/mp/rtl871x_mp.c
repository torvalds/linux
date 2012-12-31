/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL871X_MP_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <rtl871x_mp_phy_regdef.h>
#include <rtl8712_cmd.h>

#ifdef CONFIG_MP_INCLUDED

static void _init_mp_priv_(struct mp_priv *pmp_priv)
{
	pmp_priv->mode = _LOOPBOOK_MODE_;

	pmp_priv->curr_ch = 1;
	pmp_priv->curr_modem = MIXED_PHY;
	pmp_priv->curr_rateidx = 0;
	pmp_priv->curr_txpoweridx = 0x14;

	pmp_priv->antenna_tx = ANTENNA_A;
	pmp_priv->antenna_rx = ANTENNA_AB;

	pmp_priv->check_mp_pkt = 0;

	pmp_priv->tx_pktcount = 0;

	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_crcerrpktcount = 0;

}

#ifdef PLATFORM_WINDOWS
/*
void mp_wi_callback(
	IN NDIS_WORK_ITEM*	pwk_item,
	IN PVOID			cntx
	)
{
	_adapter* padapter =(_adapter *)cntx;
	struct mp_priv *pmppriv=&padapter->mppriv;
	struct mp_wi_cntx	*pmp_wi_cntx=&pmppriv->wi_cntx;

	// Execute specified action.
	if(pmp_wi_cntx->curractfunc != NULL)
	{
		LARGE_INTEGER	cur_time;
		ULONGLONG start_time, end_time;
		NdisGetCurrentSystemTime(&cur_time);	// driver version
		start_time = cur_time.QuadPart/10; // The return value is in microsecond

		pmp_wi_cntx->curractfunc(padapter);

		NdisGetCurrentSystemTime(&cur_time);	// driver version
		end_time = cur_time.QuadPart/10; // The return value is in microsecond

		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_info_,
			 ("WorkItemActType: %d, time spent: %I64d us\n",
			  pmp_wi_cntx->param.act_type, (end_time-start_time)));
	}

	NdisAcquireSpinLock(&(pmp_wi_cntx->mp_wi_lock));
	pmp_wi_cntx->bmp_wi_progress= _FALSE;
	NdisReleaseSpinLock(&(pmp_wi_cntx->mp_wi_lock));

	if (pmp_wi_cntx->bmpdrv_unload)
	{
		NdisSetEvent(&(pmp_wi_cntx->mp_wi_evt));
	}

}
*/

int init_mp_priv (struct mp_priv *pmp_priv)
{
	struct wlan_network *pnetwork = &pmp_priv->mp_network;
	struct mp_wi_cntx *pmp_wi_cntx = &pmp_priv->wi_cntx;

	_init_mp_priv_(pmp_priv);

	pmp_priv->network_macaddr[0] = 0x00;
	pmp_priv->network_macaddr[1] = 0xE0;
	pmp_priv->network_macaddr[2] = 0x4C;
	pmp_priv->network_macaddr[3] = 0x87;
	pmp_priv->network_macaddr[4] = 0x66;
	pmp_priv->network_macaddr[5] = 0x55;

	pnetwork->network.MacAddress[0] = 0x00;
	pnetwork->network.MacAddress[1] = 0xE0;
	pnetwork->network.MacAddress[2] = 0x4C;
	pnetwork->network.MacAddress[3] = 0x87;
	pnetwork->network.MacAddress[4] = 0x66;
	pnetwork->network.MacAddress[5] = 0x55;

	pnetwork->network.Ssid.SsidLength = 8;
	_memcpy(pnetwork->network.Ssid.Ssid, "mp_871x", pnetwork->network.Ssid.SsidLength);

	pmp_priv->rx_testcnt = 0;
	pmp_priv->rx_testcnt1 = 0;
	pmp_priv->rx_testcnt2 = 0;

	pmp_priv->tx_testcnt = 0;
	pmp_priv->tx_testcnt1 = 0;

	pmp_wi_cntx->bmpdrv_unload = _FALSE;
	pmp_wi_cntx->bmp_wi_progress = _FALSE;
	pmp_wi_cntx->curractfunc = NULL;

	return _SUCCESS;
}
#endif

#ifdef PLATFORM_LINUX
int init_mp_priv(struct mp_priv *pmp_priv)
{
	int i, res;
	struct mp_xmit_frame *pmp_xmitframe;

	//MSG_8712("+_init_mp_priv\n");

	_init_mp_priv_(pmp_priv);

	_init_queue(&pmp_priv->free_mp_xmitqueue);

	pmp_priv->pallocated_mp_xmitframe_buf = NULL;
	pmp_priv->pallocated_mp_xmitframe_buf = _vmalloc(NR_MP_XMITFRAME * sizeof(struct mp_xmit_frame) + 4);
	if (pmp_priv->pallocated_mp_xmitframe_buf == NULL) {
		//ERR_8712("_init_mp_priv, alloc mp_xmitframe_buf fail\n");
		res = _FAIL;
		goto _exit_init_mp_priv;
	}

	pmp_priv->pmp_xmtframe_buf = pmp_priv->pallocated_mp_xmitframe_buf + 4 - ((uint) (pmp_priv->pallocated_mp_xmitframe_buf) & 3);

	pmp_xmitframe = (struct mp_xmit_frame*)pmp_priv->pmp_xmtframe_buf;

	for (i = 0; i < NR_MP_XMITFRAME; i++)
	{
		_init_listhead(&(pmp_xmitframe->list));
		list_insert_tail(&(pmp_xmitframe->list), &(pmp_priv->free_mp_xmitqueue.queue));

		pmp_xmitframe->pkt = NULL;
		pmp_xmitframe->frame_tag = MP_FRAMETAG;
		pmp_xmitframe->padapter = pmp_priv->papdater;

		pmp_xmitframe++;
	}

	pmp_priv->free_mp_xmitframe_cnt = NR_MP_XMITFRAME;

	res = _SUCCESS;

_exit_init_mp_priv:

	return res;
}

int free_mp_priv(struct mp_priv *pmp_priv)
{
	int res = 0;

	//MSG_8712("+_free_mp_priv\n");

	if (pmp_priv->pallocated_mp_xmitframe_buf)
		_vmfree(pmp_priv->pallocated_mp_xmitframe_buf, NR_MP_XMITFRAME * sizeof(struct mp_xmit_frame) + 4);

	return res;
}
#endif

void mp871xinit(_adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;
	pmppriv->papdater = padapter;

	init_mp_priv(pmppriv);
}


void mp871xdeinit(_adapter *padapter)
{
	struct mp_priv *pmppriv = &padapter->mppriv;

	free_mp_priv(pmppriv);
}


void _irqlevel_changed_(_irqL *irqlevel, u8 bLower)
{

#ifdef PLATFORM_OS_XP

	if (bLower == LOWER) {
		*irqlevel = KeGetCurrentIrql();

		if (*irqlevel > PASSIVE_LEVEL) {
				KeLowerIrql(PASSIVE_LEVEL);
			//DEBUG_ERR(("\n <=== KeLowerIrql.\n"));
		}
	} else {
		if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
			KeRaiseIrql(DISPATCH_LEVEL, irqlevel);
			//DEBUG_ERR(("\n <=== KeRaiseIrql.\n"));
		}
	}

#endif

}

/*
 * Special for bb and rf reg read/write
 */
static u32 fw_iocmd_read(PADAPTER pAdapter, IOCMD_STRUCT iocmd)
{
	u32 cmd32 = 0, val32 = 0;

	u8 iocmd_class	= iocmd.cmdclass;
	u16 iocmd_value	= iocmd.value;
	u8 iocmd_idx	= iocmd.index;

	cmd32 = (iocmd_class << 24) | (iocmd_value << 8) | iocmd_idx ;
//	RT_TRACE(_module_rtl871x_mp_c_, _drv_alert_, ("fw_iocmd_read = cmd32:%x ........\n",cmd32));

	if (fw_cmd(pAdapter, cmd32))
		fw_cmd_data(pAdapter, &val32, 1);
	else
		val32 = 0;

	return val32;
}

static u8 fw_iocmd_write(PADAPTER pAdapter, IOCMD_STRUCT iocmd, u32 value)
{
	u32 cmd32 = 0;

	u8 iocmd_class	= iocmd.cmdclass;
	u32 iocmd_value	= iocmd.value;
	u8 iocmd_idx	= iocmd.index;

	fw_cmd_data(pAdapter, &value, 0);
	usleep_os(100);

	cmd32 = (iocmd_class << 24) | (iocmd_value << 8) | iocmd_idx ;
	return fw_cmd(pAdapter, cmd32);
}

u32 bb_reg_read(PADAPTER pAdapter, u16 offset)// offset : 0X800~0XFFF
{
	u8 shift = offset & 0x0003;	// 4 byte access
	u16 bb_addr = offset & 0x0FFC;	// 4 byte access
	u32 bb_val = 0;

	IOCMD_STRUCT iocmd;

	iocmd.cmdclass	= IOCMD_CLASS_BB_RF;
	iocmd.value	= bb_addr;
	iocmd.index	= IOCMD_BB_READ_IDX;
	bb_val = fw_iocmd_read(pAdapter, iocmd);

	if (shift != 0) {
		u32 bb_val2 = 0;
		bb_val >>= (shift * 8);
		iocmd.value += 4;
		bb_val2 = fw_iocmd_read(pAdapter, iocmd);
		bb_val2 <<= ((4 - shift) * 8);
		bb_val |= bb_val2;
	}

	return bb_val;
}

u8 bb_reg_write(PADAPTER pAdapter, u16 offset, u32 value)// offset : 0X800~0XFFF
{
	u8 shift = offset & 0x0003;	// 4 byte access
	u16 bb_addr = offset & 0x0FFC;	// 4 byte access

	IOCMD_STRUCT iocmd;

	iocmd.cmdclass	= IOCMD_CLASS_BB_RF;
	iocmd.value	= bb_addr;
	iocmd.index	= IOCMD_BB_WRITE_IDX;

	if (shift != 0) {
		u32 oldValue = 0;
		u32 newValue = value;

		oldValue = bb_reg_read(pAdapter, iocmd.value);
		oldValue &= (0xFFFFFFFF >> ((4 - shift) * 8));
		value = oldValue | (newValue << (shift * 8));
		if (fw_iocmd_write(pAdapter, iocmd, value) == _FALSE)
			return _FALSE;

		iocmd.value += 4;
		oldValue = bb_reg_read(pAdapter, iocmd.value);
		oldValue &= (0xFFFFFFFF << (shift * 8));
		value = oldValue | (newValue >> ((4 - shift) * 8));
	}

	return fw_iocmd_write(pAdapter, iocmd, value);
}

u32 rf_reg_read(PADAPTER pAdapter, u8 path, u8 offset) // offset : 0x00 ~ 0xFF
{
	u16 rf_addr = (path << 8) | offset;
	u32 rf_data;

	IOCMD_STRUCT iocmd;

	iocmd.cmdclass	= IOCMD_CLASS_BB_RF ;
	iocmd.value	= rf_addr ;
	iocmd.index	= IOCMD_RF_READ_IDX;

	rf_data = fw_iocmd_read(pAdapter,iocmd);

	return rf_data;
}

u8 rf_reg_write(PADAPTER pAdapter, u8 path, u8 offset, u32 value)
{
	u16 rf_addr = (path << 8) | offset;

	IOCMD_STRUCT iocmd;

	iocmd.cmdclass	= IOCMD_CLASS_BB_RF;
	iocmd.value	= rf_addr;
	iocmd.index	= IOCMD_RF_WRIT_IDX;

	return fw_iocmd_write(pAdapter, iocmd, value);
}

static u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask>>i) &  0x1) == 1) break;

	return i;
}

u32 get_bb_reg(PADAPTER pAdapter, u16 offset, u32 bitmask)
{
	u32 org_value, bit_shift, new_value;

	org_value = bb_reg_read(pAdapter ,offset);
	bit_shift = bitshift(bitmask);
//	RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("get_bb_reg: org=0x%08x\n", org_value));
	new_value = (org_value & bitmask) >> bit_shift;

	return new_value;
}

u8 set_bb_reg(PADAPTER pAdapter, u16 offset, u32 bitmask, u32 value)
{
	u32 org_value, bit_shift, new_value;

	if (bitmask != bMaskDWord) {
		org_value = bb_reg_read(pAdapter ,offset);
		bit_shift = bitshift(bitmask);
		new_value = ((org_value & (~bitmask)) | (value << bit_shift));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_,
			 ("set_bb_reg: offset=0x%04x org=0x%08x new=0x%08x\n",
			  offset, org_value, new_value));
	} else {
		new_value = value;
		RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_,
			 ("set_bb_reg: offset=0x%04x value=0x%08x\n",
			  offset, new_value));
	}

	return bb_reg_write(pAdapter,offset,new_value);
}

u32 get_rf_reg(PADAPTER pAdapter, u8 path, u8 offset, u32 bitmask)
{
	u32 org_value, bit_shift, new_value;
	org_value = rf_reg_read(pAdapter, path, offset);
	bit_shift = bitshift(bitmask);
	new_value = (org_value & bitmask) >> bit_shift;

	return new_value;
}

u8 set_rf_reg(PADAPTER pAdapter, u8 path, u8 offset, u32 bitmask, u32 value)
{
	u32 org_value, bit_shift, new_value;

	if (bitmask != bMaskDWord) {
		org_value = rf_reg_read(pAdapter, path, offset);
		bit_shift = bitshift(bitmask);
		new_value = ((org_value & (~bitmask)) | (value << bit_shift));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("set_rf_reg: v=0x%08x org=0x%08x new=0x%08x\n", value, org_value, new_value));
	} else {
		new_value = value;
		RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("set_rf_reg: v=0x%08x new=0x%08x\n", value, new_value));
	}

	return rf_reg_write(pAdapter, path, offset, new_value);
}

/*
 * SetChannel
 * Description
 *	Use H2C command to change channel,
 *	not only modify rf register, but also other setting need to be done.
 */
void SetChannel(PADAPTER pAdapter)
{
#if 1
	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetChannel_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetChannel);

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_,
		 ("+SetChannel: %d\n", pAdapter->mppriv.curr_ch));

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetChannel: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetChannel_parm*)_malloc(sizeof(struct SetChannel_parm));
 	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetChannel: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->curr_ch = pAdapter->mppriv.curr_ch;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);
#else
	u32 curr_ch = pAdapter->mppriv.curr_ch;
	u8 eRFPath;

	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_alert_, ("+SetChannel: %d\n", curr_ch));
	for (eRFPath = 0; eRFPath < MAX_RF_PATH_NUMS; eRFPath++) {
		set_rf_reg(pAdapter, eRFPath, rRfChannel, 0x3FF, curr_ch);
		usleep_os(100);
	}
#endif
}

void SetCCKTxPower(PADAPTER pAdapter, u8 TxPower)
{
	u16 TxAGC = 0;

	TxAGC = TxPower;
	set_bb_reg(pAdapter, rTxAGC_CCK_Mcs32, bTxAGCRateCCK, TxAGC);
	RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_notice_, ("-SetCCKTxPower: %d\n", TxPower));
}

void SetOFDMTxPower(PADAPTER pAdapter, u8 TxPower)
{
	u32 TxAGC = 0;

	TxAGC |= ((TxPower<<24)|(TxPower<<16)|(TxPower<<8)|TxPower);

	set_bb_reg(pAdapter, rTxAGC_Rate18_06, bTxAGCRate18_06, TxAGC);
	set_bb_reg(pAdapter, rTxAGC_Rate54_24, bTxAGCRate54_24, TxAGC);
	set_bb_reg(pAdapter, rTxAGC_Mcs03_Mcs00, bTxAGCRateMCS3_MCS0, TxAGC);
	set_bb_reg(pAdapter, rTxAGC_Mcs07_Mcs04, bTxAGCRateMCS7_MCS4, TxAGC);
	set_bb_reg(pAdapter, rTxAGC_Mcs11_Mcs08, bTxAGCRateMCS11_MCS8, TxAGC);
	set_bb_reg(pAdapter, rTxAGC_Mcs15_Mcs12, bTxAGCRateMCS15_MCS12, TxAGC);
	RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("-SetOFDMTxPower: %d\n", TxPower));
}

void SetTxPower(PADAPTER pAdapter)
{
#ifdef MP_FIRMWARE_OFFLOAD
	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetTxPower_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetTxPower);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("-SetTxPower: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetTxPower_parm*)_malloc(sizeof(struct SetTxPower_parm));
 	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("-SetTxPower: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->TxPower = pAdapter->mppriv.curr_txpoweridx;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);
#else

	u8 TxPower = pAdapter->mppriv.curr_txpoweridx;
#if 0
	if (pAdapter->mppriv.curr_rateidx <= MPT_RATE_11M)
	{
		SetCCKTxPower( pAdapter,TxPower);
	}
	else if ((pAdapter->mppriv.curr_rateidx>= MPT_RATE_6M) &&
		 (pAdapter->mppriv.curr_rateidx<= MPT_RATE_MCS15))
	{
		SetOFDMTxPower(pAdapter,TxPower);
	}
#else
	SetCCKTxPower(pAdapter, TxPower);
	SetOFDMTxPower(pAdapter, TxPower);
#endif
#endif
}

void SetTxAGCOffset(PADAPTER pAdapter, u32 ulTxAGCOffset)
{
	u32 TxAGCOffset_B, TxAGCOffset_C, TxAGCOffset_D,tmpAGC;

	TxAGCOffset_B = (ulTxAGCOffset&0x000000ff);
	TxAGCOffset_C = ((ulTxAGCOffset&0x0000ff00)>>8);
	TxAGCOffset_D = ((ulTxAGCOffset&0x00ff0000)>>16);

	tmpAGC = (TxAGCOffset_D<<8 | TxAGCOffset_C<<4 | TxAGCOffset_B);
	set_bb_reg(pAdapter, rFPGA0_TxGainStage,
			(bXBTxAGC|bXCTxAGC|bXDTxAGC), tmpAGC);
}

void SetDataRate(PADAPTER pAdapter)
{
#ifdef MP_FIRMWARE_OFFLOAD
#if 1
	setdatarate_cmd(pAdapter, &pAdapter->mppriv.curr_rateidx)
#else
	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct setdatarate_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetDataRate);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetDataRate: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct setdatarate_parm*)_malloc(sizeof(struct setdatarate_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetDataRate: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->curr_rateidx = pAdapter->mppriv.curr_rateidx;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);
#endif
#else
	u8 path = RF_PATH_A;
	u8 offset = 0x26;
	u32 value;
	value = (pAdapter->mppriv.curr_rateidx < 4) ? 0x4440 : 0xF200;

	rf_reg_write(pAdapter, path, offset, value);
#endif
}

void SwitchBandwidth(PADAPTER pAdapter)
{
#ifdef MP_FIRMWARE_OFFLOAD

	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SwitchBandwidth_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SwitchBandwidth);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SwitchBandwidth: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SwitchBandwidth_parm*)_malloc(sizeof(struct SwitchBandwidth_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SwitchBandwidth: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->curr_bandwidth = pAdapter->mppriv.curr_bandwidth;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);

#else
	//3 1.Set MAC register : BWOPMODE  bit2:1 20MhzBW
	u8 regBwOpMode = 0;
	u8 Bandwidth = pAdapter->mppriv.curr_bandwidth;

	regBwOpMode = read8(pAdapter, 0x10250203);

	if (Bandwidth == HT_CHANNEL_WIDTH_20) {
		regBwOpMode |= BIT(2);
	} else {
		regBwOpMode &= ~(BIT(2));
	}
	write8(pAdapter, 0x10250203, regBwOpMode);

	//3 2.Set PHY related register
	switch (Bandwidth)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			set_bb_reg(pAdapter, rFPGA0_RFMOD, bRFMOD, 0x0);
			set_bb_reg(pAdapter, rFPGA1_RFMOD, bRFMOD, 0x0);

			// Use PHY_REG.txt default value. Do not need to change.
			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			// It is set in Tx descriptor for 8192x series
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter1, bMaskDWord, 0x1a1b0000);
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord, 0x090e1317);
			//PHY_SetBBReg(Adapter, rCCK0_DebugPort, bMaskDWord, 0x00000204);
			// From SD3 WHChang
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter1, 0x00300000, 3);
			set_bb_reg(pAdapter, rFPGA0_AnalogParameter2, bMaskDWord, 0x58);

			break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			set_bb_reg(pAdapter, rFPGA0_RFMOD, bRFMOD, 0x1);
			set_bb_reg(pAdapter, rFPGA1_RFMOD, bRFMOD, 0x1);

			// Use PHY_REG.txt default value. Do not need to change.
			// Correct the tx power for CCK rate in 40M. Suggest by YN, 20071207
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter1, bMaskDWord, 0x35360000);
			//PHY_SetBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord, 0x121c252e);
			//PHY_SetBBReg(Adapter, rCCK0_DebugPort, bMaskDWord, 0x00000409);
			// From SD3 WHChang
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter1, 0x00300000, 3);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			set_bb_reg(pAdapter, rCCK0_System, bCCKSideBand, (HAL_PRIME_CHNL_OFFSET_DONT_CARE>>1));
			set_bb_reg(pAdapter, rOFDM1_LSTF, 0xC00, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

			set_bb_reg(pAdapter, rFPGA0_AnalogParameter2, bMaskDWord, 0x18);

			break;
		default:
			break;
	}

	//3 3.Set RF related register
	switch (Bandwidth)
	{
		case HT_CHANNEL_WIDTH_20:
			set_rf_reg(pAdapter, RF_PATH_A, RF_CHNLBW, BIT(10)|BIT(11), 0x01);
			break;

		case HT_CHANNEL_WIDTH_40:
			set_rf_reg(pAdapter, RF_PATH_A, RF_CHNLBW, BIT(10)|BIT(11), 0x00);
			break;

		default:
			break;
	}
#endif
}
/*------------------------------Define structure----------------------------*/
typedef struct _R_ANTENNA_SELECT_OFDM {
	u32	r_tx_antenna:4;
	u32	r_ant_l:4;
	u32	r_ant_non_ht:4;
	u32	r_ant_ht1:4;
	u32	r_ant_ht2:4;
	u32	r_ant_ht_s1:4;
	u32	r_ant_non_ht_s1:4;
	u32	OFDM_TXSC:2;
	u32	Reserved:2;
}R_ANTENNA_SELECT_OFDM;

typedef struct _R_ANTENNA_SELECT_CCK {
	u8	r_cckrx_enable_2:2;
	u8	r_cckrx_enable:2;
	u8	r_ccktx_enable:4;
}R_ANTENNA_SELECT_CCK;

void SwitchAntenna(PADAPTER pAdapter)
{
//#ifdef MP_FIRMWARE_OFFLOAD
#if 0
	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SwitchAntenna_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SwitchAntenna);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SwitchAntenna: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SwitchAntenna_parm*)_malloc(sizeof(struct SwitchAntenna_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SwitchAntenna: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->antenna_tx = pAdapter->mppriv.antenna_tx;
	pparm->antenna_rx = pAdapter->mppriv.antenna_rx;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);
#else
	u32	ofdm_tx_en_val = 0, ofdm_tx_ant_sel_val = 0;
	u8	ofdm_rx_ant_sel_val = 0;
	u8	cck_ant_select_val = 0;
	u32	cck_ant_sel_val = 0;

	R_ANTENNA_SELECT_CCK *p_cck_txrx;


	p_cck_txrx = (R_ANTENNA_SELECT_CCK*)&cck_ant_select_val;

	switch (pAdapter->mppriv.antenna_tx)
	{
		case ANTENNA_A:
			// From SD3 Willis suggestion !!! Set RF A=TX and B as standby
			set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 1);

			ofdm_tx_en_val			= 0x3;
			ofdm_tx_ant_sel_val		= 0x11111111;// Power save

			p_cck_txrx->r_ccktx_enable	= 0x8;
			break;

		case ANTENNA_B:
			set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 1);
			set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

			ofdm_tx_en_val			= 0x3;
			ofdm_tx_ant_sel_val		= 0x22222222;// Power save

			p_cck_txrx->r_ccktx_enable	= 0x4;
			break;

		case ANTENNA_AB:	// For 8192S
			set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

			ofdm_tx_en_val			= 0x3;
			ofdm_tx_ant_sel_val		= 0x3321333;	// Disable Power save

			p_cck_txrx->r_ccktx_enable	= 0xC;
			break;

		default:
			break;
	}
	set_bb_reg(pAdapter, rFPGA1_TxInfo, 0xffffffff, ofdm_tx_ant_sel_val);	//OFDM Tx
	set_bb_reg(pAdapter, rFPGA0_TxInfo, 0x0000000f, ofdm_tx_en_val);	//OFDM Tx

	switch (pAdapter->mppriv.antenna_rx)
	{
		case ANTENNA_A:
			ofdm_rx_ant_sel_val		= 0x1;	// A
			p_cck_txrx->r_cckrx_enable 	= 0x0;	// default: A
			p_cck_txrx->r_cckrx_enable_2	= 0x0;	// option: A
			break;

		case ANTENNA_B:
			ofdm_rx_ant_sel_val		= 0x2;	// B
			p_cck_txrx->r_cckrx_enable 	= 0x1;	// default: B
			p_cck_txrx->r_cckrx_enable_2	= 0x1;	// option: B
			break;

		case ANTENNA_AB:
			ofdm_rx_ant_sel_val		= 0x3;	// AB
			p_cck_txrx->r_cckrx_enable 	= 0x0;	// default:A
			p_cck_txrx->r_cckrx_enable_2	= 0x1;	// option:B
			break;

		default:
			break;
	}
	set_bb_reg(pAdapter, rOFDM0_TRxPathEnable, 0x0000000f, ofdm_rx_ant_sel_val);	//OFDM Rx
	set_bb_reg(pAdapter, rOFDM1_TRxPathEnable, 0x0000000f, ofdm_rx_ant_sel_val);	//OFDM Rx

	cck_ant_sel_val = cck_ant_select_val;
	set_bb_reg(pAdapter, rCCK0_AFESetting, bMaskByte3, cck_ant_sel_val);		//CCK TxRx
#endif

	RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}

void SetCrystalCap(PADAPTER pAdapter)
{
#ifdef MP_FIRMWARE_OFFLOAD
	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetCrystalCap_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetCrystalCap);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetCrystalCap: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetCrystalCap_parm*)_malloc(sizeof(struct SetCrystalCap_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetCrystalCap: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->curr_crystalcap = pAdapter->mppriv.curr_crystalcap;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);
#else
	set_bb_reg(pAdapter, rFPGA0_AnalogParameter1, bXtalCap, pAdapter->mppriv.curr_crystalcap);
	RT_TRACE(_module_rtl871x_mp_c_, _drv_notice_, ("-SetCrystalCap: %d\n", pAdapter->mppriv.curr_crystalcap));
#endif
}

void TriggerRFThermalMeter(PADAPTER pAdapter)
{
	set_rf_reg(pAdapter, RF_PATH_A, RF_T_METER, bRFRegOffsetMask, 0x60);	// 0x24: RF Reg[6:5]
//	RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("TriggerRFThermalMeter() finished.\n" ));
}

u32 ReadRFThermalMeter(PADAPTER pAdapter)
{
	u32 ThermalValue = 0;

	ThermalValue = get_rf_reg(pAdapter, RF_PATH_A, RF_T_METER, 0x1F);	// 0x24: RF Reg[4:0]
//	RT_TRACE(_module_rtl871x_mp_c_, _drv_alert_, ("ThermalValue = 0x%x\n", ThermalValue));
	return ThermalValue;
}

void GetThermalMeter(PADAPTER pAdapter, u32 *value)
{
#if 0
	fw_cmd(pAdapter, IOCMD_GET_THERMAL_METER);
	msleep_os(1000);
	fw_cmd_data(pAdapter, value, 1);
	*value &= 0xFF;
#else
	TriggerRFThermalMeter(pAdapter);
	msleep_os(1000);
	*value = ReadRFThermalMeter(pAdapter);
#endif
}

void SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
#ifdef MP_FIRMWARE_OFFLOAD

	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetSingleCarrierTx_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetSingleCarrierTx);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetSingleCarrierTx: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetSingleCarrierTx_parm*)_malloc(sizeof(struct SetSingleCarrierTx_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetSingleCarrierTx: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->bStart = bStart;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);

#else

	if (bStart)// Start Single Carrier.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetSingleCarrierTx test start.........\n"));
		// 1. if OFDM block on?
		if(!get_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on

		// 2. set CCK test mode off, set to CCK normal mode
		set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

		// 3. turn on scramble setting
		set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, bEnable);

		// 4. Turn On Continue Tx and turn off the other test modes.
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bEnable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
	}
	else// Stop Single Carrier.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetSingleCarrierTx test stop.........\n"));
		//Turn off all test modes.
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

		//Delay 10 ms //delay_ms(10);
		msleep_os(10);

		//BB Reset
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
#endif
}

void SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
#ifdef MP_FIRMWARE_OFFLOAD

	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetSingleToneTx_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetSingleToneTx);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetSingleToneTx: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetSingleToneTx_parm*)_malloc(sizeof(struct SetSingleToneTx_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetSingleToneTx: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->bStart = bStart;
	switch (pAdapter->mppriv.antenna_tx)
	{
		case ANTENNA_B:
			pparm->curr_rfpath = RF_PATH_B;
			break;
		case ANTENNA_A:
		default:
			pparm->curr_rfpath = RF_PATH_A;
			break;
	}

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);

#else
	u8 rfPath = pAdapter->mppriv.curr_rfpath;

	switch (pAdapter->mppriv.antenna_tx)
	{
		case ANTENNA_B:
			rfPath = RF_PATH_B;
			break;
		case ANTENNA_A:
		default:
			rfPath = RF_PATH_A;
			break;
	}

	if (bStart)// Start Single Tone.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetSingleToneTx test start.........\n"));
		set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x0);
		set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x0);
		//set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMTxDACPhase, 0x1);

		set_rf_reg(pAdapter, rfPath, 0x21, bRFRegOffsetMask, 0xd4000);
		usleep_os(100);
		set_rf_reg(pAdapter, rfPath, 0x00, bRFRegOffsetMask, 0x2001f); // PAD all on.
		usleep_os(100);
	}
	else// Stop Single Tone.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetSingleToneTx test stop.........\n"));
		set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x1);
		set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
		//set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMTxDACPhase, 0x1);
		set_rf_reg(pAdapter, rfPath, 0x21, bRFRegOffsetMask, 0x54000);
		usleep_os(100);
		set_rf_reg(pAdapter, rfPath, 0x00, bRFRegOffsetMask, 0x30000); // PAD all on.
		usleep_os(100);
	}
#endif
}

void SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
#ifdef MP_FIRMWARE_OFFLOAD

	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetCarrierSuppressionTx_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetCarrierSuppressionTx);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetSingleToneTx: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetCarrierSuppressionTx_parm*)_malloc(sizeof(struct SetCarrierSuppressionTx_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetCarrierSuppressionTx: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->bStart = bStart;
	pparm->curr_rateidx = pAdapter->mppriv.curr_rateidx;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);

#else

	if (bStart) // Start Carrier Suppression.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetCarrierSuppressionTx test start.........\n"));
		//if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
		if (pAdapter->mppriv.curr_rateidx <= MPT_RATE_11M) {
			// 1. if CCK block on?
			if(!get_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn))
				set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

			//Turn Off All Test Mode
			set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
			set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
			set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

			set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    //transmit mode
			set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  //turn off scramble setting

			//Set CCK Tx Test Rate
			//PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, pMgntInfo->ForcedDataRate);
			set_bb_reg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    //Set FTxRate to 1Mbps
		}
	}
	else// Stop Carrier Suppression.
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetCarrierSuppressionTx test stop.........\n"));
		//if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
		if (pAdapter->mppriv.curr_rateidx <= MPT_RATE_11M ) {
			set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    //normal mode
			set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  //turn on scramble setting

			//BB Reset
			set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
			set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
		}
	}
	//DbgPrint("\n MPT_ProSetCarrierSupp() is finished. \n");
#endif
}

void SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	u32 cckrate;

	if (bStart)
	{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetCCKContinuousTx test start.........\n"));

		// 1. if CCK block on?
		if(!get_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn))
			set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

		//Turn Off All Test Mode
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		//Set CCK Tx Test Rate
		#if 0
		switch(pAdapter->mppriv.curr_rateidx)
		{
			case 2:
				cckrate = 0;
				break;
			case 4:
				cckrate = 1;
				break;
			case 11:
				cckrate = 2;
				break;
			case 22:
				cckrate = 3;
				break;
			default:
				cckrate = 0;
				break;
		}
		#else
		cckrate  = pAdapter->mppriv.curr_rateidx;
		#endif
		set_bb_reg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);
		set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	//transmit mode
		set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	//turn on scramble setting
	}
	else{
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetCCKContinuousTx test stop.........\n"));
		set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	//normal mode
		set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	//turn on scramble setting

		//BB Reset
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
}/* mpt_StartCckContTx */

void SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	if (bStart) {
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetOFDMContinuousTx test start.........\n"));
		// 1. if OFDM block on?
		if(!get_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on

		// 2. set CCK test mode off, set to CCK normal mode
		set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

		// 3. turn on scramble setting
		set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, bEnable);

		// 4. Turn On Continue Tx and turn off the other test modes.
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bEnable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
	} else {
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetOFDMContinuousTx test stop.........\n"));
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		set_bb_reg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		//Delay 10 ms
		msleep_os(10);
		//BB Reset
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
}/* mpt_StartOfdmContTx */

void SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
#ifdef MP_FIRMWARE_OFFLOAD

	struct cmd_priv *pcmdpriv = &pAdapter->cmdpriv;
	struct cmd_obj *pcmd = NULL;
	struct SetContinuousTx_parm *pparm = NULL;
	u16 code = GEN_CMD_CODE(_SetContinuousTx);

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetContinuousTx: memory allocate for cmd_obj fail!!!\n"));
		return;
	}

	pparm = (struct SetContinuousTx_parm*)_malloc(sizeof(struct SetContinuousTx_parm));
	if (pparm == NULL) {
		if (pcmd != NULL)
			_mfree((u8*)pcmd, sizeof(struct cmd_obj));
		RT_TRACE(_module_rtl871x_mp_c_, _drv_err_,
			 ("SetContinuousTx: memory allocate for parm fail!!!\n"));
		return;
	}
	pparm->bStart = bStart;
	pparm->CCK_flag = 1;	// CCK
	pparm->curr_rateidx = pAdapter->mppriv.curr_rateidx;
	if ((pparm->curr_rateidx >= MPT_RATE_6M) &&
	    (pparm->curr_rateidx <= MPT_RATE_MCS15))
		pparm->CCK_flag = 2;	// OFDM

	init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code);
	enqueue_cmd(pcmdpriv, pcmd);

#else
	// ADC turn off [bit24-21] adc port0 ~ port1
	if (bStart) {
		bb_reg_write(pAdapter, rRx_Wait_CCCA, bb_reg_read(pAdapter, rRx_Wait_CCCA) & 0xFE1FFFFF);
		usleep_os(100);
	}
	RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("SetContinuousTx #2 rate:%d........\n", pAdapter->mppriv.curr_rateidx));
	if (pAdapter->mppriv.curr_rateidx <= MPT_RATE_11M)
	{
		SetCCKContinuousTx(pAdapter, bStart);
	}
	else if ((pAdapter->mppriv.curr_rateidx >= MPT_RATE_6M) &&
		 (pAdapter->mppriv.curr_rateidx <= MPT_RATE_MCS15))
	{
		SetOFDMContinuousTx(pAdapter, bStart);
	}
	// ADC turn on [bit24-21] adc port0 ~ port1
	if (!bStart) {
		bb_reg_write(pAdapter, rRx_Wait_CCCA, bb_reg_read(pAdapter, rRx_Wait_CCCA) | 0x01E00000);
	}
#endif
}

void ResetPhyRxPktCount(PADAPTER pAdapter)
{
	u32 i, phyrx_set = 0;

	for (i = OFDM_PPDU_BIT; i <= HT_MPDU_FAIL_BIT; i++) {
		phyrx_set = 0;
		phyrx_set |= (i << 28);		//select
		phyrx_set |= 0x08000000;	// set counter to zero
		write32(pAdapter, RXERR_RPT, phyrx_set);
	}
}

static u32 GetPhyRxPktCounts(PADAPTER pAdapter, u32 selbit)
{
	//selection
	u32 phyrx_set = 0, count = 0;
	u32 SelectBit;

	SelectBit = selbit << 28;
	phyrx_set |= (SelectBit & 0xF0000000);

	write32(pAdapter, RXERR_RPT, phyrx_set);

	//Read packet count
	count = read32(pAdapter, RXERR_RPT) & RPTMaxCount;

	return count;
}

u32 GetPhyRxPktReceived(PADAPTER pAdapter)
{
	u32 OFDM_cnt = 0, CCK_cnt = 0, HT_cnt = 0;

	OFDM_cnt = GetPhyRxPktCounts(pAdapter, OFDM_MPDU_OK_BIT);
	CCK_cnt = GetPhyRxPktCounts(pAdapter, CCK_MPDU_OK_BIT);
	HT_cnt = GetPhyRxPktCounts(pAdapter, HT_MPDU_OK_BIT);

	return OFDM_cnt + CCK_cnt + HT_cnt;
}

u32 GetPhyRxPktCRC32Error(PADAPTER pAdapter)
{
	u32 OFDM_cnt = 0, CCK_cnt = 0, HT_cnt = 0;

	OFDM_cnt = GetPhyRxPktCounts(pAdapter, OFDM_MPDU_FAIL_BIT);
	CCK_cnt = GetPhyRxPktCounts(pAdapter, CCK_MPDU_FAIL_BIT);
	HT_cnt = GetPhyRxPktCounts(pAdapter, HT_MPDU_FAIL_BIT);

	return OFDM_cnt + CCK_cnt + HT_cnt;
}

/*-----------------------------------------------------------------------------
 * Function:	PHY_IQCalibrateBcut()
 *
 * Overview:	After all MAC/PHY/RF is configued. We must execute IQ calibration
 *			to improve RF EVM!!?
 *
 * Input:		IN	PADAPTER	pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	11/18/2008	MHC		Create. Document from SD3 RFSI Jenyu.
 *						92S B-cut QFN 68 pin IQ calibration procedure.doc
 *
 *---------------------------------------------------------------------------*/
typedef enum _RF90_RADIO_PATH {
	RF90_PATH_A = 0,	//Radio Path A
	RF90_PATH_B = 1,	//Radio Path B
	RF90_PATH_C = 2,	//Radio Path C
	RF90_PATH_D = 3,	//Radio Path D
	RF90_PATH_MAX		//Max RF number 90 support
}RF90_RADIO_PATH_E, *PRF90_RADIO_PATH_E;

void IQCalibrateBcut(PADAPTER pAdapter)
{
	u32	i, reg;
	u32	old_value;
	s32	X, Y, TX0[4];
	u32	TXA[4];
	u16	calibrate_set[13] = {0};
	u32	load_value[13];

	RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("@@@@@@ IQCalibrateBcut Start... \n"));
	// 0. Check QFN68 or 64 92S (Read from EEPROM/EFUSE)

	//
	// 1. Save e70~ee0 register setting, and load calibration setting
	//
	calibrate_set [0] = 0xee0;
	calibrate_set [1] = 0xedc;
	calibrate_set [2] = 0xe70;
	calibrate_set [3] = 0xe74;
	calibrate_set [4] = 0xe78;
	calibrate_set [5] = 0xe7c;
	calibrate_set [6] = 0xe80;
	calibrate_set [7] = 0xe84;
	calibrate_set [8] = 0xe88;
	calibrate_set [9] = 0xe8c;
	calibrate_set [10] = 0xed0;
	calibrate_set [11] = 0xed4;
	calibrate_set [12] = 0xed8;

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Save e70~ee0 register setting\n"));
	for (i = 0; i < 13; i++)
	{
		load_value[i] = get_bb_reg(pAdapter, calibrate_set[i], bMaskDWord);
		set_bb_reg(pAdapter, calibrate_set[i], bMaskDWord, 0x3fed92fb);
	}

	//
	// 2. QFN 68
	//
	// For 1T2R IQK only now !!!
	for (i = 0; i < 10; i++)
	{
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("IQK -%d\n", i));
		RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("@@@@@@ IQK -%d\n", i));

		//BB switch to PI mode. If default is PI mode, ignoring 2 commands below.
		//if (pMgntInfo->bRFSiOrPi == 0)	// SI
		{
			set_bb_reg(pAdapter, 0x820, bMaskDWord, 0x01000100);
			set_bb_reg(pAdapter, 0x828, bMaskDWord, 0x01000100);
		}

		// IQK
		// 2. IQ calibration & LO leakage calibration
		set_bb_reg(pAdapter, 0xc04, bMaskDWord, 0x00a05430);

		udelay_os(5);
		set_bb_reg(pAdapter, 0xc08, bMaskDWord, 0x000800e4);

		udelay_os(5);
		set_bb_reg(pAdapter, 0xe28, bMaskDWord, 0x80800000);
		udelay_os(5);
		//path-A IQ K and LO K gain setting
		set_bb_reg(pAdapter, 0xe40, bMaskDWord, 0x02140102);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xe44, bMaskDWord, 0x681604c2);
		udelay_os(5);
		//set LO calibration
		set_bb_reg(pAdapter, 0xe4c, bMaskDWord, 0x000028d1);
		udelay_os(5);
		//path-B IQ K and LO K gain setting
		set_bb_reg(pAdapter, 0xe60, bMaskDWord, 0x02140102);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xe64, bMaskDWord, 0x28160d05);
		udelay_os(5);
		//K idac_I & IQ
		set_bb_reg(pAdapter, 0xe48, bMaskDWord, 0xfb000000);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xe48, bMaskDWord, 0xf8000000);
		udelay_os(5);

		// delay 2ms
		udelay_os(2000);

		//idac_Q setting
		set_bb_reg(pAdapter, 0xe6c, bMaskDWord, 0x020028d1);
		udelay_os(5);
		//K idac_Q & IQ
		set_bb_reg(pAdapter, 0xe48, bMaskDWord, 0xfb000000);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xe48, bMaskDWord, 0xf8000000);

		// delay 2ms
		udelay_os(2000);

		set_bb_reg(pAdapter, 0xc04, bMaskDWord, 0x00a05433);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xc08, bMaskDWord, 0x000000e4);
		udelay_os(5);
		set_bb_reg(pAdapter, 0xe28, bMaskDWord, 0x0);

		//f (pMgntInfo->bRFSiOrPi == 0)	// SI
		{
			set_bb_reg(pAdapter, 0x820, bMaskDWord, 0x01000000);
			set_bb_reg(pAdapter, 0x828, bMaskDWord, 0x01000000);
		}

		reg = get_bb_reg(pAdapter, 0xeac, bMaskDWord);

		// 3.	check fail bit, and fill BB IQ matrix
		// Readback IQK value and rewrite
		if (!(reg&(BIT(27)|BIT(28)|BIT(30)|BIT(31))))
		{
			old_value = (get_bb_reg(pAdapter, 0xc80, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX0
			X = (get_bb_reg(pAdapter, 0xe94, bMaskDWord) & 0x03FF0000)>>16;

			TXA[RF90_PATH_A] = (X * old_value)/0x100;

			reg = get_bb_reg(pAdapter, 0xc80, bMaskDWord);
			reg = (reg & 0xFFFFFC00) | (u32)TXA[RF90_PATH_A];
			set_bb_reg(pAdapter, 0xc80, bMaskDWord, reg);
			udelay_os(5);

			// Calibrate init gain for C path for TX0
			Y = ( get_bb_reg(pAdapter, 0xe9C, bMaskDWord) & 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = get_bb_reg(pAdapter, 0xc80, bMaskDWord);
			reg = (reg & 0xffc0ffff) |((u32) (TX0[RF90_PATH_C]&0x3F)<<16);
			set_bb_reg(pAdapter, 0xc80, bMaskDWord, reg);
			reg = get_bb_reg(pAdapter, 0xc94, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			set_bb_reg(pAdapter, 0xc94, bMaskDWord, reg);
			udelay_os(5);

			// Calibrate RX A and B for RX0
			reg = get_bb_reg(pAdapter, 0xc14, bMaskDWord);
			X = (get_bb_reg(pAdapter, 0xea4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			set_bb_reg(pAdapter, 0xc14, bMaskDWord, reg);
			Y = (get_bb_reg(pAdapter, 0xeac, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |(Y<<10);
			set_bb_reg(pAdapter, 0xc14, bMaskDWord, reg);
			udelay_os(5);
			old_value = (get_bb_reg(pAdapter, 0xc88, bMaskDWord) & 0x3FF);

			// Calibrate init gain for A path for TX1 !!!!!!
			X = (get_bb_reg(pAdapter, 0xeb4, bMaskDWord) & 0x03FF0000)>>16;
			reg = get_bb_reg(pAdapter, 0xc88, bMaskDWord);
			TXA[RF90_PATH_A] = (X * old_value) / 0x100;
			reg = (reg & 0xFFFFFC00) | TXA[RF90_PATH_A];
			set_bb_reg(pAdapter, 0xc88, bMaskDWord, reg);
			udelay_os(5);

			// Calibrate init gain for C path for TX1
			Y = (get_bb_reg(pAdapter, 0xebc, bMaskDWord)& 0x03FF0000)>>16;
			TX0[RF90_PATH_C] = ((Y * old_value)/0x100);
			reg = get_bb_reg(pAdapter, 0xc88, bMaskDWord);
			reg = (reg & 0xffc0ffff) |( (TX0[RF90_PATH_C]&0x3F)<<16);
			set_bb_reg(pAdapter, 0xc88, bMaskDWord, reg);
			reg = get_bb_reg(pAdapter, 0xc9c, bMaskDWord);
			reg = (reg & 0x0fffffff) |(((Y&0x3c0)>>6)<<28);
			set_bb_reg(pAdapter, 0xc9c, bMaskDWord, reg);
			udelay_os(5);

			// Calibrate RX A and B for RX1
			reg = get_bb_reg(pAdapter, 0xc1c, bMaskDWord);
			X = (get_bb_reg(pAdapter, 0xec4, bMaskDWord) & 0x03FF0000)>>16;
			reg = (reg & 0xFFFFFC00) |X;
			set_bb_reg(pAdapter, 0xc1c, bMaskDWord, reg);

			Y = (get_bb_reg(pAdapter, 0xecc, bMaskDWord) & 0x003F0000)>>16;
			reg = (reg & 0xFFFF03FF) |Y<<10;
			set_bb_reg(pAdapter, 0xc1c, bMaskDWord, reg);
			udelay_os(5);

			//RT_TRACE(COMP_INIT, DBG_LOUD, ("PHY_IQCalibrate OK\n"));
			RT_TRACE(_module_rtl871x_mp_c_,_drv_alert_, ("@@@@@@ PHY_IQCalibrate OK\n"));
			break;
		}
	}

	//
	// 4. Reload e70~ee0 register setting.
	//
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Reload e70~ee0 register setting.\n"));
	for (i = 0; i < 13; i++)
		set_bb_reg(pAdapter, calibrate_set[i], bMaskDWord, load_value[i]);

	//
	// 3. QFN64. Not enabled now !!! We must use different gain table for 1T2R.
	//

}	// PHY_IQCalibrateBcut

#endif

