//===-- cache_frag.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Header for cache-fragmentation-specific code.
//===----------------------------------------------------------------------===//

#ifndef CACHE_FRAG_H
#define CACHE_FRAG_H

namespace __esan {

void processCacheFragCompilationUnitInit(void *Ptr);
void processCacheFragCompilationUnitExit(void *Ptr);

void initializeCacheFrag();
int finalizeCacheFrag();
void reportCacheFrag();

} // namespace __esan

#endif  // CACHE_FRAG_H
