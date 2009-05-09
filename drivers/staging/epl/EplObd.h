/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for api function of EplOBD-Module

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

                $RCSfile: EplObd.h,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                Microsoft VC7

  -------------------------------------------------------------------------

  Revision History:

  2006/06/02 k.t.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPLOBD_H_
#define _EPLOBD_H_

#include "EplInc.h"

// ============================================================================
// defines
// ============================================================================

#define EPL_OBD_TABLE_INDEX_END     0xFFFF

// for the usage of BOOLEAN in OD
#define OBD_TRUE    0x01
#define OBD_FALSE   0x00

// default OD index for Node id
#define EPL_OBD_NODE_ID_INDEX               0x1F93
// default subindex for NodeId in OD
#define EPL_OBD_NODE_ID_SUBINDEX            0x01
// default subindex for NodeIDByHW_BOOL
#define EPL_OBD_NODE_ID_HWBOOL_SUBINDEX     0x02

// ============================================================================
// enums
// ============================================================================

// directions for access to object dictionary
typedef enum {
	kEplObdDirInit = 0x00,	// initialising after power on
	kEplObdDirStore = 0x01,	// store all object values to non volatile memory
	kEplObdDirLoad = 0x02,	// load all object values from non volatile memory
	kEplObdDirRestore = 0x03,	// deletes non volatile memory (restore)
	kEplObdDirOBKCheck = 0xFF	// reserved
} tEplObdDir;

// commands for store
typedef enum {
	kEplObdCommNothing = 0x00,
	kEplObdCommOpenWrite = 0x01,
	kEplObdCommWriteObj = 0x02,
	kEplObdCommCloseWrite = 0x03,
	kEplObdCommOpenRead = 0x04,
	kEplObdCommReadObj = 0x05,
	kEplObdCommCloseRead = 0x06,
	kEplObdCommClear = 0x07,
	kEplObdCommUnknown = 0xFF
} tEplObdCommand;

//-----------------------------------------------------------------------------------------------------------
// events of object callback function
typedef enum {
//                                                                                                      m_pArg points to
//                                                                                                    ---------------------
	kEplObdEvCheckExist = 0x06,	// checking if object does exist (reading and writing)    NULL
	kEplObdEvPreRead = 0x00,	// before reading an object                               source data buffer in OD
	kEplObdEvPostRead = 0x01,	// after reading an object                                destination data buffer from caller
	kEplObdEvWrStringDomain = 0x07,	// event for changing string/domain data pointer or size  struct tEplObdVStringDomain in RAM
	kEplObdEvInitWrite = 0x04,	// initializes writing an object (checking object size)   size of object in OD (tEplObdSize)
	kEplObdEvPreWrite = 0x02,	// before writing an object                               source data buffer from caller
	kEplObdEvPostWrite = 0x03,	// after writing an object                                destination data buffer in OD
//    kEplObdEvAbortSdo              = 0x05     // after an abort of an SDO transfer

} tEplObdEvent;

// part of OD (bit oriented)
typedef unsigned int tEplObdPart;

#define kEplObdPartNo          0x00	// nothing
#define kEplObdPartGen         0x01	//  part      (0x1000 - 0x1FFF)
#define kEplObdPartMan         0x02	// manufacturer part (0x2000 - 0x5FFF)
#define kEplObdPartDev         0x04	// device part       (0x6000 - 0x9FFF)
#define kEplObdPartUsr         0x08	// dynamic part e.g. for ICE61131-3

// combinations
#define kEplObdPartApp         (              kEplObdPartMan | kEplObdPartDev | kEplObdPartUsr)	// manufacturer and device part (0x2000 - 0x9FFF) and user OD
#define kEplObdPartAll         (kEplObdPartGen | kEplObdPartMan | kEplObdPartDev | kEplObdPartUsr)	// whole OD

//-----------------------------------------------------------------------------------------------------------
// access types for objects
// must be a difine because bit-flags
typedef unsigned int tEplObdAccess;

