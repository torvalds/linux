/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  demoapplication for EPL MN (with SDO over UDP)
                under Linux on X86 with RTL8139 Ethernet controller

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: demo_main.c,v $

                $Author: D.Krueger $

                $Revision: 1.10 $  $Date: 2008/11/19 18:11:43 $

                $State: Exp $

                Build Environment:
                GCC

  -------------------------------------------------------------------------

  Revision History:

  2006/09/01 d.k.:   start of implementation

****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>

#include "Epl.h"
#include "proc_fs.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    // remove ("make invisible") obsolete symbols for kernel versions 2.6
    // and higher
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define EXPORT_NO_SYMBOLS
#else
#error "This driver needs a 2.6.x kernel or higher"
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

// Metainformation
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_AUTHOR
MODULE_AUTHOR("Daniel.Krueger@SYSTEC-electronic.com");
MODULE_DESCRIPTION("EPL MN demo");
#endif

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#endif

#define NODEID      0xF0	//=> MN
#define CYCLE_LEN   5000	// [us]
#define IP_ADDR     0xc0a86401	// 192.168.100.1
#define SUBNET_MASK 0xFFFFFF00	// 255.255.255.0
#define HOSTNAME    "SYS TEC electronic EPL Stack    "
#define IF_ETH      EPL_VETH_NAME

// LIGHT EFFECT
#define DEFAULT_MAX_CYCLE_COUNT 20	// 6 is very fast
#define APP_DEFAULT_MODE        0x01
#define APP_LED_COUNT           5	// number of LEDs in one row
#define APP_LED_MASK            ((1 << APP_LED_COUNT) - 1)
#define APP_DOUBLE_LED_MASK     ((1 << (APP_LED_COUNT * 2)) - 1)
#define APP_MODE_COUNT          5
#define APP_MODE_MASK           ((1 << APP_MODE_COUNT) - 1)

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

