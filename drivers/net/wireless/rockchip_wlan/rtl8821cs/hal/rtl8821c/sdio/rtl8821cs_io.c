/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#define _RTL8821CS_IO_C_

#include <drv_types.h>		/* PADAPTER and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include <rtw_sdio.h>		/* rtw_sdio_write_cmd53() */
#include <sdio_ops_linux.h>	/* SDIO_ERR_VAL8 and etc. */
#include "rtl8821cs.h"		/* rtl8821cs_get_interrupt(), rtl8821cs_clear_interrupt() */
#include "rtl8821cs_recv.h"	/* rtl8821cs_rxhandler() */
#include "../../hal_halmac.h"	/* rtw_halmac_sdio_get_rx_addr and etc. */

/*#define SDIO_DEBUG_IO 1*/

/*
 * For Core I/O API
 */

static u8 sdio_f0_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	struct dvobj_priv *d;
	u8 val = 0;
	u8 ret;


	d = pintfhdl->pintf_dev;
	ret = rtw_sdio_f0_read(d, addr, &val, 1);
	if (_FAIL == ret)
		RTW_ERR("%s: Read f0 register(0x%x) FAIL!\n",
			__FUNCTION__, addr);

	return val;
}

/*
 * Description:
 *	Read from RX FIFO
 *	Round read size to block size,
 *	and make sure data transfer will be done in one command.
 *
 * Parameters:
 *	pintfhdl	a pointer of intf_hdl
 *	addr		port ID
 *	cnt		size to read
 *	mem		address to put data
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
/* #define CONFIG_NEW_SDIO_RP_FUNC*/ /*will cause system crash*/
#ifdef CONFIG_NEW_SDIO_RP_FUNC
static s32 _sdio_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *mem)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	u32 rxaddr;

	size_t buflen;
	u32 ret;


	d = pintfhdl->pintf_dev;
	hal = GET_HAL_DATA(pintfhdl->padapter);

	cnt = _RND4(cnt);
	rxaddr = rtw_halmac_sdio_get_rx_addr(d, &hal->SdioRxFIFOCnt);

	/* align size to guarantee I/O would be done in one command */
	buflen = rtw_sdio_cmd53_align_size(d, cnt);

	if (buflen >= (MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ)) {
		RTW_INFO("%s [ERROR] buflen(%zu) > skb_len(%d), may cause memory overwrite\n"
			, __func__, buflen, (MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ));
		rtw_warn_on(1);
		buflen = MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ;
	}

	ret = rtw_sdio_read_cmd53(d, rxaddr, mem, buflen);

	return ret;
}
#else
static u32 _sdio_read_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem)
{
	struct dvobj_priv *d = pintfhdl->pintf_dev;
	PADAPTER padapter = pintfhdl->padapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	s32 err;

	addr = rtw_halmac_sdio_get_rx_addr(d, &pHalData->SdioRxFIFOCnt);

	cnt = _RND4(cnt);
	cnt = rtw_sdio_cmd53_align_size(d, cnt);

	err = _sd_read(pintfhdl, addr, cnt, mem);
	/*err = sd_read(pintfhdl, addr, cnt, mem);*/



	if (err)
		return _FAIL;
	return _SUCCESS;
}
#endif
/*
 * Description:
 *	Read from RX FIFO
 *	Round read size to block size,
 *	and make sure data transfer will be done in one command.
 *
 * Parameters:
 *	pintfhdl	a pointer of intf_hdl
 *	addr		port ID
 *	cnt		size to read
 *	mem		struct recv_buf*
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
static u32 sdio_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *mem)
{
	return _sdio_read_port(pintfhdl, addr, cnt, mem);
#if 0
	RTW_INFO("SDIO can not handle recvbuf->pbuf\n");
	rtw_warn_on(1);
	return _FALSE;

	struct recv_buf *recvbuf;

	recvbuf = (struct recv_buf *)mem;
	return _sdio_read_port(pintfhdl, addr, cnt, recvbuf->pbuf);
#endif
}

/*
 * Description:
 *	Write to TX FIFO
 *	Align write size to block size,
 *	and check enough FIFO size to write.
 *
 * Parameters:
 *	d		a pointer of dvobj_priv
 *	addr		not use
 *	cnt		size to write
 *	mem		buffer to write
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
u32 rtl8821cs_write_port(struct dvobj_priv *d, u32 cnt, u8 *mem)
{
	u32 txaddr, txsize;
	u32 ret = _FAIL;

	cnt = _RND4(cnt);
	txaddr = rtw_halmac_sdio_get_tx_addr(d, mem, cnt);
	if (!txaddr)
		goto exit;

	/* align size to guarantee I/O would be done in one command */
	txsize = rtw_sdio_cmd53_align_size(d, cnt);

	ret = rtw_sdio_write_cmd53(d, txaddr, mem, txsize);

