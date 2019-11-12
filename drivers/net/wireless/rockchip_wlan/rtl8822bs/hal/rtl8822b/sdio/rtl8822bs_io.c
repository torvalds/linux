/******************************************************************************
 *
 * Copyright(c) 2015 - 2018 Realtek Corporation.
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
#define _RTL8822BS_IO_C_

#include <drv_types.h>		/* PADAPTER and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include <rtw_sdio.h>		/* rtw_sdio_write_cmd53() */
#include <sdio_ops_linux.h>	/* SDIO_ERR_VAL8 and etc. */
#include "rtl8822bs.h"		/* rtl8822bs_get_interrupt(), rtl8822bs_clear_interrupt() and etc. */
#include "../../hal_halmac.h"	/* rtw_halmac_sdio_get_rx_addr() */


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
 *	d		a pointer of dvobj_priv
 *	addr		not use
 *	cnt		size to write
 *	mem		buffer to write
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
u32 rtl8822bs_read_port(struct dvobj_priv *d, u32 cnt, u8 *mem)
{
	struct _ADAPTER *adapter;
	struct hal_com_data *hal;
	u32 rxaddr;
	void *buf;
	size_t buflen;
	u32 ret;


	adapter = dvobj_get_primary_adapter(d);
	hal = GET_HAL_DATA(adapter);

	rxaddr = rtw_halmac_sdio_get_rx_addr(d, &hal->SdioRxFIFOCnt);
	buf = mem;

	/* align size to guarantee I/O would be done in one command */
	buflen = rtw_sdio_cmd53_align_size(d, cnt);
	if (buflen != cnt) {
		buf = rtw_zmalloc(buflen);
		if (!buf)
			return _FAIL;
	}

	ret = rtw_sdio_read_cmd53(d, rxaddr, buf, buflen);

	if (buflen != cnt) {
		_rtw_memcpy(mem, buf, cnt);
		rtw_mfree(buf, buflen);
	}

	return ret;
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
 *	mem		struct recv_buf*
 *
 * Return:
 *	_SUCCESS(1)	Success
 *	_FAIL(0)	Fail
 */
static u32 sdio_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *mem)
{
	struct recv_buf *recvbuf;


	recvbuf = (struct recv_buf *)mem;
	return rtl8822bs_read_port(pintfhdl->pintf_dev, cnt, recvbuf->pbuf);
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
u32 rtl8822bs_write_port(struct dvobj_priv *d, u32 cnt, u8 *mem)
{
	u32 txaddr, txsize;
	u32 ret = _FAIL;


	txaddr = rtw_halmac_sdio_get_tx_addr(d, mem, cnt);
	if (!txaddr)
		goto exit;
	/*
	 * Align size to SDIO IC excpeted,
	 * and this would be done by calling halmac function later.
	 */
	cnt = _RND4(cnt);

	/* align size to guarantee I/O would be done in one command */
	txsize = rtw_sdio_cmd53_align_size(d, cnt);

	ret = rtw_sdio_write_cmd53(d, txaddr, mem, txsize);

exit:

	return ret;
}

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

	ret = rtl8822bs_write_port(d, cnt, xmitbuf->pdata);

	rtw_sctx_done_err(&xmitbuf->sctx,
		(_FAIL == ret) ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);

	return ret;
}

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

static struct recv_buf *sd_recv_rxfifo(PADAPTER adapter, u32 size)
{
	struct dvobj_priv *d;
	struct recv_priv *recvpriv;
	struct recv_buf	*recvbuf;
	u32 readsz, bufsz;
	u8 *rbuf;
	_pkt *pkt;
	s32 ret;


	d = adapter_to_dvobj(adapter);

	/*
	 * Patch for some SDIO Host 4 bytes issue
	 * ex. RK3188
	 */
	readsz = RND4(size);

	/* round to block size */
	bufsz = rtw_sdio_cmd53_align_size(d, readsz);

	/* 1. alloc recvbuf */
	recvpriv = &adapter->recvpriv;
	recvbuf = rtw_dequeue_recvbuf(&recvpriv->free_recv_buf_queue);
	if (recvbuf == NULL) {
#ifndef CONFIG_RECV_THREAD_MODE
		RTW_WARN("%s:alloc recvbuf FAIL!\n", __FUNCTION__);
#endif /* !CONFIG_RECV_THREAD_MODE */
		return NULL;
	}

