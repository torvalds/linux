/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
#ifndef SERVICES_PDUMP_H
#define SERVICES_PDUMP_H

#include "img_types.h"
#include "services_km.h"


/* A PDump out2.txt script is made up of 3 sections from three buffers:
 * *
 *  - Init phase buffer    - holds PDump data written during driver
 *                            initialisation, non-volatile.
 *  - Main phase buffer   - holds PDump data written after driver init,
 *                            volatile.
 *  - Deinit phase buffer - holds PDump data  needed to shutdown HW/play back,
 *                            written only during driver initialisation using
 *                            the DEINIT flag.
 *
 * Volatile in this sense means that the buffer is drained and cleared when
 * the pdump capture application connects and transfers the data to file.
 *
 * The PDump sub-system uses the driver state (init/post-init), whether
 * the pdump capture application is connected or not (capture range set/unset)
 * and, if pdump connected whether the frame is in the range set, to decide
 * which of the 3 buffers to write the PDump data. Hence there are several
 * key time periods in the lifetime of the kernel driver that is enabled
 * with PDUMP=1 (flag XX labels below time line):
 *
 * Events:load              init        pdump       enter          exit         pdump
 *       driver             done       connects     range          range     disconnects
 *         |__________________|____________|__________|______________|____________|______ . . .
 * State:  |   init phase     | no capture | <- capture client connected ->       | no capture
 *         |                  |            |                                      |
 *         |__________________|____________|______________________________________|_____ . . .
 * Flag:   | CT,DI            | NONE,CT,PR | NONE,CT,PR                           | See no
 *         | Never NONE or PR | Never DI   | Never DI                             |   capture
 *         |__________________|____________|______________________________________|_____ . . .
 * Write   | NONE -undef      | -No write  | -No write | -Main buf    | -No write | See no
 * buffer  | CT -Init buf     | -Main buf  | -Main buf | -Main buf    | -Main buf |   capture
 *         | PR -undef        | -Init buf  | -undef    | -Init & Main | -undef    |
 *         | DI -Deinit buf   | -undef     | -undef    | -undef       | -undef    |
 *         |__________________|____________|___________|______________|___________|_____ . . .
 *
 * Note: The time line could repeat if the pdump capture application is
 * disconnected and reconnected without unloading the driver module.
 *
 * The DEINIT (DI) | CONTINUOUS (CT) | PERSISTENT (PR) flags must never
 * be OR'd together and given to a PDump call since undefined behaviour may
 * result and produce an invalid PDump which does not play back cleanly.
 *
 * The decision on which flag to use comes down to which time period the
 * client or server driver makes the PDump write call AND the nature/purpose
 * of the data.
 *
 * Note: This is a simplified time line, not all conditions represented.
 *
 */

typedef IMG_UINT32 PDUMP_FLAGS_T;

#define PDUMP_FLAGS_NONE            PDUMP_NONE     /*<! Output this entry with no special treatment i.e. output
                                                          only if in frame range. */
#define PDUMP_FLAGS_BLKDATA         PDUMP_BLKDATA  /*<! This flag indicates block-mode PDump data to be recorded
                                                          in Block script stream in addition to Main script stream,
                                                          if capture mode is set to BLOCKED */

#define PDUMP_FLAGS_DEINIT          0x20000000U    /*<! Output this entry to the de-initialisation section, must
                                                          only be used by the initialisation code in the Server. */

#define PDUMP_FLAGS_POWER           0x08000000U    /*<! Output this entry even when a power transition is ongoing,
                                                          as directed by other PDUMP flags. */

#define PDUMP_FLAGS_CONTINUOUS      PDUMP_CONT     /*<! Output this entry always regardless of framed capture range,
                                                          used by client applications being dumped.
                                                          During init phase of driver such data carrying this flag
                                                          will be recorded and present for all PDump client
                                                          connections.
                                                          Never combine with the PERSIST flag. */

#define PDUMP_FLAGS_PERSISTENT      PDUMP_PERSIST  /*<! Output this entry always regardless of app and range,
                                                          used by persistent resources created *after* driver
                                                          initialisation that must appear in all PDump captures
                                                          (i.e. current capture regardless of frame range (CONT)
                                                          and all future PDump captures) for that driver
                                                          instantiation/session.
                                                          Effectively this is data that is not forgotten
                                                          for the second and subsequent PDump client connections.
                                                          Never combine with the CONTINUOUS flag. */

#define PDUMP_FLAGS_DEBUG           0x00010000U    /*<! For internal debugging use */

#define PDUMP_FLAGS_NOHW            0x00000001U    /* For internal use: Skip sending instructions to the hardware
                                                        when NO_HARDWARE=0 AND PDUMP=1 */

#define PDUMP_FLAGS_FORCESPLIT      0x00000002U	   /* Forces Main and Block script streams to split - Internal
                                                        flag used in Block mode of PDump */

