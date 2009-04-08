/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  definitions for generating instances

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

                $RCSfile: EplInstDef.h,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    ...

  -------------------------------------------------------------------------

  Revision History:

  r.d.: first implementation

****************************************************************************/

#ifndef _EPLINSTDEF_H_
#define _EPLINSTDEF_H_

#include <linux/kernel.h>

// =========================================================================
// types and macros for generating instances
// =========================================================================

typedef enum {
	kStateUnused = 0,
	kStateDeleted = 1,
	kStateUsed = 0xFF
} tInstState;

//------------------------------------------------------------------------------------------

typedef void *tEplPtrInstance;
typedef u8 tEplInstanceHdl;

// define const for illegale values
#define CCM_ILLINSTANCE      NULL
#define CCM_ILLINSTANCE_HDL  0xFF

//------------------------------------------------------------------------------------------
// if more than one instance then use this macros
#if (EPL_MAX_INSTANCES > 1)

    //--------------------------------------------------------------------------------------
    // macro definition for instance table definition
    //--------------------------------------------------------------------------------------

    // memory attributes for instance table
#define STATIC			// prevent warnings for variables with same name

#define INSTANCE_TYPE_BEGIN     typedef struct {
#define INSTANCE_TYPE_END       } tEplInstanceInfo;

    //--------------------------------------------------------------------------------------
    // macro definition for API interface
    //--------------------------------------------------------------------------------------

    // declaration:

    // macros for declaration within function header or prototype of API functions
#define CCM_DECL_INSTANCE_HDL                   tEplInstanceHdl InstanceHandle
#define CCM_DECL_INSTANCE_HDL_                  tEplInstanceHdl InstanceHandle,

    // macros for declaration of pointer to instance handle within function header or prototype of API functions
#define CCM_DECL_PTR_INSTANCE_HDL               tEplInstanceHdl *pInstanceHandle
#define CCM_DECL_PTR_INSTANCE_HDL_              tEplInstanceHdl *pInstanceHandle,

    // macros for declaration instance as lokacl variable within functions
#define CCM_DECL_INSTANCE_PTR_LOCAL             tCcmInstanceInfo *pInstance;
#define CCM_DECL_PTR_INSTANCE_HDL_LOCAL         tEplInstanceHdl  *pInstanceHandle;

    // reference:

    // macros for reference of instance handle for function parameters
#define CCM_INSTANCE_HDL                        InstanceHandle
#define CCM_INSTANCE_HDL_                       InstanceHandle,

    // macros for reference of instance parameter for function parameters
#define CCM_INSTANCE_PARAM(par)                 par
#define CCM_INSTANCE_PARAM_(par)                par,

    // macros for reference of instance parameter for writing or reading values
#define CCM_INST_ENTRY                          (*((tEplPtrInstance)pInstance))

    // processing:

    // macros for process instance handle
#define CCM_CHECK_INSTANCE_HDL()                if (InstanceHandle >= EPL_MAX_INSTANCES) \
                                                        {return (kEplIllegalInstance);}

    // macros for process pointer to instance handle
#define CCM_CHECK_PTR_INSTANCE_HDL()            if (pInstanceHandle == NULL) \
                                                        {return (kEplInvalidInstanceParam);}

    // This macro returned the handle and pointer to next free instance.
#define CCM_GET_FREE_INSTANCE_AND_HDL()         pInstance = CcmGetFreeInstanceAndHandle (pInstanceHandle); \
                                                    ASSERT (*pInstanceHandle != CCM_ILLINSTANCE_HDL);

#define CCM_CHECK_INSTANCE_PTR()                if (pInstance == CCM_ILLINSTANCE) \
                                                        {return (kEplNoFreeInstance);}

#define CCM_GET_INSTANCE_PTR()                  pInstance = CcmGetInstancePtr (InstanceHandle);
#define CCM_GET_FREE_INSTANCE_PTR()             pInstance = GetFreeInstance (); \
                                                    ASSERT (pInstance != CCM_ILLINSTANCE);

    //--------------------------------------------------------------------------------------
    // macro definition for stack interface
    //--------------------------------------------------------------------------------------

    // macros for declaration within the function header, prototype or local var list
    // Declaration of pointers within function paramater list must defined as void *
    // pointer.
#define EPL_MCO_DECL_INSTANCE_PTR                   void *pInstance
#define EPL_MCO_DECL_INSTANCE_PTR_                  void *pInstance,
#define EPL_MCO_DECL_INSTANCE_PTR_LOCAL             tEplPtrInstance  pInstance;

    // macros for reference of pointer to instance
    // These macros are used for parameter passing to called function.
