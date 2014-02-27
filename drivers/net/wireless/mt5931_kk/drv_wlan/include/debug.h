/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/debug.h#1 $
*/

/*! \file   debug.h
    \brief  Definition of SW debugging level.

    In this file, it describes the definition of various SW debugging levels and
    assert functions.
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
** $Log: debug.h $
 *
 * 12 16 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * fixed the Windows DDK free build compiling error.
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Adjust code for DBG and CONFIG_XLOG.
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 10 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Using the new XLOG define for dum Memory.
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Add dumpMemory8 at XLOG support.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adding the code for XLOG.
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 07 2011 wh.su
 * [WCXRP00000326] [MT6620][Wi-Fi][Driver] check in the binary format gl_sec.o.new instead of use change type!!!
 * .
 *
 * 09 23 2010 cp.wu
 * NULL
 * add BOW index for debugging message and passing compilation
 *
 * 07 20 2010 wh.su
 *
 * adding the wapi code.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Support CFG_MQM_MIGRATION flag
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Add one more debug moduel for P2P.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add management dispatching function table.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add bss.c.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add debug module index for cnm and ais.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 17 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add CFG_STARTUP_DEBUG for debugging starting up issue.
 *
 * 04 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) surpress compiler warning
 * 2) when acqruing LP-own, keep writing WHLPCR whenever OWN is not acquired yet
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-10-29 19:47:50 GMT mtk01084
**  add emu catagory
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-04-17 18:12:04 GMT mtk01426
**  Don't use dynamic memory allocate for debug message
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:11:29 GMT mtk01426
**  Init for develop
**
*/

#ifndef _DEBUG_H
#define _DEBUG_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#ifndef BUILD_QA_DBG
#define BUILD_QA_DBG 0
#endif

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_typedef.h"
#if CFG_SUPPORT_XLOG
#include "linux/xlog.h"
#endif

extern UINT_8   aucDebugModule[];
extern UINT_32  u4DebugModule;

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* Define debug category (class):
 * (1) ERROR (2) WARN (3) STATE (4) EVENT (5) TRACE (6) INFO (7) LOUD (8) TEMP
 */
#define DBG_CLASS_ERROR         BIT(0)
#define DBG_CLASS_WARN          BIT(1)
#define DBG_CLASS_STATE         BIT(2)
#define DBG_CLASS_EVENT         BIT(3)
#define DBG_CLASS_TRACE         BIT(4)
#define DBG_CLASS_INFO          BIT(5)
#define DBG_CLASS_LOUD          BIT(6)
#define DBG_CLASS_TEMP          BIT(7)
#define DBG_CLASS_MASK          BITS(0,7)


#if defined(LINUX)
#define DBG_PRINTF_64BIT_DEC    "lld"

#else  //Windows
#define DBG_PRINTF_64BIT_DEC    "I64d"

#endif
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Define debug module index */
typedef enum _ENUM_DBG_MODULE_T {
    DBG_INIT_IDX = 0,       /* For driver initial */
    DBG_HAL_IDX,            /* For HAL(HW) Layer */
    DBG_INTR_IDX,           /* For Interrupt */
    DBG_REQ_IDX,
    DBG_TX_IDX,
    DBG_RX_IDX,
    DBG_RFTEST_IDX,         /* For RF test mode*/
    DBG_EMU_IDX,            /* Developer specific */

    DBG_SW1_IDX,            /* Developer specific */
    DBG_SW2_IDX,            /* Developer specific */
    DBG_SW3_IDX,            /* Developer specific */
    DBG_SW4_IDX,            /* Developer specific */

    DBG_HEM_IDX,            /* HEM */
    DBG_AIS_IDX,            /* AIS */
    DBG_RLM_IDX,            /* RLM */
    DBG_MEM_IDX,            /* RLM */
    DBG_CNM_IDX,            /* CNM */
    DBG_RSN_IDX,            /* RSN */
    DBG_BSS_IDX,            /* BSS */
    DBG_SCN_IDX,            /* SCN */
    DBG_SAA_IDX,            /* SAA */
    DBG_AAA_IDX,            /* AAA */
    DBG_P2P_IDX,            /* P2P */
    DBG_QM_IDX,             /* QUE_MGT */
    DBG_SEC_IDX,            /* SEC */
    DBG_BOW_IDX,            /* BOW */
    DBG_WAPI_IDX,           /* WAPI */
    DBG_ROAMING_IDX,        /* ROAMING */

    DBG_MODULE_NUM          /* Notice the XLOG check */
} ENUM_DBG_MODULE_T;

