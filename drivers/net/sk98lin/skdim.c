/******************************************************************************
 *
 * Name:	skdim.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.5 $
 * Date:	$Date: 2003/11/28 12:55:40 $
 * Purpose:	All functions to maintain interrupt moderation
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 * This module is intended to manage the dynamic interrupt moderation on both   
 * GEnesis and Yukon adapters.
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef	lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skdim.c,v 1.5 2003/11/28 12:55:40 rroesler Exp $ (C) SysKonnect.";
#endif

#define __SKADDR_C

#ifdef __cplusplus
#error C++ is not yet supported.
extern "C" {
#endif

/*******************************************************************************
**
** Includes
**
*******************************************************************************/

#ifndef __INC_SKDRV1ST_H
#include "h/skdrv1st.h"
#endif

#ifndef __INC_SKDRV2ND_H
#include "h/skdrv2nd.h"
#endif

#include	<linux/kernel_stat.h>

/*******************************************************************************
**
** Defines
**
*******************************************************************************/

/*******************************************************************************
**
** Typedefs
**
*******************************************************************************/

/*******************************************************************************
**
** Local function prototypes 
**
*******************************************************************************/

static unsigned int GetCurrentSystemLoad(SK_AC *pAC);
static SK_U64       GetIsrCalls(SK_AC *pAC);
static SK_BOOL      IsIntModEnabled(SK_AC *pAC);
static void         SetCurrIntCtr(SK_AC *pAC);
static void         EnableIntMod(SK_AC *pAC); 
static void         DisableIntMod(SK_AC *pAC);
static void         ResizeDimTimerDuration(SK_AC *pAC);
static void         DisplaySelectedModerationType(SK_AC *pAC);
static void         DisplaySelectedModerationMask(SK_AC *pAC);
static void         DisplayDescrRatio(SK_AC *pAC);

/*******************************************************************************
**
** Global variables
**
*******************************************************************************/

/*******************************************************************************
**
** Local variables
**
*******************************************************************************/

/*******************************************************************************
**
** Global functions 
**
*******************************************************************************/

/*******************************************************************************
** Function     : SkDimModerate
** Description  : Called in every ISR to check if moderation is to be applied
**                or not for the current number of interrupts
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : void (!)
** Notes        : -
*******************************************************************************/

void 
SkDimModerate(SK_AC *pAC) {
    unsigned int CurrSysLoad    = 0;  /* expressed in percent */
    unsigned int LoadIncrease   = 0;  /* expressed in percent */
    SK_U64       ThresholdInts  = 0;
    SK_U64       IsrCallsPerSec = 0;

#define M_DIMINFO pAC->DynIrqModInfo

    if (!IsIntModEnabled(pAC)) {
        if (M_DIMINFO.IntModTypeSelect == C_INT_MOD_DYNAMIC) {
            CurrSysLoad = GetCurrentSystemLoad(pAC);
            if (CurrSysLoad > 75) {
                    /* 
                    ** More than 75% total system load! Enable the moderation 
                    ** to shield the system against too many interrupts.
                    */
                    EnableIntMod(pAC);
            } else if (CurrSysLoad > M_DIMINFO.PrevSysLoad) {
                LoadIncrease = (CurrSysLoad - M_DIMINFO.PrevSysLoad);
                if (LoadIncrease > ((M_DIMINFO.PrevSysLoad *
                                         C_INT_MOD_ENABLE_PERCENTAGE) / 100)) {
                    if (CurrSysLoad > 10) {
                        /* 
                        ** More than 50% increase with respect to the 
                        ** previous load of the system. Most likely this 
                        ** is due to our ISR-proc...
                        */
                        EnableIntMod(pAC);
                    }
                }
            } else {
                /*
                ** Neither too much system load at all nor too much increase
                ** with respect to the previous system load. Hence, we can leave
                ** the ISR-handling like it is without enabling moderation.
                */
            }
            M_DIMINFO.PrevSysLoad = CurrSysLoad;
        }   
    } else {
        if (M_DIMINFO.IntModTypeSelect == C_INT_MOD_DYNAMIC) {
            ThresholdInts  = ((M_DIMINFO.MaxModIntsPerSec *
                                   C_INT_MOD_DISABLE_PERCENTAGE) / 100);
            IsrCallsPerSec = GetIsrCalls(pAC);
            if (IsrCallsPerSec <= ThresholdInts) {
                /* 
                ** The number of interrupts within the last second is 
                ** lower than the disable_percentage of the desried 
                ** maxrate. Therefore we can disable the moderation.
                */
                DisableIntMod(pAC);
                M_DIMINFO.MaxModIntsPerSec = 
                   (M_DIMINFO.MaxModIntsPerSecUpperLimit +
                    M_DIMINFO.MaxModIntsPerSecLowerLimit) / 2;
            } else {
                /*
                ** The number of interrupts per sec is the same as expected.
                ** Evalulate the descriptor-ratio. If it has changed, a resize 
                ** in the moderation timer might be usefull
                */
                if (M_DIMINFO.AutoSizing) {
                    ResizeDimTimerDuration(pAC);
                }
            }
        }
    }

    /*
    ** Some information to the log...
    */
    if (M_DIMINFO.DisplayStats) {
        DisplaySelectedModerationType(pAC);
        DisplaySelectedModerationMask(pAC);
        DisplayDescrRatio(pAC);
    }

    M_DIMINFO.NbrProcessedDescr = 0; 
    SetCurrIntCtr(pAC);
}

