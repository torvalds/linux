// Copyright 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// Google Mock - a framework for writing C++ mock classes.
//
// This file implements some commonly used actions.

// GOOGLETEST_CM0002 DO NOT DELETE

#ifndef GMOCK_INCLUDE_GMOCK_GMOCK_ACTIONS_H_
#define GMOCK_INCLUDE_GMOCK_GMOCK_ACTIONS_H_

#ifndef _WIN32_WCE
# include <errno.h>
#endif

#include <algorithm>
#include <string>

#include "gmock/internal/gmock-internal-utils.h"
#include "gmock/internal/gmock-port.h"

#if GTEST_LANG_CXX11  // Defined by gtest-port.h via gmock-port.h.
#include <functional>
#include <type_traits>
#endif  // GTEST_LANG_CXX11

namespace testing {

// To implement an action Foo, define:
//   1. a class FooAction that implements the ActionInterface interface, and
//   2. a factory function that creates an Action object from a
//      const FooAction*.
//
// The two-level delegation design follows that of Matcher, providing
// consistency for extension developers.  It also eases ownership
// management as Action objects can now be copied like plain values.

namespace internal {

template <typename F1, typename F2>
class ActionAdaptor;

// BuiltInDefaultValueGetter<T, true>::Get() returns a
// default-constructed T value.  BuiltInDefaultValueGetter<T,
// false>::Get() crashes with an error.
//
// This primary template is used when kDefaultConstructible is true.
template <typename T, bool kDefaultConstructible>
struct BuiltInDefaultValueGetter {
  static T Get() { return T(); }
};
template <typename T>
struct BuiltInDefaultValueGetter<T, false> {
  static T Get() {
    Assert(false, __FILE__, __LINE__,
           "Default action undefined for the function return type.");
    return internal::Invalid<T>();
    // The above statement will never be reached, but is required in
    // order for this function to compile.
  }
};

// BuiltInDefaultValue<T>::Get() returns the "built-in" default value
// for type T, which is NULL when T is a raw pointer type, 0 when T is
// a numeric type, false when T is bool, or "" when T is string or
// std::string.  In addition, in C++11 and above, it turns a
// default-constructed T value if T is default constructible.  For any
// other type T, the built-in default T value is undefined, and the
// function will abort the process.
template <typename T>
class BuiltInDefaultValue {
 public:
#if GTEST_LANG_CXX11
  // This function returns true iff type T has a built-in default value.
  static bool Exists() {
    return ::std::is_default_constructible<T>::value;
  }

  static T Get() {
    return BuiltInDefaultValueGetter<
        T, ::std::is_default_constructible<T>::value>::Get();
  }

#else  // GTEST_LANG_CXX11
  // This function returns true iff type T has a built-in default value.
  static bool Exists() {
    return false;
  }

  static T Get() {
    return BuiltInDefaultValueGetter<T, false>::Get();
  }

#endif  // GTEST_LANG_CXX11
};

// This partial specialization says that we use the same built-in
// default value for T and const T.
template <typename T>
class BuiltInDefaultValue<const T> {
 public:
  static bool Exists() { return BuiltInDefaultValue<T>::Exists(); }
  static T Get() { return BuiltInDefaultValue<T>::Get(); }
};

// This partial specialization defines the default values for pointer
// types.
template <typename T>
class BuiltInDefaultValue<T*> {
 public:
  static bool Exists() { return true; }
  static T* Get() { return NULL; }
};

// The following specializations define the default values for
// specific types we care about.
#define GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(type, value) \
  template <> \
  class BuiltInDefaultValue<type> { \
   public: \
    static bool Exists() { return true; } \
    static type Get() { return value; } \
  }

GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(void, );  // NOLINT
#if GTEST_HAS_GLOBAL_STRING
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(::string, "");
#endif  // GTEST_HAS_GLOBAL_STRING
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(::std::string, "");
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(bool, false);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(unsigned char, '\0');
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(signed char, '\0');
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(char, '\0');

// There's no need for a default action for signed wchar_t, as that
// type is the same as wchar_t for gcc, and invalid for MSVC.
//
// There's also no need for a default action for unsigned wchar_t, as
// that type is the same as unsigned int for gcc, and invalid for
// MSVC.
#if GMOCK_WCHAR_T_IS_NATIVE_
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(wchar_t, 0U);  // NOLINT
#endif

GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(unsigned short, 0U);  // NOLINT
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(signed short, 0);     // NOLINT
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(unsigned int, 0U);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(signed int, 0);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(unsigned long, 0UL);  // NOLINT
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(signed long, 0L);     // NOLINT
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(UInt64, 0);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(Int64, 0);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(float, 0);
GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_(double, 0);

#undef GMOCK_DEFINE_DEFAULT_ACTION_FOR_RETURN_TYPE_

}  // namespace internal

// When an unexpected function call is encountered, Google Mock will
// let it return a default value if the user has specified one for its
// return type, or if the return type has a built-in default value;
// otherwise Google Mock won't know what value to return and will have
// to abort the process.
//
// The DefaultValue<T> class allows a user to specify the
// default value for a type T that is both copyable and publicly
// destructible (i.e. anything that can be used as a function return
// type).  The usage is:
//
//   // Sets the default value for type T to be foo.
//   DefaultValue<T>::Set(foo);
template <typename T>
class DefaultValue {
 public:
  // Sets the default value for type T; requires T to be
  // copy-constructable and have a public destructor.
  static void Set(T x) {
    delete producer_;
    producer_ = new FixedValueProducer(x);
  }