	/* 2. alloc skb */
	pkt = rtl8822bs_alloc_recvbuf_skb(recvbuf, bufsz);
	if (!pkt) {
		RTW_ERR("%s: alloc_skb fail! alloc=%d read=%d\n", __FUNCTION__, bufsz, size);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
		return NULL;
	}

	/* 3. read data from rxfifo */
	rbuf = skb_put(pkt, size);
	ret = rtl8822bs_read_port(d, bufsz, rbuf);
	if (_FAIL == ret) {
		RTW_ERR("%s: read port FAIL!\n", __FUNCTION__);
		rtl8822bs_free_recvbuf_skb(recvbuf);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
		return NULL;
	}

	/* 4. init recvbuf */
	recvbuf->len = pkt->len;
	recvbuf->phead = pkt->head;
	recvbuf->pdata = pkt->data;
	recvbuf->ptail = skb_tail_pointer(pkt);
	recvbuf->pend = skb_end_pointer(pkt);

	return recvbuf;
}
#ifdef CONFIG_RECV_THREAD_MODE
static u32 sdio_recv_and_drop(PADAPTER adapter, u32 size)
{
	struct dvobj_priv *d;
	u32 readsz, bufsz;
	u8 *rbuf;
	s32 ret = _SUCCESS;


	d = adapter_to_dvobj(adapter);

	/*
	 * Patch for some SDIO Host 4 bytes issue
	 * ex. RK3188
	 */
	readsz = RND4(size);

	/* round to block size */
	bufsz = rtw_sdio_cmd53_align_size(d, readsz);

	rbuf = rtw_zmalloc(bufsz);
	if (NULL == rbuf) {
		ret = _FAIL;
		goto _exit;
	}

	ret = rtl8822bs_read_port(d, bufsz, rbuf);
	if (_FAIL == ret)
		RTW_ERR("%s: read port FAIL!\n", __FUNCTION__);

	if (NULL != rbuf)
		rtw_mfree(rbuf, bufsz);

_exit:
	return ret;
}
#endif
void sd_int_dpc(PADAPTER adapter)
{
	PHAL_DATA_TYPE phal;
	struct dvobj_priv *dvobj;
	struct pwrctrl_priv *pwrctl;


	phal = GET_HAL_DATA(adapter);
	dvobj = adapter_to_dvobj(adapter);
	pwrctl = dvobj_to_pwrctl(dvobj);

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
	if (phal->sdio_hisr & BIT_SDIO_AVAL_8822B)
		_rtw_up_sema(&adapter->xmitpriv.xmit_sema);

#endif /* CONFIG_SDIO_TX_ENABLE_AVAL_INT */

	if (phal->sdio_hisr & BIT_SDIO_CPWM1_8822B) {
		struct reportpwrstate_parm report;

#ifdef CONFIG_LPS_RPWM_TIMER
		_cancel_timer_ex(&pwrctl->pwr_rpwm_timer);
#endif /* CONFIG_LPS_RPWM_TIMER */

		report.state = rtw_read8(adapter, REG_SDIO_HCPWM1_V2_8822B);

#ifdef CONFIG_LPS_LCLK
		_set_workitem(&(pwrctl->cpwm_event));
#endif /* CONFIG_LPS_LCLK */
	}

	if (phal->sdio_hisr & BIT_SDIO_TXERR_8822B) {
		u32 status;
		u32 addr;

		addr = REG_TXDMA_STATUS_8822B;
		status = rtw_read32(adapter, addr);
		rtw_write32(adapter, addr, status);

		RTW_INFO("%s: SDIO_HISR_TXERR (0x%08x)\n", __FUNCTION__, status);
	}

	if (phal->sdio_hisr & BIT_SDIO_TXBCNOK_8822B)
		RTW_INFO("%s: SDIO_HISR_TXBCNOK\n", __FUNCTION__);

	if (phal->sdio_hisr & BIT_SDIO_TXBCNERR_8822B)
		RTW_INFO("%s: SDIO_HISR_TXBCNERR\n", __FUNCTION__);

	if (phal->sdio_hisr & BIT_SDIO_RXFOVW_8822B)
		RTW_INFO("%s: Rx Overflow\n", __FUNCTION__);

	if (phal->sdio_hisr & BIT_SDIO_RXERR_8822B)
		RTW_INFO("%s: Rx Error\n", __FUNCTION__);

	if (phal->sdio_hisr & BIT_RX_REQUEST_8822B) {
		struct recv_buf *precvbuf;
		int rx_fail_time = 0;
		u16 rx_len;


		/* No need to write 1 clear for RX_REQUEST */
		phal->sdio_hisr ^= BIT_RX_REQUEST_8822B;

		rx_len = phal->SdioRxFIFOSize;
		do {
			if (!rx_len)
				break;

			precvbuf = sd_recv_rxfifo(adapter, rx_len);
			if (precvbuf) {
				rtl8822bs_rxhandler(adapter, precvbuf);
			} else {
				rx_fail_time++;
#ifdef CONFIG_RECV_THREAD_MODE
				if (rx_fail_time >= 10) {
					if (_FAIL == sdio_recv_and_drop(adapter, rx_len))
						break;

					rx_fail_time = 0;
				} else {
					rtw_msleep_os(1);
					continue;
				}
#else /* !CONFIG_RECV_THREAD_MODE */
				RTW_WARN("%s: recv fail!(time=%d)\n", __FUNCTION__, rx_fail_time);
				if (rx_fail_time >= 10)
					break;
#endif /* !CONFIG_RECV_THREAD_MODE */
			}

			rx_len = 0;
			rtl8822bs_get_interrupt(adapter, NULL, &rx_len);
		} while (1);

		if (rx_fail_time == 10)
			RTW_ERR("%s: exit because recv failed more than 10 times!\n", __FUNCTION__);
	}
}

