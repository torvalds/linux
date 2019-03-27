//===- ELFObjcopy.h ---------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_ELFOBJCOPY_H
#define LLVM_TOOLS_OBJCOPY_ELFOBJCOPY_H

namespace llvm {
class MemoryBuffer;

namespace object {
class ELFObjectFileBase;
} // end namespace object

namespace objcopy {
struct CopyConfig;
class Buffer;

namespace elf {
void executeObjcopyOnRawBinary(const CopyConfig &Config, MemoryBuffer &In,
                               Buffer &Out);
void executeObjcopyOnBinary(const CopyConfig &Config,
                            object::ELFObjectFileBase &In, Buffer &Out);

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_ELFOBJCOPY_H
