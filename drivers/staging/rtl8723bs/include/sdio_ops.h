/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __SDIO_OPS_H__
#define __SDIO_OPS_H__


#include <sdio_ops_linux.h>

extern void sdio_set_intf_ops(struct adapter *padapter, struct _io_ops *pops);

/* extern void sdio_func1cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem); */
/* extern void sdio_func1cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem); */
extern u8 SdioLocalCmd52Read1Byte(struct adapter *padapter, u32 addr);
extern void SdioLocalCmd52Write1Byte(struct adapter *padapter, u32 addr, u8 v);
extern s32 sdio_local_read(struct adapter *padapter, u32 addr, u32 cnt, u8 *pbuf);
extern s32 sdio_local_write(struct adapter *padapter, u32 addr, u32 cnt, u8 *pbuf);

u32 _sdio_read32(struct adapter *padapter, u32 addr);
s32 _sdio_write32(struct adapter *padapter, u32 addr, u32 val);

extern void sd_int_hdl(struct adapter *padapter);
extern u8 CheckIPSStatus(struct adapter *padapter);

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern u8 RecvOnePkt(struct adapter *padapter, u32 size);
#endif /*  CONFIG_WOWLAN */
extern void InitInterrupt8723BSdio(struct adapter *padapter);
extern void InitSysInterrupt8723BSdio(struct adapter *padapter);
extern void EnableInterrupt8723BSdio(struct adapter *padapter);
extern void DisableInterrupt8723BSdio(struct adapter *padapter);
extern u8 HalQueryTxBufferStatus8723BSdio(struct adapter *padapter);
extern u8 HalQueryTxOQTBufferStatus8723BSdio(struct adapter *padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern void ClearInterrupt8723BSdio(struct adapter *padapter);
#endif /* CONFIG_WOWLAN */

#endif /*  !__SDIO_OPS_H__ */
