//===- xray-registry.cpp: Implement a command registry. -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implement a simple subcommand registry.
//
//===----------------------------------------------------------------------===//
#include "xray-registry.h"

#include "llvm/Support/ManagedStatic.h"
#include <unordered_map>

namespace llvm {
namespace xray {

using HandlerType = std::function<Error()>;

ManagedStatic<std::unordered_map<cl::SubCommand *, HandlerType>> Commands;

CommandRegistration::CommandRegistration(cl::SubCommand *SC,
                                         HandlerType Command) {
  assert(Commands->count(SC) == 0 &&
         "Attempting to overwrite a command handler");
  assert(Command && "Attempting to register an empty std::function<Error()>");
  (*Commands)[SC] = Command;
}

HandlerType dispatch(cl::SubCommand *SC) {
  auto It = Commands->find(SC);
  assert(It != Commands->end() &&
         "Attempting to dispatch on un-registered SubCommand.");
  return It->second;
}

} // namespace xray
} // namespace llvm
