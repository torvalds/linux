//===- ArrayRef.h - Array Reference Wrapper ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ARRAYREF_H
#define LLVM_ADT_ARRAYREF_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

namespace llvm {

  /// ArrayRef - Represent a constant reference to an array (0 or more elements
  /// consecutively in memory), i.e. a start pointer and a length.  It allows
  /// various APIs to take consecutive elements easily and conveniently.
  ///
  /// This class does not own the underlying data, it is expected to be used in
  /// situations where the data resides in some other buffer, whose lifetime
  /// extends past that of the ArrayRef. For this reason, it is not in general
  /// safe to store an ArrayRef.
  ///
  /// This is intended to be trivially copyable, so it should be passed by
  /// value.
  template<typename T>
  class LLVM_NODISCARD ArrayRef {
  public:
    using iterator = const T *;
    using const_iterator = const T *;
    using size_type = size_t;
    using reverse_iterator = std::reverse_iterator<iterator>;

  private:
    /// The start of the array, in an external buffer.
    const T *Data = nullptr;

    /// The number of elements.
    size_type Length = 0;

  public:
    /// @name Constructors
    /// @{

    /// Construct an empty ArrayRef.
    /*implicit*/ ArrayRef() = default;

    /// Construct an empty ArrayRef from None.
    /*implicit*/ ArrayRef(NoneType) {}

    /// Construct an ArrayRef from a single element.
    /*implicit*/ ArrayRef(const T &OneElt)
      : Data(&OneElt), Length(1) {}

    /// Construct an ArrayRef from a pointer and length.
    /*implicit*/ ArrayRef(const T *data, size_t length)
      : Data(data), Length(length) {}

    /// Construct an ArrayRef from a range.
    ArrayRef(const T *begin, const T *end)
      : Data(begin), Length(end - begin) {}

    /// Construct an ArrayRef from a SmallVector. This is templated in order to
    /// avoid instantiating SmallVectorTemplateCommon<T> whenever we
    /// copy-construct an ArrayRef.
    template<typename U>
    /*implicit*/ ArrayRef(const SmallVectorTemplateCommon<T, U> &Vec)
      : Data(Vec.data()), Length(Vec.size()) {
    }

    /// Construct an ArrayRef from a std::vector.
    template<typename A>
    /*implicit*/ ArrayRef(const std::vector<T, A> &Vec)
      : Data(Vec.data()), Length(Vec.size()) {}

    /// Construct an ArrayRef from a std::array
    template <size_t N>
    /*implicit*/ constexpr ArrayRef(const std::array<T, N> &Arr)
        : Data(Arr.data()), Length(N) {}

    /// Construct an ArrayRef from a C array.
    template <size_t N>
    /*implicit*/ constexpr ArrayRef(const T (&Arr)[N]) : Data(Arr), Length(N) {}

    /// Construct an ArrayRef from a std::initializer_list.
    /*implicit*/ ArrayRef(const std::initializer_list<T> &Vec)
    : Data(Vec.begin() == Vec.end() ? (T*)nullptr : Vec.begin()),
      Length(Vec.size()) {}

    /// Construct an ArrayRef<const T*> from ArrayRef<T*>. This uses SFINAE to
    /// ensure that only ArrayRefs of pointers can be converted.
    template <typename U>
    ArrayRef(
        const ArrayRef<U *> &A,
        typename std::enable_if<
           std::is_convertible<U *const *, T const *>::value>::type * = nullptr)
      : Data(A.data()), Length(A.size()) {}

    /// Construct an ArrayRef<const T*> from a SmallVector<T*>. This is
    /// templated in order to avoid instantiating SmallVectorTemplateCommon<T>
    /// whenever we copy-construct an ArrayRef.
    template<typename U, typename DummyT>
    /*implicit*/ ArrayRef(
      const SmallVectorTemplateCommon<U *, DummyT> &Vec,
      typename std::enable_if<
          std::is_convertible<U *const *, T const *>::value>::type * = nullptr)
      : Data(Vec.data()), Length(Vec.size()) {
    }

    /// Construct an ArrayRef<const T*> from std::vector<T*>. This uses SFINAE
    /// to ensure that only vectors of pointers can be converted.
    template<typename U, typename A>
    ArrayRef(const std::vector<U *, A> &Vec,
             typename std::enable_if<
                 std::is_convertible<U *const *, T const *>::value>::type* = 0)
      : Data(Vec.data()), Length(Vec.size()) {}

    /// @}
    /// @name Simple Operations
    /// @{

