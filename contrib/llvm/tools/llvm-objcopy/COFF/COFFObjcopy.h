//===- COFFObjcopy.h --------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_COFFOBJCOPY_H
#define LLVM_TOOLS_OBJCOPY_COFFOBJCOPY_H

namespace llvm {

namespace object {
class COFFObjectFile;
} // end namespace object

namespace objcopy {
struct CopyConfig;
class Buffer;

namespace coff {
void executeObjcopyOnBinary(const CopyConfig &Config,
                            object::COFFObjectFile &In, Buffer &Out);

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_COFFOBJCOPY_H