void sd_int_hdl(PADAPTER adapter)
{
	PHAL_DATA_TYPE phal;
	u8 pwr;


	if (RTW_CANNOT_RUN(adapter))
		return;

	phal = GET_HAL_DATA(adapter);

	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &pwr);
	if (pwr != _TRUE) {
		RTW_WARN("%s: unexpected interrupt!\n", __FUNCTION__);
		return;
	}

	rtl8822bs_get_interrupt(adapter, &phal->sdio_hisr, &phal->SdioRxFIFOSize);
	if (phal->sdio_hisr & phal->sdio_himr) {
		phal->sdio_hisr &= phal->sdio_himr;
		sd_int_dpc(adapter);
		rtl8822bs_clear_interrupt(adapter, phal->sdio_hisr);
	} else {
		RTW_INFO("%s: HISR(0x%08x) and HIMR(0x%08x) no match!\n",
			 __FUNCTION__, phal->sdio_hisr, phal->sdio_himr);
	}
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
u8 rtw_hal_enable_cpwm2(_adapter *adapter)
{
	rtl8822bs_disable_interrupt_but_cpwm2(adapter);
	return _SUCCESS;
}

u8 RecvOnePkt(PADAPTER adapter)
{
	struct recv_buf *precvbuf;
	struct dvobj_priv *psddev;
	PSDIO_DATA psdio_data;
	PHAL_DATA_TYPE phal;
	struct sdio_func *func;
	u8 res = _TRUE;
	u32 len = 0;

	if (adapter == NULL) {
		RTW_ERR("%s: adapter is NULL!\n", __func__);
		return _FALSE;
	}

	psddev = adapter->dvobj;
	psdio_data = &psddev->intf_data;
	func = psdio_data->func;
	phal = GET_HAL_DATA(adapter);

	rtl8822bs_get_interrupt(adapter, &phal->sdio_hisr,
				&phal->SdioRxFIFOSize);

	len = phal->SdioRxFIFOSize;

	RTW_DBG("+%s: hisr: %08x size=%d+\n",
		 __func__, phal->sdio_hisr, phal->SdioRxFIFOSize);

	if (len) {
		sdio_claim_host(func);
		precvbuf = sd_recv_rxfifo(adapter, len);
		if (precvbuf)
			rtl8822bs_rxhandler(adapter, precvbuf);
		else
			res = _FALSE;
		sdio_release_host(func);
	}
	return res;
}
#endif /* CONFIG_WOWLAN */