    iterator begin() const { return Data; }
    iterator end() const { return Data + Length; }

    reverse_iterator rbegin() const { return reverse_iterator(end()); }
    reverse_iterator rend() const { return reverse_iterator(begin()); }

    /// empty - Check if the array is empty.
    bool empty() const { return Length == 0; }

    const T *data() const { return Data; }

    /// size - Get the array size.
    size_t size() const { return Length; }

    /// front - Get the first element.
    const T &front() const {
      assert(!empty());
      return Data[0];
    }

    /// back - Get the last element.
    const T &back() const {
      assert(!empty());
      return Data[Length-1];
    }

    // copy - Allocate copy in Allocator and return ArrayRef<T> to it.
    template <typename Allocator> ArrayRef<T> copy(Allocator &A) {
      T *Buff = A.template Allocate<T>(Length);
      std::uninitialized_copy(begin(), end(), Buff);
      return ArrayRef<T>(Buff, Length);
    }

    /// equals - Check for element-wise equality.
    bool equals(ArrayRef RHS) const {
      if (Length != RHS.Length)
        return false;
      return std::equal(begin(), end(), RHS.begin());
    }

    /// slice(n, m) - Chop off the first N elements of the array, and keep M
    /// elements in the array.
    ArrayRef<T> slice(size_t N, size_t M) const {
      assert(N+M <= size() && "Invalid specifier");
      return ArrayRef<T>(data()+N, M);
    }

    /// slice(n) - Chop off the first N elements of the array.
    ArrayRef<T> slice(size_t N) const { return slice(N, size() - N); }

    /// Drop the first \p N elements of the array.
    ArrayRef<T> drop_front(size_t N = 1) const {
      assert(size() >= N && "Dropping more elements than exist");
      return slice(N, size() - N);
    }

    /// Drop the last \p N elements of the array.
    ArrayRef<T> drop_back(size_t N = 1) const {
      assert(size() >= N && "Dropping more elements than exist");
      return slice(0, size() - N);
    }

    /// Return a copy of *this with the first N elements satisfying the
    /// given predicate removed.
    template <class PredicateT> ArrayRef<T> drop_while(PredicateT Pred) const {
      return ArrayRef<T>(find_if_not(*this, Pred), end());
    }

    /// Return a copy of *this with the first N elements not satisfying
    /// the given predicate removed.
    template <class PredicateT> ArrayRef<T> drop_until(PredicateT Pred) const {
      return ArrayRef<T>(find_if(*this, Pred), end());
    }

    /// Return a copy of *this with only the first \p N elements.
    ArrayRef<T> take_front(size_t N = 1) const {
      if (N >= size())
        return *this;
      return drop_back(size() - N);
    }

    /// Return a copy of *this with only the last \p N elements.
    ArrayRef<T> take_back(size_t N = 1) const {
      if (N >= size())
        return *this;
      return drop_front(size() - N);
    }

    /// Return the first N elements of this Array that satisfy the given
    /// predicate.
    template <class PredicateT> ArrayRef<T> take_while(PredicateT Pred) const {
      return ArrayRef<T>(begin(), find_if_not(*this, Pred));
    }

    /// Return the first N elements of this Array that don't satisfy the
    /// given predicate.
    template <class PredicateT> ArrayRef<T> take_until(PredicateT Pred) const {
      return ArrayRef<T>(begin(), find_if(*this, Pred));
    }

    /// @}
    /// @name Operator Overloads
    /// @{
    const T &operator[](size_t Index) const {
      assert(Index < Length && "Invalid index!");
      return Data[Index];
    }

    /// Disallow accidental assignment from a temporary.
    ///
    /// The declaration here is extra complicated so that "arrayRef = {}"
    /// continues to select the move assignment operator.
    template <typename U>
    typename std::enable_if<std::is_same<U, T>::value, ArrayRef<T>>::type &
    operator=(U &&Temporary) = delete;

    /// Disallow accidental assignment from a temporary.
    ///
    /// The declaration here is extra complicated so that "arrayRef = {}"
    /// continues to select the move assignment operator.
    template <typename U>
    typename std::enable_if<std::is_same<U, T>::value, ArrayRef<T>>::type &
    operator=(std::initializer_list<U>) = delete;

    /// @}
    /// @name Expensive Operations
    /// @{
    std::vector<T> vec() const {
      return std::vector<T>(Data, Data+Length);
    }

    /// @}
    /// @name Conversion operators
    /// @{
    operator std::vector<T>() const {
      return std::vector<T>(Data, Data+Length);
    }