exit:

	return ret;
}
#ifdef CONFIG_NEW_SDIO_WP_FUNC
/*
 * Description:
 *	Write to TX FIFO
 *	Align write size to block size,
 *	and check enough FIFO size to write.
 *
 * Parameters:
 *	pintfhdl	a pointer of intf_hdl
 *	addr		not use
 *	cnt		size to write
 *	mem		struct xmit_buf*
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
static u32 sdio_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *mem)
{
	struct dvobj_priv *d;
	PADAPTER adapter;
	struct xmit_buf *xmitbuf;
	u32 txaddr, txsize;
	u32 ret = _FAIL;


	d = pintfhdl->pintf_dev;
	adapter = pintfhdl->padapter;
	xmitbuf = (struct xmit_buf *)mem;

#if 0 /* who will call this when hardware not be initialized? */
	if (!rtw_is_hw_init_completed(adapter)) {
		RTW_INFO("%s [addr=0x%x cnt=%d] adapter->hw_init_completed == _FALSE\n",
			 __FUNCTION__, addr, cnt);
		goto exit;
	}
#endif

	ret = rtl8821cs_write_port(d, cnt, xmitbuf->pdata);

	rtw_sctx_done_err(&xmitbuf->sctx,
		(_FAIL == ret) ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);

	return ret;
}
#else
static u32 sdio_write_port(
	struct intf_hdl *pintfhdl,
	u32 addr,
	u32 cnt,
	u8 *mem)
{
	PADAPTER padapter;
	s32 err;
	struct xmit_buf *xmitbuf = (struct xmit_buf *)mem;

	padapter = pintfhdl->padapter;

	if (!rtw_is_hw_init_completed(padapter)) {
		RTW_INFO("%s [addr=0x%x cnt=%d] padapter->hw_init_completed == _FALSE\n", __func__, addr, cnt);
		return _FAIL;
	}

	cnt = _RND4(cnt);
	addr = rtw_halmac_sdio_get_tx_addr(adapter_to_dvobj(padapter), xmitbuf->pdata, cnt);
	if (!addr) {
		RTW_ERR("%s, addr error=%d\n", __func__, addr);

		return _FAIL;
	}

	cnt = rtw_sdio_cmd53_align_size(adapter_to_dvobj(padapter), cnt);

	err = sd_write(pintfhdl, addr, cnt, xmitbuf->pdata);

	rtw_sctx_done_err(&xmitbuf->sctx,
		 err ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);

	if (err) {
		RTW_ERR("%s, error=%d\n", __func__, err);

		return _FAIL;
	}
	return _SUCCESS;
}
#endif

