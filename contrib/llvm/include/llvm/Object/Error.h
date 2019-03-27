//===- Error.h - system_error extensions for Object -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This declares a new error_category for the Object library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ERROR_H
#define LLVM_OBJECT_ERROR_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include <system_error>

namespace llvm {
namespace object {

class Binary;

const std::error_category &object_category();

enum class object_error {
  // Error code 0 is absent. Use std::error_code() instead.
  arch_not_found = 1,
  invalid_file_type,
  parse_failed,
  unexpected_eof,
  string_table_non_null_end,
  invalid_section_index,
  bitcode_section_not_found,
  invalid_symbol_index,
};

inline std::error_code make_error_code(object_error e) {
  return std::error_code(static_cast<int>(e), object_category());
}

/// Base class for all errors indicating malformed binary files.
///
/// Having a subclass for all malformed binary files allows archive-walking
/// code to skip malformed files without having to understand every possible
/// way that a binary file might be malformed.
///
/// Currently inherits from ECError for easy interoperability with
/// std::error_code, but this will be removed in the future.
class BinaryError : public ErrorInfo<BinaryError, ECError> {
  virtual void anchor();
public:
  static char ID;
  BinaryError() {
    // Default to parse_failed, can be overridden with setErrorCode.
    setErrorCode(make_error_code(object_error::parse_failed));
  }
};

/// Generic binary error.
///
/// For errors that don't require their own specific sub-error (most errors)
/// this class can be used to describe the error via a string message.
class GenericBinaryError : public ErrorInfo<GenericBinaryError, BinaryError> {
public:
  static char ID;
  GenericBinaryError(Twine Msg);
  GenericBinaryError(Twine Msg, object_error ECOverride);
  const std::string &getMessage() const { return Msg; }
  void log(raw_ostream &OS) const override;
private:
  std::string Msg;
};

/// isNotObjectErrorInvalidFileType() is used when looping through the children
/// of an archive after calling getAsBinary() on the child and it returns an
/// llvm::Error.  In the cases we want to loop through the children and ignore the
/// non-objects in the archive this is used to test the error to see if an
/// error() function needs to called on the llvm::Error.
Error isNotObjectErrorInvalidFileType(llvm::Error Err);

} // end namespace object.

} // end namespace llvm.

namespace std {
template <>
struct is_error_code_enum<llvm::object::object_error> : std::true_type {};
}

#endif