  // Provides a factory function to be called to generate the default value.
  // This method can be used even if T is only move-constructible, but it is not
  // limited to that case.
  typedef T (*FactoryFunction)();
  static void SetFactory(FactoryFunction factory) {
    delete producer_;
    producer_ = new FactoryValueProducer(factory);
  }

  // Unsets the default value for type T.
  static void Clear() {
    delete producer_;
    producer_ = NULL;
  }

  // Returns true iff the user has set the default value for type T.
  static bool IsSet() { return producer_ != NULL; }

  // Returns true if T has a default return value set by the user or there
  // exists a built-in default value.
  static bool Exists() {
    return IsSet() || internal::BuiltInDefaultValue<T>::Exists();
  }

  // Returns the default value for type T if the user has set one;
  // otherwise returns the built-in default value. Requires that Exists()
  // is true, which ensures that the return value is well-defined.
  static T Get() {
    return producer_ == NULL ?
        internal::BuiltInDefaultValue<T>::Get() : producer_->Produce();
  }

 private:
  class ValueProducer {
   public:
    virtual ~ValueProducer() {}
    virtual T Produce() = 0;
  };

  class FixedValueProducer : public ValueProducer {
   public:
    explicit FixedValueProducer(T value) : value_(value) {}
    virtual T Produce() { return value_; }

   private:
    const T value_;
    GTEST_DISALLOW_COPY_AND_ASSIGN_(FixedValueProducer);
  };

  class FactoryValueProducer : public ValueProducer {
   public:
    explicit FactoryValueProducer(FactoryFunction factory)
        : factory_(factory) {}
    virtual T Produce() { return factory_(); }

   private:
    const FactoryFunction factory_;
    GTEST_DISALLOW_COPY_AND_ASSIGN_(FactoryValueProducer);
  };

  static ValueProducer* producer_;
};

// This partial specialization allows a user to set default values for
// reference types.
template <typename T>
class DefaultValue<T&> {
 public:
  // Sets the default value for type T&.
  static void Set(T& x) {  // NOLINT
    address_ = &x;
  }

  // Unsets the default value for type T&.
  static void Clear() {
    address_ = NULL;
  }

  // Returns true iff the user has set the default value for type T&.
  static bool IsSet() { return address_ != NULL; }

  // Returns true if T has a default return value set by the user or there
  // exists a built-in default value.
  static bool Exists() {
    return IsSet() || internal::BuiltInDefaultValue<T&>::Exists();
  }

  // Returns the default value for type T& if the user has set one;
  // otherwise returns the built-in default value if there is one;
  // otherwise aborts the process.
  static T& Get() {
    return address_ == NULL ?
        internal::BuiltInDefaultValue<T&>::Get() : *address_;
  }

 private:
  static T* address_;
};

// This specialization allows DefaultValue<void>::Get() to
// compile.
template <>
class DefaultValue<void> {
 public:
  static bool Exists() { return true; }
  static void Get() {}
};

// Points to the user-set default value for type T.
template <typename T>
typename DefaultValue<T>::ValueProducer* DefaultValue<T>::producer_ = NULL;

// Points to the user-set default value for type T&.
template <typename T>
T* DefaultValue<T&>::address_ = NULL;

// Implement this interface to define an action for function type F.
template <typename F>
class ActionInterface {
 public:
  typedef typename internal::Function<F>::Result Result;
  typedef typename internal::Function<F>::ArgumentTuple ArgumentTuple;

  ActionInterface() {}
  virtual ~ActionInterface() {}

  // Performs the action.  This method is not const, as in general an
  // action can have side effects and be stateful.  For example, a
  // get-the-next-element-from-the-collection action will need to
  // remember the current element.
  virtual Result Perform(const ArgumentTuple& args) = 0;

 private:
  GTEST_DISALLOW_COPY_AND_ASSIGN_(ActionInterface);
};

// An Action<F> is a copyable and IMMUTABLE (except by assignment)
// object that represents an action to be taken when a mock function
// of type F is called.  The implementation of Action<T> is just a
// linked_ptr to const ActionInterface<T>, so copying is fairly cheap.
// Don't inherit from Action!
//
// You can view an object implementing ActionInterface<F> as a
// concrete action (including its current state), and an Action<F>
// object as a handle to it.
template <typename F>
class Action {
 public:
  typedef typename internal::Function<F>::Result Result;
  typedef typename internal::Function<F>::ArgumentTuple ArgumentTuple;

  // Constructs a null Action.  Needed for storing Action objects in
  // STL containers.
  Action() {}

#if GTEST_LANG_CXX11
  // Construct an Action from a specified callable.
  // This cannot take std::function directly, because then Action would not be
  // directly constructible from lambda (it would require two conversions).
  template <typename G,
            typename = typename ::std::enable_if<
                ::std::is_constructible<::std::function<F>, G>::value>::type>
  Action(G&& fun) : fun_(::std::forward<G>(fun)) {}  // NOLINT
#endif