#define EPL_MCO_INSTANCE_PTR                        pInstance
#define EPL_MCO_INSTANCE_PTR_                       pInstance,
#define EPL_MCO_ADDR_INSTANCE_PTR_                  &pInstance,

    // macro for access of struct members of one instance
    // An access to a member of instance table must be casted by the local
    // defined type of instance table.
#define EPL_MCO_INST_ENTRY                          (*(tEplPtrInstance)pInstance)
#define EPL_MCO_GLB_VAR(var)                        (((tEplPtrInstance)pInstance)->var)

    // macros for process pointer to instance
#define EPL_MCO_GET_INSTANCE_PTR()                  pInstance = (tEplPtrInstance) GetInstancePtr (InstanceHandle);
#define EPL_MCO_GET_FREE_INSTANCE_PTR()             pInstance = (tEplPtrInstance) GetFreeInstance (); \
                                                    ASSERT (pInstance != CCM_ILLINSTANCE);

    // This macro should be used to check the passed pointer to an public function
#define EPL_MCO_CHECK_INSTANCE_STATE()              ASSERT (pInstance != NULL); \
                                                    ASSERT (((tEplPtrInstance)pInstance)->m_InstState == kStateUsed);

    // macros for declaration of pointer to instance pointer
#define EPL_MCO_DECL_PTR_INSTANCE_PTR               void **pInstancePtr
#define EPL_MCO_DECL_PTR_INSTANCE_PTR_              void **pInstancePtr,

    // macros for reference of pointer to instance pointer
    // These macros are used for parameter passing to called function.
#define EPL_MCO_PTR_INSTANCE_PTR                    pInstancePtr
#define EPL_MCO_PTR_INSTANCE_PTR_                   pInstancePtr,

    // macros for process pointer to instance pointer
#define EPL_MCO_CHECK_PTR_INSTANCE_PTR()            ASSERT (pInstancePtr != NULL);
#define EPL_MCO_SET_PTR_INSTANCE_PTR()              (*pInstancePtr = pInstance);

#define EPL_MCO_INSTANCE_PARAM(a)                   (a)
#define EPL_MCO_INSTANCE_PARAM_(a)                  (a),
#define EPL_MCO_INSTANCE_PARAM_IDX_()               EPL_MCO_INSTANCE_PARAM_ (EPL_MCO_GLB_VAR (m_bInstIndex))
#define EPL_MCO_INSTANCE_PARAM_IDX()                EPL_MCO_INSTANCE_PARAM (EPL_MCO_GLB_VAR (m_bInstIndex))
#define EPL_MCO_WRITE_INSTANCE_STATE(a)             EPL_MCO_GLB_VAR (m_InstState) = a;

    // this macro deletes all instance entries as unused
#define EPL_MCO_DELETE_INSTANCE_TABLE()                                    \
    {                                                                      \
        tEplInstanceInfo *   pInstance       = &aEplInstanceTable_g[0];    \
        tFastByte            InstNumber      = 0;                          \
        tFastByte            i               = EPL_MAX_INSTANCES;          \
        do {                                                               \
            pInstance->m_InstState = (u8) kStateUnused;                  \
            pInstance->m_bInstIndex = (u8) InstNumber;                   \
            pInstance++; InstNumber++; i--;                                \
        } while (i != 0);                                                  \
    }

    // definition of functions which has to be defined in each module of CANopen stack
#define EPL_MCO_DEFINE_INSTANCE_FCT() \
        static tEplPtrInstance GetInstancePtr (tEplInstanceHdl InstHandle_p);  \
        static tEplPtrInstance GetFreeInstance (void);
#define EPL_MCO_DECL_INSTANCE_FCT()                                            \
        static tEplPtrInstance GetInstancePtr (tEplInstanceHdl InstHandle_p) { \
            return &aEplInstanceTable_g[InstHandle_p]; }                       \
        static tEplPtrInstance GetFreeInstance (void) {                        \
            tEplInstanceInfo *pInstance   = &aEplInstanceTable_g[0];           \
            tFastByte         i           = EPL_MAX_INSTANCES;                 \
            do { if (pInstance->m_InstState != kStateUsed) {                   \
                    return (tEplPtrInstance) pInstance; }                      \
                pInstance++; i--; }                                            \
            while (i != 0);                                                    \
            return CCM_ILLINSTANCE; }

    // this macro defines the instance table. Each entry is reserved for an instance of CANopen.
#define EPL_MCO_DECL_INSTANCE_VAR() \
        static tEplInstanceInfo aEplInstanceTable_g [EPL_MAX_INSTANCES];

    // this macro defines member variables in instance table which are needed in
    // all modules of Epl stack
#define EPL_MCO_DECL_INSTANCE_MEMBER() \
        STATIC  u8                            m_InstState; \
        STATIC  u8                            m_bInstIndex;

