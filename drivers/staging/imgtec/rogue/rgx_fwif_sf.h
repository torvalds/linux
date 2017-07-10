/*************************************************************************/ /*!
@File			rgx_fwif_sf.h
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

/*****************************************************************************
 * *DO*NOT* rearrange or delete lines in SFIDLIST or SFGROUPLIST or you
 *  		 WILL BREAK fw tracing message compatibility with previous
 *  		 fw versions. Only add new ones, if so required.
 ****************************************************************************/
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
	X(RGXFW_GROUP_DATA,DATA)        \
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
X( 0, RGXFW_GROUP_NULL, RGXFW_SF_FIRST, "You should not use this string\n", 0) \
\
X( 1,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3D_DEPRECATED, "Kick 3D: FWCtx 0x%08.8X @ %d, RTD 0x%08x. Partial render:%d, CSW resume:%d, prio:%d\n", 6) \
X( 2,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_FINISHED, "3D finished, HWRTData0State=%d, HWRTData1State=%d\n", 2) \
X( 3,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK3D_TQ_DEPRECATED, "Kick 3D TQ: FWCtx 0x%08.8X @ %d, CSW resume:%d, prio: %d\n", 4) \
X( 4,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_TQ_FINISHED, "3D Transfer finished\n", 0) \
X( 5,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_COMPUTE_DEPRECATED, "Kick Compute: FWCtx 0x%08.8X @ %d, prio: %d\n", 3) \
X( 6,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_FINISHED, "Compute finished\n", 0) \
X( 7,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TA_DEPRECATED, "Kick TA: FWCtx 0x%08.8X @ %d, RTD 0x%08x. First kick:%d, Last kick:%d, CSW resume:%d, prio:%d\n", 7) \
X( 8,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_FINISHED, "TA finished\n", 0) \
X( 9,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_RESTART_AFTER_PRENDER, "Restart TA after partial render\n", 0) \
X(10,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_RESUME_WOUT_PRENDER, "Resume TA without partial render\n", 0) \
X(11,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OOM, "Out of memory! Context 0x%08x, HWRTData 0x%x\n", 2) \
X(12,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TLA_DEPRECATED, "Kick TLA: FWCtx 0x%08.8X @ %d, prio:%d\n", 3) \
X(13,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TLA_FINISHED, "TLA finished\n", 0) \
X(14,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CCCB_WOFF_UPDATE, "cCCB Woff update = %d, DM = %d, FWCtx = 0x%08.8X\n", 3) \
X(16,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_START, "UFO Checks for FWCtx %08.8X @ %d\n", 2) \
X(17,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK, "UFO Check: [%08.8X] is %08.8X requires %08.8X\n", 3) \
X(18,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_SUCCEEDED, "UFO Checks succeeded\n", 0) \
X(19,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_PR_CHECK, "UFO PR-Check: [%08.8X] is %08.8X requires >= %08.8X\n", 3) \
X(20,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK_START, "UFO SPM PR-Checks for FWCtx %08.8X\n", 1) \
X(21,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK_DEPRECATED, "UFO SPM special PR-Check: [%08.8X] is %08.8X requires >= ????????, [%08.8X] is ???????? requires %08.8X\n", 4) \
X(22,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_UPDATE_START, "UFO Updates for FWCtx %08.8X @ %d\n", 2) \
X(23,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_UPDATE, "UFO Update: [%08.8X] = %08.8X\n", 2) \
X(24,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ASSERT_FAILED, "ASSERT Failed: line %d of: \n", 1) \
X(25,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_LOCKUP_DEPRECATED, "HWR: Lockup detected on DM%d, FWCtx: %08.8X\n", 2) \
X(26,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_RESET_FW_DEPRECATED, "HWR: Reset fw state for DM%d, FWCtx: %08.8X, MemCtx: %08.8X\n", 3) \
X(27,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_RESET_HW_DEPRECATED, "HWR: Reset HW\n", 0) \
X(28,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_TERMINATED_DEPRECATED, "HWR: Lockup recovered.\n", 0) \
X(29,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_FALSE_LOCKUP_DEPRECATED, "HWR: False lockup detected for DM%u\n", 1) \
X(30,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ALIGN_FAILED, "Alignment check %d failed: host = %X, fw = %X\n", 3) \
X(31,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_GP_USC_TRIGGERED, "GP USC triggered\n", 0) \
X(32,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_OVERALLOC_REGS, "Overallocating %u temporary registers and %u shared registers for breakpoint handler\n", 2) \
X(33,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_SET_DEPRECATED, "Setting breakpoint: Addr 0x%08.8X\n", 1) \
X(34,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_STORE, "Store breakpoint state\n", 0) \
X(35,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_UNSET, "Unsetting BP Registers\n", 0) \
X(36,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NONZERO_RT, "Active RTs expected to be zero, actually %u\n", 1) \
X(37,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTC_PRESENT, "RTC present, %u active render targets\n", 1) \
X(38,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_EST_POWER, "Estimated Power 0x%x\n", 1) \
X(39,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTA_TARGET, "RTA render target %u\n", 1) \
X(40,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTA_KICK_RENDER, "Kick RTA render %u of %u\n", 2) \
X(41,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_SIZES_CHECK, "HWR sizes check %d failed: addresses = %d, sizes = %d\n", 3) \
X(42,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_ENABLE_DEPRECATED, "Pow: DUSTS_ENABLE = 0x%X\n", 1) \
X(43,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_HWREQ_DEPRECATED, "Pow: On(1)/Off(0): %d, Units: 0x%08.8X\n", 2) \
X(44,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_CHANGE_DEPRECATED, "Pow: Changing number of dusts from %d to %d\n", 2) \
X(45,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_SIDEKICK_IDLE_DEPRECATED, "Pow: Sidekick ready to be powered down\n", 0) \
X(46,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_POW_DUSTS_CHANGE_REQ_DEPRECATED, "Pow: Request to change num of dusts to %d (bPowRascalDust=%d)\n", 2) \
X(47,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PARTIALRENDER_WITHOUT_ZSBUFFER_STORE, "No ZS Buffer used for partial render (store)\n", 0) \
X(48,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_PARTIALRENDER_WITHOUT_ZSBUFFER_LOAD, "No Depth/Stencil Buffer used for partial render (load)\n", 0) \
X(49,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_SET_LOCKUP_DEPRECATED, "HWR: Lock-up DM%d FWCtx: %08.8X \n", 2) \
X(50,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_REG_VALUE_DEPRECATED, "MLIST%d checker: CatBase TE=0x%08x (%d Pages), VCE=0x%08x (%d Pages), ALIST=0x%08x, IsTA=%d\n", 7) \
X(51,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_MLIST_VALUE, "MLIST%d checker: MList[%d] = 0x%08x\n", 3) \
X(52,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_OK, "MLIST%d OK\n", 1) \
X(53,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_EMPTY, "MLIST%d is empty\n", 1) \
X(54,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_MLIST_CHECKER_REG_VALUE, "MLIST%d checker: CatBase TE=0x%08X%08X, VCE=0x%08x%08X, ALIST=0x%08x%08X, IsTA=%d\n", 8) \
X(55,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_40480KICK, "3D OQ flush kick\n", 0) \
X(56,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWP_UNSUPPORTED_BLOCK, "HWPerf block ID (0x%x) unsupported by device\n", 1) \
X(57,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BREAKPOINT_SET, "Setting breakpoint: Addr 0x%08.8X DM%u\n", 2) \
X(58,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU_DEPRECATED, "Kick RTU: FWCtx 0x%08.8X @ %d, prio: %d\n", 3) \
X(59,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTU_FINISHED, "RDM finished on context %u\n", 1) \
X(60,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_SHG_DEPRECATED, "Kick SHG: FWCtx 0x%08.8X @ %d, prio: %d\n", 3) \
X(61,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SHG_FINISHED, "SHG finished\n", 0) \
X(62,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FBA_FINISHED, "FBA finished on context %u\n", 1) \
X(63,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_CHECK_FAILED, "UFO Checks failed\n", 0) \
X(64,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_START, "Kill DM%d start\n", 1) \
X(65,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_COMPLETE, "Kill DM%d complete\n", 1) \
X(66,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FC_CCB_UPDATE, "FC%u cCCB Woff update = %u\n", 2) \
X(67,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU_DEPRECATED2, "Kick RTU: FWCtx 0x%08.8X @ %d, prio: %d, Frame Context: %d\n", 4) \
X(68,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIDEKICK_INIT, "Sidekick init\n", 0) \
X(69,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RD_INIT, "Rascal+Dusts init (# dusts mask: %X)\n", 1) \
X(70,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGTIMES, "Register access cycles: read: %d cycles, write: %d cycles, iterations: %d\n", 3) \
X(71,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGCONFIG_ADD, "Register configuration added. Address: 0x%x Value: 0x%x%x\n", 3) \
X(72,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_REGCONFIG_SET, "Register configuration applied to type %d. (0:pow on, 1:Rascal/dust init, 2-5: TA,3D,CDM,TLA, 6:All)\n", 1) \
X(73,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TPC_FLUSH, "Perform TPC flush.\n", 0) \
X(74,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_LOCKUP, "GPU has locked up (see HWR logs for more info)\n", 0) \
X(75,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_OUTOFTIME, "HWR has been triggered - GPU has overrun its deadline (see HWR logs)\n", 0) \
X(76,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HWR_HIT_POLLFAILURE, "HWR has been triggered - GPU has failed a poll (see HWR logs)\n", 0) \
X(77,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DOPPLER_OOM, "Doppler out of memory event for FC %u\n", 1) \
X(78,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK1, "UFO SPM special PR-Check: [%08.8X] is %08.8X requires >= %08.8X\n", 3) \
X(79,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_SPM_PR_CHECK2, "UFO SPM special PR-Check: [%08.8X] is %08.8X requires %08.8X\n", 3) \
X(80,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TIMESTAMP, "TIMESTAMP -> [%08.8X]\n", 1) \
X(81,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_RMW_UPDATE_START, "UFO RMW Updates for FWCtx %08.8X @ %d\n", 2) \
X(82,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UFO_RMW_UPDATE, "UFO Update: [%08.8X] = %08.8X\n", 2) \
X(83,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NULLCMD, "Kick Null cmd: FWCtx 0x%08.8X @ %d\n", 2) \
X(84,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RPM_OOM, "RPM Out of memory! Context 0x%08x, SH requestor %d\n", 2) \
X(85,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RTU_ABORT_DISCARD, "Discard RTU due to RPM abort: FWCtx 0x%08.8X @ %d, prio: %d, Frame Context: %d\n", 4) \
X(86,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED, "Deferring DM%u from running context 0x%08x @ %d (deferred DMs = 0x%08x)\n", 4) \
X(87,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED_WAITING_TURN, "Deferring DM%u from running context 0x%08x @ %d to let other deferred DMs run (deferred DMs = 0x%08x)\n", 4) \
X(88,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_DEFERRED_NO_LONGER, "No longer deferring DM%u from running context = 0x%08x @ %d (deferred DMs = 0x%08x)\n", 4) \
X(89,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_FWCCB_DEPRECATED, "FWCCB for DM%u is full, we will have to wait for space! (Roff = %u, Woff = %u)\n", 3) \
X(90,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_FWCCB, "FWCCB for OSid %u is full, we will have to wait for space! (Roff = %u, Woff = %u)\n", 3) \
X(91,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SYNC_PART, "Host Sync Partition marker: %d\n", 1) \
X(92,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SYNC_PART_RPT, "Host Sync Partition repeat: %d\n", 1) \
X(93,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CLOCK_SPEED_CHANGE, "Core clock set to %d Hz\n", 1) \
X(94,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_OFFSETS, "Compute Queue: FWCtx 0x%08.8X, prio: %d, queue: 0x%08X%08X (Roff = %u, Woff = %u, Size = %u)\n", 7) \
X(95,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE, "Signal check failed, Required Data: 0x%X, Address: 0x%08x%08x\n", 3) \
X(96,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_UPDATE, "Signal update, Snoop Filter: %u, MMU Ctx: %u, Signal Id: %u, Signals Base: 0x%08x%08x\n", 5) \
X(97,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWCONTEXT_SIGNALED, "Signalled the previously waiting FWCtx: 0x%08.8X, OSId: %u, Signal Address: 0x%08x%08x\n", 4) \
X(98,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_STALLED_DEPRECATED, "Compute stalled\n", 0) \
X(99,  RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_STALLED, "Compute stalled (Roff = %u, Woff = %u, Size = %u)\n", 3) \
X(100, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_RESUMED_FROM_STALL, "Compute resumed (Roff = %u, Woff = %u, Size = %u)\n", 3) \
X(101, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NOTIFY_SIGNAL_UPDATE, "Signal update notification from the host, PC Physical Address: 0x%08x%08x, Signal Virtual Address: 0x%08x%08x\n", 4) \
X(102, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_UPDATE_OSID_DM, "Signal update from DM: %u, OSId: %u, PC Physical Address: 0x%08x%08x\n", 4) \
X(103, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE_DM, "DM: %u signal check failed\n", 1) \
X(104, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TDM_DEPRECATED, "Kick TDM: FWCtx 0x%08.8X @ %d, prio:%d\n", 3) \
X(105, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_FINISHED, "TDM finished\n", 0) \
X(106, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TE_PIPE_STATUS, "MMU_PM_CAT_BASE_TE[%d]_PIPE[%d]:  0x%08X 0x%08X)\n", 4) \
X(107, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_HIT, "BRN 54141 HIT\n", 0) \
X(108, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_APPLYING_DUMMY_TA, "BRN 54141 Dummy TA kicked\n", 0) \
X(109, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_RESUME_TA, "BRN 54141 resume TA\n", 0) \
X(110, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_DOUBLE_HIT, "BRN 54141 double hit after applying WA\n", 0) \
X(111, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_BRN_54141_DUMMY_TA_VDM_BASE, "BRN 54141 Dummy TA VDM base address: 0x%08x%08x\n", 2) \
X(112, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIGNAL_WAIT_FAILURE_WITH_CURRENT, "Signal check failed, Required Data: 0x%X, Current Data: 0x%X, Address: 0x%08x%08x\n", 4) \
X(113, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_BUFFER_STALL, "TDM stalled (Roff = %u, Woff = %u)\n", 2) \
X(114, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_NOTIFY_WRITE_OFFSET_UPDATE, "Write Offset update notification for stalled FWCtx %08.8X\n", 1) \
X(115, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_PRIORITY_CHANGE, "Changing OSid %d's priority from %u to %u \n", 3) \
X(116, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_COMPUTE_RESUMED, "Compute resumed\n", 0) \
X(117, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TLA, "Kick TLA: FWCtx 0x%08.8X @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 7) \
X(118, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TDM, "Kick TDM: FWCtx 0x%08.8X @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 7) \
X(119, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_TA, "Kick TA: FWCtx 0x%08.8X @ %d, RTD 0x%08x, First kick:%d, Last kick:%d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 11) \
X(120, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3D, "Kick 3D: FWCtx 0x%08.8X @ %d, RTD 0x%08x, Partial render:%d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 10) \
X(121, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_3DTQ, "Kick 3D TQ: FWCtx 0x%08.8X @ %d, CSW resume:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 8) \
X(122, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_COMPUTE, "Kick Compute: FWCtx 0x%08.8X @ %d. (PID:%d, prio:%d, ext:0x%08X, int:0x%08X)\n", 6) \
X(123, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_RTU, "Kick RTU: FWCtx 0x%08.8X @ %d, Frame Context:%d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 8) \
X(124, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KICK_SHG, "Kick SHG: FWCtx 0x%08.8X @ %d. (PID:%d, prio:%d, frame:%d, ext:0x%08X, int:0x%08X)\n", 7) \
X(125, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_CSRM_RECONFIG, "Reconfigure CSRM: special coeff support enable %d.\n", 1) \
X(127, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TA_REQ_MAX_COEFFS, "TA requires max coeff mode, deferring: %d.\n", 1) \
X(128, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_REQ_MAX_COEFFS, "3D requires max coeff mode, deferring: %d.\n", 1) \
X(129, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_KILLDM_FAILED, "Kill DM%d failed\n", 1) \
X(130, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_QUEUE, "Thread Queue is full, we will have to wait for space! (Roff = %u, Woff = %u)\n", 2) \
X(131, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_WAITING_FOR_QUEUE_FENCE, "Thread Queue is fencing, we are waiting for Roff = %d (Roff = %u, Woff = %u)\n", 3) \
X(132, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SET_HCS_TRIGGERED, "DM %d failed to Context Switch on time. Triggered HCS (see HWR logs).\n", 1) \
X(133, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_HCS_SET, "HCS changed to %d ms\n", 1) \
X(134, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UPDATE_TILES_IN_FLIGHT, "Updating Tiles In Flight (Dusts=%d, PartitionMask=0x%08x, ISPCtl=0x%08x%08x)\n", 4) \
X(135, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SET_TILES_IN_FLIGHT, "  Phantom %d: USCTiles=%d\n", 2) \
X(136, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ISOLATION_CONF_OFF, "Isolation grouping is disabled \n", 0) \
X(137, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_ISOLATION_CONF, "Isolation group configured with a priority threshold of %d\n", 1) \
X(138, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_ONLINE, "OS %d has come online \n", 1) \
X(139, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_OFFLINE, "OS %d has gone offline \n", 1) \
X(140, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_FWCONTEXT_SIGNAL_REKICK, "Signalled the previously stalled FWCtx: 0x%08.8X, OSId: %u, Signal Address: 0x%08x%08x\n", 4) \
X(141, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_OFFSETS, "TDM Queue: FWCtx 0x%08.8X, prio: %d, queue: 0x%08X%08X (Roff = %u, Woff = %u, Size = %u)\n", 7) \
X(142, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_TDM_OFFSET_READ_RESET, "Reset TDM Queue Read Offset: FWCtx 0x%08.8X, queue: 0x%08X%08X (Roff = %u becomes 0, Woff = %u, Size = %u)\n", 6) \
X(143, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_UMQ_MISMATCHED_READ_OFFSET, "User Mode Queue mismatched stream start: FWCtx 0x%08.8X, queue: 0x%08X%08X (Roff = %u, StreamStartOffset = %u)\n", 5) \
X(144, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_SIDEKICK_DEINIT, "Sidekick deinit\n", 0) \
X(145, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_RD_DEINIT, "Rascal+Dusts deinit\n", 0) \
X(146, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_OS_INIT_CONFIG, "Initialised OS %d with config flags 0x%08x\n", 2) \
X(148, RGXFW_GROUP_MAIN, RGXFW_SF_MAIN_3D_62850KICK, "3D Dummy stencil store\n", 0) \
\
X( 1, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_KICK_DEPRECATED, "Bg Task DM = %u, counted = %d\n", 2) \
X( 2, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_COMPLETE_DEPRECATED, "Bg Task complete DM = %u\n", 1) \
X( 3, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_KICK, "Irq Task DM = %u, Breq = %d, SBIrq = 0x%X\n", 3) \
X( 4, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_COMPLETE_DEPRECATED, "Irq Task complete DM = %u\n", 1) \
X( 5, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KICK_MTS_BG_ALL, "Kick MTS Bg task DM=All\n", 0) \
X( 6, RGXFW_GROUP_MTS, RGXFW_SF_MTS_KICK_MTS_IRQ, "Kick MTS Irq task DM=%d \n", 1) \
X( 7, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYCELLTYPE_DEPRECATED, "Ready queue debug DM = %u, celltype = %d\n", 2) \
X( 8, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYTORUN_DEPRECATED, "Ready-to-run debug DM = %u, item = 0x%x\n", 2) \
X( 9, RGXFW_GROUP_MTS, RGXFW_SF_MTS_CMDHEADER, "Client command header DM = %u, client CCB = %x, cmd = %x\n", 3) \
X(10, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYTORUN, "Ready-to-run debug OSid = %u, DM = %u, item = 0x%x\n", 3) \
X(11, RGXFW_GROUP_MTS, RGXFW_SF_MTS_READYCELLTYPE, "Ready queue debug DM = %u, celltype = %d, OSid = %u\n", 3) \
X(12, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_KICK, "Bg Task DM = %u, counted = %d, OSid = %u\n", 3 ) \
X(13, RGXFW_GROUP_MTS, RGXFW_SF_MTS_BG_COMPLETE, "Bg Task complete DM Bitfield: %u\n", 1) \
X(14, RGXFW_GROUP_MTS, RGXFW_SF_MTS_IRQ_COMPLETE, "Irq Task complete.\n", 0) \
\
X( 1, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FWCTX_CLEANUP, "FwCommonContext [0x%08x] cleaned\n", 1) \
X( 2, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FWCTX_BUSY, "FwCommonContext [0x%08x] is busy: ReadOffset = %d, WriteOffset = %d\n", 3) \
X( 3, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANUP, "HWRTData [0x%08x] for DM=%d, received cleanup request\n", 2) \
X( 4, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANED_FOR_DM, "HWRTData [0x%08x] HW Context cleaned for DM%u, executed commands = %d\n", 3) \
X( 5, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_BUSY_DEPRECATED, "HWRTData [0x%08x] HW Context for DM%u is busy\n", 2) \
X( 6, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_CLEANED, "HWRTData [0x%08x] HW Context %u cleaned\n", 2) \
X( 7, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_FL_CLEANED, "Freelist [0x%08x] cleaned\n", 1) \
X( 8, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_ZSBUFFER_CLEANED, "ZSBuffer [0x%08x] cleaned\n", 1) \
X( 9, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_ZSBUFFER_BUSY, "ZSBuffer [0x%08x] is busy: submitted = %d, executed = %d\n", 3) \
X(10, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRTD_BUSY, "HWRTData [0x%08x] HW Context for DM%u is busy: submitted = %d, executed = %d\n", 4) \
X(11, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANUP, "HW Ray Frame data [0x%08x] for DM=%d, received cleanup request\n", 2) \
X(12, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANED_FOR_DM, "HW Ray Frame Data [0x%08x] cleaned for DM%u, executed commands = %d\n", 3) \
X(13, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_BUSY, "HW Ray Frame Data [0x%08x] for DM%u is busy: submitted = %d, executed = %d\n", 4) \
X(14, RGXFW_GROUP_CLEANUP, RGXFW_SF_CLEANUP_HWRFD_CLEANED, "HW Ray Frame Data [0x%08x] HW Context %u cleaned\n", 2) \
\
X( 1, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_NEEDS_RESUME, "CDM FWCtx 0x%08.8X needs resume\n", 1) \
X( 2, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_RESUME, "*** CDM FWCtx 0x%08.8X resume from snapshot buffer 0x%08X%08X\n", 3) \
X( 3, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_SHARED, "CDM FWCtx shared alloc size load 0x%X\n", 1) \
X( 4, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_COMPLETE, "*** CDM FWCtx store complete\n", 0) \
X( 5, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_START, "*** CDM FWCtx store start\n", 0) \
X( 6, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_SOFT_RESET, "CDM Soft Reset\n", 0) \
X( 7, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_NEEDS_RESUME, "3D FWCtx 0x%08.8X needs resume\n", 1) \
X( 8, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME, "*** 3D FWCtx 0x%08.8X resume\n", 1) \
X( 9, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_COMPLETE, "*** 3D context store complete\n", 0) \
X(10, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE_DEPRECATED, "3D context store pipe state: 0x%08.8X 0x%08.8X 0x%08.8X\n", 3) \
X(11, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_START, "*** 3D context store start\n", 0) \
X(12, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_TQ_RESUME, "*** 3D TQ FWCtx 0x%08.8X resume\n", 1) \
X(13, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_NEEDS_RESUME, "TA FWCtx 0x%08.8X needs resume\n", 1) \
X(14, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_RESUME, "*** TA FWCtx 0x%08.8X resume from snapshot buffer 0x%08X%08X\n", 3) \
X(15, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_SHARED, "TA context shared alloc size store 0x%X, load 0x%X\n", 2) \
X(16, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_COMPLETE, "*** TA context store complete\n", 0) \
X(17, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_START, "*** TA context store start\n", 0) \
X(18, RGXFW_GROUP_CSW, RGXFW_SF_CSW_HIGHER_PRIORITY_SCHEDULED_DEPRECATED, "Higher priority context scheduled for DM %u, old prio:%d, new prio:%d\n", 3) \
X(19, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SET_CONTEXT_PRIORITY, "Set FWCtx 0x%x priority to %u\n", 2) \
X(20, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_STORE_PIPE_STATE, "3D context store pipe%d state: 0x%08.8X\n", 2) \
X(21, RGXFW_GROUP_CSW, RGXFW_SF_CSW_3D_RESUME_PIPE_STATE, "3D context resume pipe%d state: 0x%08.8X\n", 2) \
X(22, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_NEEDS_RESUME, "SHG FWCtx 0x%08.8X needs resume\n", 1) \
X(23, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_RESUME, "*** SHG FWCtx 0x%08.8X resume from snapshot buffer 0x%08X%08X\n", 3) \
X(24, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_SHARED, "SHG context shared alloc size store 0x%X, load 0x%X\n", 2) \
X(25, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_STORE_COMPLETE, "*** SHG context store complete\n", 0) \
X(26, RGXFW_GROUP_CSW, RGXFW_SF_CSW_SHG_STORE_START, "*** SHG context store start\n", 0) \
X(27, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_PIPE_INDIRECT, "Performing TA indirection, last used pipe %d\n", 1) \
X(28, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_STORE_CTRL_STREAM_TERMINATE, "CDM context store hit ctrl stream terminate. Skip resume.\n", 0) \
X(29, RGXFW_GROUP_CSW, RGXFW_SF_CSW_CDM_RESUME_AB_BUFFER, "*** CDM FWCtx 0x%08.8X resume from snapshot buffer 0x%08X%08X, shader state %u\n", 4) \
X(30, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STATE_BUFFER_FLIP, "TA PDS/USC state buffer flip (%d->%d)\n", 2) \
X(31, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_52563_HIT, "TA context store hit BRN 52563: vertex store tasks outstanding\n", 0) \
X(32, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_USC_POLL_FAILED, "TA USC poll failed (USC vertex task count: %d)\n", 1) \
X(33, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TA_STORE_DEFERRED, "TA context store deferred due to BRN 54141.", 0) \
X(34, RGXFW_GROUP_CSW, RGXFW_SF_CSW_HIGHER_PRIORITY_SCHEDULED, "Higher priority context scheduled for DM %u. Prios (OSid, OSid Prio, Context Prio): Current: %u, %u, %u New: %u, %u, %u\n", 7) \
X(35, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_START, "*** TDM context store start\n", 0) \
X(36, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_COMPLETE, "*** TDM context store complete\n", 0) \
X(37, RGXFW_GROUP_CSW, RGXFW_SF_CSW_TDM_STORE_NEEDS_RESUME, "TDM context needs resume, header [%08.8X, %08.8X]\n", 2) \
\
X( 1, RGXFW_GROUP_BIF, RGXFW_SF_BIF_ACTIVATE, "Activate MemCtx=0x%08x BIFreq=%d secure=%d\n", 3) \
X( 2, RGXFW_GROUP_BIF, RGXFW_SF_BIF_DEACTIVATE, "Deactivate MemCtx=0x%08x \n", 1) \
X( 3, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_ALLOC, "Alloc PC reg %d\n", 1) \
X( 4, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_GRAB, "Grab reg %d refcount now %d\n", 2) \
X( 5, RGXFW_GROUP_BIF, RGXFW_SF_BIF_PCREG_UNGRAB, "Ungrab reg %d refcount now %d\n", 2) \
X( 6, RGXFW_GROUP_BIF, RGXFW_SF_BIF_SETUP_REG, "Setup reg=%d BIFreq=%d, expect=0x%08x%08x, actual=0x%08x%08x\n", 6) \
X( 7, RGXFW_GROUP_BIF, RGXFW_SF_BIF_TRUST, "Trust enabled:%d, for BIFreq=%d\n", 2) \
X( 8, RGXFW_GROUP_BIF, RGXFW_SF_BIF_TILECFG, "BIF Tiling Cfg %d base %08x%08x len %08x%08x enable %d stride %d --> %08x%08x\n", 9) \
X( 9, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSID0, "Wrote the Value %d to OSID0, Cat Base %d, Register's contents are now %08x %08x\n", 4) \
X(10, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSID1, "Wrote the Value %d to OSID1, Context  %d, Register's contents are now %04x\n", 3) \
X(11, RGXFW_GROUP_BIF, RGXFW_SF_BIF_OSIDx, "ui32OSid = %u, Catbase = %u, Reg Address = %x, Reg index = %u, Bitshift index = %u, Val = %08x%08x\n", 7) \
\
X( 1, RGXFW_GROUP_PM, RGXFW_SF_PM_AMLIST, "ALIST%d SP = %u, MLIST%d SP = %u (VCE 0x%08x%08x, TE 0x%08x%08x, ALIST 0x%08x%08x)\n", 10) \
X( 2, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_SHARED, "Is TA: %d, finished: %d on HW %u (HWRTData = 0x%08x, MemCtx = 0x%08x). FL different between TA/3D: global:%d, local:%d, mmu:%d\n", 8) \
X( 3, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_3DBASE, "UFL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-3D-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), MFL-3D-Base: 0x%08x%08x (SP = %u, 4PT = %u)\n", 14) \
X( 4, RGXFW_GROUP_PM, RGXFW_SF_PM_UFL_TABASE, "UFL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), FL-TA-Base: 0x%08x%08x (SP = %u, 4PB = %u, 4PT = %u), MFL-TA-Base: 0x%08x%08x (SP = %u, 4PT = %u)\n", 14) \
X( 5, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_GROW_COMPLETE, "Freelist grow completed [0x%08x]: added pages 0x%08x, total pages 0x%08x, new DevVirtAddr 0x%08x%08x\n", 5) \
X( 6, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_GROW_DENIED, "Grow for freelist ID=0x%08x denied by host\n", 1) \
X( 7, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_UPDATE_COMPLETE, "Freelist update completed [0x%08x]: old total pages 0x%08x, new total pages 0x%08x, new DevVirtAddr 0x%08x%08x\n", 5) \
X( 8, RGXFW_GROUP_PM, RGXFW_SF_PM_FL_RECONSTRUCTION_FAILED_DEPRECATED, "Reconstruction of freelist ID=0x%08x failed\n", 1) \
X( 9, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_WARNING, "Ignored attempt to pause or unpause the DM while there is no relevant operation in progress (0-TA,1-3D): %d, operation(0-unpause, 1-pause): %d\n", 2) \
X( 10, RGXFW_GROUP_PM, RGXFW_SF_PM_3D_TIMEOUT_STATUS, "Force free 3D Context memory, FWCtx: %08x, status(1:success, 0:fail): %d\n", 2)\
X( 11, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_ALLOC, "PM pause TA ALLOC: PM_PAGE_MANAGEOP set to 0x%x\n", 1) \
X( 12, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_UNPAUSE_ALLOC, "PM unpause TA ALLOC: PM_PAGE_MANAGEOP set to 0x%x\n", 1) \
X( 13, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_DALLOC, "PM pause 3D DALLOC: PM_PAGE_MANAGEOP set to 0x%x\n", 1) \
X( 14, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_UNPAUSE_DALLOC, "PM unpause 3D DALLOC: PM_PAGE_MANAGEOP set to 0x%x\n", 1) \
X( 15, RGXFW_GROUP_PM, RGXFW_SF_PM_DM_PAUSE_FAILED, "PM ALLOC/DALLOC change was not actioned: PM_PAGE_MANAGEOP_STATUS=0x%x\n", 1) \
\
X( 1, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GLL_DYNAMIC_STATUS, "Global link list dynamic page count: vertex 0x%x, varying 0x%x, node 0x%x\n", 3) \
X( 2, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GLL_STATIC_STATUS, "Global link list static page count: vertex 0x%x, varying 0x%x, node 0x%x\n", 3) \
X( 3, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_WAIT_FOR_GROW, "RPM request failed. Waiting for freelist grow.\n", 0) \
X( 4, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_ABORT, "RPM request failed. Aborting the current frame.\n", 0) \
X( 5, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_WAIT_FOR_PENDING_GROW, "RPM waiting for pending grow on freelist 0x%08x\n", 1) \
X( 6, RGXFW_GROUP_RPM, RGXFW_SF_RPM_REQUEST_HOST_GROW, "Request freelist grow [0x%08x] current pages %d, grow size %d\n", 3) \
X( 7, RGXFW_GROUP_RPM, RGXFW_SF_RPM_FREELIST_LOAD, "Freelist load: SHF = 0x%08x, SHG = 0x%08x\n", 2) \
X( 8, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_DEPRECATED, "SHF FPL register: 0x%08X.%08X\n", 2) \
X( 9, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_DEPRECATED, "SHG FPL register: 0x%08X.%08X\n", 2) \
X(10, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_FREELIST, "Kernel requested RPM grow on freelist (type %d) at 0x%08x from current size %d to new size %d, RPM restart: %d (1=Yes)\n", 5) \
X(11, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_RESTART, "Restarting SHG\n", 0) \
X(12, RGXFW_GROUP_RPM, RGXFW_SF_RPM_GROW_ABORTED, "Grow failed, aborting the current frame.\n", 0) \
X(13, RGXFW_GROUP_RPM, RGXFW_SF_RPM_ABORT_COMPLETE, "RPM abort complete on HWFrameData [0x%08x].\n", 1) \
X(14, RGXFW_GROUP_RPM, RGXFW_SF_RPM_CLEANUP_NEEDS_ABORT, "RPM freelist cleanup [0x%08x] requires abort to proceed.\n", 1) \
X(15, RGXFW_GROUP_RPM, RGXFW_SF_RPM_RPM_PT, "RPM page table base register: 0x%08X.%08X\n", 2) \
X(16, RGXFW_GROUP_RPM, RGXFW_SF_RPM_OOM_ABORT, "Issuing RPM abort.\n", 0) \
X(17, RGXFW_GROUP_RPM, RGXFW_SF_RPM_OOM_TOGGLE_CHECK_FULL, "RPM OOM received but toggle bits indicate free pages available\n", 0) \
X(18, RGXFW_GROUP_RPM, RGXFW_SF_RPM_STATE_HW_TIMEOUT, "RPM hardware timeout. Unable to process OOM event.\n", 0) \
X(19, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_LOAD, "SHF FL (0x%08x) load, FPL: 0x%08X.%08X, roff: 0x%08X, woff: 0x%08X\n", 5) \
X(20, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_LOAD, "SHG FL (0x%08x) load, FPL: 0x%08X.%08X, roff: 0x%08X, woff: 0x%08X\n", 5) \
X(21, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHF_FPL_STORE, "SHF FL (0x%08x) store, roff: 0x%08X, woff: 0x%08X\n", 3) \
X(22, RGXFW_GROUP_RPM, RGXFW_SF_RPM_SHG_FPL_STORE, "SHG FL (0x%08x) store, roff: 0x%08X, woff: 0x%08X\n", 3) \
\
X( 1, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_FINISHED, "3D RTData 0x%08x finished on HW context %u\n", 2) \
X( 2, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_READY, "3D RTData 0x%08x ready on HW context %u\n", 2) \
X( 3, RGXFW_GROUP_RTD, RGXFW_SF_RTD_PB_SET_TO, "CONTEXT_PB_BASE set to %X, FL different between TA/3D: local: %d, global: %d, mmu: %d\n", 4) \
X( 4, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOADVFP_3D, "Loading VFP table 0x%08x%08x for 3D\n", 2) \
X( 5, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOADVFP_TA, "Loading VFP table 0x%08x%08x for TA\n", 2) \
X( 6, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL_DEPRECATED, "Load Freelist 0x%X type: %d (0:local,1:global,2:mmu) for DM%d: TotalPMPages = %d, FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u\n", 10) \
X( 7, RGXFW_GROUP_RTD, RGXFW_SF_RTD_VHEAP_STORE, "Perform VHEAP table store\n", 0) \
X( 8, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_MATCH_FOUND, "RTData 0x%08x: found match in Context=%d: Load=No, Store=No\n", 2) \
X( 9, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_NULL_FOUND, "RTData 0x%08x: found NULL in Context=%d: Load=Yes, Store=No\n", 2) \
X(10, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_3D_FINISHED, "RTData 0x%08x: found state 3D finished (0x%08x) in Context=%d: Load=Yes, Store=Yes \n", 3) \
X(11, RGXFW_GROUP_RTD, RGXFW_SF_RTD_RTDATA_TA_FINISHED, "RTData 0x%08x: found state TA finished (0x%08x) in Context=%d: Load=Yes, Store=Yes \n", 3) \
X(12, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_STACK_POINTERS, "Loading stack-pointers for %d (0:MidTA,1:3D) on context %d, MLIST = 0x%08x, ALIST = 0x%08x%08x\n", 5) \
X(13, RGXFW_GROUP_RTD, RGXFW_SF_RTD_STORE_PB_DEPRECATED, "Store Freelist 0x%X type: %d (0:local,1:global,2:mmu) for DM%d: TotalPMPages = %d, FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u\n", 10) \
X(14, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_FINISHED, "TA RTData 0x%08x finished on HW context %u\n", 2) \
X(15, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_LOADED, "TA RTData 0x%08x loaded on HW context %u\n", 2) \
X(16, RGXFW_GROUP_RTD, RGXFW_SF_RTD_STORE_PB, "Store Freelist 0x%X type: %d (0:local,1:global,2:mmu) for DM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u\n", 12) \
X(17, RGXFW_GROUP_RTD, RGXFW_SF_RTD_LOAD_FL, "Load  Freelist 0x%X type: %d (0:local,1:global,2:mmu) for DM%d: FL Total Pages %u (max=%u,grow size=%u), FL-addr = 0x%08x%08x, stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u\n", 12) \
X(18, RGXFW_GROUP_RTD, RGXFW_SF_RTD_DEBUG, "Freelist 0x%X RESET!!!!!!!!\n", 1) \
X(19, RGXFW_GROUP_RTD, RGXFW_SF_RTD_DEBUG2, "Freelist 0x%X stacktop = 0x%08x%08x, Alloc Page Count = %u, Alloc MMU Page Count = %u\n", 5) \
X(20, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_DEPRECATED, "Request reconstruction of Freelist 0x%X type: %d (0:local,1:global,2:mmu) on HW context %u\n", 3) \
X(21, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_ACK_DEPRECATED, "Freelist reconstruction ACK from host (HWR state :%u)\n", 1) \
X(22, RGXFW_GROUP_RTD, RGXFW_SF_RTD_FL_RECON_ACK_DEPRECATED2, "Freelist reconstruction completed\n", 0) \
X(23, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_LOADED_DEPRECATED, "TA RTData 0x%08x loaded on HW context %u HWRTDataNeedsLoading=%d\n", 3) \
X(24, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TE_RGNHDR_INFO, "TE Region headers base 0x%08x%08x (RGNHDR Init: %d)\n", 3) \
X(25, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RTDATA_BUFFER_ADDRS, "TA Buffers: FWCtx 0x%08x, RT 0x%08x, RTData 0x%08x, VHeap 0x%08x%08x, TPC 0x%08x%08x (MemCtx 0x%08x)\n", 8) \
X(26, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_LOADED, "3D RTData 0x%08x loaded on HW context %u\n", 2) \
X(27, RGXFW_GROUP_RTD, RGXFW_SF_RTD_3D_RTDATA_BUFFER_ADDRS, "3D Buffers: FWCtx 0x%08x, RT 0x%08x, RTData 0x%08x (MemCtx 0x%08x)\n", 4) \
X(28, RGXFW_GROUP_RTD, RGXFW_SF_RTD_TA_RESTART_AFTER_PR_EXECUTED, "Restarting TA after partial render, HWRTData0State=%d, HWRTData1State=%d\n", 2) \
\
X( 1, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZLOAD_DEPRECATED, "Force Z-Load for partial render\n", 0) \
X( 2, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSTORE_DEPRECATED, "Force Z-Store for partial render\n", 0) \
X( 3, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_LOCAL, "3D MemFree: Local FL 0x%08x\n", 1) \
X( 4, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_MMU, "3D MemFree: MMU FL 0x%08x\n", 1) \
X( 5, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_GLOBAL, "3D MemFree: Global FL 0x%08x\n", 1) \
X( 6, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD, "OOM TA/3D PR Check: [%08.8X] is %08.8X requires %08.8X, HardwareSync Fence [%08.8X] is %08.8X requires %08.8X\n", 6) \
X( 7, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD_UN_FL, "OOM TA_cmd=0x%08x, U-FL 0x%08x, N-FL 0x%08x\n", 3) \
X( 8, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OOM_TACMD_UN_MMU_FL, "OOM TA_cmd=0x%08x, OOM MMU:%d, U-FL 0x%08x, N-FL 0x%08x, MMU-FL 0x%08x\n", 5) \
X( 9, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_AVOIDED, "Partial render avoided\n", 0) \
X(10, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_DISCARDED, "Partial render discarded\n", 0) \
X(11, RGXFW_GROUP_SPM, RGXFW_SF_SPM_PRENDER_FINISHED, "Partial Render finished\n", 0) \
X(12, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_3DBG, "SPM Owner = 3D-BG\n", 0) \
X(13, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_3DIRQ, "SPM Owner = 3D-IRQ\n", 0) \
X(14, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_NONE, "SPM Owner = NONE\n", 0) \
X(15, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_TABG, "SPM Owner = TA-BG\n", 0) \
X(16, RGXFW_GROUP_SPM, RGXFW_SF_SPM_OWNER_TAIRQ, "SPM Owner = TA-IRQ\n", 0) \
X(17, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSTORE_ADDRESS, "ZStore address 0x%08x%08x\n", 2) \
X(18, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SSTORE_ADDRESS, "SStore address 0x%08x%08x\n", 2) \
X(19, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZLOAD_ADDRESS, "ZLoad address 0x%08x%08x\n", 2) \
X(20, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SLOAD_ADDRESS, "SLoad address 0x%08x%08x\n", 2) \
X(21, RGXFW_GROUP_SPM, RGXFW_SF_SPM_NO_DEFERRED_ZSBUFFER, "No deferred ZS Buffer provided\n", 0) \
X(22, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_POPULATED, "ZS Buffer successfully populated (ID=0x%08x)\n", 1) \
X(23, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_POP_UNNEEDED, "No need to populate ZS Buffer (ID=0x%08x)\n", 1) \
X(24, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNPOPULATED, "ZS Buffer successfully unpopulated (ID=0x%08x)\n", 1) \
X(25, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNPOP_UNNEEDED, "No need to unpopulate ZS Buffer (ID=0x%08x)\n", 1) \
X(26, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST, "Send ZS-Buffer backing request to host (ID=0x%08x)\n", 1) \
X(27, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNBACKING_REQUEST, "Send ZS-Buffer unbacking request to host (ID=0x%08x)\n", 1) \
X(28, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST_PENDING, "Don't send ZS-Buffer backing request. Previous request still pending (ID=0x%08x)\n", 1) \
X(29, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_UNBACKING_REQUEST_PENDING, "Don't send ZS-Buffer unbacking request. Previous request still pending (ID=0x%08x)\n", 1) \
X(30, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZBUFFER_NOT_READY, "Partial Render waiting for ZBuffer to be backed (ID=0x%08x)\n", 1) \
X(31, RGXFW_GROUP_SPM, RGXFW_SF_SPM_SBUFFER_NOT_READY, "Partial Render waiting for SBuffer to be backed (ID=0x%08x)\n", 1) \
X(32, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_NONE, "SPM State = none\n", 0) \
X(33, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_BLOCKED, "SPM State = PR blocked\n", 0) \
X(34, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_GROW, "SPM State = wait for grow\n", 0) \
X(35, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_HW, "SPM State = wait for HW\n", 0) \
X(36, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_RUNNING, "SPM State = PR running\n", 0) \
X(37, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_AVOIDED, "SPM State = PR avoided\n", 0) \
X(38, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_PR_EXECUTED, "SPM State = PR executed\n", 0) \
X(39, RGXFW_GROUP_SPM, RGXFW_SF_SPM_FREELIST_MATCH, "3DMemFree matches freelist 0x%08x (FL type = %u)\n", 2) \
X(40, RGXFW_GROUP_SPM, RGXFW_SF_SPM_3DMEMFREE_FLAG_SET, "Raise the 3DMemFreeDedected flag\n", 0) \
X(41, RGXFW_GROUP_SPM, RGXFW_SF_SPM_STATE_WAIT_FOR_PENDING_GROW, "Wait for pending grow on Freelist 0x%08x\n", 1) \
X(42, RGXFW_GROUP_SPM, RGXFW_SF_SPM_ZSBUFFER_BACKING_REQUEST_FAILED, "ZS Buffer failed to be populated (ID=0x%08x)\n", 1) \
\
X( 1, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECK_DEPRECATED, "Check Pow state DM%d int: 0x%X, ext: 0x%X, pow flags: 0x%X\n", 4) \
X( 2, RGXFW_GROUP_POW, RGXFW_SF_POW_SIDEKICK_IDLE, "Sidekick idle (might be powered down). Pow state int: 0x%X, ext: 0x%X, flags: 0x%X\n", 3) \
X( 3, RGXFW_GROUP_POW, RGXFW_SF_POW_OSREQ, "OS requested pow off (forced = %d), DM%d, pow flags: 0x%8.8X\n", 3) \
X( 4, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_DEPRECATED, "Initiate powoff query. Inactive DMs: %d %d %d %d\n", 4) \
X( 5, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECKOFF_DEPRECATED, "Any RD-DM pending? %d, Any RD-DM Active? %d\n", 2) \
X( 6, RGXFW_GROUP_POW, RGXFW_SF_POW_SIDEKICK_OFF, "Sidekick ready to be powered down. Pow state int: 0x%X, ext: 0x%X, flags: 0x%X\n", 3) \
X( 7, RGXFW_GROUP_POW, RGXFW_SF_POW_HWREQ, "HW Request On(1)/Off(0): %d, Units: 0x%08.8X\n", 2) \
X( 8, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE_REQ, "Request to change num of dusts to %d (Power flags=%d)\n", 2) \
X( 9, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE, "Changing number of dusts from %d to %d\n", 2) \
X(11, RGXFW_GROUP_POW, RGXFW_SF_POW_SIDEKICK_INIT_DEPRECATED, "Sidekick init\n", 0) \
X(12, RGXFW_GROUP_POW, RGXFW_SF_POW_RD_INIT_DEPRECATED, "Rascal+Dusts init (# dusts mask: %X)\n", 1) \
X(13, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_RD, "Initiate powoff query for RD-DMs.\n", 0) \
X(14, RGXFW_GROUP_POW, RGXFW_SF_POW_INIOFF_TLA, "Initiate powoff query for TLA-DM.\n", 0) \
X(15, RGXFW_GROUP_POW, RGXFW_SF_POW_REQUESTEDOFF_RD, "Any RD-DM pending? %d, Any RD-DM Active? %d\n", 2) \
X(16, RGXFW_GROUP_POW, RGXFW_SF_POW_REQUESTEDOFF_TLA, "TLA-DM pending? %d, TLA-DM Active? %d\n", 2) \
X(17, RGXFW_GROUP_POW, RGXFW_SF_POW_BRN37566, "Request power up due to BRN37566. Pow stat int: 0x%X\n", 1) \
X(18, RGXFW_GROUP_POW, RGXFW_SF_POW_REQ_CANCEL, "Cancel power off request int: 0x%X, ext: 0x%X, pow flags: 0x%X\n", 3) \
X(19, RGXFW_GROUP_POW, RGXFW_SF_POW_FORCED_IDLE, "OS requested forced IDLE, pow flags: 0x%X\n", 1) \
X(20, RGXFW_GROUP_POW, RGXFW_SF_POW_CANCEL_FORCED_IDLE, "OS cancelled forced IDLE, pow flags: 0x%X\n", 1) \
X(21, RGXFW_GROUP_POW, RGXFW_SF_POW_IDLE_TIMER, "Idle timer start. Pow state int: 0x%X, ext: 0x%X, flags: 0x%X\n", 3) \
X(22, RGXFW_GROUP_POW, RGXFW_SF_POW_CANCEL_IDLE_TIMER, "Cancel idle timer. Pow state int: 0x%X, ext: 0x%X, flags: 0x%X\n", 3) \
X(23, RGXFW_GROUP_POW, RGXFW_SF_POW_APM_LATENCY_CHANGE, "Active PM latency set to %dms. Core clock: %d Hz\n", 2) \
X(24, RGXFW_GROUP_POW, RGXFW_SF_POW_CDM_CLUSTERS, "Compute cluster mask change to 0x%X, %d dusts powered.\n", 2) \
X(25, RGXFW_GROUP_POW, RGXFW_SF_POW_NULL_CMD_INIOFF_RD, "Null command executed, repeating initiate powoff query for RD-DMs.\n", 0) \
X(26, RGXFW_GROUP_POW, RGXFW_SF_POW_POWMON_ENERGY, "Power monitor: Estimate of dynamic energy %u\n", 1) \
X(27, RGXFW_GROUP_POW, RGXFW_SF_POW_CHECK, "Check Pow state: Int: 0x%X, Ext: 0x%X, Pow flags: 0x%X\n", 3) \
X(28, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_NEW_DEADLINE, "Proactive DVFS: New deadline, time = 0x%08x%08x\n", 2) \
X(29, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_NEW_WORKLOAD, "Proactive DVFS: New workload, cycles = 0x%08x%08x\n", 2) \
X(30, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_CALCULATE, "Proactive DVFS: Proactive frequency calculated = 0x%08x\n", 1) \
X(31, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UTILISATION, "Proactive DVFS: Reactive utilisation = 0x%08x\n", 1) \
X(32, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_REACT, "Proactive DVFS: Reactive frequency calculated = 0x%08x%08x\n", 2) \
X(33, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GPIO_SEND, "Proactive DVFS: OPP Point Sent = 0x%x\n", 1) \
X(34, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_DEADLINE_REMOVED, "Proactive DVFS: Deadline removed = 0x%08x%08x\n", 2) \
X(35, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_WORKLOAD_REMOVED, "Proactive DVFS: Workload removed = 0x%08x%08x\n", 2) \
X(36, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_THROTTLE, "Proactive DVFS: Throttle to a maximum = 0x%x\n", 1) \
X(37, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GPIO_FAILURE, "Proactive DVFS: Failed to pass OPP point via GPIO.\n", 0) \
X(38, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_INVALID_NODE, "Proactive DVFS: Invalid node passed to function.\n", 0) \
X(39, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_GUEST_BAD_ACCESS, "Proactive DVFS: Guest OS attempted to do a privileged action. OSid = %u\n", 1) \
X(40, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UNPROFILED_STARTED, "Proactive DVFS: Unprofiled work started. Total unprofiled work present: 0x%x\n", 1) \
X(41, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_UNPROFILED_FINISHED, "Proactive DVFS: Unprofiled work finished. Total unprofiled work present: 0x%x\n", 1) \
X(42, RGXFW_GROUP_POW, RGXFW_SF_POW_PDVFS_DISABLED, "Proactive DVFS: Disabled: Not enabled by host.\n", 0) \
X(43, RGXFW_GROUP_POW, RGXFW_SF_POW_HWREQ_RESULT, "HW Request Completed(1)/Aborted(0): %d, Ticks: %d\n", 2) \
X(44, RGXFW_GROUP_POW, RGXFW_SF_POW_DUSTS_CHANGE_FIX_59042, "Allowed number of dusts is %d due to BRN59042.\n", 1) \
X(45, RGXFW_GROUP_POW, RGXFW_SF_POW_HOST_TIMEOUT_NOTIFICATION, "Host timed out while waiting for a forced idle state. Pow state int: 0x%X, ext: 0x%X, flags: 0x%X\n", 3) \
\
X(1, RGXFW_GROUP_HWR, RGXFW_SF_HWR_LOCKUP_DEPRECATED, "Lockup detected on DM%d, FWCtx: %08.8X\n", 2) \
X(2, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_FW_DEPRECATED, "Reset fw state for DM%d, FWCtx: %08.8X, MemCtx: %08.8X\n", 3) \
X(3, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW_DEPRECATED, "Reset HW\n", 0) \
X(4, RGXFW_GROUP_HWR, RGXFW_SF_HWR_TERMINATED_DEPRECATED, "Lockup recovered.\n", 0) \
X(5, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP_DEPRECATED, "Lock-up DM%d FWCtx: %08.8X \n", 2) \
X(6, RGXFW_GROUP_HWR, RGXFW_SF_HWR_LOCKUP_DETECTED_DEPRECATED, "Lockup detected: GLB(%d->%d), PER-DM(0x%08X->0x%08X)\n", 4) \
X(7, RGXFW_GROUP_HWR, RGXFW_SF_HWR_EARLY_FAULT_DETECTION_DEPRECATED, "Early fault detection: GLB(%d->%d), PER-DM(0x%08X)\n", 3) \
X(8, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_LOCKUP_DEPRECATED, "Hold scheduling due lockup: GLB(%d), PER-DM(0x%08X->0x%08X)\n", 3) \
X(9, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FALSE_LOCKUP_DEPRECATED, "False lockup detected: GLB(%d->%d), PER-DM(0x%08X->0x%08X)\n", 4) \
X(10, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BRN37729_DEPRECATED, "BRN37729: GLB(%d->%d), PER-DM(0x%08X->0x%08X)\n", 4) \
X(11, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FREELISTS_RECONSTRUCTED_DEPRECATED, "Freelists reconstructed: GLB(%d->%d), PER-DM(0x%08X)\n", 3) \
X(12, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RECONSTRUCTING_FREELISTS_DEPRECATED, "Reconstructing freelists: %u (0-No, 1-Yes): GLB(%d->%d), PER-DM(0x%08X)\n", 4) \
X(13, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FAILED_HW_POLL, "HW poll %u (0-Unset 1-Set) failed (reg:0x%08X val:0x%08X)\n", 3) \
X(14, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_DISCARDED_DEPRECATED, "Discarded cmd on DM%u FWCtx=0x%08X\n", 2) \
X(15, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_DISCARDED, "Discarded cmd on DM%u (reason=%u) HWRTData=0x%08X (st: %d), FWCtx 0x%08X @ %d\n", 6) \
X(16, RGXFW_GROUP_HWR, RGXFW_SF_HWR_PM_FENCE, "PM fence WA could not be applied, Valid TA Setup: %d, RD powered off: %d\n", 2) \
X(17, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_SNAPSHOT, "FL snapshot RTD 0x%08.8X - local (0x%08.8X): %d, global (0x%08.8X): %d\n", 5) \
X(18, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_CHECK, "FL check RTD 0x%08.8X, discard: %d - local (0x%08.8X): s%d?=c%d, global (0x%08.8X): s%d?=c%d\n", 8) \
X(19, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_DEPRECATED, "FL reconstruction 0x%08.8X c%d\n", 2) \
X(20, RGXFW_GROUP_HWR, RGXFW_SF_HWR_3D_CHECK, "3D check: missing TA FWCtx 0x%08.8X @ %d, RTD 0x%08x.\n", 3) \
X(21, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW_DEPRECATED2, "Reset HW (mmu:%d, extmem: %d)\n", 2) \
X(22, RGXFW_GROUP_HWR, RGXFW_SF_HWR_ZERO_TA_CACHES, "Zero TA caches for FWCtx: %08.8X (TPC addr: %08X%08X, size: %d bytes)\n", 4) \
X(23, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FREELISTS_RECONSTRUCTED, "Recovery DM%u: Freelists reconstructed. New R-Flags=0x%08X\n", 2) \
X(24, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SKIPPED_CMD, "Recovery DM%u: FWCtx 0x%08x skipped to command @ %u. PR=%u. New R-Flags=0x%08X \n", 5) \
X(25, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_RECOVERED, "Recovery DM%u: DM fully recovered\n", 1) \
X(26, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_LOCKUP, "DM%u: Hold scheduling due to R-Flag = 0x%08x\n", 2) \
X(27, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_RECONSTRUCTION, "Analysis: Need freelist reconstruction\n", 0) \
X(28, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_SKIP, "Analysis DM%u: Lockup FWCtx: %08.8X. Need to skip to next command\n", 2) \
X(29, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_SKIP_OOM_TA, "Analysis DM%u: Lockup while TA is OOM FWCtx: %08.8X. Need to skip to next command\n", 2) \
X(30, RGXFW_GROUP_HWR, RGXFW_SF_HWR_NEEDS_PR_CLEANUP, "Analysis DM%u: Lockup while partial render FWCtx: %08.8X. Need PR cleanup\n", 2) \
X(31, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_LOCKUP, "GPU has locked up\n", 0) \
X(32, RGXFW_GROUP_HWR, RGXFW_SF_HWR_READY, "DM%u ready for HWR\n", 1) \
X(33, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_UPDATE_RECOVERY, "Recovery DM%u: Updated Recovery counter. New R-Flags=0x%08X\n", 2) \
X(34, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BRN37729, "Analysis: BRN37729 detected, reset TA and re-kicked 0x%08X)\n", 1) \
X(35, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_TIMED_OUT, "DM%u timed out\n", 1) \
X(36, RGXFW_GROUP_HWR, RGXFW_SF_HWR_EVENT_STATUS_REG, "RGX_CR_EVENT_STATUS=0x%08x\n", 1) \
X(37, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DM_FALSE_LOCKUP, "DM%u lockup falsely detected, R-Flags=0x%08X\n", 2) \
X(38, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_OUTOFTIME, "GPU has overrun its deadline\n", 0) \
X(39, RGXFW_GROUP_HWR, RGXFW_SF_HWR_SET_POLLFAILURE, "GPU has failed a poll\n", 0) \
X(40, RGXFW_GROUP_HWR, RGXFW_SF_HWR_PERF_PHASE_REG, "RGX DM%u phase count=0x%08x\n", 2) \
X(41, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_HW, "Reset HW (loop:%d, poll failures: 0x%08X)\n", 2) \
X(42, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MMU_FAULT_EVENT, "MMU fault event: 0x%08X\n", 1) \
X(43, RGXFW_GROUP_HWR, RGXFW_SF_HWR_BIF1_FAULT, "BIF1 page fault detected (Bank1 MMU Status: 0x%08X)\n", 1) \
X(44, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CRC_CHECK_TRUE_DEPRECATED, "Fast CRC Failed. Proceeding to full register checking (DM: %u).\n", 1) \
X(45, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MMU_META_FAULT, "Meta MMU page fault detected (Meta MMU Status: 0x%08X%08X)\n", 2) \
X(46, RGXFW_GROUP_HWR, RGXFW_SF_HWR_CRC_CHECK, "Fast CRC Check result for DM%u is HWRNeeded=%u\n", 2) \
X(47, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FULL_CHECK, "Full Signature Check result for DM%u is HWRNeeded=%u\n", 2) \
X(48, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FINAL_RESULT, "Final result for DM%u is HWRNeeded=%u with HWRChecksToGo=%u\n", 3) \
X(49, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_CHECK, "USC Slots result for DM%u is HWRNeeded=%u USCSlotsUsedByDM=%d\n", 3) \
X(50, RGXFW_GROUP_HWR, RGXFW_SF_HWR_DEADLINE_CHECK, "Deadline counter for DM%u is HWRDeadline=%u\n", 2) \
X(51, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_FREELIST_DEPRECATED, "Holding Scheduling on OSid %u due to pending freelist reconstruction\n", 1) \
X(52, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_REQUEST, "Requesting reconstruction for freelist 0x%X (ID=%d)\n", 2) \
X(53, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_PASSED, "Reconstruction of freelist ID=%d complete\n", 1) \
X(54, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_NEEDED, "Reconstruction needed for freelist 0x%X (ID=%d) type: %d (0:local,1:global,2:mmu) on HW context %u\n", 4) \
X(55, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FL_RECON_FAILED, "Reconstruction of freelist ID=%d failed\n", 1) \
X(56, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESTRICTING_PDS_TASKS, "Restricting PDS Tasks to help other stalling DMs (RunningMask=0x%02X, StallingMask=0x%02X, PDS_CTRL=0x%08X%08X)\n", 4) \
X(57, RGXFW_GROUP_HWR, RGXFW_SF_HWR_UNRESTRICTING_PDS_TASKS, "Unrestricting PDS Tasks again (RunningMask=0x%02X, StallingMask=0x%02X, PDS_CTRL=0x%08X%08X)\n", 4) \
X(58, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_USED, "USC slots: %u used by DM%u\n", 2) \
X(59, RGXFW_GROUP_HWR, RGXFW_SF_HWR_USC_SLOTS_EMPTY, "USC slots: %u empty\n", 1) \
X(60, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HCS_FIRE, "HCS DM%d's Context Switch failed to meet deadline. Current time: %08x%08x, deadline: %08x%08x\n", 5) \
X(61, RGXFW_GROUP_HWR, RGXFW_SF_HWR_START_HW_RESET, "Begin hardware reset (HWR Counter=%d)\n", 1) \
X(62, RGXFW_GROUP_HWR, RGXFW_SF_HWR_FINISH_HW_RESET, "Finished hardware reset (HWR Counter=%d)\n", 1) \
X(63, RGXFW_GROUP_HWR, RGXFW_SF_HWR_HOLD_SCHEDULING_DUE_TO_FREELIST, "Holding Scheduling on DM %u for OSid %u due to pending freelist reconstruction\n", 2) \
X(64, RGXFW_GROUP_HWR, RGXFW_SF_HWR_RESET_UMQ_READ_OFFSET, "User Mode Queue ROff reset: FWCtx 0x%08.8X, queue: 0x%08X%08X (Roff = %u becomes StreamStartOffset = %u)\n", 5) \
X(65, RGXFW_GROUP_HWR, RGXFW_SF_HWR_MIPS_FAULT, "Mips page fault detected (BadVAddr: 0x%08x, EntryLo0: 0x%08x, EntryLo1: 0x%08x)\n", 3) \
X(67, RGXFW_GROUP_HWR, RGXFW_SF_HWR_ANOTHER_CHANCE, "At least one other DM is running okay so DM%u will get another chance\n", 1) \
\
X( 1, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CFGBLK, "Block 0x%x mapped to Config Idx %u\n", 2) \
X( 2, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_OMTBLK, "Block 0x%x omitted from event - not enabled in HW\n", 1) \
X( 3, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INCBLK, "Block 0x%x included in event - enabled in HW\n", 1) \
X( 4, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_SELREG, "Select register state hi_0x%x lo_0x%x\n", 2) \
X( 5, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CSBHDR, "Counter stream block header word 0x%x\n", 1) \
X( 6, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CTROFF, "Counter register offset 0x%x\n", 1) \
X( 7, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CFGSKP, "Block 0x%x config unset, skipping\n", 1) \
X( 8, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INDBLK, "Accessing Indirect block 0x%x\n", 1) \
X( 9, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DIRBLK, "Accessing Direct block 0x%x\n", 1) \
X(10, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CNTPRG, "Programmed counter select register at offset 0x%x\n", 1) \
X(11, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKPRG, "Block register offset 0x%x and value 0x%x\n", 2) \
X(12, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKCG, "Reading config block from driver 0x%x\n", 1) \
X(13, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKRG, "Reading block range 0x%x to 0x%x\n", 2) \
X(14, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_BLKREC, "Recording block 0x%x config from driver\n", 1) \
X(15, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_UBLKED, "Finished reading config block from driver\n", 0) \
X(16, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CUSTOM_COUNTER, "Custom Counter offset: %x   value: %x \n", 2) \
X(17, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_SELECT_CNTR, "Select counter n:%u  ID:%x\n", 2) \
X(18, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_SELECT_PACK, "The counter ID %x is not allowed. The package [b:%u, n:%u] will be discarded\n", 3) \
X(19, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CHANGE_FILTER_STATUS, "Custom Counters filter status %d\n", 1) \
X(20, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_WRONG_BLOCK, "The Custom block %d is not allowed. Use only blocks lower than %d. The package will be discarded\n", 2) \
X(21, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_DROP_TOO_MANY_ID, "The package will be discarded because it contains %d counters IDs while the upper limit is %d\n", 2) \
X(22, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CHECK_FILTER, "Check Filter %x is %x ?\n", 2) \
X(23, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_RESET_CUSTOM_BLOCK, "The custom block %u is reset\n", 1) \
X(24, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_INVALID_CMD, "Encountered an invalid command (%d)\n", 1) \
X(25, RGXFW_GROUP_HWP, RGXFW_SF_HWP_WAITING_FOR_QUEUE_DEPRECATED, "HWPerf Queue is full, we will have to wait for space! (Roff = %u, Woff = %u)\n", 2) \
X(26, RGXFW_GROUP_HWP, RGXFW_SF_HWP_WAITING_FOR_QUEUE_FENCE_DEPRECATED, "HWPerf Queue is fencing, we are waiting for Roff = %d (Roff = %u, Woff = %u)\n", 3) \
X(27, RGXFW_GROUP_HWP, RGXFW_SF_HWP_I_CUSTOM_BLOCK, "Custom Counter block: %d \n", 1) \
\
X( 1, RGXFW_GROUP_DMA, RGXFW_SF_DMA_TRANSFER_REQUEST, "Transfer 0x%02x request: 0x%02x%08x -> 0x%08x, size %u\n", 5) \
X( 2, RGXFW_GROUP_DMA, RGXFW_SF_DMA_TRANSFER_COMPLETE, "Transfer of type 0x%02x expected on channel %u, 0x%02x found, status %u\n", 4) \
X( 3, RGXFW_GROUP_DMA, RGXFW_SF_DMA_INT_REG, "DMA Interrupt register 0x%08x\n", 1) \
X( 4, RGXFW_GROUP_DMA, RGXFW_SF_DMA_WAIT, "Waiting for transfer ID %u completion...\n", 1) \
X( 5, RGXFW_GROUP_DMA, RGXFW_SF_DMA_CCB_LOADING_FAILED, "Loading of cCCB data from FW common context 0x%08x (offset: %u, size: %u) failed\n", 3) \
X( 6, RGXFW_GROUP_DMA, RGXFW_SF_DMA_CCB_LOAD_INVALID, "Invalid load of cCCB data from FW common context 0x%08x (offset: %u, size: %u)\n", 3) \
X( 7, RGXFW_GROUP_DMA, RGXFW_SF_DMA_POLL_FAILED, "Transfer 0x%02x request poll failure\n", 1) \
\
X(1, RGXFW_GROUP_DBG, RGXFW_SF_DBG_INTPAIR, "0x%8.8x 0x%8.8x\n", 2) \
\
X(65535, RGXFW_GROUP_NULL, RGXFW_SF_LAST, "You should not use this string\n", 15)


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
#define RGXFW_LOG_IDMARKER			(0x70000000)
#define RGXFW_LOG_CREATESFID(a,b,e) ((a) | (b<<12) | (e<<16)) | RGXFW_LOG_IDMARKER

#define RGXFW_LOG_IDMASK			(0xFFF00000)
#define RGXFW_LOG_VALIDID(I)		(((I) & RGXFW_LOG_IDMASK) == RGXFW_LOG_IDMARKER)

typedef enum RGXFW_LOG_SFids { 
#define X(a, b, c, d, e) c = RGXFW_LOG_CREATESFID(a,b,e),
	RGXFW_LOG_SFIDLIST 
#undef X
} RGXFW_LOG_SFids;

/* Return the group id that the given (enum generated) id belongs to */
#define RGXFW_SF_GID(x) (((x)>>12) & 0xf)
/* Returns how many arguments the SF(string format) for the given (enum generated) id requires */
#define RGXFW_SF_PARAMNUM(x) (((x)>>16) & 0xf)

#endif /* RGX_FWIF_SF_H */