  // Constructs an Action from its implementation.
  explicit Action(ActionInterface<F>* impl) : impl_(impl) {}

  // This constructor allows us to turn an Action<Func> object into an
  // Action<F>, as long as F's arguments can be implicitly converted
  // to Func's and Func's return type can be implicitly converted to
  // F's.
  template <typename Func>
  explicit Action(const Action<Func>& action);

  // Returns true iff this is the DoDefault() action.
  bool IsDoDefault() const {
#if GTEST_LANG_CXX11
    return impl_ == nullptr && fun_ == nullptr;
#else
    return impl_ == NULL;
#endif
  }

  // Performs the action.  Note that this method is const even though
  // the corresponding method in ActionInterface is not.  The reason
  // is that a const Action<F> means that it cannot be re-bound to
  // another concrete action, not that the concrete action it binds to
  // cannot change state.  (Think of the difference between a const
  // pointer and a pointer to const.)
  Result Perform(ArgumentTuple args) const {
    if (IsDoDefault()) {
      internal::IllegalDoDefault(__FILE__, __LINE__);
    }
#if GTEST_LANG_CXX11
    if (fun_ != nullptr) {
      return internal::Apply(fun_, ::std::move(args));
    }
#endif
    return impl_->Perform(args);
  }

 private:
  template <typename F1, typename F2>
  friend class internal::ActionAdaptor;

  template <typename G>
  friend class Action;

  // In C++11, Action can be implemented either as a generic functor (through
  // std::function), or legacy ActionInterface. In C++98, only ActionInterface
  // is available. The invariants are as follows:
  // * in C++98, impl_ is null iff this is the default action
  // * in C++11, at most one of fun_ & impl_ may be nonnull; both are null iff
  //   this is the default action
#if GTEST_LANG_CXX11
  ::std::function<F> fun_;
#endif
  internal::linked_ptr<ActionInterface<F> > impl_;
};

// The PolymorphicAction class template makes it easy to implement a
// polymorphic action (i.e. an action that can be used in mock
// functions of than one type, e.g. Return()).
//
// To define a polymorphic action, a user first provides a COPYABLE
// implementation class that has a Perform() method template:
//
//   class FooAction {
//    public:
//     template <typename Result, typename ArgumentTuple>
//     Result Perform(const ArgumentTuple& args) const {
//       // Processes the arguments and returns a result, using
//       // tr1::get<N>(args) to get the N-th (0-based) argument in the tuple.
//     }
//     ...
//   };
//
// Then the user creates the polymorphic action using
// MakePolymorphicAction(object) where object has type FooAction.  See
// the definition of Return(void) and SetArgumentPointee<N>(value) for
// complete examples.
template <typename Impl>
class PolymorphicAction {
 public:
  explicit PolymorphicAction(const Impl& impl) : impl_(impl) {}

  template <typename F>
  operator Action<F>() const {
    return Action<F>(new MonomorphicImpl<F>(impl_));
  }

 private:
  template <typename F>
  class MonomorphicImpl : public ActionInterface<F> {
   public:
    typedef typename internal::Function<F>::Result Result;
    typedef typename internal::Function<F>::ArgumentTuple ArgumentTuple;

    explicit MonomorphicImpl(const Impl& impl) : impl_(impl) {}

    virtual Result Perform(const ArgumentTuple& args) {
      return impl_.template Perform<Result>(args);
    }

   private:
    Impl impl_;

    GTEST_DISALLOW_ASSIGN_(MonomorphicImpl);
  };

  Impl impl_;

  GTEST_DISALLOW_ASSIGN_(PolymorphicAction);
};

// Creates an Action from its implementation and returns it.  The
// created Action object owns the implementation.
template <typename F>
Action<F> MakeAction(ActionInterface<F>* impl) {
  return Action<F>(impl);
}

// Creates a polymorphic action from its implementation.  This is
// easier to use than the PolymorphicAction<Impl> constructor as it
// doesn't require you to explicitly write the template argument, e.g.
//
//   MakePolymorphicAction(foo);
// vs
//   PolymorphicAction<TypeOfFoo>(foo);
template <typename Impl>
inline PolymorphicAction<Impl> MakePolymorphicAction(const Impl& impl) {
  return PolymorphicAction<Impl>(impl);
}

namespace internal {

// Allows an Action<F2> object to pose as an Action<F1>, as long as F2
// and F1 are compatible.
template <typename F1, typename F2>
class ActionAdaptor : public ActionInterface<F1> {
 public:
  typedef typename internal::Function<F1>::Result Result;
  typedef typename internal::Function<F1>::ArgumentTuple ArgumentTuple;

  explicit ActionAdaptor(const Action<F2>& from) : impl_(from.impl_) {}

  virtual Result Perform(const ArgumentTuple& args) {
    return impl_->Perform(args);
  }

 private:
  const internal::linked_ptr<ActionInterface<F2> > impl_;

