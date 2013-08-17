/*************************************************************************/ /*!
@Title          Linked list shared functions templates.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Definition of the linked list function templates.
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

#ifndef __LISTS_UTILS__
#define __LISTS_UTILS__

/* instruct QAC to ignore warnings about the following custom formatted macros */
/* PRQA S 0881,3410 ++ */
#include <stdarg.h>
#include "img_types.h"

/*
 - USAGE -

 The list functions work with any structure that provides the fields psNext and
 ppsThis. In order to make a function available for a given type, it is required
 to use the funcion template macro that creates the actual code.

 There are 4 main types of functions:
 - INSERT	: given a pointer to the head pointer of the list and a pointer to
 			  the node, inserts it as the new head.
 - REMOVE	: given a pointer to a node, removes it from its list.
 - FOR EACH	: apply a function over all the elements of a list.
 - ANY		: apply a function over the elements of a list, until one of them
 			  return a non null value, and then returns it.

 The two last functions can have a variable argument form, with allows to pass
 additional parameters to the callback function. In order to do this, the
 callback function must take two arguments, the first is the current node and
 the second is a list of variable arguments (va_list).

 The ANY functions have also another for wich specifies the return type of the
 callback function and the default value returned by the callback function.

*/

/*!
******************************************************************************
    @Function       List_##TYPE##_ForEach

    @Description    Apply a callback function to all the elements of a list.

    @Input          psHead - the head of the list to be processed.
    @Input          pfnCallBack - the function to be applied to each element 
                        of the list.

    @Return         None
******************************************************************************/
#define DECLARE_LIST_FOR_EACH(TYPE) \
IMG_VOID List_##TYPE##_ForEach(TYPE *psHead, IMG_VOID(*pfnCallBack)(TYPE* psNode))

#define IMPLEMENT_LIST_FOR_EACH(TYPE) \
IMG_VOID List_##TYPE##_ForEach(TYPE *psHead, IMG_VOID(*pfnCallBack)(TYPE* psNode))\
{\
	while(psHead)\
	{\
		pfnCallBack(psHead);\
		psHead = psHead->psNext;\
	}\
}


#define DECLARE_LIST_FOR_EACH_VA(TYPE) \
IMG_VOID List_##TYPE##_ForEach_va(TYPE *psHead, IMG_VOID(*pfnCallBack)(TYPE* psNode, va_list va), ...)

#define IMPLEMENT_LIST_FOR_EACH_VA(TYPE) \
IMG_VOID List_##TYPE##_ForEach_va(TYPE *psHead, IMG_VOID(*pfnCallBack)(TYPE* psNode, va_list va), ...) \
{\
	va_list ap;\
	while(psHead)\
	{\
		va_start(ap, pfnCallBack);\
		pfnCallBack(psHead, ap);\
		psHead = psHead->psNext;\
		va_end(ap);\
	}\
}


/*!
******************************************************************************
    @Function       List_##TYPE##_Any

    @Description    Applies a callback function to the elements of a list until 
                    the function returns a non null value, then returns it.

    @Input          psHead - the head of the list to be processed.
    @Input          pfnCallBack - the function to be applied to each element 
                    of the list.

    @Return         None
******************************************************************************/
#define DECLARE_LIST_ANY(TYPE) \
IMG_VOID* List_##TYPE##_Any(TYPE *psHead, IMG_VOID* (*pfnCallBack)(TYPE* psNode))

#define IMPLEMENT_LIST_ANY(TYPE) \
IMG_VOID* List_##TYPE##_Any(TYPE *psHead, IMG_VOID* (*pfnCallBack)(TYPE* psNode))\
{ \
	IMG_VOID *pResult;\
	TYPE *psNextNode;\
	pResult = IMG_NULL;\
	psNextNode = psHead;\
	while(psHead && !pResult)\
	{\
		psNextNode = psNextNode->psNext;\
		pResult = pfnCallBack(psHead);\
		psHead = psNextNode;\
	}\
	return pResult;\
}


/*with variable arguments, that will be passed as a va_list to the callback function*/

