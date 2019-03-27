//===-- LanguageRuntime.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/LanguageRuntime.h"
#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

ExceptionSearchFilter::ExceptionSearchFilter(const lldb::TargetSP &target_sp,
                                             lldb::LanguageType language,
                                             bool update_module_list)
    : SearchFilter(target_sp, FilterTy::Exception), m_language(language),
      m_language_runtime(nullptr), m_filter_sp() {
  if (update_module_list)
    UpdateModuleListIfNeeded();
}

bool ExceptionSearchFilter::ModulePasses(const lldb::ModuleSP &module_sp) {
  UpdateModuleListIfNeeded();
  if (m_filter_sp)
    return m_filter_sp->ModulePasses(module_sp);
  return false;
}

bool ExceptionSearchFilter::ModulePasses(const FileSpec &spec) {
  UpdateModuleListIfNeeded();
  if (m_filter_sp)
    return m_filter_sp->ModulePasses(spec);
  return false;
}

void ExceptionSearchFilter::Search(Searcher &searcher) {
  UpdateModuleListIfNeeded();
  if (m_filter_sp)
    m_filter_sp->Search(searcher);
}

void ExceptionSearchFilter::GetDescription(Stream *s) {
  UpdateModuleListIfNeeded();
  if (m_filter_sp)
    m_filter_sp->GetDescription(s);
}

void ExceptionSearchFilter::UpdateModuleListIfNeeded() {
  ProcessSP process_sp(m_target_sp->GetProcessSP());
  if (process_sp) {
    bool refreash_filter = !m_filter_sp;
    if (m_language_runtime == nullptr) {
      m_language_runtime = process_sp->GetLanguageRuntime(m_language);
      refreash_filter = true;
    } else {
      LanguageRuntime *language_runtime =
          process_sp->GetLanguageRuntime(m_language);
      if (m_language_runtime != language_runtime) {
        m_language_runtime = language_runtime;
        refreash_filter = true;
      }
    }

    if (refreash_filter && m_language_runtime) {
      m_filter_sp = m_language_runtime->CreateExceptionSearchFilter();
    }
  } else {
    m_filter_sp.reset();
    m_language_runtime = nullptr;
  }
}

SearchFilterSP
ExceptionSearchFilter::DoCopyForBreakpoint(Breakpoint &breakpoint) {
  return SearchFilterSP(
      new ExceptionSearchFilter(TargetSP(), m_language, false));
}

SearchFilter *ExceptionSearchFilter::CreateFromStructuredData(
    Target &target, const StructuredData::Dictionary &data_dict,
    Status &error) {
  SearchFilter *result = nullptr;
  return result;
}

StructuredData::ObjectSP ExceptionSearchFilter::SerializeToStructuredData() {
  StructuredData::ObjectSP result_sp;

  return result_sp;
}

// The Target is the one that knows how to create breakpoints, so this function
// is meant to be used either by the target or internally in
// Set/ClearExceptionBreakpoints.
class ExceptionBreakpointResolver : public BreakpointResolver {
public:
  ExceptionBreakpointResolver(lldb::LanguageType language, bool catch_bp,
                              bool throw_bp)
      : BreakpointResolver(nullptr, BreakpointResolver::ExceptionResolver),
        m_language(language), m_language_runtime(nullptr), m_catch_bp(catch_bp),
        m_throw_bp(throw_bp) {}

  ~ExceptionBreakpointResolver() override = default;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override {

    if (SetActualResolver())
      return m_actual_resolver_sp->SearchCallback(filter, context, addr,
                                                  containing);
    else
      return eCallbackReturnStop;
  }

  lldb::SearchDepth GetDepth() override {
    if (SetActualResolver())
      return m_actual_resolver_sp->GetDepth();
    else
      return lldb::eSearchDepthTarget;
  }

  void GetDescription(Stream *s) override {
    Language *language_plugin = Language::FindPlugin(m_language);
    if (language_plugin)
      language_plugin->GetExceptionResolverDescription(m_catch_bp, m_throw_bp,
                                                       *s);
    else
      Language::GetDefaultExceptionResolverDescription(m_catch_bp, m_throw_bp,
                                                       *s);

    SetActualResolver();
    if (m_actual_resolver_sp) {
      s->Printf(" using: ");
      m_actual_resolver_sp->GetDescription(s);
    } else
      s->Printf(" the correct runtime exception handler will be determined "
                "when you run");
  }

  void Dump(Stream *s) const override {}

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverName *) { return true; }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::ExceptionResolver;
  }

