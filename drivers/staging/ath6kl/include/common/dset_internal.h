//------------------------------------------------------------------------------
// <copyright file="dset_internal.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================


#ifndef __DSET_INTERNAL_H__
#define __DSET_INTERNAL_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

/*
 * Internal dset definitions, common for DataSet layer.
 */

#define DSET_TYPE_STANDARD      0
#define DSET_TYPE_BPATCHED      1
#define DSET_TYPE_COMPRESSED    2

/* Dataset descriptor */

typedef PREPACK struct dset_descriptor_s {
  struct dset_descriptor_s  *next;         /* List link. NULL only at the last
                                              descriptor */
  A_UINT16                   id;           /* Dset ID */
  A_UINT16                   size;         /* Dset size. */
  void                      *DataPtr;      /* Pointer to raw data for standard
                                              DataSet or pointer to original
                                              dset_descriptor for patched
                                              DataSet */
  A_UINT32                   data_type;    /* DSET_TYPE_*, above */

  void                      *AuxPtr;       /* Additional data that might
                                              needed for data_type. For
                                              example, pointer to patch
                                              Dataset descriptor for BPatch. */
} POSTPACK dset_descriptor_t;

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __DSET_INTERNAL_H__ */
