/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/nic/hal.h#1 $
*/

/*! \file   "hal.h"
    \brief  The declaration of hal functions

    N/A
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
** $Log: hal.h $
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 04 01 2011 tsaiyuan.hsu
 * [WCXRP00000615] [MT 6620 Wi-Fi][Driver] Fix klocwork issues
 * fix the klocwork issues, 57500, 57501, 57502 and 57503.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 11 08 2010 cp.wu
 * [WCXRP00000166] [MT6620 Wi-Fi][Driver] use SDIO CMD52 for enabling/disabling interrupt to reduce transaction period
 * change to use CMD52 for enabling/disabling interrupt to reduce SDIO transaction time
 *
 * 09 01 2010 cp.wu
 * NULL
 * move HIF CR initialization from where after sdioSetupCardFeature() to wlanAdapterStart()
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change zero-padding for TX port access to HAL.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access for prGlueInfo->fgIsCardRemoved in non-glue layer
 *
 * 01 27 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. eliminate improper variable in rHifInfo
 *  *  *  * 2. block TX/ordinary OID when RF test mode is engaged
 *  *  *  * 3. wait until firmware finish operation when entering into and leaving from RF test mode
 *  *  *  * 4. correct some HAL implementation
**  \main\maintrunk.MT6620WiFiDriver_Prj\17 2009-12-16 18:02:26 GMT mtk02752
**  include precomp.h
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-12-10 16:43:16 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-11-13 13:54:15 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-11 10:36:01 GMT mtk01084
**  modify HAL functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-09 22:56:28 GMT mtk01084
**  modify HW access routines
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-10-29 19:50:09 GMT mtk01084
**  add new macro HAL_TX_PORT_WR
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-10-23 16:08:10 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-13 21:58:50 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-05-18 14:28:10 GMT mtk01084
**  fix issue in HAL_DRIVER_OWN_BY_SDIO_CMD52()
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-05-11 17:26:33 GMT mtk01084
**  modify the bit definition to check driver own status
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-04-28 10:30:22 GMT mtk01461
**  Fix typo
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-01 10:50:34 GMT mtk01461
**  Redefine HAL_PORT_RD/WR macro for SW pre test
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-24 09:46:49 GMT mtk01084
**  fix LINT error
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-23 16:53:38 GMT mtk01084
**  add HAL_DRIVER_OWN_BY_SDIO_CMD52()
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-18 20:53:13 GMT mtk01426
**  Fixed lint warn
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:20 GMT mtk01426
**  Init for develop
**
*/

#ifndef _HAL_H
#define _HAL_H

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

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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

/* Macros for flag operations for the Adapter structure */
#define HAL_SET_FLAG(_M, _F)             ((_M)->u4HwFlags |= (_F))
#define HAL_CLEAR_FLAG(_M, _F)           ((_M)->u4HwFlags &= ~(_F))
#define HAL_TEST_FLAG(_M, _F)            ((_M)->u4HwFlags & (_F))
#define HAL_TEST_FLAGS(_M, _F)           (((_M)->u4HwFlags & (_F)) == (_F))

#if defined(_HIF_SDIO)
#define HAL_MCR_RD(_prAdapter, _u4Offset, _pu4Value) \
    { \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
                ASSERT(0); \
            } \
            if (kalDevRegRead(_prAdapter->prGlueInfo, _u4Offset, _pu4Value) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("HAL_MCR_RD access fail! 0x%x: 0x%x \n", _u4Offset, *_pu4Value)); \
            } \
        } else { \
            DBGLOG(HAL, WARN, ("ignore HAL_MCR_RD access! 0x%x\n", _u4Offset)); \
        } \
    }

#define HAL_MCR_WR(_prAdapter, _u4Offset, _u4Value) \
    { \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
                ASSERT(0); \
            } \
            if (kalDevRegWrite(_prAdapter->prGlueInfo, _u4Offset, _u4Value) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("HAL_MCR_WR access fail! 0x%x: 0x%x \n", _u4Offset, _u4Value)); \
            } \
        } else { \
            DBGLOG(HAL, WARN, ("ignore HAL_MCR_WR access! 0x%x: 0x%x \n", _u4Offset, _u4Value)); \
        } \
     }

