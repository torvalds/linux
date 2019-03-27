//===- BinaryStreamError.cpp - Error extensions for streams -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

char BinaryStreamError::ID = 0;

BinaryStreamError::BinaryStreamError(stream_error_code C)
    : BinaryStreamError(C, "") {}

BinaryStreamError::BinaryStreamError(StringRef Context)
    : BinaryStreamError(stream_error_code::unspecified, Context) {}

BinaryStreamError::BinaryStreamError(stream_error_code C, StringRef Context)
    : Code(C) {
  ErrMsg = "Stream Error: ";
  switch (C) {
  case stream_error_code::unspecified:
    ErrMsg += "An unspecified error has occurred.";
    break;
  case stream_error_code::stream_too_short:
    ErrMsg += "The stream is too short to perform the requested operation.";
    break;
  case stream_error_code::invalid_array_size:
    ErrMsg += "The buffer size is not a multiple of the array element size.";
    break;
  case stream_error_code::invalid_offset:
    ErrMsg += "The specified offset is invalid for the current stream.";
    break;
  case stream_error_code::filesystem_error:
    ErrMsg += "An I/O error occurred on the file system.";
    break;
  }

  if (!Context.empty()) {
    ErrMsg += "  ";
    ErrMsg += Context;
  }
}

void BinaryStreamError::log(raw_ostream &OS) const { OS << ErrMsg; }

StringRef BinaryStreamError::getErrorMessage() const { return ErrMsg; }

std::error_code BinaryStreamError::convertToErrorCode() const {
  return inconvertibleErrorCode();
}
