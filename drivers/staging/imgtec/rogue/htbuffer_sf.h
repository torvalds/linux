/*************************************************************************/ /*!
@File           htbuffer_sf.h
@Title          Host Trace Buffer interface string format specifiers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the Host Trace Buffer logging messages. The following
                list are the messages the host driver prints. Changing anything
				but the first column or spelling mistakes in the strings will
                break compatibility with log files created with older/newer
                driver versions.
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
#ifndef __HTBUFFER_SF_H__
#define __HTBUFFER_SF_H__

#if defined (__cplusplus)
extern "C" {
#endif


/*****************************************************************************
 * *DO*NOT* rearrange or delete lines in SFIDLIST or SFGROUPLIST or you
 *          WILL BREAK host tracing message compatibility with previous
 *          driver versions. Only add new ones, if so required.
 ****************************************************************************/


/* String used in pvrdebug -h output */
#define HTB_LOG_GROUPS_STRING_LIST   "ctrl,mmu,sync,main,brg"

/* Used in print statements to display log group state, one %s per group defined */
#define HTB_LOG_ENABLED_GROUPS_LIST_PFSPEC  "%s%s%s%s%s"

/* Available log groups - Master template
 *
 * Group usage is as follows:
 *    CTRL  - Internal Host Trace information and synchronisation data
 *    MMU   - MMU page mapping information
 *    SYNC  - Synchronisation debug
 *    MAIN  - Data master kicks, etc. tying in with the MAIN group in FWTrace
 *    DBG   - Temporary debugging group, logs not to be left in the driver
 *
 */
#define HTB_LOG_SFGROUPLIST                               \
	X( HTB_GROUP_NONE,     NONE  )                        \
/*     gid,                group flag / apphint name */   \
	X( HTB_GROUP_CTRL,     CTRL  )                        \
	X( HTB_GROUP_MMU,      MMU   )                        \
	X( HTB_GROUP_SYNC,     SYNC  )                        \
	X( HTB_GROUP_MAIN,     MAIN  )                        \
	X( HTB_GROUP_BRG,      BRG  )                         \
/* Debug group HTB_GROUP_DBG must always be last */       \
	X( HTB_GROUP_DBG,      DBG   )


/* Table of String Format specifiers, the group they belong and the number of
 * arguments each expects. Xmacro styled macros are used to generate what is
 * needed without requiring hand editing.
 *
 * id		: unique id within a group
 * gid		: group id as defined above
 * sym name	: symbolic name of enumerations used to identify message strings
 * string	: Actual string
 * #args	: number of arguments the string format requires
 */
#define HTB_LOG_SFIDLIST \
/*id,  gid,             sym name,                       string,                           # arguments */ \
X( 0,  HTB_GROUP_NONE,  HTB_SF_FIRST,                   "You should not use this string", 0) \
\
X( 1,  HTB_GROUP_CTRL,  HTB_SF_CTRL_LOGMODE,            "HTB log mode set to %d (1- all PID, 2 - restricted PID)\n", 1) \
X( 2,  HTB_GROUP_CTRL,  HTB_SF_CTRL_ENABLE_PID,         "HTB enable logging for PID %d\n", 1) \
X( 3,  HTB_GROUP_CTRL,  HTB_SF_CTRL_ENABLE_GROUP,       "HTB enable logging groups 0x%08x\n", 1) \
X( 4,  HTB_GROUP_CTRL,  HTB_SF_CTRL_LOG_LEVEL,          "HTB log level set to %d\n", 1) \
X( 5,  HTB_GROUP_CTRL,  HTB_SF_CTRL_OPMODE,             "HTB operating mode set to %d (1 - droplatest, 2 - drop oldest, 3 - block)\n", 1) \
X( 6,  HTB_GROUP_CTRL,  HTB_SF_CTRL_FWSYNC_SCALE,       "HTBFWSync OSTS=%08x%08x CRTS=%08x%08x CalcClkSpd=%d\n", 5) \
X( 7,  HTB_GROUP_CTRL,  HTB_SF_CTRL_FWSYNC_SCALE_RPT,   "FW Sync scale info OSTS=%08x%08x CRTS=%08x%08x CalcClkSpd=%d\n", 5) \
X( 8,  HTB_GROUP_CTRL,  HTB_SF_CTRL_FWSYNC_MARK,        "FW Sync Partition marker: %d\n", 1) \
X( 9,  HTB_GROUP_CTRL,  HTB_SF_CTRL_FWSYNC_MARK_RPT,    "FW Sync Partition repeat: %d\n", 1) \
\
X( 1,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_TABLE,       "MMU page op table entry page_id=%08x%08x index=%d level=%d val=%08x%08x map=%d\n", 7) \
X( 2,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_ALLOC,       "MMU allocating DevVAddr from %08x%08x to %08x%08x\n", 4) \
X( 3,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_FREE,        "MMU freeing DevVAddr from %08x%08x to %08x%08x\n", 4) \
X( 4,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_MAP,         "MMU mapping DevVAddr %08x%08x to DevPAddr %08x%08x\n", 4) \
X( 5,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_PMRMAP,      "MMU mapping PMR DevVAddr %08x%08x to DevPAddr %08x%08x\n", 4) \
X( 6,  HTB_GROUP_MMU,   HTB_SF_MMU_PAGE_OP_UNMAP,       "MMU unmapping DevVAddr %08x%08x\n", 2) \
\
X( 1,  HTB_GROUP_SYNC,  HTB_SF_SYNC_SERVER_ALLOC,       "Server sync allocation [%08X]\n", 1) \
X( 2,  HTB_GROUP_SYNC,  HTB_SF_SYNC_SERVER_UNREF,       "Server sync unreferenced [%08X]\n", 1) \
X( 3,  HTB_GROUP_SYNC,  HTB_SF_SYNC_PRIM_OP_CREATE,     "Sync OP create 0x%08x, block count=%d, server syncs=%d, client syncs=%d\n", 4) \
X( 4,  HTB_GROUP_SYNC,  HTB_SF_SYNC_PRIM_OP_TAKE,       "Sync OP take 0x%08x server syncs=%d, client syncs=%d\n", 3) \
X( 5,  HTB_GROUP_SYNC,  HTB_SF_SYNC_PRIM_OP_COMPLETE,   "Sync OP complete 0x%08x\n", 1) \
X( 6,  HTB_GROUP_SYNC,  HTB_SF_SYNC_PRIM_OP_DESTROY,    "Sync OP destroy 0x%08x\n", 1) \
\
X( 1,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_TA,            "Kick TA: FWCtx %08X @ %d\n", 2) \
X( 2,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_3D,            "Kick 3D: FWCtx %08X @ %d\n", 2) \
X( 3,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_CDM,           "Kick CDM: FWCtx %08X @ %d\n", 2) \
X( 4,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_RTU,           "Kick RTU: FWCtx %08X @ %d\n", 2) \
X( 5,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_SHG,           "Kick SHG: FWCtx %08X @ %d\n", 2) \
X( 6,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_2D,            "Kick 2D: FWCtx %08X @ %d\n", 2) \
X( 7,  HTB_GROUP_MAIN,  HTB_SF_MAIN_KICK_UNCOUNTED,     "Kick (uncounted) for all DMs\n", 0) \
X( 8,  HTB_GROUP_MAIN,  HTB_SF_MAIN_FWCCB_CMD,          "FW CCB Cmd: %d\n", 1) \
\
X( 1,  HTB_GROUP_BRG,   HTB_SF_BRG_BRIDGE_CALL,         "Bridge call: start: %010u: bid %03d fid %d\n", 3) \
X( 2,  HTB_GROUP_BRG,   HTB_SF_BRG_BRIDGE_CALL_ERR,     "Bridge call: start: %010u: bid %03d fid %d error %d\n", 4) \
\
X( 1,  HTB_GROUP_DBG,   HTB_SF_DBG_INTPAIR,             "0x%8.8x 0x%8.8x\n", 2) \
\
X( 65535, HTB_GROUP_NONE, HTB_SF_LAST,                  "You should not use this string\n", 15)



