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
#ifndef __SDIO_OPS_H__
#define __SDIO_OPS_H__


/* Follow mac team suggestion, default I/O fail return value is 0xFF */
#define SDIO_ERR_VAL8	0xFF
#define SDIO_ERR_VAL16	0xFFFF
#define SDIO_ERR_VAL32	0xFFFFFFFF

#ifdef PLATFORM_LINUX
#include <sdio_ops_linux.h>
#endif

#ifdef PLATFORM_WINDOWS

#ifdef PLATFORM_OS_XP
#include <sdio_ops_xp.h>
struct async_context {
	PMDL pmdl;
	PSDBUS_REQUEST_PACKET sdrp;
	unsigned char *r_buf;
	unsigned char *padapter;
};
#endif

#ifdef PLATFORM_OS_CE
#include <sdio_ops_ce.h>
#endif

#endif /* PLATFORM_WINDOWS */


extern void sdio_set_intf_ops(_adapter *padapter, struct _io_ops *pops);
void dump_sdio_card_info(void *sel, struct dvobj_priv *dvobj);

#if 0
extern void sdio_func1cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
extern void sdio_func1cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);
#endif
extern u8 SdioLocalCmd52Read1Byte(PADAPTER padapter, u32 addr);
extern void SdioLocalCmd52Write1Byte(PADAPTER padapter, u32 addr, u8 v);
extern s32 _sdio_local_read(PADAPTER padapter, u32 addr, u32 cnt, u8 *pbuf);
extern s32 sdio_local_read(PADAPTER padapter, u32 addr, u32 cnt, u8 *pbuf);
extern s32 _sdio_local_write(PADAPTER padapter, u32 addr, u32 cnt, u8 *pbuf);
extern s32 sdio_local_write(PADAPTER padapter, u32 addr, u32 cnt, u8 *pbuf);

u32 _sdio_read32(PADAPTER padapter, u32 addr);
s32 _sdio_write32(PADAPTER padapter, u32 addr, u32 val);

extern void sd_int_hdl(PADAPTER padapter);
extern u8 CheckIPSStatus(PADAPTER padapter);

#ifdef CONFIG_RTL8188E
extern void InitInterrupt8188ESdio(PADAPTER padapter);
extern void EnableInterrupt8188ESdio(PADAPTER padapter);
extern void DisableInterrupt8188ESdio(PADAPTER padapter);
extern void UpdateInterruptMask8188ESdio(PADAPTER padapter, u32 AddMSR, u32 RemoveMSR);
extern u8 HalQueryTxBufferStatus8189ESdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8189ESdio(PADAPTER padapter);
extern void ClearInterrupt8188ESdio(PADAPTER padapter);
#endif /* CONFIG_RTL8188E */

#ifdef CONFIG_RTL8821A
extern void InitInterrupt8821AS(PADAPTER padapter);
extern void EnableInterrupt8821AS(PADAPTER padapter);
extern void DisableInterrupt8821AS(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8821AS(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8821ASdio(PADAPTER padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void ClearInterrupt8821AS(PADAPTER padapter);
#endif /* defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) */
#endif /* CONFIG_RTL8821A */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
#if defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822B)
u8 rtw_hal_enable_cpwm2(_adapter *adapter);
#endif
extern u8 RecvOnePkt(PADAPTER padapter);
#endif /* CONFIG_WOWLAN */
#ifdef CONFIG_RTL8723B
extern void InitInterrupt8723BSdio(PADAPTER padapter);
extern void InitSysInterrupt8723BSdio(PADAPTER padapter);
extern void EnableInterrupt8723BSdio(PADAPTER padapter);
extern void DisableInterrupt8723BSdio(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8723BSdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8723BSdio(PADAPTER padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern void DisableInterruptButCpwm28723BSdio(PADAPTER padapter);
extern void ClearInterrupt8723BSdio(PADAPTER padapter);
#endif /* CONFIG_WOWLAN */
#endif


#ifdef CONFIG_RTL8192E
extern void InitInterrupt8192ESdio(PADAPTER padapter);
extern void EnableInterrupt8192ESdio(PADAPTER padapter);
extern void DisableInterrupt8192ESdio(PADAPTER padapter);
extern void UpdateInterruptMask8192ESdio(PADAPTER padapter, u32 AddMSR, u32 RemoveMSR);
extern u8 HalQueryTxBufferStatus8192ESdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8192ESdio(PADAPTER padapter);
extern void ClearInterrupt8192ESdio(PADAPTER padapter);
#endif /* CONFIG_RTL8192E */

#ifdef CONFIG_RTL8703B
extern void InitInterrupt8703BSdio(PADAPTER padapter);
extern void InitSysInterrupt8703BSdio(PADAPTER padapter);
extern void EnableInterrupt8703BSdio(PADAPTER padapter);
extern void DisableInterrupt8703BSdio(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8703BSdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8703BSdio(PADAPTER padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern void DisableInterruptButCpwm28703BSdio(PADAPTER padapter);
extern void ClearInterrupt8703BSdio(PADAPTER padapter);
#endif /* CONFIG_WOWLAN */
#endif

#ifdef CONFIG_RTL8723D
extern void InitInterrupt8723DSdio(PADAPTER padapter);
extern void InitSysInterrupt8723DSdio(PADAPTER padapter);
extern void EnableInterrupt8723DSdio(PADAPTER padapter);
extern void DisableInterrupt8723DSdio(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8723DSdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8723DSdio(PADAPTER padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern void DisableInterruptButCpwm28723dSdio(PADAPTER padapter);
extern void ClearInterrupt8723DSdio(PADAPTER padapter);
#endif /* CONFIG_WOWLAN */
#endif

#ifdef CONFIG_RTL8188F
extern void InitInterrupt8188FSdio(PADAPTER padapter);
extern void InitSysInterrupt8188FSdio(PADAPTER padapter);
extern void EnableInterrupt8188FSdio(PADAPTER padapter);
extern void DisableInterrupt8188FSdio(PADAPTER padapter);
extern u8 HalQueryTxBufferStatus8188FSdio(PADAPTER padapter);
extern u8 HalQueryTxOQTBufferStatus8188FSdio(PADAPTER padapter);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
extern void DisableInterruptButCpwm28188FSdio(PADAPTER padapter);
extern void ClearInterrupt8188FSdio(PADAPTER padapter);
#endif /* defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) */
#endif

#endif /* !__SDIO_OPS_H__ */
