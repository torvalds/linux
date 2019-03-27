//===-- CPlusPlusLanguage.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CPlusPlusLanguage_h_
#define liblldb_CPlusPlusLanguage_h_

#include <set>
#include <vector>

#include "llvm/ADT/StringRef.h"

#include "Plugins/Language/ClangCommon/ClangHighlighter.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CPlusPlusLanguage : public Language {
  ClangHighlighter m_highlighter;

public:
  class MethodName {
  public:
    MethodName()
        : m_full(), m_basename(), m_context(), m_arguments(), m_qualifiers(),
          m_parsed(false), m_parse_error(false) {}

    MethodName(const ConstString &s)
        : m_full(s), m_basename(), m_context(), m_arguments(), m_qualifiers(),
          m_parsed(false), m_parse_error(false) {}

    void Clear();

    bool IsValid() {
      if (!m_parsed)
        Parse();
      if (m_parse_error)
        return false;
      return (bool)m_full;
    }

    const ConstString &GetFullName() const { return m_full; }

    std::string GetScopeQualifiedName();

    llvm::StringRef GetBasename();

    llvm::StringRef GetContext();

    llvm::StringRef GetArguments();

    llvm::StringRef GetQualifiers();

  protected:
    void Parse();
    bool TrySimplifiedParse();

    ConstString m_full; // Full name:
                        // "lldb::SBTarget::GetBreakpointAtIndex(unsigned int)
                        // const"
    llvm::StringRef m_basename;   // Basename:     "GetBreakpointAtIndex"
    llvm::StringRef m_context;    // Decl context: "lldb::SBTarget"
    llvm::StringRef m_arguments;  // Arguments:    "(unsigned int)"
    llvm::StringRef m_qualifiers; // Qualifiers:   "const"
    bool m_parsed;
    bool m_parse_error;
  };

  CPlusPlusLanguage() = default;

  ~CPlusPlusLanguage() override = default;

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeC_plus_plus;
  }

  std::unique_ptr<TypeScavenger> GetTypeScavenger() override;
  lldb::TypeCategoryImplSP GetFormatters() override;

  HardcodedFormatters::HardcodedSummaryFinder GetHardcodedSummaries() override;

  HardcodedFormatters::HardcodedSyntheticFinder
  GetHardcodedSynthetics() override;

  bool IsSourceFile(llvm::StringRef file_path) const override;

  const Highlighter *GetHighlighter() const override { return &m_highlighter; }

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::Language *CreateInstance(lldb::LanguageType language);

  static lldb_private::ConstString GetPluginNameStatic();

  static bool IsCPPMangledName(const char *name);

  // Extract C++ context and identifier from a string using heuristic matching
  // (as opposed to
  // CPlusPlusLanguage::MethodName which has to have a fully qualified C++ name
  // with parens and arguments.
  // If the name is a lone C identifier (e.g. C) or a qualified C identifier
  // (e.g. A::B::C) it will return true,
  // and identifier will be the identifier (C and C respectively) and the
  // context will be "" and "A::B" respectively.
  // If the name fails the heuristic matching for a qualified or unqualified
  // C/C++ identifier, then it will return false
  // and identifier and context will be unchanged.

  static bool ExtractContextAndIdentifier(const char *name,
                                          llvm::StringRef &context,
                                          llvm::StringRef &identifier);

  // Given a mangled function name, calculates some alternative manglings since
  // the compiler mangling may not line up with the symbol we are expecting
  static uint32_t
  FindAlternateFunctionManglings(const ConstString mangled,
                                 std::set<ConstString> &candidates);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;
};

} // namespace lldb_private

#endif // liblldb_CPlusPlusLanguage_h_