#define HAL_PORT_RD(_prAdapter, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) \
    { \
        /*fgResult = FALSE; */\
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (kalDevPortRead(_prAdapter->prGlueInfo, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("HAL_PORT_RD access fail! 0x%x\n", _u4Port)); \
            } \
            else { \
                /*fgResult = TRUE;*/ } \
        } else { \
            DBGLOG(HAL, WARN, ("ignore HAL_PORT_RD access! 0x%x\n", _u4Port)); \
        } \
    }

#define HAL_PORT_WR(_prAdapter, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) \
    { \
        /*fgResult = FALSE; */\
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (kalDevPortWrite(_prAdapter->prGlueInfo, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("HAL_PORT_WR access fail! 0x%x\n", _u4Port)); \
            } \
            else { \
                /*fgResult = TRUE;*/ } \
        } else { \
            DBGLOG(HAL, WARN, ("ignore HAL_PORT_WR access! 0x%x\n", _u4Port)); \
        } \
    }

#define HAL_BYTE_WR(_prAdapter, _u4Port, _ucBuf) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (kalDevWriteWithSdioCmd52(_prAdapter->prGlueInfo, _u4Port, _ucBuf) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("HAL_BYTE_WR access fail! 0x%x\n", _u4Port)); \
            } \
            else { \
            } \
        } \
        else { \
            DBGLOG(HAL, WARN, ("ignore HAL_BYTE_WR access! 0x%x\n", _u4Port)); \
        } \
    }


#define HAL_DRIVER_OWN_BY_SDIO_CMD52(_prAdapter, _pfgDriverIsOwnReady) \
    { \
        UINT_8 ucBuf = BIT(1); \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        if (HAL_TEST_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR) == FALSE) { \
            if (kalDevReadAfterWriteWithSdioCmd52(_prAdapter->prGlueInfo, MCR_WHLPCR_BYTE1, &ucBuf, 1) == FALSE) {\
                HAL_SET_FLAG(_prAdapter, ADAPTER_FLAG_HW_ERR); \
                fgIsBusAccessFailed = TRUE; \
                DBGLOG(HAL, ERROR, ("kalDevReadAfterWriteWithSdioCmd52 access fail!\n")); \
            } \
            else { \
                *_pfgDriverIsOwnReady = (ucBuf & BIT(0)) ? TRUE : FALSE; \
            } \
        } else { \
            DBGLOG(HAL, WARN, ("ignore HAL_DRIVER_OWN_BY_SDIO_CMD52 access!\n")); \
        } \
    }

#else /* #if defined(_HIF_SDIO) */
#define HAL_MCR_RD(_prAdapter, _u4Offset, _pu4Value) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        kalDevRegRead(_prAdapter->prGlueInfo, _u4Offset, _pu4Value); \
    }

#define HAL_MCR_WR(_prAdapter, _u4Offset, _u4Value) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        kalDevRegWrite(_prAdapter->prGlueInfo, _u4Offset, _u4Value); \
    }

#define HAL_PORT_RD(_prAdapter, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        kalDevPortRead(_prAdapter->prGlueInfo, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize); \
    }

#define HAL_PORT_WR(_prAdapter, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        kalDevPortWrite(_prAdapter->prGlueInfo, _u4Port, _u4Len, _pucBuf, _u4ValidBufSize); \
    }

#define HAL_BYTE_WR(_prAdapter, _u4Port, _ucBuf) \
    { \
        if (_prAdapter->rAcpiState == ACPI_STATE_D3) { \
            ASSERT(0); \
        } \
        kalDevWriteWithSdioCmd52(_prAdapter->prGlueInfo, _u4Port, _ucBuf); \
    }

#endif /* #if defined(_HIF_SDIO) */


#define HAL_READ_RX_PORT(prAdapter, u4PortId, u4Len, pvBuf, _u4ValidBufSize) \
    { \
    ASSERT(u4PortId < 2); \
    HAL_PORT_RD(prAdapter, \
        ((u4PortId == 0) ? MCR_WRDR0 : MCR_WRDR1), \
        u4Len, \
        pvBuf, \
        _u4ValidBufSize/*temp!!*//*4Kbyte*/) \
    }