//XLOG
//#define XLOG_DBG_MODULE_IDX    28 /* DBG_MODULE_NUM */
//#if (XLOG_DBG_MODULE_IDX != XLOG_DBG_MODULE_IDX)
//#error "Please modify the DBG_MODULE_NUM and make sure this include at XLOG"
//#endif

/* Define who owns developer specific index */
#define DBG_YARCO_IDX           DBG_SW1_IDX
#define DBG_KEVIN_IDX           DBG_SW2_IDX
#define DBG_CMC_IDX             DBG_SW3_IDX
#define DBG_GEORGE_IDX          DBG_SW4_IDX

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
/* Debug print format string for the OS system time */
#define OS_SYSTIME_DBG_FORMAT               "0x%08x"

/* Debug print argument for the OS system time */
#define OS_SYSTIME_DBG_ARGUMENT(systime)    (systime)

/* Debug print format string for the MAC Address */
#define MACSTR          "%02x:%02x:%02x:%02x:%02x:%02x"

/* Debug print argument for the MAC Address */
#define MAC2STR(a)      ((PUINT_8)a)[0], ((PUINT_8)a)[1], ((PUINT_8)a)[2], \
                        ((PUINT_8)a)[3], ((PUINT_8)a)[4], ((PUINT_8)a)[5]

/* The pre-defined format to dump the value of a varaible with its name shown. */
#define DUMPVAR(variable, format)           (#variable " = " format "\n", variable)

/* The pre-defined format to dump the MAC type value with its name shown. */
#define DUMPMACADDR(addr)                   (#addr " = " MACSTR "\n", MAC2STR(addr))


/* Basiclly, we just do renaming of KAL functions although they should
 * be defined as "Nothing to do" if DBG=0. But in some compiler, the macro
 * syntax does not support  #define LOG_FUNC(x,...)
 *
 * A caller shall not invoke these three macros when DBG=0.
 */
#define LOG_FUNC_TIME           kalPrint
#define LOG_FUNC                kalPrint

