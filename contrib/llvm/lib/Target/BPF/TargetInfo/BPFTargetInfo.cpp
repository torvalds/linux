//===-- BPFTargetInfo.cpp - BPF Target Implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

namespace llvm {
Target &getTheBPFleTarget() {
  static Target TheBPFleTarget;
  return TheBPFleTarget;
}
Target &getTheBPFbeTarget() {
  static Target TheBPFbeTarget;
  return TheBPFbeTarget;
}
Target &getTheBPFTarget() {
  static Target TheBPFTarget;
  return TheBPFTarget;
}
} // namespace llvm

extern "C" void LLVMInitializeBPFTargetInfo() {
  TargetRegistry::RegisterTarget(getTheBPFTarget(), "bpf", "BPF (host endian)",
                                 "BPF", [](Triple::ArchType) { return false; },
                                 true);
  RegisterTarget<Triple::bpfel, /*HasJIT=*/true> X(
      getTheBPFleTarget(), "bpfel", "BPF (little endian)", "BPF");
  RegisterTarget<Triple::bpfeb, /*HasJIT=*/true> Y(getTheBPFbeTarget(), "bpfeb",
                                                   "BPF (big endian)", "BPF");
}
