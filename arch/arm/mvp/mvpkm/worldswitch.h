/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 *  @file
 *
 *  @brief Definition of the world switch page
 *
 *  Two pages are maintained to facilitate switching from the vmx to
 *  the monitor - a data and code page. The data page contains:
 *   - the necessary information about itself (its MPN, KVA, ...)
 *   - the saved register file of the other world (including some cp15 regs)
 *   - some information about the monitor's address space (the monVA member)
 *     that needed right after the w.s before any communication channels
 *     could have been established
 *   - a world switch related L2 table of the monitor -- this could be
 *     elsewhere.
 *
 *   The code page contains:
 *   - the actual switching code that saves/restores the registers
 *
 *   The world switch data page is mapped into the user, kernel, and the monitor
 *   address spaces. In case of the user and monitor spaces the global variable
 *   wsp points to the world switch page (in the vmx and the monitor
 *   respectively). The kernel address of the world switch page is saved on
 *   the page itself: wspHKVA.
 *
 *   The kernel virtual address for both code and data pages is mapped into
 *   the monitor's space temporarily at the time of the actual switch. This is
 *   needed to provide a stable code and data page while the L1 page table
 *   base is changing. As the monitor does not need the world switch data page
 *   at its KVA for its internal operation, that map is severed right after the
 *   switching to the monitor and re-established before switching back.
 */
#ifndef _WORLDSWITCH_H
#define _WORLDSWITCH_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/**
 *  @brief Area for saving the monitor/kernel register files.
 *
 *  The order of the registers in this structure was designed to
 *  facilitate the organization of the switching code. For example
 *  all Supervisor Mode registers are grouped together allowing the
 * @code
 *      switch to svc,
 *      stm old svc regs
 *      ldm new svc regs
 * @endcode
 *  code to work using a single base register for both the store and
 *  load area.
 */
#define MAX_REGISTER_SAVE_SIZE   464

#ifndef __ASSEMBLER__
typedef struct {
   uint32 kSPSR_svc;
   uint32 kr13_svc;
   uint32 kr14_svc;
   uint32 mSPSR_svc;
   uint32 mR13_svc;
   uint32 mR14_svc;

   uint32 kSPSR_abt;
   uint32 kr13_abt;
   uint32 kr14_abt;
   uint32 mSPSR_abt;
   uint32 mR13_abt;
   uint32 mR14_abt;

   uint32 kSPSR_und;
   uint32 kr13_und;
   uint32 kr14_und;
   uint32 mSPSR_und;
   uint32 mR13_und;
   uint32 mR14_und;

   uint32 kSPSR_irq;
   uint32 kr13_irq;
   uint32 kr14_irq;
   uint32 mSPSR_irq;
   uint32 mR13_irq;
   uint32 mR14_irq;

   uint32 kSPSR_fiq;
   uint32 kr8_fiq;
   uint32 kr9_fiq;
   uint32 kr10_fiq;
   uint32 kr11_fiq;
   uint32 kr12_fiq;
   uint32 kr13_fiq;
   uint32 kr14_fiq;
   uint32 mSPSR_fiq;
   uint32 mR8_fiq;
   uint32 mR9_fiq;
   uint32 mR10_fiq;
   uint32 mR11_fiq;
   uint32 mR12_fiq;
   uint32 mR13_fiq;
   uint32 mR14_fiq;
} BankedRegisterSave;

/**
 * @brief Registers for monitor execution context.
 */
typedef struct {
   uint32 mCPSR;
   uint32 mR1;
   uint32 mR4;
   uint32 mR5;
   uint32 mR6;
   uint32 mR7;
   uint32 mR8;
   uint32 mR9;
   uint32 mR10;
   uint32 mR11;
   uint32 mSP;
   uint32 mLR;   // =mPC
} MonitorRegisterSave;

/**
 * @brief LPV monitor register save/restore.
 */
