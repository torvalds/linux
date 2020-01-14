/* SPDX-License-Identifier: GPL-2.0 */

/*  --------------------------------------------------------------------------------------------------------
 *  File:   connection_data.h
 *  --------------------------------------------------------------------------------------------------------
 */

#ifndef __CONNECTION_DATA_H__
#define __CONNECTION_DATA_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */
// #include <linux/kernel.h>

#include "handle.h"
#include "img_types.h"
#include "pvrsrv_cleanup.h"


/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */
typedef struct _CONNECTION_DATA_ {
	PVRSRV_HANDLE_BASE		*psHandleBase;
	PROCESS_HANDLE_BASE		*psProcessHandleBase;
	struct _SYNC_CONNECTION_DATA_	*psSyncConnectionData;
	struct _PDUMP_CONNECTION_DATA_	*psPDumpConnectionData;

	/* Holds the client flags supplied at connection time */
	IMG_UINT32			ui32ClientFlags;

	/*
	 * OS specific data can be stored via this handle.
	 * See osconnection_server.h for a generic mechanism
	 * for initialising this field.
	 */
	IMG_HANDLE			hOsPrivateData;

	IMG_PID				pid;

	void				*hSecureData;

	IMG_HANDLE			hProcessStats;

	IMG_HANDLE			hClientTLStream;

	/* Structure which is hooked into the cleanup thread work list */
	PVRSRV_CLEANUP_THREAD_WORK sCleanupThreadFn;

	/* List navigation for deferred freeing of connection data */
	struct _CONNECTION_DATA_	**ppsThis;
	struct _CONNECTION_DATA_	*psNext;
} CONNECTION_DATA;

/* ---------------------------------------------------------------------------------------------------------
 *  Global Functions' Prototype
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Inline Functions Implementation
 * ---------------------------------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* __CONNECTION_DATA_H__ */

