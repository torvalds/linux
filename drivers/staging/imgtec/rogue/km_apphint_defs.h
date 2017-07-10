/*************************************************************************/ /*!
@File
@Title          Services AppHint definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/


#ifndef __KM_APPHINT_DEFS_H__
#define __KM_APPHINT_DEFS_H__

/* NB: The 'DEVICE' AppHints must be last in this list as they will be
 * duplicated in the case of a driver supporting multiple devices
 */
#define APPHINT_LIST_ALL \
	APPHINT_LIST_BUILDVAR \
	APPHINT_LIST_MODPARAM \
	APPHINT_LIST_DEBUGFS \
	APPHINT_LIST_DEPRECATED \
	APPHINT_LIST_DEBUGFS_DEVICE

/*
*******************************************************************************
 Build variables
 All of these should be configurable only through the 'default' value
******************************************************************************/
#define APPHINT_LIST_BUILDVAR \
/* name,                            type,           class,       default,                                         helper,         */ \
X(HWRDebugDumpLimit,                UINT32,         DEBUG,       PVRSRV_APPHINT_HWRDEBUGDUMPLIMIT,                NULL             ) \
X(EnableTrustedDeviceAceConfig,     BOOL,           GPUVIRT_VAL, PVRSRV_APPHINT_ENABLETRUSTEDDEVICEACECONFIG,     NULL             ) \
X(HTBufferSize,                     UINT32,         ALWAYS,      PVRSRV_APPHINT_HTBUFFERSIZE,                     NULL             ) \
X(CleanupThreadPriority,            UINT32,         NEVER,       PVRSRV_APPHINT_CLEANUPTHREADPRIORITY,            NULL             ) \
X(CleanupThreadWeight,              UINT32,         NEVER,       PVRSRV_APPHINT_CLEANUPTHREADWEIGHT,              NULL             ) \
X(WatchdogThreadPriority,           UINT32,         NEVER,       PVRSRV_APPHINT_WATCHDOGTHREADPRIORITY,           NULL             ) \
X(WatchdogThreadWeight,             UINT32,         NEVER,       PVRSRV_APPHINT_WATCHDOGTHREADWEIGHT,             NULL             ) \

/*
*******************************************************************************
 Module parameters
******************************************************************************/
#define APPHINT_LIST_MODPARAM \
/* name,                            type,           class,       default,                                         helper,         */ \
X(EnableSignatureChecks,            BOOL,           PDUMP,       PVRSRV_APPHINT_ENABLESIGNATURECHECKS,            NULL             ) \
X(SignatureChecksBufSize,           UINT32,         PDUMP,       PVRSRV_APPHINT_SIGNATURECHECKSBUFSIZE,           NULL             ) \
\
X(DisableClockGating,               BOOL,           FWDBGCTRL,   PVRSRV_APPHINT_DISABLECLOCKGATING,               NULL             ) \
X(DisableDMOverlap,                 BOOL,           FWDBGCTRL,   PVRSRV_APPHINT_DISABLEDMOVERLAP,                 NULL             ) \
\
X(EnableCDMKillingRandMode,         BOOL,           VALIDATION,  PVRSRV_APPHINT_ENABLECDMKILLINGRANDMODE,         NULL             ) \
X(EnableFWContextSwitch,            UINT32,         FWDBGCTRL,   PVRSRV_APPHINT_ENABLEFWCONTEXTSWITCH,            NULL             ) \
X(EnableRDPowerIsland,              UINT32,         FWDBGCTRL,   PVRSRV_APPHINT_ENABLERDPOWERISLAND,              NULL             ) \
\
X(GeneralNon4KHeapPageSize,         UINT32,         ALWAYS,      PVRSRV_APPHINT_GENERAL_NON4K_HEAP_PAGE_SIZE,     NULL             ) \
\
X(FirmwarePerf,                     UINT32,         VALIDATION,  PVRSRV_APPHINT_FIRMWAREPERF,                     NULL             ) \
X(FWContextSwitchProfile,           UINT32,         VALIDATION,  PVRSRV_APPHINT_FWCONTEXTSWITCHPROFILE,           NULL             ) \
X(HWPerfDisableCustomCounterFilter, BOOL,           VALIDATION,  PVRSRV_APPHINT_HWPERFDISABLECUSTOMCOUNTERFILTER, NULL             ) \
X(HWPerfFWBufSizeInKB,              UINT32,         VALIDATION,  PVRSRV_APPHINT_HWPERFFWBUFSIZEINKB,              NULL             ) \
X(HWPerfHostBufSizeInKB,            UINT32,         VALIDATION,  PVRSRV_APPHINT_HWPERFHOSTBUFSIZEINKB,            NULL             ) \
\
X(JonesDisableMask,                 UINT32,         VALIDATION,  PVRSRV_APPHINT_JONESDISABLEMASK,                 NULL             ) \
X(NewFilteringMode,                 BOOL,           VALIDATION,  PVRSRV_APPHINT_NEWFILTERINGMODE,                 NULL             ) \
X(TruncateMode,                     UINT32,         VALIDATION,  PVRSRV_APPHINT_TRUNCATEMODE,                     NULL             ) \
X(UseMETAT1,                        UINT32,         VALIDATION,  PVRSRV_APPHINT_USEMETAT1,                        NULL             ) \
X(RGXBVNC,                          STRING,         ALWAYS,      PVRSRV_APPHINT_RGXBVNC,                          NULL             ) \

