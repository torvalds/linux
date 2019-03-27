//=- WebAssemblySelectionDAGInfo.h - WebAssembly SelectionDAG Info -*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the WebAssembly subclass for
/// SelectionDAGTargetInfo.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

namespace llvm {

class WebAssemblySelectionDAGInfo final : public SelectionDAGTargetInfo {
public:
  ~WebAssemblySelectionDAGInfo() override;
};

} // end namespace llvm

#endif
