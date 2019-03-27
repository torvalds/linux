//===-- BreakpointResolverAddress.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointResolverAddress_h_
#define liblldb_BreakpointResolverAddress_h_

#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Core/ModuleSpec.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointResolverAddress BreakpointResolverAddress.h
/// "lldb/Breakpoint/BreakpointResolverAddress.h" This class sets breakpoints
/// on a given Address.  This breakpoint only takes once, and then it won't
/// attempt to reset itself.
//----------------------------------------------------------------------

class BreakpointResolverAddress : public BreakpointResolver {
public:
  BreakpointResolverAddress(Breakpoint *bkpt, const Address &addr);

  BreakpointResolverAddress(Breakpoint *bkpt, const Address &addr,
                            const FileSpec &module_spec);

  ~BreakpointResolverAddress() override;

  static BreakpointResolver *
  CreateFromStructuredData(Breakpoint *bkpt,
                           const StructuredData::Dictionary &options_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

  void ResolveBreakpoint(SearchFilter &filter) override;

  void ResolveBreakpointInModules(SearchFilter &filter,
                                  ModuleList &modules) override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  void Dump(Stream *s) const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverAddress *) { return true; }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::AddressResolver;
  }

  lldb::BreakpointResolverSP CopyForBreakpoint(Breakpoint &breakpoint) override;

protected:
  Address
      m_addr; // The address - may be Section Offset or may be just an offset
  lldb::addr_t m_resolved_addr; // The current value of the resolved load
                                // address for this breakpoint,
  FileSpec m_module_filespec;   // If this filespec is Valid, and m_addr is an
                                // offset, then it will be converted
  // to a Section+Offset address in this module, whenever that module gets
  // around to being loaded.
private:
  DISALLOW_COPY_AND_ASSIGN(BreakpointResolverAddress);
};

} // namespace lldb_private

#endif // liblldb_BreakpointResolverAddress_h_
