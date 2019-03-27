//===-- BreakpointResolverScripted.cpp ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverScripted.h"


#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// BreakpointResolverScripted:
//----------------------------------------------------------------------
BreakpointResolverScripted::BreakpointResolverScripted(
    Breakpoint *bkpt, 
    const llvm::StringRef class_name,
    lldb::SearchDepth depth,
    StructuredDataImpl *args_data,
    ScriptInterpreter &script_interp)
    : BreakpointResolver(bkpt, BreakpointResolver::PythonResolver),
      m_class_name(class_name), m_depth(depth), m_args_ptr(args_data) {
  CreateImplementationIfNeeded();
}

void BreakpointResolverScripted::CreateImplementationIfNeeded() {
  if (m_implementation_sp)
    return;
  
  if (m_class_name.empty())
    return;
  
  if (m_breakpoint) {
    TargetSP target_sp = m_breakpoint->GetTargetSP();
    ScriptInterpreter *script_interp = target_sp->GetDebugger()
                                                .GetCommandInterpreter()
                                                .GetScriptInterpreter();
    if (!script_interp)
      return;
    lldb::BreakpointSP bkpt_sp(m_breakpoint->shared_from_this());
    m_implementation_sp = script_interp->CreateScriptedBreakpointResolver(
        m_class_name.c_str(), m_args_ptr, bkpt_sp);
  }
}

void BreakpointResolverScripted::NotifyBreakpointSet() {
  CreateImplementationIfNeeded();
}

BreakpointResolverScripted::~BreakpointResolverScripted() {}

BreakpointResolver *
BreakpointResolverScripted::CreateFromStructuredData(
    Breakpoint *bkpt, const StructuredData::Dictionary &options_dict,
    Status &error) {
  llvm::StringRef class_name;
  bool success;
  
  if (!bkpt)
    return nullptr;

  success = options_dict.GetValueForKeyAsString(
      GetKey(OptionNames::PythonClassName), class_name);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find class name entry.");
    return nullptr;
  }
  lldb::SearchDepth depth;
  int depth_as_int;
  success = options_dict.GetValueForKeyAsInteger(
      GetKey(OptionNames::SearchDepth), depth_as_int);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find class name entry.");
    return nullptr;
  }
  if (depth_as_int >= (int) OptionNames::LastOptionName) {
    error.SetErrorString("BRFL::CFSD: Invalid value for search depth.");
    return nullptr;
  }
  depth = (lldb::SearchDepth) depth_as_int;
  
  StructuredDataImpl *args_data_impl = new StructuredDataImpl();
  StructuredData::Dictionary *args_dict = new StructuredData::Dictionary();
  success = options_dict.GetValueForKeyAsDictionary(
    GetKey(OptionNames::ScriptArgs), args_dict);
  if (success) {
    // FIXME: The resolver needs a copy of the ARGS dict that it can own,
    // so I need to make a copy constructor for the Dictionary so I can pass
    // that to it here.  For now the args are empty.
    //StructuredData::Dictionary *dict_copy = new StructuredData::Dictionary(args_dict);
    
  }
  ScriptInterpreter *script_interp = bkpt->GetTarget()
                                         .GetDebugger()
                                         .GetCommandInterpreter()
                                         .GetScriptInterpreter();
  return new BreakpointResolverScripted(bkpt, class_name, depth, args_data_impl,
                                      *script_interp);
}

StructuredData::ObjectSP
BreakpointResolverScripted::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());

  options_dict_sp->AddStringItem(GetKey(OptionNames::PythonClassName),
                                   m_class_name);
  return WrapOptionsDict(options_dict_sp);
}

ScriptInterpreter *BreakpointResolverScripted::GetScriptInterpreter() {
    return m_breakpoint->GetTarget().GetDebugger().GetCommandInterpreter()
        .GetScriptInterpreter();
}

Searcher::CallbackReturn
BreakpointResolverScripted::SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) {
  assert(m_breakpoint != NULL);
  bool should_continue = true;
  if (!m_implementation_sp)
    return Searcher::eCallbackReturnStop;
  
  ScriptInterpreter *interp = GetScriptInterpreter();
  should_continue = interp->ScriptedBreakpointResolverSearchCallback(
      m_implementation_sp,
      &context);
  if (should_continue)
    return Searcher::eCallbackReturnContinue;
  else
    return Searcher::eCallbackReturnStop;
}

lldb::SearchDepth
BreakpointResolverScripted::GetDepth() {
  assert(m_breakpoint != NULL);
  lldb::SearchDepth depth = lldb::eSearchDepthModule;
  if (m_implementation_sp) {
    ScriptInterpreter *interp = GetScriptInterpreter();
    depth = interp->ScriptedBreakpointResolverSearchDepth(
        m_implementation_sp);
  }
  return depth;
}

void BreakpointResolverScripted::GetDescription(Stream *s) {
  StructuredData::GenericSP generic_sp;
  std::string short_help;

  if (m_implementation_sp) {
    ScriptInterpreter *interp = GetScriptInterpreter();
    interp->GetShortHelpForCommandObject(m_implementation_sp,
                                         short_help);
  }
  if (!short_help.empty())
    s->PutCString(short_help.c_str());
  else
    s->Printf("python class = %s", m_class_name.c_str());
}

void BreakpointResolverScripted::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverScripted::CopyForBreakpoint(Breakpoint &breakpoint) {
  ScriptInterpreter *script_interp = GetScriptInterpreter();
  // FIXME: Have to make a copy of the arguments from the m_args_ptr and then
  // pass that to the new resolver.
  lldb::BreakpointResolverSP ret_sp(
      new BreakpointResolverScripted(&breakpoint, m_class_name, 
                                   m_depth, nullptr, *script_interp));
  return ret_sp;
}
