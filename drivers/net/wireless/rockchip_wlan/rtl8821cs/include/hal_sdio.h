/* SPDX-License-Identifier: GPL-2.0 */
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
#ifndef __HAL_SDIO_H_
#define __HAL_SDIO_H_

#define ffaddr2deviceId(pdvobj, addr)	(pdvobj->Queue2Pipe[addr])

#ifndef RTW_HALMAC
extern const char *_sdio_tx_queue_str[];
#define sdio_tx_queue_str(_page_idx) (_page_idx >= SDIO_MAX_TX_QUEUE ? "UNKNOWN" : _sdio_tx_queue_str[_page_idx])
#endif

u8 rtw_hal_sdio_max_txoqt_free_space(_adapter *padapter);
u8 rtw_hal_sdio_query_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_sdio_update_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_set_sdio_tx_max_length(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ, u8 div_num);
u32 rtw_hal_get_sdio_tx_max_length(PADAPTER padapter, u8 queue_idx);
bool sdio_power_on_check(PADAPTER padapter);

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
#if defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV) ||defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8192F) || defined(CONFIG_RTL8723D)
void rtw_hal_sdio_avail_page_threshold_init(_adapter *adapter);
void rtw_hal_sdio_avail_page_threshold_en(_adapter *adapter, u8 qidx, u8 pg_num);
#endif
#endif /* CONFIG_SDIO_TX_ENABLE_AVAL_INT */

#ifdef CONFIG_FW_C2H_REG
void sd_c2h_hisr_hdl(_adapter *adapter);
#endif

#if defined(CONFIG_RTL8188F) || defined (CONFIG_RTL8188GTV) || defined (CONFIG_RTL8192F) || defined(CONFIG_RTL8723D)
#define SDIO_LOCAL_CMD_ADDR(addr) ((SDIO_LOCAL_DEVICE_ID << 13) | ((addr) & SDIO_LOCAL_MSK))
#endif

#ifdef CONFIG_SDIO_CHK_HCI_RESUME
bool sdio_chk_hci_resume(struct intf_hdl *pintfhdl);
void sdio_chk_hci_suspend(struct intf_hdl *pintfhdl);
#else
#define sdio_chk_hci_resume(pintfhdl) _FALSE
#define sdio_chk_hci_suspend(pintfhdl) do {} while (0)
#endif /* CONFIG_SDIO_CHK_HCI_RESUME */

#ifdef CONFIG_SDIO_INDIRECT_ACCESS
/* program indirect access register in sdio local to read/write page0 registers */
s32 sdio_iread(PADAPTER padapter, u32 addr, u8 size, u8 *v);
s32 sdio_iwrite(PADAPTER padapter, u32 addr, u8 size, u8 *v);
u8 sdio_iread8(struct intf_hdl *pintfhdl, u32 addr);
u16 sdio_iread16(struct intf_hdl *pintfhdl, u32 addr);
u32 sdio_iread32(struct intf_hdl *pintfhdl, u32 addr);
s32 sdio_iwrite8(struct intf_hdl *pintfhdl, u32 addr, u8 val);
s32 sdio_iwrite16(struct intf_hdl *pintfhdl, u32 addr, u16 val);
s32 sdio_iwrite32(struct intf_hdl *pintfhdl, u32 addr, u32 val);
#endif /* CONFIG_SDIO_INDIRECT_ACCESS */
u32 cmd53_4byte_alignment(struct intf_hdl *pintfhdl, u32 addr);

#ifndef CONFIG_SDIO_TX_TASKLET
#ifdef SDIO_FREE_XMIT_BUF_SEMA
void _rtw_sdio_free_xmitbuf_sema_up(struct xmit_priv *xmit);
void _rtw_sdio_free_xmitbuf_sema_down(struct xmit_priv *xmit);
#ifdef DBG_SDIO_FREE_XMIT_BUF_SEMA
void dbg_rtw_sdio_free_xmitbuf_sema_up(struct xmit_priv *xmit, const char *caller);
void dbg_rtw_sdio_free_xmitbuf_sema_down(struct xmit_priv *xmit, const char *caller);
#define rtw_sdio_free_xmitbuf_sema_up(_xmit) dbg_rtw_sdio_free_xmitbuf_sema_up(_xmit, __func__)
#define rtw_sdio_free_xmitbuf_sema_down(_xmit) dbg_rtw_sdio_free_xmitbuf_sema_down(_xmit, __func__)
#else
#define rtw_sdio_free_xmitbuf_sema_up(_xmit) _rtw_sdio_free_xmitbuf_sema_up(_xmit)
#define rtw_sdio_free_xmitbuf_sema_down(_xmit) _rtw_sdio_free_xmitbuf_sema_down(_xmit)
#endif /* DBG_SDIO_FREE_XMIT_BUF_SEMA */
#endif /* SDIO_FREE_XMIT_BUF_SEMA */
#endif /* !CONFIG_SDIO_TX_TASKLET */

s32 sdio_initrecvbuf(struct recv_buf *recvbuf, _adapter *adapter);
void sdio_freerecvbuf(struct recv_buf *recvbuf);

#ifdef CONFIG_SDIO_RECVBUF_PWAIT
void dump_recvbuf_pwait_conf(void *sel, struct recv_priv *recvpriv);
#ifdef CONFIG_SDIO_RECVBUF_PWAIT_RUNTIME_ADJUST
int recvbuf_pwait_config_req(struct recv_priv *recvpriv, enum rtw_pwait_type type, s32 time, s32 cnt_lmt);
int recvbuf_pwait_config_hdl(struct recv_priv *recvpriv, struct recv_buf *rbuf);
#endif /* CONFIG_SDIO_RECVBUF_PWAIT_RUNTIME_ADJUST */
#endif /* CONFIG_SDIO_RECVBUF_PWAIT */

#endif /* __HAL_SDIO_H_ */
