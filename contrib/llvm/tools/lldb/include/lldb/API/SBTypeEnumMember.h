
//===-- SBTypeEnumMember.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTypeEnumMember_h_
#define LLDB_SBTypeEnumMember_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeEnumMember {
public:
  SBTypeEnumMember();

  SBTypeEnumMember(const SBTypeEnumMember &rhs);

  ~SBTypeEnumMember();

  SBTypeEnumMember &operator=(const SBTypeEnumMember &rhs);

  bool IsValid() const;

  int64_t GetValueAsSigned();

  uint64_t GetValueAsUnsigned();

  const char *GetName();

  lldb::SBType GetType();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

protected:
  friend class SBType;
  friend class SBTypeEnumMemberList;

  void reset(lldb_private::TypeEnumMemberImpl *);

  lldb_private::TypeEnumMemberImpl &ref();

  const lldb_private::TypeEnumMemberImpl &ref() const;

  lldb::TypeEnumMemberImplSP m_opaque_sp;

  SBTypeEnumMember(const lldb::TypeEnumMemberImplSP &);
};

class SBTypeEnumMemberList {
public:
  SBTypeEnumMemberList();

  SBTypeEnumMemberList(const SBTypeEnumMemberList &rhs);

  ~SBTypeEnumMemberList();

  SBTypeEnumMemberList &operator=(const SBTypeEnumMemberList &rhs);

  bool IsValid();

  void Append(SBTypeEnumMember entry);

  SBTypeEnumMember GetTypeEnumMemberAtIndex(uint32_t index);

  uint32_t GetSize();

private:
  std::unique_ptr<lldb_private::TypeEnumMemberListImpl> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBTypeEnumMember_h_
