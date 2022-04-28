/*************************************************************************/ /*!
@File           rgx_fwif_sf.h
@Title          RGX firmware interface string format specifiers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the rgx firmware logging messages. The following
                list are the messages the firmware prints. Changing anything
                but the first column or spelling mistakes in the strings will
                break compatibility with log files created with older/newer
                firmware versions.
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
#ifndef RGX_FWIF_SF_H
#define RGX_FWIF_SF_H

/******************************************************************************
 * *DO*NOT* rearrange or delete lines in SFIDLIST or SFGROUPLIST or you
 *           WILL BREAK fw tracing message compatibility with previous
 *           fw versions. Only add new ones, if so required.
 *****************************************************************************/
/* Available log groups */
#define RGXFW_LOG_SFGROUPLIST       \
	X(RGXFW_GROUP_NULL,NULL)        \
	X(RGXFW_GROUP_MAIN,MAIN)        \
	X(RGXFW_GROUP_CLEANUP,CLEANUP)  \
	X(RGXFW_GROUP_CSW,CSW)          \
	X(RGXFW_GROUP_PM, PM)           \
	X(RGXFW_GROUP_RTD,RTD)          \
	X(RGXFW_GROUP_SPM,SPM)          \
	X(RGXFW_GROUP_MTS,MTS)          \
	X(RGXFW_GROUP_BIF,BIF)          \
	X(RGXFW_GROUP_MISC,MISC)        \
	X(RGXFW_GROUP_POW,POW)          \
	X(RGXFW_GROUP_HWR,HWR)          \
	X(RGXFW_GROUP_HWP,HWP)          \
	X(RGXFW_GROUP_RPM,RPM)          \
	X(RGXFW_GROUP_DMA,DMA)          \
	X(RGXFW_GROUP_DBG,DBG)

enum RGXFW_LOG_SFGROUPS {
#define X(A,B) A,
	RGXFW_LOG_SFGROUPLIST
#undef X
};

#define IMG_SF_STRING_MAX_SIZE 256U

typedef struct {
	IMG_UINT32 ui32Id;
	IMG_CHAR sName[IMG_SF_STRING_MAX_SIZE];
} RGXFW_STID_FMT; /*  pair of string format id and string formats */

typedef struct {
	IMG_UINT32 ui32Id;
	const IMG_CHAR *psName;
} RGXKM_STID_FMT; /*  pair of string format id and string formats */

/* Table of String Format specifiers, the group they belong and the number of
 * arguments each expects. Xmacro styled macros are used to generate what is
 * needed without requiring hand editing.
 *
 * id		: id within a group
 * gid		: group id
 * Sym name	: name of enumerations used to identify message strings
 * String	: Actual string
 * #args	: number of arguments the string format requires
 */
