//===- TemplateInstCallback.h - Template Instantiation Callback - C++ --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file defines the TemplateInstantiationCallback class, which is the
// base class for callbacks that will be notified at template instantiations.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TEMPLATE_INST_CALLBACK_H
#define LLVM_CLANG_TEMPLATE_INST_CALLBACK_H

#include "clang/Sema/Sema.h"

namespace clang {

/// This is a base class for callbacks that will be notified at every
/// template instantiation.
class TemplateInstantiationCallback {
public:
  virtual ~TemplateInstantiationCallback() = default;

  /// Called before doing AST-parsing.
  virtual void initialize(const Sema &TheSema) = 0;

  /// Called after AST-parsing is completed.
  virtual void finalize(const Sema &TheSema) = 0;

  /// Called when instantiation of a template just began.
  virtual void atTemplateBegin(const Sema &TheSema,
                               const Sema::CodeSynthesisContext &Inst) = 0;

  /// Called when instantiation of a template is just about to end.
  virtual void atTemplateEnd(const Sema &TheSema,
                             const Sema::CodeSynthesisContext &Inst) = 0;
};

template <class TemplateInstantiationCallbackPtrs>
void initialize(TemplateInstantiationCallbackPtrs &Callbacks,
                const Sema &TheSema) {
  for (auto &C : Callbacks) {
    if (C)
      C->initialize(TheSema);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void finalize(TemplateInstantiationCallbackPtrs &Callbacks,
              const Sema &TheSema) {
  for (auto &C : Callbacks) {
    if (C)
      C->finalize(TheSema);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void atTemplateBegin(TemplateInstantiationCallbackPtrs &Callbacks,
                     const Sema &TheSema,
                     const Sema::CodeSynthesisContext &Inst) {
  for (auto &C : Callbacks) {
    if (C)
      C->atTemplateBegin(TheSema, Inst);
  }
}

template <class TemplateInstantiationCallbackPtrs>
void atTemplateEnd(TemplateInstantiationCallbackPtrs &Callbacks,
                   const Sema &TheSema,
                   const Sema::CodeSynthesisContext &Inst) {
  for (auto &C : Callbacks) {
    if (C)
      C->atTemplateEnd(TheSema, Inst);
  }
}

} // namespace clang

#endif
