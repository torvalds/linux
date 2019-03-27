//===-- ItaniumABILanguageRuntime.cpp --------------------------------------*-
//C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ItaniumABILanguageRuntime.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

static const char *vtable_demangled_prefix = "vtable for ";

bool ItaniumABILanguageRuntime::CouldHaveDynamicValue(ValueObject &in_value) {
  const bool check_cxx = true;
  const bool check_objc = false;
  return in_value.GetCompilerType().IsPossibleDynamicType(NULL, check_cxx,
                                                          check_objc);
}

TypeAndOrName ItaniumABILanguageRuntime::GetTypeInfoFromVTableAddress(
    ValueObject &in_value, lldb::addr_t original_ptr,
    lldb::addr_t vtable_load_addr) {
  if (m_process && vtable_load_addr != LLDB_INVALID_ADDRESS) {
    // Find the symbol that contains the "vtable_load_addr" address
    Address vtable_addr;
    Target &target = m_process->GetTarget();
    if (!target.GetSectionLoadList().IsEmpty()) {
      if (target.GetSectionLoadList().ResolveLoadAddress(vtable_load_addr,
                                                         vtable_addr)) {
        // See if we have cached info for this type already
        TypeAndOrName type_info = GetDynamicTypeInfo(vtable_addr);
        if (type_info)
          return type_info;

        SymbolContext sc;
        target.GetImages().ResolveSymbolContextForAddress(
            vtable_addr, eSymbolContextSymbol, sc);
        Symbol *symbol = sc.symbol;
        if (symbol != NULL) {
          const char *name =
              symbol->GetMangled()
                  .GetDemangledName(lldb::eLanguageTypeC_plus_plus)
                  .AsCString();
          if (name && strstr(name, vtable_demangled_prefix) == name) {
            Log *log(
                lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));
            if (log)
              log->Printf("0x%16.16" PRIx64
                          ": static-type = '%s' has vtable symbol '%s'\n",
                          original_ptr, in_value.GetTypeName().GetCString(),
                          name);
            // We are a C++ class, that's good.  Get the class name and look it
            // up:
            const char *class_name = name + strlen(vtable_demangled_prefix);
            // We know the class name is absolute, so tell FindTypes that by
            // prefixing it with the root namespace:
            std::string lookup_name("::");
            lookup_name.append(class_name);
            
            type_info.SetName(class_name);
            const bool exact_match = true;
            TypeList class_types;

            uint32_t num_matches = 0;
            // First look in the module that the vtable symbol came from and
            // look for a single exact match.
            llvm::DenseSet<SymbolFile *> searched_symbol_files;
            if (sc.module_sp) {
              num_matches = sc.module_sp->FindTypes(
                  ConstString(lookup_name), exact_match, 1,
                  searched_symbol_files, class_types);
            }

            // If we didn't find a symbol, then move on to the entire module
            // list in the target and get as many unique matches as possible
            if (num_matches == 0) {
              num_matches = target.GetImages().FindTypes(
                  nullptr, ConstString(lookup_name), exact_match, UINT32_MAX,
                  searched_symbol_files, class_types);
            }

            lldb::TypeSP type_sp;
            if (num_matches == 0) {
              if (log)
                log->Printf("0x%16.16" PRIx64 ": is not dynamic\n",
                            original_ptr);
              return TypeAndOrName();
            }
            if (num_matches == 1) {
              type_sp = class_types.GetTypeAtIndex(0);
              if (type_sp) {
                if (ClangASTContext::IsCXXClassType(
                        type_sp->GetForwardCompilerType())) {
                  if (log)
                    log->Printf(
                        "0x%16.16" PRIx64
                        ": static-type = '%s' has dynamic type: uid={0x%" PRIx64
                        "}, type-name='%s'\n",
                        original_ptr, in_value.GetTypeName().AsCString(),
                        type_sp->GetID(), type_sp->GetName().GetCString());
                  type_info.SetTypeSP(type_sp);
                }
              }
            } else if (num_matches > 1) {
              size_t i;
              if (log) {
                for (i = 0; i < num_matches; i++) {
                  type_sp = class_types.GetTypeAtIndex(i);
                  if (type_sp) {
                    if (log)
                      log->Printf(
                          "0x%16.16" PRIx64
                          ": static-type = '%s' has multiple matching dynamic "
                          "types: uid={0x%" PRIx64 "}, type-name='%s'\n",
                          original_ptr, in_value.GetTypeName().AsCString(),
                          type_sp->GetID(), type_sp->GetName().GetCString());
                  }
                }
              }

              for (i = 0; i < num_matches; i++) {
                type_sp = class_types.GetTypeAtIndex(i);
                if (type_sp) {
                  if (ClangASTContext::IsCXXClassType(
                          type_sp->GetForwardCompilerType())) {
                    if (log)
                      log->Printf(
                          "0x%16.16" PRIx64 ": static-type = '%s' has multiple "
                                            "matching dynamic types, picking "
                                            "this one: uid={0x%" PRIx64
                          "}, type-name='%s'\n",
                          original_ptr, in_value.GetTypeName().AsCString(),
                          type_sp->GetID(), type_sp->GetName().GetCString());
                    type_info.SetTypeSP(type_sp);
                  }
                }
              }

              if (log && i == num_matches) {
                log->Printf(
                    "0x%16.16" PRIx64
                    ": static-type = '%s' has multiple matching dynamic "
                    "types, didn't find a C++ match\n",
                    original_ptr, in_value.GetTypeName().AsCString());
              }
            }
            if (type_info)
              SetDynamicTypeInfo(vtable_addr, type_info);
            return type_info;
          }
        }
      }
    }
  }
  return TypeAndOrName();
}

