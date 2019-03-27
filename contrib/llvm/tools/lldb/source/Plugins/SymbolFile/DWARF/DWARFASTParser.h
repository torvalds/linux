//===-- DWARFASTParser.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFASTParser_h_
#define SymbolFileDWARF_DWARFASTParser_h_

#include "DWARFDefines.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"

class DWARFDIE;
namespace lldb_private {
class CompileUnit;
class ExecutionContext;
}
class SymbolFileDWARF;

class DWARFASTParser {
public:
  virtual ~DWARFASTParser() {}

  virtual lldb::TypeSP ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                                          const DWARFDIE &die,
                                          lldb_private::Log *log,
                                          bool *type_is_new_ptr) = 0;

  virtual lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const DWARFDIE &die) = 0;

  virtual bool
  CompleteTypeFromDWARF(const DWARFDIE &die, lldb_private::Type *type,
                        lldb_private::CompilerType &compiler_type) = 0;

  virtual lldb_private::CompilerDecl
  GetDeclForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual lldb_private::CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual lldb_private::CompilerDeclContext
  GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual std::vector<DWARFDIE>
  GetDIEForDeclContext(lldb_private::CompilerDeclContext decl_context) = 0;

  static llvm::Optional<lldb_private::SymbolFile::ArrayInfo>
  ParseChildArrayInfo(const DWARFDIE &parent_die,
                      const lldb_private::ExecutionContext *exe_ctx = nullptr);
};

#endif // SymbolFileDWARF_DWARFASTParser_h_
