/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/hif/sdio/include/hif.h#1 $
*/

/*! \file   "hif.h"
    \brief  Functions for the driver to register bus and setup the IRQ

    Functions for the driver to register bus and setup the IRQ
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: hif.h $
 *
 * 11 01 2010 yarco.yang
 * [WCXRP00000149] [MT6620 WI-Fi][Driver]Fine tune performance on MT6516 platform
 * Add GPIO debug function
 *
 * 10 19 2010 jeffrey.chang
 * [WCXRP00000120] [MT6620 Wi-Fi][Driver] Refine linux kernel module to the license of MTK propietary and enable MTK HIF by default
 * Refine linux kernel module to the license of MTK and enable MTK HIF
 *
 * 08 18 2010 jeffrey.chang
 * NULL
 * support multi-function sdio
 *
 * 08 17 2010 cp.wu
 * NULL
 * add ENE SDIO host workaround for x86 linux platform.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port 
 * initial import for Linux port
**  \main\maintrunk.MT5921\4 2009-10-20 17:38:28 GMT mtk01090
**  Refine driver unloading and clean up procedure. Block requests, stop main thread and clean up queued requests, and then stop hw.
**  \main\maintrunk.MT5921\3 2009-09-28 20:19:20 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\2 2009-08-18 22:57:05 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\2 2008-09-22 23:18:17 GMT mtk01461
**  Update driver for code review
** Revision 1.1  2007/07/05 07:25:33  MTK01461
** Add Linux initial code, modify doc, add 11BB, RF init code
**
** Revision 1.3  2007/06/27 02:18:51  MTK01461
** Update SCAN_FSM, Initial(Can Load Module), Proc(Can do Reg R/W), TX API
**
*/

#ifndef _HIF_H
#define _HIF_H


#if MTK_WCN_HIF_SDIO
#include "hif_sdio.h"
#endif

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define SDIO_X86_WORKAROUND_WRITE_MCR   0x00C4

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* host interface's private data structure, which is attached to os glue
** layer info structure.
 */
typedef struct _GL_HIF_INFO_T {
#if MTK_WCN_HIF_SDIO 
    MTK_WCN_HIF_SDIO_CLTCTX cltCtx;
    const MTK_WCN_HIF_SDIO_FUNCINFO *prFuncInfo;
#else
    struct sdio_func	    *func;
#endif
    BOOLEAN                 fgIntReadClear;
    BOOLEAN                 fgMbxReadClear;
} GL_HIF_INFO_T, *P_GL_HIF_INFO_T;


#if CFG_DBG_GPIO_PINS

extern void
mtk_wcn_stp_debug_gpio_assert(UINT_32 dwIndex, UINT_32 dwMethod);


/* Platform DEPENDENT configurations: MT6516 GPIO pin name */
#define GPIO_6516(x) (GPIO##x)
#define GPIO_PLATFORM(x) GPIO_6516(x)

/* Platform independent configurations */
#define DBG_TIE_DIR 0x40000000UL
#define DBG_LOW 0x0UL
#define DBG_HIGH 0x1UL
#define DBG_TIE_LOW (DBG_TIE_DIR | DBG_LOW)
#define DBG_TIE_HIGH (DBG_TIE_DIR | DBG_HIGH)

#define DBG_TOGGLE(x) (0x80000000UL | x)
#define DBG_TOGGLE_NUM(x) (x & 0xFFF)
#define GPIO_INVALID 0xFFFF

typedef enum {
    IDX_ERR = 0,
    IDX_TX_THREAD,
    IDX_TX_REQ,
    IDX_TX_PORT_WRITE,
    IDX_STP_MTX_BT,
    IDX_STP_MTX_FM, /* 5 */
    IDX_STP_MTX_GPS,
    IDX_STP_MTX_WIFI,
    IDX_STP_MTX_WMT,
    IDX_LOOP_CNT,
    IDX_NO_BUF, /*10 */
    IDX_BT_TX,
    IDX_BT_RX,
    IDX_GPS_RX,
    IDX_GPS_2,
    IDX_GPS_3,
    IDX_GPS_TX,
    IDX_STP_MAX
} DBG_PIN;

#define DBG_MTX_OFFSET (IDX_STP_MTX_BT) /* index of mutex0 */
#endif
/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

WLAN_STATUS
glRegisterBus(
    probe_card pfProbe,
    remove_card pfRemove
    );

VOID
glUnregisterBus(
    remove_card pfRemove
    );

VOID
glSetHifInfo (
    P_GLUE_INFO_T prGlueInfo,
    UINT_32 u4Cookie
    );

VOID
glClearHifInfo (
    P_GLUE_INFO_T prGlueInfo
    );

BOOL
glBusInit (
    PVOID pvData
    );

VOID
glBusRelease (
    PVOID pData
    );

INT_32
glBusSetIrq (
    PVOID pvData,
    PVOID pfnIsr,
    PVOID pvCookie
    );

VOID
glBusFreeIrq (
    PVOID pvData,
    PVOID pvCookie
    );

VOID
glSetPowerState (
    IN P_GLUE_INFO_T  prGlueInfo,
    IN UINT_32 ePowerMode
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _HIF_H */