/*
*******************************************************************************
 Debugfs parameters - driver configuration
******************************************************************************/
#define APPHINT_LIST_DEBUGFS \
/* name,                            type,           class,       default,                                         helper,         */ \
X(EnableHTBLogGroup,                UINT32Bitfield, ALWAYS,      PVRSRV_APPHINT_ENABLEHTBLOGGROUP,                htb_loggroup_tbl ) \
X(HTBOperationMode,                 UINT32List,     ALWAYS,      PVRSRV_APPHINT_HTBOPERATIONMODE,                 htb_opmode_tbl   ) \
X(HWPerfFWFilter,                   UINT64,         ALWAYS,      PVRSRV_APPHINT_HWPERFFWFILTER,                   NULL             ) \
X(HWPerfHostFilter,                 UINT32,         ALWAYS,      PVRSRV_APPHINT_HWPERFHOSTFILTER,                 NULL             ) \
X(TimeCorrClock,                    UINT32List,     ALWAYS,      PVRSRV_APPHINT_TIMECORRCLOCK,                    timecorr_clk_tbl )

/*
*******************************************************************************
 Debugfs parameters - device configuration
******************************************************************************/
#define APPHINT_LIST_DEBUGFS_DEVICE \
/* name,                            type,           class,       default,                                         helper,         */ \
/* Device Firmware config */\
X(AssertOnHWRTrigger,               BOOL,           ALWAYS,      PVRSRV_APPHINT_ASSERTONHWRTRIGGER,               NULL             ) \
X(AssertOutOfMemory,                BOOL,           ALWAYS,      PVRSRV_APPHINT_ASSERTOUTOFMEMORY,                NULL             ) \
X(CheckMList,                       BOOL,           ALWAYS,      PVRSRV_APPHINT_CHECKMLIST,                       NULL             ) \
X(EnableHWR,                        BOOL,           ALWAYS,      APPHNT_BLDVAR_ENABLEHWR,                         NULL             ) \
X(EnableLogGroup,                   UINT32Bitfield, ALWAYS,      PVRSRV_APPHINT_ENABLELOGGROUP,                   fwt_loggroup_tbl ) \
X(FirmwareLogType,                  UINT32List,     ALWAYS,      PVRSRV_APPHINT_FIRMWARELOGTYPE,                  fwt_logtype_tbl  ) \
/* Device host config */ \
X(EnableAPM,                        UINT32,         ALWAYS,      PVRSRV_APPHINT_ENABLEAPM,                        NULL             ) \
X(DisableFEDLogging,                BOOL,           ALWAYS,      PVRSRV_APPHINT_DISABLEFEDLOGGING,                NULL             ) \
X(ZeroFreelist,                     BOOL,           ALWAYS,      PVRSRV_APPHINT_ZEROFREELIST,                     NULL             ) \
X(DustRequestInject,                BOOL,           VALIDATION,  PVRSRV_APPHINT_DUSTREQUESTINJECT,                NULL             ) \
X(DisablePDumpPanic,                BOOL,           PDUMP,       PVRSRV_APPHINT_DISABLEPDUMPPANIC,                NULL             ) \
X(EnableFWPoisonOnFree,             BOOL,           ALWAYS,      PVRSRV_APPHINT_ENABLEFWPOISONONFREE,             NULL             ) \
X(FWPoisonOnFreeValue,              UINT32,         ALWAYS,      PVRSRV_APPHINT_FWPOISONONFREEVALUE,              NULL             ) \

