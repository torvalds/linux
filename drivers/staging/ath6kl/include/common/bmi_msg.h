//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef __BMI_MSG_H__
#define __BMI_MSG_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

/*
 * Bootloader Messaging Interface (BMI)
 *
 * BMI is a very simple messaging interface used during initialization
 * to read memory, write memory, execute code, and to define an
 * application entry PC.
 *
 * It is used to download an application to AR6K, to provide
 * patches to code that is already resident on AR6K, and generally
 * to examine and modify state.  The Host has an opportunity to use
 * BMI only once during bootup.  Once the Host issues a BMI_DONE
 * command, this opportunity ends.
 *
 * The Host writes BMI requests to mailbox0, and reads BMI responses
 * from mailbox0.   BMI requests all begin with a command
 * (see below for specific commands), and are followed by
 * command-specific data.
 *
 * Flow control:
 * The Host can only issue a command once the Target gives it a
 * "BMI Command Credit", using AR6K Counter #4.  As soon as the
 * Target has completed a command, it issues another BMI Command
 * Credit (so the Host can issue the next command).
 *
 * BMI handles all required Target-side cache flushing.
 */


/* Maximum data size used for BMI transfers */
#define BMI_DATASZ_MAX                      256

/* BMI Commands */

#define BMI_NO_COMMAND                      0

#define BMI_DONE                            1
        /*
         * Semantics: Host is done using BMI
         * Request format:
         *    A_UINT32      command (BMI_DONE)
         * Response format: none
         */

#define BMI_READ_MEMORY                     2
        /*
         * Semantics: Host reads AR6K memory
         * Request format:
         *    A_UINT32      command (BMI_READ_MEMORY)
         *    A_UINT32      address
         *    A_UINT32      length, at most BMI_DATASZ_MAX
         * Response format:
         *    A_UINT8       data[length]
         */

#define BMI_WRITE_MEMORY                    3
        /*
         * Semantics: Host writes AR6K memory
         * Request format:
         *    A_UINT32       command (BMI_WRITE_MEMORY)
         *    A_UINT32      address
         *    A_UINT32      length, at most BMI_DATASZ_MAX
         *    A_UINT8       data[length]
         * Response format: none
         */

#define BMI_EXECUTE                         4
        /*
         * Semantics: Causes AR6K to execute code
         * Request format:
         *    A_UINT32      command (BMI_EXECUTE)
         *    A_UINT32      address
         *    A_UINT32      parameter
         * Response format:
         *    A_UINT32      return value
         */

#define BMI_SET_APP_START                   5
        /*
         * Semantics: Set Target application starting address
         * Request format:
         *    A_UINT32      command (BMI_SET_APP_START)
         *    A_UINT32      address
         * Response format: none
         */

#define BMI_READ_SOC_REGISTER               6
        /*
         * Semantics: Read a 32-bit Target SOC register.
         * Request format:
         *    A_UINT32      command (BMI_READ_REGISTER)
         *    A_UINT32      address
         * Response format: 
         *    A_UINT32      value
         */

#define BMI_WRITE_SOC_REGISTER              7
        /*
         * Semantics: Write a 32-bit Target SOC register.
         * Request format:
         *    A_UINT32      command (BMI_WRITE_REGISTER)
         *    A_UINT32      address
         *    A_UINT32      value
         *
         * Response format: none
         */

#define BMI_GET_TARGET_ID                  8
#define BMI_GET_TARGET_INFO                8
        /*
         * Semantics: Fetch the 4-byte Target information
         * Request format:
         *    A_UINT32      command (BMI_GET_TARGET_ID/INFO)
         * Response format1 (old firmware):
         *    A_UINT32      TargetVersionID
         * Response format2 (newer firmware):
         *    A_UINT32      TARGET_VERSION_SENTINAL
         *    struct bmi_target_info;
         */

PREPACK struct bmi_target_info {
    A_UINT32 target_info_byte_count; /* size of this structure */
    A_UINT32 target_ver;             /* Target Version ID */
    A_UINT32 target_type;            /* Target type */
} POSTPACK;
#define TARGET_VERSION_SENTINAL 0xffffffff
#define TARGET_TYPE_AR6001 1
#define TARGET_TYPE_AR6002 2
#define TARGET_TYPE_AR6003 3


#define BMI_ROMPATCH_INSTALL               9
        /*
         * Semantics: Install a ROM Patch.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_INSTALL)
         *    A_UINT32      Target ROM Address
         *    A_UINT32      Target RAM Address or Value (depending on Target Type)
         *    A_UINT32      Size, in bytes
         *    A_UINT32      Activate? 1-->activate;
         *                            0-->install but do not activate
         * Response format:
         *    A_UINT32      PatchID
         */

#define BMI_ROMPATCH_UNINSTALL             10
        /*
         * Semantics: Uninstall a previously-installed ROM Patch,
         * automatically deactivating, if necessary.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_UNINSTALL)
         *    A_UINT32      PatchID
         *
         * Response format: none
         */

#define BMI_ROMPATCH_ACTIVATE              11
        /*
         * Semantics: Activate a list of previously-installed ROM Patches.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_ACTIVATE)
         *    A_UINT32      rompatch_count
         *    A_UINT32      PatchID[rompatch_count]
         *
         * Response format: none
         */

#define BMI_ROMPATCH_DEACTIVATE            12
        /*
         * Semantics: Deactivate a list of active ROM Patches.
         * Request format:
         *    A_UINT32      command (BMI_ROMPATCH_DEACTIVATE)
         *    A_UINT32      rompatch_count
         *    A_UINT32      PatchID[rompatch_count]
         *
         * Response format: none
         */


#define BMI_LZ_STREAM_START                13
        /*
         * Semantics: Begin an LZ-compressed stream of input
         * which is to be uncompressed by the Target to an
         * output buffer at address.  The output buffer must
         * be sufficiently large to hold the uncompressed
         * output from the compressed input stream.  This BMI
         * command should be followed by a series of 1 or more
         * BMI_LZ_DATA commands.
         *    A_UINT32      command (BMI_LZ_STREAM_START)
         *    A_UINT32      address
         * Note: Not supported on all versions of ROM firmware.
         */

#define BMI_LZ_DATA                        14
        /*
         * Semantics: Host writes AR6K memory with LZ-compressed
         * data which is uncompressed by the Target.  This command
         * must be preceded by a BMI_LZ_STREAM_START command. A series
         * of BMI_LZ_DATA commands are considered part of a single
         * input stream until another BMI_LZ_STREAM_START is issued.
         * Request format:
         *    A_UINT32      command (BMI_LZ_DATA)
         *    A_UINT32      length (of compressed data),
         *                  at most BMI_DATASZ_MAX
         *    A_UINT8       CompressedData[length]
         * Response format: none
         * Note: Not supported on all versions of ROM firmware.
         */

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __BMI_MSG_H__ */