typedef struct {
   uint32 kR2;   // =kCPSR
   uint32 kR4;
   uint32 kR5;
   uint32 kR6;
   uint32 kR7;
   uint32 kR8;
   uint32 kR9;
   uint32 kR10;
   uint32 kR11;
   uint32 kR13;
   uint32 kR14;  // =kPC

   BankedRegisterSave bankedRegs;

   uint32 kCtrlReg;
   uint32 kTTBR0;
   uint32 kDACR;
   uint32 kASID;
   uint32 kTIDUserRW;
   uint32 kTIDUserRO;
   uint32 kTIDPrivRW;
   uint32 kCSSELR;
   uint32 kPMNCIntEn;
   uint32 kPMNCCCCNT;
   uint32 kPMNCOvFlag;
   uint32 kOpEnabled;
   uint32 mCtrlReg;
   uint32 mTTBR0;
   uint32 mASID;
   uint32 mTIDUserRW;
   uint32 mTIDUserRO;
   uint32 mTIDPrivRW;
   uint32 mCSSELR;

   MonitorRegisterSave monRegs;
} RegisterSaveLPV;

/**
 * @brief VE monitor register save/restore.
 */
typedef struct {
   uint32 mHTTBR;

   uint32 kR3;
   uint32 kR4;
   uint32 kR5;
   uint32 kR6;
   uint32 kR7;
   uint32 kR8;
   uint32 kR9;
   uint32 kR10;
   uint32 kR11;
   uint32 kR12;
   uint32 kCPSR;
   uint32 kRet;

   BankedRegisterSave bankedRegs;

   uint32 kCSSELR;
   uint32 kCtrlReg;
   uint32 kTTBR0[2];
   uint32 kTTBR1[2];
   uint32 kTTBRC;
   uint32 kDACR;
   uint32 kDFSR;
   uint32 kIFSR;
   uint32 kAuxDFSR;
   uint32 kAuxIFSR;
   uint32 kDFAR;
   uint32 kIFAR;
   uint32 kPAR[2];
   uint32 kPRRR;
   uint32 kNMRR;
   uint32 kASID;
   uint32 kTIDUserRW;
   uint32 kTIDUserRO;
   uint32 kTIDPrivRW;
   uint32 mCSSELR;
   uint32 mCtrlReg;
   uint32 mTTBR0[2];
   uint32 mTTBR1[2];
   uint32 mTTBRC;
   uint32 mDACR;
   uint32 mDFSR;
   uint32 mIFSR;
   uint32 mAuxDFSR;
   uint32 mAuxIFSR;
   uint32 mDFAR;
   uint32 mIFAR;
   uint32 mPAR[2];
   uint32 mPRRR;
   uint32 mNMRR;
   uint32 mASID;
   uint32 mTIDUserRW;
   uint32 mTIDUserRO;
   uint32 mTIDPrivRW;

   uint32 mHCR;
   uint32 mHDCR;
   uint32 mHCPTR;
   uint32 mHSTR;
   uint32 mVTTBR[2];
   uint32 mVTCR;

   MonitorRegisterSave monRegs;
} RegisterSaveVE;

typedef union {
   unsigned char reserve_space[MAX_REGISTER_SAVE_SIZE];
   RegisterSaveLPV lpv;
   RegisterSaveVE ve;
} RegisterSave;

MY_ASSERTS(REGSAVE,
   ASSERT_ON_COMPILE(sizeof(RegisterSave) == MAX_REGISTER_SAVE_SIZE);
)

/**
 *  @brief Area for saving the monitor/kernel VFP state.
 */
typedef struct VFPSave {
   uint32 fpexc, fpscr, fpinst, fpinst2, cpacr, fpexc_;

   uint64 fpregs[32];  // Hardware requires that this must be 8-byte (64-bit)
                       // aligned, however the SaveVFP/LoadVFP code does not
                       // align its pointer before accessing so we don't have
                       // an 'aligned(8)' attribute here.  However, the
                       // alignment is checked via asserts in SetupMonitor()
                       // where it initializes the contents.

                       // So if the preceding uint32's are changed and fpregs[]
                       // is no longer 8-byte aligned, the assert will fire.
                       // Then the uint32's will have to be fixed AND THE CODE
                       // in SaveVFP/LoadVFP will have to be CHANGED EQUALLY to
                       // compensate, as simply padding the uint32's (or
                       // sticking an aligned(8) attribute here) will leave the
                       // this structure mismatched with the code.

} VFPSave __attribute__((aligned(8)));
                       // Keep the aligned(8) attribute here though so the
                       // VFPSave structures begin on an 8-byte boundary.

