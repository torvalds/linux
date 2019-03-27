//===-- BreakpointName.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Breakpoint_Name_h_
#define liblldb_Breakpoint_Name_h_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointLocationCollection.h"
#include "lldb/Breakpoint/BreakpointLocationList.h"
#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Breakpoint/Stoppoint.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {

class BreakpointName {
public:
  class Permissions
  {
  public:
  
    enum PermissionKinds { listPerm = 0, disablePerm = 1, 
                       deletePerm = 2, allPerms = 3 };

    Permissions(bool in_list, bool in_disable, bool in_delete) 
    {
      m_permissions[listPerm]    = in_list;
      m_permissions[disablePerm] = in_disable;
      m_permissions[deletePerm]  = in_delete;
      m_set_mask.Set(permissions_mask[allPerms]);
    }
    
    Permissions(const Permissions &rhs)
    {
      m_permissions[listPerm]    = rhs.m_permissions[listPerm];
      m_permissions[disablePerm] = rhs.m_permissions[disablePerm];
      m_permissions[deletePerm]  = rhs.m_permissions[deletePerm];
      m_set_mask = rhs.m_set_mask;
    }
    
    Permissions() 
    {
      m_permissions[listPerm]    = true;
      m_permissions[disablePerm] = true;
      m_permissions[deletePerm]  = true;
      m_set_mask.Clear();
    }
    
    const Permissions &operator= (const Permissions &rhs)
    {
      if (this != &rhs) {
        m_permissions[listPerm]    = rhs.m_permissions[listPerm];
        m_permissions[disablePerm] = rhs.m_permissions[disablePerm];
        m_permissions[deletePerm]  = rhs.m_permissions[deletePerm];
        m_set_mask = rhs.m_set_mask;
      }
      return *this;
    }
    
    void Clear() {
      *this = Permissions();
    }
    
    // Merge the permissions from incoming into this set of permissions. Only
    // merge set permissions, and most restrictive permission wins.
    void MergeInto(const Permissions &incoming)
    {
      MergePermission(incoming, listPerm);
      MergePermission(incoming, disablePerm);
      MergePermission(incoming, deletePerm);
    }

    bool GetAllowList() const { return GetPermission(listPerm); }
    bool SetAllowList(bool value) { return SetPermission(listPerm, value); }
    
    bool GetAllowDelete() const { return GetPermission(deletePerm); }
    bool SetAllowDelete(bool value) { return SetPermission(deletePerm, value); }
    
    bool GetAllowDisable() const { return GetPermission(disablePerm); }
    bool SetAllowDisable(bool value) { return SetPermission(disablePerm, 
                                                            value); }

    bool GetPermission(enum PermissionKinds permission) const
    {
      return m_permissions[permission];
    }

    bool GetDescription(Stream *s, lldb::DescriptionLevel level);

    bool IsSet(enum PermissionKinds permission) const
    {
      return m_set_mask.Test(permissions_mask[permission]);
    }
    
    bool AnySet() {
      return m_set_mask.AnySet(permissions_mask[allPerms]);
    }
    
  private:
    static const Flags::ValueType permissions_mask[allPerms + 1];
    
    bool m_permissions[allPerms];
    Flags m_set_mask;
    
    bool SetPermission(enum PermissionKinds permission, bool value)
    {
      bool old_value = m_permissions[permission];
      m_permissions[permission] = value;
      m_set_mask.Set(permissions_mask[permission]);
      return old_value;
    }
    
    // If either side disallows the permission, the resultant disallows it.
    void MergePermission(const Permissions &incoming, 
                         enum PermissionKinds permission)
    {
      if (incoming.IsSet(permission))
      {
        SetPermission(permission, !(m_permissions[permission] |
            incoming.m_permissions[permission]));
      }
    }
  };
  
  BreakpointName(const ConstString &name, const char *help = nullptr) :
      m_name(name), m_options(false)
   {
     SetHelp(help);
   }
      
  BreakpointName(const ConstString &name,
                 BreakpointOptions &options,
                 const Permissions &permissions = Permissions(),
                 const char *help = nullptr) :
      m_name(name), m_options(options), 
      m_permissions(permissions) {
        SetHelp(help);
  };
  
  BreakpointName(const BreakpointName &rhs) :
      m_name(rhs.m_name), m_options(rhs.m_options),
      m_permissions(rhs.m_permissions), m_help(rhs.m_help)
  {}
  
  BreakpointName(const ConstString &name, const Breakpoint &bkpt,
                 const char *help);
      
  const ConstString &GetName() const { return m_name; }
  BreakpointOptions &GetOptions() { return m_options; }
  const BreakpointOptions &GetOptions() const { return m_options; }
  
  void SetOptions(const BreakpointOptions &options) {
    m_options = options;
  }
  
  Permissions &GetPermissions() { return m_permissions; }
  const Permissions &GetPermissions() const { return m_permissions; }
  void SetPermissions(const Permissions &permissions) {
    m_permissions = permissions;
  }
  
  bool GetPermission(Permissions::PermissionKinds permission) const
  {
    return m_permissions.GetPermission(permission);
  }
  
  void SetHelp(const char *description)
  {
    if (description)
      m_help.assign(description);
    else
      m_help.clear();
  }
  
  const char *GetHelp()
  {
    return m_help.c_str();
  }
  
  // Returns true if any options were set in the name
  bool GetDescription(Stream *s, lldb::DescriptionLevel level);
  
  void ConfigureBreakpoint(lldb::BreakpointSP bp_sp);
  
private:
  ConstString        m_name;
  BreakpointOptions  m_options;
  Permissions        m_permissions;
  std::string        m_help;
};

} // namespace lldb_private

#endif // liblldb_Breakpoint_Name_h_
