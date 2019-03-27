//===-- Use.cpp - Implement the Use class ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include <new>

namespace llvm {

void Use::swap(Use &RHS) {
  if (Val == RHS.Val)
    return;

  if (Val)
    removeFromList();

  Value *OldVal = Val;
  if (RHS.Val) {
    RHS.removeFromList();
    Val = RHS.Val;
    Val->addUse(*this);
  } else {
    Val = nullptr;
  }

  if (OldVal) {
    RHS.Val = OldVal;
    RHS.Val->addUse(RHS);
  } else {
    RHS.Val = nullptr;
  }
}

User *Use::getUser() const {
  const Use *End = getImpliedUser();
  const UserRef *ref = reinterpret_cast<const UserRef *>(End);
  return ref->getInt() ? ref->getPointer()
                       : reinterpret_cast<User *>(const_cast<Use *>(End));
}

unsigned Use::getOperandNo() const {
  return this - getUser()->op_begin();
}

// Sets up the waymarking algorithm's tags for a series of Uses. See the
// algorithm details here:
//
//   http://www.llvm.org/docs/ProgrammersManual.html#the-waymarking-algorithm
//
Use *Use::initTags(Use *const Start, Use *Stop) {
  ptrdiff_t Done = 0;
  while (Done < 20) {
    if (Start == Stop--)
      return Start;
    static const PrevPtrTag tags[20] = {
        fullStopTag,  oneDigitTag,  stopTag,      oneDigitTag, oneDigitTag,
        stopTag,      zeroDigitTag, oneDigitTag,  oneDigitTag, stopTag,
        zeroDigitTag, oneDigitTag,  zeroDigitTag, oneDigitTag, stopTag,
        oneDigitTag,  oneDigitTag,  oneDigitTag,  oneDigitTag, stopTag};
    new (Stop) Use(tags[Done++]);
  }

  ptrdiff_t Count = Done;
  while (Start != Stop) {
    --Stop;
    if (!Count) {
      new (Stop) Use(stopTag);
      ++Done;
      Count = Done;
    } else {
      new (Stop) Use(PrevPtrTag(Count & 1));
      Count >>= 1;
      ++Done;
    }
  }

  return Start;
}

void Use::zap(Use *Start, const Use *Stop, bool del) {
  while (Start != Stop)
    (--Stop)->~Use();
  if (del)
    ::operator delete(Start);
}

const Use *Use::getImpliedUser() const {
  const Use *Current = this;

  while (true) {
    unsigned Tag = (Current++)->Prev.getInt();
    switch (Tag) {
    case zeroDigitTag:
    case oneDigitTag:
      continue;

    case stopTag: {
      ++Current;
      ptrdiff_t Offset = 1;
      while (true) {
        unsigned Tag = Current->Prev.getInt();
        switch (Tag) {
        case zeroDigitTag:
        case oneDigitTag:
          ++Current;
          Offset = (Offset << 1) + Tag;
          continue;
        default:
          return Current + Offset;
        }
      }
    }

    case fullStopTag:
      return Current;
    }
  }
}

} // End llvm namespace