/*******************************************************************************
** Function     : SkDimStartModerationTimer
** Description  : Starts the audit-timer for the dynamic interrupt moderation
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : void (!)
** Notes        : -
*******************************************************************************/

void 
SkDimStartModerationTimer(SK_AC *pAC) {
    SK_EVPARA    EventParam;   /* Event struct for timer event */
 
    SK_MEMSET((char *) &EventParam, 0, sizeof(EventParam));
    EventParam.Para32[0] = SK_DRV_MODERATION_TIMER;
    SkTimerStart(pAC, pAC->IoBase, &pAC->DynIrqModInfo.ModTimer,
                 SK_DRV_MODERATION_TIMER_LENGTH,
                 SKGE_DRV, SK_DRV_TIMER, EventParam);
}

/*******************************************************************************
** Function     : SkDimEnableModerationIfNeeded
** Description  : Either enables or disables moderation
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : void (!)
** Notes        : This function is called when a particular adapter is opened
**                There is no Disable function, because when all interrupts 
**                might be disable, the moderation timer has no meaning at all
******************************************************************************/

void
SkDimEnableModerationIfNeeded(SK_AC *pAC) {

    if (M_DIMINFO.IntModTypeSelect == C_INT_MOD_STATIC) {
        EnableIntMod(pAC);   /* notification print in this function */
    } else if (M_DIMINFO.IntModTypeSelect == C_INT_MOD_DYNAMIC) {
        SkDimStartModerationTimer(pAC);
        if (M_DIMINFO.DisplayStats) {
            printk("Dynamic moderation has been enabled\n");
        }
    } else {
        if (M_DIMINFO.DisplayStats) {
            printk("No moderation has been enabled\n");
        }
    }
}

/*******************************************************************************
** Function     : SkDimDisplayModerationSettings
** Description  : Displays the current settings regaring interrupt moderation
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : void (!)
** Notes        : -
*******************************************************************************/

void 
SkDimDisplayModerationSettings(SK_AC *pAC) {
    DisplaySelectedModerationType(pAC);
    DisplaySelectedModerationMask(pAC);
}

/*******************************************************************************
**
** Local functions 
**
*******************************************************************************/

/*******************************************************************************
** Function     : GetCurrentSystemLoad
** Description  : Retrieves the current system load of the system. This load
**                is evaluated for all processors within the system.
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : unsigned int: load expressed in percentage
** Notes        : The possible range being returned is from 0 up to 100.
**                Whereas 0 means 'no load at all' and 100 'system fully loaded'
**                It is impossible to determine what actually causes the system
**                to be in 100%, but maybe that is due to too much interrupts.
*******************************************************************************/

