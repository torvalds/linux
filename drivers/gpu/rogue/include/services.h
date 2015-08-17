/*************************************************************************/ /*!
@File
@Title          Services API Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported services API details
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

#ifndef __SERVICES_H__
#define __SERVICES_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "sync_external.h"
#include "pdumpdefs.h"
#include "lock_types.h"
#include "pvr_debug.h"

/* 
*/

#if defined(LDDM)
/* LDDM build needs to include this for the allocation structure */
#include "umallocation.h"
#endif

#include "pvrsrv_device_types.h"

/* The comment below is the front page for code-generated doxygen documentation */
/*!
 ******************************************************************************
 @mainpage
 This document details the APIs and implementation of the Consumer Services.
 It is intended to be used in conjunction with the Consumer Services
 Software Architectural Specification and the Consumer Services Software
 Functional Specification.
 *****************************************************************************/

/******************************************************************************
 * 	#defines
 *****************************************************************************/

/*! 4k page size definition */
#define PVRSRV_4K_PAGE_SIZE					4096UL      /*!< Size of a 4K Page */
#define PVRSRV_4K_PAGE_SIZE_ALIGNSHIFT		12          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 16k page size definition */
#define PVRSRV_16K_PAGE_SIZE					16384UL      /*!< Size of a 16K Page */
#define PVRSRV_16K_PAGE_SIZE_ALIGNSHIFT		14          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 64k page size definition */
#define PVRSRV_64K_PAGE_SIZE					65536UL      /*!< Size of a 64K Page */
#define PVRSRV_64K_PAGE_SIZE_ALIGNSHIFT		16          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 256k page size definition */
#define PVRSRV_256K_PAGE_SIZE					262144UL      /*!< Size of a 256K Page */
#define PVRSRV_256K_PAGE_SIZE_ALIGNSHIFT		18          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 1MB page size definition */
#define PVRSRV_1M_PAGE_SIZE					1048576UL      /*!< Size of a 1M Page */
#define PVRSRV_1M_PAGE_SIZE_ALIGNSHIFT		20          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 2MB page size definition */
#define PVRSRV_2M_PAGE_SIZE					2097152UL      /*!< Size of a 2M Page */
#define PVRSRV_2M_PAGE_SIZE_ALIGNSHIFT		21          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */


#define EVENTOBJNAME_MAXLENGTH (50) /*!< Max length of an event object name */


/*!
	Flags for Services connection.
	Allows to define per-client policy for Services
*/
#define SRV_FLAGS_PERSIST		(1U << 0)  /*!< Persist client flag */
#define SRV_FLAGS_INIT_PROCESS	(1U << 1)  /*!< Allows connect to succeed if SrvInit
                                            * has not yet run (used by SrvInit itself) */
#define SRV_FLAGS_PDUMPCTRL     (1U << 31) /*!< PDump Ctrl client flag */

/*
	Pdump flags which are accessible to Services clients
*/
/* 
*/
#define PVRSRV_PDUMP_FLAGS_CONTINUOUS		0x40000000UL /*!< pdump continuous */

#define PVRSRV_UNDEFINED_HEAP_ID			(~0LU)

/*!
 ******************************************************************************
 * User Module type
 *****************************************************************************/
typedef enum
{
	IMG_EGL				= 0x00000001,       /*!< EGL Module */
	IMG_OPENGLES1		= 0x00000002,       /*!< OGLES1 Module */
	IMG_OPENGLES3		= 0x00000003,       /*!< OGLES3 Module */
	IMG_D3DM			= 0x00000004,       /*!< D3DM Module */
	IMG_SRV_UM			= 0x00000005,       /*!< Services User-Mode */
	IMG_SRV_INIT		= 0x00000006,		/*!< Services initialisation */
	IMG_SRVCLIENT		= 0x00000007,       /*!< Services Client */
	IMG_WDDMKMD			= 0x00000008,       /*!< WDDM KMD */
	IMG_WDDM3DNODE		= 0x00000009,       /*!< WDDM 3D Node */
	IMG_WDDMMVIDEONODE	= 0x0000000A,       /*!< WDDM MVideo Node */
	IMG_WDDMVPBNODE		= 0x0000000B,       /*!< WDDM VPB Node */
	IMG_OPENGL			= 0x0000000C,       /*!< OpenGL */
	IMG_D3D				= 0x0000000D,       /*!< D3D */
	IMG_OPENCL			= 0x0000000E,       /*!< OpenCL */
	IMG_ANDROID_HAL		= 0x0000000F,       /*!< Graphics HAL */
	IMG_WEC_GPE			= 0x00000010,		/*!< WinEC-specific GPE */
	IMG_PVRGPE			= 0x00000011,		/*!< WinEC/WinCE GPE */
	IMG_RSCOMPUTE       = 0x00000012,       /*!< RenderScript Compute */
	IMG_OPENRL          = 0x00000013,       /*!< OpenRL Module */
	IMG_PDUMPCTRL		= 0x00000014,       /*!< PDump control client */

} IMG_MODULE_ID;

/*! Max length of an App-Hint string */
#define APPHINT_MAX_STRING_SIZE	256

/*!
 ******************************************************************************
 * IMG data types
 *****************************************************************************/
