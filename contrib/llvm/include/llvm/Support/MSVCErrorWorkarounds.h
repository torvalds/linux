//===--- MSVCErrorWorkarounds.h - Enable future<Error> in MSVC --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MSVC's promise/future implementation requires types to be default
// constructible, so this header provides analogues of Error an Expected
// that are default constructed in a safely destructible state.
//
// FIXME: Kill off this header and migrate all users to Error/Expected once we
//        move to MSVC versions that support non-default-constructible types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H
#define LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H

#include "llvm/Support/Error.h"

namespace llvm {

// A default-constructible llvm::Error that is suitable for use with MSVC's
// std::future implementation which requires default constructible types.
class MSVCPError : public Error {
public:
  MSVCPError() { (void)!!*this; }

  MSVCPError(MSVCPError &&Other) : Error(std::move(Other)) {}

  MSVCPError &operator=(MSVCPError Other) {
    Error::operator=(std::move(Other));
    return *this;
  }

  MSVCPError(Error Err) : Error(std::move(Err)) {}
};

// A default-constructible llvm::Expected that is suitable for use with MSVC's
// std::future implementation, which requires default constructible types.
template <typename T> class MSVCPExpected : public Expected<T> {
public:
  MSVCPExpected()
      : Expected<T>(make_error<StringError>("", inconvertibleErrorCode())) {
    consumeError(this->takeError());
  }

  MSVCPExpected(MSVCPExpected &&Other) : Expected<T>(std::move(Other)) {}

  MSVCPExpected &operator=(MSVCPExpected &&Other) {
    Expected<T>::operator=(std::move(Other));
    return *this;
  }

  MSVCPExpected(Error Err) : Expected<T>(std::move(Err)) {}

  template <typename OtherT>
  MSVCPExpected(
      OtherT &&Val,
      typename std::enable_if<std::is_convertible<OtherT, T>::value>::type * =
          nullptr)
      : Expected<T>(std::move(Val)) {}

  template <class OtherT>
  MSVCPExpected(
      Expected<OtherT> &&Other,
      typename std::enable_if<std::is_convertible<OtherT, T>::value>::type * =
          nullptr)
      : Expected<T>(std::move(Other)) {}

  template <class OtherT>
  explicit MSVCPExpected(
      Expected<OtherT> &&Other,
      typename std::enable_if<!std::is_convertible<OtherT, T>::value>::type * =
          nullptr)
      : Expected<T>(std::move(Other)) {}
};

} // end namespace llvm

#endif // LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H
