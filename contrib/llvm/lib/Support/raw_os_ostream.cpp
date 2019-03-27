//===--- raw_os_ostream.cpp - Implement the raw_os_ostream class ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements support adapting raw_ostream to std::ostream.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/raw_os_ostream.h"
#include <ostream>
using namespace llvm;

//===----------------------------------------------------------------------===//
//  raw_os_ostream
//===----------------------------------------------------------------------===//

raw_os_ostream::~raw_os_ostream() {
  flush();
}

void raw_os_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.write(Ptr, Size);
}

uint64_t raw_os_ostream::current_pos() const { return OS.tellp(); }