  GTEST_DISALLOW_ASSIGN_(ActionAdaptor);
};

// Helper struct to specialize ReturnAction to execute a move instead of a copy
// on return. Useful for move-only types, but could be used on any type.
template <typename T>
struct ByMoveWrapper {
  explicit ByMoveWrapper(T value) : payload(internal::move(value)) {}
  T payload;
};

// Implements the polymorphic Return(x) action, which can be used in
// any function that returns the type of x, regardless of the argument
// types.
//
// Note: The value passed into Return must be converted into
// Function<F>::Result when this action is cast to Action<F> rather than
// when that action is performed. This is important in scenarios like
//
// MOCK_METHOD1(Method, T(U));
// ...
// {
//   Foo foo;
//   X x(&foo);
//   EXPECT_CALL(mock, Method(_)).WillOnce(Return(x));
// }
//
// In the example above the variable x holds reference to foo which leaves
// scope and gets destroyed.  If copying X just copies a reference to foo,
// that copy will be left with a hanging reference.  If conversion to T
// makes a copy of foo, the above code is safe. To support that scenario, we
// need to make sure that the type conversion happens inside the EXPECT_CALL
// statement, and conversion of the result of Return to Action<T(U)> is a
// good place for that.
//
// The real life example of the above scenario happens when an invocation
// of gtl::Container() is passed into Return.
//
template <typename R>
class ReturnAction {
 public:
  // Constructs a ReturnAction object from the value to be returned.
  // 'value' is passed by value instead of by const reference in order
  // to allow Return("string literal") to compile.
  explicit ReturnAction(R value) : value_(new R(internal::move(value))) {}

  // This template type conversion operator allows Return(x) to be
  // used in ANY function that returns x's type.
  template <typename F>
  operator Action<F>() const {
    // Assert statement belongs here because this is the best place to verify
    // conditions on F. It produces the clearest error messages
    // in most compilers.
    // Impl really belongs in this scope as a local class but can't
    // because MSVC produces duplicate symbols in different translation units
    // in this case. Until MS fixes that bug we put Impl into the class scope
    // and put the typedef both here (for use in assert statement) and
    // in the Impl class. But both definitions must be the same.
    typedef typename Function<F>::Result Result;
    GTEST_COMPILE_ASSERT_(
        !is_reference<Result>::value,
        use_ReturnRef_instead_of_Return_to_return_a_reference);
    return Action<F>(new Impl<R, F>(value_));
  }

 private:
  // Implements the Return(x) action for a particular function type F.
  template <typename R_, typename F>
  class Impl : public ActionInterface<F> {
   public:
    typedef typename Function<F>::Result Result;
    typedef typename Function<F>::ArgumentTuple ArgumentTuple;

    // The implicit cast is necessary when Result has more than one
    // single-argument constructor (e.g. Result is std::vector<int>) and R
    // has a type conversion operator template.  In that case, value_(value)
    // won't compile as the compiler doesn't known which constructor of
    // Result to call.  ImplicitCast_ forces the compiler to convert R to
    // Result without considering explicit constructors, thus resolving the
    // ambiguity. value_ is then initialized using its copy constructor.
    explicit Impl(const linked_ptr<R>& value)
        : value_before_cast_(*value),
          value_(ImplicitCast_<Result>(value_before_cast_)) {}

    virtual Result Perform(const ArgumentTuple&) { return value_; }

   private:
    GTEST_COMPILE_ASSERT_(!is_reference<Result>::value,
                          Result_cannot_be_a_reference_type);
    // We save the value before casting just in case it is being cast to a
    // wrapper type.
    R value_before_cast_;
    Result value_;

    GTEST_DISALLOW_COPY_AND_ASSIGN_(Impl);
  };

  // Partially specialize for ByMoveWrapper. This version of ReturnAction will
  // move its contents instead.
  template <typename R_, typename F>
  class Impl<ByMoveWrapper<R_>, F> : public ActionInterface<F> {
   public:
    typedef typename Function<F>::Result Result;
    typedef typename Function<F>::ArgumentTuple ArgumentTuple;

    explicit Impl(const linked_ptr<R>& wrapper)
        : performed_(false), wrapper_(wrapper) {}

    virtual Result Perform(const ArgumentTuple&) {
      GTEST_CHECK_(!performed_)
          << "A ByMove() action should only be performed once.";
      performed_ = true;
      return internal::move(wrapper_->payload);
    }

   private:
    bool performed_;
    const linked_ptr<R> wrapper_;

    GTEST_DISALLOW_ASSIGN_(Impl);
  };

  const linked_ptr<R> value_;

  GTEST_DISALLOW_ASSIGN_(ReturnAction);
};

// Implements the ReturnNull() action.
class ReturnNullAction {
 public:
  // Allows ReturnNull() to be used in any pointer-returning function. In C++11
  // this is enforced by returning nullptr, and in non-C++11 by asserting a
  // pointer type on compile time.
  template <typename Result, typename ArgumentTuple>
  static Result Perform(const ArgumentTuple&) {
#if GTEST_LANG_CXX11
    return nullptr;
#else
    GTEST_COMPILE_ASSERT_(internal::is_pointer<Result>::value,
                          ReturnNull_can_be_used_to_return_a_pointer_only);
    return NULL;
#endif  // GTEST_LANG_CXX11
  }
};

// Implements the Return() action.
class ReturnVoidAction {
 public:
  // Allows Return() to be used in any void-returning function.
  template <typename Result, typename ArgumentTuple>
  static void Perform(const ArgumentTuple&) {
    CompileAssertTypesEqual<void, Result>();
  }
};

// Implements the polymorphic ReturnRef(x) action, which can be used
// in any function that returns a reference to the type of x,
// regardless of the argument types.
template <typename T>
class ReturnRefAction {
 public:
  // Constructs a ReturnRefAction object from the reference to be returned.
  explicit ReturnRefAction(T& ref) : ref_(ref) {}  // NOLINT

