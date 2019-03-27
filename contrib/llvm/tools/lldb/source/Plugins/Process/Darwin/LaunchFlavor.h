//===-- LaunchFlavor.h ---------------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LaunchFlavor_h
#define LaunchFlavor_h

namespace lldb_private {
namespace process_darwin {

enum class LaunchFlavor {
  Default = 0,
  PosixSpawn = 1,
  ForkExec = 2,
#ifdef WITH_SPRINGBOARD
  SpringBoard = 3,
#endif
#ifdef WITH_BKS
  BKS = 4,
#endif
#ifdef WITH_FBS
  FBS = 5
#endif
};
}
} // namespaces

#endif /* LaunchFlavor_h */
