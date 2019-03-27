//===- llvm/Testing/Support/Error.cpp -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Testing/Support/Error.h"

#include "llvm/ADT/StringRef.h"

using namespace llvm;

llvm::detail::ErrorHolder llvm::detail::TakeError(llvm::Error Err) {
  std::vector<std::shared_ptr<ErrorInfoBase>> Infos;
  handleAllErrors(std::move(Err),
                  [&Infos](std::unique_ptr<ErrorInfoBase> Info) {
                    Infos.emplace_back(std::move(Info));
                  });
  return {std::move(Infos)};
}