  // This template type conversion operator allows ReturnRef(x) to be
  // used in ANY function that returns a reference to x's type.
  template <typename F>
  operator Action<F>() const {
    typedef typename Function<F>::Result Result;
    // Asserts that the function return type is a reference.  This
    // catches the user error of using ReturnRef(x) when Return(x)
    // should be used, and generates some helpful error message.
    GTEST_COMPILE_ASSERT_(internal::is_reference<Result>::value,
                          use_Return_instead_of_ReturnRef_to_return_a_value);
    return Action<F>(new Impl<F>(ref_));
  }

 private:
  // Implements the ReturnRef(x) action for a particular function type F.
  template <typename F>
  class Impl : public ActionInterface<F> {
   public:
    typedef typename Function<F>::Result Result;
    typedef typename Function<F>::ArgumentTuple ArgumentTuple;

    explicit Impl(T& ref) : ref_(ref) {}  // NOLINT

    virtual Result Perform(const ArgumentTuple&) {
      return ref_;
    }

   private:
    T& ref_;

    GTEST_DISALLOW_ASSIGN_(Impl);
  };

  T& ref_;

  GTEST_DISALLOW_ASSIGN_(ReturnRefAction);
};

// Implements the polymorphic ReturnRefOfCopy(x) action, which can be
// used in any function that returns a reference to the type of x,
// regardless of the argument types.
template <typename T>
class ReturnRefOfCopyAction {
 public:
  // Constructs a ReturnRefOfCopyAction object from the reference to
  // be returned.
  explicit ReturnRefOfCopyAction(const T& value) : value_(value) {}  // NOLINT

  // This template type conversion operator allows ReturnRefOfCopy(x) to be
  // used in ANY function that returns a reference to x's type.
  template <typename F>
  operator Action<F>() const {
    typedef typename Function<F>::Result Result;
    // Asserts that the function return type is a reference.  This
    // catches the user error of using ReturnRefOfCopy(x) when Return(x)
    // should be used, and generates some helpful error message.
    GTEST_COMPILE_ASSERT_(
        internal::is_reference<Result>::value,
        use_Return_instead_of_ReturnRefOfCopy_to_return_a_value);
    return Action<F>(new Impl<F>(value_));
  }

 private:
  // Implements the ReturnRefOfCopy(x) action for a particular function type F.
  template <typename F>
  class Impl : public ActionInterface<F> {
   public:
    typedef typename Function<F>::Result Result;
    typedef typename Function<F>::ArgumentTuple ArgumentTuple;

    explicit Impl(const T& value) : value_(value) {}  // NOLINT

    virtual Result Perform(const ArgumentTuple&) {
      return value_;
    }

   private:
    T value_;

    GTEST_DISALLOW_ASSIGN_(Impl);
  };

  const T value_;

  GTEST_DISALLOW_ASSIGN_(ReturnRefOfCopyAction);
};

// Implements the polymorphic DoDefault() action.
class DoDefaultAction {
 public:
  // This template type conversion operator allows DoDefault() to be
  // used in any function.
  template <typename F>
  operator Action<F>() const { return Action<F>(); }  // NOLINT
};

// Implements the Assign action to set a given pointer referent to a
// particular value.
template <typename T1, typename T2>
class AssignAction {
 public:
  AssignAction(T1* ptr, T2 value) : ptr_(ptr), value_(value) {}

  template <typename Result, typename ArgumentTuple>
  void Perform(const ArgumentTuple& /* args */) const {
    *ptr_ = value_;
  }

 private:
  T1* const ptr_;
  const T2 value_;

  GTEST_DISALLOW_ASSIGN_(AssignAction);
};

#if !GTEST_OS_WINDOWS_MOBILE

// Implements the SetErrnoAndReturn action to simulate return from
// various system calls and libc functions.
template <typename T>
class SetErrnoAndReturnAction {
 public:
  SetErrnoAndReturnAction(int errno_value, T result)
      : errno_(errno_value),
        result_(result) {}
  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple& /* args */) const {
    errno = errno_;
    return result_;
  }

 private:
  const int errno_;
  const T result_;

  GTEST_DISALLOW_ASSIGN_(SetErrnoAndReturnAction);
};

#endif  // !GTEST_OS_WINDOWS_MOBILE

// Implements the SetArgumentPointee<N>(x) action for any function
// whose N-th argument (0-based) is a pointer to x's type.  The
// template parameter kIsProto is true iff type A is ProtocolMessage,
// proto2::Message, or a sub-class of those.
template <size_t N, typename A, bool kIsProto>
class SetArgumentPointeeAction {
 public:
  // Constructs an action that sets the variable pointed to by the
  // N-th function argument to 'value'.
  explicit SetArgumentPointeeAction(const A& value) : value_(value) {}