#define PDUMP_FLAGS_PDUMP_LOCK_HELD 0x00000004U    /* This flags denotes that PDUMP_LOCK is held already so
                                                        further calls to PDUMP_LOCK  with this flag set will not
                                                        try to take pdump lock. If PDUMP_LOCK is called without
                                                        this flag by some other thread then it will try to take
                                                        PDUMP_LOCK which would make that thread sleep. This flag
                                                        introduced to enforce the order of pdumping after bridge
                                                        lock removal */

#define PDUMP_FILEOFFSET_FMTSPEC    "0x%08X"
typedef IMG_UINT32 PDUMP_FILEOFFSET_T;

/* PDump stream macros*/

/* Parameter stream */
#define PDUMP_PARAM_INIT_STREAM_NAME       "paramInit"
#define PDUMP_PARAM_MAIN_STREAM_NAME       "paramMain"
#define PDUMP_PARAM_DEINIT_STREAM_NAME     "paramDeinit"
#define PDUMP_PARAM_BLOCK_STREAM_NAME      "paramBlock"

/*** Parameter stream sizes ***/

/* Parameter Init Stream */

/* The streams are made tunable in core.mk, wherein the size of these buffers can be specified.
 * In case nothing specified in core.mk, then the default size is taken
 */
#if defined(PDUMP_PARAM_INIT_STREAM_SIZE)
	#if (PDUMP_PARAM_INIT_STREAM_SIZE < (700 * 1024))
		#error PDUMP_PARAM_INIT_STREAM_SIZE must be at least 700 KB
	#endif
#endif

/* Parameter Main Stream */
#if defined(PDUMP_PARAM_MAIN_STREAM_SIZE)
	#if (PDUMP_PARAM_MAIN_STREAM_SIZE < (2 * 1024 * 1024))
		#error PDUMP_PARAM_MAIN_STREAM_SIZE must be at least 2 MB
	#endif
#endif

/* Parameter Deinit Stream */
#if defined(PDUMP_PARAM_DEINIT_STREAM_SIZE)
	#if (PDUMP_PARAM_DEINIT_STREAM_SIZE < (64 * 1024))
		#error PDUMP_PARAM_DEINIT_STREAM_SIZE must be at least 64 KB
	#endif
#endif

/* Parameter Block Stream */
/* There is no separate parameter Block stream as the Block script stream is
 * just a filtered Main script stream. Hence it will refer to the Main stream
 * parameters themselves.
 */

/*Script stream */
#define PDUMP_SCRIPT_INIT_STREAM_NAME      "scriptInit"
#define PDUMP_SCRIPT_MAIN_STREAM_NAME      "scriptMain"
#define PDUMP_SCRIPT_DEINIT_STREAM_NAME    "scriptDeinit"
#define PDUMP_SCRIPT_BLOCK_STREAM_NAME     "scriptBlock"

/*** Script stream sizes ***/

/* Script Init Stream */
#if defined(PDUMP_SCRIPT_INIT_STREAM_SIZE)
	#if (PDUMP_SCRIPT_INIT_STREAM_SIZE < (256 * 1024))
		#error PDUMP_SCRIPT_INIT_STREAM_SIZE must be at least 256 KB
	#endif
#endif

/* Script Main Stream */
#if defined(PDUMP_SCRIPT_MAIN_STREAM_SIZE)
	#if (PDUMP_SCRIPT_MAIN_STREAM_SIZE < (2 * 1024 * 1024))
		#error PDUMP_SCRIPT_MAIN_STREAM_SIZE must be at least 2 MB
	#endif
#endif

/* Script Deinit Stream */
#if defined(PDUMP_SCRIPT_DEINIT_STREAM_SIZE)
	#if (PDUMP_SCRIPT_DEINIT_STREAM_SIZE < (64 * 1024))
		#error PDUMP_SCRIPT_DEINIT_STREAM_SIZE must be at least 64 KB
	#endif
#endif

/* Script Block Stream */
#if defined(PDUMP_SCRIPT_BLOCK_STREAM_SIZE)
	#if (PDUMP_SCRIPT_BLOCK_STREAM_SIZE < (2 * 1024 * 1024))
		#error PDUMP_SCRIPT_BLOCK_STREAM_SIZE must be at least 2 MB
	#endif
#endif


#define PDUMP_PARAM_0_FILE_NAME     "%%0%%.prm"      /*!< Initial Param filename used in PDump capture */
#define PDUMP_PARAM_N_FILE_NAME     "%%0%%_%02u.prm" /*!< Param filename used when PRM file split */
#define PDUMP_PARAM_MAX_FILE_NAME   32               /*!< Max Size of parameter name used in out2.txt */

#define PDUMP_IS_CONTINUOUS(flags) ((flags & PDUMP_FLAGS_CONTINUOUS) != 0)

#endif /* SERVICES_PDUMP_H */