typedef enum
{
	IMG_STRING_TYPE		= 1,                    /*!< String type */
	IMG_FLOAT_TYPE		,                       /*!< Float type */
	IMG_UINT_TYPE		,                       /*!< Unsigned Int type */
	IMG_INT_TYPE		,                       /*!< (Signed) Int type */
	IMG_FLAG_TYPE                               /*!< Flag Type */
}IMG_DATA_TYPE;


/******************************************************************************
 * Structure definitions.
 *****************************************************************************/

/*!
 * Forward declaration
 */
typedef struct _PVRSRV_DEV_DATA_ *PPVRSRV_DEV_DATA;
/*!
 * Forward declaration (look on connection.h)
 */
typedef struct _PVRSRV_CONNECTION_ PVRSRV_CONNECTION;


/*************************************************************************/ /*!
 * Client dev info
 */ /*************************************************************************/
typedef struct _PVRSRV_CLIENT_DEV_DATA_
{
	IMG_UINT32		ui32NumDevices;				                                /*!< Number of services-managed devices connected */
	PVRSRV_DEVICE_IDENTIFIER asDevID[PVRSRV_MAX_DEVICES];		                /*!< Device identifiers */
	PVRSRV_ERROR	(*apfnDevConnect[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);	/*!< device-specific connection callback */
	PVRSRV_ERROR	(*apfnDumpTrace[PVRSRV_MAX_DEVICES])(PPVRSRV_DEV_DATA);		/*!< device-specific debug trace callback */

} PVRSRV_CLIENT_DEV_DATA;

/*!
 ******************************************************************************
 * This structure allows the user mode glue code to have an OS independent
 * set of prototypes.
 *****************************************************************************/
typedef struct _PVRSRV_DEV_DATA_
{
	PVRSRV_CONNECTION	 *psConnection;	/*!< Services connection info */
	IMG_HANDLE			hDevCookie;				/*!< Dev cookie */

} PVRSRV_DEV_DATA;

/*************************************************************************/ /*! 
    PVR Client Event handling in Services
*/ /**************************************************************************/
typedef enum _PVRSRV_CLIENT_EVENT_
{
	PVRSRV_CLIENT_EVENT_HWTIMEOUT = 0,              /*!< hw timeout event */
} PVRSRV_CLIENT_EVENT;

/**************************************************************************/ /*!
@Function       PVRSRVClientEvent
@Description    Handles timeouts occurring in client drivers
@Input          eEvent          event type
@Input          psDevData       pointer to the PVRSRV_DEV_DATA context
@Input          pvData          client-specific data
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_ 
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVClientEvent(const PVRSRV_CLIENT_EVENT eEvent,
											PVRSRV_DEV_DATA *psDevData,
											IMG_PVOID pvData);

/******************************************************************************
 * PVR Services API prototypes.
 *****************************************************************************/

/**************************************************************************/ /*!
@Function       PVRSRVConnect
@Description    Creates a connection from an application to the services 
                module and initialises device-specific call-back functions.
@Output         ppsConnection   on Success, *ppsConnection is set to the new 
                                PVRSRV_CONNECTION instance.
@Input          ui32SrvFlags    a bit-wise OR of the following:
                                SRV_FLAGS_PERSIST
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_ 
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVConnect(PVRSRV_CONNECTION **ppsConnection, IMG_UINT32 ui32SrvFlags);

/**************************************************************************/ /*!
@Function       PVRSRVDisconnect 
@Description    Disconnects from the services module
@Input          psConnection    the connection to be disconnected
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDisconnect(PVRSRV_CONNECTION *psConnection);

/**************************************************************************/ /*!
@Function       PVRSRVEnumerateDevices
@Description    Enumerate all services managed devices in the 
                system.

                The function returns a list of the device IDs stored either
                in the services (or constructed in the user mode glue 
                component in certain environments). The number of devices 
                in the list is also returned.

                The user is required to provide a buffer large enough to 
                receive an array of MAX_NUM_DEVICE_IDS *
                PVRSRV_DEVICE_IDENTIFIER structures.

                In a binary layered component which does not support dynamic
                runtime selection, the glue code should compile to return 
                the supported devices statically, e.g. multiple instances of
                the same device if multiple devices are supported

                In the case of an environment (for instance) where one 
                services managed device may connect to two display devices
                this code would enumerate all three devices and even
                non-dynamic device selection code should retain the facility
                to parse the list to find the index of a given device.}

@Input          psConnection    Services connection
@Output         puiNumDevices   Number of devices present in the system
@Output         puiDevIDs       Pointer to called supplied array of
                                PVRSRV_DEVICE_IDENTIFIER structures. The
                                array is assumed to be at least
                                PVRSRV_MAX_DEVICES long.
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevices(const PVRSRV_CONNECTION 	*psConnection,
													IMG_UINT32 					*puiNumDevices,
													PVRSRV_DEVICE_IDENTIFIER 	*puiDevIDs);

/**************************************************************************/ /*!
@Function       PVRSRVAcquireDeviceData
@Description    Returns device info structure pointer for the requested device.
                This populates a PVRSRV_DEV_DATA structure with appropriate 
                pointers to the DevInfo structure for the device requested.

                In a non-plug-and-play the first call to GetDeviceInfo for a
                device causes device initialisation

                Calls to GetDeviceInfo are reference counted
@Input          psConnection    Services connection
@Input          uiDevIndex      Index to the required device obtained from the 
                                PVRSRVEnumerateDevice function 
@Output         psDevData       The returned Device Data
@Input          eDeviceType     Required device type. If type is unknown use 
                                uiDevIndex to locate device data
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceData(PVRSRV_CONNECTION 	*psConnection,
													IMG_UINT32			uiDevIndex,
													PVRSRV_DEV_DATA		*psDevData,
													PVRSRV_DEVICE_TYPE	eDeviceType);

/**************************************************************************/ /*!
@Function       PVRSRVPollForValue
@Description    Polls for a value to match a masked read of System Memory.
                The function returns when either (1) the value read back
                matches ui32Value, or (2) the maximum number of tries has
                been reached.
@Input          psConnection        Services connection
@Input          hOSEvent            Handle to OS event object to wait for
@Input          pui32LinMemAddr     the address of the memory to poll
@Input          ui32Value           the required value
@Input          ui32Mask            the mask to use
@Input          ui32Waitus          interval between tries (us)
@Input          ui32Tries           number of tries to make before giving up
@Return                             PVRSRV_OK on success. Otherwise, a 
                                    PVRSRV_ error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR PVRSRVPollForValue(const PVRSRV_CONNECTION	*psConnection,
								IMG_HANDLE				hOSEvent,
								volatile IMG_UINT32		*pui32LinMemAddr,
								IMG_UINT32				ui32Value,
								IMG_UINT32				ui32Mask,
								IMG_UINT32				ui32Waitus,
								IMG_UINT32				ui32Tries);

/* this function is almost the same as PVRSRVPollForValue. The only difference
 * is that it now handles the interval between tries itself. Therefore it can
 * correctly handles the differences between the different platforms.
 */
IMG_IMPORT
PVRSRV_ERROR PVRSRVWaitForValue(const PVRSRV_CONNECTION	*psConnection,
                                IMG_HANDLE				hOSEvent,
                                volatile IMG_UINT32		*pui32LinMemAddr,
                                IMG_UINT32				ui32Value,
                                IMG_UINT32				ui32Mask);


/**************************************************************************/ /*!
 @Function      PVRSRVConditionCheckCallback
 @Description   Function prototype for use with the PVRSRVWaitForCondition()
                API. Clients implement this callback to test if the condition
                waited for has been met and become true.

 @Input         pvUserData      Pointer to client user data needed for
                                 the check
 @Output        pbCondMet       Updated on exit with condition state

 @Return        PVRSRV_OK  when condition tested without error
                PVRSRV_*   other system error that will lead to the
                           abnormal termination of the wait API.
 */
/******************************************************************************/
typedef
PVRSRV_ERROR (*PVRSRVConditionCheckCallback)(
        IMG_PVOID  pvUserData,
        IMG_BOOL*  pbCondMet);


/**************************************************************************/ /*!
@Function       PVRSRVWaitForCondition
@Description    Wait using PVRSRVEventObjectWait() for a
                condition (pfnCallback) to become true. It periodically
                checks the condition state by employing a loop and
                waiting on either the event supplied or sleeping for a brief
                time (if hEvent is null) each time the condition is
                checked and found not to be met. When the condition is true
                the function returns. It will also return when the time
                period has been exceeded or an error has occurred.

@Input          psConnection    Services connection
@Input          hEvent          Event to wait on or NULL not to use event
                                 objects but OS wait for a short time.
@Input          pfnCallback     Client condition check callback
@Input          pvUserData      Client user data supplied to callback

@Return         PVRSRV_OK	          When condition met
                PVRSRV_ERROR_TIMEOUT  When condition not met and time is up
                PVRSRV_*              Otherwise, some other error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWaitForCondition(
        const PVRSRV_CONNECTION*     psConnection,
        IMG_HANDLE                   hEvent,
        PVRSRVConditionCheckCallback pfnCallback,
        IMG_PVOID                    pvUserData);


/**************************************************************************/ /*!
@Function       PVRSRVWaitUntilSyncPrimOpReady
@Description    Wait using PVRSRVWaitForCondition for a sync operation to
                become ready.

@Input          psConnection    Services connection
@Input          hEvent          Event to wait on or NULL not to use event
                                 objects but OS wait for a short time.
@Input          psOpCookie      Sync operation cookie to test

@Return         PVRSRV_OK	          When condition met
                PVRSRV_ERROR_TIMEOUT  When condition not met and time is up
                PVRSRV_*              Otherwise, some other error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWaitUntilSyncPrimOpReady(
        const PVRSRV_CONNECTION* psConnection,
        IMG_HANDLE               hEvent,
        PSYNC_OP_COOKIE          psOpCookie);


/******************************************************************************
 * PDUMP Function prototypes...
 *****************************************************************************/
#if defined(PDUMP)
/**************************************************************************/ /*!
@Function       PVRSRVPDumpInit
@Description    Pdump initialisation
@Input          psConnection    Services connection
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpInit(const PVRSRV_CONNECTION *psConnection);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpStartInitPhase
@Description    Resume the pdump init phase state   
@Input          psConnection    Services connection
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStartInitPhase(const PVRSRV_CONNECTION *psConnection);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpStopInitPhase
@Description    Stop the pdump init phase state
@Input          psConnection    Services connection
@Input          eModuleID       Which module is requesting to stop the init phase
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpStopInitPhase(const PVRSRV_CONNECTION *psConnection,
												IMG_MODULE_ID eModuleID);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpSetFrame
@Description    Sets the pdump frame
@Input          psConnection    Services connection
@Input          ui32Frame       frame id
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSetFrame(const PVRSRV_CONNECTION *psConnection,
											  IMG_UINT32 ui32Frame);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpGetFrame
@Description    Gets the current pdump frame
@Input          psConnection    Services connection
@Output         pui32Frame       frame id
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_error code
*/ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpGetFrame(const PVRSRV_CONNECTION *psConnection,
											  IMG_UINT32 *pui32Frame);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpIsLastCaptureFrame
@Description    Returns whether this is the last frame of the capture range
@Input          psConnection    Services connection
@Return                         IMG_TRUE if last frame,
                                IMG_FALSE otherwise
*/ /**************************************************************************/
IMG_IMPORT
IMG_BOOL IMG_CALLCONV PVRSRVPDumpIsLastCaptureFrame(const PVRSRV_CONNECTION *psConnection);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpAfterRender
@Description    Executes TraceBuffer and SignatureBuffer commands
@Input          psDevData       Device data
*/ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpAfterRender(PVRSRV_DEV_DATA *psDevData);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpComment
@Description    PDumps a comment
@Input          psConnection        Services connection
@Input          pszComment          Comment to be inserted
@Input          bContinuous         pdump contiunous boolean
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpComment(const PVRSRV_CONNECTION *psConnection,
											 const IMG_CHAR *pszComment,
											 IMG_BOOL bContinuous);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpCommentf
@Description    PDumps a formatted comment
@Input          psConnection        Services connection
@Input          bContinuous         pdump continuous boolean
@Input          pszFormat           Format string
@Input          ...                 vararg list
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentf(const PVRSRV_CONNECTION *psConnection,
											  IMG_BOOL bContinuous,
											  const IMG_CHAR *pszFormat, ...)
											  IMG_FORMAT_PRINTF(3, 4);

/**************************************************************************/ /*!
@Function       PVRSRVPDumpCommentWithFlagsf
@Description    PDumps a formatted comment, passing in flags
@Input          psConnection        Services connection
@Input          ui32Flags           Flags
@Input          pszFormat           Format string
@Input          ...                 vararg list
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpCommentWithFlagsf(const PVRSRV_CONNECTION *psConnection,
													   IMG_UINT32 ui32Flags,
													   const IMG_CHAR *pszFormat, ...)
													   IMG_FORMAT_PRINTF(3, 4);


/**************************************************************************/ /*!
@Function       PVRSRVPDumpIsCapturing
@Description    Reports whether PDump is currently capturing or not
@Input          psConnection        Services connection
@Output         pbIsCapturing       Indicates whether PDump is currently
                                    capturing
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpIsCapturing(const PVRSRV_CONNECTION *psConnection,
								 				IMG_BOOL *pbIsCapturing);


/**************************************************************************/ /*!
@Function       PVRSRVPDumpIsCapturingTest
@Description    checks whether pdump is currently in frame capture range
@Input          psConnection        Services connection
@Return         IMG_BOOL
 */ /**************************************************************************/
IMG_IMPORT
IMG_BOOL IMG_CALLCONV PVRSRVPDumpIsCapturingTest(const PVRSRV_CONNECTION *psConnection);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPDumpSetDefaultCaptureParams(const PVRSRV_CONNECTION *psConnection,
                                                             IMG_UINT32 ui32Mode,
                                                             IMG_UINT32 ui32Start,
                                                             IMG_UINT32 ui32End,
                                                             IMG_UINT32 ui32Interval,
                                                             IMG_UINT32 ui32MaxParamFileSize);

#else	/* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpInit)
#endif
static INLINE PVRSRV_ERROR 
PVRSRVPDumpInit(const PVRSRV_CONNECTION *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpStartInitPhase)
#endif
static INLINE PVRSRV_ERROR 
PVRSRVPDumpStartInitPhase(const PVRSRV_CONNECTION *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpStopInitPhase)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpStopInitPhase(const PVRSRV_CONNECTION *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpSetFrame)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpSetFrame(const PVRSRV_CONNECTION *psConnection,
					IMG_UINT32 ui32Frame)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Frame);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpGetFrame)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpGetFrame(const PVRSRV_CONNECTION *psConnection,
					IMG_UINT32 *pui32Frame)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(pui32Frame);
	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpIsLastCaptureFrame)
