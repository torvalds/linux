//===--- TypeLocBuilder.cpp - Type Source Info collector ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines TypeLocBuilder, a class for building TypeLocs
//  bottom-up.
//
//===----------------------------------------------------------------------===//

#include "TypeLocBuilder.h"

using namespace clang;

void TypeLocBuilder::pushFullCopy(TypeLoc L) {
  size_t Size = L.getFullDataSize();
  reserve(Size);

  SmallVector<TypeLoc, 4> TypeLocs;
  TypeLoc CurTL = L;
  while (CurTL) {
    TypeLocs.push_back(CurTL);
    CurTL = CurTL.getNextTypeLoc();
  }

  for (unsigned i = 0, e = TypeLocs.size(); i < e; ++i) {
    TypeLoc CurTL = TypeLocs[e-i-1];
    switch (CurTL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    case TypeLoc::CLASS: { \
      CLASS##TypeLoc NewTL = push<class CLASS##TypeLoc>(CurTL.getType()); \
      memcpy(NewTL.getOpaqueData(), CurTL.getOpaqueData(), NewTL.getLocalDataSize()); \
      break; \
    }
#include "clang/AST/TypeLocNodes.def"
    }
  }
}

void TypeLocBuilder::grow(size_t NewCapacity) {
  assert(NewCapacity > Capacity);

  // Allocate the new buffer and copy the old data into it.
  char *NewBuffer = new char[NewCapacity];
  unsigned NewIndex = Index + NewCapacity - Capacity;
  memcpy(&NewBuffer[NewIndex],
         &Buffer[Index],
         Capacity - Index);

  if (Buffer != InlineBuffer.buffer)
    delete[] Buffer;

  Buffer = NewBuffer;
  Capacity = NewCapacity;
  Index = NewIndex;
}

TypeLoc TypeLocBuilder::pushImpl(QualType T, size_t LocalSize, unsigned LocalAlignment) {
#ifndef NDEBUG
  QualType TLast = TypeLoc(T, nullptr).getNextTypeLoc().getType();
  assert(TLast == LastTy &&
         "mismatch between last type and new type's inner type");
  LastTy = T;
#endif

  assert(LocalAlignment <= BufferMaxAlignment && "Unexpected alignment");

  // If we need to grow, grow by a factor of 2.
  if (LocalSize > Index) {
    size_t RequiredCapacity = Capacity + (LocalSize - Index);
    size_t NewCapacity = Capacity * 2;
    while (RequiredCapacity > NewCapacity)
      NewCapacity *= 2;
    grow(NewCapacity);
  }

  // Because we're adding elements to the TypeLoc backwards, we have to
  // do some extra work to keep everything aligned appropriately.
  // FIXME: This algorithm is a absolute mess because every TypeLoc returned
  // needs to be valid.  Partial TypeLocs are a terrible idea.
  // FIXME: 4 and 8 are sufficient at the moment, but it's pretty ugly to
  // hardcode them.
  if (LocalAlignment == 4) {
    if (NumBytesAtAlign8 == 0) {
      NumBytesAtAlign4 += LocalSize;
    } else {
      unsigned Padding = NumBytesAtAlign4 % 8;
      if (Padding == 0) {
        if (LocalSize % 8 == 0) {
          // Everything is set: there's no padding and we don't need to add
          // any.
        } else {
          assert(LocalSize % 8 == 4);
          // No existing padding; add in 4 bytes padding
          memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
          Index -= 4;
        }
      } else {
        assert(Padding == 4);
        if (LocalSize % 8 == 0) {
          // Everything is set: there's 4 bytes padding and we don't need
          // to add any.
        } else {
          assert(LocalSize % 8 == 4);
          // There are 4 bytes padding, but we don't need any; remove it.
          memmove(&Buffer[Index + 4], &Buffer[Index], NumBytesAtAlign4);
          Index += 4;
        }
      }
      NumBytesAtAlign4 += LocalSize;
    }
  } else if (LocalAlignment == 8) {
    if (NumBytesAtAlign8 == 0) {
      // We have not seen any 8-byte aligned element yet. We insert a padding
      // only if the new Index is not 8-byte-aligned.
      if ((Index - LocalSize) % 8 != 0) {
        memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
        Index -= 4;
      }
    } else {
      unsigned Padding = NumBytesAtAlign4 % 8;
      if (Padding == 0) {
        if (LocalSize % 8 == 0) {
          // Everything is set: there's no padding and we don't need to add
          // any.
        } else {
          assert(LocalSize % 8 == 4);
          // No existing padding; add in 4 bytes padding
          memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
          Index -= 4;
        }
      } else {
        assert(Padding == 4);
        if (LocalSize % 8 == 0) {
          // Everything is set: there's 4 bytes padding and we don't need
          // to add any.
        } else {
          assert(LocalSize % 8 == 4);
          // There are 4 bytes padding, but we don't need any; remove it.
          memmove(&Buffer[Index + 4], &Buffer[Index], NumBytesAtAlign4);
          Index += 4;
        }
      }
    }

    // Forget about any padding.
    NumBytesAtAlign4 = 0;
    NumBytesAtAlign8 += LocalSize;
  } else {
    assert(LocalSize == 0);
  }

  Index -= LocalSize;

  assert(Capacity - Index == TypeLoc::getFullDataSizeForType(T) &&
         "incorrect data size provided to CreateTypeSourceInfo!");

  return getTemporaryTypeLoc(T);
}
