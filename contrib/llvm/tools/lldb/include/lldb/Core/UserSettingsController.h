//====-- UserSettingsController.h --------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UserSettingsController_h_
#define liblldb_UserSettingsController_h_

#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"

#include "llvm/ADT/StringRef.h"

#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class CommandInterpreter;
}
namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class ExecutionContext;
}
namespace lldb_private {
class Property;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

class Properties {
public:
  Properties() : m_collection_sp() {}

  Properties(const lldb::OptionValuePropertiesSP &collection_sp)
      : m_collection_sp(collection_sp) {}

  virtual ~Properties() {}

  virtual lldb::OptionValuePropertiesSP GetValueProperties() const {
    // This function is virtual in case subclasses want to lazily implement
    // creating the properties.
    return m_collection_sp;
  }

  virtual lldb::OptionValueSP GetPropertyValue(const ExecutionContext *exe_ctx,
                                               llvm::StringRef property_path,
                                               bool will_modify,
                                               Status &error) const;

  virtual Status SetPropertyValue(const ExecutionContext *exe_ctx,
                                  VarSetOperationType op,
                                  llvm::StringRef property_path,
                                  llvm::StringRef value);

  virtual Status DumpPropertyValue(const ExecutionContext *exe_ctx,
                                   Stream &strm, llvm::StringRef property_path,
                                   uint32_t dump_mask);

  virtual void DumpAllPropertyValues(const ExecutionContext *exe_ctx,
                                     Stream &strm, uint32_t dump_mask);

  virtual void DumpAllDescriptions(CommandInterpreter &interpreter,
                                   Stream &strm) const;

  size_t Apropos(llvm::StringRef keyword,
                 std::vector<const Property *> &matching_properties) const;

  lldb::OptionValuePropertiesSP GetSubProperty(const ExecutionContext *exe_ctx,
                                               const ConstString &name);

  // We sometimes need to introduce a setting to enable experimental features,
  // but then we don't want the setting for these to cause errors when the
  // setting goes away.  Add a sub-topic of the settings using this
  // experimental name, and two things will happen.  One is that settings that
  // don't find the name will not be treated as errors.  Also, if you decide to
  // keep the settings just move them into the containing properties, and we
  // will auto-forward the experimental settings to the real one.
  static const char *GetExperimentalSettingsName();

  static bool IsSettingExperimental(llvm::StringRef setting);

protected:
  lldb::OptionValuePropertiesSP m_collection_sp;
};

} // namespace lldb_private

#endif // liblldb_UserSettingsController_h_