static unsigned int
GetCurrentSystemLoad(SK_AC *pAC) {
	unsigned long jif         = jiffies;
	unsigned int  UserTime    = 0;
	unsigned int  SystemTime  = 0;
	unsigned int  NiceTime    = 0;
	unsigned int  IdleTime    = 0;
	unsigned int  TotalTime   = 0;
	unsigned int  UsedTime    = 0;
	unsigned int  SystemLoad  = 0;

	/* unsigned int  NbrCpu      = 0; */

	/*
	** The following lines have been commented out, because
	** from kernel 2.5.44 onwards, the kernel-owned structure
	**
	**      struct kernel_stat kstat
	**
	** is not marked as an exported symbol in the file
	**
	**      kernel/ksyms.c 
	**
	** As a consequence, using this driver as KLM is not possible
	** and any access of the structure kernel_stat via the 
	** dedicated macros kstat_cpu(i).cpustat.xxx is to be avoided.
	**
	** The kstat-information might be added again in future 
	** versions of the 2.5.xx kernel, but for the time being, 
	** number of interrupts will serve as indication how much 
	** load we currently have... 
	**
	** for (NbrCpu = 0; NbrCpu < num_online_cpus(); NbrCpu++) {
	**	UserTime   = UserTime   + kstat_cpu(NbrCpu).cpustat.user;
	**	NiceTime   = NiceTime   + kstat_cpu(NbrCpu).cpustat.nice;
	**	SystemTime = SystemTime + kstat_cpu(NbrCpu).cpustat.system;
	** }
	*/
	SK_U64 ThresholdInts  = 0;
	SK_U64 IsrCallsPerSec = 0;

	ThresholdInts  = ((M_DIMINFO.MaxModIntsPerSec *
			   C_INT_MOD_ENABLE_PERCENTAGE) + 100);
	IsrCallsPerSec = GetIsrCalls(pAC);
	if (IsrCallsPerSec >= ThresholdInts) {
	    /*
	    ** We do not know how much the real CPU-load is!
	    ** Return 80% as a default in order to activate DIM
	    */
	    SystemLoad = 80;
	    return (SystemLoad);  
	} 

	UsedTime  = UserTime + NiceTime + SystemTime;

	IdleTime  = jif * num_online_cpus() - UsedTime;
	TotalTime = UsedTime + IdleTime;

	SystemLoad = ( 100 * (UsedTime  - M_DIMINFO.PrevUsedTime) ) /
						(TotalTime - M_DIMINFO.PrevTotalTime);

	if (M_DIMINFO.DisplayStats) {
		printk("Current system load is: %u\n", SystemLoad);
	}

	M_DIMINFO.PrevTotalTime = TotalTime;
	M_DIMINFO.PrevUsedTime  = UsedTime;

	return (SystemLoad);
}

/*******************************************************************************
** Function     : GetIsrCalls
** Description  : Depending on the selected moderation mask, this function will
**                return the number of interrupts handled in the previous time-
**                frame. This evaluated number is based on the current number 
**                of interrupts stored in PNMI-context and the previous stored 
**                interrupts.
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : int:   the number of interrupts being executed in the last
**                       timeframe
** Notes        : It makes only sense to call this function, when dynamic 
**                interrupt moderation is applied
*******************************************************************************/

static SK_U64
GetIsrCalls(SK_AC *pAC) {
    SK_U64   RxPort0IntDiff = 0;
    SK_U64   RxPort1IntDiff = 0;
    SK_U64   TxPort0IntDiff = 0;
    SK_U64   TxPort1IntDiff = 0;

    if (pAC->DynIrqModInfo.MaskIrqModeration == IRQ_MASK_TX_ONLY) {
        if (pAC->GIni.GIMacsFound == 2) {
            TxPort1IntDiff = pAC->Pnmi.Port[1].TxIntrCts - 
                             pAC->DynIrqModInfo.PrevPort1TxIntrCts;
        }
        TxPort0IntDiff = pAC->Pnmi.Port[0].TxIntrCts - 
                         pAC->DynIrqModInfo.PrevPort0TxIntrCts;
    } else if (pAC->DynIrqModInfo.MaskIrqModeration == IRQ_MASK_RX_ONLY) {
        if (pAC->GIni.GIMacsFound == 2) {
            RxPort1IntDiff = pAC->Pnmi.Port[1].RxIntrCts - 
                             pAC->DynIrqModInfo.PrevPort1RxIntrCts;
        }
        RxPort0IntDiff = pAC->Pnmi.Port[0].RxIntrCts - 
                         pAC->DynIrqModInfo.PrevPort0RxIntrCts;
    } else {
        if (pAC->GIni.GIMacsFound == 2) {
            RxPort1IntDiff = pAC->Pnmi.Port[1].RxIntrCts - 
                             pAC->DynIrqModInfo.PrevPort1RxIntrCts;
            TxPort1IntDiff = pAC->Pnmi.Port[1].TxIntrCts - 
                             pAC->DynIrqModInfo.PrevPort1TxIntrCts;
        } 
        RxPort0IntDiff = pAC->Pnmi.Port[0].RxIntrCts - 
                         pAC->DynIrqModInfo.PrevPort0RxIntrCts;
        TxPort0IntDiff = pAC->Pnmi.Port[0].TxIntrCts - 
                         pAC->DynIrqModInfo.PrevPort0TxIntrCts;
    }

    return (RxPort0IntDiff + RxPort1IntDiff + TxPort0IntDiff + TxPort1IntDiff);
}

