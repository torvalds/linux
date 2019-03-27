//===-- InferiorCallPOSIX.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InferiorCallPOSIX.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Host/Config.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanCallFunction.h"

#ifndef LLDB_DISABLE_POSIX
#include <sys/mman.h>
#else
// define them
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#endif

using namespace lldb;
using namespace lldb_private;

bool lldb_private::InferiorCallMmap(Process *process, addr_t &allocated_addr,
                                    addr_t addr, addr_t length, unsigned prot,
                                    unsigned flags, addr_t fd, addr_t offset) {
  Thread *thread =
      process->GetThreadList().GetExpressionExecutionThread().get();
  if (thread == NULL)
    return false;

  const bool append = true;
  const bool include_symbols = true;
  const bool include_inlines = false;
  SymbolContextList sc_list;
  const uint32_t count = process->GetTarget().GetImages().FindFunctions(
      ConstString("mmap"), eFunctionNameTypeFull, include_symbols,
      include_inlines, append, sc_list);
  if (count > 0) {
    SymbolContext sc;
    if (sc_list.GetContextAtIndex(0, sc)) {
      const uint32_t range_scope =
          eSymbolContextFunction | eSymbolContextSymbol;
      const bool use_inline_block_range = false;
      EvaluateExpressionOptions options;
      options.SetStopOthers(true);
      options.SetUnwindOnError(true);
      options.SetIgnoreBreakpoints(true);
      options.SetTryAllThreads(true);
      options.SetDebug(false);
      options.SetTimeout(std::chrono::milliseconds(500));
      options.SetTrapExceptions(false);

      addr_t prot_arg;
      if (prot == eMmapProtNone)
        prot_arg = PROT_NONE;
      else {
        prot_arg = 0;
        if (prot & eMmapProtExec)
          prot_arg |= PROT_EXEC;
        if (prot & eMmapProtRead)
          prot_arg |= PROT_READ;
        if (prot & eMmapProtWrite)
          prot_arg |= PROT_WRITE;
      }

      AddressRange mmap_range;
      if (sc.GetAddressRange(range_scope, 0, use_inline_block_range,
                             mmap_range)) {
        ClangASTContext *clang_ast_context =
            process->GetTarget().GetScratchClangASTContext();
        CompilerType clang_void_ptr_type =
            clang_ast_context->GetBasicType(eBasicTypeVoid).GetPointerType();
        const ArchSpec arch = process->GetTarget().GetArchitecture();
        MmapArgList args =
            process->GetTarget().GetPlatform()->GetMmapArgumentList(
                arch, addr, length, prot_arg, flags, fd, offset);
        lldb::ThreadPlanSP call_plan_sp(
            new ThreadPlanCallFunction(*thread, mmap_range.GetBaseAddress(),
                                       clang_void_ptr_type, args, options));
        if (call_plan_sp) {
          DiagnosticManager diagnostics;

          StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
          if (frame) {
            ExecutionContext exe_ctx;
            frame->CalculateExecutionContext(exe_ctx);
            ExpressionResults result = process->RunThreadPlan(
                exe_ctx, call_plan_sp, options, diagnostics);
            if (result == eExpressionCompleted) {

              allocated_addr =
                  call_plan_sp->GetReturnValueObject()->GetValueAsUnsigned(
                      LLDB_INVALID_ADDRESS);
              if (process->GetAddressByteSize() == 4) {
                if (allocated_addr == UINT32_MAX)
                  return false;
              } else if (process->GetAddressByteSize() == 8) {
                if (allocated_addr == UINT64_MAX)
                  return false;
              }
              return true;
            }
          }
        }
      }
    }
  }

  return false;
}

bool lldb_private::InferiorCallMunmap(Process *process, addr_t addr,
                                      addr_t length) {
  Thread *thread =
      process->GetThreadList().GetExpressionExecutionThread().get();
  if (thread == NULL)
    return false;

  const bool append = true;
  const bool include_symbols = true;
  const bool include_inlines = false;
  SymbolContextList sc_list;
  const uint32_t count = process->GetTarget().GetImages().FindFunctions(
      ConstString("munmap"), eFunctionNameTypeFull, include_symbols,
      include_inlines, append, sc_list);
  if (count > 0) {
    SymbolContext sc;
    if (sc_list.GetContextAtIndex(0, sc)) {
      const uint32_t range_scope =
          eSymbolContextFunction | eSymbolContextSymbol;
      const bool use_inline_block_range = false;
      EvaluateExpressionOptions options;
      options.SetStopOthers(true);
      options.SetUnwindOnError(true);
      options.SetIgnoreBreakpoints(true);
      options.SetTryAllThreads(true);
      options.SetDebug(false);
      options.SetTimeout(std::chrono::milliseconds(500));
      options.SetTrapExceptions(false);

      AddressRange munmap_range;
      if (sc.GetAddressRange(range_scope, 0, use_inline_block_range,
                             munmap_range)) {
        lldb::addr_t args[] = {addr, length};
        lldb::ThreadPlanSP call_plan_sp(
            new ThreadPlanCallFunction(*thread, munmap_range.GetBaseAddress(),
                                       CompilerType(), args, options));
        if (call_plan_sp) {
          DiagnosticManager diagnostics;

          StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
          if (frame) {
            ExecutionContext exe_ctx;
            frame->CalculateExecutionContext(exe_ctx);
            ExpressionResults result = process->RunThreadPlan(
                exe_ctx, call_plan_sp, options, diagnostics);
            if (result == eExpressionCompleted) {
              return true;
            }
          }
        }
      }
    }
  }

  return false;
}

// FIXME: This has nothing to do with Posix, it is just a convenience function
// that calls a
// function of the form "void * (*)(void)".  We should find a better place to
// put this.

bool lldb_private::InferiorCall(Process *process, const Address *address,
                                addr_t &returned_func, bool trap_exceptions) {
  Thread *thread =
      process->GetThreadList().GetExpressionExecutionThread().get();
  if (thread == NULL || address == NULL)
    return false;

  EvaluateExpressionOptions options;
  options.SetStopOthers(true);
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTryAllThreads(true);
  options.SetDebug(false);
  options.SetTimeout(std::chrono::milliseconds(500));
  options.SetTrapExceptions(trap_exceptions);

  ClangASTContext *clang_ast_context =
      process->GetTarget().GetScratchClangASTContext();
  CompilerType clang_void_ptr_type =
      clang_ast_context->GetBasicType(eBasicTypeVoid).GetPointerType();
  lldb::ThreadPlanSP call_plan_sp(
      new ThreadPlanCallFunction(*thread, *address, clang_void_ptr_type,
                                 llvm::ArrayRef<addr_t>(), options));
  if (call_plan_sp) {
    DiagnosticManager diagnostics;

    StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
    if (frame) {
      ExecutionContext exe_ctx;
      frame->CalculateExecutionContext(exe_ctx);
      ExpressionResults result =
          process->RunThreadPlan(exe_ctx, call_plan_sp, options, diagnostics);
      if (result == eExpressionCompleted) {
        returned_func =
            call_plan_sp->GetReturnValueObject()->GetValueAsUnsigned(
                LLDB_INVALID_ADDRESS);

        if (process->GetAddressByteSize() == 4) {
          if (returned_func == UINT32_MAX)
            return false;
        } else if (process->GetAddressByteSize() == 8) {
          if (returned_func == UINT64_MAX)
            return false;
        }
        return true;
      }
    }
  }

  return false;
}
