//===-- MSVCUndecoratedNameParser.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MSVCUndecoratedNameParser_h_
#define liblldb_MSVCUndecoratedNameParser_h_

#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

class MSVCUndecoratedNameSpecifier {
public:
  MSVCUndecoratedNameSpecifier(llvm::StringRef full_name,
                               llvm::StringRef base_name)
      : m_full_name(full_name), m_base_name(base_name) {}

  llvm::StringRef GetFullName() const { return m_full_name; }
  llvm::StringRef GetBaseName() const { return m_base_name; }

private:
  llvm::StringRef m_full_name;
  llvm::StringRef m_base_name;
};

class MSVCUndecoratedNameParser {
public:
  explicit MSVCUndecoratedNameParser(llvm::StringRef name);

  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> GetSpecifiers() const {
    return m_specifiers;
  }

  static bool IsMSVCUndecoratedName(llvm::StringRef name);
  static bool ExtractContextAndIdentifier(llvm::StringRef name,
                                          llvm::StringRef &context,
                                          llvm::StringRef &identifier);

  static llvm::StringRef DropScope(llvm::StringRef name);

private:
  std::vector<MSVCUndecoratedNameSpecifier> m_specifiers;
};

#endif