#define HAL_WRITE_TX_PORT(_prAdapter, _ucTxPortIdx, _u4Len, _pucBuf, _u4ValidBufSize) \
    { \
    ASSERT(_ucTxPortIdx < 2); \
    if((_u4ValidBufSize - _u4Len) >= sizeof(UINT_32)) { \
        /* fill with single dword of zero as TX-aggregation termination */ \
        *(PUINT_32) (&((_pucBuf)[ALIGN_4(_u4Len)])) = 0; \
    } \
    HAL_PORT_WR(_prAdapter, \
                (_ucTxPortIdx == 0) ? MCR_WTDR0 : MCR_WTDR1, \
                _u4Len, \
                _pucBuf, \
                _u4ValidBufSize/*temp!!*//*4KByte*/) \
    }

/* The macro to read the given MCR several times to check if the wait
   condition come true. */
#define HAL_MCR_RD_AND_WAIT(_pAdapter, _offset, _pReadValue, _waitCondition, _waitDelay, _waitCount, _status) \
        { \
            UINT_32 count; \
            (_status) = FALSE; \
            for (count = 0; count < (_waitCount); count++) { \
                HAL_MCR_RD((_pAdapter), (_offset), (_pReadValue)); \
                if ((_waitCondition)) { \
                    (_status) = TRUE; \
                    break; \
                } \
                kalUdelay((_waitDelay)); \
            } \
        }


/* The macro to write 1 to a R/S bit and read it several times to check if the
   command is done */
#define HAL_MCR_WR_AND_WAIT(_pAdapter, _offset, _writeValue, _busyMask, _waitDelay, _waitCount, _status) \
        { \
            UINT_32 u4Temp; \
            UINT_32 u4Count = _waitCount; \
            (_status) = FALSE; \
            HAL_MCR_WR((_pAdapter), (_offset), (_writeValue)); \
            do { \
                kalUdelay((_waitDelay)); \
                HAL_MCR_RD((_pAdapter), (_offset), &u4Temp); \
                if (!(u4Temp & (_busyMask))) { \
                    (_status) = TRUE; \
                    break; \
                } \
                u4Count--; \
            } while (u4Count); \
        }

#define HAL_GET_CHIP_ID_VER(_prAdapter, pu2ChipId, pu2Version) \
    { \
    UINT_32 u4Value; \
    HAL_MCR_RD(_prAdapter, \
        MCR_WCIR, \
        &u4Value); \
    *pu2ChipId = (UINT_16)(u4Value & WCIR_CHIP_ID); \
    *pu2Version = (UINT_16)(u4Value & WCIR_REVISION_ID) >> 16; \
    }

#define HAL_WAIT_WIFI_FUNC_READY(_prAdapter) \
    { \
    UINT_32 u4Value; \
    UINT_32 i; \
    for (i = 0; i < 100; i++) { \
        HAL_MCR_RD(_prAdapter, \
            MCR_WCIR, \
            &u4Value); \
         if (u4Value & WCIR_WLAN_READY) { \
            break; \
         } \
        NdisMSleep(10); \
    } \
    }

#define HAL_INTR_DISABLE(_prAdapter) \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHLPCR, \
        WHLPCR_INT_EN_CLR)

#define HAL_INTR_ENABLE(_prAdapter) \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHLPCR, \
        WHLPCR_INT_EN_SET)

#define HAL_INTR_ENABLE_AND_LP_OWN_SET(_prAdapter) \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHLPCR, \
        (WHLPCR_INT_EN_SET | WHLPCR_FW_OWN_REQ_SET))

#define HAL_LP_OWN_SET(_prAdapter) \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHLPCR, \
        WHLPCR_FW_OWN_REQ_SET)

#define HAL_LP_OWN_CLR_OK(_prAdapter, _pfgResult) \
    { \
        UINT_32 i; \
        UINT_32 u4RegValue; \
        UINT_32 u4LoopCnt = 2048 / 8; \
        *_pfgResult = TRUE; \
        /* Software get LP ownership */ \
        HAL_MCR_WR(_prAdapter, \
            MCR_WHLPCR, \
            WHLPCR_FW_OWN_REQ_CLR) \
        for (i = 0; i < u4LoopCnt; i++) { \
            HAL_MCR_RD(_prAdapter, MCR_WHLPCR, &u4RegValue); \
            if (u4RegValue & WHLPCR_IS_DRIVER_OWN) { \
                break; \
            } \
            else { \
                kalUdelay(8); \
            } \
        } \
        if (i == u4LoopCnt) { \
            *_pfgResult = FALSE; \
            /*ERRORLOG(("LP cannot be own back (%ld)", u4LoopCnt));*/ \
            /* check the time of LP instructions need to perform from Sleep to On */ \
            /*ASSERT(0); */ \
        } \
    }

