/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for Epl Configuration Manager Module

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

                $RCSfile: EplCfgMau.h,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                VC7

  -------------------------------------------------------------------------

  Revision History:

  2006/07/14 k.t.:   start of the implementation
                     -> based on CANopen CfgMa-Modul (CANopen version 5.34)

****************************************************************************/

#ifndef _EPLCFGMA_H_
#define _EPLCFGMA_H_

#include "../EplInc.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_CFGMA)) != 0)

#include "EplObdu.h"
#include "EplSdoComu.h"

//define max number of timeouts for configuration of 1 device
#define EPL_CFGMA_MAX_TIMEOUT   3

//callbackfunction, called if configuration is finished
typedef void (* tfpEplCfgMaCb)(unsigned int uiNodeId_p,
			       tEplKernel Errorstate_p);

//State for configuartion manager Statemachine
typedef enum {
	// general states
	kEplCfgMaIdle = 0x0000,	// Configurationsprocess
	// is idle
	kEplCfgMaWaitForSdocEvent = 0x0001,	// wait until the last
	// SDOC is finisched
	kEplCfgMaSkipMappingSub0 = 0x0002,	// write Sub0 of mapping
	// parameter with 0

	kEplCfgMaFinished = 0x0004	// configuartion is finished
} tEplCfgState;

typedef enum {
	kEplCfgMaDcfTypSystecSeg = 0x00,
	kEplCfgMaDcfTypConDcf = 0x01,
	kEplCfgMaDcfTypDcf = 0x02,	// not supported
	kEplCfgMaDcfTypXdc = 0x03	// not supported
} tEplCfgMaDcfTyp;

typedef enum {
	kEplCfgMaCommon = 0,	// all other index
	kEplCfgMaPdoComm = 1,	// communication index
	kEplCfgMaPdoMapp = 2,	// mapping index
	kEplCfgMaPdoCommAfterMapp = 3,	// write PDO Cob-Id after mapping subindex 0(set PDO valid)

} tEplCfgMaIndexType;

//bitcoded answer about the last sdo transfer saved in m_SdocState
// also used to singal start of the State Maschine
typedef enum {
	kEplCfgMaSdocBusy = 0x00,	// SDOC activ
	kEplCfgMaSdocReady = 0x01,	// SDOC finished
	kEplCfgMaSdocTimeout = 0x02,	// SDOC Timeout
	kEplCfgMaSdocAbortReceived = 0x04,	// SDOC Abort, see Abortcode
	kEplCfgMaSdocStart = 0x08	// start State Mschine
} tEplSdocState;

//internal structure (instancetable for modul configuration manager)
typedef struct {
	tEplCfgState m_CfgState;	// state of the configuration state maschine
	tEplSdoComConHdl m_SdoComConHdl;	// handle for sdo connection
	u32 m_dwLastAbortCode;
	unsigned int m_uiLastIndex;	// last index of configuration, to compair with actual index
	u8 *m_pbConcise;	// Ptr to concise DCF
	u8 *m_pbActualIndex;	// Ptr to actual index in the DCF segment
	tfpEplCfgMaCb m_pfnCfgMaCb;	// Ptr to CfgMa Callback, is call if configuration finished
	tEplKernel m_EplKernelError;	// errorcode
	u32 m_dwNumValueCopy;	// numeric values are copied in this variable
	unsigned int m_uiPdoNodeId;	// buffer for PDO node id
	u8 m_bNrOfMappedObject;	// number of mapped objects
	unsigned int m_uiNodeId;	// Epl node addresse
	tEplSdocState m_SdocState;	// bitcoded state of the SDO transfer
	unsigned int m_uiLastSubIndex;	// last subindex of configuration
	BOOL m_fOneTranferOk;	// atleased one transfer was successful
	u8 m_bEventFlag;	// for Eventsignaling to the State Maschine
	u32 m_dwCntObjectInDcf;	// number of Objects in DCF
	tEplCfgMaIndexType m_SkipCfg;	// TRUE if a adsitional Configurationprocess
	// have to insert e.g. PDO-mapping
	u16 m_wTimeOutCnt;	// Timeout Counter, break configuration is
	// m_wTimeOutCnt == CFGMA_MAX_TIMEOUT

} tEplCfgMaNode;

