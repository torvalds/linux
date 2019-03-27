//===------------------------------- unwind.h -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
// C++ ABI Level 1 ABI documented at:
//   http://mentorembedded.github.io/cxx-abi/abi-eh.html
//
//===----------------------------------------------------------------------===//

#ifndef __UNWIND_H__
#define __UNWIND_H__

#include <__libunwind_config.h>

#include <stdint.h>
#include <stddef.h>

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__) && defined(_WIN32)
#include <windows.h>
#include <ntverp.h>
#endif

#if defined(__APPLE__)
#define LIBUNWIND_UNAVAIL __attribute__ (( unavailable ))
#else
#define LIBUNWIND_UNAVAIL
#endif

typedef enum {
  _URC_NO_REASON = 0,
  _URC_OK = 0,
  _URC_FOREIGN_EXCEPTION_CAUGHT = 1,
  _URC_FATAL_PHASE2_ERROR = 2,
  _URC_FATAL_PHASE1_ERROR = 3,
  _URC_NORMAL_STOP = 4,
  _URC_END_OF_STACK = 5,
  _URC_HANDLER_FOUND = 6,
  _URC_INSTALL_CONTEXT = 7,
  _URC_CONTINUE_UNWIND = 8,
#if defined(_LIBUNWIND_ARM_EHABI)
  _URC_FAILURE = 9
#endif
} _Unwind_Reason_Code;

typedef enum {
  _UA_SEARCH_PHASE = 1,
  _UA_CLEANUP_PHASE = 2,
  _UA_HANDLER_FRAME = 4,
  _UA_FORCE_UNWIND = 8,
  _UA_END_OF_STACK = 16 // gcc extension to C++ ABI
} _Unwind_Action;

typedef struct _Unwind_Context _Unwind_Context;   // opaque

#if defined(_LIBUNWIND_ARM_EHABI)
typedef uint32_t _Unwind_State;

static const _Unwind_State _US_VIRTUAL_UNWIND_FRAME   = 0;
static const _Unwind_State _US_UNWIND_FRAME_STARTING  = 1;
static const _Unwind_State _US_UNWIND_FRAME_RESUME    = 2;
static const _Unwind_State _US_ACTION_MASK            = 3;
/* Undocumented flag for force unwinding. */
static const _Unwind_State _US_FORCE_UNWIND           = 8;

typedef uint32_t _Unwind_EHT_Header;

struct _Unwind_Control_Block;
typedef struct _Unwind_Control_Block _Unwind_Control_Block;
typedef struct _Unwind_Control_Block _Unwind_Exception; /* Alias */

struct _Unwind_Control_Block {
  uint64_t exception_class;
  void (*exception_cleanup)(_Unwind_Reason_Code, _Unwind_Control_Block*);

  /* Unwinder cache, private fields for the unwinder's use */
  struct {
    uint32_t reserved1; /* init reserved1 to 0, then don't touch */
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
  } unwinder_cache;

  /* Propagation barrier cache (valid after phase 1): */
  struct {
    uint32_t sp;
    uint32_t bitpattern[5];
  } barrier_cache;

  /* Cleanup cache (preserved over cleanup): */
  struct {
    uint32_t bitpattern[4];
  } cleanup_cache;

  /* Pr cache (for pr's benefit): */
  struct {
    uint32_t fnstart; /* function start address */
    _Unwind_EHT_Header* ehtp; /* pointer to EHT entry header word */
    uint32_t additional;
    uint32_t reserved1;
  } pr_cache;

  long long int :0; /* Enforce the 8-byte alignment */
} __attribute__((__aligned__(8)));

typedef _Unwind_Reason_Code (*_Unwind_Stop_Fn)
      (_Unwind_State state,
       _Unwind_Exception* exceptionObject,
       struct _Unwind_Context* context);

typedef _Unwind_Reason_Code (*__personality_routine)
      (_Unwind_State state,
       _Unwind_Exception* exceptionObject,
       struct _Unwind_Context* context);
#else
struct _Unwind_Context;   // opaque
struct _Unwind_Exception; // forward declaration
typedef struct _Unwind_Exception _Unwind_Exception;

struct _Unwind_Exception {
  uint64_t exception_class;
  void (*exception_cleanup)(_Unwind_Reason_Code reason,
                            _Unwind_Exception *exc);
#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__)
  uintptr_t private_[6];
#else
  uintptr_t private_1; // non-zero means forced unwind
  uintptr_t private_2; // holds sp that phase1 found for phase2 to use
#endif
#if __SIZEOF_POINTER__ == 4
  // The implementation of _Unwind_Exception uses an attribute mode on the
  // above fields which has the side effect of causing this whole struct to
  // round up to 32 bytes in size (48 with SEH). To be more explicit, we add
  // pad fields added for binary compatibility.
  uint32_t reserved[3];
#endif
  // The Itanium ABI requires that _Unwind_Exception objects are "double-word
  // aligned".  GCC has interpreted this to mean "use the maximum useful
  // alignment for the target"; so do we.
} __attribute__((__aligned__));