  template <typename Result, typename ArgumentTuple>
  void Perform(const ArgumentTuple& args) const {
    CompileAssertTypesEqual<void, Result>();
    *::testing::get<N>(args) = value_;
  }

 private:
  const A value_;

  GTEST_DISALLOW_ASSIGN_(SetArgumentPointeeAction);
};

template <size_t N, typename Proto>
class SetArgumentPointeeAction<N, Proto, true> {
 public:
  // Constructs an action that sets the variable pointed to by the
  // N-th function argument to 'proto'.  Both ProtocolMessage and
  // proto2::Message have the CopyFrom() method, so the same
  // implementation works for both.
  explicit SetArgumentPointeeAction(const Proto& proto) : proto_(new Proto) {
    proto_->CopyFrom(proto);
  }

  template <typename Result, typename ArgumentTuple>
  void Perform(const ArgumentTuple& args) const {
    CompileAssertTypesEqual<void, Result>();
    ::testing::get<N>(args)->CopyFrom(*proto_);
  }

 private:
  const internal::linked_ptr<Proto> proto_;

  GTEST_DISALLOW_ASSIGN_(SetArgumentPointeeAction);
};

// Implements the InvokeWithoutArgs(f) action.  The template argument
// FunctionImpl is the implementation type of f, which can be either a
// function pointer or a functor.  InvokeWithoutArgs(f) can be used as an
// Action<F> as long as f's type is compatible with F (i.e. f can be
// assigned to a tr1::function<F>).
template <typename FunctionImpl>
class InvokeWithoutArgsAction {
 public:
  // The c'tor makes a copy of function_impl (either a function
  // pointer or a functor).
  explicit InvokeWithoutArgsAction(FunctionImpl function_impl)
      : function_impl_(function_impl) {}

  // Allows InvokeWithoutArgs(f) to be used as any action whose type is
  // compatible with f.
  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple&) { return function_impl_(); }

 private:
  FunctionImpl function_impl_;

  GTEST_DISALLOW_ASSIGN_(InvokeWithoutArgsAction);
};

// Implements the InvokeWithoutArgs(object_ptr, &Class::Method) action.
template <class Class, typename MethodPtr>
class InvokeMethodWithoutArgsAction {
 public:
  InvokeMethodWithoutArgsAction(Class* obj_ptr, MethodPtr method_ptr)
      : obj_ptr_(obj_ptr), method_ptr_(method_ptr) {}

  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple&) const {
    return (obj_ptr_->*method_ptr_)();
  }

 private:
  Class* const obj_ptr_;
  const MethodPtr method_ptr_;

  GTEST_DISALLOW_ASSIGN_(InvokeMethodWithoutArgsAction);
};

// Implements the InvokeWithoutArgs(callback) action.
template <typename CallbackType>
class InvokeCallbackWithoutArgsAction {
 public:
  // The c'tor takes ownership of the callback.
  explicit InvokeCallbackWithoutArgsAction(CallbackType* callback)
      : callback_(callback) {
    callback->CheckIsRepeatable();  // Makes sure the callback is permanent.
  }

  // This type conversion operator template allows Invoke(callback) to
  // be used wherever the callback's return type can be implicitly
  // converted to that of the mock function.
  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple&) const { return callback_->Run(); }

 private:
  const internal::linked_ptr<CallbackType> callback_;

  GTEST_DISALLOW_ASSIGN_(InvokeCallbackWithoutArgsAction);
};

// Implements the IgnoreResult(action) action.
template <typename A>
class IgnoreResultAction {
 public:
  explicit IgnoreResultAction(const A& action) : action_(action) {}

  template <typename F>
  operator Action<F>() const {
    // Assert statement belongs here because this is the best place to verify
    // conditions on F. It produces the clearest error messages
    // in most compilers.
    // Impl really belongs in this scope as a local class but can't
    // because MSVC produces duplicate symbols in different translation units
    // in this case. Until MS fixes that bug we put Impl into the class scope
    // and put the typedef both here (for use in assert statement) and
    // in the Impl class. But both definitions must be the same.
    typedef typename internal::Function<F>::Result Result;

    // Asserts at compile time that F returns void.
    CompileAssertTypesEqual<void, Result>();

    return Action<F>(new Impl<F>(action_));
  }

 private:
  template <typename F>
  class Impl : public ActionInterface<F> {
   public:
    typedef typename internal::Function<F>::Result Result;
    typedef typename internal::Function<F>::ArgumentTuple ArgumentTuple;

    explicit Impl(const A& action) : action_(action) {}

    virtual void Perform(const ArgumentTuple& args) {
      // Performs the action and ignores its result.
      action_.Perform(args);
    }

   private:
    // Type OriginalFunction is the same as F except that its return
    // type is IgnoredValue.
    typedef typename internal::Function<F>::MakeResultIgnoredValue
        OriginalFunction;

    const Action<OriginalFunction> action_;

    GTEST_DISALLOW_ASSIGN_(Impl);
  };