#endif
static INLINE IMG_BOOL
PVRSRVPDumpIsLastCaptureFrame(const PVRSRV_CONNECTION *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	return IMG_FALSE;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpAfterRender)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpAfterRender(PVRSRV_DEV_DATA *psDevData)
{
	PVR_UNREFERENCED_PARAMETER(psDevData);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpComment)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpComment(const PVRSRV_CONNECTION *psConnection,
				   const IMG_CHAR *pszComment,
				   IMG_BOOL bContinuous)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(pszComment);
	PVR_UNREFERENCED_PARAMETER(bContinuous);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpCommentf)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpCommentf(const PVRSRV_CONNECTION *psConnection,
					IMG_BOOL bContinuous,
					const IMG_CHAR *pszFormat, ...)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(bContinuous);
	PVR_UNREFERENCED_PARAMETER(pszFormat);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpCommentWithFlagsf)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpCommentWithFlagsf(const PVRSRV_CONNECTION *psConnection,
							 IMG_UINT32 ui32Flags,
							 const IMG_CHAR *pszFormat, ...)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(pszFormat);
	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpIsCapturing)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpIsCapturing(const PVRSRV_CONNECTION *psConnection,
					   IMG_BOOL *pbIsCapturing)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	*pbIsCapturing = IMG_FALSE;
	return PVRSRV_OK;
}								 				

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPDumpIsCapturingTest)
#endif
static INLINE IMG_BOOL
PVRSRVPDumpIsCapturingTest(const PVRSRV_CONNECTION *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	return IMG_FALSE;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpSetPidCapRange)
#endif
static INLINE PVRSRV_ERROR
PVRSRVPDumpSetDefaultCaptureParams(const PVRSRV_CONNECTION *psConnection,
                                   IMG_UINT32 ui32Mode,
                                   IMG_UINT32 ui32Start,
                                   IMG_UINT32 ui32End,
                                   IMG_UINT32 ui32Interval,
                                   IMG_UINT32 ui32MaxParamFileSize)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Mode);
	PVR_UNREFERENCED_PARAMETER(ui32Start);
	PVR_UNREFERENCED_PARAMETER(ui32End);
	PVR_UNREFERENCED_PARAMETER(ui32Interval);
	PVR_UNREFERENCED_PARAMETER(ui32MaxParamFileSize);

	return PVRSRV_OK;
}