/* gid - Group numbers */
typedef enum _HTB_LOG_SFGROUPS {
#define X(A,B) A,
	HTB_LOG_SFGROUPLIST
#undef X
} HTB_LOG_SFGROUPS;


/* group flags are stored in an array of elements */
/* each of which have a certain number of bits */
#define HTB_FLAG_EL_T                   IMG_UINT32
#define HTB_FLAG_NUM_BITS_IN_EL         ( sizeof(HTB_FLAG_EL_T) * 8 )

#define HTB_LOG_GROUP_FLAG_GROUP(gid)   ( (gid-1) / HTB_FLAG_NUM_BITS_IN_EL )
#define HTB_LOG_GROUP_FLAG(gid)         (gid? (0x1 << ((gid-1)%HTB_FLAG_NUM_BITS_IN_EL)): 0)
#define HTB_LOG_GROUP_FLAG_NAME(gid)    HTB_LOG_TYPE_ ## gid

/* group enable flags */
typedef enum _HTB_LOG_TYPE {
#define X(a, b) HTB_LOG_GROUP_FLAG_NAME(b) = HTB_LOG_GROUP_FLAG(a),
	HTB_LOG_SFGROUPLIST
#undef X
} HTB_LOG_TYPE;



/*  The symbolic names found in the table above are assigned an ui32 value of
 *  the following format:
 *  31 30 28 27       20   19  16    15  12      11            0   bits
 *  -   ---   ---- ----     ----      ----        ---- ---- ----
 *     0-11: id number
 *    12-15: group id number
 *    16-19: number of parameters
 *    20-27: unused
 *    28-30: active: identify SF packet, otherwise regular int32
 *       31: reserved for signed/unsigned compatibility
 *
 *   The following macro assigns those values to the enum generated SF ids list.
 */
#define HTB_LOG_IDMARKER            (0x70000000)
#define HTB_LOG_CREATESFID(a,b,e)   ((a) | (b<<12) | (e<<16)) | HTB_LOG_IDMARKER

#define HTB_LOG_IDMASK              (0xFFF00000)
#define HTB_LOG_VALIDID(I)          ( ((I) & HTB_LOG_IDMASK) == HTB_LOG_IDMARKER )

typedef enum HTB_LOG_SFids {
#define X(a, b, c, d, e) c = HTB_LOG_CREATESFID(a,b,e),
	HTB_LOG_SFIDLIST
#undef X
} HTB_LOG_SFids;

/* Return the group id that the given (enum generated) id belongs to */
#define HTB_SF_GID(x) (((x)>>12) & 0xf)
/* future improvement to support log levels */
#define HTB_SF_LVL(x) (0)
/* Returns how many arguments the SF(string format) for the given (enum generated) id requires */
#define HTB_SF_PARAMNUM(x) (((x)>>16) & 0xf)

/* format of messages is: SF:PID:TIME:[PARn]*
 */
#define HTB_LOG_HEADER_SIZE         3
#define HTB_LOG_MAX_PARAMS          15

#if defined (__cplusplus)
}
#endif

#endif /* __HTBUFFER_SF_H__ */
/*****************************************************************************
 End of file (htbuffer_sf.h)
*****************************************************************************/