CONST BYTE abMacAddr[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

BYTE bVarIn1_l;
BYTE bVarOut1_l;
BYTE bVarOut1Old_l;
BYTE bModeSelect_l;		// state of the pushbuttons to select the mode
BYTE bSpeedSelect_l;		// state of the pushbuttons to increase/decrease the speed
BYTE bSpeedSelectOld_l;		// old state of the pushbuttons
DWORD dwLeds_l;			// current state of all LEDs
BYTE bLedsRow1_l;		// current state of the LEDs in row 1
BYTE bLedsRow2_l;		// current state of the LEDs in row 2
BYTE abSelect_l[3];		// pushbuttons from CNs

DWORD dwMode_l;			// current mode
int iCurCycleCount_l;		// current cycle count
int iMaxCycleCount_l;		// maximum cycle count (i.e. number of cycles until next light movement step)
int iToggle;			// indicates the light movement direction

BYTE abDomain_l[3000];

static wait_queue_head_t WaitQueueShutdown_g;	// wait queue for tEplNmtEventSwitchOff
static atomic_t AtomicShutdown_g = ATOMIC_INIT(FALSE);

static DWORD dw_le_CycleLen_g;

static uint uiNodeId_g = EPL_C_ADR_INVALID;
module_param_named(nodeid, uiNodeId_g, uint, 0);

static uint uiCycleLen_g = CYCLE_LEN;
module_param_named(cyclelen, uiCycleLen_g, uint, 0);

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

// This function is the entry point for your object dictionary. It is defined
// in OBJDICT.C by define EPL_OBD_INIT_RAM_NAME. Use this function name to define
// this function prototype here. If you want to use more than one Epl
// instances then the function name of each object dictionary has to differ.

tEplKernel PUBLIC EplObdInitRam(tEplObdInitParam MEM * pInitParam_p);

tEplKernel PUBLIC AppCbEvent(tEplApiEventType EventType_p,	// IN: event type (enum)
			     tEplApiEventArg * pEventArg_p,	// IN: event argument (union)
			     void GENERIC * pUserArg_p);

tEplKernel PUBLIC AppCbSync(void);

static int __init EplLinInit(void);
static void __exit EplLinExit(void);

//---------------------------------------------------------------------------
//  Kernel Module specific Data Structures
//---------------------------------------------------------------------------

EXPORT_NO_SYMBOLS;

//module_init(EplLinInit);
//module_exit(EplLinExit);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:
//
// Description:
//
//
//
// Parameters:
//
//
// Returns:
//
//
// State:
//
//---------------------------------------------------------------------------
static int __init EplLinInit(void)
{
	tEplKernel EplRet;
	int iRet;
	static tEplApiInitParam EplApiInitParam = { 0 };
	char *sHostname = HOSTNAME;
	char *argv[4], *envp[3];
	char sBuffer[16];
	unsigned int uiVarEntries;
	tEplObdSize ObdSize;

	atomic_set(&AtomicShutdown_g, TRUE);

	// get node ID from insmod command line
	EplApiInitParam.m_uiNodeId = uiNodeId_g;

	if (EplApiInitParam.m_uiNodeId == EPL_C_ADR_INVALID) {	// invalid node ID set
		// set default node ID
		EplApiInitParam.m_uiNodeId = NODEID;
	}

	uiNodeId_g = EplApiInitParam.m_uiNodeId;

	// calculate IP address
	EplApiInitParam.m_dwIpAddress =
	    (0xFFFFFF00 & IP_ADDR) | EplApiInitParam.m_uiNodeId;

	EplApiInitParam.m_fAsyncOnly = FALSE;

	EplApiInitParam.m_uiSizeOfStruct = sizeof(EplApiInitParam);
	EPL_MEMCPY(EplApiInitParam.m_abMacAddress, abMacAddr,
		   sizeof(EplApiInitParam.m_abMacAddress));
//    EplApiInitParam.m_abMacAddress[5] = (BYTE) EplApiInitParam.m_uiNodeId;
	EplApiInitParam.m_dwFeatureFlags = -1;
	EplApiInitParam.m_dwCycleLen = uiCycleLen_g;	// required for error detection
	EplApiInitParam.m_uiIsochrTxMaxPayload = 100;	// const
	EplApiInitParam.m_uiIsochrRxMaxPayload = 100;	// const
	EplApiInitParam.m_dwPresMaxLatency = 50000;	// const; only required for IdentRes
	EplApiInitParam.m_uiPreqActPayloadLimit = 36;	// required for initialisation (+28 bytes)
	EplApiInitParam.m_uiPresActPayloadLimit = 36;	// required for initialisation of Pres frame (+28 bytes)
	EplApiInitParam.m_dwAsndMaxLatency = 150000;	// const; only required for IdentRes
	EplApiInitParam.m_uiMultiplCycleCnt = 0;	// required for error detection
	EplApiInitParam.m_uiAsyncMtu = 1500;	// required to set up max frame size
	EplApiInitParam.m_uiPrescaler = 2;	// required for sync
	EplApiInitParam.m_dwLossOfFrameTolerance = 500000;
	EplApiInitParam.m_dwAsyncSlotTimeout = 3000000;
	EplApiInitParam.m_dwWaitSocPreq = 150000;
	EplApiInitParam.m_dwDeviceType = -1;	// NMT_DeviceType_U32
	EplApiInitParam.m_dwVendorId = -1;	// NMT_IdentityObject_REC.VendorId_U32
	EplApiInitParam.m_dwProductCode = -1;	// NMT_IdentityObject_REC.ProductCode_U32
	EplApiInitParam.m_dwRevisionNumber = -1;	// NMT_IdentityObject_REC.RevisionNo_U32
	EplApiInitParam.m_dwSerialNumber = -1;	// NMT_IdentityObject_REC.SerialNo_U32
	EplApiInitParam.m_dwSubnetMask = SUBNET_MASK;
	EplApiInitParam.m_dwDefaultGateway = 0;
	EPL_MEMCPY(EplApiInitParam.m_sHostname, sHostname,
		   sizeof(EplApiInitParam.m_sHostname));

	// currently unset parameters left at default value 0
	//EplApiInitParam.m_qwVendorSpecificExt1;
	//EplApiInitParam.m_dwVerifyConfigurationDate; // CFM_VerifyConfiguration_REC.ConfDate_U32
	//EplApiInitParam.m_dwVerifyConfigurationTime; // CFM_VerifyConfiguration_REC.ConfTime_U32
	//EplApiInitParam.m_dwApplicationSwDate;       // PDL_LocVerApplSw_REC.ApplSwDate_U32 on programmable device or date portion of NMT_ManufactSwVers_VS on non-programmable device
	//EplApiInitParam.m_dwApplicationSwTime;       // PDL_LocVerApplSw_REC.ApplSwTime_U32 on programmable device or time portion of NMT_ManufactSwVers_VS on non-programmable device
	//EplApiInitParam.m_abVendorSpecificExt2[48];

	// set callback functions
	EplApiInitParam.m_pfnCbEvent = AppCbEvent;
	EplApiInitParam.m_pfnCbSync = AppCbSync;

	printk
	    ("\n\n Hello, I'm a simple POWERLINK node running as %s!\n  (build: %s / %s)\n\n",
	     (uiNodeId_g ==
	      EPL_C_ADR_MN_DEF_NODE_ID ? "Managing Node" : "Controlled Node"),
	     __DATE__, __TIME__);

	// initialize the Linux a wait queue for shutdown of this module
	init_waitqueue_head(&WaitQueueShutdown_g);

	// initialize the procfs device
	EplRet = EplLinProcInit();
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}
	// initialize POWERLINK stack
	EplRet = EplApiInitialize(&EplApiInitParam);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}
	// link process variables used by CN to object dictionary
	ObdSize = sizeof(bVarIn1_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x6000, &bVarIn1_l, &uiVarEntries, &ObdSize, 0x01);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

	ObdSize = sizeof(bVarOut1_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x6200, &bVarOut1_l, &uiVarEntries, &ObdSize,
			     0x01);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}
	// link process variables used by MN to object dictionary
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	ObdSize = sizeof(bLedsRow1_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x2000, &bLedsRow1_l, &uiVarEntries, &ObdSize,
			     0x01);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

	ObdSize = sizeof(bLedsRow2_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x2000, &bLedsRow2_l, &uiVarEntries, &ObdSize,
			     0x02);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

	ObdSize = sizeof(bSpeedSelect_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x2000, &bSpeedSelect_l, &uiVarEntries, &ObdSize,
			     0x03);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

	ObdSize = sizeof(bSpeedSelectOld_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x2000, &bSpeedSelectOld_l, &uiVarEntries,
			     &ObdSize, 0x04);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}

	ObdSize = sizeof(abSelect_l[0]);
	uiVarEntries = sizeof(abSelect_l);
	EplRet =
	    EplApiLinkObject(0x2200, &abSelect_l[0], &uiVarEntries, &ObdSize,
			     0x01);
	if (EplRet != kEplSuccessful) {
		goto Exit;
	}