typedef _Unwind_Reason_Code (*_Unwind_Stop_Fn)
    (int version,
     _Unwind_Action actions,
     uint64_t exceptionClass,
     _Unwind_Exception* exceptionObject,
     struct _Unwind_Context* context,
     void* stop_parameter );

typedef _Unwind_Reason_Code (*__personality_routine)
      (int version,
       _Unwind_Action actions,
       uint64_t exceptionClass,
       _Unwind_Exception* exceptionObject,
       struct _Unwind_Context* context);
#endif

#ifdef __cplusplus
extern "C" {
#endif

//
// The following are the base functions documented by the C++ ABI
//
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_RaiseException(_Unwind_Exception *exception_object);
extern void _Unwind_SjLj_Resume(_Unwind_Exception *exception_object);
#else
extern _Unwind_Reason_Code
    _Unwind_RaiseException(_Unwind_Exception *exception_object);
extern void _Unwind_Resume(_Unwind_Exception *exception_object);
#endif
extern void _Unwind_DeleteException(_Unwind_Exception *exception_object);

#if defined(_LIBUNWIND_ARM_EHABI)
typedef enum {
  _UVRSC_CORE = 0, /* integer register */
  _UVRSC_VFP = 1, /* vfp */
  _UVRSC_WMMXD = 3, /* Intel WMMX data register */
  _UVRSC_WMMXC = 4 /* Intel WMMX control register */
} _Unwind_VRS_RegClass;

typedef enum {
  _UVRSD_UINT32 = 0,
  _UVRSD_VFPX = 1,
  _UVRSD_UINT64 = 3,
  _UVRSD_FLOAT = 4,
  _UVRSD_DOUBLE = 5
} _Unwind_VRS_DataRepresentation;

typedef enum {
  _UVRSR_OK = 0,
  _UVRSR_NOT_IMPLEMENTED = 1,
  _UVRSR_FAILED = 2
} _Unwind_VRS_Result;

extern void _Unwind_Complete(_Unwind_Exception* exception_object);

extern _Unwind_VRS_Result
_Unwind_VRS_Get(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep);

extern _Unwind_VRS_Result
_Unwind_VRS_Set(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep);

extern _Unwind_VRS_Result
_Unwind_VRS_Pop(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t discriminator,
                _Unwind_VRS_DataRepresentation representation);
#endif

#if !defined(_LIBUNWIND_ARM_EHABI)

extern uintptr_t _Unwind_GetGR(struct _Unwind_Context *context, int index);
extern void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                          uintptr_t new_value);
extern uintptr_t _Unwind_GetIP(struct _Unwind_Context *context);
extern void _Unwind_SetIP(struct _Unwind_Context *, uintptr_t new_value);

#else  // defined(_LIBUNWIND_ARM_EHABI)

#if defined(_LIBUNWIND_UNWIND_LEVEL1_EXTERNAL_LINKAGE)
#define _LIBUNWIND_EXPORT_UNWIND_LEVEL1 extern
#else
#define _LIBUNWIND_EXPORT_UNWIND_LEVEL1 static __inline__
#endif

// These are de facto helper functions for ARM, which delegate the function
// calls to _Unwind_VRS_Get/Set().  These are not a part of ARM EHABI
// specification, thus these function MUST be inlined.  Please don't replace
// these with the "extern" function declaration; otherwise, the program
// including this <unwind.h> header won't be ABI compatible and will result in
// link error when we are linking the program with libgcc.

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
uintptr_t _Unwind_GetGR(struct _Unwind_Context *context, int index) {
  uintptr_t value = 0;
  _Unwind_VRS_Get(context, _UVRSC_CORE, (uint32_t)index, _UVRSD_UINT32, &value);
  return value;
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                   uintptr_t value) {
  _Unwind_VRS_Set(context, _UVRSC_CORE, (uint32_t)index, _UVRSD_UINT32, &value);
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  // remove the thumb-bit before returning
  return _Unwind_GetGR(context, 15) & (~(uintptr_t)0x1);
}

_LIBUNWIND_EXPORT_UNWIND_LEVEL1
void _Unwind_SetIP(struct _Unwind_Context *context, uintptr_t value) {
  uintptr_t thumb_bit = _Unwind_GetGR(context, 15) & ((uintptr_t)0x1);
  _Unwind_SetGR(context, 15, value | thumb_bit);
}
#endif  // defined(_LIBUNWIND_ARM_EHABI)

extern uintptr_t _Unwind_GetRegionStart(struct _Unwind_Context *context);
extern uintptr_t
    _Unwind_GetLanguageSpecificData(struct _Unwind_Context *context);
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_ForcedUnwind(_Unwind_Exception *exception_object,
                              _Unwind_Stop_Fn stop, void *stop_parameter);
#else
extern _Unwind_Reason_Code
    _Unwind_ForcedUnwind(_Unwind_Exception *exception_object,
                         _Unwind_Stop_Fn stop, void *stop_parameter);
#endif