void sdio_set_intf_ops(PADAPTER adapter, struct _io_ops *pops)
{

	pops->_read8 = rtw_halmac_read8;
	pops->_read16 = rtw_halmac_read16;
	pops->_read32 = rtw_halmac_read32;
	pops->_read_mem = rtw_halmac_read_mem;
	pops->_read_port = sdio_read_port;

	pops->_write8 = rtw_halmac_write8;
	pops->_write16 = rtw_halmac_write16;
	pops->_write32 = rtw_halmac_write32;
	pops->_writeN = NULL;
	pops->_write_mem = NULL;
	pops->_write_port = sdio_write_port;

	pops->_sd_f0_read8 = sdio_f0_read8;

#ifdef CONFIG_SDIO_INDIRECT_ACCESS
	pops->_sd_iread8 = rtw_halmac_iread8;
	pops->_sd_iread16 = rtw_halmac_iread16;
	pops->_sd_iread32 = rtw_halmac_iread32;
	pops->_sd_iwrite8 = rtw_halmac_write8;
	pops->_sd_iwrite16 = rtw_halmac_write16;
	pops->_sd_iwrite32 = rtw_halmac_write32;
#endif /* CONFIG_SDIO_INDIRECT_ACCESS */

}
#ifdef CONFIG_SDIO_RX_COPY
static u32 sd_recv_rxfifo(PADAPTER padapter, u32 size, struct recv_buf **recvbuf_ret)
{
	u32 readsize, ret = _SUCCESS;
	u8 *preadbuf;
	struct recv_priv *precvpriv;
	struct recv_buf	*precvbuf;

	*recvbuf_ret = NULL;

	readsize = size;

	if (readsize > MAX_RECVBUF_SZ) {
		RTW_ERR(FUNC_ADPT_FMT" %u\n", FUNC_ADPT_ARG(padapter), readsize);
		rtw_warn_on(readsize > MAX_RECVBUF_SZ);
	}

	/*1. alloc recvbuf*/
	precvpriv = &padapter->recvpriv;
	precvbuf = rtw_dequeue_recvbuf(&precvpriv->free_recv_buf_queue);
	if (precvbuf == NULL) {
		/*RTW_ERR("%s: recvbuf unavailable\n", __func__);*/
		return RTW_RBUF_UNAVAIL;
	}

	/* 2. alloc skb*/
	if (precvbuf->pskb == NULL) {
		SIZE_PTR tmpaddr = 0;
		SIZE_PTR alignment = 0;

		RTW_WARN("%s- [WARN] - alloc_skb for rx buffer\n", __func__);

		precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

		if (precvbuf->pskb == NULL) {
			RTW_ERR("%s: alloc_skb fail! read=%d\n", __func__, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
			rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
			return RTW_RBUF_PKT_UNAVAIL;
		}

		if (precvbuf->pskb) {
			precvbuf->pskb->dev = padapter->pnetdev;

			tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
			skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

			precvbuf->phead = precvbuf->pskb->head;
			precvbuf->pdata = precvbuf->pskb->data;
			precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
			precvbuf->pend = skb_end_pointer(precvbuf->pskb);
			precvbuf->len = 0;
		}

	}

	/*3. read data from rxfifo*/
	preadbuf = precvbuf->pdata;
	ret = _sdio_read_port(&padapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	if (ret == _FAIL) {
		RTW_ERR("%s: sdio read port FAIL!\n", __func__);
		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
		return RTW_SDIO_READ_PORT_FAIL;
	}

	/*4. init recvbuf*/
	precvbuf->len = readsize;
	*recvbuf_ret = precvbuf;

	return ret;
}
static u32 sd_recv_and_drop(PADAPTER adapter, u32 size)
{
	u32 readsize, buflen;
	u8 *rbuf;
	s32 ret = _FAIL;

	readsize = RND4(size);
	buflen = rtw_sdio_cmd53_align_size(adapter_to_dvobj(adapter), readsize);

	if (buflen > MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ) {
		RTW_ERR(FUNC_ADPT_FMT" %u\n", FUNC_ADPT_ARG(adapter), readsize);
		rtw_warn_on(readsize > MAX_RECVBUF_SZ);
	}

	rbuf = rtw_zmalloc(buflen);
	if (rbuf) {
		if (_sdio_read_port(&adapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, buflen, rbuf) == _FAIL) {
			RTW_ERR("%s: sdio read port FAIL!\n", __func__);
			ret = RTW_SDIO_READ_PORT_FAIL;
		} else
			ret = _SUCCESS;

		rtw_mfree(rbuf, buflen);
	}

	return ret;
}

#else
static struct recv_buf *sd_recv_rxfifo(PADAPTER padapter, u32 size)
{
	u32 readsize, allocsize, ret;
	u8 *preadbuf;
	_pkt *ppkt;
	struct recv_priv *precvpriv;
	struct recv_buf	*precvbuf;


	readsize = size;

	/*1.alloc skb*/
	/* align to block size*/
	allocsize = _RND(readsize, rtw_sdio_get_block_size(adapter_to_dvobj(padapter)));

	ppkt = rtw_skb_alloc(allocsize);

	if (ppkt == NULL) {
		RTW_INFO("%s: alloc_skb fail! alloc=%d read=%d\n", __func__, allocsize, readsize);
		return NULL;
	}

	/*2. read data from rxfifo*/
	preadbuf = skb_put(ppkt, readsize);
	/*	rtw_read_port(padapter, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf); */
	ret = _sdio_read_port(&padapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, readsize, preadbuf);
	if (ret == _FAIL) {
		rtw_skb_free(ppkt);
		RTW_INFO("%s: read port FAIL!\n", __func__);
		return NULL;
	}

	/*3. alloc recvbuf*/
	precvpriv = &padapter->recvpriv;
	precvbuf = rtw_dequeue_recvbuf(&precvpriv->free_recv_buf_queue);
	if (precvbuf == NULL) {
		rtw_skb_free(ppkt);
		/*RTW_INFO("%s: alloc recvbuf FAIL!\n", __func__);*/
		return NULL;
	}

	/*4. init recvbuf*/
	precvbuf->pskb = ppkt;

	precvbuf->len = ppkt->len;

	precvbuf->phead = ppkt->head;
	precvbuf->pdata = ppkt->data;
	precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
	precvbuf->pend = skb_end_pointer(precvbuf->pskb);

	return precvbuf;
}
#endif

#if 0
static struct recv_buf *sd_recv_rxfifo(PADAPTER adapter, u32 size)
{
	struct recv_priv *recvpriv;
	struct recv_buf	*recvbuf;
	u32 readsz, bufsz;
	u8 *rbuf;
	_pkt *pkt;
	s32 ret;


	/*
	 * Patch for some SDIO Host 4 bytes issue
	 * ex. RK3188
	 */
	readsz = RND4(size);

	/* round to block size */
	bufsz = rtw_sdio_cmd53_align_size(adapter_to_dvobj(padapter), readsz);

	/*1.alloc recvbuf*/
	recvpriv = &adapter->recvpriv;
	recvbuf = rtw_dequeue_recvbuf(&recvpriv->free_recv_buf_queue);
	if (recvbuf == NULL) {
		RTW_INFO("%s: <ERR> alloc recvbuf FAIL!\n", __FUNCTION__);
		return NULL;
	}

	/*2.alloc skb*/
	pkt = rtl8821cs_alloc_recvbuf_skb(recvbuf, bufsz);
	if (!pkt) {
		RTW_INFO("%s: <ERR> alloc_skb fail! alloc=%d read=%d\n", __FUNCTION__, bufsz, size);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
		return NULL;
	}

	/* 3.read data from rxfifo*/
	rbuf = skb_put(pkt, size);
	ret = _sdio_read_port(&adapter->iopriv.intf, WLAN_RX0FF_DEVICE_ID, bufsz, rbuf);
	if (_FAIL == ret) {
		RTW_INFO("%s: <ERR> read port FAIL!\n", __FUNCTION__);
		rtl8821cs_free_recvbuf_skb(recvbuf);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
		return NULL;
	}

	/*4.init recvbuf*/
	recvbuf->len = pkt->len;
	recvbuf->phead = pkt->head;
	recvbuf->pdata = pkt->data;
	recvbuf->ptail = skb_tail_pointer(pkt);
	recvbuf->pend = skb_end_pointer(pkt);

	return recvbuf;
}
#endif

void sd_int_dpc(PADAPTER adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	HAL_DATA_TYPE	*phal_Data = GET_HAL_DATA(adapter);
	struct intf_hdl *pintfhdl = &adapter->iopriv.intf;


#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
	if (phal_Data->sdio_hisr & BIT_SDIO_AVAL_8821C) {
		u32 freepage;

		freepage = rtw_read32(adapter, SDIO_REG_FREE_TXPG);
		_rtw_up_sema(&adapter->xmitpriv.xmit_sema);
	}
#endif
	if (phal_Data->sdio_hisr & BIT_SDIO_CPWM1_8821C) {
		struct reportpwrstate_parm report;

#ifdef CONFIG_LPS_RPWM_TIMER
		_cancel_timer_ex(&pwrctl->pwr_rpwm_timer);
#endif /* CONFIG_LPS_RPWM_TIMER */

		report.state = rtw_read8(adapter, REG_SDIO_HCPWM1_V2_8821C);

#ifdef CONFIG_LPS_LCLK
		_set_workitem(&(pwrctl->cpwm_event));
#endif
	}
#ifdef CONFIG_WOWLAN
	if (phal_Data->sdio_hisr & BIT_SDIO_CPWM2_8821C) {
		u32 value;

		value = rtw_read32(adapter, REG_SDIO_HISR_8821C);

		RTW_PRINT("Reset SDIO HISR(0x%08x) original:0x%08x\n",
			  REG_SDIO_HISR_8821C, value);
		value |= BIT19;
		rtw_write32(adapter, REG_SDIO_HISR_8821C, value);

		value = rtw_read8(adapter, REG_SDIO_HIMR_8821C + 2);
		RTW_PRINT("Reset SDIO HIMR CPWM2(0x%08x) original:0x%02x\n",
			  SDIO_LOCAL_BASE + SDIO_REG_HIMR + 2, value);
	}
#endif

#ifdef CONFIG_ERROR_STATE_MONITOR
	if (phal_Data->sdio_hisr & BIT_SDIO_TXERR_8821C) {
		u32 status = 0;

		status = rtw_read32(adapter, REG_TXDMA_STATUS_8821C);
		rtw_write32(adapter, REG_TXDMA_STATUS_8821C, status);

		RTW_ERR("[DMA ERROR] TXDMA_STATUS (0x%08x)\n", status);
	}

	if (phal_Data->sdio_hisr & BIT_SDIO_RXERR_8821C) {
		u32 status = 0;

		status = rtw_read32(adapter, REG_RXDMA_STATUS_8821C);
		rtw_write32(adapter, REG_RXDMA_STATUS_8821C, status);
		RTW_ERR("[DMA ERROR] RXDMA_STATUS (0x%08x)\n", status);

	}
#endif
#ifdef CONFIG_MONITOR_OVERFLOW
	if (phal_Data->sdio_hisr & BIT_SDIO_TXFOVW_8821C)
		RTW_ERR("%s TXFOVW\n", __func__);

	if (phal_Data->sdio_hisr & BIT_SDIO_RXFOVW_8821C)
		RTW_ERR("%s RXFOVW\n", __func__);
#endif


#ifdef CONFIG_INTERRUPT_BASED_TXBCN
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
	if (phal_Data->sdio_hisr & BIT_SDIO_BCNERLY_INT_8821C) {
		/*RTW_INFO("%s: SDIO_BCNERLY_INT\n", __func__);*/
		rtw_mi_set_tx_beacon_cmd(adapter);
	}
	#endif

	#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	if (phal_Data->sdio_hisr & (BIT_SDIO_TXBCNOK_8821C | BIT_SDIO_TXBCNERR_8821C)) {
		if (phal_Data->sdio_hisr & BIT_SDIO_TXBCNOK_8821C)
			RTW_INFO("%s: SDIO_HISR_TXBCNOK\n", __func__);


		if (phal_Data->sdio_hisr & BIT_SDIO_TXBCNERR_8821C)
			RTW_INFO("%s: SDIO_HISR_TXBCNERR\n", __func__);

		rtw_mi_set_tx_beacon_cmd(adapter);
	}
	#endif
#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

#ifdef CONFIG_C2H_EVT_INT
	if (phal_Data->sdio_hisr & BIT_SDIO_C2HCMD_INT_8821C) {

	}
#endif
	if (phal_Data->sdio_hisr & BIT_RX_REQUEST_8821C) {
		struct recv_buf *precvbuf = NULL;
		int alloc_fail_time = 0;
		u8 data[4] = {0};
		u32 ret;

		phal_Data->sdio_hisr ^= BIT_RX_REQUEST_8821C;
		do {
			if (phal_Data->SdioRxFIFOSize == 0) {
				rtw_read_mem(adapter, REG_SDIO_RX_REQ_LEN_8821C, 4, data);
				phal_Data->SdioRxFIFOSize = le16_to_cpu(*(u16 *)data);
			}

			if (phal_Data->SdioRxFIFOSize) {
				ret = sd_recv_rxfifo(adapter, phal_Data->SdioRxFIFOSize, &precvbuf);
				if (precvbuf) {
					rtl8821cs_rxhandler(adapter, precvbuf);
				} else {
					if (RTW_SDIO_READ_PORT_FAIL == ret) {
						RTW_ERR("%s: sd_recv_rxfifo - SDIO read port failed\n", __func__);
						rtw_warn_on(1);
						break;
					}

					alloc_fail_time++;
					/*RTW_ERR("%s: recv fail!(time=%d)\n", __func__, alloc_fail_time);*/
					if (alloc_fail_time >= 10) {
						if (sd_recv_and_drop(adapter, phal_Data->SdioRxFIFOSize) == RTW_SDIO_READ_PORT_FAIL) {
							RTW_ERR("%s: sd_recv_and_drop - SDIO read port failed\n", __func__);
							rtw_warn_on(1);
						break;
				}
						#ifdef CONFIG_RECV_THREAD_MODE
						alloc_fail_time = 0;
						#else
						break;
						#endif
					} else
						rtw_msleep_os(1);
				}
				phal_Data->SdioRxFIFOSize = 0;
			} else
				break;

#ifdef CONFIG_SDIO_DISABLE_RXFIFO_POLLING_LOOP
		} while (0);
#else
		} while (1);
#endif

		if (alloc_fail_time >= 10)
			RTW_INFO("%s: exit because recv failed more than 10 times!\n", __FUNCTION__);
	}
}