typedef struct WorldSwitchPage WorldSwitchPage;
typedef void (SwitchToMonitor)(RegisterSave *regSave);
typedef void (SwitchToUser)(RegisterSave *regSaveEnd);

#include "atomic.h"
#include "monva_common.h"
#include "mksck_shared.h"

struct WorldSwitchPage {
   uint32          mvpkmVersion;   ///< The version number of mvpkm

   HKVA            wspHKVA;        ///< host kernel virtual address of this page
   ARM_L1D         wspKVAL1D;      ///< The l1D entry at the above location

   SwitchToMonitor*switchToMonitor;///< entrypoint of the switching function
   SwitchToUser   *switchToUser;   ///< ditto

   MonVA           monVA;          ///< monitor virtual address space description
   union {
      ARM_L2D monAttribL2D;        ///< {S,TEX,CB} attributes for monitor mappings (LPV)
      ARM_MemAttrNormal memAttr;   ///< Normal memory attributes for monitor (VE)
   };

   MonitorType     monType;        ///< the type of the monitor. Used by mvpkm
   _Bool           allowInts;      ///<  true: monitor runs with ints enabled as much as possible (normal)
                                   ///< false: monitor runs with ints blocked as much as possible (debug)

   struct {
      uint64       switchedAt64;   ///< approx time CP15 TSC was set to...
      uint32       switchedAtTSC;  ///< CP15 TSC value on entry from monitor
      uint32       tscToRate64Mult;  ///< multiplier to convert TSC_READ()s to our RATE64s
      uint32       tscToRate64Shift; ///< shift      to convert TSC_READ()s to our RATE64s
   };

   struct {
      AtmUInt32    hostActions;    ///< actions for monitor on instruction boundary
      Mksck_VmId   guestId;        ///< vmId of the monitor page
   };

   struct {                        ///< Mksck attributes needed by Mksck_WspRelease()
      uint32       critSecCount;   ///< if >0 the monitor is in critical section
                                   ///< and expects to regain control
      _Bool        isPageMapped[MKSCK_MAX_SHARES]; ///< host mksckPages known to the monitor
      _Bool        guestPageMapped;///< the guest Mksck page has been mapped in MVA space
      uint32       isOpened;       ///< bitfield indicating which mkscks
                                   ///< are open on the guest's mksckPage.
      /* Note that isOpened is per VM not per VCPU. Also note
       * that this and other bitfields in the MksckPage structure
       * limit the number of sockets to 32.
       */
   };

#define WSP_PARAMS_SIZE 512
   uint8           params_[WSP_PARAMS_SIZE]; ///< opaque worldswitch call parameters

   RegisterSave    regSave;        ///< Save area for the worldswitch code below
   VFPSave         hostVFP;        ///< Save areas for monitor/kernel VFP state
   VFPSave         monVFP;

__attribute__((aligned(ARM_L2PT_COARSE_SIZE)))
   ARM_L2D wspDoubleMap[ARM_L2PT_COARSE_ENTRIES]; ///< maps worldswitch page at its HKVA
   uint8 secondHalfPadding[ARM_L2PT_COARSE_SIZE];
};

/*
 * These asserts duplicate the assert at the beginning of SetL1L2esc.
 */
MY_ASSERTS(WSP,
   ASSERT_ON_COMPILE(offsetof(struct WorldSwitchPage, wspDoubleMap) %
                     ARM_L2PT_COARSE_SIZE == 0);
)

extern void SaveVFP(VFPSave *);
extern void LoadVFP(VFPSave *);

#define SWITCH_VFP_TO_MONITOR                                \
   do {                                                      \
      SaveVFP(&wsp->hostVFP);                                \
      LoadVFP(&wsp->monVFP);                                 \
   } while(0)

#define SWITCH_VFP_TO_HOST                                   \
   do {                                                      \
      SaveVFP(&wsp->monVFP);                                 \
      LoadVFP(&wsp->hostVFP);                                \
   } while(0)

#endif /// __ASSEMBLER__

#define OFFSETOF_KR3_REGSAVE_VE_WSP 616

#endif /// _WORLDSWITCH_H