#endif	/* PDUMP */

/**************************************************************************/ /*!
@Function       PVRSRVLoadLibrary
@Description    Load the named Dynamic-Link (Shared) Library. This will perform
				reference counting in association with PVRSRVUnloadLibrary,
				so for example if the same library is loaded twice and unloaded once,
				a reference to the library will remain.
@Input          pszLibraryName      the name of the library to load
@Return                             On success, the handle of the newly-loaded
                                    library. Otherwise, zero.
 */ /**************************************************************************/
IMG_IMPORT IMG_HANDLE	PVRSRVLoadLibrary(const IMG_CHAR *pszLibraryName);

/**************************************************************************/ /*!
@Function       PVRSRVUnloadLibrary
@Description    Unload the Dynamic-Link (Shared) Library which had previously been
				loaded using PVRSRVLoadLibrary(). See PVRSRVLoadLibrary() for
				information regarding reference counting.
@Input          hExtDrv             handle of the Dynamic-Link / Shared library
                                    to unload, as returned by PVRSRVLoadLibrary().
@Return                             PVRSRV_OK if successful. Otherwise,
                                    PVRSRV_ERROR_UNLOAD_LIBRARY_FAILED.
 */ /**************************************************************************/
IMG_IMPORT PVRSRV_ERROR	PVRSRVUnloadLibrary(IMG_HANDLE hExtDrv);

