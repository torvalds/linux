//===- DerivedUser.h - Base for non-IR Users --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DERIVEDUSER_H
#define LLVM_IR_DERIVEDUSER_H

#include "llvm/IR/User.h"

namespace llvm {

class Type;
class Use;

/// Extension point for the Value hierarchy. All classes outside of lib/IR
/// that wish to inherit from User should instead inherit from DerivedUser
/// instead. Inheriting from this class is discouraged.
///
/// Generally speaking, Value is the base of a closed class hierarchy
/// that can't be extended by code outside of lib/IR. This class creates a
/// loophole that allows classes outside of lib/IR to extend User to leverage
/// its use/def list machinery.
class DerivedUser : public User {
protected:
  using  DeleteValueTy = void (*)(DerivedUser *);

private:
  friend class Value;

  DeleteValueTy DeleteValue;

public:
  DerivedUser(Type *Ty, unsigned VK, Use *U, unsigned NumOps,
              DeleteValueTy DeleteValue)
      : User(Ty, VK, U, NumOps), DeleteValue(DeleteValue) {}
};

} // end namespace llvm

#endif // LLVM_IR_DERIVEDUSER_H
