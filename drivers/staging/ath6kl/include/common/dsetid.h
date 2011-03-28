//------------------------------------------------------------------------------
// <copyright file="dsetid.h" company="Atheros">
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


#ifndef __DSETID_H__
#define __DSETID_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

/* Well-known DataSet IDs */
#define DSETID_UNUSED                     0x00000000
#define DSETID_BOARD_DATA                 0x00000001 /* Cal and board data */
#define DSETID_REGDB                      0x00000002 /* Regulatory Database */
#define DSETID_POWER_CONTROL              0x00000003 /* TX Pwr Lim & Ant Gain */
#define DSETID_USER_CONFIG                0x00000004 /* User Configuration */

#define DSETID_ANALOG_CONTROL_DATA_START  0x00000005
#define DSETID_ANALOG_CONTROL_DATA_END    0x00000025
/*
 * Get DSETID for various reference clock speeds.
 * For each speed there are three DataSets that correspond
 * to the three columns of bank6 data (addr, 11a, 11b/g).
 * This macro returns the dsetid of the first of those
 * three DataSets.
 */
#define ANALOG_CONTROL_DATA_DSETID(refclk) \
        (DSETID_ANALOG_CONTROL_DATA_START + 3*refclk)

/*
 * There are TWO STARTUP_PATCH DataSets.
 * DSETID_STARTUP_PATCH is historical, and was applied before BMI on
 * earlier systems.  On AR6002, it is applied after BMI, just like
 * DSETID_STARTUP_PATCH2.
 */
#define DSETID_STARTUP_PATCH              0x00000026
#define DSETID_GPIO_CONFIG_PATCH          0x00000027
#define DSETID_WLANREGS                   0x00000028 /* override wlan regs */
#define DSETID_STARTUP_PATCH2             0x00000029

#define DSETID_WOW_CONFIG                 0x00000090 /* WoW Configuration */

/* Add WHAL_INI_DATA_ID to DSETID_INI_DATA for a specific WHAL INI table. */
#define DSETID_INI_DATA                   0x00000100
/* Reserved for WHAL INI Tables: 0x100..0x11f */
#define DSETID_INI_DATA_END               0x0000011f

#define DSETID_VENDOR_START               0x00010000 /* Vendor-defined DataSets */

#define DSETID_INDEX_END                  0xfffffffe /* Reserved to indicate the
                                                        end of a memory-based
                                                        DataSet Index */
#define DSETID_INDEX_FREE                 0xffffffff /* An unused index entry */

/* 
 * PATCH DataSet format:
 * A list of patches, terminated by a patch with
 * address=PATCH_END.
 *
 * This allows for patches to be stored in flash.
 */
PREPACK struct patch_s {
    u32 *address;
    u32 data;
} POSTPACK ;

/*
 * Skip some patches.  Can be used to erase a single patch in a
 * patch DataSet without having to re-write the DataSet.  May
 * also be used to embed information for use by subsequent
 * patch code.  The "data" in a PATCH_SKIP tells how many
 * bytes of length "patch_s" to skip.
 */
#define PATCH_SKIP      ((u32 *)0x00000000)

/*
 * Execute code at the address specified by "data".
 * The address of the patch structure is passed as
 * the one parameter.
 */
#define PATCH_CODE_ABS  ((u32 *)0x00000001)

/*
 * Same as PATCH_CODE_ABS, but treat "data" as an
 * offset from the start of the patch word.
 */
#define PATCH_CODE_REL  ((u32 *)0x00000002)

/* Mark the end of this patch DataSet. */
#define PATCH_END       ((u32 *)0xffffffff)

/*
 * A DataSet which contains a Binary Patch to some other DataSet
 * uses the original dsetid with the DSETID_BPATCH_FLAG bit set.
 * Such a BPatch DataSet consists of BPatch metadata followed by
 * the bdiff bytes.  BPatch metadata consists of a single 32-bit
 * word that contains the size of the BPatched final image.
 *
 * To create a suitable bdiff DataSet, use bdiff in host/tools/bdiff
 * to create "diffs":
 *  bdiff -q -O -nooldmd5 -nonewmd5 -d ORIGfile NEWfile diffs
 * Then add BPatch metadata to the start of "diffs".
 *
 * NB: There are some implementation-induced restrictions
 * on which DataSets can be BPatched.
 */
#define DSETID_BPATCH_FLAG                0x80000000

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __DSETID_H__ */
