.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2025, Google LLC.

.. _context-analysis:

Compiler-Based Context Analysis
===============================

Context Analysis is a language extension, which enables statically checking
that required contexts are active (or inactive) by acquiring and releasing
user-definable "context locks". An obvious application is lock-safety checking
for the kernel's various synchronization primitives (each of which represents a
"context lock"), and checking that locking rules are not violated.

The Clang compiler currently supports the full set of context analysis
features. To enable for Clang, configure the kernel with::

    CONFIG_WARN_CONTEXT_ANALYSIS=y

The feature requires Clang 22 or later.

The analysis is *opt-in by default*, and requires declaring which modules and
subsystems should be analyzed in the respective `Makefile`::

    CONTEXT_ANALYSIS_mymodule.o := y

Or for all translation units in the directory::

    CONTEXT_ANALYSIS := y

It is possible to enable the analysis tree-wide, however, which will result in
numerous false positive warnings currently and is *not* generally recommended::

    CONFIG_WARN_CONTEXT_ANALYSIS_ALL=y

Programming Model
-----------------

The below describes the programming model around using context lock types.

.. note::
   Enabling context analysis can be seen as enabling a dialect of Linux C with
   a Context System. Some valid patterns involving complex control-flow are
   constrained (such as conditional acquisition and later conditional release
   in the same function).

Context analysis is a way to specify permissibility of operations to depend on
context locks being held (or not held). Typically we are interested in
protecting data and code in a critical section by requiring a specific context
to be active, for example by holding a specific lock. The analysis ensures that
callers cannot perform an operation without the required context being active.

Context locks are associated with named structs, along with functions that
operate on struct instances to acquire and release the associated context lock.

Context locks can be held either exclusively or shared. This mechanism allows
assigning more precise privileges when a context is active, typically to
distinguish where a thread may only read (shared) or also write (exclusive) to
data guarded within a context.

The set of contexts that are actually active in a given thread at a given point
in program execution is a run-time concept. The static analysis works by
calculating an approximation of that set, called the context environment. The
context environment is calculated for every program point, and describes the
set of contexts that are statically known to be active, or inactive, at that
particular point. This environment is a conservative approximation of the full
set of contexts that will actually be active in a thread at run-time.

More details are also documented `here
<https://clang.llvm.org/docs/ThreadSafetyAnalysis.html>`_.

.. note::
   Clang's analysis explicitly does not infer context locks acquired or
   released by inline functions. It requires explicit annotations to (a) assert
   that it's not a bug if a context lock is released or acquired, and (b) to
   retain consistency between inline and non-inline function declarations.

Supported Kernel Primitives
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Currently the following synchronization primitives are supported:
`raw_spinlock_t`, `spinlock_t`, `rwlock_t`, `mutex`, `seqlock_t`,
`bit_spinlock`, RCU, SRCU (`srcu_struct`), `rw_semaphore`, `local_lock_t`,
`ww_mutex`.

To initialize variables guarded by a context lock with an initialization
function (``type_init(&lock)``), prefer using ``guard(type_init)(&lock)`` or
``scoped_guard(type_init, &lock) { ... }`` to initialize such guarded members
or globals in the enclosing scope. This initializes the context lock and treats
the context as active within the initialization scope (initialization implies
exclusive access to the underlying object).

For example::

    struct my_data {
            spinlock_t lock;
            int counter __guarded_by(&lock);
    };

    void init_my_data(struct my_data *d)
    {
            ...
            guard(spinlock_init)(&d->lock);
            d->counter = 0;
            ...
    }

Alternatively, initializing guarded variables can be done with context analysis
disabled, preferably in the smallest possible scope (due to lack of any other
checking): either with a ``context_unsafe(var = init)`` expression, or by
marking small initialization functions with the ``__context_unsafe(init)``
attribute.

Lockdep assertions, such as `lockdep_assert_held()`, inform the compiler's
context analysis that the associated synchronization primitive is held after
the assertion. This avoids false positives in complex control-flow scenarios
and encourages the use of Lockdep where static analysis is limited. For
example, this is useful when a function doesn't *always* require a lock, making
`__must_hold()` inappropriate.

Keywords
~~~~~~~~

.. kernel-doc:: include/linux/compiler-context-analysis.h
   :identifiers: context_lock_struct
                 token_context_lock token_context_lock_instance
                 __guarded_by __pt_guarded_by
                 __must_hold
                 __must_not_hold
                 __acquires
                 __cond_acquires
                 __releases
                 __must_hold_shared
                 __acquires_shared
                 __cond_acquires_shared
                 __releases_shared
                 __acquire
                 __release
                 __acquire_shared
                 __release_shared
                 __acquire_ret
                 __acquire_shared_ret
                 context_unsafe
                 __context_unsafe
                 disable_context_analysis enable_context_analysis

.. note::
   The function attribute `__no_context_analysis` is reserved for internal
   implementation of context lock types, and should be avoided in normal code.

Background
----------

Clang originally called the feature `Thread Safety Analysis
<https://clang.llvm.org/docs/ThreadSafetyAnalysis.html>`_, with some keywords
and documentation still using the thread-safety-analysis-only terminology. This
was later changed and the feature became more flexible, gaining the ability to
define custom "capabilities". Its foundations can be found in `Capability
Systems <https://www.cs.cornell.edu/talc/papers/capabilities.pdf>`_, used to
specify the permissibility of operations to depend on some "capability" being
held (or not held).

Because the feature is not just able to express capabilities related to
synchronization primitives, and "capability" is already overloaded in the
kernel, the naming chosen for the kernel departs from Clang's initial "Thread
Safety" and "capability" nomenclature; we refer to the feature as "Context
Analysis" to avoid confusion. The internal implementation still makes
references to Clang's terminology in a few places, such as `-Wthread-safety`
being the warning option that also still appears in diagnostic messages.
