//===-- SBTypeNameSpecifier.h --------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTypeNameSpecifier_h_
#define LLDB_SBTypeNameSpecifier_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeNameSpecifier {
public:
  SBTypeNameSpecifier();

  SBTypeNameSpecifier(const char *name, bool is_regex = false);

  SBTypeNameSpecifier(SBType type);

  SBTypeNameSpecifier(const lldb::SBTypeNameSpecifier &rhs);

  ~SBTypeNameSpecifier();

  bool IsValid() const;

  const char *GetName();

  SBType GetType();

  bool IsRegex();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBTypeNameSpecifier &operator=(const lldb::SBTypeNameSpecifier &rhs);

  bool IsEqualTo(lldb::SBTypeNameSpecifier &rhs);

  bool operator==(lldb::SBTypeNameSpecifier &rhs);

  bool operator!=(lldb::SBTypeNameSpecifier &rhs);

protected:
  friend class SBDebugger;
  friend class SBTypeCategory;

  lldb::TypeNameSpecifierImplSP GetSP();

  void SetSP(const lldb::TypeNameSpecifierImplSP &type_namespec_sp);

  lldb::TypeNameSpecifierImplSP m_opaque_sp;

  SBTypeNameSpecifier(const lldb::TypeNameSpecifierImplSP &);
};

} // namespace lldb

#endif // LLDB_SBTypeNameSpecifier_h_
