/*************************************************************************/ /*!
@File
@Title          Provides splay-trees.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implementation of splay-trees.
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

#include "allocmem.h" /* for OSMemAlloc / OSMemFree */
#include "osfunc.h" /* for OSMemFree */
#include "pvr_debug.h"
#include "uniq_key_splay_tree.h"

/**
 * This function performs a simple top down splay
 *
 * @param uiFlags the flags that must splayed to the root (if possible).
 * @param psTree The tree to splay.
 * @return the resulting tree after the splay operation.
 */
IMG_INTERNAL
IMG_PSPLAY_TREE PVRSRVSplay (IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree)
{
	IMG_SPLAY_TREE sTmp1;
	IMG_PSPLAY_TREE psLeft;
	IMG_PSPLAY_TREE psRight;
	IMG_PSPLAY_TREE psTmp2;

	if (psTree == NULL)
	{
		return NULL;
	}

	sTmp1.psLeft = NULL;
	sTmp1.psRight = NULL;

	psLeft = &sTmp1;
	psRight = &sTmp1;

	for (;;)
	{
		if (uiFlags < psTree->uiFlags)
		{
			if (psTree->psLeft == NULL)
			{
				break;
			}

			if (uiFlags < psTree->psLeft->uiFlags)
			{
				/* if we get to this point, we need to rotate right the tree */
				psTmp2 = psTree->psLeft;
				psTree->psLeft = psTmp2->psRight;
				psTmp2->psRight = psTree;
				psTree = psTmp2;
				if (psTree->psLeft == NULL)
				{
					break;
				}
			}

			/* if we get to this point, we need to link right */
			psRight->psLeft = psTree;
			psRight = psTree;
			psTree = psTree->psLeft;
		}
		else
		{
			if (uiFlags > psTree->uiFlags)
			{
				if (psTree->psRight == NULL)
				{
					break;
				}

				if (uiFlags > psTree->psRight->uiFlags)
				{
					/* if we get to this point, we need to rotate left the tree */
					psTmp2 = psTree->psRight;
					psTree->psRight = psTmp2->psLeft;
					psTmp2->psLeft = psTree;
					psTree = psTmp2;
					if (psTree->psRight == NULL)
					{
						break;
					}
				}

				/* if we get to this point, we need to link left */
				psLeft->psRight = psTree;
				psLeft = psTree;
				psTree = psTree->psRight;
			}
			else
			{
				break;
			}
		}
	}

	/* at this point re-assemble the tree */
	psLeft->psRight = psTree->psLeft;
	psRight->psLeft = psTree->psRight;
	psTree->psLeft = sTmp1.psRight;
	psTree->psRight = sTmp1.psLeft;
	return psTree;
}


/**
 * This function inserts a node into the Tree (unless it is already present, in
 * which case it is equivalent to performing only a splay operation
 *
 * @param uiFlags the key of the new node
 * @param psTree The tree into which one wants to add a new node
 * @return The resulting with the node in it
 */
IMG_INTERNAL
IMG_PSPLAY_TREE PVRSRVInsert(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree)
{
	IMG_PSPLAY_TREE psNew;

	if (psTree != NULL)
	{
		psTree = PVRSRVSplay(uiFlags, psTree);
		if (psTree->uiFlags == uiFlags)
		{
			return psTree;
		}
	}

	psNew = (IMG_PSPLAY_TREE) OSAllocMem(sizeof(IMG_SPLAY_TREE));
	if (psNew == NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR, "Error: failed to allocate memory to add a node to the splay tree."));
		return NULL;
	}

	psNew->uiFlags = uiFlags;
	OSCachedMemSet(&(psNew->buckets[0]), 0, sizeof(psNew->buckets));

#if defined(PVR_CTZLL)
	psNew->bHasEltsMapping = ~(((IMG_ELTS_MAPPINGS) 1 << (sizeof(psNew->buckets) / (sizeof(psNew->buckets[0])))) - 1);
#endif

	if (psTree == NULL)
	{
		psNew->psLeft  = NULL;
		psNew->psRight = NULL;
		return psNew;
	}

	if (uiFlags < psTree->uiFlags)
	{
		psNew->psLeft  = psTree->psLeft;
		psNew->psRight = psTree;
		psTree->psLeft = NULL;
	}
	else
	{
		psNew->psRight  = psTree->psRight;
		psNew->psLeft   = psTree;
		psTree->psRight = NULL;
	}

	return psNew;
}


/**
 * Deletes a node from the tree (unless it is not there, in which case it is
 * equivalent to a splay operation)
 *
 * @param uiFlags the value of the node to remove
 * @param psTree the tree into which the node must be removed
 * @return the resulting tree
 */
IMG_INTERNAL
IMG_PSPLAY_TREE PVRSRVDelete(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree)
{
	IMG_PSPLAY_TREE psTmp;
	if (psTree == NULL)
	{
		return NULL;
	}

	psTree = PVRSRVSplay(uiFlags, psTree);
	if (uiFlags == psTree->uiFlags)
	{
		/* The value was present in the tree */
		if (psTree->psLeft == NULL)
		{
			psTmp = psTree->psRight;
		}
		else
		{
			psTmp = PVRSRVSplay(uiFlags, psTree->psLeft);
			psTmp->psRight = psTree->psRight;
		}
		OSFreeMem(psTree);
		return psTmp;
	}

	/* The value was not present in the tree, so just return it as is
	 * (after the splay) */
	return psTree;
}

/**
 * This function picks up the appropriate node for the given flags
 *
 * @param uiFlags the flags that must associated with the node.
 * @param psTree current splay tree node.
 * @return the resulting tree node after the search operation.
 */
IMG_INTERNAL
IMG_PSPLAY_TREE PVRSRVFindNode(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree)
{
	if (psTree == NULL)
	{
		return NULL;
	}

	while (psTree)
	{
		if (uiFlags == psTree->uiFlags)
		{
			return psTree;
		}

		if (uiFlags < psTree->uiFlags)
		{
			psTree = psTree->psLeft;
			continue;
		}

		if (uiFlags > psTree->uiFlags)
		{
			psTree = psTree->psRight;
			continue;
		}
	}

	return NULL;
}
