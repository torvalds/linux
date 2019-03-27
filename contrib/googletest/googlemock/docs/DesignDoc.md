This page discusses the design of new Google Mock features.



# Macros for Defining Actions #

## Problem ##

Due to the lack of closures in C++, it currently requires some
non-trivial effort to define a custom action in Google Mock.  For
example, suppose you want to "increment the value pointed to by the
second argument of the mock function and return it", you could write:

```
int IncrementArg1(Unused, int* p, Unused) {
  return ++(*p);
}

... WillOnce(Invoke(IncrementArg1));
```

There are several things unsatisfactory about this approach:

  * Even though the action only cares about the second argument of the mock function, its definition needs to list other arguments as dummies.  This is tedious.
  * The defined action is usable only in mock functions that takes exactly 3 arguments - an unnecessary restriction.
  * To use the action, one has to say `Invoke(IncrementArg1)`, which isn't as nice as `IncrementArg1()`.

The latter two problems can be overcome using `MakePolymorphicAction()`,
but it requires much more boilerplate code:

```
class IncrementArg1Action {
 public:
  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple& args) const {
    return ++(*tr1::get<1>(args));
  }
};

PolymorphicAction<IncrementArg1Action> IncrementArg1() {
  return MakePolymorphicAction(IncrementArg1Action());
}

... WillOnce(IncrementArg1());
```

Our goal is to allow defining custom actions with the least amount of
boiler-plate C++ requires.

## Solution ##

We propose to introduce a new macro:
```
ACTION(name) { statements; }
```

Using this in a namespace scope will define an action with the given
name that executes the statements.  Inside the statements, you can
refer to the K-th (0-based) argument of the mock function as `argK`.
For example:
```
ACTION(IncrementArg1) { return ++(*arg1); }
```
allows you to write
```
... WillOnce(IncrementArg1());
```

Note that you don't need to specify the types of the mock function
arguments, as brevity is a top design goal here.  Rest assured that
your code is still type-safe though: you'll get a compiler error if
`*arg1` doesn't support the `++` operator, or if the type of
`++(*arg1)` isn't compatible with the mock function's return type.

Another example:
```
ACTION(Foo) {
  (*arg2)(5);
  Blah();
  *arg1 = 0;
  return arg0;
}
```
defines an action `Foo()` that invokes argument #2 (a function pointer)
with 5, calls function `Blah()`, sets the value pointed to by argument
#1 to 0, and returns argument #0.

For more convenience and flexibility, you can also use the following
pre-defined symbols in the body of `ACTION`:

| `argK_type` | The type of the K-th (0-based) argument of the mock function |
|:------------|:-------------------------------------------------------------|
| `args`      | All arguments of the mock function as a tuple                |
| `args_type` | The type of all arguments of the mock function as a tuple    |
| `return_type` | The return type of the mock function                         |
| `function_type` | The type of the mock function                                |

For example, when using an `ACTION` as a stub action for mock function:
```
int DoSomething(bool flag, int* ptr);
```
we have:
| **Pre-defined Symbol** | **Is Bound To** |
|:-----------------------|:----------------|
| `arg0`                 | the value of `flag` |
| `arg0_type`            | the type `bool` |
| `arg1`                 | the value of `ptr` |
| `arg1_type`            | the type `int*` |
| `args`                 | the tuple `(flag, ptr)` |
| `args_type`            | the type `std::tr1::tuple<bool, int*>` |
| `return_type`          | the type `int`  |
| `function_type`        | the type `int(bool, int*)` |

## Parameterized actions ##

Sometimes you'll want to parameterize the action.   For that we propose
another macro
```
ACTION_P(name, param) { statements; }
```

For example,
```
ACTION_P(Add, n) { return arg0 + n; }
```
will allow you to write
```
// Returns argument #0 + 5.
... WillOnce(Add(5));
```

For convenience, we use the term _arguments_ for the values used to
invoke the mock function, and the term _parameters_ for the values
used to instantiate an action.

Note that you don't need to provide the type of the parameter either.
Suppose the parameter is named `param`, you can also use the
Google-Mock-defined symbol `param_type` to refer to the type of the
parameter as inferred by the compiler.

We will also provide `ACTION_P2`, `ACTION_P3`, and etc to support
multi-parameter actions.  For example,
```
ACTION_P2(ReturnDistanceTo, x, y) {
  double dx = arg0 - x;
  double dy = arg1 - y;
  return sqrt(dx*dx + dy*dy);
}
```
lets you write
```
... WillOnce(ReturnDistanceTo(5.0, 26.5));
```

You can view `ACTION` as a degenerated parameterized action where the
number of parameters is 0.

