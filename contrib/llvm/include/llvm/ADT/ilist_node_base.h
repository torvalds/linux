//===- llvm/ADT/ilist_node_base.h - Intrusive List Node Base -----*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_BASE_H
#define LLVM_ADT_ILIST_NODE_BASE_H

#include "llvm/ADT/PointerIntPair.h"

namespace llvm {

/// Base class for ilist nodes.
///
/// Optionally tracks whether this node is the sentinel.
template <bool EnableSentinelTracking> class ilist_node_base;

template <> class ilist_node_base<false> {
  ilist_node_base *Prev = nullptr;
  ilist_node_base *Next = nullptr;

public:
  void setPrev(ilist_node_base *Prev) { this->Prev = Prev; }
  void setNext(ilist_node_base *Next) { this->Next = Next; }
  ilist_node_base *getPrev() const { return Prev; }
  ilist_node_base *getNext() const { return Next; }

  bool isKnownSentinel() const { return false; }
  void initializeSentinel() {}
};

template <> class ilist_node_base<true> {
  PointerIntPair<ilist_node_base *, 1> PrevAndSentinel;
  ilist_node_base *Next = nullptr;

public:
  void setPrev(ilist_node_base *Prev) { PrevAndSentinel.setPointer(Prev); }
  void setNext(ilist_node_base *Next) { this->Next = Next; }
  ilist_node_base *getPrev() const { return PrevAndSentinel.getPointer(); }
  ilist_node_base *getNext() const { return Next; }

  bool isSentinel() const { return PrevAndSentinel.getInt(); }
  bool isKnownSentinel() const { return isSentinel(); }
  void initializeSentinel() { PrevAndSentinel.setInt(true); }
};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_BASE_H