#endif

	// link a DOMAIN to object 0x6100, but do not exit, if it is missing
	ObdSize = sizeof(abDomain_l);
	uiVarEntries = 1;
	EplRet =
	    EplApiLinkObject(0x6100, &abDomain_l, &uiVarEntries, &ObdSize,
			     0x00);
	if (EplRet != kEplSuccessful) {
		printk("EplApiLinkObject(0x6100): returns 0x%X\n", EplRet);
	}
	// reset old process variables
	bVarOut1Old_l = 0;
	bSpeedSelectOld_l = 0;
	dwMode_l = APP_DEFAULT_MODE;
	iMaxCycleCount_l = DEFAULT_MAX_CYCLE_COUNT;

	// configure IP address of virtual network interface
	// for TCP/IP communication over the POWERLINK network
	sprintf(sBuffer, "%lu.%lu.%lu.%lu",
		(EplApiInitParam.m_dwIpAddress >> 24),
		((EplApiInitParam.m_dwIpAddress >> 16) & 0xFF),
		((EplApiInitParam.m_dwIpAddress >> 8) & 0xFF),
		(EplApiInitParam.m_dwIpAddress & 0xFF));
	/* set up a minimal environment */
	iRet = 0;
	envp[iRet++] = "HOME=/";
	envp[iRet++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[iRet] = NULL;

	/* set up the argument list */
	iRet = 0;
	argv[iRet++] = "/sbin/ifconfig";
	argv[iRet++] = IF_ETH;
	argv[iRet++] = sBuffer;
	argv[iRet] = NULL;

	/* call ifconfig to configure the virtual network interface */
	iRet = call_usermodehelper(argv[0], argv, envp, 1);
	printk("ifconfig %s %s returned %d\n", argv[1], argv[2], iRet);

	// start the NMT state machine
	EplRet = EplApiExecNmtCommand(kEplNmtEventSwReset);
	atomic_set(&AtomicShutdown_g, FALSE);

      Exit:
	printk("EplLinInit(): returns 0x%X\n", EplRet);
	return EplRet;
}

