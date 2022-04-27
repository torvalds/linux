/*************************************************************************/ /*!
@File
@Title          Splay trees interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides debug functionality
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

#ifndef UNIQ_KEY_SPLAY_TREE_H_
#define UNIQ_KEY_SPLAY_TREE_H_

#include "img_types.h"
#include "pvr_intrinsics.h"

#if defined(PVR_CTZLL)
  /* map the is_bucket_n_free to an int.
   * This way, the driver can find the first non empty without loop
   */
  typedef IMG_UINT64 IMG_ELTS_MAPPINGS;
#endif

typedef IMG_UINT64 IMG_PSPLAY_FLAGS_T;

/* head of list of free boundary tags for indexed by pvr_log2 of the
   boundary tag size */

#define FREE_TABLE_LIMIT 40

struct _BT_;

typedef struct img_splay_tree
{
	/* left child/subtree */
    struct img_splay_tree * psLeft;

	/* right child/subtree */
    struct img_splay_tree * psRight;

    /* Flags to match on this span, used as the key. */
    IMG_PSPLAY_FLAGS_T uiFlags;
#if defined(PVR_CTZLL)
	/* each bit of this int is a boolean telling if the corresponding
	   bucket is empty or not */
    IMG_ELTS_MAPPINGS bHasEltsMapping;
#endif
	struct _BT_ * buckets[FREE_TABLE_LIMIT];
} IMG_SPLAY_TREE, *IMG_PSPLAY_TREE;

IMG_PSPLAY_TREE PVRSRVSplay (IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree);
IMG_PSPLAY_TREE PVRSRVInsert(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree);
IMG_PSPLAY_TREE PVRSRVDelete(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree);
IMG_PSPLAY_TREE PVRSRVFindNode(IMG_PSPLAY_FLAGS_T uiFlags, IMG_PSPLAY_TREE psTree);


#endif /* !UNIQ_KEY_SPLAY_TREE_H_ */