/**************************************************************************/ /*!
@Function       PVRSRVGetLibFuncAddr
@Description    Returns the address of a function in a Dynamic-Link / Shared
                Library.
@Input          hExtDrv             handle of the Dynamic-Link / Shared Library
                                    in which the function resides
@Input          pszFunctionName     the name of the function
@Output         ppvFuncAddr         on success, the address of the function
                                    requested. Otherwise, NULL.
@Return                             PVRSRV_OK if successful. Otherwise,
                                    PVRSRV_ERROR_UNABLE_TO_GET_FUNC_ADDR.
 */ /**************************************************************************/
IMG_IMPORT PVRSRV_ERROR	PVRSRVGetLibFuncAddr(IMG_HANDLE hExtDrv, 
                                            const IMG_CHAR *pszFunctionName, 
                                            IMG_VOID **ppvFuncAddr);

/**************************************************************************/ /*!
@Function       PVRSRVClockus
@Description    Returns the current system clock time, in microseconds.  Note 
                that this does not necessarily guarantee microsecond accuracy.
@Return                             the curent system clock time, in 
                                    microseconds
 */ /**************************************************************************/
IMG_IMPORT IMG_UINT32 PVRSRVClockus (void);

/**************************************************************************/ /*!
@Function       PVRSRVWaitus
@Description    Waits for the specified number of microseconds
@Input          ui32Timeus          the time to wait for, in microseconds 
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID PVRSRVWaitus (IMG_UINT32 ui32Timeus);

/**************************************************************************/ /*!
@Function       PVRSRVReleaseThreadQuanta
@Description    Releases thread quanta
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID PVRSRVReleaseThreadQuanta (void);

/**************************************************************************/ /*!
@Function       PVRSRVGetCurrentProcessID
@Description    Returns handle for current process
@Return         ID of current process
 */ /**************************************************************************/
IMG_IMPORT IMG_PID  IMG_CALLCONV PVRSRVGetCurrentProcessID(void);

/**************************************************************************/ /*!
@Function       PVRSRVSetLocale
@Description    Thin wrapper on posix setlocale
@Input          pszLocale
@Return         IMG_NULL (currently)
 */ /**************************************************************************/
IMG_IMPORT IMG_CHAR * IMG_CALLCONV PVRSRVSetLocale(const IMG_CHAR *pszLocale);