static void __exit EplLinExit(void)
{
	tEplKernel EplRet;

	// halt the NMT state machine
	// so the processing of POWERLINK frames stops
	EplRet = EplApiExecNmtCommand(kEplNmtEventSwitchOff);

	// wait until NMT state machine is shut down
	wait_event_interruptible(WaitQueueShutdown_g,
				 (atomic_read(&AtomicShutdown_g) == TRUE));
/*    if ((iErr != 0) || (atomic_read(&AtomicShutdown_g) == EVENT_STATE_IOCTL))
    {   // waiting was interrupted by signal or application called wrong function
        EplRet = kEplShutdown;
    }*/
	// delete instance for all modules
	EplRet = EplApiShutdown();
	printk("EplApiShutdown():  0x%X\n", EplRet);

	// deinitialize proc fs
	EplRet = EplLinProcFree();
	printk("EplLinProcFree():        0x%X\n", EplRet);

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    AppCbEvent
//
// Description: event callback function called by EPL API layer within
//              user part (low priority).
//
// Parameters:  EventType_p     = event type
//              pEventArg_p     = pointer to union, which describes
//                                the event in detail
//              pUserArg_p      = user specific argument
//
// Returns:     tEplKernel      = error code,
//                                kEplSuccessful = no error
//                                kEplReject = reject further processing
//                                otherwise = post error event to API layer
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC AppCbEvent(tEplApiEventType EventType_p,	// IN: event type (enum)
			     tEplApiEventArg * pEventArg_p,	// IN: event argument (union)
			     void GENERIC * pUserArg_p)
{
	tEplKernel EplRet = kEplSuccessful;

	// check if NMT_GS_OFF is reached
	switch (EventType_p) {
	case kEplApiEventNmtStateChange:
		{
			switch (pEventArg_p->m_NmtStateChange.m_NewNmtState) {
			case kEplNmtGsOff:
				{	// NMT state machine was shut down,
					// because of user signal (CTRL-C) or critical EPL stack error
					// -> also shut down EplApiProcess() and main()
					EplRet = kEplShutdown;

					printk
					    ("AppCbEvent(kEplNmtGsOff) originating event = 0x%X\n",
					     pEventArg_p->m_NmtStateChange.
					     m_NmtEvent);

					// wake up EplLinExit()
					atomic_set(&AtomicShutdown_g, TRUE);
					wake_up_interruptible
					    (&WaitQueueShutdown_g);
					break;
				}

			case kEplNmtGsResetCommunication:
				{
					DWORD dwBuffer;

					// configure OD for MN in state ResetComm after reseting the OD
					// TODO: setup your own network configuration here
					dwBuffer = (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS);	// 0x00000003L
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x01,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x02,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x03,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x04,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x05,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x06,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x07,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x08,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x20,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0xFE,
								   &dwBuffer,
								   4);
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0x6E,
								   &dwBuffer,
								   4);

//                    dwBuffer |= EPL_NODEASSIGN_MANDATORY_CN;    // 0x0000000BL
//                    EplRet = EplApiWriteLocalObject(0x1F81, 0x6E, &dwBuffer, 4);
					dwBuffer = (EPL_NODEASSIGN_MN_PRES | EPL_NODEASSIGN_NODE_EXISTS);	// 0x00010001L
					EplRet =
					    EplApiWriteLocalObject(0x1F81, 0xF0,
								   &dwBuffer,
								   4);

					// continue
				}

			case kEplNmtGsResetConfiguration:
				{
					unsigned int uiSize;

					// fetch object 0x1006 NMT_CycleLen_U32 from local OD (in little endian byte order)
					// for configuration of remote CN
					uiSize = 4;
					EplRet =
					    EplApiReadObject(NULL, 0, 0x1006,
							     0x00,
							     &dw_le_CycleLen_g,
							     &uiSize,
							     kEplSdoTypeAsnd,
							     NULL);
					if (EplRet != kEplSuccessful) {	// local OD access failed
						break;
					}
					// continue
				}

			case kEplNmtMsPreOperational1:
				{
					printk
					    ("AppCbEvent(0x%X) originating event = 0x%X\n",
					     pEventArg_p->m_NmtStateChange.
					     m_NewNmtState,
					     pEventArg_p->m_NmtStateChange.
					     m_NmtEvent);

					// continue
				}

			case kEplNmtGsInitialising:
			case kEplNmtGsResetApplication:
			case kEplNmtMsNotActive:
			case kEplNmtCsNotActive:
			case kEplNmtCsPreOperational1:
				{
					break;
				}

			case kEplNmtCsOperational:
			case kEplNmtMsOperational:
				{
					break;
				}

			default:
				{
					break;
				}
			}

/*
            switch (pEventArg_p->m_NmtStateChange.m_NmtEvent)
            {
                case kEplNmtEventSwReset:
                case kEplNmtEventResetNode:
                case kEplNmtEventResetCom:
                case kEplNmtEventResetConfig:
                case kEplNmtEventInternComError:
                case kEplNmtEventNmtCycleError:
                {
                    printk("AppCbEvent(0x%X) originating event = 0x%X\n",
                           pEventArg_p->m_NmtStateChange.m_NewNmtState,
                           pEventArg_p->m_NmtStateChange.m_NmtEvent);
                    break;
                }

                default:
                {
                    break;
                }
            }
*/
			break;
		}

	case kEplApiEventCriticalError:
	case kEplApiEventWarning:
		{		// error or warning occured within the stack or the application
			// on error the API layer stops the NMT state machine

			printk
			    ("AppCbEvent(Err/Warn): Source=%02X EplError=0x%03X",
			     pEventArg_p->m_InternalError.m_EventSource,
			     pEventArg_p->m_InternalError.m_EplError);
			// check additional argument
			switch (pEventArg_p->m_InternalError.m_EventSource) {
			case kEplEventSourceEventk:
			case kEplEventSourceEventu:
				{	// error occured within event processing
					// either in kernel or in user part
					printk(" OrgSource=%02X\n",
					       pEventArg_p->m_InternalError.
					       m_Arg.m_EventSource);
					break;
				}

			case kEplEventSourceDllk:
				{	// error occured within the data link layer (e.g. interrupt processing)
					// the DWORD argument contains the DLL state and the NMT event
					printk(" val=%lX\n",
					       pEventArg_p->m_InternalError.
					       m_Arg.m_dwArg);
					break;
				}

			default:
				{
					printk("\n");
					break;
				}
			}
			break;
		}

	case kEplApiEventNode:
		{
//            printk("AppCbEvent(Node): Source=%02X EplError=0x%03X", pEventArg_p->m_InternalError.m_EventSource, pEventArg_p->m_InternalError.m_EplError);
			// check additional argument
			switch (pEventArg_p->m_Node.m_NodeEvent) {
			case kEplNmtNodeEventCheckConf:
				{
					tEplSdoComConHdl SdoComConHdl;
					// update object 0x1006 on CN
					EplRet =
					    EplApiWriteObject(&SdoComConHdl,
							      pEventArg_p->
							      m_Node.m_uiNodeId,
							      0x1006, 0x00,
							      &dw_le_CycleLen_g,
							      4,
							      kEplSdoTypeAsnd,
							      NULL);
					if (EplRet == kEplApiTaskDeferred) {	// SDO transfer started
						EplRet = kEplReject;
					} else if (EplRet == kEplSuccessful) {	// local OD access (should not occur)
						printk
						    ("AppCbEvent(Node) write to local OD\n");
					} else {	// error occured
						TGT_DBG_SIGNAL_TRACE_POINT(1);

						EplRet =
						    EplApiFreeSdoChannel
						    (SdoComConHdl);
						SdoComConHdl = 0;

						EplRet =
						    EplApiWriteObject
						    (&SdoComConHdl,
						     pEventArg_p->m_Node.
						     m_uiNodeId, 0x1006, 0x00,
						     &dw_le_CycleLen_g, 4,
						     kEplSdoTypeAsnd, NULL);
						if (EplRet == kEplApiTaskDeferred) {	// SDO transfer started
							EplRet = kEplReject;
						} else {
							printk
							    ("AppCbEvent(Node): EplApiWriteObject() returned 0x%02X\n",
							     EplRet);
						}
					}

					break;
				}

			default:
				{
					break;
				}
			}
			break;
		}

	case kEplApiEventSdo:
		{		// SDO transfer finished
			EplRet =
			    EplApiFreeSdoChannel(pEventArg_p->m_Sdo.
						 m_SdoComConHdl);
			if (EplRet != kEplSuccessful) {
				break;
			}
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			if (pEventArg_p->m_Sdo.m_SdoComConState == kEplSdoComTransferFinished) {	// continue boot-up of CN with NMT command Reset Configuration
				EplRet =
				    EplApiMnTriggerStateChange(pEventArg_p->
							       m_Sdo.m_uiNodeId,
							       kEplNmtNodeCommandConfReset);
			} else {	// indicate configuration error CN
				EplRet =
				    EplApiMnTriggerStateChange(pEventArg_p->
							       m_Sdo.m_uiNodeId,
							       kEplNmtNodeCommandConfErr);
			}
#endif

			break;
		}

	default:
		break;
	}

	return EplRet;
}

