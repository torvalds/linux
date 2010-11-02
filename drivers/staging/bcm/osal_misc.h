	/*++

	Copyright (c) Beceem Communications Inc.

	Module Name:
		OSAL_Misc.h

	Abstract:
		Provides the OS Abstracted macros to access:
			Linked Lists
			Dispatcher Objects(Events,Semaphores,Spin Locks and the like)
			Files


	Revision History:
		Who         When        What
		--------    --------    ----------------------------------------------
		Name		Date		Created/reviewed/modified
		Rajeev		24/1/08		Created
	Notes:

	--*/
#ifndef _OSAL_MISC_H_
#define _OSAL_MISC_H_
//OSAL Macros
//OSAL Primitives
typedef PUCHAR  POSAL_NW_PACKET  ;		//Nw packets


#define OsalMemAlloc(n,t) kmalloc(n,GFP_KERNEL)

#define OsalMemFree(x,n) bcm_kfree(x)

#define OsalMemMove(dest, src, len)		\
{										\
			memcpy(dest,src, len);		\
}

#define OsalZeroMemory(pDest, Len)		\
{										\
			memset(pDest,0,Len);		\
}

//#define OsalMemSet(pSrc,Char,Len) memset(pSrc,Char,Len)

bool OsalMemCompare(void *dest, void *src, UINT len);

#endif