#ifdef __USING_SJLJ_EXCEPTIONS__
typedef struct _Unwind_FunctionContext *_Unwind_FunctionContext_t;
extern void _Unwind_SjLj_Register(_Unwind_FunctionContext_t fc);
extern void _Unwind_SjLj_Unregister(_Unwind_FunctionContext_t fc);
#endif

//
// The following are semi-suppoted extensions to the C++ ABI
//

//
//  called by __cxa_rethrow().
//
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_Resume_or_Rethrow(_Unwind_Exception *exception_object);
#else
extern _Unwind_Reason_Code
    _Unwind_Resume_or_Rethrow(_Unwind_Exception *exception_object);
#endif

// _Unwind_Backtrace() is a gcc extension that walks the stack and calls the
// _Unwind_Trace_Fn once per frame until it reaches the bottom of the stack
// or the _Unwind_Trace_Fn function returns something other than _URC_NO_REASON.
typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn)(struct _Unwind_Context *,
                                                void *);
extern _Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Trace_Fn, void *);

// _Unwind_GetCFA is a gcc extension that can be called from within a
// personality handler to get the CFA (stack pointer before call) of
// current frame.
extern uintptr_t _Unwind_GetCFA(struct _Unwind_Context *);


// _Unwind_GetIPInfo is a gcc extension that can be called from within a
// personality handler.  Similar to _Unwind_GetIP() but also returns in
// *ipBefore a non-zero value if the instruction pointer is at or before the
// instruction causing the unwind. Normally, in a function call, the IP returned
// is the return address which is after the call instruction and may be past the
// end of the function containing the call instruction.
extern uintptr_t _Unwind_GetIPInfo(struct _Unwind_Context *context,
                                   int *ipBefore);


// __register_frame() is used with dynamically generated code to register the
// FDE for a generated (JIT) code.  The FDE must use pc-rel addressing to point
// to its function and optional LSDA.
// __register_frame() has existed in all versions of Mac OS X, but in 10.4 and
// 10.5 it was buggy and did not actually register the FDE with the unwinder.
// In 10.6 and later it does register properly.
extern void __register_frame(const void *fde);
extern void __deregister_frame(const void *fde);

// _Unwind_Find_FDE() will locate the FDE if the pc is in some function that has
// an associated FDE. Note, Mac OS X 10.6 and later, introduces "compact unwind
// info" which the runtime uses in preference to DWARF unwind info.  This
// function will only work if the target function has an FDE but no compact
// unwind info.
struct dwarf_eh_bases {
  uintptr_t tbase;
  uintptr_t dbase;
  uintptr_t func;
};
extern const void *_Unwind_Find_FDE(const void *pc, struct dwarf_eh_bases *);


// This function attempts to find the start (address of first instruction) of
// a function given an address inside the function.  It only works if the
// function has an FDE (DWARF unwind info).
// This function is unimplemented on Mac OS X 10.6 and later.  Instead, use
// _Unwind_Find_FDE() and look at the dwarf_eh_bases.func result.
extern void *_Unwind_FindEnclosingFunction(void *pc);

// Mac OS X does not support text-rel and data-rel addressing so these functions
// are unimplemented
extern uintptr_t _Unwind_GetDataRelBase(struct _Unwind_Context *context)
    LIBUNWIND_UNAVAIL;
extern uintptr_t _Unwind_GetTextRelBase(struct _Unwind_Context *context)
    LIBUNWIND_UNAVAIL;

// Mac OS X 10.4 and 10.5 had implementations of these functions in
// libgcc_s.dylib, but they never worked.
/// These functions are no longer available on Mac OS X.
extern void __register_frame_info_bases(const void *fde, void *ob, void *tb,
                                        void *db) LIBUNWIND_UNAVAIL;
extern void __register_frame_info(const void *fde, void *ob)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_info_table_bases(const void *fde, void *ob,
                                              void *tb, void *db)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_info_table(const void *fde, void *ob)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_table(const void *fde)
    LIBUNWIND_UNAVAIL;
extern void *__deregister_frame_info(const void *fde)
    LIBUNWIND_UNAVAIL;
extern void *__deregister_frame_info_bases(const void *fde)
    LIBUNWIND_UNAVAIL;

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__)
#ifndef _WIN32
typedef struct _EXCEPTION_RECORD EXCEPTION_RECORD;
typedef struct _CONTEXT CONTEXT;
typedef struct _DISPATCHER_CONTEXT DISPATCHER_CONTEXT;
#elif !defined(__MINGW32__) && VER_PRODUCTBUILD < 8000
typedef struct _DISPATCHER_CONTEXT DISPATCHER_CONTEXT;
#endif
// This is the common wrapper for GCC-style personality functions with SEH.
extern EXCEPTION_DISPOSITION _GCC_specific_handler(EXCEPTION_RECORD *exc,
                                                   void *frame,
                                                   CONTEXT *ctx,
                                                   DISPATCHER_CONTEXT *disp,
                                                   __personality_routine pers);
#endif

#ifdef __cplusplus
}
#endif

#endif // __UNWIND_H__