//---------------------------------------------------------------------------
// Function:    EplCfgMaInit()
//
// Description: Function creates first instance of Configuration Manager
//
// Parameters:
//
// Returns:     tEplKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaInit(void);

//---------------------------------------------------------------------------
// Function:    EplCfgMaAddInstance()
//
// Description: Function creates additional instance of Configuration Manager
//
// Parameters:
//
// Returns:     tEplKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaAddInstance(void);

//---------------------------------------------------------------------------
// Function:    EplCfgMaDelInstance()
//
// Description: Function delete instance of Configuration Manager
//
// Parameters:
//
// Returns:     tEplKernel              = error code
//---------------------------------------------------------------------------
tEplKernel plCfgMaDelInstance(void);

//---------------------------------------------------------------------------
// Function:    EplCfgMaStartConfig()
//
// Description: Function starts the configuration process
//              initialization the statemachine for CfgMa- process
//
// Parameters:  uiNodeId_p              = NodeId of the node to configure
//              pbConcise_p             = pointer to DCF
//              fpCfgMaCb_p             = pointer to callback function (should not be NULL)
//              SizeOfConcise_p         = size of DCF in u8 -> for future use
//              DcfType_p               = type of the DCF
//
// Returns:     tCopKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaStartConfig(unsigned int uiNodeId_p,
			       u8 * pbConcise_p,
			       tfpEplCfgMaCb fpCfgMaCb_p,
			       tEplObdSize SizeOfConcise_p,
			       tEplCfgMaDcfTyp DcfType_p);

//---------------------------------------------------------------------------
// Function:    CfgMaStartConfigurationNode()
//
// Description: Function started the configuration process
//              with the DCF from according OD-entry Subindex == bNodeId_p
//
// Parameters:  uiNodeId_p              = NodeId of the node to configure
//              fpCfgMaCb_p             = pointer to callback function (should not be NULL)
//              DcfType_p               = type of the DCF
//
// Returns:     tCopKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaStartConfigNode(unsigned int uiNodeId_p,
				   tfpEplCfgMaCb fpCfgMaCb_p,
				   tEplCfgMaDcfTyp DcfType_p);

//---------------------------------------------------------------------------
// Function:    EplCfgMaStartConfigNodeDcf()
//
// Description: Function starts the configuration process
//              and links the configuration data to the OD
//
// Parameters:  uiNodeId_p              = NodeId of the node to configure
//              pbConcise_p             = pointer to DCF
//              fpCfgMaCb_p             = pointer to callback function (should not be NULL)
//              SizeOfConcise_p         = size of DCF in u8 -> for future use
//              DcfType_p               = type of the DCF
//
// Returns:     tCopKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaStartConfigNodeDcf(unsigned int uiNodeId_p,
				      u8 * pbConcise_p,
				      tfpEplCfgMaCb fpCfgMaCb_p,
				      tEplObdSize SizeOfConcise_p,
				      tEplCfgMaDcfTyp DcfType_p);

//---------------------------------------------------------------------------
// Function:    EplCfgMaLinkDcf()
//
// Description: Function links the configuration data to the OD
//
// Parameters:  uiNodeId_p              = NodeId of the node to configure
//              pbConcise_p             = pointer to DCF
//              SizeOfConcise_p        = size of DCF in u8 -> for future use
//              DcfType_p               = type of the DCF
//
// Returns:     tCopKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaLinkDcf(unsigned int uiNodeId_p,
			   u8 * pbConcise_p,
			   tEplObdSize SizeOfConcise_p,
			   tEplCfgMaDcfTyp DcfType_p);

//---------------------------------------------------------------------------
// Function:    EplCfgMaCheckDcf()
//
// Description: Function check if there is allready a configuration file linked
//              to the OD (type is given by DcfType_p)
//
// Parameters:  uiNodeId_p              = NodeId
//              DcfType_p               = type of the DCF
//
// Returns:     tCopKernel              = error code
//---------------------------------------------------------------------------
tEplKernel EplCfgMaCheckDcf(unsigned int uiNodeId_p, tEplCfgMaDcfTyp DcfType_p);

#endif // #if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_CFGMA)) != 0)

#endif // _EPLCFGMA_H_

// EOF