/*******************************************************************************
** Function     : GetRxCalls
** Description  : This function will return the number of times a receive inter-
**                rupt was processed. This is needed to evaluate any resizing 
**                factor.
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : SK_U64: the number of RX-ints being processed
** Notes        : It makes only sense to call this function, when dynamic 
**                interrupt moderation is applied
*******************************************************************************/

static SK_U64
GetRxCalls(SK_AC *pAC) {
    SK_U64   RxPort0IntDiff = 0;
    SK_U64   RxPort1IntDiff = 0;

    if (pAC->GIni.GIMacsFound == 2) {
        RxPort1IntDiff = pAC->Pnmi.Port[1].RxIntrCts - 
                         pAC->DynIrqModInfo.PrevPort1RxIntrCts;
    }
    RxPort0IntDiff = pAC->Pnmi.Port[0].RxIntrCts - 
                     pAC->DynIrqModInfo.PrevPort0RxIntrCts;

    return (RxPort0IntDiff + RxPort1IntDiff);
}

/*******************************************************************************
** Function     : SetCurrIntCtr
** Description  : Will store the current number orf occured interrupts in the 
**                adapter context. This is needed to evaluated the number of 
**                interrupts within a current timeframe.
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : void (!)
** Notes        : -
*******************************************************************************/

static void
SetCurrIntCtr(SK_AC *pAC) {
    if (pAC->GIni.GIMacsFound == 2) {
        pAC->DynIrqModInfo.PrevPort1RxIntrCts = pAC->Pnmi.Port[1].RxIntrCts;
        pAC->DynIrqModInfo.PrevPort1TxIntrCts = pAC->Pnmi.Port[1].TxIntrCts;
    } 
    pAC->DynIrqModInfo.PrevPort0RxIntrCts = pAC->Pnmi.Port[0].RxIntrCts;
    pAC->DynIrqModInfo.PrevPort0TxIntrCts = pAC->Pnmi.Port[0].TxIntrCts;
}

/*******************************************************************************
** Function     : IsIntModEnabled()
** Description  : Retrieves the current value of the interrupts moderation
**                command register. Its content determines whether any 
**                moderation is running or not.
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : SK_TRUE  : if mod timer running
**                SK_FALSE : if no moderation is being performed
** Notes        : -
*******************************************************************************/

static SK_BOOL
IsIntModEnabled(SK_AC *pAC) {
    unsigned long CtrCmd;

    SK_IN32(pAC->IoBase, B2_IRQM_CTRL, &CtrCmd);
    if ((CtrCmd & TIM_START) == TIM_START) {
       return SK_TRUE;
    } else {
       return SK_FALSE;
    }
}

/*******************************************************************************
** Function     : EnableIntMod()
** Description  : Enables the interrupt moderation using the values stored in
**                in the pAC->DynIntMod data structure
** Programmer   : Ralph Roesler
** Last Modified: 22-mar-03
** Returns      : -
** Notes        : -
*******************************************************************************/

static void
EnableIntMod(SK_AC *pAC) {
    unsigned long ModBase;

    if (pAC->GIni.GIChipId == CHIP_ID_GENESIS) {
       ModBase = C_CLK_FREQ_GENESIS / pAC->DynIrqModInfo.MaxModIntsPerSec;
    } else {
       ModBase = C_CLK_FREQ_YUKON / pAC->DynIrqModInfo.MaxModIntsPerSec;
    }

    SK_OUT32(pAC->IoBase, B2_IRQM_INI,  ModBase);
    SK_OUT32(pAC->IoBase, B2_IRQM_MSK,  pAC->DynIrqModInfo.MaskIrqModeration);
    SK_OUT32(pAC->IoBase, B2_IRQM_CTRL, TIM_START);
    if (M_DIMINFO.DisplayStats) {
        printk("Enabled interrupt moderation (%i ints/sec)\n",
               M_DIMINFO.MaxModIntsPerSec);
    }
}

/*******************************************************************************
** Function     : DisableIntMod()
** Description  : Disbles the interrupt moderation independent of what inter-
**                rupts are running or not
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : -
** Notes        : -
*******************************************************************************/

