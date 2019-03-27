//===---------- ObjectTransformLayer.cpp - Object Transform Layer ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

ObjectTransformLayer::ObjectTransformLayer(ExecutionSession &ES,
                                            ObjectLayer &BaseLayer,
                                            TransformFunction Transform)
    : ObjectLayer(ES), BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

void ObjectTransformLayer::emit(MaterializationResponsibility R,
                                std::unique_ptr<MemoryBuffer> O) {
  assert(O && "Module must not be null");

  if (auto TransformedObj = Transform(std::move(O)))
    BaseLayer.emit(std::move(R), std::move(*TransformedObj));
  else {
    R.failMaterialization();
    getExecutionSession().reportError(TransformedObj.takeError());
  }
}

} // End namespace orc.
} // End namespace llvm.
