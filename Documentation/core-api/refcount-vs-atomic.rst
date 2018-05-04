===================================
refcount_t API compared to atomic_t
===================================

.. contents:: :local:

Introduction
============

The goal of refcount_t API is to provide a minimal API for implementing
an object's reference counters. While a generic architecture-independent
implementation from lib/refcount.c uses atomic operations underneath,
there are a number of differences between some of the ``refcount_*()`` and
``atomic_*()`` functions with regards to the memory ordering guarantees.
This document outlines the differences and provides respective examples
in order to help maintainers validate their code against the change in
these memory ordering guarantees.

The terms used through this document try to follow the formal LKMM defined in
tools/memory-model/Documentation/explanation.txt.

memory-barriers.txt and atomic_t.txt provide more background to the
memory ordering in general and for atomic operations specifically.

Relevant types of memory ordering
=================================

.. note:: The following section only covers some of the memory
   ordering types that are relevant for the atomics and reference
   counters and used through this document. For a much broader picture
   please consult memory-barriers.txt document.

In the absence of any memory ordering guarantees (i.e. fully unordered)
atomics & refcounters only provide atomicity and
program order (po) relation (on the same CPU). It guarantees that
each ``atomic_*()`` and ``refcount_*()`` operation is atomic and instructions
are executed in program order on a single CPU.
This is implemented using :c:func:`READ_ONCE`/:c:func:`WRITE_ONCE` and
compare-and-swap primitives.

A strong (full) memory ordering guarantees that all prior loads and
stores (all po-earlier instructions) on the same CPU are completed
before any po-later instruction is executed on the same CPU.
It also guarantees that all po-earlier stores on the same CPU
and all propagated stores from other CPUs must propagate to all
other CPUs before any po-later instruction is executed on the original
CPU (A-cumulative property). This is implemented using :c:func:`smp_mb`.

A RELEASE memory ordering guarantees that all prior loads and
stores (all po-earlier instructions) on the same CPU are completed
before the operation. It also guarantees that all po-earlier
stores on the same CPU and all propagated stores from other CPUs
must propagate to all other CPUs before the release operation
(A-cumulative property). This is implemented using
:c:func:`smp_store_release`.

A control dependency (on success) for refcounters guarantees that
if a reference for an object was successfully obtained (reference
counter increment or addition happened, function returned true),
then further stores are ordered against this operation.
Control dependency on stores are not implemented using any explicit
barriers, but rely on CPU not to speculate on stores. This is only
a single CPU relation and provides no guarantees for other CPUs.


Comparison of functions
=======================

case 1) - non-"Read/Modify/Write" (RMW) ops
-------------------------------------------

Function changes:

 * :c:func:`atomic_set` --> :c:func:`refcount_set`
 * :c:func:`atomic_read` --> :c:func:`refcount_read`

Memory ordering guarantee changes:

 * none (both fully unordered)


case 2) - increment-based ops that return no value
--------------------------------------------------

Function changes:

 * :c:func:`atomic_inc` --> :c:func:`refcount_inc`
 * :c:func:`atomic_add` --> :c:func:`refcount_add`

Memory ordering guarantee changes:

 * none (both fully unordered)

case 3) - decrement-based RMW ops that return no value
------------------------------------------------------

Function changes:

 * :c:func:`atomic_dec` --> :c:func:`refcount_dec`

Memory ordering guarantee changes:

 * fully unordered --> RELEASE ordering


case 4) - increment-based RMW ops that return a value
-----------------------------------------------------

Function changes:

 * :c:func:`atomic_inc_not_zero` --> :c:func:`refcount_inc_not_zero`
 * no atomic counterpart --> :c:func:`refcount_add_not_zero`

Memory ordering guarantees changes:

 * fully ordered --> control dependency on success for stores

.. note:: We really assume here that necessary ordering is provided as a
   result of obtaining pointer to the object!


case 5) - decrement-based RMW ops that return a value
-----------------------------------------------------

Function changes:

 * :c:func:`atomic_dec_and_test` --> :c:func:`refcount_dec_and_test`
 * :c:func:`atomic_sub_and_test` --> :c:func:`refcount_sub_and_test`
 * no atomic counterpart --> :c:func:`refcount_dec_if_one`
 * ``atomic_add_unless(&var, -1, 1)`` --> ``refcount_dec_not_one(&var)``

Memory ordering guarantees changes:

 * fully ordered --> RELEASE ordering + control dependency

.. note:: :c:func:`atomic_add_unless` only provides full order on success.


case 6) - lock-based RMW
------------------------

Function changes:

 * :c:func:`atomic_dec_and_lock` --> :c:func:`refcount_dec_and_lock`
 * :c:func:`atomic_dec_and_mutex_lock` --> :c:func:`refcount_dec_and_mutex_lock`

Memory ordering guarantees changes:

 * fully ordered --> RELEASE ordering + control dependency + hold
   :c:func:`spin_lock` on success
