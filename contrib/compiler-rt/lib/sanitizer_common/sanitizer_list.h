//===-- sanitizer_list.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains implementation of a list class to be used by
// ThreadSanitizer, etc run-times.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LIST_H
#define SANITIZER_LIST_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

// Intrusive singly-linked list with size(), push_back(), push_front()
// pop_front(), append_front() and append_back().
// This class should be a POD (so that it can be put into TLS)
// and an object with all zero fields should represent a valid empty list.
// This class does not have a CTOR, so clear() should be called on all
// non-zero-initialized objects before using.
template<class Item>
struct IntrusiveList {
  friend class Iterator;

  void clear() {
    first_ = last_ = nullptr;
    size_ = 0;
  }

  bool empty() const { return size_ == 0; }
  uptr size() const { return size_; }

  void push_back(Item *x) {
    if (empty()) {
      x->next = nullptr;
      first_ = last_ = x;
      size_ = 1;
    } else {
      x->next = nullptr;
      last_->next = x;
      last_ = x;
      size_++;
    }
  }

  void push_front(Item *x) {
    if (empty()) {
      x->next = nullptr;
      first_ = last_ = x;
      size_ = 1;
    } else {
      x->next = first_;
      first_ = x;
      size_++;
    }
  }

  void pop_front() {
    CHECK(!empty());
    first_ = first_->next;
    if (!first_)
      last_ = nullptr;
    size_--;
  }

  void extract(Item *prev, Item *x) {
    CHECK(!empty());
    CHECK_NE(prev, nullptr);
    CHECK_NE(x, nullptr);
    CHECK_EQ(prev->next, x);
    prev->next = x->next;
    if (last_ == x)
      last_ = prev;
    size_--;
  }

  Item *front() { return first_; }
  const Item *front() const { return first_; }
  Item *back() { return last_; }
  const Item *back() const { return last_; }

  void append_front(IntrusiveList<Item> *l) {
    CHECK_NE(this, l);
    if (l->empty())
      return;
    if (empty()) {
      *this = *l;
    } else if (!l->empty()) {
      l->last_->next = first_;
      first_ = l->first_;
      size_ += l->size();
    }
    l->clear();
  }

  void append_back(IntrusiveList<Item> *l) {
    CHECK_NE(this, l);
    if (l->empty())
      return;
    if (empty()) {
      *this = *l;
    } else {
      last_->next = l->first_;
      last_ = l->last_;
      size_ += l->size();
    }
    l->clear();
  }

  void CheckConsistency() {
    if (size_ == 0) {
      CHECK_EQ(first_, 0);
      CHECK_EQ(last_, 0);
    } else {
      uptr count = 0;
      for (Item *i = first_; ; i = i->next) {
        count++;
        if (i == last_) break;
      }
      CHECK_EQ(size(), count);
      CHECK_EQ(last_->next, 0);
    }
  }

  template<class ItemTy>
  class IteratorBase {
   public:
    explicit IteratorBase(ItemTy *current) : current_(current) {}
    IteratorBase &operator++() {
      current_ = current_->next;
      return *this;
    }
    bool operator!=(IteratorBase other) const {
      return current_ != other.current_;
    }
    ItemTy &operator*() {
      return *current_;
    }
   private:
    ItemTy *current_;
  };

  typedef IteratorBase<Item> Iterator;
  typedef IteratorBase<const Item> ConstIterator;

  Iterator begin() { return Iterator(first_); }
  Iterator end() { return Iterator(0); }

  ConstIterator begin() const { return ConstIterator(first_); }
  ConstIterator end() const { return ConstIterator(0); }

// private, don't use directly.
  uptr size_;
  Item *first_;
  Item *last_;
};

} // namespace __sanitizer

#endif // SANITIZER_LIST_H
