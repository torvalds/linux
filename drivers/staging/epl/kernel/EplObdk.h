/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for Epl-Obd-Kernel-Modul

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

                $RCSfile: EplObdk.h,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/19 k.t.:   start of the implementation

****************************************************************************/

#ifndef _EPLOBDK_H_
#define _EPLOBDK_H_

#include "../EplObd.h"

extern u8 abEplObdTrashObject_g[8];

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
// ---------------------------------------------------------------------
tEplKernel EplObdInit(EPL_MCO_DECL_PTR_INSTANCE_PTR_ tEplObdInitParam *pInitParam_p);

// ---------------------------------------------------------------------
tEplKernel EplObdAddInstance(EPL_MCO_DECL_PTR_INSTANCE_PTR_ tEplObdInitParam *pInitParam_p);

// ---------------------------------------------------------------------
tEplKernel EplObdDeleteInstance(EPL_MCO_DECL_INSTANCE_PTR);

// ---------------------------------------------------------------------
tEplKernel EplObdWriteEntry(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			    unsigned int uiSubIndex_p,
			    void *pSrcData_p, tEplObdSize Size_p);

// ---------------------------------------------------------------------
tEplKernel EplObdReadEntry(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			   unsigned int uiSubIndex_p,
			   void *pDstData_p, tEplObdSize *pSize_p);

// ---------------------------------------------------------------------
tEplKernel EplObdSetStoreLoadObjCallback(EPL_MCO_DECL_INSTANCE_PTR_ tEplObdStoreLoadObjCallback fpCallback_p);

// ---------------------------------------------------------------------
tEplKernel EplObdAccessOdPart(EPL_MCO_DECL_INSTANCE_PTR_ tEplObdPart ObdPart_p,
			      tEplObdDir Direction_p);

// ---------------------------------------------------------------------
tEplKernel EplObdDefineVar(EPL_MCO_DECL_INSTANCE_PTR_ tEplVarParam *pVarParam_p);

// ---------------------------------------------------------------------
void *EplObdGetObjectDataPtr(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			     unsigned int uiSubIndex_p);
// ---------------------------------------------------------------------
tEplKernel EplObdRegisterUserOd(EPL_MCO_DECL_INSTANCE_PTR_ tEplObdEntryPtr pUserOd_p);

// ---------------------------------------------------------------------
void EplObdInitVarEntry(EPL_MCO_DECL_INSTANCE_PTR_ tEplObdVarEntry *pVarEntry_p,
			tEplObdType Type_p, tEplObdSize ObdSize_p);

// ---------------------------------------------------------------------
tEplObdSize EplObdGetDataSize(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			      unsigned int uiSubIndex_p);

// ---------------------------------------------------------------------
unsigned int EplObdGetNodeId(EPL_MCO_DECL_INSTANCE_PTR);

// ---------------------------------------------------------------------
tEplKernel EplObdSetNodeId(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiNodeId_p,
			   tEplObdNodeIdType NodeIdType_p);

// ---------------------------------------------------------------------
tEplKernel EplObdIsNumerical(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			     unsigned int uiSubIndex_p, BOOL *pfEntryNumerical);
// ---------------------------------------------------------------------
tEplKernel EplObdWriteEntryFromLe(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
				  unsigned int uiSubIndex_p,
				  void *pSrcData_p,
				  tEplObdSize Size_p);

// ---------------------------------------------------------------------
tEplKernel EplObdReadEntryToLe(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			       unsigned int uiSubIndex_p,
			       void *pDstData_p,
			       tEplObdSize *pSize_p);

// ---------------------------------------------------------------------
tEplKernel EplObdGetAccessType(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			       unsigned int uiSubIndex_p,
			       tEplObdAccess *pAccessTyp_p);

// ---------------------------------------------------------------------
tEplKernel EplObdSearchVarEntry(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
				unsigned int uiSubindex_p,
				tEplObdVarEntry **ppVarEntry_p);

#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)

#endif // #ifndef _EPLOBDK_H_