  const A action_;

  GTEST_DISALLOW_ASSIGN_(IgnoreResultAction);
};

// A ReferenceWrapper<T> object represents a reference to type T,
// which can be either const or not.  It can be explicitly converted
// from, and implicitly converted to, a T&.  Unlike a reference,
// ReferenceWrapper<T> can be copied and can survive template type
// inference.  This is used to support by-reference arguments in the
// InvokeArgument<N>(...) action.  The idea was from "reference
// wrappers" in tr1, which we don't have in our source tree yet.
template <typename T>
class ReferenceWrapper {
 public:
  // Constructs a ReferenceWrapper<T> object from a T&.
  explicit ReferenceWrapper(T& l_value) : pointer_(&l_value) {}  // NOLINT

  // Allows a ReferenceWrapper<T> object to be implicitly converted to
  // a T&.
  operator T&() const { return *pointer_; }
 private:
  T* pointer_;
};

// Allows the expression ByRef(x) to be printed as a reference to x.
template <typename T>
void PrintTo(const ReferenceWrapper<T>& ref, ::std::ostream* os) {
  T& value = ref;
  UniversalPrinter<T&>::Print(value, os);
}

// Does two actions sequentially.  Used for implementing the DoAll(a1,
// a2, ...) action.
template <typename Action1, typename Action2>
class DoBothAction {
 public:
  DoBothAction(Action1 action1, Action2 action2)
      : action1_(action1), action2_(action2) {}

  // This template type conversion operator allows DoAll(a1, ..., a_n)
  // to be used in ANY function of compatible type.
  template <typename F>
  operator Action<F>() const {
    return Action<F>(new Impl<F>(action1_, action2_));
  }

 private:
  // Implements the DoAll(...) action for a particular function type F.
  template <typename F>
  class Impl : public ActionInterface<F> {
   public:
    typedef typename Function<F>::Result Result;
    typedef typename Function<F>::ArgumentTuple ArgumentTuple;
    typedef typename Function<F>::MakeResultVoid VoidResult;

    Impl(const Action<VoidResult>& action1, const Action<F>& action2)
        : action1_(action1), action2_(action2) {}

    virtual Result Perform(const ArgumentTuple& args) {
      action1_.Perform(args);
      return action2_.Perform(args);
    }

   private:
    const Action<VoidResult> action1_;
    const Action<F> action2_;

    GTEST_DISALLOW_ASSIGN_(Impl);
  };

  Action1 action1_;
  Action2 action2_;

  GTEST_DISALLOW_ASSIGN_(DoBothAction);
};

}  // namespace internal

// An Unused object can be implicitly constructed from ANY value.
// This is handy when defining actions that ignore some or all of the
// mock function arguments.  For example, given
//
//   MOCK_METHOD3(Foo, double(const string& label, double x, double y));
//   MOCK_METHOD3(Bar, double(int index, double x, double y));
//
// instead of
//
//   double DistanceToOriginWithLabel(const string& label, double x, double y) {
//     return sqrt(x*x + y*y);
//   }
//   double DistanceToOriginWithIndex(int index, double x, double y) {
//     return sqrt(x*x + y*y);
//   }
//   ...
//   EXPECT_CALL(mock, Foo("abc", _, _))
//       .WillOnce(Invoke(DistanceToOriginWithLabel));
//   EXPECT_CALL(mock, Bar(5, _, _))
//       .WillOnce(Invoke(DistanceToOriginWithIndex));
//
// you could write
//
//   // We can declare any uninteresting argument as Unused.
//   double DistanceToOrigin(Unused, double x, double y) {
//     return sqrt(x*x + y*y);
//   }
//   ...
//   EXPECT_CALL(mock, Foo("abc", _, _)).WillOnce(Invoke(DistanceToOrigin));
//   EXPECT_CALL(mock, Bar(5, _, _)).WillOnce(Invoke(DistanceToOrigin));
typedef internal::IgnoredValue Unused;

// This constructor allows us to turn an Action<From> object into an
// Action<To>, as long as To's arguments can be implicitly converted
// to From's and From's return type cann be implicitly converted to
// To's.
template <typename To>
template <typename From>
Action<To>::Action(const Action<From>& from)
    :
#if GTEST_LANG_CXX11
      fun_(from.fun_),
#endif
      impl_(from.impl_ == NULL ? NULL
                               : new internal::ActionAdaptor<To, From>(from)) {
}

// Creates an action that returns 'value'.  'value' is passed by value
// instead of const reference - otherwise Return("string literal")
// will trigger a compiler error about using array as initializer.
template <typename R>
internal::ReturnAction<R> Return(R value) {
  return internal::ReturnAction<R>(internal::move(value));
}

// Creates an action that returns NULL.
inline PolymorphicAction<internal::ReturnNullAction> ReturnNull() {
  return MakePolymorphicAction(internal::ReturnNullAction());
}

// Creates an action that returns from a void function.
inline PolymorphicAction<internal::ReturnVoidAction> Return() {
  return MakePolymorphicAction(internal::ReturnVoidAction());
}