//---------------------------------------------------------------------------
//
// Function:    AppCbSync
//
// Description: sync event callback function called by event module within
//              kernel part (high priority).
//              This function sets the outputs, reads the inputs and runs
//              the control loop.
//
// Parameters:  void
//
// Returns:     tEplKernel      = error code,
//                                kEplSuccessful = no error
//                                otherwise = post error event to API layer
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC AppCbSync(void)
{
	tEplKernel EplRet = kEplSuccessful;

	if (bVarOut1Old_l != bVarOut1_l) {	// output variable has changed
		bVarOut1Old_l = bVarOut1_l;
		// set LEDs

//        printk("bVarIn = 0x%02X bVarOut = 0x%02X\n", (WORD) bVarIn_l, (WORD) bVarOut_l);
	}
	if (uiNodeId_g != EPL_C_ADR_MN_DEF_NODE_ID) {
		bVarIn1_l++;
	}

	if (uiNodeId_g == EPL_C_ADR_MN_DEF_NODE_ID) {	// we are the master and must run the control loop

		// collect inputs from CNs and own input
		bSpeedSelect_l = (bVarIn1_l | abSelect_l[0]) & 0x07;

		bModeSelect_l = abSelect_l[1] | abSelect_l[2];

		if ((bModeSelect_l & APP_MODE_MASK) != 0) {
			dwMode_l = bModeSelect_l & APP_MODE_MASK;
		}

		iCurCycleCount_l--;

		if (iCurCycleCount_l <= 0) {
			if ((dwMode_l & 0x01) != 0) {	// fill-up
				if (iToggle) {
					if ((dwLeds_l & APP_DOUBLE_LED_MASK) ==
					    0x00) {
						dwLeds_l = 0x01;
					} else {
						dwLeds_l <<= 1;
						dwLeds_l++;
						if (dwLeds_l >=
						    APP_DOUBLE_LED_MASK) {
							iToggle = 0;
						}
					}
				} else {
					dwLeds_l <<= 1;
					if ((dwLeds_l & APP_DOUBLE_LED_MASK) ==
					    0x00) {
						iToggle = 1;
					}
				}
				bLedsRow1_l =
				    (unsigned char)(dwLeds_l & APP_LED_MASK);
				bLedsRow2_l =
				    (unsigned char)((dwLeds_l >> APP_LED_COUNT)
						    & APP_LED_MASK);
			}

			else if ((dwMode_l & 0x02) != 0) {	// running light forward
				dwLeds_l <<= 1;
				if ((dwLeds_l > APP_DOUBLE_LED_MASK)
				    || (dwLeds_l == 0x00000000L)) {
					dwLeds_l = 0x01;
				}
				bLedsRow1_l =
				    (unsigned char)(dwLeds_l & APP_LED_MASK);
				bLedsRow2_l =
				    (unsigned char)((dwLeds_l >> APP_LED_COUNT)
						    & APP_LED_MASK);
			}

			else if ((dwMode_l & 0x04) != 0) {	// running light backward
				dwLeds_l >>= 1;
				if ((dwLeds_l > APP_DOUBLE_LED_MASK)
				    || (dwLeds_l == 0x00000000L)) {
					dwLeds_l = 1 << (APP_LED_COUNT * 2);
				}
				bLedsRow1_l =
				    (unsigned char)(dwLeds_l & APP_LED_MASK);
				bLedsRow2_l =
				    (unsigned char)((dwLeds_l >> APP_LED_COUNT)
						    & APP_LED_MASK);
			}

			else if ((dwMode_l & 0x08) != 0) {	// Knightrider
				if (bLedsRow1_l == 0x00) {
					bLedsRow1_l = 0x01;
					iToggle = 1;
				} else if (iToggle) {
					bLedsRow1_l <<= 1;
					if (bLedsRow1_l >=
					    (1 << (APP_LED_COUNT - 1))) {
						iToggle = 0;
					}
				} else {
					bLedsRow1_l >>= 1;
					if (bLedsRow1_l <= 0x01) {
						iToggle = 1;
					}
				}
				bLedsRow2_l = bLedsRow1_l;
			}

			else if ((dwMode_l & 0x10) != 0) {	// Knightrider
				if ((bLedsRow1_l == 0x00)
				    || (bLedsRow2_l == 0x00)
				    || ((bLedsRow2_l & ~APP_LED_MASK) != 0)) {
					bLedsRow1_l = 0x01;
					bLedsRow2_l =
					    (1 << (APP_LED_COUNT - 1));
					iToggle = 1;
				} else if (iToggle) {
					bLedsRow1_l <<= 1;
					bLedsRow2_l >>= 1;
					if (bLedsRow1_l >=
					    (1 << (APP_LED_COUNT - 1))) {
						iToggle = 0;
					}
				} else {
					bLedsRow1_l >>= 1;
					bLedsRow2_l <<= 1;
					if (bLedsRow1_l <= 0x01) {
						iToggle = 1;
					}
				}
			}
			// set own output
			bVarOut1_l = bLedsRow1_l;
//            bVarOut1_l = (bLedsRow1_l & 0x03) | (bLedsRow2_l << 2);

			// restart cycle counter
			iCurCycleCount_l = iMaxCycleCount_l;
		}

		if (bSpeedSelectOld_l == 0) {
			if ((bSpeedSelect_l & 0x01) != 0) {
				if (iMaxCycleCount_l < 200) {
					iMaxCycleCount_l++;
				}
				bSpeedSelectOld_l = bSpeedSelect_l;
			} else if ((bSpeedSelect_l & 0x02) != 0) {
				if (iMaxCycleCount_l > 1) {
					iMaxCycleCount_l--;
				}
				bSpeedSelectOld_l = bSpeedSelect_l;
			} else if ((bSpeedSelect_l & 0x04) != 0) {
				iMaxCycleCount_l = DEFAULT_MAX_CYCLE_COUNT;
				bSpeedSelectOld_l = bSpeedSelect_l;
			}
		} else if (bSpeedSelect_l == 0) {
			bSpeedSelectOld_l = 0;
		}
	}

	TGT_DBG_SIGNAL_TRACE_POINT(1);

	return EplRet;
}

// EOF