void sd_int_hdl(PADAPTER adapter)
{
	u8 data[8] = {0};
	u32 v32 = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);
	u8 pwr;
	
	if (RTW_CANNOT_RUN(adapter))
		return;

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &pwr);
	if (pwr != _TRUE) {
		RTW_WARN("%s: unexpected interrupt!\n", __FUNCTION__);
		return;
	}


	/*pHalData->sdio_hisr = rtl8821cs_get_interrupt(adapter);*/
	rtw_read_mem(adapter, REG_SDIO_HISR_8821C, 8, data);

	pHalData->sdio_hisr = le32_to_cpu(*(u32 *)data);
	pHalData->SdioRxFIFOSize = le16_to_cpu(*(u16 *)&data[4]);


	if (pHalData->sdio_hisr & pHalData->sdio_himr) {
		pHalData->sdio_hisr &= pHalData->sdio_himr;

		/* clear HISR */
		v32 = cpu_to_le32(pHalData->sdio_hisr & MASK_SDIO_HISR_CLEAR);
		if (v32)
			rtl8821cs_clear_interrupt(adapter, v32);

		sd_int_dpc(adapter);
	}
#ifdef CONFIG_PLATFORM_INT_MONITOR
	else {
		RTW_INFO("%s: HISR(0x%08x) and HIMR(0x%08x) no match!\n",
			 __FUNCTION__, pHalData->sdio_hisr, pHalData->sdio_himr);
	}
