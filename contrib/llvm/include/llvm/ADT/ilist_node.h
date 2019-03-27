//===- llvm/ADT/ilist_node.h - Intrusive Linked List Helper -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ilist_node class template, which is a convenient
// base class for creating classes that can be used with ilists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_H
#define LLVM_ADT_ILIST_NODE_H

#include "llvm/ADT/ilist_node_base.h"
#include "llvm/ADT/ilist_node_options.h"

namespace llvm {

namespace ilist_detail {

struct NodeAccess;

} // end namespace ilist_detail

template <class OptionsT, bool IsReverse, bool IsConst> class ilist_iterator;
template <class OptionsT> class ilist_sentinel;

/// Implementation for an ilist node.
///
/// Templated on an appropriate \a ilist_detail::node_options, usually computed
/// by \a ilist_detail::compute_node_options.
///
/// This is a wrapper around \a ilist_node_base whose main purpose is to
/// provide type safety: you can't insert nodes of \a ilist_node_impl into the
/// wrong \a simple_ilist or \a iplist.
template <class OptionsT> class ilist_node_impl : OptionsT::node_base_type {
  using value_type = typename OptionsT::value_type;
  using node_base_type = typename OptionsT::node_base_type;
  using list_base_type = typename OptionsT::list_base_type;

  friend typename OptionsT::list_base_type;
  friend struct ilist_detail::NodeAccess;
  friend class ilist_sentinel<OptionsT>;
  friend class ilist_iterator<OptionsT, false, false>;
  friend class ilist_iterator<OptionsT, false, true>;
  friend class ilist_iterator<OptionsT, true, false>;
  friend class ilist_iterator<OptionsT, true, true>;

protected:
  using self_iterator = ilist_iterator<OptionsT, false, false>;
  using const_self_iterator = ilist_iterator<OptionsT, false, true>;
  using reverse_self_iterator = ilist_iterator<OptionsT, true, false>;
  using const_reverse_self_iterator = ilist_iterator<OptionsT, true, true>;

  ilist_node_impl() = default;

private:
  ilist_node_impl *getPrev() {
    return static_cast<ilist_node_impl *>(node_base_type::getPrev());
  }

  ilist_node_impl *getNext() {
    return static_cast<ilist_node_impl *>(node_base_type::getNext());
  }

  const ilist_node_impl *getPrev() const {
    return static_cast<ilist_node_impl *>(node_base_type::getPrev());
  }

  const ilist_node_impl *getNext() const {
    return static_cast<ilist_node_impl *>(node_base_type::getNext());
  }

  void setPrev(ilist_node_impl *N) { node_base_type::setPrev(N); }
  void setNext(ilist_node_impl *N) { node_base_type::setNext(N); }

public:
  self_iterator getIterator() { return self_iterator(*this); }
  const_self_iterator getIterator() const { return const_self_iterator(*this); }

  reverse_self_iterator getReverseIterator() {
    return reverse_self_iterator(*this);
  }

  const_reverse_self_iterator getReverseIterator() const {
    return const_reverse_self_iterator(*this);
  }

  // Under-approximation, but always available for assertions.
  using node_base_type::isKnownSentinel;

  /// Check whether this is the sentinel node.
  ///
  /// This requires sentinel tracking to be explicitly enabled.  Use the
  /// ilist_sentinel_tracking<true> option to get this API.
  bool isSentinel() const {
    static_assert(OptionsT::is_sentinel_tracking_explicit,
                  "Use ilist_sentinel_tracking<true> to enable isSentinel()");
    return node_base_type::isSentinel();
  }
};

/// An intrusive list node.
///
/// A base class to enable membership in intrusive lists, including \a
/// simple_ilist, \a iplist, and \a ilist.  The first template parameter is the
/// \a value_type for the list.
///
/// An ilist node can be configured with compile-time options to change
/// behaviour and/or add API.
///
/// By default, an \a ilist_node knows whether it is the list sentinel (an
/// instance of \a ilist_sentinel) if and only if
/// LLVM_ENABLE_ABI_BREAKING_CHECKS.  The function \a isKnownSentinel() always
/// returns \c false tracking is off.  Sentinel tracking steals a bit from the
/// "prev" link, which adds a mask operation when decrementing an iterator, but
/// enables bug-finding assertions in \a ilist_iterator.
///
/// To turn sentinel tracking on all the time, pass in the
/// ilist_sentinel_tracking<true> template parameter.  This also enables the \a
/// isSentinel() function.  The same option must be passed to the intrusive
/// list.  (ilist_sentinel_tracking<false> turns sentinel tracking off all the
/// time.)
///
/// A type can inherit from ilist_node multiple times by passing in different
/// \a ilist_tag options.  This allows a single instance to be inserted into
/// multiple lists simultaneously, where each list is given the same tag.
///
/// \example
/// struct A {};
/// struct B {};
/// struct N : ilist_node<N, ilist_tag<A>>, ilist_node<N, ilist_tag<B>> {};
///
/// void foo() {
///   simple_ilist<N, ilist_tag<A>> ListA;
///   simple_ilist<N, ilist_tag<B>> ListB;
///   N N1;
///   ListA.push_back(N1);
///   ListB.push_back(N1);
/// }
/// \endexample
///
/// See \a is_valid_option for steps on adding a new option.
template <class T, class... Options>
class ilist_node
    : public ilist_node_impl<
          typename ilist_detail::compute_node_options<T, Options...>::type> {
  static_assert(ilist_detail::check_options<Options...>::value,
                "Unrecognized node option!");
};

namespace ilist_detail {

/// An access class for ilist_node private API.
///
/// This gives access to the private parts of ilist nodes.  Nodes for an ilist
/// should friend this class if they inherit privately from ilist_node.
///
/// Using this class outside of the ilist implementation is unsupported.
struct NodeAccess {
protected:
  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getNodePtr(typename OptionsT::pointer N) {
    return N;
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getNodePtr(typename OptionsT::const_pointer N) {
    return N;
  }

  template <class OptionsT>
  static typename OptionsT::pointer getValuePtr(ilist_node_impl<OptionsT> *N) {
    return static_cast<typename OptionsT::pointer>(N);
  }

  template <class OptionsT>
  static typename OptionsT::const_pointer
  getValuePtr(const ilist_node_impl<OptionsT> *N) {
    return static_cast<typename OptionsT::const_pointer>(N);
  }

  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getPrev(ilist_node_impl<OptionsT> &N) {
    return N.getPrev();
  }

  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getNext(ilist_node_impl<OptionsT> &N) {
    return N.getNext();
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getPrev(const ilist_node_impl<OptionsT> &N) {
    return N.getPrev();
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getNext(const ilist_node_impl<OptionsT> &N) {
    return N.getNext();
  }
};

template <class OptionsT> struct SpecificNodeAccess : NodeAccess {
protected:
  using pointer = typename OptionsT::pointer;
  using const_pointer = typename OptionsT::const_pointer;
  using node_type = ilist_node_impl<OptionsT>;

  static node_type *getNodePtr(pointer N) {
    return NodeAccess::getNodePtr<OptionsT>(N);
  }

  static const node_type *getNodePtr(const_pointer N) {
    return NodeAccess::getNodePtr<OptionsT>(N);
  }

  static pointer getValuePtr(node_type *N) {
    return NodeAccess::getValuePtr<OptionsT>(N);
  }

  static const_pointer getValuePtr(const node_type *N) {
    return NodeAccess::getValuePtr<OptionsT>(N);
  }
};

} // end namespace ilist_detail

template <class OptionsT>
class ilist_sentinel : public ilist_node_impl<OptionsT> {
public:
  ilist_sentinel() {
    this->initializeSentinel();
    reset();
  }

  void reset() {
    this->setPrev(this);
    this->setNext(this);
  }

  bool empty() const { return this == this->getPrev(); }
};

/// An ilist node that can access its parent list.
///
/// Requires \c NodeTy to have \a getParent() to find the parent node, and the
/// \c ParentTy to have \a getSublistAccess() to get a reference to the list.
template <typename NodeTy, typename ParentTy, class... Options>
class ilist_node_with_parent : public ilist_node<NodeTy, Options...> {
protected:
  ilist_node_with_parent() = default;

private:
  /// Forward to NodeTy::getParent().
  ///
  /// Note: do not use the name "getParent()".  We want a compile error
  /// (instead of recursion) when the subclass fails to implement \a
  /// getParent().
  const ParentTy *getNodeParent() const {
    return static_cast<const NodeTy *>(this)->getParent();
  }

public:
  /// @name Adjacent Node Accessors
  /// @{
  /// Get the previous node, or \c nullptr for the list head.
  NodeTy *getPrevNode() {
    // Should be separated to a reused function, but then we couldn't use auto
    // (and would need the type of the list).
    const auto &List =
        getNodeParent()->*(ParentTy::getSublistAccess((NodeTy *)nullptr));
    return List.getPrevNode(*static_cast<NodeTy *>(this));
  }

  /// Get the previous node, or \c nullptr for the list head.
  const NodeTy *getPrevNode() const {
    return const_cast<ilist_node_with_parent *>(this)->getPrevNode();
  }

  /// Get the next node, or \c nullptr for the list tail.
  NodeTy *getNextNode() {
    // Should be separated to a reused function, but then we couldn't use auto
    // (and would need the type of the list).
    const auto &List =
        getNodeParent()->*(ParentTy::getSublistAccess((NodeTy *)nullptr));
    return List.getNextNode(*static_cast<NodeTy *>(this));
  }

  /// Get the next node, or \c nullptr for the list tail.
  const NodeTy *getNextNode() const {
    return const_cast<ilist_node_with_parent *>(this)->getNextNode();
  }
  /// @}
};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_H