#if DBG

    #define TMP_BUF_LEN   256
    #define TMP_WBUF_LEN  (TMP_BUF_LEN * 2)

    extern PINT_16 g_wbuf_p;
    extern PINT_8 g_buf_p;

    /* If __FUNCTION__ is already defined by compiler, we just use it. */
    #if defined(__FUNCTION__)
        #define DEBUGFUNC(_Func)
    #else
        #define DEBUGFUNC(_Func) static const char __FUNCTION__[] = _Func;
    #endif

    /* The following macros are used for future debug message. */
    /* TODO(Kevin): We should remove INITLOG/ERRORLOG/WARNLOG macro sooner or later */
    #define INITLOG(_Fmt) \
    { \
        if (aucDebugModule[DBG_INIT_IDX] & DBG_CLASS_TRACE) { \
            LOG_FUNC("%s: ", __FUNCTION__); \
            LOG_FUNC _Fmt; \
        } \
    }

    #define ERRORLOG(_Fmt) \
    { \
        if (aucDebugModule[DBG_INIT_IDX] & DBG_CLASS_ERROR) { \
            LOG_FUNC("**Error[%s:%d]-", __FILE__, __LINE__); \
            LOG_FUNC _Fmt; \
        } \
    }

    #define WARNLOG(_Fmt) \
    { \
        if (aucDebugModule[DBG_INIT_IDX] & DBG_CLASS_WARN) { \
            LOG_FUNC("**Warning[%s:%d]-", __FILE__, __LINE__); \
            LOG_FUNC _Fmt; \
        } \
    }

    /*lint -save -e960 Multiple use of '#/##' */
    #define DBGLOG(_Module, _Class, _Fmt) \
        { \
            if (aucDebugModule[DBG_##_Module##_IDX] & DBG_CLASS_##_Class) { \
                if (DBG_CLASS_##_Class == DBG_CLASS_ERROR) { \
                    LOG_FUNC_TIME("**Error[%s:%d]-", __FILE__, __LINE__); \
                    LOG_FUNC("%s: (" #_Module " " #_Class ") ", __FUNCTION__); \
                } \
                else if (DBG_CLASS_##_Class == DBG_CLASS_WARN) { \
                    LOG_FUNC_TIME("**Warning[%s:%d]-", __FILE__, __LINE__); \
                    LOG_FUNC("%s: (" #_Module " " #_Class ") ", __FUNCTION__); \
                } \
                else if (DBG_CLASS_##_Class == DBG_CLASS_EVENT) { \
                } \
                else { \
                    LOG_FUNC_TIME("%s: (" #_Module " " #_Class ") ", __FUNCTION__); \
                } \
                LOG_FUNC _Fmt; \
            } \
        }

    #define DBGLOG_MEM8(_Module, _Class, _StartAddr, _Length) \
        { \
            if (aucDebugModule[DBG_##_Module##_IDX] & DBG_CLASS_##_Class) { \
                LOG_FUNC("%s: (" #_Module " " #_Class ")\n", __FUNCTION__); \
                dumpMemory8((PUINT_8) (_StartAddr), (UINT_32) (_Length)); \
            } \
        }

    #define DBGLOG_MEM32(_Module, _Class, _StartAddr, _Length) \
        { \
            if (aucDebugModule[DBG_##_Module##_IDX] & DBG_CLASS_##_Class) { \
                LOG_FUNC("%s: (" #_Module " " #_Class ")\n", __FUNCTION__); \
                dumpMemory32((PUINT_32) (_StartAddr), (UINT_32) (_Length)); \
            } \
        }
    /*lint -restore */

    /*lint -save -e961 use of '#undef' is discouraged */
    #undef ASSERT
    /*lint -restore */

    #ifdef _lint
    #define ASSERT(_exp) \
        { \
            if (!(_exp)) {do {} while (1);} \
        }
    #else
    #define ASSERT(_exp) \
        { \
            if (!(_exp) && !fgIsBusAccessFailed) { \
                LOG_FUNC("Assertion failed: %s:%d %s\n", __FILE__, __LINE__, #_exp); \
                kalBreakPoint(); \
            } \
        }
    #endif /* _lint */

    #define ASSERT_REPORT(_exp, _fmt) \
        { \
            if (!(_exp) && !fgIsBusAccessFailed) { \
                LOG_FUNC("Assertion failed: %s:%d %s\n", __FILE__, __LINE__, #_exp); \
                LOG_FUNC _fmt; \
                kalBreakPoint(); \
            } \
        }

    #define DISP_STRING(_str)       _str

#else /* !DBG */

    #define DEBUGFUNC(_Func)
    #define INITLOG(_Fmt)
    #define ERRORLOG(_Fmt)
    #define WARNLOG(_Fmt)

#if defined(LINUX)
    #define DBGLOG(_Module, _Class, _Fmt) \
    { \
        _Module##_##_Class##_LOGFUNC _Fmt; \
    }
#else
    #define DBGLOG(_Module, _Class, _Fmt)
#endif

#if CFG_SUPPORT_XLOG
    #define DBGLOG_MEM8(_Module, _Class, _StartAddr, _Length) \
    { \
        _Module##_##_Class##_LOGFUNC (__FUNCTION__);\
        _Module##_##_Class##_LOGDUMP8(_StartAddr, _Length); \
    }
#else
    #define DBGLOG_MEM8(_Module, _Class, _StartAddr, _Length)
#endif
    #define DBGLOG_MEM32(_Module, _Class, _StartAddr, _Length)

    #undef ASSERT

#if BUILD_QA_DBG
    #if defined(LINUX) /* For debugging in Linux w/o GDB */
        #define ASSERT(_exp) \
            { \
                if (!(_exp) && !fgIsBusAccessFailed) { \
                    LOG_FUNC("Assertion failed: %s:%d (%s)\n", __FILE__, __LINE__, #_exp); \
                    kalBreakPoint(); \
                } \
            }

        #define ASSERT_REPORT(_exp, _fmt) \
            { \
                if (!(_exp) && !fgIsBusAccessFailed) { \
                    LOG_FUNC("Assertion failed: %s:%d (%s)\n", __FILE__, __LINE__, #_exp); \
                    LOG_FUNC _fmt; \
                    kalBreakPoint(); \
                } \
            }
    #else
        #ifdef WINDOWS_CE
            #define UNICODE_TEXT(_msg)  TEXT(_msg)
            #define ASSERT(_exp) \
                { \
                    if (!(_exp) && !fgIsBusAccessFailed) { \
                        TCHAR rUbuf[256]; \
                        kalBreakPoint(); \
                        _stprintf(rUbuf, TEXT("Assertion failed: %s:%d %s\n"), \
                            UNICODE_TEXT(__FILE__), \
                            __LINE__, \
                            UNICODE_TEXT(#_exp)); \
                        MessageBox(NULL, rUbuf, TEXT("ASSERT!"), MB_OK); \
                    } \
                }

            #define ASSERT_REPORT(_exp, _fmt) \
                { \
                    if (!(_exp) && !fgIsBusAccessFailed) { \
                        TCHAR rUbuf[256]; \
                        kalBreakPoint(); \
                        _stprintf(rUbuf, TEXT("Assertion failed: %s:%d %s\n"), \
                            UNICODE_TEXT(__FILE__), \
                            __LINE__, \
                            UNICODE_TEXT(#_exp)); \
                        MessageBox(NULL, rUbuf, TEXT("ASSERT!"), MB_OK); \
                    } \
                }
        #else
            #define ASSERT(_exp) \
                { \
                    if (!(_exp) && !fgIsBusAccessFailed) { \
                        kalBreakPoint(); \
                    } \
                }

            #define ASSERT_REPORT(_exp, _fmt) \
                { \
                    if (!(_exp) && !fgIsBusAccessFailed) { \
                        kalBreakPoint(); \
                    } \
                }
        #endif /* WINDOWS_CE */
    #endif /* LINUX */
#else
    #define ASSERT(_exp)
    #define ASSERT_REPORT(_exp, _fmt)
#endif /* BUILD_QA_DBG */

    #define DISP_STRING(_str)       ""

#endif /* DBG */

#if CFG_STARTUP_DEBUG
    #if defined(LINUX)
#define DBGPRINTF kalPrint
    #else
#define DBGPRINTF DbgPrint
    #endif
#else
#define DBGPRINTF(...)
#endif


/* The following macro is used for debugging packed structures. */
#ifndef DATA_STRUC_INSPECTING_ASSERT
#define DATA_STRUC_INSPECTING_ASSERT(expr) \
        switch (0) {case 0: case (expr): default:;}
#endif

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
#if DBG
VOID
dumpMemory8 (
    IN PUINT_8 pucStartAddr,
    IN UINT_32 u4Length
    );

VOID
dumpMemory32 (
    IN PUINT_32 pu4StartAddr,
    IN UINT_32  u4Length
    );
#elif CFG_SUPPORT_XLOG
VOID
dumpMemory8 (
    IN UINT_32  log_level,
    IN PUINT_8 pucStartAddr,
    IN UINT_32 u4Length
    );
#endif /* DBG */

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _DEBUG_H */

