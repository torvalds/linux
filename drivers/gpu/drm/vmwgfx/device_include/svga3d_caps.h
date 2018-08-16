/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2007-2015 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * svga3d_caps.h --
 *
 *       Definitions for SVGA3D hardware capabilities.  Capabilities
 *       are used to query for optional rendering features during
 *       driver initialization. The capability data is stored as very
 *       basic key/value dictionary within the "FIFO register" memory
 *       area at the beginning of BAR2.
 *
 *       Note that these definitions are only for 3D capabilities.
 *       The SVGA device also has "device capabilities" and "FIFO
 *       capabilities", which are non-3D-specific and are stored as
 *       bitfields rather than key/value pairs.
 */

#ifndef _SVGA3D_CAPS_H_
#define _SVGA3D_CAPS_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL

#include "includeCheck.h"

#include "svga_reg.h"

#define SVGA_FIFO_3D_CAPS_SIZE   (SVGA_FIFO_3D_CAPS_LAST - \
                                  SVGA_FIFO_3D_CAPS + 1)


/*
 * SVGA3dCapsRecordType
 *
 *    Record types that can be found in the caps block.
 *    Related record types are grouped together numerically so that
 *    SVGA3dCaps_FindRecord() can be applied on a range of record
 *    types.
 */

typedef enum {
   SVGA3DCAPS_RECORD_UNKNOWN        = 0,
   SVGA3DCAPS_RECORD_DEVCAPS_MIN    = 0x100,
   SVGA3DCAPS_RECORD_DEVCAPS        = 0x100,
   SVGA3DCAPS_RECORD_DEVCAPS_MAX    = 0x1ff,
} SVGA3dCapsRecordType;


/*
 * SVGA3dCapsRecordHeader
 *
 *    Header field leading each caps block record. Contains the offset (in
 *    register words, NOT bytes) to the next caps block record (or the end
 *    of caps block records which will be a zero word) and the record type
 *    as defined above.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCapsRecordHeader {
   uint32 length;
   SVGA3dCapsRecordType type;
}
#include "vmware_pack_end.h"
SVGA3dCapsRecordHeader;


/*
 * SVGA3dCapsRecord
 *
 *    Caps block record; "data" is a placeholder for the actual data structure
 *    contained within the record;
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCapsRecord {
   SVGA3dCapsRecordHeader header;
   uint32 data[1];
}
#include "vmware_pack_end.h"
SVGA3dCapsRecord;


typedef uint32 SVGA3dCapPair[2];

#endif
