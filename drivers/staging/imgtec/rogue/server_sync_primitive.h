/* SPDX-License-Identifier: GPL-2.0 */

/*  --------------------------------------------------------------------------------------------------------
 *  File:   server_sync_primitive.h
 *  --------------------------------------------------------------------------------------------------------
 */


#ifndef __SERVER_SYNC_PRIMITIVE_H__
#define __SERVER_SYNC_PRIMITIVE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */

#include "lock_types.h"
#include "dllist.h"
#include "powervr/sync_external.h"
#include "pvrsrv_device_node.h"

/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition
 * ---------------------------------------------------------------------------------------------------------
 */

/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */

struct _SERVER_SYNC_PRIMITIVE_ {
	PVRSRV_DEVICE_NODE		*psDevNode;
	PVRSRV_CLIENT_SYNC_PRIM *psSync;
	IMG_UINT32				ui32NextOp;
	IMG_UINT32				ui32RefCount;
	IMG_UINT32				ui32UID;
	IMG_UINT32				ui32LastSyncRequesterID;
	DLLIST_NODE				sNode;
	/* PDump only data */
	IMG_BOOL				bSWOperation;
	IMG_BOOL				bSWOpStartedInCaptRange;
	IMG_UINT32				ui32LastHWUpdate;
	IMG_BOOL				bPDumped;
	POS_LOCK				hLock;
	IMG_CHAR				szClassName[SYNC_MAX_CLASS_NAME_LEN];
};
typedef struct _SERVER_SYNC_PRIMITIVE_ SERVER_SYNC_PRIMITIVE;

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

#endif /* __SERVER_SYNC_PRIMITIVE_H__ */

