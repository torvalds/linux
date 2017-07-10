/**************************************************************************/ /*!
@File
@Title          PowerVR notifier interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
*/ /***************************************************************************/

#if !defined(__PVR_NOTIFIER_H__)
#define __PVR_NOTIFIER_H__

#include "img_types.h"
#include "pvr_debug.h"


/**************************************************************************/ /*!
Command Complete Notifier Interface
*/ /***************************************************************************/

typedef IMG_HANDLE PVRSRV_CMDCOMP_HANDLE;
typedef void (*PFN_CMDCOMP_NOTIFY)(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle);

/**************************************************************************/ /*!
@Function       PVRSRVCmdCompleteInit
@Description    Performs initialisation of the command complete notifier
                interface.
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVCmdCompleteInit(void);

/**************************************************************************/ /*!
@Function       PVRSRVCmdCompleteDeinit
@Description    Performs cleanup for the command complete notifier interface.
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
void
PVRSRVCmdCompleteDeinit(void);

/**************************************************************************/ /*!
@Function       PVRSRVRegisterCmdCompleteNotify
@Description    Register a callback function that is called when some device
                finishes some work, which is signalled via a call to
                PVRSRVCheckStatus.
@Output         phNotify             On success, points to command complete
                                     notifier handle
@Input          pfnCmdCompleteNotify Function callback
@Input          hPrivData            Data to be passed back to the caller via
                                     the callback function
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRegisterCmdCompleteNotify(IMG_HANDLE *phNotify,
								PFN_CMDCOMP_NOTIFY pfnCmdCompleteNotify,
								PVRSRV_CMDCOMP_HANDLE hPrivData);

/**************************************************************************/ /*!
@Function       PVRSRVUnregisterCmdCompleteNotify
@Description    Unregister a previously registered callback function.
@Input          hNotify              Command complete notifier handle
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVUnregisterCmdCompleteNotify(IMG_HANDLE hNotify);

/**************************************************************************/ /*!
@Function       PVRSRVCheckStatus
@Description    Notify any registered command complete handlers that some work
                has been finished (unless hCmdCompCallerHandle matches a
                handler's hPrivData). Also signal the global event object.
@Input          hCmdCompCallerHandle Used to prevent a handler from being
                                     notified. A NULL value results in all
									 handlers being notified.
*/ /***************************************************************************/
void
PVRSRVCheckStatus(PVRSRV_CMDCOMP_HANDLE hCmdCompCallerHandle);


/**************************************************************************/ /*!
Debug Notifier Interface
*/ /***************************************************************************/

#define DEBUG_REQUEST_DC                0
#define DEBUG_REQUEST_SERVERSYNC        1
#define DEBUG_REQUEST_SYS               2
#define DEBUG_REQUEST_ANDROIDSYNC       3
#define DEBUG_REQUEST_LINUXFENCE        4
#define DEBUG_REQUEST_SYNCCHECKPOINT    5
#define DEBUG_REQUEST_HTB               6
#define DEBUG_REQUEST_APPHINT           7

#define DEBUG_REQUEST_VERBOSITY_LOW		0
#define DEBUG_REQUEST_VERBOSITY_MEDIUM	1
#define DEBUG_REQUEST_VERBOSITY_HIGH	2
#define DEBUG_REQUEST_VERBOSITY_MAX		DEBUG_REQUEST_VERBOSITY_HIGH

/*
 * Macro used within debug dump functions to send output either to PVR_LOG or
 * a custom function. The custom function should be stored as a function pointer
 * in a local variable called 'pfnDumpDebugPrintf'. 'pvDumpDebugFile' is also
 * required as a local variable to serve as a file identifier for the printf
 * function if required.
 */
#define PVR_DUMPDEBUG_LOG(...)                                            \
	do                                                                \
	{                                                                 \
		if (pfnDumpDebugPrintf)                                   \
			pfnDumpDebugPrintf(pvDumpDebugFile, __VA_ARGS__); \
		else                                                      \
			PVR_LOG((__VA_ARGS__));                           \
	} while(0)