/**************************************************************************/ /*!
@Function       PVRSRVCreateAppHintState
@Description    Create app hint state
@Input          eModuleID       module id
@Input          pszAppName      app name
@Output         ppvState        state
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVCreateAppHintState(IMG_MODULE_ID eModuleID,
														const IMG_CHAR *pszAppName,
														IMG_VOID **ppvState);
/**************************************************************************/ /*!
@Function       PVRSRVFreeAppHintState
@Description    Free the app hint state, if it was created
@Input          eModuleID       module id
@Input          pvHintState     app hint state
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVFreeAppHintState(IMG_MODULE_ID eModuleID,
										 IMG_VOID *pvHintState);

/**************************************************************************/ /*!
@Function       PVRSRVGetAppHint
@Description    Return the value of this hint from state or use default
@Input          pvHintState     hint state
@Input          pszHintName     hint name
@Input          eDataType       data type
@Input          pvDefault       default value
@Output         pvReturn        hint value
@Return                         True if hint read, False if used default
 */ /**************************************************************************/
IMG_IMPORT IMG_BOOL IMG_CALLCONV PVRSRVGetAppHint(IMG_VOID			*pvHintState,
												  const IMG_CHAR	*pszHintName,
												  IMG_DATA_TYPE		eDataType,
												  const IMG_VOID	*pvDefault,
												  IMG_VOID			*pvReturn);

/******************************************************************************
 * Memory API(s)
 *****************************************************************************/

/* Exported APIs */
/**************************************************************************/ /*!
@Function       PVRSRVAllocUserModeMem
@Description    Allocate a block of user-mode memory
@Input          ui32Size    the amount of memory to allocate
@Return                     On success, a pointer to the memory allocated.
                            Otherwise, NULL.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMem (IMG_SIZE_T ui32Size);

/**************************************************************************/ /*!
@Function       PVRSRVCallocUserModeMem
@Description    Allocate a block of user-mode memory
@Input          ui32Size    the amount of memory to allocate
@Return                     On success, a pointer to the memory allocated.
                            Otherwise, NULL.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMem (IMG_SIZE_T ui32Size);

/**************************************************************************/ /*!
@Function       PVRSRVReallocUserModeMem
@Description    Re-allocate a block of memory
@Input          pvBase      the address of the existing memory, previously
                            allocated with PVRSRVAllocUserModeMem
@Input          uNewSize    the newly-desired size of the memory chunk
@Return                     On success, a pointer to the memory block. If the
                            size of the block could not be changed, the
                            return value is NULL.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMem (IMG_PVOID pvBase, IMG_SIZE_T uNewSize);
/**************************************************************************/ /*!
@Function       PVRSRVFreeUserModeMem
@Description    Free a block of memory previously allocated with
                PVRSRVAllocUserModeMem
@Input          pvMem       pointer to the block of memory to be freed
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMem (IMG_PVOID pvMem);

/**************************************************************************/ /*!
@Function       PVRSRVMemCopy
@Description    Copy a block of memory
                Safe implementation of memset for use with device memory.
@Input          pvDst       Pointer to the destination
@Input          pvSrc       Pointer to the source location
@Input          uiSize      The amount of memory to copy in bytes
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID PVRSRVMemCopy(IMG_VOID *pvDst, const IMG_VOID *pvSrc, IMG_SIZE_T uiSize);

/**************************************************************************/ /*!
@Function       PVRSRVMemSet
@Description    Set all bytes in a region of memory to the specified value.
                Safe implementation of memset for use with device memory.
@Input          pvDest      Pointer to the start of the memory region
@Input          ui8Value    The value to be written
@Input          uiSize      The number of bytes to be set to ui8Value
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID PVRSRVMemSet(IMG_VOID *pvDest, IMG_UINT8 ui8Value, IMG_SIZE_T uiSize);

/**************************************************************************/ /*!
@Function       PVRSRVLockProcessGlobalMutex
@Description    Locking function for non-recursive coarse-grained mutex shared
                between all threads in a proccess.
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockProcessGlobalMutex(void);

/**************************************************************************/ /*!
@Function       PVRSRVUnlockProcessGlobalMutex
@Description    Unlocking function for non-recursive coarse-grained mutex shared
                between all threads in a proccess.
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockProcessGlobalMutex(void);


typedef	struct _OS_MUTEX_ *PVRSRV_MUTEX_HANDLE;

/**************************************************************************/ /*!
@Function       PVRSRVCreateMutex
@Description    creates a mutex
@Output         phMutex             ptr to mutex handle
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
#if !defined(PVR_DEBUG_MUTEXES)
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateMutex(PVRSRV_MUTEX_HANDLE *phMutex);
#else
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateMutex(PVRSRV_MUTEX_HANDLE *phMutex,
													   IMG_CHAR pszMutexName[],
													   IMG_CHAR pszFilename[],
													   IMG_INT iLine);
#define PVRSRVCreateMutex(phMutex) \
	PVRSRVCreateMutex(phMutex, #phMutex, __FILE__, __LINE__)
#endif

/**************************************************************************/ /*!
@Function       PVRSRVDestroyMutex
@Description    Create a mutex.
@Input          hMutex              On success, filled with the new Mutex
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**********************************************************************/
#if !defined(PVR_DEBUG_MUTEXES)
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyMutex(PVRSRV_MUTEX_HANDLE hMutex);
#else
IMG_IMPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyMutex(PVRSRV_MUTEX_HANDLE hMutex,
														IMG_CHAR pszMutexName[],
														IMG_CHAR pszFilename[],
														IMG_INT iLine);
