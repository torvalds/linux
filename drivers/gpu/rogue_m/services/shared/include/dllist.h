/*************************************************************************/ /*!
@File
@Title          Double linked list header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Double linked list interface
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

#ifndef _DLLIST_
#define _DLLIST_

#include "img_types.h"

/*!
	Pointer to a linked list node
*/
typedef struct _DLLIST_NODE_	*PDLLIST_NODE;


/*!
	Node in a linked list
*/
/*
 * Note: the following structure's size is architecture-dependent and
 * clients may need to create a mirror the structure definition if it needs
 * to be used in a structure shared between host and device. Consider such
 * clients if any changes are made to this structure.
 */ 
typedef struct _DLLIST_NODE_
{
	struct _DLLIST_NODE_	*psPrevNode;
	struct _DLLIST_NODE_	*psNextNode;
} DLLIST_NODE;


/*!
	Static initialiser
*/
#define DECLARE_DLLIST(n) \
DLLIST_NODE n = {&n, &n}


/*************************************************************************/ /*!
@Function       dllist_init

@Description    Initialize a new double linked list

@Input          psListHead              List head Node

*/
/*****************************************************************************/
static INLINE
IMG_VOID dllist_init(PDLLIST_NODE psListHead)
{
	psListHead->psPrevNode = psListHead;
	psListHead->psNextNode = psListHead;
}


/*************************************************************************/ /*!
@Function       dllist_is_empty

@Description    Returns whether the list is empty

@Input          psListHead              List head Node

*/
/*****************************************************************************/
static INLINE
IMG_BOOL dllist_is_empty(PDLLIST_NODE psListHead)
{
	return ((psListHead->psPrevNode == psListHead) 
				&& (psListHead->psNextNode == psListHead));
}


/*************************************************************************/ /*!
@Function       dllist_add_to_head

@Description    Add psNewNode to head of list psListHead

@Input          psListHead             Head Node
@Input          psNewNode              New Node

*/
/*****************************************************************************/
static INLINE
IMG_VOID dllist_add_to_head(PDLLIST_NODE psListHead, PDLLIST_NODE psNewNode)
{
	PDLLIST_NODE psTmp;

	psTmp = psListHead->psNextNode;

	psListHead->psNextNode = psNewNode;
	psNewNode->psNextNode = psTmp;

	psTmp->psPrevNode = psNewNode;
	psNewNode->psPrevNode = psListHead;
}


/*************************************************************************/ /*!
@Function       dllist_add_to_tail

@Description    Add psNewNode to tail of list psListHead

@Input          psListHead             Head Node
@Input          psNewNode              New Node

*/
/*****************************************************************************/
static INLINE
IMG_VOID dllist_add_to_tail(PDLLIST_NODE psListHead, PDLLIST_NODE psNewNode)
{
	PDLLIST_NODE psTmp;

	psTmp = psListHead->psPrevNode;

	psListHead->psPrevNode = psNewNode;
	psNewNode->psPrevNode = psTmp;

	psTmp->psNextNode = psNewNode;
	psNewNode->psNextNode = psListHead;
}


/*************************************************************************/ /*!
@Function       dllist_node_is_in_list

@Description    Returns IMG_TRUE if psNode is in a list 

@Input          psNode             List node

*/
/*****************************************************************************/
static INLINE
IMG_BOOL dllist_node_is_in_list(PDLLIST_NODE psNode)
{
	return (psNode->psNextNode != 0);
}


/*************************************************************************/ /*!
@Function       dllist_get_next_node

@Description    Returns the list node after psListHead or IMG_NULL psListHead
				is the only element in the list.

@Input          psListHead             List node to start the operation

*/
/*****************************************************************************/
static INLINE
PDLLIST_NODE dllist_get_next_node(PDLLIST_NODE psListHead)
{
	if (psListHead->psNextNode == psListHead)
	{
		return IMG_NULL;
	}
	else
	{
		return psListHead->psNextNode;
	}
} 


/*************************************************************************/ /*!
@Function       dllist_remove_node

@Description    Removes psListNode from the list where it currently belongs

@Input          psListNode             List node to be removed

*/
/*****************************************************************************/
static INLINE
IMG_VOID dllist_remove_node(PDLLIST_NODE psListNode)
{
	psListNode->psNextNode->psPrevNode = psListNode->psPrevNode;
	psListNode->psPrevNode->psNextNode = psListNode->psNextNode;

	/* Clear the node to show it's not on a list */
	psListNode->psPrevNode = 0;
	psListNode->psNextNode = 0;
}


/*!
	Callback function called on each element of the list
*/
typedef IMG_BOOL (*PFN_NODE_CALLBACK)(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData);


/*************************************************************************/ /*!
@Function       dllist_foreach_node

@Description    Walk through all the nodes on the list until the 
				end or a callback returns FALSE

@Input          psListHead			List node to start the operation
@Input			pfnCallBack			PFN_NODE_CALLBACK function called on each element	
@Input			pvCallbackData		Data passed to pfnCallBack alongside the current Node

*/
/*****************************************************************************/
IMG_INTERNAL
IMG_VOID dllist_foreach_node(PDLLIST_NODE psListHead,
							  PFN_NODE_CALLBACK pfnCallBack,
							  IMG_PVOID pvCallbackData);


#endif	/* _DLLIST_ */