#define DECLARE_LIST_ANY_VA(TYPE) \
IMG_VOID* List_##TYPE##_Any_va(TYPE *psHead, IMG_VOID*(*pfnCallBack)(TYPE* psNode, va_list va), ...)

#define IMPLEMENT_LIST_ANY_VA(TYPE) \
IMG_VOID* List_##TYPE##_Any_va(TYPE *psHead, IMG_VOID*(*pfnCallBack)(TYPE* psNode, va_list va), ...)\
{\
	va_list ap;\
	TYPE *psNextNode;\
	IMG_VOID* pResult = IMG_NULL;\
	while(psHead && !pResult)\
	{\
		psNextNode = psHead->psNext;\
		va_start(ap, pfnCallBack);\
		pResult = pfnCallBack(psHead, ap);\
		va_end(ap);\
		psHead = psNextNode;\
	}\
	return pResult;\
}

/*those ones are for extra type safety, so there's no need to use castings for the results*/

#define DECLARE_LIST_ANY_2(TYPE, RTYPE, CONTINUE) \
RTYPE List_##TYPE##_##RTYPE##_Any(TYPE *psHead, RTYPE (*pfnCallBack)(TYPE* psNode))

#define IMPLEMENT_LIST_ANY_2(TYPE, RTYPE, CONTINUE) \
RTYPE List_##TYPE##_##RTYPE##_Any(TYPE *psHead, RTYPE (*pfnCallBack)(TYPE* psNode))\
{ \
	RTYPE result;\
	TYPE *psNextNode;\
	result = CONTINUE;\
	psNextNode = psHead;\
	while(psHead && result == CONTINUE)\
	{\
		psNextNode = psNextNode->psNext;\
		result = pfnCallBack(psHead);\
		psHead = psNextNode;\
	}\
	return result;\
}


#define DECLARE_LIST_ANY_VA_2(TYPE, RTYPE, CONTINUE) \
RTYPE List_##TYPE##_##RTYPE##_Any_va(TYPE *psHead, RTYPE(*pfnCallBack)(TYPE* psNode, va_list va), ...)

#define IMPLEMENT_LIST_ANY_VA_2(TYPE, RTYPE, CONTINUE) \
RTYPE List_##TYPE##_##RTYPE##_Any_va(TYPE *psHead, RTYPE(*pfnCallBack)(TYPE* psNode, va_list va), ...)\
{\
	va_list ap;\
	TYPE *psNextNode;\
	RTYPE result = CONTINUE;\
	while(psHead && result == CONTINUE)\
	{\
		psNextNode = psHead->psNext;\
		va_start(ap, pfnCallBack);\
		result = pfnCallBack(psHead, ap);\
		va_end(ap);\
		psHead = psNextNode;\
	}\
	return result;\
}


/*!
******************************************************************************
    @Function       List_##TYPE##_Remove

    @Description    Removes a given node from the list.

    @Input          psNode - the pointer to the node to be removed.

    @Return         None
******************************************************************************/
#define DECLARE_LIST_REMOVE(TYPE) \
IMG_VOID List_##TYPE##_Remove(TYPE *psNode)

#define IMPLEMENT_LIST_REMOVE(TYPE) \
IMG_VOID List_##TYPE##_Remove(TYPE *psNode)\
{\
	(*psNode->ppsThis)=psNode->psNext;\
	if(psNode->psNext)\
	{\
		psNode->psNext->ppsThis = psNode->ppsThis;\
	}\
}

/*!
******************************************************************************
    @Function       List_##TYPE##_Insert

    @Description    Inserts a given node at the beginnning of the list.

    @Input          psHead - The pointer to the pointer to the head node.
    @Input          psNode - The pointer to the node to be inserted.

    @Return         None
******************************************************************************/
#define DECLARE_LIST_INSERT(TYPE) \
IMG_VOID List_##TYPE##_Insert(TYPE **ppsHead, TYPE *psNewNode)

#define IMPLEMENT_LIST_INSERT(TYPE) \
IMG_VOID List_##TYPE##_Insert(TYPE **ppsHead, TYPE *psNewNode)\
{\
	psNewNode->ppsThis = ppsHead;\
	psNewNode->psNext = *ppsHead;\
	*ppsHead = psNewNode;\
	if(psNewNode->psNext)\
	{\
		psNewNode->psNext->ppsThis = &(psNewNode->psNext);\
	}\
}