#define RGXFW_LOG_SFIDLIST \
/*id, gid,              id name,        string,                           # arguments */ \
X(  0, RGXFW_GROUP_NULL, RGXFW_SF_FIRST, "You should not use this string", 0) \
\
X(  1, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3D_DEPRECATED, "Kick 3D: FWCtx 0x%08.8x @ %d, RTD 0x%08x. Partial render:%d, CSW resume:%d, prio:%d", 6) \
X(  2, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_FINISHED, "3D finished, HWRTData0State=%x, HWRTData1State=%x", 2) \
X(  3, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK3D_TQ_DEPRECATED, "Kick 3D TQ: FWCtx 0x%08.8x @ %d, CSW resume:%d, prio: %d", 4) \
X(  4, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_TQ_FINISHED, "3D Transfer finished", 0) \
X(  5, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_COMPUTE_DEPRECATED, "Kick Compute: FWCtx 0x%08.8x @ %d, prio: %d", 3) \
X(  6, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_FINISHED, "Compute finished", 0) \
X(  7, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TA_DEPRECATED, "Kick TA: FWCtx 0x%08.8x @ %d, RTD 0x%08x. First kick:%d, Last kick:%d, CSW resume:%d, prio:%d", 7) \
X(  8, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_FINISHED, "TA finished", 0) \
X(  9, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_RESTART_AFTER_PRENDER, "Restart TA after partial render", 0) \
X( 10, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_RESUME_WOUT_PRENDER, "Resume TA without partial render", 0) \
X( 11, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OOM, "Out of memory! Context 0x%08x, HWRTData 0x%x", 2) \
X( 12, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TLA_DEPRECATED, "Kick TLA: FWCtx 0x%08.8x @ %d, prio:%d", 3) \
X( 13, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TLA_FINISHED, "TLA finished", 0) \
X( 14, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CCCB_WOFF_UPDATE, "cCCB Woff update = %d, DM = %d, FWCtx = 0x%08.8x", 3) \
X( 16, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_START, "UFO Checks for FWCtx 0x%08.8x @ %d", 2) \
X( 17, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK, "UFO Check: [0x%08.8x] is 0x%08.8x requires 0x%08.8x", 3) \
X( 18, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_SUCCEEDED, "UFO Checks succeeded", 0) \
X( 19, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_PR_CHECK, "UFO PR-Check: [0x%08.8x] is 0x%08.8x requires >= 0x%08.8x", 3) \
X( 20, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK_START, "UFO SPM PR-Checks for FWCtx 0x%08.8x", 1) \
X( 21, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK_DEPRECATED, "UFO SPM special PR-Check: [0x%08.8x] is 0x%08.8x requires >= ????????, [0x%08.8x] is ???????? requires 0x%08.8x", 4) \
X( 22, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_UPDATE_START, "UFO Updates for FWCtx 0x%08.8x @ %d", 2) \
X( 23, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_UPDATE, "UFO Update: [0x%08.8x] = 0x%08.8x", 2) \
X( 24, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ASSERT_FAILED, "ASSERT Failed: line %d of:", 1) \
X( 25, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_LOCKUP_DEPRECATED, "HWR: Lockup detected on DM%d, FWCtx: 0x%08.8x", 2) \
X( 26, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_RESET_FW_DEPRECATED, "HWR: Reset fw state for DM%d, FWCtx: 0x%08.8x, MemCtx: 0x%08.8x", 3) \
X( 27, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_RESET_HW_DEPRECATED, "HWR: Reset HW", 0) \
X( 28, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_TERMINATED_DEPRECATED, "HWR: Lockup recovered.", 0) \
X( 29, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_FALSE_LOCKUP_DEPRECATED, "HWR: False lockup detected for DM%u", 1) \
X( 30, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ALIGN_FAILED, "Alignment check %d failed: host = 0x%x, fw = 0x%x", 3) \
X( 31, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GP_USC_TRIGGERED, "GP USC triggered", 0) \
X( 32, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_OVERALLOC_REGS, "Overallocating %u temporary registers and %u shared registers for breakpoint handler", 2) \
X( 33, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_SET_DEPRECATED, "Setting breakpoint: Addr 0x%08.8x", 1) \
X( 34, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_STORE, "Store breakpoint state", 0) \
X( 35, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_UNSET, "Unsetting BP Registers", 0) \
X( 36, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NONZERO_RT, "Active RTs expected to be zero, actually %u", 1) \
X( 37, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTC_PRESENT, "RTC present, %u active render targets", 1) \
X( 38, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_EST_POWER_DEPRECATED, "Estimated Power 0x%x", 1) \
X( 39, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTA_TARGET, "RTA render target %u", 1) \
X( 40, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTA_KICK_RENDER, "Kick RTA render %u of %u", 2) \
X( 41, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_SIZES_CHECK_DEPRECATED, "HWR sizes check %d failed: addresses = %d, sizes = %d", 3) \
X( 42, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_ENABLE_DEPRECATED, "Pow: DUSTS_ENABLE = 0x%x", 1) \
X( 43, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_HWREQ_DEPRECATED, "Pow: On(1)/Off(0): %d, Units: 0x%08.8x", 2) \
X( 44, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_CHANGE_DEPRECATED, "Pow: Changing number of dusts from %d to %d", 2) \
X( 45, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_SIDEKICK_IDLE_DEPRECATED, "Pow: Sidekick ready to be powered down", 0) \
X( 46, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_CHANGE_REQ_DEPRECATED, "Pow: Request to change num of dusts to %d (bPowRascalDust=%d)", 2) \
X( 47, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PARTIALRENDER_WITHOUT_ZSBUFFER_STORE, "No ZS Buffer used for partial render (store)", 0) \
X( 48, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PARTIALRENDER_WITHOUT_ZSBUFFER_LOAD, "No Depth/Stencil Buffer used for partial render (load)", 0) \
X( 49, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_SET_LOCKUP_DEPRECATED, "HWR: Lock-up DM%d FWCtx: 0x%08.8x", 2) \
X( 50, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_REG_VALUE_DEPRECATED, "MLIST%d checker: CatBase TE=0x%08x (%d Pages), VCE=0x%08x (%d Pages), ALIST=0x%08x, IsTA=%d", 7) \
X( 51, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_MLIST_VALUE, "MLIST%d checker: MList[%d] = 0x%08x", 3) \
X( 52, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_OK, "MLIST%d OK", 1) \
X( 53, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_EMPTY, "MLIST%d is empty", 1) \
X( 54, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_REG_VALUE, "MLIST%d checker: CatBase TE=0x%08x%08x, VCE=0x%08x%08x, ALIST=0x%08x%08x, IsTA=%d", 8) \
X( 55, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_40480KICK, "3D OQ flush kick", 0) \
X( 56, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWP_UNSUPPORTED_BLOCK, "HWPerf block ID (0x%x) unsupported by device", 1) \
X( 57, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_SET_DEPRECATED2, "Setting breakpoint: Addr 0x%08.8x DM%u", 2) \
X( 58, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU_DEPRECATED, "Kick RTU: FWCtx 0x%08.8x @ %d, prio: %d", 3) \
X( 59, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTU_FINISHED_DEPRECATED, "RDM finished on context %u", 1) \
X( 60, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_SHG_DEPRECATED, "Kick SHG: FWCtx 0x%08.8x @ %d, prio: %d", 3) \
X( 61, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SHG_FINISHED_DEPRECATED, "SHG finished", 0) \
X( 62, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FBA_FINISHED_DEPRECATED, "FBA finished on context %u", 1) \
X( 63, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_FAILED, "UFO Checks failed", 0) \
X( 64, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_START, "Kill DM%d start", 1) \
X( 65, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_COMPLETE, "Kill DM%d complete", 1) \
X( 66, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FC_CCB_UPDATE_DEPRECATED, "FC%u cCCB Woff update = %u", 2) \
X( 67, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU_DEPRECATED2, "Kick RTU: FWCtx 0x%08.8x @ %d, prio: %d, Frame Context: %d", 4) \
X( 68, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GPU_INIT, "GPU init", 0) \
X( 69, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNITS_INIT, "GPU Units init (# mask: 0x%x)", 1) \
X( 70, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGTIMES, "Register access cycles: read: %d cycles, write: %d cycles, iterations: %d", 3) \
X( 71, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGCONFIG_ADD, "Register configuration added. Address: 0x%x Value: 0x%x%x", 3) \
X( 72, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGCONFIG_SET, "Register configuration applied to type %d. (0:pow on, 1:Rascal/dust init, 2-5: TA,3D,CDM,TLA, 6:All)", 1) \
X( 73, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TPC_FLUSH, "Perform TPC flush.", 0) \
X( 74, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_LOCKUP_DEPRECATED, "GPU has locked up (see HWR logs for more info)", 0) \
X( 75, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_OUTOFTIME, "HWR has been triggered - GPU has overrun its deadline (see HWR logs)", 0) \
X( 76, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_POLLFAILURE, "HWR has been triggered - GPU has failed a poll (see HWR logs)", 0) \
X( 77, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DOPPLER_OOM_DEPRECATED, "Doppler out of memory event for FC %u", 1) \
X( 78, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK1, "UFO SPM special PR-Check: [0x%08.8x] is 0x%08.8x requires >= 0x%08.8x", 3) \
X( 79, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK2, "UFO SPM special PR-Check: [0x%08.8x] is 0x%08.8x requires 0x%08.8x", 3) \
X( 80, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TIMESTAMP, "TIMESTAMP -> [0x%08.8x]", 1) \
X( 81, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_RMW_UPDATE_START, "UFO RMW Updates for FWCtx 0x%08.8x @ %d", 2) \
X( 82, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_RMW_UPDATE, "UFO Update: [0x%08.8x] = 0x%08.8x", 2) \
X( 83, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NULLCMD, "Kick Null cmd: FWCtx 0x%08.8x @ %d", 2) \
X( 84, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RPM_OOM_DEPRECATED, "RPM Out of memory! Context 0x%08x, SH requestor %d", 2) \
X( 85, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTU_ABORT_DISCARD_DEPRECATED, "Discard RTU due to RPM abort: FWCtx 0x%08.8x @ %d, prio: %d, Frame Context: %d", 4) \
X( 86, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED, "Deferring DM%u from running context 0x%08x @ %d (deferred DMs = 0x%08x)", 4) \
X( 87, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED_WAITING_TURN_DEPRECATED, "Deferring DM%u from running context 0x%08x @ %d to let other deferred DMs run (deferred DMs = 0x%08x)", 4) \
X( 88, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED_NO_LONGER, "No longer deferring DM%u from running context = 0x%08x @ %d (deferred DMs = 0x%08x)", 4) \
X( 89, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_FWCCB_DEPRECATED, "FWCCB for DM%u is full, we will have to wait for space! (Roff = %u, Woff = %u)", 3) \
X( 90, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_FWCCB, "FWCCB for OSid %u is full, we will have to wait for space! (Roff = %u, Woff = %u)", 3) \
X( 91, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SYNC_PART, "Host Sync Partition marker: %d", 1) \
X( 92, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SYNC_PART_RPT, "Host Sync Partition repeat: %d", 1) \
X( 93, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CLOCK_SPEED_CHANGE, "Core clock set to %d Hz", 1) \
X( 94, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_OFFSETS, "Compute Queue: FWCtx 0x%08.8x, prio: %d, queue: 0x%08x%08x (Roff = %u, Woff = %u, Size = %u)", 7) \
X( 95, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE_DEPRECATED, "Signal check failed, Required Data: 0x%x, Address: 0x%08x%08x", 3) \
X( 96, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_UPDATE_DEPRECATED, "Signal update, Snoop Filter: %u, MMU Ctx: %u, Signal Id: %u, Signals Base: 0x%08x%08x", 5) \
X( 97, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWCONTEXT_SIGNALED, "Signalled the previously waiting FWCtx: 0x%08.8x, OSId: %u, Signal Address: 0x%08x%08x", 4) \
X( 98, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_STALLED_DEPRECATED, "Compute stalled", 0) \
X( 99, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_STALLED, "Compute stalled (Roff = %u, Woff = %u, Size = %u)", 3) \
X(100, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_RESUMED_FROM_STALL, "Compute resumed (Roff = %u, Woff = %u, Size = %u)", 3) \
X(101, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NOTIFY_SIGNAL_UPDATE, "Signal update notification from the host, PC Physical Address: 0x%08x%08x, Signal Virtual Address: 0x%08x%08x", 4) \
X(102, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_UPDATE_OSID_DM_DEPRECATED, "Signal update from DM: %u, OSId: %u, PC Physical Address: 0x%08x%08x", 4) \
X(103, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE_DM_DEPRECATED, "DM: %u signal check failed", 1) \
X(104, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TDM_DEPRECATED, "Kick TDM: FWCtx 0x%08.8x @ %d, prio:%d", 3) \
X(105, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_FINISHED, "TDM finished", 0) \
X(106, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TE_PIPE_STATUS_DEPRECATED, "MMU_PM_CAT_BASE_TE[%d]_PIPE[%d]:  0x%08x 0x%08x)", 4) \
X(107, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_HIT_DEPRECATED, "BRN 54141 HIT", 0) \
X(108, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_APPLYING_DUMMY_TA_DEPRECATED, "BRN 54141 Dummy TA kicked", 0) \
X(109, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_RESUME_TA_DEPRECATED, "BRN 54141 resume TA", 0) \
X(110, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_DOUBLE_HIT_DEPRECATED, "BRN 54141 double hit after applying WA", 0) \
X(111, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_DUMMY_TA_VDM_BASE_DEPRECATED, "BRN 54141 Dummy TA VDM base address: 0x%08x%08x", 2) \
X(112, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE_WITH_CURRENT, "Signal check failed, Required Data: 0x%x, Current Data: 0x%x, Address: 0x%08x%08x", 4) \
X(113, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BUFFER_STALL_DEPRECATED, "TDM stalled (Roff = %u, Woff = %u)", 2) \
X(114, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NOTIFY_WRITE_OFFSET_UPDATE, "Write Offset update notification for stalled FWCtx 0x%08.8x", 1) \
X(115, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_PRIORITY_CHANGE_DEPRECATED, "Changing OSid %d's priority from %u to %u", 3) \
X(116, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_RESUMED, "Compute resumed", 0) \
X(117, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TLA, "Kick TLA: FWCtx 0x%08.8x @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 7) \
X(118, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TDM, "Kick TDM: FWCtx 0x%08.8x @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 7) \
X(119, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TA, "Kick TA: FWCtx 0x%08.8x @ %d, RTD 0x%08x, First kick:%d, Last kick:%d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 11) \
X(120, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3D, "Kick 3D: FWCtx 0x%08.8x @ %d, RTD 0x%08x, Partial render:%d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 10) \
X(121, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3DTQ, "Kick 3D TQ: FWCtx 0x%08.8x @ %d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 8) \
X(122, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_COMPUTE, "Kick Compute: FWCtx 0x%08.8x @ %d. (PID:%d, prio:%d, ext:0x%08x, int:0x%08x)", 6) \
X(123, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU_DEPRECATED3, "Kick RTU: FWCtx 0x%08.8x @ %d, Frame Context:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 8) \
X(124, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_SHG_DEPRECATED2, "Kick SHG: FWCtx 0x%08.8x @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 7) \
X(125, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CSRM_RECONFIG, "Reconfigure CSRM: special coeff support enable %d.", 1) \
X(127, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_REQ_MAX_COEFFS, "TA requires max coeff mode, deferring: %d.", 1) \
X(128, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_REQ_MAX_COEFFS, "3D requires max coeff mode, deferring: %d.", 1) \
X(129, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_FAILED, "Kill DM%d failed", 1) \
X(130, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_QUEUE, "Thread Queue is full, we will have to wait for space! (Roff = %u, Woff = %u)", 2) \
X(131, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_QUEUE_FENCE, "Thread Queue is fencing, we are waiting for Roff = %d (Roff = %u, Woff = %u)", 3) \
X(132, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SET_HCS_TRIGGERED, "DM %d failed to Context Switch on time. Triggered HCS (see HWR logs).", 1) \
X(133, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HCS_SET_DEPRECATED, "HCS changed to %d ms", 1) \
X(134, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UPDATE_TILES_IN_FLIGHT_DEPRECATED, "Updating Tiles In Flight (Dusts=%d, PartitionMask=0x%08x, ISPCtl=0x%08x%08x)", 4) \
X(135, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SET_TILES_IN_FLIGHT, "  Phantom %d: USCTiles=%d", 2) \
X(136, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ISOLATION_CONF_OFF_DEPRECATED, "Isolation grouping is disabled", 0) \
X(137, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ISOLATION_CONF_DEPRECATED, "Isolation group configured with a priority threshold of %d", 1) \
X(138, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_ONLINE_DEPRECATED, "OS %d has come online", 1) \
X(139, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_OFFLINE_DEPRECATED, "OS %d has gone offline", 1) \
X(140, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWCONTEXT_SIGNAL_REKICK, "Signalled the previously stalled FWCtx: 0x%08.8x, OSId: %u, Signal Address: 0x%08x%08x", 4) \
X(141, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_OFFSETS_DEPRECATED, "TDM Queue: FWCtx 0x%08.8x, prio: %d, queue: 0x%08x%08x (Roff = %u, Woff = %u, Size = %u)", 7) \
X(142, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_OFFSET_READ_RESET, "Reset TDM Queue Read Offset: FWCtx 0x%08.8x, queue: 0x%08x%08x (Roff = %u becomes 0, Woff = %u, Size = %u)", 6) \
X(143, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UMQ_MISMATCHED_READ_OFFSET, "User Mode Queue mismatched stream start: FWCtx 0x%08.8x, queue: 0x%08x%08x (Roff = %u, StreamStartOffset = %u)", 5) \
X(144, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GPU_DEINIT, "GPU deinit", 0) \
X(145, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNITS_DEINIT, "GPU units deinit", 0) \
X(146, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_INIT_CONFIG, "Initialised OS %d with config flags 0x%08x", 2) \
X(147, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_LIMIT, "UFO limit exceeded %d/%d", 2) \
X(148, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_62850KICK, "3D Dummy stencil store", 0) \
X(149, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_INIT_CONFIG_DEPRECATED, "Initialised OS %d with config flags 0x%08x and extended config flags 0x%08x", 3) \
X(150, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNKNOWN_COMMAND_DEPRECATED, "Unknown Command (eCmdType=0x%08x)", 1) \
X(151, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_FORCED_UPDATE, "UFO forced update: FWCtx 0x%08.8x @ %d [0x%08.8x] = 0x%08.8x", 4) \
X(152, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_FORCED_UPDATE_NOP, "UFO forced update NOP: FWCtx 0x%08.8x @ %d [0x%08.8x] = 0x%08.8x, reason %d", 5) \
X(153, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BRN66075_CHECK, "TDM context switch check: Roff %u points to 0x%08x, Match=%u", 3) \
X(154, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_INIT_CCBS, "OSid %d CCB init status: %d (1-ok 0-fail): kCCBCtl@0x%x kCCB@0x%x fwCCBCtl@0x%x fwCCB@0x%x", 6) \
X(155, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWIRQ, "FW IRQ # %u @ %u", 2) \
X(156, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_SET, "Setting breakpoint: Addr 0x%08.8x DM%u usc_breakpoint_ctrl_dm = %u", 3) \
X(157, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_KERNEL_CCB_DEPRECATED, "Invalid KCCB setup for OSid %u: KCCB 0x%08x, KCCB Ctrl 0x%08x", 3) \
X(158, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_KERNEL_CCB_CMD, "Invalid KCCB cmd (%u) for OSid %u @ KCCB 0x%08x", 3) \
X(159, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FW_FAULT, "FW FAULT: At line %d in file 0x%08x%08x, additional data=0x%08x", 4) \
X(160, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_INVALID, "Invalid breakpoint: MemCtx 0x%08x Addr 0x%08.8x DM%u usc_breakpoint_ctrl_dm = %u", 4) \
X(161, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FLUSHINVAL_CMD_INVALID_DEPRECATED, "Discarding invalid SLC flushinval command for OSid %u: DM %u, FWCtx 0x%08x", 3) \
X(162, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_NOTIFY_WRITE_OFFSET_UPDATE_DEPRECATED, "Invalid Write Offset update notification from OSid %u to DM %u: FWCtx 0x%08x, MemCtx 0x%08x", 4) \
X(163, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_KCCB_KICK_CMD_DEPRECATED, "Null FWCtx in KCCB kick cmd for OSid %u: KCCB 0x%08x, ROff %u, WOff %u", 4) \
X(164, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FULL_CHPTCCB, "Checkpoint CCB for OSid %u is full, signalling host for full check state (Roff = %u, Woff = %u)", 3) \
X(165, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_INIT_CCBS_DEPRECATED, "OSid %d CCB init status: %d (1-ok 0-fail): kCCBCtl@0x%x kCCB@0x%x fwCCBCtl@0x%x fwCCB@0x%x chptCCBCtl@0x%x chptCCB@0x%x", 8) \
X(166, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_STATE_CHANGE, "OSid %d fw state transition request: from %d to %d (0-offline 1-ready 2-active 3-offloading). Status %d (1-ok 0-fail)", 4) \
X(167, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_STALE_KCCB_CMDS, "OSid %u has %u stale commands in its KCCB", 2) \
X(168, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_VCE_PAUSE, "Applying VCE pause", 0) \
X(169, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KCCB_UPDATE_RTN_SLOT_DEPRECATED, "OSid %u KCCB slot %u value updated to %u", 3) \
X(170, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNKNOWN_KCCB_COMMAND, "Unknown KCCB Command: KCCBCtl=0x%08x, KCCB=0x%08x, Roff=%u, Woff=%u, Wrap=%u, Cmd=0x%08x, CmdType=0x%08x", 7) \
X(171, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNKNOWN_CCB_COMMAND1, "Unknown Client CCB Command processing fences: FWCtx=0x%08x, CCBCtl=0x%08x, CCB=0x%08x, Roff=%u, Doff=%u, Woff=%u, Wrap=%u, CmdHdr=0x%08x, CmdType=0x%08x, CmdSize=%u", 10) \
X(172, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UNKNOWN_CCB_COMMAND2, "Unknown Client CCB Command executing kick: FWCtx=0x%08x, CCBCtl=0x%08x, CCB=0x%08x, Roff=%u, Doff=%u, Woff=%u, Wrap=%u, CmdHdr=0x%08x, CmdType=0x%08x, CmdSize=%u", 10) \
X(173, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_KCCB_KICK_CMD, "Null FWCtx in KCCB kick cmd for OSid %u with WOff %u", 2) \
X(174, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FLUSHINVAL_CMD_INVALID, "Discarding invalid SLC flushinval command for OSid %u, FWCtx 0x%08x", 2) \
X(175, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_NOTIFY_WRITE_OFFSET_UPDATE, "Invalid Write Offset update notification from OSid %u: FWCtx 0x%08x, MemCtx 0x%08x", 3) \
X(176, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FW_INIT_CONFIG, "Initialised Firmware with config flags 0x%08x and extended config flags 0x%08x", 2) \
X(177, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PHR_CONFIG, "Set Periodic Hardware Reset Mode: %d", 1) \
X(179, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PHR_TRIG, "PHR mode %d, FW state: 0x%08x, HWR flags: 0x%08x", 3) \
X(180, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PHR_RESET_DEPRECATED, "PHR mode %d triggered a reset", 1) \
X(181, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_UPDATE, "Signal update, Snoop Filter: %u, Signal Id: %u", 2) \
X(182, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FIXME_SERIES8, "WARNING: Skipping FW KCCB Cmd type %d which is not yet supported on Series8.", 1) \
X(183, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INCONSISTENT_MMU_FLAGS, "MMU context cache data NULL, but cache flags=0x%x (sync counter=%u, update value=%u) OSId=%u", 4) \
X(184, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SLC_FLUSH, "SLC range based flush: Context=%u VAddr=0x%02x%08x, Size=0x%08x, Invalidate=%d", 5) \
X(185, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FBSC_INVAL, "FBSC invalidate for Context [0x%08x]: Entry mask 0x%08x%08x.", 3) \
X(186, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BRN66284_UPDATE, "TDM context switch check: Roff %u was not valid for kick starting at %u, moving back to %u", 3) \
X(187, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SPFILTER_UPDATES, "Signal updates: FIFO: %u, Signals: 0x%08x", 2) \
X(188, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_FBSC_CMD, "Invalid FBSC cmd: FWCtx 0x%08x, MemCtx 0x%08x", 2) \
X(189, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BRN68497_BLIT, "Insert BRN68497 WA blit after TDM Context store.", 0) \
X(190, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PENDING_UFO_UPDATE_START, "UFO Updates for previously finished FWCtx 0x%08.8x", 1) \
X(191, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTC_RTA_PRESENT, "RTC with RTA present, %u active render targets", 1) \
X(192, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NULL_RTAS, "Invalid RTA Set-up. The ValidRenderTargets array in RTACtl is Null!", 0) \
X(193, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_INVALID_COUNTER, "Block 0x%x / Counter 0x%x INVALID and ignored", 2) \
X(194, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ECC_FAULT_DEPRECATED, "ECC fault GPU=0x%08x FW=0x%08x", 2) \
X(195, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PROCESS_XPU_EVENT, "Processing XPU event on DM = %d", 1) \
X(196, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_VZ_WDG_TRIGGER, "OSid %u failed to respond to the virtualisation watchdog in time. Timestamp of its last input = %u", 2) \
X(197, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_LOCKUP, "GPU-%u has locked up (see HWR logs for more info)", 1) \
X(198, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UPDATE_TILES_IN_FLIGHT, "Updating Tiles In Flight (Dusts=%d, PartitionMask=0x%08x, ISPCtl=0x%08x)", 3) \
X(199, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_LOCKUP_DM, "GPU has locked up (see HWR logs for more info)", 0) \
X(200, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REPROCESS_XPU_EVENTS, "Reprocessing outstanding XPU events from cores 0x%02x", 1) \
X(201, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SECONDARY_XPU_EVENT, "Secondary XPU event on DM=%d, CoreMask=0x%02x, Raised=0x%02x", 3) \
X(202, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_OFFSETS, "TDM Queue: Core %u, FWCtx 0x%08.8x, prio: %d, queue: 0x%08x%08x (Roff = %u, Woff = %u, Size = %u)", 8) \
X(203, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BUFFER_STALL, "TDM stalled Core %u (Roff = %u, Woff = %u)", 3) \
X(204, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_CORE_OFFSETS, "Compute Queue: Core %u, FWCtx 0x%08.8x, prio: %d, queue: 0x%08x%08x (Roff = %u, Woff = %u, Size = %u)", 8) \
X(205, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_CORE_STALLED, "Compute stalled core %u (Roff = %u, Woff = %u, Size = %u)", 4) \
X(206, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UMQ_MISMATCHED_CORE_READ_OFFSET, "User Mode Queue mismatched stream start: Core %u, FWCtx 0x%08.8x, queue: 0x%08x%08x (Roff = %u, StreamStartOffset = %u)", 6) \
X(207, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_RESUMED_FROM_STALL, "TDM resumed core %u (Roff = %u, Woff = %u)", 3) \
X(208, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_CORE_RESUMED_FROM_STALL, "Compute resumed core %u (Roff = %u, Woff = %u, Size = %u)", 4) \
X(209, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_MTS_PERMISSION_CHANGED, " Updated permission for OSid %u to perform MTS kicks: %u (1 = allowed, 0 = not allowed)", 2) \
X(210, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TEST1, "Mask = 0x%X, mask2 = 0x%X", 2) \
X(211, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TEST2, "  core %u, reg = %u, mask = 0x%X)", 3) \
X(212, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ECC_FAULT_SAFETY_BUS, "ECC fault received from safety bus: 0x%08x", 1) \
X(213, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SAFETY_WDG_CONFIG, "Safety Watchdog threshold period set to 0x%x clock cycles", 1) \
X(214, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SAFETY_WDG_TRIGGER, "MTS Safety Event trigged by the safety watchdog.", 0) \
X(215, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_USC_TASKS_RANGE, "DM%d USC tasks range limit 0 - %d, stride %d", 3) \
X(216, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GPU_ECC_FAULT, "ECC fault GPU=0x%08x", 1) \
X(217, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GPU_SAFETY_RESET, "GPU Hardware units reset to prevent transient faults.", 0) \
X(218, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ABORTCMD, "Kick Abort cmd: FWCtx 0x%08.8x @ %d", 2) \
X(219, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RAY, "Kick Ray: FWCtx 0x%08.8x @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08x, int:0x%08x)", 7)\
X(220, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RAY_FINISHED, "Ray finished", 0) \
X(221, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWDATA_INIT_STATUS, "State of firmware's private data at boot time: %d (0 = uninitialised, 1 = initialised); Fw State Flags = 0x%08X", 2) \
X(222, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CFI_TIMEOUT, "CFI Timeout detected (%d increasing to %d)", 2) \
X(223, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CFI_TIMEOUT_FBM, "CFI Timeout detected for FBM (%d increasing to %d)", 2) \
X(224, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GEOM_OOM_DISALLOWED, "Geom OOM event not allowed", 0) \
X(225, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_PRIORITY_CHANGE, "Changing OSid %d's priority from %u to %u; Isolation = %u (0 = off; 1 = on)", 4) \
\
X(  1, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_KICK_DEPRECATED, "Bg Task DM = %u, counted = %d", 2) \
X(  2, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_COMPLETE_DEPRECATED, "Bg Task complete DM = %u", 1) \
X(  3, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_KICK, "Irq Task DM = %u, Breq = %d, SBIrq = 0x%x", 3) \
X(  4, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_COMPLETE_DEPRECATED, "Irq Task complete DM = %u", 1) \
X(  5, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KICK_MTS_BG_ALL_DEPRECATED, "Kick MTS Bg task DM=All", 0) \
X(  6, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KICK_MTS_IRQ, "Kick MTS Irq task DM=%d", 1) \
X(  7, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYCELLTYPE_DEPRECATED, "Ready queue debug DM = %u, celltype = %d", 2) \
X(  8, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYTORUN_DEPRECATED, "Ready-to-run debug DM = %u, item = 0x%x", 2) \
X(  9, RGXFW_GROUP_MTS, RGXFW_SF_MTS_CMDHEADER, "Client command header DM = %u, client CCB = 0x%x, cmd = 0x%x", 3) \
X( 10, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYTORUN, "Ready-to-run debug OSid = %u, DM = %u, item = 0x%x", 3) \
X( 11, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYCELLTYPE_DEPRECATED2, "Ready queue debug DM = %u, celltype = %d, OSid = %u", 3) \
X( 12, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_KICK_DEPRECATED2, "Bg Task DM = %u, counted = %d, OSid = %u", 3) \
X( 13, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_COMPLETE, "Bg Task complete DM Bitfield: %u", 1) \
X( 14, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_COMPLETE, "Irq Task complete.", 0) \
X( 15, RGXFW_GROUP_MTS, RGXFW_SF_MTS_CMD_DISCARD, "Discarded Command Type: %d OS ID = %d PID = %d context = 0x%08x cccb ROff = 0x%x, due to USC breakpoint hit by OS ID = %d PID = %d.", 7) \
X( 16, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KCCBCMD_EXEC_DEPRECATED, "KCCB Slot %u: DM=%u, Cmd=0x%08x, OSid=%u", 4) \
X( 17, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KCCBCMD_RTN_VALUE, "KCCB Slot %u: Return value %u", 2) \
X( 18, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_KICK, "Bg Task OSid = %u", 1) \
X( 19, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KCCBCMD_EXEC, "KCCB Slot %u: Cmd=0x%08x, OSid=%u", 3) \
\
X(  1, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FWCTX_CLEANUP, "FwCommonContext [0x%08x] cleaned", 1) \
X(  2, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FWCTX_BUSY, "FwCommonContext [0x%08x] is busy: ReadOffset = %d, WriteOffset = %d", 3) \
X(  3, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANUP_DEPRECATED, "HWRTData [0x%08x] for DM=%d, received cleanup request", 2) \
X(  4, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANED_FOR_DM_DEPRECATED, "HWRTData [0x%08x] HW Context cleaned for DM%u, executed commands = %d", 3) \
X(  5, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_BUSY_DEPRECATED, "HWRTData [0x%08x] HW Context for DM%u is busy", 2) \
X(  6, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANED_DEPRECATED, "HWRTData [0x%08x] HW Context %u cleaned", 2) \
X(  7, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FL_CLEANED, "Freelist [0x%08x] cleaned", 1) \
X(  8, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_ZSBUFFER_CLEANED, "ZSBuffer [0x%08x] cleaned", 1) \
X(  9, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_ZSBUFFER_BUSY, "ZSBuffer [0x%08x] is busy: submitted = %d, executed = %d", 3) \
X( 10, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_BUSY_DEPRECATED2, "HWRTData [0x%08x] HW Context for DM%u is busy: submitted = %d, executed = %d", 4) \
X( 11, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANUP_DEPRECATED, "HW Ray Frame data [0x%08x] for DM=%d, received cleanup request", 2) \
X( 12, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANED_FOR_DM_DEPRECATED, "HW Ray Frame Data [0x%08x] cleaned for DM%u, executed commands = %d", 3) \
X( 13, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_BUSY_DEPRECATED, "HW Ray Frame Data [0x%08x] for DM%u is busy: submitted = %d, executed = %d", 4) \
X( 14, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANED_DEPRECATED, "HW Ray Frame Data [0x%08x] HW Context %u cleaned", 2) \
X( 15, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_INVALID_REQUEST, "Discarding invalid cleanup request of type 0x%x", 1) \
X( 16, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANUP, "Received cleanup request for HWRTData [0x%08x]", 1) \
X( 17, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_BUSY, "HWRTData [0x%08x] HW Context is busy: submitted = %d, executed = %d", 3) \
X( 18, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANED, "HWRTData [0x%08x] HW Context %u cleaned, executed commands = %d", 3) \
\
X(  1, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_NEEDS_RESUME, "CDM FWCtx 0x%08.8x needs resume", 1) \
X(  2, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_RESUME_DEPRECATED, "*** CDM FWCtx 0x%08.8x resume from snapshot buffer 0x%08x%08x", 3) \
X(  3, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_SHARED, "CDM FWCtx shared alloc size load 0x%x", 1) \
X(  4, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_COMPLETE, "*** CDM FWCtx store complete", 0) \
X(  5, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_START, "*** CDM FWCtx store start", 0) \
X(  6, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_SOFT_RESET, "CDM Soft Reset", 0) \
X(  7, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_NEEDS_RESUME, "3D FWCtx 0x%08.8x needs resume", 1) \
X(  8, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME, "*** 3D FWCtx 0x%08.8x resume", 1) \
X(  9, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_COMPLETE, "*** 3D context store complete", 0) \
X( 10, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE_DEPRECATED, "3D context store pipe state: 0x%08.8x 0x%08.8x 0x%08.8x", 3) \
X( 11, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_START, "*** 3D context store start", 0) \
X( 12, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_TQ_RESUME, "*** 3D TQ FWCtx 0x%08.8x resume", 1) \
X( 13, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_NEEDS_RESUME, "TA FWCtx 0x%08.8x needs resume", 1) \
X( 14, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_RESUME, "*** TA FWCtx 0x%08.8x resume from snapshot buffer 0x%08x%08x", 3) \
X( 15, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_SHARED, "TA context shared alloc size store 0x%x, load 0x%x", 2) \
X( 16, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_COMPLETE, "*** TA context store complete", 0) \
X( 17, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_START, "*** TA context store start", 0) \
X( 18, RGXFW_GROUP_CSW, RGXFW_SF_CSW_HIGHER_PRIORITY_SCHEDULED_DEPRECATED, "Higher priority context scheduled for DM %u, old prio:%d, new prio:%d", 3) \
X( 19, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SET_CONTEXT_PRIORITY, "Set FWCtx 0x%x priority to %u", 2) \
X( 20, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE_DEPRECATED2, "3D context store pipe%d state: 0x%08.8x", 2) \
X( 21, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME_PIPE_STATE_DEPRECATED, "3D context resume pipe%d state: 0x%08.8x", 2) \
X( 22, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_NEEDS_RESUME_DEPRECATED, "SHG FWCtx 0x%08.8x needs resume", 1) \
X( 23, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_RESUME_DEPRECATED, "*** SHG FWCtx 0x%08.8x resume from snapshot buffer 0x%08x%08x", 3) \
X( 24, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_SHARED_DEPRECATED, "SHG context shared alloc size store 0x%x, load 0x%x", 2) \
X( 25, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_STORE_COMPLETE_DEPRECATED, "*** SHG context store complete", 0) \
X( 26, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_STORE_START_DEPRECATED, "*** SHG context store start", 0) \
X( 27, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_PIPE_INDIRECT, "Performing TA indirection, last used pipe %d", 1) \
X( 28, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_CTRL_STREAM_TERMINATE, "CDM context store hit ctrl stream terminate. Skip resume.", 0) \
X( 29, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_RESUME_AB_BUFFER, "*** CDM FWCtx 0x%08.8x resume from snapshot buffer 0x%08x%08x, shader state %u", 4) \
X( 30, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STATE_BUFFER_FLIP, "TA PDS/USC state buffer flip (%d->%d)", 2) \
X( 31, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_52563_HIT_DEPRECATED, "TA context store hit BRN 52563: vertex store tasks outstanding", 0) \
X( 32, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_USC_POLL_FAILED, "TA USC poll failed (USC vertex task count: %d)", 1) \
X( 33, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_DEFERRED_DEPRECATED, "TA context store deferred due to BRN 54141.", 0) \
X( 34, RGXFW_GROUP_CSW, RGXFW_SF_CSW_HIGHER_PRIORITY_SCHEDULED_DEPRECATED2, "Higher priority context scheduled for DM %u. Prios (OSid, OSid Prio, Context Prio): Current: %u, %u, %u New: %u, %u, %u", 7) \
X( 35, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_START, "*** TDM context store start", 0) \
X( 36, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_COMPLETE, "*** TDM context store complete", 0) \
X( 37, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_NEEDS_RESUME_DEPRECATED, "TDM context needs resume, header [0x%08.8x, 0x%08.8x]", 2) \
X( 38, RGXFW_GROUP_CSW, RGXFW_SF_CSW_HIGHER_PRIORITY_SCHEDULED, "Higher priority context scheduled for DM %u. Prios (OSid, OSid Prio, Context Prio): Current: %u, %u, %u New: %u, %u, %u. Hard Context Switching: %u", 8) \
X( 39, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE, "3D context store pipe %2d (%2d) state: 0x%08.8x", 3) \
X( 40, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME_PIPE_STATE, "3D context resume pipe %2d (%2d) state: 0x%08.8x", 3) \
X( 41, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_START_VOLCANIC, "*** 3D context store start version %d (1=IPP_TILE, 2=ISP_TILE)", 1) \
X( 42, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE_VOLCANIC, "3D context store pipe%d state: 0x%08.8x%08x", 3) \
X( 43, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME_PIPE_STATE_VOLCANIC, "3D context resume pipe%d state: 0x%08.8x%08x", 3) \
X( 44, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME_IPP_STATE,  "3D context resume IPP state: 0x%08.8x%08x", 2) \
X( 45, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_PIPES_EMPTY, "All 3D pipes empty after ISP tile mode store! IPP_status: 0x%08x", 1) \
X( 46, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_RESUME_PIPE_STATE_DEPRECATED, "TDM context resume pipe%d state: 0x%08.8x%08x", 3) \
X( 47, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_LEVEL4_STORE_START, "*** 3D context store start version 4", 0) \
X( 48, RGXFW_GROUP_CSW, RGXFW_SF_CSW_RESUME_MULTICORE, "Multicore context resume on DM%d active core mask 0x%04.4x", 2) \
X( 49, RGXFW_GROUP_CSW, RGXFW_SF_CSW_STORE_MULTICORE, "Multicore context store on DM%d active core mask 0x%04.4x", 2) \
X( 50, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_RESUME_PIPE_STATE, "TDM context resume Core %d, pipe%d state: 0x%08.8x%08x%08x", 5) \
X( 51, RGXFW_GROUP_CSW, RGXFW_SF_CSW_RDM_STORE_COMPLETE, "*** RDM FWCtx store complete", 0) \
X( 52, RGXFW_GROUP_CSW, RGXFW_SF_CSW_RDM_STORE_START, "*** RDM FWCtx store start", 0) \
X( 53, RGXFW_GROUP_CSW, RGXFW_SF_CSW_RDM_NEEDS_RESUME, "RDM FWCtx 0x%08.8x needs resume", 1) \
X( 54, RGXFW_GROUP_CSW, RGXFW_SF_CSW_RDM_RESUME, "RDM FWCtx 0x%08.8x resume", 1) \
\
X(  1, RGXFW_GROUP_BIF, RGXFW_SF_BIF_ACTIVATE_BIFREQ_DEPRECATED, "Activate MemCtx=0x%08x BIFreq=%d secure=%d", 3) \
X(  2, RGXFW_GROUP_BIF, RGXFW_SF_BIF_DEACTIVATE, "Deactivate MemCtx=0x%08x", 1) \
X(  3, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_ALLOC, "Alloc PC reg %d", 1) \
X(  4, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_GRAB, "Grab reg %d refcount now %d", 2) \
X(  5, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_UNGRAB, "Ungrab reg %d refcount now %d", 2) \
X(  6, RGXFW_GROUP_BIF, RGXFW_SF_BIF_SETUP_REG_BIFREQ_DEPRECATED, "Setup reg=%d BIFreq=%d, expect=0x%08x%08x, actual=0x%08x%08x", 6) \
X(  7, RGXFW_GROUP_BIF, RGXFW_SF_BIF_TRUST_DEPRECATED, "Trust enabled:%d, for BIFreq=%d", 2) \
X(  8, RGXFW_GROUP_BIF, RGXFW_SF_BIF_TILECFG_DEPRECATED, "BIF Tiling Cfg %d base 0x%08x%08x len 0x%08x%08x enable %d stride %d --> 0x%08x%08x", 9) \
X(  9, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSID0, "Wrote the Value %d to OSID0, Cat Base %d, Register's contents are now 0x%08x 0x%08x", 4) \
X( 10, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSID1, "Wrote the Value %d to OSID1, Context  %d, Register's contents are now 0x%04x", 3) \
X( 11, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSIDx, "ui32OSid = %u, Catbase = %u, Reg Address = 0x%x, Reg index = %u, Bitshift index = %u, Val = 0x%08x%08x", 7) \
X( 12, RGXFW_GROUP_BIF, RGXFW_SF_BIF_MAP_GPU_MEMORY_BIFREQ_DEPRECATED, "Map GPU memory DevVAddr 0x%x%08x, Size %u, Context ID %u, BIFREQ %u", 5) \
X( 13, RGXFW_GROUP_BIF, RGXFW_SF_BIF_UNMAP_GPU_MEMORY, "Unmap GPU memory (event status 0x%x)", 1) \
X( 14, RGXFW_GROUP_BIF, RGXFW_SF_BIF_ACTIVATE_DM, "Activate MemCtx=0x%08x DM=%d secure=%d", 3) \
X( 15, RGXFW_GROUP_BIF, RGXFW_SF_BIF_SETUP_REG_DM, "Setup reg=%d DM=%d, expect=0x%08x%08x, actual=0x%08x%08x", 6) \
X( 16, RGXFW_GROUP_BIF, RGXFW_SF_BIF_MAP_GPU_MEMORY, "Map GPU memory DevVAddr 0x%x%08x, Size %u, Context ID %u", 4) \
X( 17, RGXFW_GROUP_BIF, RGXFW_SF_BIF_TRUST_DM, "Trust enabled:%d, for DM=%d", 2) \
X( 18, RGXFW_GROUP_BIF, RGXFW_SF_BIF_MAP_GPU_MEMORY_DM, "Map GPU memory DevVAddr 0x%x%08x, Size %u, Context ID %u, DM %u", 5) \
\
X(  1, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_WRITE, "GPIO write 0x%02x", 1) \
X(  2, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_READ, "GPIO read 0x%02x", 1) \
X(  3, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_ENABLED, "GPIO enabled", 0) \
X(  4, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_DISABLED, "GPIO disabled", 0) \
X(  5, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_STATUS, "GPIO status=%d (0=OK, 1=Disabled)", 1) \
X(  6, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_AP_READ, "GPIO_AP: Read address=0x%02x (%d byte(s))", 2) \
X(  7, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_AP_WRITE, "GPIO_AP: Write address=0x%02x (%d byte(s))", 2) \
X(  8, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_AP_TIMEOUT, "GPIO_AP timeout!", 0) \
X(  9, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_AP_ERROR, "GPIO_AP error. GPIO status=%d (0=OK, 1=Disabled)", 1) \
X( 10, RGXFW_GROUP_MISC, RGXFW_SF_MISC_GPIO_ALREADY_READ, "GPIO already read 0x%02x", 1) \
X( 11, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_CHECK_BUFFER_AVAILABLE, "SR: Check buffer %d available returned %d", 2) \
X( 12, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_WAITING_BUFFER_AVAILABLE, "SR: Waiting for buffer %d", 1) \
X( 13, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_WAIT_BUFFER_TIMEOUT, "SR: Timeout waiting for buffer %d (after %d ticks)", 2) \
X( 14, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_SKIP_FRAME_CHECK, "SR: Skip frame check for strip %d returned %d (0=No skip, 1=Skip frame)", 2) \
X( 15, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_SKIP_REMAINING_STRIPS, "SR: Skip remaining strip %d in frame", 1) \
X( 16, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_FRAME_SKIP_NEW_FRAME, "SR: Inform HW that strip %d is a new frame", 1) \
X( 17, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_SKIP_FRAME_TIMEOUT, "SR: Timeout waiting for INTERRUPT_FRAME_SKIP (after %d ticks)", 1) \
X( 18, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_STRIP_MODE, "SR: Strip mode is %d", 1) \
X( 19, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_STRIP_INDEX, "SR: Strip Render start (strip %d)", 1) \
X( 20, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_BUFFER_RENDERED, "SR: Strip Render complete (buffer %d)", 1) \
X( 21, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SR_BUFFER_FAULT, "SR: Strip Render fault (buffer %d)", 1) \
X( 22, RGXFW_GROUP_MISC, RGXFW_SF_MISC_TRP_STATE, "TRP state: %d", 1) \
X( 23, RGXFW_GROUP_MISC, RGXFW_SF_MISC_TRP_FAILURE, "TRP failure: %d", 1) \
X( 24, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SW_TRP_STATE, "SW TRP State: %d", 1) \
X( 25, RGXFW_GROUP_MISC, RGXFW_SF_MISC_SW_TRP_FAILURE, "SW TRP failure: %d", 1) \
X( 26, RGXFW_GROUP_MISC, RGXFW_SF_MISC_HW_KICK, "HW kick event (%u)", 1) \
X( 27, RGXFW_GROUP_MISC, RGXFW_SF_MISC_WGP_CHECKSUMS, "GPU core (%u/%u): checksum 0x%08x vs. 0x%08x", 4) \
X( 28, RGXFW_GROUP_MISC, RGXFW_SF_MISC_WGP_UNIT_CHECKSUMS, "GPU core (%u/%u), unit (%u,%u): checksum 0x%08x vs. 0x%08x", 6) \
X( 29, RGXFW_GROUP_MISC, RGXFW_SF_MISC_HWR_CHECK_REG, "HWR: Core%u, Register=0x%08x, OldValue=0x%08x%08x, CurrValue=0x%08x%08x", 6) \
X( 30, RGXFW_GROUP_MISC, RGXFW_SF_MISC_HWR_USC_SLOTS_CHECK, "HWR: USC Core%u, ui32TotalSlotsUsedByDM=0x%08x, psDMHWCtl->ui32USCSlotsUsedByDM=0x%08x, bHWRNeeded=%u", 4) \
X( 31, RGXFW_GROUP_MISC, RGXFW_SF_MISC_HWR_USC_REG_CHECK, "HWR: USC Core%u, Register=0x%08x, OldValue=0x%08x%08x, CurrValue=0x%08x%08x", 6) \
\
X(  1, RGXFW_GROUP_PM, RGXFW_SF_PM_AMLIST, "ALIST%d SP = %u, MLIST%d SP = %u (VCE 0x%08x%08x, TE 0x%08x%08x, ALIST 0x%08x%08x)", 10) \
X(  2, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_SHARED_DEPRECATED, "Is TA: %d, finished: %d on HW %u (HWRTData = 0x%08x, MemCtx = 0x%08x). FL different between TA/3D: global:%d, local:%d, mmu:%d", 8) \
X(  3, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_3DBASE_DEPRECATED, "UFL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), MFL-3D-Base: 0x%08x%08x (SP = %u, 4PT = %u)", 14) \
X(  4, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_TABASE_DEPRECATED, "UFL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), MFL-TA-Base: 0x%08x%08x (SP = %u, 4PT = %u)", 14) \
X(  5, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_GROW_COMPLETE_DEPRECATED, "Freelist grow completed [0x%08x]: added pages 0x%08x, total pages 0x%08x, new DevVirtAddr 0x%08x%08x", 5) \
X(  6, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_GROW_DENIED_DEPRECATED, "Grow for freelist ID=0x%08x denied by host", 1) \
X(  7, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_UPDATE_COMPLETE, "Freelist update completed [0x%08x]: old total pages 0x%08x, new total pages 0x%08x, new DevVirtAddr 0x%08x%08x", 5) \
X(  8, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_RECONSTRUCTION_FAILED_DEPRECATED, "Reconstruction of freelist ID=0x%08x failed", 1) \
X(  9, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_WARNING, "Ignored attempt to pause or unpause the DM while there is no relevant operation in progress (0-TA,1-3D): %d, operation(0-unpause, 1-pause): %d", 2) \
X( 10, RGXFW_GROUP_PM, RGXFW_SF_PM_3D_TIMEOUT_STATUS, "Force free 3D Context memory, FWCtx: 0x%08x, status(1:success, 0:fail): %d", 2)\
X( 11, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_ALLOC, "PM pause TA ALLOC: PM_PAGE_MANAGEOP set to 0x%x", 1) \
X( 12, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_UNPAUSE_ALLOC, "PM unpause TA ALLOC: PM_PAGE_MANAGEOP set to 0x%x", 1) \
X( 13, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_DALLOC, "PM pause 3D DALLOC: PM_PAGE_MANAGEOP set to 0x%x", 1) \
X( 14, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_UNPAUSE_DALLOC, "PM unpause 3D DALLOC: PM_PAGE_MANAGEOP set to 0x%x", 1) \
X( 15, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_FAILED, "PM ALLOC/DALLOC change was not actioned: PM_PAGE_MANAGEOP_STATUS=0x%x", 1) \
X( 16, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_SHARED, "Is TA: %d, finished: %d on HW %u (HWRTData = 0x%08x, MemCtx = 0x%08x). FL different between TA/3D: global:%d, local:%d", 7) \
X( 17, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_3DBASE, "UFL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u)", 10) \
X( 18, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_TABASE, "UFL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u)", 10) \
X( 19, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_UPDATE_COMPLETE_VOLCANIC, "Freelist update completed [0x%08x / FL State 0x%08x%08x]: old total pages 0x%08x, new total pages 0x%08x, new DevVirtAddr 0x%08x%08x", 7) \
X( 20, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_UPDATE_FAILED, "Freelist update failed [0x%08x / FL State 0x%08x%08x]: old total pages 0x%08x, new total pages 0x%08x, new DevVirtAddr 0x%08x%08x", 7) \
X( 21, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_3DBASE_VOLCANIC, "UFL-3D-State-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-3D-State-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u)", 10) \
X( 22, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_TABASE_VOLCANIC, "UFL-TA-State-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-TA-State-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u)", 10) \
X( 23, RGXFW_GROUP_PM, RGXFW_SF_PM_CHECK_FL_BASEADDR, "Freelist 0x%08x base address from HW: 0x%02x%08x (expected value: 0x%02x%08x)", 5) \
X( 24, RGXFW_GROUP_PM, RGXFW_SF_PM_ANALYSE_FL_GROW, "Analysis of FL grow: Pause=(%u,%u) Paused+Valid(%u,%u) PMStateBuffer=0x%x", 5) \
X( 25, RGXFW_GROUP_PM, RGXFW_SF_PM_ATTEMPT_FL_GROW, "Attempt FL grow for FL: 0x%08x, new dev address: 0x%02x%08x, new page count: %u, new ready count: %u", 5) \
X( 26, RGXFW_GROUP_PM, RGXFW_SF_PM_DEFER_FL_GROW, "Deferring FL grow for non-loaded FL: 0x%08x, new dev address: 0x%02x%08x, new page count: %u, new ready count: %u", 5) \
X( 27, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_SHARED_ALBIORIX, "Is GEOM: %d, finished: %d (HWRTData = 0x%08x, MemCtx = 0x%08x)", 4) \
X( 28, RGXFW_GROUP_PM, RGXFW_SF_PM_3D_TIMEOUT, "3D Timeout Now for FWCtx 0x%08.8x", 1) \
X( 29, RGXFW_GROUP_PM, RGXFW_SF_PM_RECYCLE, "GEOM PM Recycle for FWCtx 0x%08.8x", 1) \
\
X(  1, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GLL_DYNAMIC_STATUS_DEPRECATED, "Global link list dynamic page count: vertex 0x%x, varying 0x%x, node 0x%x", 3) \
X(  2, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GLL_STATIC_STATUS_DEPRECATED, "Global link list static page count: vertex 0x%x, varying 0x%x, node 0x%x", 3) \
X(  3, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_WAIT_FOR_GROW_DEPRECATED, "RPM request failed. Waiting for freelist grow.", 0) \
X(  4, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_ABORT_DEPRECATED, "RPM request failed. Aborting the current frame.", 0) \
X(  5, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_WAIT_FOR_PENDING_GROW_DEPRECATED, "RPM waiting for pending grow on freelist 0x%08x", 1) \
X(  6, RGXFW_GROUP_RPM, RGXFW_SF_RPM_REQUEST_HOST_GROW_DEPRECATED, "Request freelist grow [0x%08x] current pages %d, grow size %d", 3) \
X(  7, RGXFW_GROUP_RPM, RGXFW_SF_RPM_FREELIST_LOAD_DEPRECATED, "Freelist load: SHF = 0x%08x, SHG = 0x%08x", 2) \
X(  8, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_DEPRECATED, "SHF FPL register: 0x%08x.0x%08x", 2) \
X(  9, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_DEPRECATED, "SHG FPL register: 0x%08x.0x%08x", 2) \
X( 10, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_FREELIST_DEPRECATED, "Kernel requested RPM grow on freelist (type %d) at 0x%08x from current size %d to new size %d, RPM restart: %d (1=Yes)", 5) \
X( 11, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_RESTART_DEPRECATED, "Restarting SHG", 0) \
X( 12, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_ABORTED_DEPRECATED, "Grow failed, aborting the current frame.", 0) \
X( 13, RGXFW_GROUP_RPM, RGXFW_SF_RPM_ABORT_COMPLETE_DEPRECATED, "RPM abort complete on HWFrameData [0x%08x].", 1) \
X( 14, RGXFW_GROUP_RPM, RGXFW_SF_RPM_CLEANUP_NEEDS_ABORT_DEPRECATED, "RPM freelist cleanup [0x%08x] requires abort to proceed.", 1) \
X( 15, RGXFW_GROUP_RPM, RGXFW_SF_RPM_RPM_PT_DEPRECATED, "RPM page table base register: 0x%08x.0x%08x", 2) \
X( 16, RGXFW_GROUP_RPM, RGXFW_SF_RPM_OOM_ABORT_DEPRECATED, "Issuing RPM abort.", 0) \
X( 17, RGXFW_GROUP_RPM, RGXFW_SF_RPM_OOM_TOGGLE_CHECK_FULL_DEPRECATED, "RPM OOM received but toggle bits indicate free pages available", 0) \
X( 18, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_HW_TIMEOUT_DEPRECATED, "RPM hardware timeout. Unable to process OOM event.", 0) \
X( 19, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_LOAD_DEPRECATED_DEPRECATED, "SHF FL (0x%08x) load, FPL: 0x%08x.0x%08x, roff: 0x%08x, woff: 0x%08x", 5) \
X( 20, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_LOAD_DEPRECATED, "SHG FL (0x%08x) load, FPL: 0x%08x.0x%08x, roff: 0x%08x, woff: 0x%08x", 5) \
X( 21, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_STORE_DEPRECATED, "SHF FL (0x%08x) store, roff: 0x%08x, woff: 0x%08x", 3) \
X( 22, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_STORE_DEPRECATED, "SHG FL (0x%08x) store, roff: 0x%08x, woff: 0x%08x", 3) \
\
X(  1, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_FINISHED, "3D RTData 0x%08x finished on HW context %u", 2) \
X(  2, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_READY, "3D RTData 0x%08x ready on HW context %u", 2) \
X(  3, RGXFW_GROUP_RTD, RGXFW_SF_RTD_PB_SET_TO_DEPRECATED, "CONTEXT_PB_BASE set to 0x%x, FL different between TA/3D: local: %d, global: %d, mmu: %d", 4) \
X(  4, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOADVFP_3D_DEPRECATED, "Loading VFP table 0x%08x%08x for 3D", 2) \
X(  5, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOADVFP_TA_DEPRECATED, "Loading VFP table 0x%08x%08x for TA", 2) \
X(  6, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL_DEPRECATED, "Load Freelist 0x%x type: %d (0:local,1:global,2:mmu) for DM%d: TotalPMPages = %d, FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 10) \
X(  7, RGXFW_GROUP_RTD, RGXFW_SF_RTD_VHEAP_STORE, "Perform VHEAP table store", 0) \
X(  8, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_MATCH_FOUND, "RTData 0x%08x: found match in Context=%d: Load=No, Store=No", 2) \
X(  9, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_NULL_FOUND, "RTData 0x%08x: found NULL in Context=%d: Load=Yes, Store=No", 2) \
X( 10, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_3D_FINISHED, "RTData 0x%08x: found state 3D finished (0x%08x) in Context=%d: Load=Yes, Store=Yes", 3) \
X( 11, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_TA_FINISHED, "RTData 0x%08x: found state TA finished (0x%08x) in Context=%d: Load=Yes, Store=Yes", 3) \
X( 12, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_STACK_POINTERS, "Loading stack-pointers for %d (0:MidTA,1:3D) on context %d, MLIST = 0x%08x, ALIST = 0x%08x%08x", 5) \
X( 13, RGXFW_GROUP_RTD, RGXFW_SF_RTD_STORE_PB_DEPRECATED, "Store Freelist 0x%x type: %d (0:local,1:global,2:mmu) for DM%d: TotalPMPages = %d, FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 10) \
X( 14, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_FINISHED, "TA RTData 0x%08x finished on HW context %u", 2) \
X( 15, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_LOADED, "TA RTData 0x%08x loaded on HW context %u", 2) \
X( 16, RGXFW_GROUP_RTD, RGXFW_SF_RTD_STORE_PB_DEPRECATED2, "Store Freelist 0x%x type: %d (0:local,1:global,2:mmu) for DM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 12) \
X( 17, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL_DEPRECATED2, "Load  Freelist 0x%x type: %d (0:local,1:global,2:mmu) for DM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 12) \
X( 18, RGXFW_GROUP_RTD, RGXFW_SF_RTD_DEBUG_DEPRECATED, "Freelist 0x%x RESET!!!!!!!!", 1) \
X( 19, RGXFW_GROUP_RTD, RGXFW_SF_RTD_DEBUG2_DEPRECATED, "Freelist 0x%x stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 5) \
X( 20, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_DEPRECATED, "Request reconstruction of Freelist 0x%x type: %d (0:local,1:global,2:mmu) on HW context %u", 3) \
X( 21, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_ACK_DEPRECATED, "Freelist reconstruction ACK from host (HWR state :%u)", 1) \
X( 22, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_ACK_DEPRECATED2, "Freelist reconstruction completed", 0) \
X( 23, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_LOADED_DEPRECATED, "TA RTData 0x%08x loaded on HW context %u HWRTDataNeedsLoading=%d", 3) \
X( 24, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TE_RGNHDR_INFO, "TE Region headers base 0x%08x%08x (RGNHDR Init: %d)", 3) \
X( 25, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_BUFFER_ADDRS_DEPRECATED, "TA Buffers: FWCtx 0x%08x, RT 0x%08x, RTData 0x%08x, VHeap 0x%08x%08x, TPC 0x%08x%08x (MemCtx 0x%08x)", 8) \
X( 26, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_LOADED_DEPRECATED, "3D RTData 0x%08x loaded on HW context %u", 2) \
X( 27, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_BUFFER_ADDRS_DEPRECATED, "3D Buffers: FWCtx 0x%08x, RT 0x%08x, RTData 0x%08x (MemCtx 0x%08x)", 4) \
X( 28, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RESTART_AFTER_PR_EXECUTED, "Restarting TA after partial render, HWRTData0State=0x%x, HWRTData1State=0x%x", 2) \
X( 29, RGXFW_GROUP_RTD, RGXFW_SF_RTD_PB_SET_TO, "CONTEXT_PB_BASE set to 0x%x, FL different between TA/3D: local: %d, global: %d", 3) \
X( 30, RGXFW_GROUP_RTD, RGXFW_SF_RTD_STORE_FL, "Store Freelist 0x%x type: %d (0:local,1:global) for PMDM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 12) \
X( 31, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL, "Load  Freelist 0x%x type: %d (0:local,1:global) for PMDM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u", 12) \
X( 32, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_BUFFER_ADDRS_DEPRECATED2, "3D Buffers: FWCtx 0x%08x, parent RT 0x%08x, RTData 0x%08x on ctx %d, (MemCtx 0x%08x)", 5) \
X( 33, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_BUFFER_ADDRS, "TA Buffers: FWCtx 0x%08x, RTData 0x%08x, VHeap 0x%08x%08x, TPC 0x%08x%08x (MemCtx 0x%08x)", 7) \
X( 34, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_BUFFER_ADDRS, "3D Buffers: FWCtx 0x%08x, RTData 0x%08x on ctx %d, (MemCtx 0x%08x)", 4) \
X( 35, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL_V2, "Load  Freelist 0x%x type: %d (0:local,1:global) for PMDM%d: FL Total Pages %u (max=%u,grow size=%u)", 6) \
X( 36, RGXFW_GROUP_RTD, RGXFW_SF_RTD_KILLED_TA, "TA RTData 0x%08x marked as killed.", 1) \
X( 37, RGXFW_GROUP_RTD, RGXFW_SF_RTD_KILLED_3D, "3D RTData 0x%08x marked as killed.", 1) \
X( 38, RGXFW_GROUP_RTD, RGXFW_SF_RTD_KILL_TA_AFTER_RESTART, "RTData 0x%08x will be killed after TA restart.", 1) \
X( 39, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RENDERSTATE_RESET, "RTData 0x%08x Render State Buffer 0x%08x%08x will be reset.", 3) \
\
X(  1, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZLOAD_DEPRECATED, "Force Z-Load for partial render", 0) \
X(  2, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSTORE_DEPRECATED, "Force Z-Store for partial render", 0) \
X(  3, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_LOCAL_DEPRECATED, "3D MemFree: Local FL 0x%08x", 1) \
X(  4, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_MMU_DEPRECATED, "3D MemFree: MMU FL 0x%08x", 1) \
X(  5, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_GLOBAL_DEPRECATED, "3D MemFree: Global FL 0x%08x", 1) \
X(  6, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD_DEPRECATED, "OOM TA/3D PR Check: [0x%08.8x] is 0x%08.8x requires 0x%08.8x, HardwareSync Fence [0x%08.8x] is 0x%08.8x requires 0x%08.8x", 6) \
X(  7, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD_UN_FL, "OOM TA_cmd=0x%08x, U-FL 0x%08x, N-FL 0x%08x", 3) \
X(  8, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD_UN_MMU_FL_DEPRECATED, "OOM TA_cmd=0x%08x, OOM MMU:%d, U-FL 0x%08x, N-FL 0x%08x, MMU-FL 0x%08x", 5) \
X(  9, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_AVOIDED_DEPRECATED, "Partial render avoided", 0) \
X( 10, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_DISCARDED_DEPRECATED, "Partial render discarded", 0) \
X( 11, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_FINISHED, "Partial Render finished", 0) \
X( 12, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_3DBG_DEPRECATED, "SPM Owner = 3D-BG", 0) \
X( 13, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_3DIRQ_DEPRECATED, "SPM Owner = 3D-IRQ", 0) \
X( 14, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_NONE_DEPRECATED, "SPM Owner = NONE", 0) \
X( 15, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_TABG_DEPRECATED, "SPM Owner = TA-BG", 0) \
X( 16, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_TAIRQ_DEPRECATED, "SPM Owner = TA-IRQ", 0) \
X( 17, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSTORE_ADDRESS, "ZStore address 0x%08x%08x", 2) \
X( 18, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SSTORE_ADDRESS, "SStore address 0x%08x%08x", 2) \
X( 19, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZLOAD_ADDRESS, "ZLoad address 0x%08x%08x", 2) \
X( 20, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SLOAD_ADDRESS, "SLoad address 0x%08x%08x", 2) \
X( 21, RGXFW_GROUP_SPM, RGXFW_SF_SPM_NO_DEFERRED_ZSBUFFER_DEPRECATED, "No deferred ZS Buffer provided", 0) \
X( 22, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_POPULATED, "ZS Buffer successfully populated (ID=0x%08x)", 1) \
X( 23, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_POP_UNNEEDED_DEPRECATED, "No need to populate ZS Buffer (ID=0x%08x)", 1) \
X( 24, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNPOPULATED, "ZS Buffer successfully unpopulated (ID=0x%08x)", 1) \
X( 25, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNPOP_UNNEEDED_DEPRECATED, "No need to unpopulate ZS Buffer (ID=0x%08x)", 1) \
X( 26, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST_DEPRECATED, "Send ZS-Buffer backing request to host (ID=0x%08x)", 1) \
X( 27, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNBACKING_REQUEST_DEPRECATED, "Send ZS-Buffer unbacking request to host (ID=0x%08x)", 1) \
X( 28, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST_PENDING_DEPRECATED, "Don't send ZS-Buffer backing request. Previous request still pending (ID=0x%08x)", 1) \
X( 29, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNBACKING_REQUEST_PENDING_DEPRECATED, "Don't send ZS-Buffer unbacking request. Previous request still pending (ID=0x%08x)", 1) \
X( 30, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZBUFFER_NOT_READY_DEPRECATED, "Partial Render waiting for ZBuffer to be backed (ID=0x%08x)", 1) \
X( 31, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SBUFFER_NOT_READY_DEPRECATED, "Partial Render waiting for SBuffer to be backed (ID=0x%08x)", 1) \
X( 32, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_NONE, "SPM State = none", 0) \
X( 33, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_BLOCKED, "SPM State = PR blocked", 0) \
X( 34, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_GROW, "SPM State = wait for grow", 0) \
X( 35, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_HW, "SPM State = wait for HW", 0) \
X( 36, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_RUNNING, "SPM State = PR running", 0) \
X( 37, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_AVOIDED, "SPM State = PR avoided", 0) \
X( 38, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_EXECUTED, "SPM State = PR executed", 0) \
X( 39, RGXFW_GROUP_SPM, RGXFW_SF_SPM_FREELIST_MATCH, "3DMemFree matches freelist 0x%08x (FL type = %u)", 2) \
X( 40, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_FLAG_SET, "Raise the 3DMemFreeDedected flag", 0) \
X( 41, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_PENDING_GROW, "Wait for pending grow on Freelist 0x%08x", 1) \
X( 42, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST_FAILED, "ZS Buffer failed to be populated (ID=0x%08x)", 1) \
X( 43, RGXFW_GROUP_SPM, RGXFW_SF_SPM_FL_GROW_DEBUG, "Grow update inconsistency: FL addr: 0x%02x%08x, curr pages: %u, ready: %u, new: %u", 5) \
X( 44, RGXFW_GROUP_SPM, RGXFW_SF_SPM_RESUMED_TA_WITH_SP, "OOM: Resumed TA with ready pages, FL addr: 0x%02x%08x, current pages: %u, SP : %u", 4) \
X( 45, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ACK_GROW_UPDATE_DEPRECATED, "Received grow update, FL addr: 0x%02x%08x, current pages: %u, ready pages: %u, threshold: %u", 5) \
X( 46, RGXFW_GROUP_SPM, RGXFW_SF_SPM_NO_DEFERRED_PRBUFFER, "No deferred partial render FW (Type=%d) Buffer provided", 1) \
X( 47, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_POP_UNNEEDED, "No need to populate PR Buffer (ID=0x%08x)", 1) \
X( 48, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_UNPOP_UNNEEDED, "No need to unpopulate PR Buffer (ID=0x%08x)", 1) \
X( 49, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_BACKING_REQUEST, "Send PR Buffer backing request to host (ID=0x%08x)", 1) \
X( 50, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_UNBACKING_REQUEST, "Send PR Buffer unbacking request to host (ID=0x%08x)", 1) \
X( 51, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_BACKING_REQUEST_PENDING, "Don't send PR Buffer backing request. Previous request still pending (ID=0x%08x)", 1) \
X( 52, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_UNBACKING_REQUEST_PENDING, "Don't send PR Buffer unbacking request. Previous request still pending (ID=0x%08x)", 1) \
X( 53, RGXFW_GROUP_SPM, RGXFW_SF_SPM_BUFFER_NOT_READY, "Partial Render waiting for Buffer %d type to be backed (ID=0x%08x)", 2) \
X( 54, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ACK_GROW_UPDATE, "Received grow update, FL addr: 0x%02x%08x, new pages: %u, ready pages: %u", 4) \
X( 66, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD, "OOM TA/3D PR Check: [0x%08.8x] is 0x%08.8x requires 0x%08.8x", 3) \
X( 67, RGXFW_GROUP_SPM, RGXFW_SF_SPM_RESUMED_TA, "OOM: Resumed TA with ready pages, FL addr: 0x%02x%08x, current pages: %u", 3) \
X( 68, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PR_DEADLOCK_UNBLOCKED, "OOM TA/3D PR deadlock unblocked reordering DM%d runlist head from Context 0x%08x to 0x%08x", 3) \
\
X(  1, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECK_DEPRECATED, "Check Pow state DM%d int: 0x%x, ext: 0x%x, pow flags: 0x%x", 4) \
X(  2, RGXFW_GROUP_POW, RGXFW_SF_POW_GPU_IDLE, "GPU idle (might be powered down). Pow state int: 0x%x, ext: 0x%x, flags: 0x%x", 3) \
X(  3, RGXFW_GROUP_POW, RGXFW_SF_POW_OSREQ_DEPRECATED, "OS requested pow off (forced = %d), DM%d, pow flags: 0x%x", 3) \
X(  4, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_DEPRECATED, "Initiate powoff query. Inactive DMs: %d %d %d %d", 4) \
X(  5, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECKOFF_DEPRECATED, "Any RD-DM pending? %d, Any RD-DM Active? %d", 2) \
X(  6, RGXFW_GROUP_POW, RGXFW_SF_POW_GPU_OFF, "GPU ready to be powered down. Pow state int: 0x%x, ext: 0x%x, flags: 0x%x", 3) \
X(  7, RGXFW_GROUP_POW, RGXFW_SF_POW_HWREQ, "HW Request On(1)/Off(0): %d, Units: 0x%08.8x", 2) \
X(  8, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE_REQ, "Request to change num of dusts to %d (Power flags=%d)", 2) \
X(  9, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE, "Changing number of dusts from %d to %d", 2) \
X( 11, RGXFW_GROUP_POW, RGXFW_SF_POW_SIDEKICK_INIT_DEPRECATED, "Sidekick init", 0) \
X( 12, RGXFW_GROUP_POW, RGXFW_SF_POW_RD_INIT_DEPRECATED, "Rascal+Dusts init (# dusts mask: 0x%x)", 1) \
X( 13, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_RD, "Initiate powoff query for RD-DMs.", 0) \
X( 14, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_TLA, "Initiate powoff query for TLA-DM.", 0) \
X( 15, RGXFW_GROUP_POW, RGXFW_SF_POW_REQUESTEDOFF_RD, "Any RD-DM pending? %d, Any RD-DM Active? %d", 2) \
X( 16, RGXFW_GROUP_POW, RGXFW_SF_POW_REQUESTEDOFF_TLA, "TLA-DM pending? %d, TLA-DM Active? %d", 2) \
X( 17, RGXFW_GROUP_POW, RGXFW_SF_POW_BRN37270_DEPRECATED, "Request power up due to BRN37270. Pow stat int: 0x%x", 1) \
X( 18, RGXFW_GROUP_POW, RGXFW_SF_POW_REQ_CANCEL, "Cancel power off request int: 0x%x, ext: 0x%x, pow flags: 0x%x", 3) \
X( 19, RGXFW_GROUP_POW, RGXFW_SF_POW_FORCED_IDLE, "OS requested forced IDLE, pow flags: 0x%x", 1) \
X( 20, RGXFW_GROUP_POW, RGXFW_SF_POW_CANCEL_FORCED_IDLE, "OS cancelled forced IDLE, pow flags: 0x%x", 1) \
X( 21, RGXFW_GROUP_POW, RGXFW_SF_POW_IDLE_TIMER, "Idle timer start. Pow state int: 0x%x, ext: 0x%x, flags: 0x%x", 3) \
X( 22, RGXFW_GROUP_POW, RGXFW_SF_POW_CANCEL_IDLE_TIMER, "Cancel idle timer. Pow state int: 0x%x, ext: 0x%x, flags: 0x%x", 3) \
X( 23, RGXFW_GROUP_POW, RGXFW_SF_POW_APM_LATENCY_CHANGE, "Active PM latency set to %dms. Core clock: %d Hz", 2) \
X( 24, RGXFW_GROUP_POW, RGXFW_SF_POW_CDM_CLUSTERS, "Compute cluster mask change to 0x%x, %d dusts powered.", 2) \
X( 25, RGXFW_GROUP_POW, RGXFW_SF_POW_NULL_CMD_INIOFF_RD, "Null command executed, repeating initiate powoff query for RD-DMs.", 0) \
X( 26, RGXFW_GROUP_POW, RGXFW_SF_POW_POWMON_ENERGY, "Power monitor: Estimate of dynamic energy %u", 1) \
X( 27, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECK_DEPRECATED2, "Check Pow state: Int: 0x%x, Ext: 0x%x, Pow flags: 0x%x", 3) \
X( 28, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_NEW_DEADLINE, "Proactive DVFS: New deadline, time = 0x%08x%08x", 2) \
X( 29, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_NEW_WORKLOAD, "Proactive DVFS: New workload, cycles = 0x%08x%08x", 2) \
X( 30, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_CALCULATE, "Proactive DVFS: Proactive frequency calculated = %u", 1) \
X( 31, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UTILISATION, "Proactive DVFS: Reactive utilisation = %u percent", 1) \
X( 32, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_REACT, "Proactive DVFS: Reactive frequency calculated = %u.%u", 2) \
X( 33, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GPIO_SEND_DEPRECATED, "Proactive DVFS: OPP Point Sent = 0x%x", 1) \
X( 34, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_DEADLINE_REMOVED, "Proactive DVFS: Deadline removed = 0x%08x%08x", 2) \
X( 35, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_WORKLOAD_REMOVED, "Proactive DVFS: Workload removed = 0x%08x%08x", 2) \
X( 36, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_THROTTLE, "Proactive DVFS: Throttle to a maximum = 0x%x", 1) \
X( 37, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GPIO_FAILURE, "Proactive DVFS: Failed to pass OPP point via GPIO.", 0) \
X( 38, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_INVALID_NODE_DEPRECATED, "Proactive DVFS: Invalid node passed to function.", 0) \
X( 39, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GUEST_BAD_ACCESS_DEPRECATED, "Proactive DVFS: Guest OS attempted to do a privileged action. OSid = %u", 1) \
X( 40, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UNPROFILED_STARTED, "Proactive DVFS: Unprofiled work started. Total unprofiled work present: %u", 1) \
X( 41, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UNPROFILED_FINISHED, "Proactive DVFS: Unprofiled work finished. Total unprofiled work present: %u", 1) \
X( 42, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_DISABLED, "Proactive DVFS: Disabled: Not enabled by host.", 0) \
X( 43, RGXFW_GROUP_POW, RGXFW_SF_POW_HWREQ_RESULT, "HW Request Completed(1)/Aborted(0): %d, Ticks: %d", 2) \
X( 44, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE_FIX_59042_DEPRECATED, "Allowed number of dusts is %d due to BRN59042.", 1) \
X( 45, RGXFW_GROUP_POW, RGXFW_SF_POW_HOST_TIMEOUT_NOTIFICATION, "Host timed out while waiting for a forced idle state. Pow state int: 0x%x, ext: 0x%x, flags: 0x%x", 3) \
X( 46, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECK, "Check Pow state: Int: 0x%x, Ext: 0x%x, Pow flags: 0x%x, Fence Counters: Check: %u - Update: %u", 5) \
X( 47, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GPIO_SEND, "Proactive DVFS: OPP Point Sent = 0x%x, Success = 0x%x", 2) \
X( 48, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_TO_IDLE, "Proactive DVFS: GPU transitioned to idle", 0) \
X( 49, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_TO_ACTIVE, "Proactive DVFS: GPU transitioned to active", 0) \
X( 50, RGXFW_GROUP_POW, RGXFW_SF_POW_POWDUMP_BUFFER_SIZE, "Power counter dumping: Data truncated writing register %u. Buffer too small.", 1) \
X( 51, RGXFW_GROUP_POW, RGXFW_SF_POW_POWCTRL_ABORT, "Power controller returned ABORT for last request so retrying.", 0) \
X( 52, RGXFW_GROUP_POW, RGXFW_SF_POW_INVALID_POWER_REQUEST_DEPRECATED, "Discarding invalid power request: type 0x%x, DM %u", 2) \
X( 53, RGXFW_GROUP_POW, RGXFW_SF_POW_CANCEL_FORCED_IDLE_NOT_IDLE, "Detected attempt to cancel forced idle while not forced idle (pow state 0x%x, pow flags 0x%x)", 2) \
X( 54, RGXFW_GROUP_POW, RGXFW_SF_POW_FORCED_POW_OFF_NOT_IDLE, "Detected attempt to force power off while not forced idle (pow state 0x%x, pow flags 0x%x)", 2) \
X( 55, RGXFW_GROUP_POW, RGXFW_SF_POW_NUMDUST_CHANGE_NOT_IDLE, "Detected attempt to change dust count while not forced idle (pow state 0x%x)", 1) \
X( 56, RGXFW_GROUP_POW, RGXFW_SF_POW_POWMON_RESULT, "Power monitor: Type = %d (0 = power, 1 = energy), Estimate result = 0x%08x%08x", 3) \
X( 57, RGXFW_GROUP_POW, RGXFW_SF_POW_MINMAX_CONFLICT, "Conflicting clock frequency range: OPP min = %u, max = %u", 2) \
X( 58, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_FLOOR, "Proactive DVFS: Set floor to a minimum = 0x%x", 1) \
X( 59, RGXFW_GROUP_POW, RGXFW_SF_POW_OSREQ, "OS requested pow off (forced = %d), pow flags: 0x%x", 2) \
X( 60, RGXFW_GROUP_POW, RGXFW_SF_POW_INVALID_POWER_REQUEST, "Discarding invalid power request: type 0x%x", 1) \
X( 61, RGXFW_GROUP_POW, RGXFW_SF_POW_SPU_POW_STATE_CHANGE_REQ, "Request to change SPU power state mask from 0x%x to 0x%x. Pow flags: 0x%x", 3) \
X( 62, RGXFW_GROUP_POW, RGXFW_SF_POW_SPU_POW_STATE_CHANGE, "Changing SPU power state mask from 0x%x to 0x%x", 2) \
X( 63, RGXFW_GROUP_POW, RGXFW_SF_POW_SPU_POW_CHANGE_NOT_IDLE, "Detected attempt to change SPU power state mask while not forced idle (pow state 0x%x)", 1) \
X( 64, RGXFW_GROUP_POW, RGXFW_SF_POW_INVALID_SPU_POWER_MASK, "Invalid SPU power mask 0x%x! Changing to 1", 1) \
X( 65, RGXFW_GROUP_POW, RGXFW_SF_POW_CLKDIV_UPDATE, "Proactive DVFS: Send OPP %u with clock divider value %u", 2) \
X( 66, RGXFW_GROUP_POW, RGXFW_SF_POW_POWMON_PERF_MODE, "PPA block started in perf validation mode.", 0) \
X( 67, RGXFW_GROUP_POW, RGXFW_SF_POW_POWMON_RESET, "Reset PPA block state %u (1=reset, 0=recalculate).", 1) \
X( 68, RGXFW_GROUP_POW, RGXFW_SF_POW_POWCTRL_ABORT_WITH_CORE, "Power controller returned ABORT for Core-%d last request so retrying.", 1) \
X( 69, RGXFW_GROUP_POW, RGXFW_SF_POW_HWREQ64BIT, "HW Request On(1)/Off(0): %d, Units: 0x%08x%08x", 3) \
\
X(  1, RGXFW_GROUP_HWR, RGXFW_SF_HWR_LOCKUP_DEPRECATED, "Lockup detected on DM%d, FWCtx: 0x%08.8x", 2) \
X(  2, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_FW_DEPRECATED, "Reset fw state for DM%d, FWCtx: 0x%08.8x, MemCtx: 0x%08.8x", 3) \
X(  3, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW_DEPRECATED, "Reset HW", 0) \
X(  4, RGXFW_GROUP_HWR, RGXFW_SF_HWR_TERMINATED_DEPRECATED, "Lockup recovered.", 0) \
X(  5, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP_DEPRECATED, "Lock-up DM%d FWCtx: 0x%08.8x", 2) \
X(  6, RGXFW_GROUP_HWR, RGXFW_SF_HWR_LOCKUP_DETECTED_DEPRECATED, "Lockup detected: GLB(%d->%d), PER-DM(0x%08x->0x%08x)", 4) \
X(  7, RGXFW_GROUP_HWR, RGXFW_SF_HWR_EARLY_FAULT_DETECTION_DEPRECATED, "Early fault detection: GLB(%d->%d), PER-DM(0x%08x)", 3) \
X(  8, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_LOCKUP_DEPRECATED, "Hold scheduling due lockup: GLB(%d), PER-DM(0x%08x->0x%08x)", 3) \
X(  9, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FALSE_LOCKUP_DEPRECATED, "False lockup detected: GLB(%d->%d), PER-DM(0x%08x->0x%08x)", 4) \
X( 10, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BRN37729_DEPRECATED, "BRN37729: GLB(%d->%d), PER-DM(0x%08x->0x%08x)", 4) \
X( 11, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FREELISTS_RECONSTRUCTED_DEPRECATED, "Freelists reconstructed: GLB(%d->%d), PER-DM(0x%08x)", 3) \
X( 12, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RECONSTRUCTING_FREELISTS_DEPRECATED, "Reconstructing freelists: %u (0-No, 1-Yes): GLB(%d->%d), PER-DM(0x%08x)", 4) \
X( 13, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FAILED_HW_POLL, "HW poll %u (0-Unset 1-Set) failed (reg:0x%08x val:0x%08x)", 3) \
X( 14, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_DISCARDED_DEPRECATED, "Discarded cmd on DM%u FWCtx=0x%08x", 2) \
X( 15, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_DISCARDED, "Discarded cmd on DM%u (reason=%u) HWRTData=0x%08x (st: %d), FWCtx 0x%08x @ %d", 6) \
X( 16, RGXFW_GROUP_HWR, RGXFW_SF_HWR_PM_FENCE_DEPRECATED, "PM fence WA could not be applied, Valid TA Setup: %d, RD powered off: %d", 2) \
X( 17, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_SNAPSHOT, "FL snapshot RTD 0x%08.8x - local (0x%08.8x): %d, global (0x%08.8x): %d", 5) \
X( 18, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_CHECK, "FL check RTD 0x%08.8x, discard: %d - local (0x%08.8x): s%d?=c%d, global (0x%08.8x): s%d?=c%d", 8) \
X( 19, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_DEPRECATED, "FL reconstruction 0x%08.8x c%d", 2) \
X( 20, RGXFW_GROUP_HWR, RGXFW_SF_HWR_3D_CHECK, "3D check: missing TA FWCtx 0x%08.8x @ %d, RTD 0x%08x.", 3) \
X( 21, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW_DEPRECATED2, "Reset HW (mmu:%d, extmem: %d)", 2) \
X( 22, RGXFW_GROUP_HWR, RGXFW_SF_HWR_ZERO_TA_CACHES, "Zero TA caches for FWCtx: 0x%08.8x (TPC addr: 0x%08x%08x, size: %d bytes)", 4) \
X( 23, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FREELISTS_RECONSTRUCTED_DEPRECATED2, "Recovery DM%u: Freelists reconstructed. New R-Flags=0x%08x", 2) \
X( 24, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SKIPPED_CMD, "Recovery DM%u: FWCtx 0x%08x skipped to command @ %u. PR=%u. New R-Flags=0x%08x", 5) \
X( 25, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_RECOVERED, "Recovery DM%u: DM fully recovered", 1) \
X( 26, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_LOCKUP, "DM%u: Hold scheduling due to R-Flag = 0x%08x", 2) \
X( 27, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_RECONSTRUCTION, "Analysis: Need freelist reconstruction", 0) \
X( 28, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_SKIP, "Analysis DM%u: Lockup FWCtx: 0x%08.8x. Need to skip to next command", 2) \
X( 29, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_SKIP_OOM_TA, "Analysis DM%u: Lockup while TA is OOM FWCtx: 0x%08.8x. Need to skip to next command", 2) \
X( 30, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_PR_CLEANUP, "Analysis DM%u: Lockup while partial render FWCtx: 0x%08.8x. Need PR cleanup", 2) \
X( 31, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP_DEPRECATED2, "GPU has locked up", 0) \
X( 32, RGXFW_GROUP_HWR, RGXFW_SF_HWR_READY, "DM%u ready for HWR", 1) \
X( 33, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_UPDATE_RECOVERY, "Recovery DM%u: Updated Recovery counter. New R-Flags=0x%08x", 2) \
X( 34, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BRN37729_DEPRECATED2, "Analysis: BRN37729 detected, reset TA and re-kicked 0x%08x)", 1) \
X( 35, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_TIMED_OUT, "DM%u timed out", 1) \
X( 36, RGXFW_GROUP_HWR, RGXFW_SF_HWR_EVENT_STATUS_REG, "RGX_CR_EVENT_STATUS=0x%08x", 1) \
X( 37, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_FALSE_LOCKUP, "DM%u lockup falsely detected, R-Flags=0x%08x", 2) \
X( 38, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_OUTOFTIME, "GPU has overrun its deadline", 0) \
X( 39, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_POLLFAILURE, "GPU has failed a poll", 0) \
X( 40, RGXFW_GROUP_HWR, RGXFW_SF_HWR_PERF_PHASE_REG, "RGX DM%u phase count=0x%08x", 2) \
X( 41, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW_DEPRECATED3, "Reset HW (loop:%d, poll failures: 0x%08x)", 2) \
X( 42, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MMU_FAULT_EVENT, "MMU fault event: 0x%08x", 1) \
X( 43, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BIF1_FAULT, "BIF1 page fault detected (Bank1 MMU Status: 0x%08x)", 1) \
X( 44, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CRC_CHECK_TRUE_DEPRECATED, "Fast CRC Failed. Proceeding to full register checking (DM: %u).", 1) \
X( 45, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MMU_META_FAULT, "Meta MMU page fault detected (Meta MMU Status: 0x%08x%08x)", 2) \
X( 46, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CRC_CHECK_DEPRECATED, "Fast CRC Check result for DM%u is HWRNeeded=%u", 2) \
X( 47, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FULL_CHECK_DEPRECATED, "Full Signature Check result for DM%u is HWRNeeded=%u", 2) \
X( 48, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FINAL_RESULT, "Final result for DM%u is HWRNeeded=%u with HWRChecksToGo=%u", 3) \
X( 49, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_CHECK_DEPRECATED, "USC Slots result for DM%u is HWRNeeded=%u USCSlotsUsedByDM=%d", 3) \
X( 50, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DEADLINE_CHECK_DEPRECATED, "Deadline counter for DM%u is HWRDeadline=%u", 2) \
X( 51, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_FREELIST_DEPRECATED, "Holding Scheduling on OSid %u due to pending freelist reconstruction", 1) \
X( 52, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_REQUEST, "Requesting reconstruction for freelist 0x%x (ID=%d)", 2) \
X( 53, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_PASSED, "Reconstruction of freelist ID=%d complete", 1) \
X( 54, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_NEEDED_DEPRECATED, "Reconstruction needed for freelist 0x%x (ID=%d) type: %d (0:local,1:global,2:mmu) on HW context %u", 4) \
X( 55, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_FAILED, "Reconstruction of freelist ID=%d failed", 1) \
X( 56, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESTRICTING_PDS_TASKS, "Restricting PDS Tasks to help other stalling DMs (RunningMask=0x%02x, StallingMask=0x%02x, PDS_CTRL=0x%08x%08x)", 4) \
X( 57, RGXFW_GROUP_HWR, RGXFW_SF_HWR_UNRESTRICTING_PDS_TASKS, "Unrestricting PDS Tasks again (RunningMask=0x%02x, StallingMask=0x%02x, PDS_CTRL=0x%08x%08x)", 4) \
X( 58, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_USED, "USC slots: %u used by DM%u", 2) \
X( 59, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_EMPTY, "USC slots: %u empty", 1) \
X( 60, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HCS_FIRE, "HCS DM%d's Context Switch failed to meet deadline. Current time: 0x%08x%08x, deadline: 0x%08x%08x", 5) \
X( 61, RGXFW_GROUP_HWR, RGXFW_SF_HWR_START_HW_RESET, "Begin hardware reset (HWR Counter=%d)", 1) \
X( 62, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FINISH_HW_RESET, "Finished hardware reset (HWR Counter=%d)", 1) \
X( 63, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_FREELIST, "Holding Scheduling on DM %u for OSid %u due to pending freelist reconstruction", 2) \
X( 64, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_UMQ_READ_OFFSET, "User Mode Queue ROff reset: FWCtx 0x%08.8x, queue: 0x%08x%08x (Roff = %u becomes StreamStartOffset = %u)", 5) \
X( 65, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_NEEDED_DEPRECATED2, "Reconstruction needed for freelist 0x%x (ID=%d) type: %d (0:local,1:global) on HW context %u", 4) \
X( 66, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MIPS_FAULT, "Mips page fault detected (BadVAddr: 0x%08x, EntryLo0: 0x%08x, EntryLo1: 0x%08x)", 3) \
X( 67, RGXFW_GROUP_HWR, RGXFW_SF_HWR_ANOTHER_CHANCE, "At least one other DM is running okay so DM%u will get another chance", 1) \
X( 68, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_FW, "Reconstructing in FW, FL: 0x%x (ID=%d)", 2) \
X( 69, RGXFW_GROUP_HWR, RGXFW_SF_HWR_ZERO_RTC, "Zero RTC for FWCtx: 0x%08.8x (RTC addr: 0x%08x%08x, size: %d bytes)", 4) \
X( 70, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_NEEDED_DEPRECATED3, "Reconstruction needed for freelist 0x%x (ID=%d) type: %d (0:local,1:global) phase: %d (0:TA, 1:3D) on HW context %u", 5) \
X( 71, RGXFW_GROUP_HWR, RGXFW_SF_HWR_START_LONG_HW_POLL, "Start long HW poll %u (0-Unset 1-Set) for (reg:0x%08x val:0x%08x)", 3) \
X( 72, RGXFW_GROUP_HWR, RGXFW_SF_HWR_END_LONG_HW_POLL, "End long HW poll (result=%d)", 1) \
X( 73, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DEADLINE_CHECK, "DM%u has taken %d ticks and deadline is %d ticks", 3) \
X( 74, RGXFW_GROUP_HWR, RGXFW_SF_HWR_WATCHDOG_CHECK_DEPRECATED, "USC Watchdog result for DM%u is HWRNeeded=%u Status=%u USCs={0x%x} with HWRChecksToGo=%u", 5) \
X( 75, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_NEEDED, "Reconstruction needed for freelist 0x%x (ID=%d) OSid: %d type: %d (0:local,1:global) phase: %d (0:TA, 1:3D) on HW context %u", 6) \
X( 76, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP, "GPU-%u has locked up", 1) \
X( 77, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP_DM, "DM%u has locked up", 1) \
X( 78, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CORE_EVENT_STATUS_REG, "Core %d RGX_CR_EVENT_STATUS=0x%08x", 2) \
X( 79, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MULTICORE_EVENT_STATUS_REG, "RGX_CR_MULTICORE_EVENT_STATUS%u=0x%08x", 2) \
X( 80, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CORE_BIF0_FAULT, "BIF0 page fault detected (Core %d MMU Status: 0x%08x%08x Req Status: 0x%08x%08x)", 5) \
X( 81, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CORE_MMU_FAULT_S7, "MMU page fault detected (Core %d MMU Status: 0x%08x%08x)", 3) \
X( 82, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CORE_MMU_FAULT, "MMU page fault detected (Core %d MMU Status: 0x%08x%08x 0x%08x)", 4) \
X( 83, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW, "Reset HW (core:%d of %d, loop:%d, poll failures: 0x%08x)", 4) \
X( 84, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CRC_CHECK, "Fast CRC Check result for Core%u, DM%u is HWRNeeded=%u", 3) \
X( 85, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FULL_CHECK, "Full Signature Check result for Core%u, DM%u is HWRNeeded=%u", 3) \
X( 86, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_CHECK, "USC Slots result for Core%u, DM%u is HWRNeeded=%u USCSlotsUsedByDM=%d", 4) \
X( 87, RGXFW_GROUP_HWR, RGXFW_SF_HWR_WATCHDOG_CHECK, "USC Watchdog result for Core%u DM%u is HWRNeeded=%u Status=%u USCs={0x%x} with HWRChecksToGo=%u", 6) \
X( 88, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MMU_RISCV_FAULT, "RISC-V MMU page fault detected (FWCORE MMU Status 0x%08x Req Status 0x%08x%08x)", 3) \
X( 89, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_BIF_TEXAS1_PFS_DEPRECATED, "After FW fault was raised, TEXAS1_PFS poll failed on core %d with value 0x%08x", 2) \
X( 90, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_BIF_PFS, "After FW fault was raised, BIF_PFS poll failed on core %d with value 0x%08x", 2) \
X( 91, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_SET_ABORT_PM_STATUS, "After FW fault was raised, MMU_ABORT_PM_STATUS set poll failed on core %d with value 0x%08x", 2) \
X( 92, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_UNSET_ABORT_PM_STATUS, "After FW fault was raised, MMU_ABORT_PM_STATUS unset poll failed on core %d with value 0x%08x", 2) \
X( 93, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_SLC_INVAL, "After FW fault was raised, MMU_CTRL_INVAL poll (all but fw) failed on core %d with value 0x%08x", 2) \
X( 94, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_SLCMMU_INVAL, "After FW fault was raised, MMU_CTRL_INVAL poll (all) failed on core %d with value 0x%08x", 2) \
X( 95, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HWR_FAULT_POLL_BIF_TEXAS_PFS, "After FW fault was raised, TEXAS%d_PFS poll failed on core %d with value 0x%08x", 3) \
X( 96, RGXFW_GROUP_HWR, RGXFW_SF_HWR_EXTRA_CHECK, "Extra Registers Check result for Core%u, DM%u is HWRNeeded=%u", 3) \
\
X(  1, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CFGBLK, "Block 0x%x mapped to Config Idx %u", 2) \
X(  2, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_OMTBLK, "Block 0x%x omitted from event - not enabled in HW", 1) \
X(  3, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INCBLK, "Block 0x%x included in event - enabled in HW", 1) \
X(  4, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_SELREG, "Select register state hi_0x%x lo_0x%x", 2) \
X(  5, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CSBHDR, "Counter stream block header word 0x%x", 1) \
X(  6, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CTROFF, "Counter register offset 0x%x", 1) \
X(  7, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CFGSKP, "Block 0x%x config unset, skipping", 1) \
X(  8, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INDBLK, "Accessing Indirect block 0x%x", 1) \
X(  9, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DIRBLK, "Accessing Direct block 0x%x", 1) \
X( 10, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CNTPRG, "Programmed counter select register at offset 0x%x", 1) \
X( 11, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKPRG, "Block register offset 0x%x and value 0x%x", 2) \
X( 12, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKCG, "Reading config block from driver 0x%x", 1) \
X( 13, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKRG, "Reading block range 0x%x to 0x%x", 2) \
X( 14, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKREC, "Recording block 0x%x config from driver", 1) \
X( 15, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKED, "Finished reading config block from driver", 0) \
X( 16, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CUSTOM_COUNTER, "Custom Counter offset: 0x%x  value: 0x%x", 2) \
X( 17, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_SELECT_CNTR, "Select counter n:%u  ID:0x%x", 2) \
X( 18, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_SELECT_PACK, "The counter ID 0x%x is not allowed. The package [b:%u, n:%u] will be discarded", 3) \
X( 19, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CHANGE_FILTER_STATUS_CUSTOM, "Custom Counters filter status %d", 1) \
X( 20, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_WRONG_BLOCK, "The Custom block %d is not allowed. Use only blocks lower than %d. The package will be discarded", 2) \
X( 21, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_TOO_MANY_ID, "The package will be discarded because it contains %d counters IDs while the upper limit is %d", 2) \
X( 22, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CHECK_FILTER, "Check Filter 0x%x is 0x%x ?", 2) \
X( 23, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_RESET_CUSTOM_BLOCK, "The custom block %u is reset", 1) \
X( 24, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INVALID_CMD_DEPRECATED, "Encountered an invalid command (%d)", 1) \
X( 25, RGXFW_GROUP_HWP, RGXFW_SF_HWP_WAITING_FOR_QUEUE_DEPRECATED, "HWPerf Queue is full, we will have to wait for space! (Roff = %u, Woff = %u)", 2) \
X( 26, RGXFW_GROUP_HWP, RGXFW_SF_HWP_WAITING_FOR_QUEUE_FENCE_DEPRECATED, "HWPerf Queue is fencing, we are waiting for Roff = %d (Roff = %u, Woff = %u)", 3) \
X( 27, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CUSTOM_BLOCK, "Custom Counter block: %d", 1) \
X( 28, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKENA, "Block 0x%x ENABLED", 1) \
X( 29, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKDIS, "Block 0x%x DISABLED", 1) \
X( 30, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INDBLK_INSTANCE, "Accessing Indirect block 0x%x, instance %u", 2) \
X( 31, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CTRVAL, "Counter register 0x%x, Value 0x%x", 2) \
X( 32, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CHANGE_FILTER_STATUS, "Counters filter status %d", 1) \
X( 33, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CTLBLK, "Block 0x%x mapped to Ctl Idx %u", 2) \
X( 34, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_WORKEST_EN, "Block(s) in use for workload estimation.", 0) \
X( 35, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CYCCTR, "GPU %u Cycle counter 0x%x, Value 0x%x", 3) \
X( 36, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CYCMAX, "GPU Mask 0x%x Cycle counter 0x%x, Value 0x%x", 3) \
X( 37, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_IGNORE_BLOCKS, "Blocks IGNORED for GPU %u", 1) \
\
X(  1, RGXFW_GROUP_DMA, RGXFW_SF_DMA_TRANSFER_REQUEST_DEPRECATED, "Transfer 0x%02x request: 0x%02x%08x -> 0x%08x, size %u", 5) \
X(  2, RGXFW_GROUP_DMA, RGXFW_SF_DMA_TRANSFER_COMPLETE, "Transfer of type 0x%02x expected on channel %u, 0x%02x found, status %u", 4) \
X(  3, RGXFW_GROUP_DMA, RGXFW_SF_DMA_INT_REG, "DMA Interrupt register 0x%08x", 1) \
X(  4, RGXFW_GROUP_DMA, RGXFW_SF_DMA_WAIT, "Waiting for transfer of type 0x%02x completion...", 1) \
X(  5, RGXFW_GROUP_DMA, RGXFW_SF_DMA_CCB_LOADING_FAILED, "Loading of cCCB data from FW common context 0x%08x (offset: %u, size: %u) failed", 3) \
X(  6, RGXFW_GROUP_DMA, RGXFW_SF_DMA_CCB_LOAD_INVALID, "Invalid load of cCCB data from FW common context 0x%08x (offset: %u, size: %u)", 3) \
X(  7, RGXFW_GROUP_DMA, RGXFW_SF_DMA_POLL_FAILED, "Transfer 0x%02x request poll failure", 1) \
X(  8, RGXFW_GROUP_DMA, RGXFW_SF_DMA_BOOT_TRANSFER_FAILED, "Boot transfer(s) failed (code? %u, data? %u), used slower memcpy instead", 2) \
X(  9, RGXFW_GROUP_DMA, RGXFW_SF_DMA_TRANSFER_REQUEST, "Transfer 0x%02x request on ch. %u: system 0x%02x%08x, coremem 0x%08x, flags 0x%x, size %u", 7) \
\
X(  1, RGXFW_GROUP_DBG, RGXFW_SF_DBG_INTPAIR, "0x%08x 0x%08x", 2) \
X(  2, RGXFW_GROUP_DBG, RGXFW_SF_DBG_1HEX, "0x%08x", 1) \
X(  3, RGXFW_GROUP_DBG, RGXFW_SF_DBG_2HEX, "0x%08x 0x%08x", 2) \
X(  4, RGXFW_GROUP_DBG, RGXFW_SF_DBG_3HEX, "0x%08x 0x%08x 0x%08x", 3) \
X(  5, RGXFW_GROUP_DBG, RGXFW_SF_DBG_4HEX, "0x%08x 0x%08x 0x%08x 0x%08x", 4) \
X(  6, RGXFW_GROUP_DBG, RGXFW_SF_DBG_5HEX, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", 5) \
X(  7, RGXFW_GROUP_DBG, RGXFW_SF_DBG_6HEX, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", 6) \
X(  8, RGXFW_GROUP_DBG, RGXFW_SF_DBG_7HEX, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", 7) \
X(  9, RGXFW_GROUP_DBG, RGXFW_SF_DBG_8HEX, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", 8) \
X( 10, RGXFW_GROUP_DBG, RGXFW_SF_DBG_1SIGNED, "%d", 1) \
X( 11, RGXFW_GROUP_DBG, RGXFW_SF_DBG_2SIGNED, "%d %d", 2) \
X( 12, RGXFW_GROUP_DBG, RGXFW_SF_DBG_3SIGNED, "%d %d %d", 3) \
X( 13, RGXFW_GROUP_DBG, RGXFW_SF_DBG_4SIGNED, "%d %d %d %d", 4) \
X( 14, RGXFW_GROUP_DBG, RGXFW_SF_DBG_5SIGNED, "%d %d %d %d %d", 5) \
X( 15, RGXFW_GROUP_DBG, RGXFW_SF_DBG_6SIGNED, "%d %d %d %d %d %d", 6) \
X( 16, RGXFW_GROUP_DBG, RGXFW_SF_DBG_7SIGNED, "%d %d %d %d %d %d %d", 7) \
X( 17, RGXFW_GROUP_DBG, RGXFW_SF_DBG_8SIGNED, "%d %d %d %d %d %d %d %d", 8) \
X( 18, RGXFW_GROUP_DBG, RGXFW_SF_DBG_1UNSIGNED, "%u", 1) \
X( 19, RGXFW_GROUP_DBG, RGXFW_SF_DBG_2UNSIGNED, "%u %u", 2) \
X( 20, RGXFW_GROUP_DBG, RGXFW_SF_DBG_3UNSIGNED, "%u %u %u", 3) \
X( 21, RGXFW_GROUP_DBG, RGXFW_SF_DBG_4UNSIGNED, "%u %u %u %u", 4) \
X( 22, RGXFW_GROUP_DBG, RGXFW_SF_DBG_5UNSIGNED, "%u %u %u %u %u", 5) \
X( 23, RGXFW_GROUP_DBG, RGXFW_SF_DBG_6UNSIGNED, "%u %u %u %u %u %u", 6) \
X( 24, RGXFW_GROUP_DBG, RGXFW_SF_DBG_7UNSIGNED, "%u %u %u %u %u %u %u", 7) \
X( 25, RGXFW_GROUP_DBG, RGXFW_SF_DBG_8UNSIGNED, "%u %u %u %u %u %u %u %u", 8) \
\
X(65535, RGXFW_GROUP_NULL, RGXFW_SF_LAST, "You should not use this string", 15)


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
#define RGXFW_LOG_IDMARKER			(0x70000000U)
#define RGXFW_LOG_CREATESFID(a,b,e) ((IMG_UINT32)(a) | ((IMG_UINT32)(b)<<12U) | ((IMG_UINT32)(e)<<16U)) | RGXFW_LOG_IDMARKER

#define RGXFW_LOG_IDMASK			(0xFFF00000)
#define RGXFW_LOG_VALIDID(I)		(((I) & RGXFW_LOG_IDMASK) == RGXFW_LOG_IDMARKER)

typedef enum {
#define X(a, b, c, d, e) c = RGXFW_LOG_CREATESFID(a,b,e),
	RGXFW_LOG_SFIDLIST
#undef X
} RGXFW_LOG_SFids;

/* Return the group id that the given (enum generated) id belongs to */
#define RGXFW_SF_GID(x) (((IMG_UINT32)(x)>>12) & 0xfU)
/* Returns how many arguments the SF(string format) for the given (enum generated) id requires */
#define RGXFW_SF_PARAMNUM(x) (((IMG_UINT32)(x)>>16) & 0xfU)

#endif /* RGX_FWIF_SF_H */