bool ItaniumABILanguageRuntime::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &dynamic_address,
    Value::ValueType &value_type) {
  // For Itanium, if the type has a vtable pointer in the object, it will be at
  // offset 0 in the object.  That will point to the "address point" within the
  // vtable (not the beginning of the vtable.)  We can then look up the symbol
  // containing this "address point" and that symbol's name demangled will
  // contain the full class name. The second pointer above the "address point"
  // is the "offset_to_top".  We'll use that to get the start of the value
  // object which holds the dynamic type.
  //

  class_type_or_name.Clear();
  value_type = Value::ValueType::eValueTypeScalar;

  // Only a pointer or reference type can have a different dynamic and static
  // type:
  if (!CouldHaveDynamicValue(in_value))
    return false;

  // First job, pull out the address at 0 offset from the object.
  AddressType address_type;
  lldb::addr_t original_ptr = in_value.GetPointerValue(&address_type);
  if (original_ptr == LLDB_INVALID_ADDRESS)
    return false;

  ExecutionContext exe_ctx(in_value.GetExecutionContextRef());

  Process *process = exe_ctx.GetProcessPtr();

  if (process == nullptr)
    return false;

  Status error;
  const lldb::addr_t vtable_address_point =
      process->ReadPointerFromMemory(original_ptr, error);

  if (!error.Success() || vtable_address_point == LLDB_INVALID_ADDRESS)
    return false;

  class_type_or_name = GetTypeInfoFromVTableAddress(in_value, original_ptr,
                                                    vtable_address_point);

  if (!class_type_or_name)
    return false;

  TypeSP type_sp = class_type_or_name.GetTypeSP();
  // There can only be one type with a given name, so we've just found
  // duplicate definitions, and this one will do as well as any other. We
  // don't consider something to have a dynamic type if it is the same as
  // the static type.  So compare against the value we were handed.
  if (!type_sp)
    return true;

  if (ClangASTContext::AreTypesSame(in_value.GetCompilerType(),
                                    type_sp->GetForwardCompilerType())) {
    // The dynamic type we found was the same type, so we don't have a
    // dynamic type here...
    return false;
  }

  // The offset_to_top is two pointers above the vtable pointer.
  const uint32_t addr_byte_size = process->GetAddressByteSize();
  const lldb::addr_t offset_to_top_location =
      vtable_address_point - 2 * addr_byte_size;
  // Watch for underflow, offset_to_top_location should be less than
  // vtable_address_point
  if (offset_to_top_location >= vtable_address_point)
    return false;
  const int64_t offset_to_top = process->ReadSignedIntegerFromMemory(
      offset_to_top_location, addr_byte_size, INT64_MIN, error);

  if (offset_to_top == INT64_MIN)
    return false;
  // So the dynamic type is a value that starts at offset_to_top above
  // the original address.
  lldb::addr_t dynamic_addr = original_ptr + offset_to_top;
  if (!process->GetTarget().GetSectionLoadList().ResolveLoadAddress(
          dynamic_addr, dynamic_address)) {
    dynamic_address.SetRawAddress(dynamic_addr);
  }
  return true;
}