    /// @}
  };

  /// MutableArrayRef - Represent a mutable reference to an array (0 or more
  /// elements consecutively in memory), i.e. a start pointer and a length.  It
  /// allows various APIs to take and modify consecutive elements easily and
  /// conveniently.
  ///
  /// This class does not own the underlying data, it is expected to be used in
  /// situations where the data resides in some other buffer, whose lifetime
  /// extends past that of the MutableArrayRef. For this reason, it is not in
  /// general safe to store a MutableArrayRef.
  ///
  /// This is intended to be trivially copyable, so it should be passed by
  /// value.
  template<typename T>
  class LLVM_NODISCARD MutableArrayRef : public ArrayRef<T> {
  public:
    using iterator = T *;
    using reverse_iterator = std::reverse_iterator<iterator>;

    /// Construct an empty MutableArrayRef.
    /*implicit*/ MutableArrayRef() = default;

    /// Construct an empty MutableArrayRef from None.
    /*implicit*/ MutableArrayRef(NoneType) : ArrayRef<T>() {}

    /// Construct an MutableArrayRef from a single element.
    /*implicit*/ MutableArrayRef(T &OneElt) : ArrayRef<T>(OneElt) {}

    /// Construct an MutableArrayRef from a pointer and length.
    /*implicit*/ MutableArrayRef(T *data, size_t length)
      : ArrayRef<T>(data, length) {}

    /// Construct an MutableArrayRef from a range.
    MutableArrayRef(T *begin, T *end) : ArrayRef<T>(begin, end) {}

    /// Construct an MutableArrayRef from a SmallVector.
    /*implicit*/ MutableArrayRef(SmallVectorImpl<T> &Vec)
    : ArrayRef<T>(Vec) {}

    /// Construct a MutableArrayRef from a std::vector.
    /*implicit*/ MutableArrayRef(std::vector<T> &Vec)
    : ArrayRef<T>(Vec) {}

    /// Construct an ArrayRef from a std::array
    template <size_t N>
    /*implicit*/ constexpr MutableArrayRef(std::array<T, N> &Arr)
        : ArrayRef<T>(Arr) {}

    /// Construct an MutableArrayRef from a C array.
    template <size_t N>
    /*implicit*/ constexpr MutableArrayRef(T (&Arr)[N]) : ArrayRef<T>(Arr) {}

    T *data() const { return const_cast<T*>(ArrayRef<T>::data()); }

    iterator begin() const { return data(); }
    iterator end() const { return data() + this->size(); }

    reverse_iterator rbegin() const { return reverse_iterator(end()); }
    reverse_iterator rend() const { return reverse_iterator(begin()); }

    /// front - Get the first element.
    T &front() const {
      assert(!this->empty());
      return data()[0];
    }

    /// back - Get the last element.
    T &back() const {
      assert(!this->empty());
      return data()[this->size()-1];
    }

    /// slice(n, m) - Chop off the first N elements of the array, and keep M
    /// elements in the array.
    MutableArrayRef<T> slice(size_t N, size_t M) const {
      assert(N + M <= this->size() && "Invalid specifier");
      return MutableArrayRef<T>(this->data() + N, M);
    }

    /// slice(n) - Chop off the first N elements of the array.
    MutableArrayRef<T> slice(size_t N) const {
      return slice(N, this->size() - N);
    }

    /// Drop the first \p N elements of the array.
    MutableArrayRef<T> drop_front(size_t N = 1) const {
      assert(this->size() >= N && "Dropping more elements than exist");
      return slice(N, this->size() - N);
    }

    MutableArrayRef<T> drop_back(size_t N = 1) const {
      assert(this->size() >= N && "Dropping more elements than exist");
      return slice(0, this->size() - N);
    }

    /// Return a copy of *this with the first N elements satisfying the
    /// given predicate removed.
    template <class PredicateT>
    MutableArrayRef<T> drop_while(PredicateT Pred) const {
      return MutableArrayRef<T>(find_if_not(*this, Pred), end());
    }

    /// Return a copy of *this with the first N elements not satisfying
    /// the given predicate removed.
    template <class PredicateT>
    MutableArrayRef<T> drop_until(PredicateT Pred) const {
      return MutableArrayRef<T>(find_if(*this, Pred), end());
    }

    /// Return a copy of *this with only the first \p N elements.
    MutableArrayRef<T> take_front(size_t N = 1) const {
      if (N >= this->size())
        return *this;
      return drop_back(this->size() - N);
    }