#define kEplObdAccRead         0x01	// object can be read
#define kEplObdAccWrite        0x02	// object can be written
#define kEplObdAccConst        0x04	// object contains a constant value
#define kEplObdAccPdo          0x08	// object can be mapped in a PDO
#define kEplObdAccArray        0x10	// object contains an array of numerical values
#define kEplObdAccRange        0x20	// object contains lower and upper limit
#define kEplObdAccVar          0x40	// object data is placed in application
#define kEplObdAccStore        0x80	// object data can be stored to non volatile memory

// combinations (not all combinations are required)
#define kEplObdAccR            (0            | 0          | 0            | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccW            (0            | 0          | 0            | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccRW           (0            | 0          | 0            | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccCR           (0            | 0          | 0            | 0          | kEplObdAccConst | 0            | kEplObdAccRead)
#define kEplObdAccGR           (0            | 0          | kEplObdAccRange | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccGW           (0            | 0          | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccGRW          (0            | 0          | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccVR           (0            | kEplObdAccVar | 0            | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccVW           (0            | kEplObdAccVar | 0            | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccVRW          (0            | kEplObdAccVar | 0            | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccVPR          (0            | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | 0            | kEplObdAccRead)
#define kEplObdAccVPW          (0            | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccVPRW         (0            | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccVGR          (0            | kEplObdAccVar | kEplObdAccRange | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccVGW          (0            | kEplObdAccVar | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccVGRW         (0            | kEplObdAccVar | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccVGPR         (0            | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | 0            | kEplObdAccRead)
#define kEplObdAccVGPW         (0            | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccVGPRW        (0            | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSR           (kEplObdAccStore | 0          | 0            | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSW           (kEplObdAccStore | 0          | 0            | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSRW          (kEplObdAccStore | 0          | 0            | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSCR          (kEplObdAccStore | 0          | 0            | 0          | kEplObdAccConst | 0            | kEplObdAccRead)
#define kEplObdAccSGR          (kEplObdAccStore | 0          | kEplObdAccRange | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSGW          (kEplObdAccStore | 0          | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSGRW         (kEplObdAccStore | 0          | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSVR          (kEplObdAccStore | kEplObdAccVar | 0            | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSVW          (kEplObdAccStore | kEplObdAccVar | 0            | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSVRW         (kEplObdAccStore | kEplObdAccVar | 0            | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSVPR         (kEplObdAccStore | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSVPW         (kEplObdAccStore | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSVPRW        (kEplObdAccStore | kEplObdAccVar | 0            | kEplObdAccPdo | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSVGR         (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | 0          | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSVGW         (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSVGRW        (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | 0          | 0            | kEplObdAccWrite | kEplObdAccRead)
#define kEplObdAccSVGPR        (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | 0            | kEplObdAccRead)
#define kEplObdAccSVGPW        (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | kEplObdAccWrite | 0          )
#define kEplObdAccSVGPRW       (kEplObdAccStore | kEplObdAccVar | kEplObdAccRange | kEplObdAccPdo | 0            | kEplObdAccWrite | kEplObdAccRead)

typedef unsigned int tEplObdSize;	// For all objects as objects size are used an unsigned int.

// -------------------------------------------------------------------------
// types for data types defined in DS301
// -------------------------------------------------------------------------

// types of objects in object dictionary
// DS-301 defines these types as u16
typedef enum {
// types which are always supported
	kEplObdTypBool = 0x0001,

	kEplObdTypInt8 = 0x0002,
	kEplObdTypInt16 = 0x0003,
	kEplObdTypInt32 = 0x0004,
	kEplObdTypUInt8 = 0x0005,
	kEplObdTypUInt16 = 0x0006,
	kEplObdTypUInt32 = 0x0007,
	kEplObdTypReal32 = 0x0008,
	kEplObdTypVString = 0x0009,
	kEplObdTypOString = 0x000A,
	kEplObdTypDomain = 0x000F,

	kEplObdTypInt24 = 0x0010,
	kEplObdTypUInt24 = 0x0016,

	kEplObdTypReal64 = 0x0011,
	kEplObdTypInt40 = 0x0012,
	kEplObdTypInt48 = 0x0013,
	kEplObdTypInt56 = 0x0014,
	kEplObdTypInt64 = 0x0015,
	kEplObdTypUInt40 = 0x0018,
	kEplObdTypUInt48 = 0x0019,
	kEplObdTypUInt56 = 0x001A,
	kEplObdTypUInt64 = 0x001B,
	kEplObdTypTimeOfDay = 0x000C,
	kEplObdTypTimeDiff = 0x000D
} tEplObdType;
// other types are not supported in this version

