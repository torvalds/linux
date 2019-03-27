//===- Error.h - system_error extensions for lld ----------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This declares a new error_category for the lld library.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_ERROR_H
#define LLD_CORE_ERROR_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include <system_error>

namespace lld {

const std::error_category &YamlReaderCategory();

enum class YamlReaderError {
  unknown_keyword,
  illegal_value
};

inline std::error_code make_error_code(YamlReaderError e) {
  return std::error_code(static_cast<int>(e), YamlReaderCategory());
}

/// Creates an error_code object that has associated with it an arbitrary
/// error messsage.  The value() of the error_code will always be non-zero
/// but its value is meaningless. The messsage() will be (a copy of) the
/// supplied error string.
/// Note:  Once ErrorOr<> is updated to work with errors other than error_code,
/// this can be updated to return some other kind of error.
std::error_code make_dynamic_error_code(StringRef msg);

/// Generic error.
///
/// For errors that don't require their own specific sub-error (most errors)
/// this class can be used to describe the error via a string message.
class GenericError : public llvm::ErrorInfo<GenericError> {
public:
  static char ID;
  GenericError(Twine Msg);
  const std::string &getMessage() const { return Msg; }
  void log(llvm::raw_ostream &OS) const override;

  std::error_code convertToErrorCode() const override {
    return make_dynamic_error_code(getMessage());
  }

private:
  std::string Msg;
};

} // end namespace lld

namespace std {
template <> struct is_error_code_enum<lld::YamlReaderError> : std::true_type {};
}

#endif