#define PVRSRVDestroyMutex(hMutex) \
	PVRSRVDestroyMutex(hMutex, #hMutex, __FILE__, __LINE__)
#endif

/**************************************************************************/ /*!
@Function       PVRSRVLockMutex
@Description    Lock the mutex passed
@Input          hMutex              handle of the mutex to be locked
@Return         None
 */ /**********************************************************************/
#if !defined(PVR_DEBUG_MUTEXES)
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockMutex(PVRSRV_MUTEX_HANDLE hMutex);
#else
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVLockMutex(PVRSRV_MUTEX_HANDLE hMutex,
												 IMG_CHAR pszMutexName[],
												 IMG_CHAR pszFilename[],
												 IMG_INT iLine);
#define PVRSRVLockMutex(hMutex) \
	PVRSRVLockMutex(hMutex, #hMutex, __FILE__, __LINE__)
#endif

/**************************************************************************/ /*!
@Function       PVRSRVUnlockMutex
@Description    Unlock the mutex passed
@Input          hMutex              handle of the mutex to be unlocked
@Return         None
 */ /**********************************************************************/
#if !defined(PVR_DEBUG_MUTEXES)
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockMutex(PVRSRV_MUTEX_HANDLE hMutex);
#else
IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVUnlockMutex(PVRSRV_MUTEX_HANDLE hMutex,
												   IMG_CHAR pszMutexName[],
												   IMG_CHAR pszFilename[],
												   IMG_INT iLine);
#define PVRSRVUnlockMutex(hMutex) \
	PVRSRVUnlockMutex(hMutex, #hMutex, __FILE__, __LINE__)
#endif

struct _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_;
typedef	struct  _PVRSRV_SEMAPHORE_OPAQUE_STRUCT_ *PVRSRV_SEMAPHORE_HANDLE; /*!< Convenience typedef */


#if defined(_MSC_VER)
    /*! 
      Used when waiting for a semaphore to become unlocked. Indicates that 
      the caller is willing to wait forever.
     */
    #define IMG_SEMAPHORE_WAIT_INFINITE       ((IMG_UINT64)0xFFFFFFFFFFFFFFFF)
#else
    /*! 
      Used when waiting for a semaphore to become unlocked. Indicates that 
      the caller is willing to wait forever.
     */
  	#define IMG_SEMAPHORE_WAIT_INFINITE       ((IMG_UINT64)0xFFFFFFFFFFFFFFFFull)
#endif

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVCreateSemaphore)
#endif
/**************************************************************************/ /*!
@Function       PVRSRVCreateSemaphore
@Description    Create a semaphore with an initial count
@Output         phSemaphore         on success, ptr to the handle of the new 
                                    semaphore. Otherwise, zero.
@Input          iInitialCount       initial count
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
static INLINE PVRSRV_ERROR PVRSRVCreateSemaphore(PVRSRV_SEMAPHORE_HANDLE *phSemaphore, 
                                                IMG_INT iInitialCount)
{
	PVR_UNREFERENCED_PARAMETER(iInitialCount);
	*phSemaphore = 0;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVDestroySemaphore)
#endif
/**************************************************************************/ /*!
@Function       PVRSRVDestroySemaphore
@Description    destroy the semaphore passed
@Input          hSemaphore          the semaphore to be destroyed
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
static INLINE PVRSRV_ERROR PVRSRVDestroySemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVWaitSemaphore)
#endif
/**************************************************************************/ /*!
@Function       PVRSRVWaitSemaphore
@Description    wait on the specified semaphore
@Input          hSemaphore          the semephore on which to wait
@Input          ui64TimeoutMicroSeconds the time to wait for the semaphore to
                                    become unlocked, if locked when the function
                                    is called.
@Return                             PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
 */ /**************************************************************************/
static INLINE PVRSRV_ERROR PVRSRVWaitSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, 
                                            IMG_UINT64 ui64TimeoutMicroSeconds)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(ui64TimeoutMicroSeconds);
	return PVRSRV_ERROR_INVALID_PARAMS;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPostSemaphore)
#endif
/**************************************************************************/ /*!
@Function       PVRSRVPostSemaphore
@Description    post semphore
@Input          hSemaphore      handle to semaphore
@Input          iPostCount      post count
@Return         None
 */ /**************************************************************************/
static INLINE IMG_VOID PVRSRVPostSemaphore(PVRSRV_SEMAPHORE_HANDLE hSemaphore, IMG_INT iPostCount)
{
	PVR_UNREFERENCED_PARAMETER(hSemaphore);
	PVR_UNREFERENCED_PARAMETER(iPostCount);
}