/*!
******************************************************************************
    @Function       List_##TYPE##_Reverse

    @Description    Reverse a list in place

    @Input          ppsHead - The pointer to the pointer to the head node.
					
    @Return         None
******************************************************************************/
#define DECLARE_LIST_REVERSE(TYPE) \
IMG_VOID List_##TYPE##_Reverse(TYPE **ppsHead)

#define IMPLEMENT_LIST_REVERSE(TYPE) \
IMG_VOID List_##TYPE##_Reverse(TYPE **ppsHead)\
{\
    TYPE *psTmpNode1; \
    TYPE *psTmpNode2; \
    TYPE *psCurNode; \
	psTmpNode1 = IMG_NULL; \
	psCurNode = *ppsHead; \
	while(psCurNode) { \
    	psTmpNode2 = psCurNode->psNext; \
        psCurNode->psNext = psTmpNode1; \
		psTmpNode1 = psCurNode; \
		psCurNode = psTmpNode2; \
		if(psCurNode) \
		{ \
			psTmpNode1->ppsThis = &(psCurNode->psNext); \
		} \
		else \
		{ \
			psTmpNode1->ppsThis = ppsHead;		\
		} \
	} \
	*ppsHead = psTmpNode1; \
}

#define IS_LAST_ELEMENT(x) ((x)->psNext == IMG_NULL)

#include "services_headers.h"

DECLARE_LIST_ANY_VA(BM_HEAP);
DECLARE_LIST_ANY_2(BM_HEAP, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_ANY_VA_2(BM_HEAP, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_FOR_EACH_VA(BM_HEAP);
DECLARE_LIST_REMOVE(BM_HEAP);
DECLARE_LIST_INSERT(BM_HEAP);

DECLARE_LIST_ANY_VA(BM_CONTEXT);
DECLARE_LIST_ANY_VA_2(BM_CONTEXT, IMG_HANDLE, IMG_NULL);
DECLARE_LIST_ANY_VA_2(BM_CONTEXT, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_FOR_EACH(BM_CONTEXT);
DECLARE_LIST_REMOVE(BM_CONTEXT);
DECLARE_LIST_INSERT(BM_CONTEXT);

DECLARE_LIST_ANY_2(PVRSRV_DEVICE_NODE, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_ANY_VA(PVRSRV_DEVICE_NODE);
DECLARE_LIST_ANY_VA_2(PVRSRV_DEVICE_NODE, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_FOR_EACH(PVRSRV_DEVICE_NODE);
DECLARE_LIST_FOR_EACH_VA(PVRSRV_DEVICE_NODE);
DECLARE_LIST_INSERT(PVRSRV_DEVICE_NODE);
DECLARE_LIST_REMOVE(PVRSRV_DEVICE_NODE);

DECLARE_LIST_ANY_VA(PVRSRV_POWER_DEV);
DECLARE_LIST_ANY_VA_2(PVRSRV_POWER_DEV, PVRSRV_ERROR, PVRSRV_OK);
DECLARE_LIST_INSERT(PVRSRV_POWER_DEV);
DECLARE_LIST_REMOVE(PVRSRV_POWER_DEV);

#undef DECLARE_LIST_ANY_2
#undef DECLARE_LIST_ANY_VA
#undef DECLARE_LIST_ANY_VA_2
#undef DECLARE_LIST_FOR_EACH
#undef DECLARE_LIST_FOR_EACH_VA
#undef DECLARE_LIST_INSERT
#undef DECLARE_LIST_REMOVE

IMG_VOID* MatchDeviceKM_AnyVaCb(PVRSRV_DEVICE_NODE* psDeviceNode, va_list va);
IMG_VOID* MatchPowerDeviceIndex_AnyVaCb(PVRSRV_POWER_DEV *psPowerDev, va_list va);

#endif

/* re-enable warnings */
/* PRQA S 0881,3410 -- */