#define HAL_GET_ABNORMAL_INTERRUPT_REASON_CODE(_prAdapter, pu4AbnormalReason) \
    { \
    HAL_MCR_RD(_prAdapter, \
        MCR_WASR, \
        pu4AbnormalReason); \
    }


#define HAL_DISABLE_RX_ENHANCE_MODE(_prAdapter) \
    { \
    UINT_32 u4Value; \
    HAL_MCR_RD(_prAdapter, \
        MCR_WHCR, \
        &u4Value); \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHCR, \
        u4Value & ~WHCR_RX_ENHANCE_MODE_EN); \
    }

#define HAL_ENABLE_RX_ENHANCE_MODE(_prAdapter) \
    { \
    UINT_32 u4Value; \
    HAL_MCR_RD(_prAdapter, \
        MCR_WHCR, \
        &u4Value); \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHCR, \
        u4Value | WHCR_RX_ENHANCE_MODE_EN); \
    }

#define HAL_CFG_MAX_HIF_RX_LEN_NUM(_prAdapter, _ucNumOfRxLen) \
    { \
    UINT_32 u4Value, ucNum; \
    ucNum = ((_ucNumOfRxLen >= 16) ? 0 : _ucNumOfRxLen); \
    u4Value = 0; \
    HAL_MCR_RD(_prAdapter, \
        MCR_WHCR, \
        &u4Value); \
    u4Value &= ~WHCR_MAX_HIF_RX_LEN_NUM; \
    u4Value |= ((((UINT_32)ucNum) << 4) & WHCR_MAX_HIF_RX_LEN_NUM); \
    HAL_MCR_WR(_prAdapter, \
        MCR_WHCR, \
        u4Value); \
    }

#define HAL_SET_INTR_STATUS_READ_CLEAR(prAdapter) \
    { \
    UINT_32 u4Value; \
    HAL_MCR_RD(prAdapter, \
        MCR_WHCR, \
        &u4Value); \
    HAL_MCR_WR(prAdapter, \
        MCR_WHCR, \
        u4Value & ~WHCR_W_INT_CLR_CTRL); \
    prAdapter->prGlueInfo->rHifInfo.fgIntReadClear = TRUE;\
    }

#define HAL_SET_INTR_STATUS_WRITE_1_CLEAR(prAdapter) \
    { \
    UINT_32 u4Value; \
    HAL_MCR_RD(prAdapter, \
        MCR_WHCR, \
        &u4Value); \
    HAL_MCR_WR(prAdapter, \
        MCR_WHCR, \
        u4Value | WHCR_W_INT_CLR_CTRL); \
    prAdapter->prGlueInfo->rHifInfo.fgIntReadClear = FALSE;\
    }

/* Note: enhance mode structure may also carried inside the buffer,
         if the length of the buffer is long enough */
#define HAL_READ_INTR_STATUS(prAdapter, length, pvBuf) \
    HAL_PORT_RD(prAdapter, \
        MCR_WHISR, \
        length, \
        pvBuf, \
        length)

#define HAL_READ_TX_RELEASED_COUNT(_prAdapter, aucTxReleaseCount) \
    { \
    PUINT_32 pu4Value = (PUINT_32)aucTxReleaseCount; \
    HAL_MCR_RD(_prAdapter, \
        MCR_WTSR0, \
        &pu4Value[0]); \
    HAL_MCR_RD(_prAdapter, \
        MCR_WTSR1, \
        &pu4Value[1]); \
    }

#define HAL_READ_RX_LENGTH(prAdapter, pu2Rx0Len, pu2Rx1Len) \
    { \
    UINT_32 u4Value; \
    u4Value = 0; \
    HAL_MCR_RD(prAdapter, \
        MCR_WRPLR, \
        &u4Value); \
    *pu2Rx0Len = (UINT_16)u4Value; \
    *pu2Rx1Len = (UINT_16)(u4Value >> 16); \
    }