/* Non-exported APIs */
#if defined(DEBUG) && (defined(__linux__) || defined(_WIN32) || defined(__QNXNTO__))
/**************************************************************************/ /*!
@Function       PVRSRVAllocUserModeMemTracking
@Description    Wrapper function for malloc, used for memory-leak detection
@Input          ui32Size            number of bytes to be allocated
@Input          pszFileName         filename of the calling code
@Input          ui32LineNumber      line number of the calling code
@Return                             On success, a ptr to the newly-allocated
                                    memory. Otherwise, NULL.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVAllocUserModeMemTracking(IMG_SIZE_T ui32Size, 
                                                                 IMG_CHAR *pszFileName, 
                                                                 IMG_UINT32 ui32LineNumber);

/**************************************************************************/ /*!
@Function       PVRSRVCallocUserModeMemTracking
@Description    Wrapper function for calloc, used for memory-leak detection
@Input          ui32Size            number of bytes to be allocated
@Input          pszFileName         filename of the calling code
@Input          ui32LineNumber      line number of the calling code
@Return                             On success, a ptr to the newly-allocated
                                    memory. Otherwise, NULL.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVCallocUserModeMemTracking(IMG_SIZE_T ui32Size, 
                                                                  IMG_CHAR *pszFileName, 
                                                                  IMG_UINT32 ui32LineNumber);

/**************************************************************************/ /*!
@Function       PVRSRVFreeUserModeMemTracking
@Description    Wrapper for free - see PVRSRVAllocUserModeMemTracking
@Input          pvMem               pointer to the memory to be freed
@Return         None
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID  IMG_CALLCONV PVRSRVFreeUserModeMemTracking(IMG_VOID *pvMem);

/**************************************************************************/ /*!
@Function       PVRSRVReallocUserModeMemTracking
@Description    Wrapper for realloc, used in memory-leak detection
@Input          pvMem           pointer to the existing memory block
@Input          ui32NewSize     the desired new size of the block
@Input          pszFileName     the filename of the calling code
@Input          ui32LineNumber  the line number of the calling code
@Return                         on success, a pointer to the memory block.
                                This may not necessarily be the same
                                location as the block was at before the
                                call. On failure, NULL is returned.
 */ /**************************************************************************/
IMG_IMPORT IMG_PVOID IMG_CALLCONV PVRSRVReallocUserModeMemTracking(IMG_VOID *pvMem, 
                                                                IMG_SIZE_T ui32NewSize, 
													            IMG_CHAR *pszFileName, 
                                                                IMG_UINT32 ui32LineNumber);
#endif /* defined(DEBUG) && (defined(__linux__) || defined(_WIN32)) */

/**************************************************************************/ /*!
@Function       PVRSRVDumpDebugInfo
@Description    Dump debug information to kernel log
@Input          psConnection    Services connection
@Return         IMG_VOID
 */ /**************************************************************************/
IMG_IMPORT IMG_VOID
PVRSRVDumpDebugInfo(const PVRSRV_CONNECTION *psConnection, IMG_UINT32 ui32VerbLevel);

/**************************************************************************/ /*!
@Function       PVRSRVGetDevClockSpeed
@Description    Gets the RGX clock speed
@Input          psConnection		Services connection
@Input          psDevData			Pointer to the PVRSRV_DEV_DATA context
@Output         pui32RGXClockSpeed  Variable for storing clock speed
@Return         IMG_BOOL			True if the operation was successful
 */ /**************************************************************************/
IMG_IMPORT IMG_BOOL IMG_CALLCONV PVRSRVGetDevClockSpeed(const PVRSRV_CONNECTION *psConnection,
														PVRSRV_DEV_DATA  *psDevData,
														IMG_PUINT32 pui32RGXClockSpeed);

/**************************************************************************/ /*!
@Function       PVRSRVResetHWRLogs
@Description    Resets the HWR Logs buffer (the hardware recovery count is not reset)
@Input          psConnection		Services connection
@Input          psDevData			Pointer to the PVRSRV_DEV_DATA context
@Return         PVRSRV_ERROR		PVRSRV_OK on success. Otherwise, a PVRSRV_
                                	error code
 */ /**************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVResetHWRLogs(const PVRSRV_CONNECTION *psConnection, PVRSRV_DEV_DATA  *psDevData);

/******************************************************************************
 * PVR Event Object API(s)
 *****************************************************************************/

/*****************************************************************************
@Function       PVRSRVAcquireGlobalEventObject
@Description    Gets a handle to an event
 Outputs            : phOSEvent    Global eventobject event
 Returns            : 
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
                      
******************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVAcquireGlobalEventObject(const PVRSRV_CONNECTION *psConnection,
											IMG_HANDLE *phOSEvent);

/*****************************************************************************
 Function Name      : PVRSRVReleaseGlobalEventObject
 Inputs             : phOSEvent    Global eventobject event
 Outputs            : 
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
                      
******************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVReleaseGlobalEventObject(const PVRSRV_CONNECTION *psConnection,
											IMG_HANDLE hOSEvent);

/**************************************************************************/ /*!
@Function       PVRSRVEventObjectWait
@Description    Wait (block) on the OS-specific event object passed
@Input          psConnection    Services connection
@Input          hOSEvent        the event object to wait on
@Return                         PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
 */ /**************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVEventObjectWait(const PVRSRV_CONNECTION *psConnection,
									IMG_HANDLE hOSEvent);


IMG_IMPORT PVRSRV_ERROR
PVRSRVKickDevices(const PVRSRV_CONNECTION *psConnection);


/**************************************************************************/ /*!
@Function       RGXSoftReset
@Description    Resets some modules of the RGX device
@Input          psConnection    Services connection
@Input          psDevData			Pointer to the PVRSRV_DEV_DATA context
@Output         ui64ResetValue  a mask for which each bit set correspond
                                to a module to reset.
@Return         PVRSRV_ERROR
*/ /***************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVSoftReset(const PVRSRV_CONNECTION *psConnection,
				PVRSRV_DEV_DATA  *psDevData,
				IMG_UINT64 ui64ResetValue);

/*!
 Time wrapping macro
*/
#define TIME_NOT_PASSED_UINT32(a,b,c)		(((a) - (b)) < (c))


#if defined (__cplusplus)
}
#endif
#endif /* __SERVICES_H__ */

/******************************************************************************
 End of file (services.h)
******************************************************************************/