#define EPL_MCO_INSTANCE_PARAM_IDX_()           EPL_MCO_INSTANCE_PARAM_ (EPL_MCO_GLB_VAR (m_bInstIndex))
#define EPL_MCO_INSTANCE_PARAM_IDX()            EPL_MCO_INSTANCE_PARAM (EPL_MCO_GLB_VAR (m_bInstIndex))

#else // only one instance is used

    // Memory attributes for instance table.
#define STATIC      static	// prevent warnings for variables with same name

#define INSTANCE_TYPE_BEGIN
#define INSTANCE_TYPE_END

// macros for declaration, initializing and member access for instance handle
// This class of macros are used by API function to inform CCM-modul which
// instance is to be used.

    // macros for reference of instance handle
    // These macros are used for parameter passing to CANopen API function.
#define CCM_INSTANCE_HDL
#define CCM_INSTANCE_HDL_

#define CCM_DECL_INSTANCE_PTR_LOCAL

    // macros for declaration within the function header or prototype
#define CCM_DECL_INSTANCE_HDL                   void
#define CCM_DECL_INSTANCE_HDL_

    // macros for process instance handle
#define CCM_CHECK_INSTANCE_HDL()

    // macros for declaration of pointer to instance handle
#define CCM_DECL_PTR_INSTANCE_HDL               void
#define CCM_DECL_PTR_INSTANCE_HDL_

    // macros for process pointer to instance handle
#define CCM_CHECK_PTR_INSTANCE_HDL()

    // This macro returned the handle and pointer to next free instance.
#define CCM_GET_FREE_INSTANCE_AND_HDL()

#define CCM_CHECK_INSTANCE_PTR()

#define CCM_GET_INSTANCE_PTR()
#define CCM_GET_FREE_INSTANCE_PTR()

#define CCM_INSTANCE_PARAM(par)
#define CCM_INSTANCE_PARAM_(par)

#define CCM_INST_ENTRY                          aCcmInstanceTable_g[0]

// macros for declaration, initializing and member access for instance pointer
// This class of macros are used by CANopen internal function to point to one instance.

    // macros for declaration within the function header, prototype or local var list
#define EPL_MCO_DECL_INSTANCE_PTR                   void
#define EPL_MCO_DECL_INSTANCE_PTR_
#define EPL_MCO_DECL_INSTANCE_PTR_LOCAL

    // macros for reference of pointer to instance
    // These macros are used for parameter passing to called function.
#define EPL_MCO_INSTANCE_PTR
#define EPL_MCO_INSTANCE_PTR_
#define EPL_MCO_ADDR_INSTANCE_PTR_

    // macros for process pointer to instance
#define EPL_MCO_GET_INSTANCE_PTR()
#define EPL_MCO_GET_FREE_INSTANCE_PTR()

    // This macro should be used to check the passed pointer to an public function
#define EPL_MCO_CHECK_INSTANCE_STATE()

    // macros for declaration of pointer to instance pointer
#define EPL_MCO_DECL_PTR_INSTANCE_PTR               void
#define EPL_MCO_DECL_PTR_INSTANCE_PTR_

    // macros for reference of pointer to instance pointer
    // These macros are used for parameter passing to called function.
#define EPL_MCO_PTR_INSTANCE_PTR
#define EPL_MCO_PTR_INSTANCE_PTR_

    // macros for process pointer to instance pointer
#define EPL_MCO_CHECK_PTR_INSTANCE_PTR()
#define EPL_MCO_SET_PTR_INSTANCE_PTR()

#define EPL_MCO_INSTANCE_PARAM(a)
#define EPL_MCO_INSTANCE_PARAM_(a)
#define EPL_MCO_INSTANCE_PARAM_IDX_()
#define EPL_MCO_INSTANCE_PARAM_IDX()

    // macro for access of struct members of one instance
#define EPL_MCO_INST_ENTRY                          aEplInstanceTable_g[0]
#define EPL_MCO_GLB_VAR(var)                        (var)
#define EPL_MCO_WRITE_INSTANCE_STATE(a)

    // this macro deletes all instance entries as unused
#define EPL_MCO_DELETE_INSTANCE_TABLE()

    // definition of functions which has to be defined in each module of CANopen stack
#define EPL_MCO_DEFINE_INSTANCE_FCT()
#define EPL_MCO_DECL_INSTANCE_FCT()

    // this macro defines the instance table. Each entry is reserved for an instance of CANopen.
#define EPL_MCO_DECL_INSTANCE_VAR()

    // this macro defines member variables in instance table which are needed in
    // all modules of CANopen stack
#define EPL_MCO_DECL_INSTANCE_MEMBER()

#endif

#endif // _EPLINSTDEF_H_

// Die letzte Zeile muﬂ unbedingt eine leere Zeile sein, weil manche Compiler
// damit ein Problem haben, wenn das nicht so ist (z.B. GNU oder Borland C++ Builder).