## Advanced Usages ##

### Overloading Actions ###

You can easily define actions overloaded on the number of parameters:
```
ACTION_P(Plus, a) { ... }
ACTION_P2(Plus, a, b) { ... }
```

### Restricting the Type of an Argument or Parameter ###

For maximum brevity and reusability, the `ACTION*` macros don't let
you specify the types of the mock function arguments and the action
parameters.  Instead, we let the compiler infer the types for us.

Sometimes, however, we may want to be more explicit about the types.
There are several tricks to do that.  For example:
```
ACTION(Foo) {
  // Makes sure arg0 can be converted to int.
  int n = arg0;
  ... use n instead of arg0 here ...
}

ACTION_P(Bar, param) {
  // Makes sure the type of arg1 is const char*.
  ::testing::StaticAssertTypeEq<const char*, arg1_type>();

  // Makes sure param can be converted to bool.
  bool flag = param;
}
```
where `StaticAssertTypeEq` is a compile-time assertion we plan to add to
Google Test (the name is chosen to match `static_assert` in C++0x).

### Using the ACTION Object's Type ###

If you are writing a function that returns an `ACTION` object, you'll
need to know its type.  The type depends on the macro used to define
the action and the parameter types.  The rule is relatively simple:
| **Given Definition** | **Expression** | **Has Type** |
|:---------------------|:---------------|:-------------|
| `ACTION(Foo)`        | `Foo()`        | `FooAction`  |
| `ACTION_P(Bar, param)` | `Bar(int_value)` | `BarActionP<int>` |
| `ACTION_P2(Baz, p1, p2)` | `Baz(bool_value, int_value)` | `BazActionP2<bool, int>` |
| ...                  | ...            | ...          |

Note that we have to pick different suffixes (`Action`, `ActionP`,
`ActionP2`, and etc) for actions with different numbers of parameters,
or the action definitions cannot be overloaded on the number of
parameters.

## When to Use ##

While the new macros are very convenient, please also consider other
means of implementing actions (e.g. via `ActionInterface` or
`MakePolymorphicAction()`), especially if you need to use the defined
action a lot.  While the other approaches require more work, they give
you more control on the types of the mock function arguments and the
action parameters, which in general leads to better compiler error
messages that pay off in the long run.  They also allow overloading
actions based on parameter types, as opposed to just the number of
parameters.

## Related Work ##

As you may have realized, the `ACTION*` macros resemble closures (also
known as lambda expressions or anonymous functions).  Indeed, both of
them seek to lower the syntactic overhead for defining a function.

C++0x will support lambdas, but they are not part of C++ right now.
Some non-standard libraries (most notably BLL or Boost Lambda Library)
try to alleviate this problem.  However, they are not a good choice
for defining actions as:

  * They are non-standard and not widely installed.  Google Mock only depends on standard libraries and `tr1::tuple`, which is part of the new C++ standard and comes with gcc 4+.  We want to keep it that way.
  * They are not trivial to learn.
  * They will become obsolete when C++0x's lambda feature is widely supported.  We don't want to make our users use a dying library.
  * Since they are based on operators, they are rather ad hoc: you cannot use statements, and you cannot pass the lambda arguments to a function, for example.
  * They have subtle semantics that easily confuses new users.  For example, in expression `_1++ + foo++`, `foo` will be incremented only once where the expression is evaluated, while `_1` will be incremented every time the unnamed function is invoked.  This is far from intuitive.

`ACTION*` avoid all these problems.

## Future Improvements ##

There may be a need for composing `ACTION*` definitions (i.e. invoking
another `ACTION` inside the definition of one `ACTION*`).  We are not
sure we want it yet, as one can get a similar effect by putting
`ACTION` definitions in function templates and composing the function
templates.  We'll revisit this based on user feedback.

The reason we don't allow `ACTION*()` inside a function body is that
the current C++ standard doesn't allow function-local types to be used
to instantiate templates.  The upcoming C++0x standard will lift this
restriction.  Once this feature is widely supported by compilers, we
can revisit the implementation and add support for using `ACTION*()`
inside a function.

C++0x will also support lambda expressions.  When they become
available, we may want to support using lambdas as actions.

# Macros for Defining Matchers #

Once the macros for defining actions are implemented, we plan to do
the same for matchers:

```
MATCHER(name) { statements; }
```

where you can refer to the value being matched as `arg`.  For example,
given:

```
MATCHER(IsPositive) { return arg > 0; }
```

you can use `IsPositive()` as a matcher that matches a value iff it is
greater than 0.

We will also add `MATCHER_P`, `MATCHER_P2`, and etc for parameterized
matchers.