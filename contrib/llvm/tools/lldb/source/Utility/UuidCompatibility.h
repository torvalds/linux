//===-- UuidCompatibility.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Include this header if your system does not have a definition of uuid_t

#ifndef utility_UUID_COMPATIBILITY_H
#define utility_UUID_COMPATIBILITY_H

// uuid_t is guaranteed to always be a 16-byte array
typedef unsigned char uuid_t[16];

#endif // utility_UUID_COMPATIBILITY_H
