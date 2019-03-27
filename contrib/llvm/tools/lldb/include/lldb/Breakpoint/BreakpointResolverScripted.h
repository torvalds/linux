//===-- BreakpointResolverScripted.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointResolverScripted_h_
#define liblldb_BreakpointResolverScripted_h_

#include "lldb/lldb-forward.h"
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Core/ModuleSpec.h"


namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointResolverScripted BreakpointResolverScripted.h
/// "lldb/Breakpoint/BreakpointResolverScripted.h" This class sets breakpoints
/// on a given Address.  This breakpoint only takes once, and then it won't
/// attempt to reset itself.
//----------------------------------------------------------------------

class BreakpointResolverScripted : public BreakpointResolver {
public:
  BreakpointResolverScripted(Breakpoint *bkpt,
                             const llvm::StringRef class_name,
                             lldb::SearchDepth depth,
                             StructuredDataImpl *args_data,
                             ScriptInterpreter &script_interp);

  ~BreakpointResolverScripted() override;

  static BreakpointResolver *
  CreateFromStructuredData(Breakpoint *bkpt,
                           const StructuredData::Dictionary &options_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  void Dump(Stream *s) const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverScripted *) { return true; }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::PythonResolver;
  }

  lldb::BreakpointResolverSP CopyForBreakpoint(Breakpoint &breakpoint) override;

protected:
  void NotifyBreakpointSet() override;
private:
  void CreateImplementationIfNeeded();
  ScriptInterpreter *GetScriptInterpreter();
  
  std::string m_class_name;
  lldb::SearchDepth m_depth;
  StructuredDataImpl *m_args_ptr; // We own this, but the implementation
                                  // has to manage the UP (since that is
                                  // how it gets stored in the
                                  // SBStructuredData).
  StructuredData::GenericSP m_implementation_sp;

  DISALLOW_COPY_AND_ASSIGN(BreakpointResolverScripted);
};

} // namespace lldb_private

#endif // liblldb_BreakpointResolverScripted_h_
