//===- llvm/CallingConv.h - LLVM Calling Conventions ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines LLVM's set of calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CALLINGCONV_H
#define LLVM_IR_CALLINGCONV_H

namespace llvm {

/// CallingConv Namespace - This namespace contains an enum with a value for
/// the well-known calling conventions.
///
namespace CallingConv {

  /// LLVM IR allows to use arbitrary numbers as calling convention identifiers.
  using ID = unsigned;

  /// A set of enums which specify the assigned numeric values for known llvm
  /// calling conventions.
  /// LLVM Calling Convention Representation
  enum {
    /// C - The default llvm calling convention, compatible with C.  This
    /// convention is the only calling convention that supports varargs calls.
    /// As with typical C calling conventions, the callee/caller have to
    /// tolerate certain amounts of prototype mismatch.
    C = 0,

    // Generic LLVM calling conventions.  None of these calling conventions
    // support varargs calls, and all assume that the caller and callee
    // prototype exactly match.

    /// Fast - This calling convention attempts to make calls as fast as
    /// possible (e.g. by passing things in registers).
    Fast = 8,

    // Cold - This calling convention attempts to make code in the caller as
    // efficient as possible under the assumption that the call is not commonly
    // executed.  As such, these calls often preserve all registers so that the
    // call does not break any live ranges in the caller side.
    Cold = 9,

    // GHC - Calling convention used by the Glasgow Haskell Compiler (GHC).
    GHC = 10,

    // HiPE - Calling convention used by the High-Performance Erlang Compiler
    // (HiPE).
    HiPE = 11,

    // WebKit JS - Calling convention for stack based JavaScript calls
    WebKit_JS = 12,

    // AnyReg - Calling convention for dynamic register based calls (e.g.
    // stackmap and patchpoint intrinsics).
    AnyReg = 13,

    // PreserveMost - Calling convention for runtime calls that preserves most
    // registers.
    PreserveMost = 14,

    // PreserveAll - Calling convention for runtime calls that preserves
    // (almost) all registers.
    PreserveAll = 15,

    // Swift - Calling convention for Swift.
    Swift = 16,

    // CXX_FAST_TLS - Calling convention for access functions.
    CXX_FAST_TLS = 17,

    // Target - This is the start of the target-specific calling conventions,
    // e.g. fastcall and thiscall on X86.
    FirstTargetCC = 64,

    /// X86_StdCall - stdcall is the calling conventions mostly used by the
    /// Win32 API. It is basically the same as the C convention with the
    /// difference in that the callee is responsible for popping the arguments
    /// from the stack.
    X86_StdCall = 64,

    /// X86_FastCall - 'fast' analog of X86_StdCall. Passes first two arguments
    /// in ECX:EDX registers, others - via stack. Callee is responsible for
    /// stack cleaning.
    X86_FastCall = 65,

    /// ARM_APCS - ARM Procedure Calling Standard calling convention (obsolete,
    /// but still used on some targets).
    ARM_APCS = 66,

    /// ARM_AAPCS - ARM Architecture Procedure Calling Standard calling
    /// convention (aka EABI). Soft float variant.
    ARM_AAPCS = 67,

    /// ARM_AAPCS_VFP - Same as ARM_AAPCS, but uses hard floating point ABI.
    ARM_AAPCS_VFP = 68,

    /// MSP430_INTR - Calling convention used for MSP430 interrupt routines.
    MSP430_INTR = 69,

    /// X86_ThisCall - Similar to X86_StdCall. Passes first argument in ECX,
    /// others via stack. Callee is responsible for stack cleaning. MSVC uses
    /// this by default for methods in its ABI.
    X86_ThisCall = 70,

    /// PTX_Kernel - Call to a PTX kernel.
    /// Passes all arguments in parameter space.
    PTX_Kernel = 71,

    /// PTX_Device - Call to a PTX device function.
    /// Passes all arguments in register or parameter space.
    PTX_Device = 72,