    /// Return a copy of *this with only the last \p N elements.
    MutableArrayRef<T> take_back(size_t N = 1) const {
      if (N >= this->size())
        return *this;
      return drop_front(this->size() - N);
    }

    /// Return the first N elements of this Array that satisfy the given
    /// predicate.
    template <class PredicateT>
    MutableArrayRef<T> take_while(PredicateT Pred) const {
      return MutableArrayRef<T>(begin(), find_if_not(*this, Pred));
    }

    /// Return the first N elements of this Array that don't satisfy the
    /// given predicate.
    template <class PredicateT>
    MutableArrayRef<T> take_until(PredicateT Pred) const {
      return MutableArrayRef<T>(begin(), find_if(*this, Pred));
    }

    /// @}
    /// @name Operator Overloads
    /// @{
    T &operator[](size_t Index) const {
      assert(Index < this->size() && "Invalid index!");
      return data()[Index];
    }
  };

  /// This is a MutableArrayRef that owns its array.
  template <typename T> class OwningArrayRef : public MutableArrayRef<T> {
  public:
    OwningArrayRef() = default;
    OwningArrayRef(size_t Size) : MutableArrayRef<T>(new T[Size], Size) {}

    OwningArrayRef(ArrayRef<T> Data)
        : MutableArrayRef<T>(new T[Data.size()], Data.size()) {
      std::copy(Data.begin(), Data.end(), this->begin());
    }

    OwningArrayRef(OwningArrayRef &&Other) { *this = Other; }

    OwningArrayRef &operator=(OwningArrayRef &&Other) {
      delete[] this->data();
      this->MutableArrayRef<T>::operator=(Other);
      Other.MutableArrayRef<T>::operator=(MutableArrayRef<T>());
      return *this;
    }

    ~OwningArrayRef() { delete[] this->data(); }
  };

  /// @name ArrayRef Convenience constructors
  /// @{

  /// Construct an ArrayRef from a single element.
  template<typename T>
  ArrayRef<T> makeArrayRef(const T &OneElt) {
    return OneElt;
  }

  /// Construct an ArrayRef from a pointer and length.
  template<typename T>
  ArrayRef<T> makeArrayRef(const T *data, size_t length) {
    return ArrayRef<T>(data, length);
  }

  /// Construct an ArrayRef from a range.
  template<typename T>
  ArrayRef<T> makeArrayRef(const T *begin, const T *end) {
    return ArrayRef<T>(begin, end);
  }

  /// Construct an ArrayRef from a SmallVector.
  template <typename T>
  ArrayRef<T> makeArrayRef(const SmallVectorImpl<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a SmallVector.
  template <typename T, unsigned N>
  ArrayRef<T> makeArrayRef(const SmallVector<T, N> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a std::vector.
  template<typename T>
  ArrayRef<T> makeArrayRef(const std::vector<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from an ArrayRef (no-op) (const)
  template <typename T> ArrayRef<T> makeArrayRef(const ArrayRef<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from an ArrayRef (no-op)
  template <typename T> ArrayRef<T> &makeArrayRef(ArrayRef<T> &Vec) {
    return Vec;
  }

  /// Construct an ArrayRef from a C array.
  template<typename T, size_t N>
  ArrayRef<T> makeArrayRef(const T (&Arr)[N]) {
    return ArrayRef<T>(Arr);
  }

  /// Construct a MutableArrayRef from a single element.
  template<typename T>
  MutableArrayRef<T> makeMutableArrayRef(T &OneElt) {
    return OneElt;
  }

  /// Construct a MutableArrayRef from a pointer and length.
  template<typename T>
  MutableArrayRef<T> makeMutableArrayRef(T *data, size_t length) {
    return MutableArrayRef<T>(data, length);
  }

  /// @}
  /// @name ArrayRef Comparison Operators
  /// @{

  template<typename T>
  inline bool operator==(ArrayRef<T> LHS, ArrayRef<T> RHS) {
    return LHS.equals(RHS);
  }

  template<typename T>
  inline bool operator!=(ArrayRef<T> LHS, ArrayRef<T> RHS) {
    return !(LHS == RHS);
  }

  /// @}

  // ArrayRefs can be treated like a POD type.
  template <typename T> struct isPodLike;
  template <typename T> struct isPodLike<ArrayRef<T>> {
    static const bool value = true;
  };

  template <typename T> hash_code hash_value(ArrayRef<T> S) {
    return hash_combine_range(S.begin(), S.end());
  }

} // end namespace llvm

#endif // LLVM_ADT_ARRAYREF_H