// -------------------------------------------------------------------------
// types for data types defined in DS301
// -------------------------------------------------------------------------

typedef unsigned char tEplObdBoolean;	// 0001
typedef signed char tEplObdInteger8;	// 0002
typedef signed short int tEplObdInteger16;	// 0003
typedef signed long tEplObdInteger32;	// 0004
typedef unsigned char tEplObdUnsigned8;	// 0005
typedef unsigned short int tEplObdUnsigned16;	// 0006
typedef unsigned long tEplObdUnsigned32;	// 0007
typedef float tEplObdReal32;	// 0008
typedef unsigned char tEplObdDomain;	// 000F
typedef signed long tEplObdInteger24;	// 0010
typedef unsigned long tEplObdUnsigned24;	// 0016

typedef s64 tEplObdInteger40;	// 0012
typedef s64 tEplObdInteger48;	// 0013
typedef s64 tEplObdInteger56;	// 0014
typedef s64 tEplObdInteger64;	// 0015

typedef u64 tEplObdUnsigned40;	// 0018
typedef u64 tEplObdUnsigned48;	// 0019
typedef u64 tEplObdUnsigned56;	// 001A
typedef u64 tEplObdUnsigned64;	// 001B

typedef double tEplObdReal64;	// 0011

typedef tTimeOfDay tEplObdTimeOfDay;	// 000C
typedef tTimeOfDay tEplObdTimeDifference;	// 000D

// -------------------------------------------------------------------------
// structur for defining a variable
// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
typedef enum {
	kVarValidSize = 0x01,
	kVarValidData = 0x02,
//    kVarValidCallback       = 0x04,
//    kVarValidArg            = 0x08,

	kVarValidAll = 0x03	// currently only size and data are implemented and used
} tEplVarParamValid;

typedef tEplKernel(*tEplVarCallback) (CCM_DECL_INSTANCE_HDL_ void *pParam_p);

typedef struct {
	tEplVarParamValid m_ValidFlag;
	unsigned int m_uiIndex;
	unsigned int m_uiSubindex;
	tEplObdSize m_Size;
	void *m_pData;
//    tEplVarCallback     m_fpCallback;
//    void *       m_pArg;

} tEplVarParam;

typedef struct {
	void *m_pData;
	tEplObdSize m_Size;
/*
    #if (EPL_PDO_USE_STATIC_MAPPING == FALSE)
        tEplVarCallback    m_fpCallback;
        void *   m_pArg;
    #endif
*/
} tEplObdVarEntry;

typedef struct {
	tEplObdSize m_Size;
	u8 *m_pString;

} tEplObdOString;		// 000C

typedef struct {
	tEplObdSize m_Size;
	char *m_pString;
} tEplObdVString;		// 000D

typedef struct {
	tEplObdSize m_Size;
	char *m_pDefString;	// $$$ d.k. it is unused, so we could delete it
	char *m_pString;

} tEplObdVStringDef;

typedef struct {
	tEplObdSize m_Size;
	u8 *m_pDefString;	// $$$ d.k. it is unused, so we could delete it
	u8 *m_pString;

} tEplObdOStringDef;

//r.d. parameter struct for changing object size and/or pointer to data of Strings or Domains
typedef struct {
	tEplObdSize m_DownloadSize;	// download size from SDO or APP
	tEplObdSize m_ObjSize;	// current object size from OD - should be changed from callback function
	void *m_pData;		// current object ptr  from OD - should be changed from callback function

} tEplObdVStringDomain;		// 000D