    /// SPIR_FUNC - Calling convention for SPIR non-kernel device functions.
    /// No lowering or expansion of arguments.
    /// Structures are passed as a pointer to a struct with the byval attribute.
    /// Functions can only call SPIR_FUNC and SPIR_KERNEL functions.
    /// Functions can only have zero or one return values.
    /// Variable arguments are not allowed, except for printf.
    /// How arguments/return values are lowered are not specified.
    /// Functions are only visible to the devices.
    SPIR_FUNC = 75,

    /// SPIR_KERNEL - Calling convention for SPIR kernel functions.
    /// Inherits the restrictions of SPIR_FUNC, except
    /// Cannot have non-void return values.
    /// Cannot have variable arguments.
    /// Can also be called by the host.
    /// Is externally visible.
    SPIR_KERNEL = 76,

    /// Intel_OCL_BI - Calling conventions for Intel OpenCL built-ins
    Intel_OCL_BI = 77,

    /// The C convention as specified in the x86-64 supplement to the
    /// System V ABI, used on most non-Windows systems.
    X86_64_SysV = 78,

    /// The C convention as implemented on Windows/x86-64 and
    /// AArch64. This convention differs from the more common
    /// \c X86_64_SysV convention in a number of ways, most notably in
    /// that XMM registers used to pass arguments are shadowed by GPRs,
    /// and vice versa.
    /// On AArch64, this is identical to the normal C (AAPCS) calling
    /// convention for normal functions, but floats are passed in integer
    /// registers to variadic functions.
    Win64 = 79,

    /// MSVC calling convention that passes vectors and vector aggregates
    /// in SSE registers.
    X86_VectorCall = 80,

    /// Calling convention used by HipHop Virtual Machine (HHVM) to
    /// perform calls to and from translation cache, and for calling PHP
    /// functions.
    /// HHVM calling convention supports tail/sibling call elimination.
    HHVM = 81,

    /// HHVM calling convention for invoking C/C++ helpers.
    HHVM_C = 82,

    /// X86_INTR - x86 hardware interrupt context. Callee may take one or two
    /// parameters, where the 1st represents a pointer to hardware context frame
    /// and the 2nd represents hardware error code, the presence of the later
    /// depends on the interrupt vector taken. Valid for both 32- and 64-bit
    /// subtargets.
    X86_INTR = 83,

    /// Used for AVR interrupt routines.
    AVR_INTR = 84,

    /// Calling convention used for AVR signal routines.
    AVR_SIGNAL = 85,

    /// Calling convention used for special AVR rtlib functions
    /// which have an "optimized" convention to preserve registers.
    AVR_BUILTIN = 86,

    /// Calling convention used for Mesa vertex shaders, or AMDPAL last shader
    /// stage before rasterization (vertex shader if tessellation and geometry
    /// are not in use, or otherwise copy shader if one is needed).
    AMDGPU_VS = 87,

    /// Calling convention used for Mesa/AMDPAL geometry shaders.
    AMDGPU_GS = 88,

    /// Calling convention used for Mesa/AMDPAL pixel shaders.
    AMDGPU_PS = 89,

    /// Calling convention used for Mesa/AMDPAL compute shaders.
    AMDGPU_CS = 90,

    /// Calling convention for AMDGPU code object kernels.
    AMDGPU_KERNEL = 91,

    /// Register calling convention used for parameters transfer optimization
    X86_RegCall = 92,

    /// Calling convention used for Mesa/AMDPAL hull shaders (= tessellation
    /// control shaders).
    AMDGPU_HS = 93,

    /// Calling convention used for special MSP430 rtlib functions
    /// which have an "optimized" convention using additional registers.
    MSP430_BUILTIN = 94,

    /// Calling convention used for AMDPAL vertex shader if tessellation is in
    /// use.
    AMDGPU_LS = 95,

    /// Calling convention used for AMDPAL shader stage before geometry shader
    /// if geometry is in use. So either the domain (= tessellation evaluation)
    /// shader if tessellation is in use, or otherwise the vertex shader.
    AMDGPU_ES = 96,

    // Calling convention between AArch64 Advanced SIMD functions
    AArch64_VectorCall = 97,

    /// The highest possible calling convention ID. Must be some 2^k - 1.
    MaxID = 1023
  };

} // end namespace CallingConv

} // end namespace llvm

#endif // LLVM_IR_CALLINGCONV_H