TypeAndOrName ItaniumABILanguageRuntime::FixUpDynamicType(
    const TypeAndOrName &type_and_or_name, ValueObject &static_value) {
  CompilerType static_type(static_value.GetCompilerType());
  Flags static_type_flags(static_type.GetTypeInfo());

  TypeAndOrName ret(type_and_or_name);
  if (type_and_or_name.HasType()) {
    // The type will always be the type of the dynamic object.  If our parent's
    // type was a pointer, then our type should be a pointer to the type of the
    // dynamic object.  If a reference, then the original type should be
    // okay...
    CompilerType orig_type = type_and_or_name.GetCompilerType();
    CompilerType corrected_type = orig_type;
    if (static_type_flags.AllSet(eTypeIsPointer))
      corrected_type = orig_type.GetPointerType();
    else if (static_type_flags.AllSet(eTypeIsReference))
      corrected_type = orig_type.GetLValueReferenceType();
    ret.SetCompilerType(corrected_type);
  } else {
    // If we are here we need to adjust our dynamic type name to include the
    // correct & or * symbol
    std::string corrected_name(type_and_or_name.GetName().GetCString());
    if (static_type_flags.AllSet(eTypeIsPointer))
      corrected_name.append(" *");
    else if (static_type_flags.AllSet(eTypeIsReference))
      corrected_name.append(" &");
    // the parent type should be a correctly pointer'ed or referenc'ed type
    ret.SetCompilerType(static_type);
    ret.SetName(corrected_name.c_str());
  }
  return ret;
}

bool ItaniumABILanguageRuntime::IsVTableName(const char *name) {
  if (name == NULL)
    return false;

  // Can we maybe ask Clang about this?
  return strstr(name, "_vptr$") == name;
}

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------
LanguageRuntime *
ItaniumABILanguageRuntime::CreateInstance(Process *process,
                                          lldb::LanguageType language) {
  // FIXME: We have to check the process and make sure we actually know that
  // this process supports
  // the Itanium ABI.
  if (language == eLanguageTypeC_plus_plus ||
      language == eLanguageTypeC_plus_plus_03 ||
      language == eLanguageTypeC_plus_plus_11 ||
      language == eLanguageTypeC_plus_plus_14)
    return new ItaniumABILanguageRuntime(process);
  else
    return NULL;
}

class CommandObjectMultiwordItaniumABI_Demangle : public CommandObjectParsed {
public:
  CommandObjectMultiwordItaniumABI_Demangle(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "demangle",
                            "Demangle a C++ mangled name.",
                            "language cplusplus demangle") {
    CommandArgumentEntry arg;
    CommandArgumentData index_arg;

    // Define the first (and only) variant of this arg.
    index_arg.arg_type = eArgTypeSymbol;
    index_arg.arg_repetition = eArgRepeatPlus;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(index_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectMultiwordItaniumABI_Demangle() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    bool demangled_any = false;
    bool error_any = false;
    for (auto &entry : command.entries()) {
      if (entry.ref.empty())
        continue;

      // the actual Mangled class should be strict about this, but on the
      // command line if you're copying mangled names out of 'nm' on Darwin,
      // they will come out with an extra underscore - be willing to strip this
      // on behalf of the user.   This is the moral equivalent of the -_/-n
      // options to c++filt
      auto name = entry.ref;
      if (name.startswith("__Z"))
        name = name.drop_front();

      Mangled mangled(name, true);
      if (mangled.GuessLanguage() == lldb::eLanguageTypeC_plus_plus) {
        ConstString demangled(
            mangled.GetDisplayDemangledName(lldb::eLanguageTypeC_plus_plus));
        demangled_any = true;
        result.AppendMessageWithFormat("%s ---> %s\n", entry.ref.str().c_str(),
                                       demangled.GetCString());
      } else {
        error_any = true;
        result.AppendErrorWithFormat("%s is not a valid C++ mangled name\n",
                                     entry.ref.str().c_str());
      }
    }

    result.SetStatus(
        error_any ? lldb::eReturnStatusFailed
                  : (demangled_any ? lldb::eReturnStatusSuccessFinishResult
                                   : lldb::eReturnStatusSuccessFinishNoResult));
    return result.Succeeded();
  }
};

class CommandObjectMultiwordItaniumABI : public CommandObjectMultiword {
public:
  CommandObjectMultiwordItaniumABI(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "cplusplus",
            "Commands for operating on the C++ language runtime.",
            "cplusplus <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "demangle",
        CommandObjectSP(
            new CommandObjectMultiwordItaniumABI_Demangle(interpreter)));
  }