/*
*******************************************************************************
 Deprecated parameters kept for backwards compatibility
******************************************************************************/
#define APPHINT_LIST_DEPRECATED \
/* name,                            type,           class,       default,                                         helper,         */ \
X(EnableFTraceGPU,                  BOOL,           ALWAYS,      0,                                               NULL             ) \
X(EnableRTUBypass,                  BOOL,           ALWAYS,      0,                                               NULL             ) \
\
X(EnableHWPerf,                     BOOL,           ALWAYS,      0,                                               NULL             ) \
X(EnableHWPerfHost,                 BOOL,           ALWAYS,      0,                                               NULL             ) \
\
X(DisablePDP,                       BOOL,           PDUMP,       PVRSRV_APPHINT_DISABLEPDUMPPANIC,                NULL             ) \
X(HWPerfFilter0,                    UINT32,         ALWAYS,      0,                                               NULL             ) \
X(HWPerfFilter1,                    UINT32,         ALWAYS,      0,                                               NULL             ) \

/*
*******************************************************************************
 * Types used in the APPHINT_LIST_<GROUP> lists must be defined here.
 * New types require specific handling code to be added
******************************************************************************/
#define APPHINT_DATA_TYPE_LIST \
X(BOOL) \
X(UINT64) \
X(UINT32) \
X(UINT32Bitfield) \
X(UINT32List) \
X(STRING)

#define APPHINT_CLASS_LIST \
X(ALWAYS) \
X(NEVER) \
X(DEBUG) \
X(FWDBGCTRL) \
X(PDUMP) \
X(VALIDATION) \
X(GPUVIRT_VAL)

/*
*******************************************************************************
 Visibility control for module parameters
 These bind build variables to AppHint Visibility Groups.
******************************************************************************/
#define APPHINT_ENABLED_CLASS_ALWAYS IMG_TRUE
#define APPHINT_ENABLED_CLASS_NEVER IMG_FALSE
#define apphint_modparam_class_ALWAYS(a, b, c) apphint_modparam_enable(a, b, c)
#if defined(DEBUG)
	#define APPHINT_ENABLED_CLASS_DEBUG IMG_TRUE
	#define apphint_modparam_class_DEBUG(a, b, c) apphint_modparam_enable(a, b, c)
#else
	#define APPHINT_ENABLED_CLASS_DEBUG IMG_FALSE
	#define apphint_modparam_class_DEBUG(a, b, c)
#endif
#if defined(SUPPORT_FWDBGCTRL)
	#define APPHINT_ENABLED_CLASS_FWDBGCTRL IMG_TRUE
	#define apphint_modparam_class_FWDBGCTRL(a, b, c) apphint_modparam_enable(a, b, c)
#else
	#define APPHINT_ENABLED_CLASS_FWDBGCTRL IMG_FALSE
	#define apphint_modparam_class_FWDBGCTRL(a, b, c)
#endif
#if defined(PDUMP)
	#define APPHINT_ENABLED_CLASS_PDUMP IMG_TRUE
	#define apphint_modparam_class_PDUMP(a, b, c) apphint_modparam_enable(a, b, c)
#else
	#define APPHINT_ENABLED_CLASS_PDUMP IMG_FALSE
	#define apphint_modparam_class_PDUMP(a, b, c)
