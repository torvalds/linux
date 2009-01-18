/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      Project independend shared buffer (linear + circular)

  Description:  Declaration of platform specific part for the
                shared buffer

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

  2006/06/27 -rs:   V 1.00 (initial version)

****************************************************************************/

#ifndef _SHBIPC_H_
#define _SHBIPC_H_

//---------------------------------------------------------------------------
//  Type definitions
//---------------------------------------------------------------------------

typedef int (*tSigHndlrNewData) (tShbInstance pShbInstance_p);
typedef void (*tSigHndlrJobReady) (tShbInstance pShbInstance_p,
				   unsigned int fTimeOut_p);

#if (TARGET_SYSTEM == _WIN32_)
#if defined(INLINE_FUNCTION_DEF)
#undef  INLINE_FUNCTION
#define INLINE_FUNCTION     INLINE_FUNCTION_DEF
#define SHBIPC_INLINE_ENABLED      TRUE
#define SHBIPC_INLINED
#include "ShbIpc-Win32.c"
#endif

#elif (TARGET_SYSTEM == _LINUX_)
#if defined(INLINE_FUNCTION_DEF)
#undef  INLINE_FUNCTION
#define INLINE_FUNCTION     INLINE_FUNCTION_DEF
#define SHBIPC_INLINE_ENABLED      TRUE
#define SHBIPC_INLINED
#include "ShbIpc-LinuxKernel.c"
#endif
#endif

//---------------------------------------------------------------------------
//  Prototypes
//---------------------------------------------------------------------------

tShbError ShbIpcInit(void);
tShbError ShbIpcExit(void);

tShbError ShbIpcAllocBuffer(unsigned long ulBufferSize_p,
			    const char *pszBufferID_p,
			    tShbInstance * ppShbInstance_p,
			    unsigned int *pfShbNewCreated_p);
tShbError ShbIpcReleaseBuffer(tShbInstance pShbInstance_p);

#if !defined(SHBIPC_INLINE_ENABLED)

tShbError ShbIpcEnterAtomicSection(tShbInstance pShbInstance_p);
tShbError ShbIpcLeaveAtomicSection(tShbInstance pShbInstance_p);

tShbError ShbIpcStartSignalingNewData(tShbInstance pShbInstance_p,
				      tSigHndlrNewData
				      pfnSignalHandlerNewData_p,
				      tShbPriority ShbPriority_p);
tShbError ShbIpcStopSignalingNewData(tShbInstance pShbInstance_p);
tShbError ShbIpcSignalNewData(tShbInstance pShbInstance_p);

tShbError ShbIpcStartSignalingJobReady(tShbInstance pShbInstance_p,
				       unsigned long ulTimeOut_p,
				       tSigHndlrJobReady
				       pfnSignalHandlerJobReady_p);
tShbError ShbIpcSignalJobReady(tShbInstance pShbInstance_p);

void *ShbIpcGetShMemPtr(tShbInstance pShbInstance_p);
#endif

#undef  SHBIPC_INLINE_ENABLED	// disable actual inlining of functions
#undef  INLINE_FUNCTION
#define INLINE_FUNCTION		// define INLINE_FUNCTION to nothing

#endif // #ifndef _SHBIPC_H_
