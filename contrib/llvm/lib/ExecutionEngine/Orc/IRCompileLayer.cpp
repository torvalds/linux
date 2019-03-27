//===--------------- IRCompileLayer.cpp - IR Compiling Layer --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"

namespace llvm {
namespace orc {

IRCompileLayer::IRCompileLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                                 CompileFunction Compile)
    : IRLayer(ES), BaseLayer(BaseLayer), Compile(std::move(Compile)) {}

void IRCompileLayer::setNotifyCompiled(NotifyCompiledFunction NotifyCompiled) {
  std::lock_guard<std::mutex> Lock(IRLayerMutex);
  this->NotifyCompiled = std::move(NotifyCompiled);
}

void IRCompileLayer::emit(MaterializationResponsibility R,
                          ThreadSafeModule TSM) {
  assert(TSM.getModule() && "Module must not be null");

  if (auto Obj = Compile(*TSM.getModule())) {
    {
      std::lock_guard<std::mutex> Lock(IRLayerMutex);
      if (NotifyCompiled)
        NotifyCompiled(R.getVModuleKey(), std::move(TSM));
      else
        TSM = ThreadSafeModule();
    }
    BaseLayer.emit(std::move(R), std::move(*Obj));
  } else {
    R.failMaterialization();
    getExecutionSession().reportError(Obj.takeError());
  }
}

} // End namespace orc.
} // End namespace llvm.
