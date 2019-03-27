//===-------------- IRTransformLayer.cpp - IR Transform Layer -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

IRTransformLayer::IRTransformLayer(ExecutionSession &ES,
                                     IRLayer &BaseLayer,
                                     TransformFunction Transform)
    : IRLayer(ES), BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

void IRTransformLayer::emit(MaterializationResponsibility R,
                            ThreadSafeModule TSM) {
  assert(TSM.getModule() && "Module must not be null");

  if (auto TransformedTSM = Transform(std::move(TSM), R))
    BaseLayer.emit(std::move(R), std::move(*TransformedTSM));
  else {
    R.failMaterialization();
    getExecutionSession().reportError(TransformedTSM.takeError());
  }
}

} // End namespace orc.
} // End namespace llvm.