static void 
DisableIntMod(SK_AC *pAC) {

    SK_OUT32(pAC->IoBase, B2_IRQM_CTRL, TIM_STOP);
    if (M_DIMINFO.DisplayStats) {
        printk("Disabled interrupt moderation\n");
    }
} 

/*******************************************************************************
** Function     : ResizeDimTimerDuration();
** Description  : Checks the current used descriptor ratio and resizes the 
**                duration timer (longer/smaller) if possible. 
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : -
** Notes        : There are both maximum and minimum timer duration value. 
**                This function assumes that interrupt moderation is already
**                enabled!
*******************************************************************************/

static void 
ResizeDimTimerDuration(SK_AC *pAC) {
    SK_BOOL IncreaseTimerDuration;
    int     TotalMaxNbrDescr;
    int     UsedDescrRatio;
    int     RatioDiffAbs;
    int     RatioDiffRel;
    int     NewMaxModIntsPerSec;
    int     ModAdjValue;
    long    ModBase;

    /*
    ** Check first if we are allowed to perform any modification
    */
    if (IsIntModEnabled(pAC)) { 
        if (M_DIMINFO.IntModTypeSelect != C_INT_MOD_DYNAMIC) {
            return; 
        } else {
            if (M_DIMINFO.ModJustEnabled) {
                M_DIMINFO.ModJustEnabled = SK_FALSE;
                return;
            }
        }
    }

    /*
    ** If we got until here, we have to evaluate the amount of the
    ** descriptor ratio change...
    */
    TotalMaxNbrDescr = pAC->RxDescrPerRing * GetRxCalls(pAC);
    UsedDescrRatio   = (M_DIMINFO.NbrProcessedDescr * 100) / TotalMaxNbrDescr;

    if (UsedDescrRatio > M_DIMINFO.PrevUsedDescrRatio) {
        RatioDiffAbs = (UsedDescrRatio - M_DIMINFO.PrevUsedDescrRatio);
        RatioDiffRel = (RatioDiffAbs * 100) / UsedDescrRatio;
        M_DIMINFO.PrevUsedDescrRatio = UsedDescrRatio;
        IncreaseTimerDuration = SK_FALSE;  /* in other words: DECREASE */
    } else if (UsedDescrRatio < M_DIMINFO.PrevUsedDescrRatio) {
        RatioDiffAbs = (M_DIMINFO.PrevUsedDescrRatio - UsedDescrRatio);
        RatioDiffRel = (RatioDiffAbs * 100) / M_DIMINFO.PrevUsedDescrRatio;
        M_DIMINFO.PrevUsedDescrRatio = UsedDescrRatio;
        IncreaseTimerDuration = SK_TRUE;   /* in other words: INCREASE */
    } else {
        RatioDiffAbs = (M_DIMINFO.PrevUsedDescrRatio - UsedDescrRatio);
        RatioDiffRel = (RatioDiffAbs * 100) / M_DIMINFO.PrevUsedDescrRatio;
        M_DIMINFO.PrevUsedDescrRatio = UsedDescrRatio;
        IncreaseTimerDuration = SK_TRUE;   /* in other words: INCREASE */
    }

    /*
    ** Now we can determine the change in percent
    */
    if ((RatioDiffRel >= 0) && (RatioDiffRel <= 5) ) {
       ModAdjValue = 1;  /*  1% change - maybe some other value in future */
    } else if ((RatioDiffRel > 5) && (RatioDiffRel <= 10) ) {
       ModAdjValue = 1;  /*  1% change - maybe some other value in future */
    } else if ((RatioDiffRel > 10) && (RatioDiffRel <= 15) ) {
       ModAdjValue = 1;  /*  1% change - maybe some other value in future */
    } else {
       ModAdjValue = 1;  /*  1% change - maybe some other value in future */
    }

    if (IncreaseTimerDuration) {
       NewMaxModIntsPerSec =  M_DIMINFO.MaxModIntsPerSec +
                             (M_DIMINFO.MaxModIntsPerSec * ModAdjValue) / 100;
    } else {
       NewMaxModIntsPerSec =  M_DIMINFO.MaxModIntsPerSec -
                             (M_DIMINFO.MaxModIntsPerSec * ModAdjValue) / 100;
    }

    /* 
    ** Check if we exceed boundaries...
    */
    if ( (NewMaxModIntsPerSec > M_DIMINFO.MaxModIntsPerSecUpperLimit) ||
         (NewMaxModIntsPerSec < M_DIMINFO.MaxModIntsPerSecLowerLimit)) {
        if (M_DIMINFO.DisplayStats) {
            printk("Cannot change ModTim from %i to %i ints/sec\n",
                   M_DIMINFO.MaxModIntsPerSec, NewMaxModIntsPerSec);
        }
        return;
    } else {
        if (M_DIMINFO.DisplayStats) {
            printk("Resized ModTim from %i to %i ints/sec\n",
                   M_DIMINFO.MaxModIntsPerSec, NewMaxModIntsPerSec);
        }
    }

    M_DIMINFO.MaxModIntsPerSec = NewMaxModIntsPerSec;

    if (pAC->GIni.GIChipId == CHIP_ID_GENESIS) {
        ModBase = C_CLK_FREQ_GENESIS / pAC->DynIrqModInfo.MaxModIntsPerSec;
    } else {
        ModBase = C_CLK_FREQ_YUKON / pAC->DynIrqModInfo.MaxModIntsPerSec;
    }

    /* 
    ** We do not need to touch any other registers
    */
    SK_OUT32(pAC->IoBase, B2_IRQM_INI, ModBase);
} 