#define HAL_GET_INTR_STATUS_FROM_ENHANCE_MODE_STRUCT(pvBuf, u2Len, pu4Status) \
    { \
    PUINT_32 pu4Buf = (PUINT_32)pvBuf; \
    *pu4Status = pu4Buf[0]; \
    }

#define HAL_GET_TX_STATUS_FROM_ENHANCE_MODE_STRUCT(pvInBuf, pu4BufOut, u4LenBufOut) \
    { \
    PUINT_32 pu4Buf = (PUINT_32)pvInBuf; \
    ASSERT(u4LenBufOut >= 8); \
    pu4BufOut[0] = pu4Buf[1]; \
    pu4BufOut[1] = pu4Buf[2]; \
    }

#define HAL_GET_RX_LENGTH_FROM_ENHANCE_MODE_STRUCT(pvInBuf, pu2Rx0Num, au2Rx0Len, pu2Rx1Num, au2Rx1Len) \
    { \
    PUINT_32 pu4Buf = (PUINT_32)pvInBuf; \
    ASSERT((sizeof(au2Rx0Len) / sizeof(UINT_16)) >= 16); \
    ASSERT((sizeof(au2Rx1Len) / sizeof(UINT_16)) >= 16); \
    *pu2Rx0Num = (UINT_16)pu4Buf[3]; \
    *pu2Rx1Num = (UINT_16)(pu4Buf[3] >> 16); \
    kalMemCopy(au2Rx0Len, &pu4Buf[4], 8); \
    kalMemCopy(au2Rx1Len, &pu4Buf[12], 8); \
    }

#define HAL_GET_MAILBOX_FROM_ENHANCE_MODE_STRUCT(pvInBuf, pu4Mailbox0, pu4Mailbox1) \
    { \
    PUINT_32 pu4Buf = (PUINT_32)pvInBuf; \
    *pu4Mailbox0 = (UINT_16)pu4Buf[21]; \
    *pu4Mailbox1 = (UINT_16)pu4Buf[22]; \
    }

#define HAL_IS_TX_DONE_INTR(u4IntrStatus) \
   ((u4IntrStatus & WHISR_TX_DONE_INT) ? TRUE : FALSE)

#define HAL_IS_RX_DONE_INTR(u4IntrStatus) \
    ((u4IntrStatus & (WHISR_RX0_DONE_INT | WHISR_RX1_DONE_INT)) ? TRUE : FALSE)

#define HAL_IS_ABNORMAL_INTR(u4IntrStatus) \
    ((u4IntrStatus & WHISR_ABNORMAL_INT) ? TRUE : FALSE)

#define HAL_IS_FW_OWNBACK_INTR(u4IntrStatus) \
    ((u4IntrStatus & WHISR_FW_OWN_BACK_INT) ? TRUE : FALSE)

#define HAL_PUT_MAILBOX(prAdapter, u4MboxId, u4Data) \
    { \
    ASSERT(u4MboxId < 2); \
    HAL_MCR_WR(prAdapter, \
        ((u4MboxId == 0) ? MCR_H2DSM0R : MCR_H2DSM1R), \
        u4Data); \
    }

#define HAL_GET_MAILBOX(prAdapter, u4MboxId, pu4Data) \
    { \
    ASSERT(u4MboxId < 2); \
    HAL_MCR_RD(prAdapter, \
        ((u4MboxId == 0) ? MCR_D2HRM0R : MCR_D2HRM1R), \
        pu4Data); \
    }

#define HAL_SET_MAILBOX_READ_CLEAR(prAdapter, fgEnableReadClear) \
    { \
        UINT_32 u4Value; \
        HAL_MCR_RD(prAdapter, MCR_WHCR, &u4Value);\
        HAL_MCR_WR(prAdapter, MCR_WHCR, \
                    (fgEnableReadClear) ? \
                        (u4Value | WHCR_W_MAILBOX_RD_CLR_EN) : \
                        (u4Value & ~WHCR_W_MAILBOX_RD_CLR_EN)); \
        prAdapter->prGlueInfo->rHifInfo.fgMbxReadClear = fgEnableReadClear;\
    }

#define HAL_GET_MAILBOX_READ_CLEAR(prAdapter) (prAdapter->prGlueInfo->rHifInfo.fgMbxReadClear)


/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/



/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _HAL_H */