protected:
  BreakpointResolverSP CopyForBreakpoint(Breakpoint &breakpoint) override {
    return BreakpointResolverSP(
        new ExceptionBreakpointResolver(m_language, m_catch_bp, m_throw_bp));
  }

  bool SetActualResolver() {
    ProcessSP process_sp;
    if (m_breakpoint) {
      process_sp = m_breakpoint->GetTarget().GetProcessSP();
      if (process_sp) {
        bool refreash_resolver = !m_actual_resolver_sp;
        if (m_language_runtime == nullptr) {
          m_language_runtime = process_sp->GetLanguageRuntime(m_language);
          refreash_resolver = true;
        } else {
          LanguageRuntime *language_runtime =
              process_sp->GetLanguageRuntime(m_language);
          if (m_language_runtime != language_runtime) {
            m_language_runtime = language_runtime;
            refreash_resolver = true;
          }
        }

        if (refreash_resolver && m_language_runtime) {
          m_actual_resolver_sp = m_language_runtime->CreateExceptionResolver(
              m_breakpoint, m_catch_bp, m_throw_bp);
        }
      } else {
        m_actual_resolver_sp.reset();
        m_language_runtime = nullptr;
      }
    } else {
      m_actual_resolver_sp.reset();
      m_language_runtime = nullptr;
    }
    return (bool)m_actual_resolver_sp;
  }

  lldb::BreakpointResolverSP m_actual_resolver_sp;
  lldb::LanguageType m_language;
  LanguageRuntime *m_language_runtime;
  bool m_catch_bp;
  bool m_throw_bp;
};

LanguageRuntime *LanguageRuntime::FindPlugin(Process *process,
                                             lldb::LanguageType language) {
  std::unique_ptr<LanguageRuntime> language_runtime_ap;
  LanguageRuntimeCreateInstance create_callback;

  for (uint32_t idx = 0;
       (create_callback =
            PluginManager::GetLanguageRuntimeCreateCallbackAtIndex(idx)) !=
       nullptr;
       ++idx) {
    language_runtime_ap.reset(create_callback(process, language));

    if (language_runtime_ap)
      return language_runtime_ap.release();
  }

  return nullptr;
}

LanguageRuntime::LanguageRuntime(Process *process) : m_process(process) {}

LanguageRuntime::~LanguageRuntime() = default;

Breakpoint::BreakpointPreconditionSP
LanguageRuntime::CreateExceptionPrecondition(lldb::LanguageType language,
                                             bool catch_bp, bool throw_bp) {
  switch (language) {
  case eLanguageTypeObjC:
    if (throw_bp)
      return Breakpoint::BreakpointPreconditionSP(
          new ObjCLanguageRuntime::ObjCExceptionPrecondition());
    break;
  default:
    break;
  }
  return Breakpoint::BreakpointPreconditionSP();
}

BreakpointSP LanguageRuntime::CreateExceptionBreakpoint(
    Target &target, lldb::LanguageType language, bool catch_bp, bool throw_bp,
    bool is_internal) {
  BreakpointResolverSP resolver_sp(
      new ExceptionBreakpointResolver(language, catch_bp, throw_bp));
  SearchFilterSP filter_sp(
      new ExceptionSearchFilter(target.shared_from_this(), language));
  bool hardware = false;
  bool resolve_indirect_functions = false;
  BreakpointSP exc_breakpt_sp(
      target.CreateBreakpoint(filter_sp, resolver_sp, is_internal, hardware,
                              resolve_indirect_functions));
  if (exc_breakpt_sp) {
    Breakpoint::BreakpointPreconditionSP precondition_sp =
        CreateExceptionPrecondition(language, catch_bp, throw_bp);
    if (precondition_sp)
      exc_breakpt_sp->SetPrecondition(precondition_sp);

    if (is_internal)
      exc_breakpt_sp->SetBreakpointKind("exception");
  }

  return exc_breakpt_sp;
}

void LanguageRuntime::InitializeCommands(CommandObject *parent) {
  if (!parent)
    return;

  if (!parent->IsMultiwordObject())
    return;

  LanguageRuntimeCreateInstance create_callback;

  for (uint32_t idx = 0;
       (create_callback =
            PluginManager::GetLanguageRuntimeCreateCallbackAtIndex(idx)) !=
       nullptr;
       ++idx) {
    if (LanguageRuntimeGetCommandObject command_callback =
            PluginManager::GetLanguageRuntimeGetCommandObjectAtIndex(idx)) {
      CommandObjectSP command =
          command_callback(parent->GetCommandInterpreter());
      if (command) {
        // the CommandObject vended by a Language plugin cannot be created once
        // and cached because we may create multiple debuggers and need one
        // instance of the command each - the implementing function is meant to
        // create a new instance of the command each time it is invoked.
        parent->LoadSubCommand(command->GetCommandName().str().c_str(), command);
      }
    }
  }
}

lldb::SearchFilterSP LanguageRuntime::CreateExceptionSearchFilter() {
  return m_process->GetTarget().GetSearchFilterForModule(nullptr);
}