  ~CommandObjectMultiwordItaniumABI() override = default;
};

void ItaniumABILanguageRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Itanium ABI for the C++ language", CreateInstance,
      [](CommandInterpreter &interpreter) -> lldb::CommandObjectSP {
        return CommandObjectSP(
            new CommandObjectMultiwordItaniumABI(interpreter));
      });
}

void ItaniumABILanguageRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ItaniumABILanguageRuntime::GetPluginNameStatic() {
  static ConstString g_name("itanium");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString ItaniumABILanguageRuntime::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ItaniumABILanguageRuntime::GetPluginVersion() { return 1; }

BreakpointResolverSP ItaniumABILanguageRuntime::CreateExceptionResolver(
    Breakpoint *bkpt, bool catch_bp, bool throw_bp) {
  return CreateExceptionResolver(bkpt, catch_bp, throw_bp, false);
}

BreakpointResolverSP ItaniumABILanguageRuntime::CreateExceptionResolver(
    Breakpoint *bkpt, bool catch_bp, bool throw_bp, bool for_expressions) {
  // One complication here is that most users DON'T want to stop at
  // __cxa_allocate_expression, but until we can do anything better with
  // predicting unwinding the expression parser does.  So we have two forms of
  // the exception breakpoints, one for expressions that leaves out
  // __cxa_allocate_exception, and one that includes it. The
  // SetExceptionBreakpoints does the latter, the CreateExceptionBreakpoint in
  // the runtime the former.
  static const char *g_catch_name = "__cxa_begin_catch";
  static const char *g_throw_name1 = "__cxa_throw";
  static const char *g_throw_name2 = "__cxa_rethrow";
  static const char *g_exception_throw_name = "__cxa_allocate_exception";
  std::vector<const char *> exception_names;
  exception_names.reserve(4);
  if (catch_bp)
    exception_names.push_back(g_catch_name);

  if (throw_bp) {
    exception_names.push_back(g_throw_name1);
    exception_names.push_back(g_throw_name2);
  }

  if (for_expressions)
    exception_names.push_back(g_exception_throw_name);

  BreakpointResolverSP resolver_sp(new BreakpointResolverName(
      bkpt, exception_names.data(), exception_names.size(),
      eFunctionNameTypeBase, eLanguageTypeUnknown, 0, eLazyBoolNo));

  return resolver_sp;
}

lldb::SearchFilterSP ItaniumABILanguageRuntime::CreateExceptionSearchFilter() {
  Target &target = m_process->GetTarget();

  if (target.GetArchitecture().GetTriple().getVendor() == llvm::Triple::Apple) {
    // Limit the number of modules that are searched for these breakpoints for
    // Apple binaries.
    FileSpecList filter_modules;
    filter_modules.Append(FileSpec("libc++abi.dylib"));
    filter_modules.Append(FileSpec("libSystem.B.dylib"));
    return target.GetSearchFilterForModuleList(&filter_modules);
  } else {
    return LanguageRuntime::CreateExceptionSearchFilter();
  }
}

lldb::BreakpointSP ItaniumABILanguageRuntime::CreateExceptionBreakpoint(
    bool catch_bp, bool throw_bp, bool for_expressions, bool is_internal) {
  Target &target = m_process->GetTarget();
  FileSpecList filter_modules;
  BreakpointResolverSP exception_resolver_sp =
      CreateExceptionResolver(NULL, catch_bp, throw_bp, for_expressions);
  SearchFilterSP filter_sp(CreateExceptionSearchFilter());
  const bool hardware = false;
  const bool resolve_indirect_functions = false;
  return target.CreateBreakpoint(filter_sp, exception_resolver_sp, is_internal,
                                 hardware, resolve_indirect_functions);
}

void ItaniumABILanguageRuntime::SetExceptionBreakpoints() {
  if (!m_process)
    return;

  const bool catch_bp = false;
  const bool throw_bp = true;
  const bool is_internal = true;
  const bool for_expressions = true;

  // For the exception breakpoints set by the Expression parser, we'll be a
  // little more aggressive and stop at exception allocation as well.

  if (m_cxx_exception_bp_sp) {
    m_cxx_exception_bp_sp->SetEnabled(true);
  } else {
    m_cxx_exception_bp_sp = CreateExceptionBreakpoint(
        catch_bp, throw_bp, for_expressions, is_internal);
    if (m_cxx_exception_bp_sp)
      m_cxx_exception_bp_sp->SetBreakpointKind("c++ exception");
  }
}

void ItaniumABILanguageRuntime::ClearExceptionBreakpoints() {
  if (!m_process)
    return;

  if (m_cxx_exception_bp_sp) {
    m_cxx_exception_bp_sp->SetEnabled(false);
  }
}

bool ItaniumABILanguageRuntime::ExceptionBreakpointsAreSet() {
  return m_cxx_exception_bp_sp && m_cxx_exception_bp_sp->IsEnabled();
}

bool ItaniumABILanguageRuntime::ExceptionBreakpointsExplainStop(
    lldb::StopInfoSP stop_reason) {
  if (!m_process)
    return false;

  if (!stop_reason || stop_reason->GetStopReason() != eStopReasonBreakpoint)
    return false;

  uint64_t break_site_id = stop_reason->GetValue();
  return m_process->GetBreakpointSiteList().BreakpointSiteContainsBreakpoint(
      break_site_id, m_cxx_exception_bp_sp->GetID());
}

ValueObjectSP ItaniumABILanguageRuntime::GetExceptionObjectForThread(
    ThreadSP thread_sp) {
  if (!thread_sp->SafeToCallFunctions())
    return {};

  ClangASTContext *clang_ast_context =
      m_process->GetTarget().GetScratchClangASTContext();
  CompilerType voidstar =
      clang_ast_context->GetBasicType(eBasicTypeVoid).GetPointerType();

  DiagnosticManager diagnostics;
  ExecutionContext exe_ctx;
  EvaluateExpressionOptions options;

  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetStopOthers(true);
  options.SetTimeout(std::chrono::milliseconds(500));
  options.SetTryAllThreads(false);
  thread_sp->CalculateExecutionContext(exe_ctx);

  const ModuleList &modules = m_process->GetTarget().GetImages();
  SymbolContextList contexts;
  SymbolContext context;

  modules.FindSymbolsWithNameAndType(
      ConstString("__cxa_current_exception_type"), eSymbolTypeCode, contexts);
  contexts.GetContextAtIndex(0, context);
  Address addr = context.symbol->GetAddress();

  Status error;
  FunctionCaller *function_caller =
      m_process->GetTarget().GetFunctionCallerForLanguage(
          eLanguageTypeC, voidstar, addr, ValueList(), "caller", error);

  ExpressionResults func_call_ret;
  Value results;
  func_call_ret = function_caller->ExecuteFunction(exe_ctx, nullptr, options,
                                                   diagnostics, results);
  if (func_call_ret != eExpressionCompleted || !error.Success()) {
    return ValueObjectSP();
  }

  size_t ptr_size = m_process->GetAddressByteSize();
  addr_t result_ptr = results.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
  addr_t exception_addr =
      m_process->ReadPointerFromMemory(result_ptr - ptr_size, error);

  lldb_private::formatters::InferiorSizedWord exception_isw(exception_addr,
                                                            *m_process);
  ValueObjectSP exception = ValueObject::CreateValueObjectFromData(
      "exception", exception_isw.GetAsData(m_process->GetByteOrder()), exe_ctx,
      voidstar);
  exception = exception->GetDynamicValue(eDynamicDontRunTarget);

  return exception;
}

TypeAndOrName ItaniumABILanguageRuntime::GetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr) {
  std::lock_guard<std::mutex> locker(m_dynamic_type_map_mutex);
  DynamicTypeCache::const_iterator pos = m_dynamic_type_map.find(vtable_addr);
  if (pos == m_dynamic_type_map.end())
    return TypeAndOrName();
  else
    return pos->second;
}

void ItaniumABILanguageRuntime::SetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr, const TypeAndOrName &type_info) {
  std::lock_guard<std::mutex> locker(m_dynamic_type_map_mutex);
  m_dynamic_type_map[vtable_addr] = type_info;
}