/*******************************************************************************
** Function     : DisplaySelectedModerationType()
** Description  : Displays what type of moderation we have
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : void!
** Notes        : -
*******************************************************************************/

static void
DisplaySelectedModerationType(SK_AC *pAC) {

    if (pAC->DynIrqModInfo.DisplayStats) {
        if (pAC->DynIrqModInfo.IntModTypeSelect == C_INT_MOD_STATIC) {
             printk("Static int moderation runs with %i INTS/sec\n",
                    pAC->DynIrqModInfo.MaxModIntsPerSec);
        } else if (pAC->DynIrqModInfo.IntModTypeSelect == C_INT_MOD_DYNAMIC) {
             if (IsIntModEnabled(pAC)) {
                printk("Dynamic int moderation runs with %i INTS/sec\n",
                       pAC->DynIrqModInfo.MaxModIntsPerSec);
             } else {
                printk("Dynamic int moderation currently not applied\n");
             }
        } else {
             printk("No interrupt moderation selected!\n");
        }
    }
}

/*******************************************************************************
** Function     : DisplaySelectedModerationMask()
** Description  : Displays what interrupts are moderated
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : void!
** Notes        : -
*******************************************************************************/

static void
DisplaySelectedModerationMask(SK_AC *pAC) {

    if (pAC->DynIrqModInfo.DisplayStats) {
        if (pAC->DynIrqModInfo.IntModTypeSelect != C_INT_MOD_NONE) {
            switch (pAC->DynIrqModInfo.MaskIrqModeration) {
                case IRQ_MASK_TX_ONLY: 
                   printk("Only Tx-interrupts are moderated\n");
                   break;
                case IRQ_MASK_RX_ONLY: 
                   printk("Only Rx-interrupts are moderated\n");
                   break;
                case IRQ_MASK_SP_ONLY: 
                   printk("Only special-interrupts are moderated\n");
                   break;
                case IRQ_MASK_TX_RX: 
                   printk("Tx- and Rx-interrupts are moderated\n");
                   break;
                case IRQ_MASK_SP_RX: 
                   printk("Special- and Rx-interrupts are moderated\n");
                   break;
                case IRQ_MASK_SP_TX: 
                   printk("Special- and Tx-interrupts are moderated\n");
                   break;
                case IRQ_MASK_RX_TX_SP:
                   printk("All Rx-, Tx and special-interrupts are moderated\n");
                   break;
                default:
                   printk("Don't know what is moderated\n");
                   break;
            }
        } else {
            printk("No specific interrupts masked for moderation\n");
        }
    } 
}

/*******************************************************************************
** Function     : DisplayDescrRatio
** Description  : Like the name states...
** Programmer   : Ralph Roesler
** Last Modified: 23-mar-03
** Returns      : void!
** Notes        : -
*******************************************************************************/

static void
DisplayDescrRatio(SK_AC *pAC) {
    int TotalMaxNbrDescr = 0;

    if (pAC->DynIrqModInfo.DisplayStats) {
        TotalMaxNbrDescr = pAC->RxDescrPerRing * GetRxCalls(pAC);
        printk("Ratio descriptors: %i/%i\n",
               M_DIMINFO.NbrProcessedDescr, TotalMaxNbrDescr);
    }
}

/*******************************************************************************
**
** End of file
**
*******************************************************************************/
