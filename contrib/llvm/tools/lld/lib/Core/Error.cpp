//===- Error.cpp - system_error extensions for lld --------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Core/Error.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include <mutex>
#include <string>
#include <vector>

using namespace lld;

namespace {
class _YamlReaderErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override {
    return "lld.yaml.reader";
  }

  std::string message(int ev) const override {
    switch (static_cast<YamlReaderError>(ev)) {
    case YamlReaderError::unknown_keyword:
      return "Unknown keyword found in yaml file";
    case YamlReaderError::illegal_value:
      return "Bad value found in yaml file";
    }
    llvm_unreachable("An enumerator of YamlReaderError does not have a "
                     "message defined.");
  }
};
} // end anonymous namespace

const std::error_category &lld::YamlReaderCategory() {
  static _YamlReaderErrorCategory o;
  return o;
}

namespace lld {

/// Temporary class to enable make_dynamic_error_code() until
/// llvm::ErrorOr<> is updated to work with error encapsulations
/// other than error_code.
class dynamic_error_category : public std::error_category {
public:
  ~dynamic_error_category() override = default;

  const char *name() const noexcept override {
    return "lld.dynamic_error";
  }

  std::string message(int ev) const override {
    assert(ev >= 0);
    assert(ev < (int)_messages.size());
    // The value is an index into the string vector.
    return _messages[ev];
  }

  int add(std::string msg) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Value zero is always the successs value.
    if (_messages.empty())
      _messages.push_back("Success");
    _messages.push_back(msg);
    // Return the index of the string just appended.
    return _messages.size() - 1;
  }

private:
  std::vector<std::string> _messages;
  std::recursive_mutex _mutex;
};

static dynamic_error_category categorySingleton;

std::error_code make_dynamic_error_code(StringRef msg) {
  return std::error_code(categorySingleton.add(msg), categorySingleton);
}

char GenericError::ID = 0;

GenericError::GenericError(Twine Msg) : Msg(Msg.str()) { }

void GenericError::log(raw_ostream &OS) const {
  OS << Msg;
}

} // namespace lld