struct _PVRSRV_DEVICE_NODE_;

typedef IMG_HANDLE PVRSRV_DBGREQ_HANDLE;
typedef void (DUMPDEBUG_PRINTF_FUNC)(void *pvDumpDebugFile,
					const IMG_CHAR *pszFormat, ...);
typedef void (*PFN_DBGREQ_NOTIFY)(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
					IMG_UINT32 ui32VerbLevel,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile);


/**************************************************************************/ /*!
@Function       PVRSRVRegisterDbgTable
@Description    Registers a debug requester table for the given device. The
                order in which the debug requester IDs appear in the given
                table determine the order in which a set of notifier callbacks
                will be called. In other words, the requester ID that appears
                first will have all of its associated debug notifier callbacks
                called first. This will then be followed by all the callbacks
                associated with the next requester ID in the table and so on.
@Input          psDevNode     Device node with which to register requester table
@Input          paui32Table   Array of requester IDs
@Input          ui32Length    Number of elements in paui32Table
@Return         PVRSRV_ERROR  PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRegisterDbgTable(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
					   IMG_UINT32 *paui32Table, IMG_UINT32 ui32Length);

/**************************************************************************/ /*!
@Function       PVRSRVUnregisterDbgTable
@Description    Unregisters a debug requester table.
@Input          psDevNode     Device node for which the requester table should
                              be unregistered
@Return         void
*/ /***************************************************************************/
void
PVRSRVUnregisterDbgTable(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

/**************************************************************************/ /*!
@Function       PVRSRVRegisterDbgRequestNotify
@Description    Register a callback function that is called when a debug request
                is made via a call PVRSRVDebugRequest. There are a number of
				verbosity levels ranging from DEBUG_REQUEST_VERBOSITY_LOW up to
                DEBUG_REQUEST_VERBOSITY_MAX. The callback will be called once
                for each level up to the highest level specified to
                PVRSRVDebugRequest.
@Output         phNotify             On success, points to debug notifier handle
@Input          psDevNode            Device node for which the debug callback
                                     should be registered
@Input          pfnDbgRequestNotify  Function callback
@Input          ui32RequesterID      Requester ID. This is used to determine
                                     the order in which callbacks are called
@Input          hDbgReqeustHandle    Data to be passed back to the caller via
                                     the callback function
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVRegisterDbgRequestNotify(IMG_HANDLE *phNotify,
							   struct _PVRSRV_DEVICE_NODE_ *psDevNode,
							   PFN_DBGREQ_NOTIFY pfnDbgRequestNotify,
							   IMG_UINT32 ui32RequesterID,
							   PVRSRV_DBGREQ_HANDLE hDbgReqeustHandle);

/**************************************************************************/ /*!
@Function       PVRSRVUnregisterDbgRequestNotify
@Description    Unregister a previously registered callback function.
@Input          hNotify              Debug notifier handle.
@Return         PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVUnregisterDbgRequestNotify(IMG_HANDLE hNotify);

/**************************************************************************/ /*!
@Function       PVRSRVDebugRequest
@Description    Notify any registered debug request handlers that a debug
                request has been made and at what level.
@Input          psDevNode           Device node for which the debug request has
                                    been made
@Input          ui32VerbLevel       The maximum verbosity level to dump
@Input          pfnDumpDebugPrintf  Used to specify the print function that
                                    should be used to dump any debug
                                    information. If this argument is NULL then
                                    PVR_LOG() will be used as the default print
                                    function.
@Input          pvDumpDebugFile     Optional file identifier to be passed to
                                    the print function if required.
@Return         void
*/ /***************************************************************************/
void
PVRSRVDebugRequest(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
				   IMG_UINT32 ui32VerbLevel,
				   DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				   void *pvDumpDebugFile);

#endif /* !defined(__PVR_NOTIFIER_H__) */