#endif
#if defined(SUPPORT_VALIDATION)
	#define APPHINT_ENABLED_CLASS_VALIDATION IMG_TRUE
	#define apphint_modparam_class_VALIDATION(a, b, c) apphint_modparam_enable(a, b, c)
#else
	#define APPHINT_ENABLED_CLASS_VALIDATION IMG_FALSE
	#define apphint_modparam_class_VALIDATION(a, b, c)
#endif
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	#define APPHINT_ENABLED_CLASS_GPUVIRT_VAL IMG_TRUE
	#define apphint_modparam_class_GPUVIRT_VAL(a, b, c) apphint_modparam_enable(a, b, c)
#else
	#define APPHINT_ENABLED_CLASS_GPUVIRT_VAL IMG_FALSE
	#define apphint_modparam_class_GPUVIRT_VAL(a, b, c)
#endif

/*
*******************************************************************************
 AppHint defaults based on other build parameters
******************************************************************************/
#if defined(HWR_DEFAULT_ENABLED)
	#define APPHNT_BLDVAR_ENABLEHWR         1
#else
	#define APPHNT_BLDVAR_ENABLEHWR         0
#endif
#if defined(DEBUG)
	#define APPHNT_BLDVAR_DEBUG             1
	#define APPHNT_BLDVAR_DBGDUMPLIMIT      RGXFWIF_HWR_DEBUG_DUMP_ALL
#else
	#define APPHNT_BLDVAR_DEBUG             0
	#define APPHNT_BLDVAR_DBGDUMPLIMIT      1
#endif
#if defined(DEBUG) || defined(PDUMP)
#define APPHNT_BLDVAR_ENABLESIGNATURECHECKS     IMG_TRUE
#else
#define APPHNT_BLDVAR_ENABLESIGNATURECHECKS     IMG_FALSE
#endif

/*
*******************************************************************************

 Table generated enums

******************************************************************************/
/* Unique ID for all AppHints */
typedef enum {
#define X(a, b, c, d, e) APPHINT_ID_ ## a,
	APPHINT_LIST_ALL
#undef X
	APPHINT_ID_MAX
} APPHINT_ID;

/* ID for build variable Apphints - used for build variable only structures */
typedef enum {
#define X(a, b, c, d, e) APPHINT_BUILDVAR_ID_ ## a,
	APPHINT_LIST_BUILDVAR
#undef X
	APPHINT_BUILDVAR_ID_MAX
} APPHINT_BUILDVAR_ID;

/* ID for Modparam Apphints - used for modparam only structures */
typedef enum {
#define X(a, b, c, d, e) APPHINT_MODPARAM_ID_ ## a,
	APPHINT_LIST_MODPARAM
#undef X
	APPHINT_MODPARAM_ID_MAX
} APPHINT_MODPARAM_ID;

/* ID for Debugfs Apphints - used for debugfs only structures */
typedef enum {
#define X(a, b, c, d, e) APPHINT_DEBUGFS_ID_ ## a,
	APPHINT_LIST_DEBUGFS
#undef X
	APPHINT_DEBUGFS_ID_MAX
} APPHINT_DEBUGFS_ID;

/* ID for Debugfs Device Apphints - used for debugfs device only structures */
typedef enum {
#define X(a, b, c, d, e) APPHINT_DEBUGFS_DEVICE_ID_ ## a,
	APPHINT_LIST_DEBUGFS_DEVICE
#undef X
	APPHINT_DEBUGFS_DEVICE_ID_MAX
} APPHINT_DEBUGFS_DEVICE_ID;

/* data types and actions */
typedef enum {
	APPHINT_DATA_TYPE_INVALID = 0,
#define X(a) APPHINT_DATA_TYPE_ ## a,
	APPHINT_DATA_TYPE_LIST
#undef X
	APPHINT_DATA_TYPE_MAX
} APPHINT_DATA_TYPE;

typedef enum {
#define X(a) APPHINT_CLASS_ ## a,
	APPHINT_CLASS_LIST
#undef X
	APPHINT_CLASS_MAX
} APPHINT_CLASS;

#endif /* __KM_APPHINT_DEFS_H__ */