// ============================================================================
// types
// ============================================================================
// -------------------------------------------------------------------------
// subindexstruct
// -------------------------------------------------------------------------

// Change not the order for this struct!!!
typedef struct {
	unsigned int m_uiSubIndex;
	tEplObdType m_Type;
	tEplObdAccess m_Access;
	void *m_pDefault;
	void *m_pCurrent;	// points always to RAM

} tEplObdSubEntry;

// r.d.: has always to be  because new OBD-Macros for arrays
typedef tEplObdSubEntry *tEplObdSubEntryPtr;

// -------------------------------------------------------------------------
// callback function for objdictionary modul
// -------------------------------------------------------------------------

// parameters for callback function
typedef struct {
	tEplObdEvent m_ObdEvent;
	unsigned int m_uiIndex;
	unsigned int m_uiSubIndex;
	void *m_pArg;
	u32 m_dwAbortCode;

} tEplObdCbParam;

// define type for callback function: pParam_p points to tEplObdCbParam
typedef tEplKernel(*tEplObdCallback) (CCM_DECL_INSTANCE_HDL_ tEplObdCbParam *pParam_p);

// do not change the order for this struct!!!

typedef struct {
	unsigned int m_uiIndex;
	tEplObdSubEntryPtr m_pSubIndex;
	unsigned int m_uiCount;
	tEplObdCallback m_fpCallback;	// function is called back if object access

} tEplObdEntry;

// allways  pointer
typedef tEplObdEntry *tEplObdEntryPtr;

// -------------------------------------------------------------------------
// structur to initialize OBD module
// -------------------------------------------------------------------------

typedef struct {
	tEplObdEntryPtr m_pPart;
	tEplObdEntryPtr m_pManufacturerPart;
	tEplObdEntryPtr m_pDevicePart;

#if (defined (EPL_OBD_USER_OD) && (EPL_OBD_USER_OD != FALSE))

	tEplObdEntryPtr m_pUserPart;

#endif

} tEplObdInitParam;

// -------------------------------------------------------------------------
// structur for parameters of STORE RESTORE command
// -------------------------------------------------------------------------

typedef struct {
	tEplObdCommand m_bCommand;
	tEplObdPart m_bCurrentOdPart;
	void *m_pData;
	tEplObdSize m_ObjSize;

} tEplObdCbStoreParam;

typedef tEplKernel(*tInitTabEntryCallback) (void *pTabEntry_p, unsigned int uiObjIndex_p);

typedef tEplKernel(*tEplObdStoreLoadObjCallback) (CCM_DECL_INSTANCE_HDL_ tEplObdCbStoreParam *pCbStoreParam_p);

// -------------------------------------------------------------------------
// this stucture is used for parameters for function ObdInitModuleTab()
// -------------------------------------------------------------------------
typedef struct {
	unsigned int m_uiLowerObjIndex;	// lower limit of ObjIndex
	unsigned int m_uiUpperObjIndex;	// upper limit of ObjIndex
	tInitTabEntryCallback m_fpInitTabEntry;	// will be called if ObjIndex was found
	void *m_pTabBase;	// base address of table
	unsigned int m_uiEntrySize;	// size of table entry      // 25-feb-2005 r.d.: expansion from u8 to u16 necessary for PDO bit mapping
	unsigned int m_uiMaxEntries;	// max. tabel entries

} tEplObdModulTabParam;

//-------------------------------------------------------------------
//  enum for function EplObdSetNodeId
//-------------------------------------------------------------------
typedef enum {
	kEplObdNodeIdUnknown = 0x00,	// unknown how the node id was set
	kEplObdNodeIdSoftware = 0x01,	// node id set by software
	kEplObdNodeIdHardware = 0x02	// node id set by hardware
} tEplObdNodeIdType;

// ============================================================================
// global variables
// ============================================================================

// ============================================================================
// public functions
// ============================================================================

#endif // #ifndef _EPLOBD_H_