#endif
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
u8 rtw_hal_enable_cpwm2(_adapter *adapter)
{
#ifdef CONFIG_GPIO_WAKEUP
	return _SUCCESS;
#else
	u32 himr = 0;

	RTW_PRINT("%s\n", __func__);
	himr = rtl8821cs_get_himr(adapter);
	RTW_INFO("read SDIO_REG_HIMR: 0x%08x\n", himr);

	himr = BIT_SDIO_CPWM2_MSK_8821C;
	rtl8821cs_update_himr(adapter, himr);
	RTW_INFO("update SDIO_REG_HIMR: 0x%08x\n", himr);

	himr = rtl8821cs_get_himr(adapter);
	RTW_INFO("read again SDIO_REG_HIMR: 0x%08x\n", himr);
	return _SUCCESS;
#endif
}

u8 RecvOnePkt(PADAPTER adapter)
{
	struct recv_buf *precvbuf;
	struct dvobj_priv *psddev;
	PSDIO_DATA psdio_data;
	struct sdio_func *func;
	u8 res = _TRUE;
	u8 data[4] = {0};
	u16 len = 0;
	u32 ret;
	if (adapter == NULL) {
		RTW_ERR("%s: adapter is NULL!\n", __func__);
		return _FALSE;
	}
	psddev = adapter->dvobj;
	psdio_data = &psddev->intf_data;
	func = psdio_data->func;

	rtw_read_mem(adapter, REG_SDIO_RX_REQ_LEN_8821C, 4, data);
	len = le16_to_cpu(*(u16 *)data);

	RTW_INFO("+%s: size=%d+\n", __func__, len);

	if (len) {
		sdio_claim_host(func);
		ret = sd_recv_rxfifo(adapter, len, &precvbuf);
		if (precvbuf)
			rtl8821cs_rxhandler(adapter, precvbuf);
		else
			res = _FALSE;
		sdio_release_host(func);
	}
	RTW_INFO("-%s-\n", __func__);
	return res;
}
#endif /* CONFIG_WOWLAN */