// Creates an action that returns the reference to a variable.
template <typename R>
inline internal::ReturnRefAction<R> ReturnRef(R& x) {  // NOLINT
  return internal::ReturnRefAction<R>(x);
}

// Creates an action that returns the reference to a copy of the
// argument.  The copy is created when the action is constructed and
// lives as long as the action.
template <typename R>
inline internal::ReturnRefOfCopyAction<R> ReturnRefOfCopy(const R& x) {
  return internal::ReturnRefOfCopyAction<R>(x);
}

// Modifies the parent action (a Return() action) to perform a move of the
// argument instead of a copy.
// Return(ByMove()) actions can only be executed once and will assert this
// invariant.
template <typename R>
internal::ByMoveWrapper<R> ByMove(R x) {
  return internal::ByMoveWrapper<R>(internal::move(x));
}

// Creates an action that does the default action for the give mock function.
inline internal::DoDefaultAction DoDefault() {
  return internal::DoDefaultAction();
}

// Creates an action that sets the variable pointed by the N-th
// (0-based) function argument to 'value'.
template <size_t N, typename T>
PolymorphicAction<
  internal::SetArgumentPointeeAction<
    N, T, internal::IsAProtocolMessage<T>::value> >
SetArgPointee(const T& x) {
  return MakePolymorphicAction(internal::SetArgumentPointeeAction<
      N, T, internal::IsAProtocolMessage<T>::value>(x));
}

#if !((GTEST_GCC_VER_ && GTEST_GCC_VER_ < 40000) || GTEST_OS_SYMBIAN)
// This overload allows SetArgPointee() to accept a string literal.
// GCC prior to the version 4.0 and Symbian C++ compiler cannot distinguish
// this overload from the templated version and emit a compile error.
template <size_t N>
PolymorphicAction<
  internal::SetArgumentPointeeAction<N, const char*, false> >
SetArgPointee(const char* p) {
  return MakePolymorphicAction(internal::SetArgumentPointeeAction<
      N, const char*, false>(p));
}

template <size_t N>
PolymorphicAction<
  internal::SetArgumentPointeeAction<N, const wchar_t*, false> >
SetArgPointee(const wchar_t* p) {
  return MakePolymorphicAction(internal::SetArgumentPointeeAction<
      N, const wchar_t*, false>(p));
}
#endif

// The following version is DEPRECATED.
template <size_t N, typename T>
PolymorphicAction<
  internal::SetArgumentPointeeAction<
    N, T, internal::IsAProtocolMessage<T>::value> >
SetArgumentPointee(const T& x) {
  return MakePolymorphicAction(internal::SetArgumentPointeeAction<
      N, T, internal::IsAProtocolMessage<T>::value>(x));
}

// Creates an action that sets a pointer referent to a given value.
template <typename T1, typename T2>
PolymorphicAction<internal::AssignAction<T1, T2> > Assign(T1* ptr, T2 val) {
  return MakePolymorphicAction(internal::AssignAction<T1, T2>(ptr, val));
}

#if !GTEST_OS_WINDOWS_MOBILE

// Creates an action that sets errno and returns the appropriate error.
template <typename T>
PolymorphicAction<internal::SetErrnoAndReturnAction<T> >
SetErrnoAndReturn(int errval, T result) {
  return MakePolymorphicAction(
      internal::SetErrnoAndReturnAction<T>(errval, result));
}

#endif  // !GTEST_OS_WINDOWS_MOBILE

// Various overloads for InvokeWithoutArgs().

// Creates an action that invokes 'function_impl' with no argument.
template <typename FunctionImpl>
PolymorphicAction<internal::InvokeWithoutArgsAction<FunctionImpl> >
InvokeWithoutArgs(FunctionImpl function_impl) {
  return MakePolymorphicAction(
      internal::InvokeWithoutArgsAction<FunctionImpl>(function_impl));
}

// Creates an action that invokes the given method on the given object
// with no argument.
template <class Class, typename MethodPtr>
PolymorphicAction<internal::InvokeMethodWithoutArgsAction<Class, MethodPtr> >
InvokeWithoutArgs(Class* obj_ptr, MethodPtr method_ptr) {
  return MakePolymorphicAction(
      internal::InvokeMethodWithoutArgsAction<Class, MethodPtr>(
          obj_ptr, method_ptr));
}

// Creates an action that performs an_action and throws away its
// result.  In other words, it changes the return type of an_action to
// void.  an_action MUST NOT return void, or the code won't compile.
template <typename A>
inline internal::IgnoreResultAction<A> IgnoreResult(const A& an_action) {
  return internal::IgnoreResultAction<A>(an_action);
}

// Creates a reference wrapper for the given L-value.  If necessary,
// you can explicitly specify the type of the reference.  For example,
// suppose 'derived' is an object of type Derived, ByRef(derived)
// would wrap a Derived&.  If you want to wrap a const Base& instead,
// where Base is a base class of Derived, just write:
//
//   ByRef<const Base>(derived)
template <typename T>
inline internal::ReferenceWrapper<T> ByRef(T& l_value) {  // NOLINT
  return internal::ReferenceWrapper<T>(l_value);
}

}  // namespace testing

#endif  // GMOCK_INCLUDE_GMOCK_GMOCK_ACTIONS_H_
