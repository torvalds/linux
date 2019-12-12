The Kernel Concurrency Sanitizer (KCSAN)
========================================

Overview
--------

*Kernel Concurrency Sanitizer (KCSAN)* is a dynamic data race detector for
kernel space. KCSAN is a sampling watchpoint-based data race detector. Key
priorities in KCSAN's design are lack of false positives, scalability, and
simplicity. More details can be found in `Implementation Details`_.

KCSAN uses compile-time instrumentation to instrument memory accesses. KCSAN is
supported in both GCC and Clang. With GCC it requires version 7.3.0 or later.
With Clang it requires version 7.0.0 or later.

Usage
-----

To enable KCSAN configure kernel with::

    CONFIG_KCSAN = y

KCSAN provides several other configuration options to customize behaviour (see
their respective help text for more info).

Error reports
~~~~~~~~~~~~~

A typical data race report looks like this::

    ==================================================================
    BUG: KCSAN: data-race in generic_permission / kernfs_refresh_inode

    write to 0xffff8fee4c40700c of 4 bytes by task 175 on cpu 4:
     kernfs_refresh_inode+0x70/0x170
     kernfs_iop_permission+0x4f/0x90
     inode_permission+0x190/0x200
     link_path_walk.part.0+0x503/0x8e0
     path_lookupat.isra.0+0x69/0x4d0
     filename_lookup+0x136/0x280
     user_path_at_empty+0x47/0x60
     vfs_statx+0x9b/0x130
     __do_sys_newlstat+0x50/0xb0
     __x64_sys_newlstat+0x37/0x50
     do_syscall_64+0x85/0x260
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    read to 0xffff8fee4c40700c of 4 bytes by task 166 on cpu 6:
     generic_permission+0x5b/0x2a0
     kernfs_iop_permission+0x66/0x90
     inode_permission+0x190/0x200
     link_path_walk.part.0+0x503/0x8e0
     path_lookupat.isra.0+0x69/0x4d0
     filename_lookup+0x136/0x280
     user_path_at_empty+0x47/0x60
     do_faccessat+0x11a/0x390
     __x64_sys_access+0x3c/0x50
     do_syscall_64+0x85/0x260
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    Reported by Kernel Concurrency Sanitizer on:
    CPU: 6 PID: 166 Comm: systemd-journal Not tainted 5.3.0-rc7+ #1
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.12.0-1 04/01/2014
    ==================================================================

The header of the report provides a short summary of the functions involved in
the race. It is followed by the access types and stack traces of the 2 threads
involved in the data race.

The other less common type of data race report looks like this::

    ==================================================================
    BUG: KCSAN: data-race in e1000_clean_rx_irq+0x551/0xb10

    race at unknown origin, with read to 0xffff933db8a2ae6c of 1 bytes by interrupt on cpu 0:
     e1000_clean_rx_irq+0x551/0xb10
     e1000_clean+0x533/0xda0
     net_rx_action+0x329/0x900
     __do_softirq+0xdb/0x2db
     irq_exit+0x9b/0xa0
     do_IRQ+0x9c/0xf0
     ret_from_intr+0x0/0x18
     default_idle+0x3f/0x220
     arch_cpu_idle+0x21/0x30
     do_idle+0x1df/0x230
     cpu_startup_entry+0x14/0x20
     rest_init+0xc5/0xcb
     arch_call_rest_init+0x13/0x2b
     start_kernel+0x6db/0x700

    Reported by Kernel Concurrency Sanitizer on:
    CPU: 0 PID: 0 Comm: swapper/0 Not tainted 5.3.0-rc7+ #2
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.12.0-1 04/01/2014
    ==================================================================

This report is generated where it was not possible to determine the other
racing thread, but a race was inferred due to the data value of the watched
memory location having changed. These can occur either due to missing
instrumentation or e.g. DMA accesses.

Selective analysis
~~~~~~~~~~~~~~~~~~

It may be desirable to disable data race detection for specific accesses,
functions, compilation units, or entire subsystems.  For static blacklisting,
the below options are available:

* KCSAN understands the ``data_race(expr)`` annotation, which tells KCSAN that
  any data races due to accesses in ``expr`` should be ignored and resulting
  behaviour when encountering a data race is deemed safe.

* Disabling data race detection for entire functions can be accomplished by
  using the function attribute ``__no_kcsan`` (or ``__no_kcsan_or_inline`` for
  ``__always_inline`` functions). To dynamically control for which functions
  data races are reported, see the `debugfs`_ blacklist/whitelist feature.

* To disable data race detection for a particular compilation unit, add to the
  ``Makefile``::

    KCSAN_SANITIZE_file.o := n

* To disable data race detection for all compilation units listed in a
  ``Makefile``, add to the respective ``Makefile``::

    KCSAN_SANITIZE := n

debugfs
~~~~~~~

* The file ``/sys/kernel/debug/kcsan`` can be read to get stats.

* KCSAN can be turned on or off by writing ``on`` or ``off`` to
  ``/sys/kernel/debug/kcsan``.

* Writing ``!some_func_name`` to ``/sys/kernel/debug/kcsan`` adds
  ``some_func_name`` to the report filter list, which (by default) blacklists
  reporting data races where either one of the top stackframes are a function
  in the list.

* Writing either ``blacklist`` or ``whitelist`` to ``/sys/kernel/debug/kcsan``
  changes the report filtering behaviour. For example, the blacklist feature
  can be used to silence frequently occurring data races; the whitelist feature
  can help with reproduction and testing of fixes.

Data Races
----------

Informally, two operations *conflict* if they access the same memory location,
and at least one of them is a write operation. In an execution, two memory
operations from different threads form a **data race** if they *conflict*, at
least one of them is a *plain access* (non-atomic), and they are *unordered* in
the "happens-before" order according to the `LKMM
<../../tools/memory-model/Documentation/explanation.txt>`_.

Relationship with the Linux Kernel Memory Model (LKMM)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The LKMM defines the propagation and ordering rules of various memory
operations, which gives developers the ability to reason about concurrent code.
Ultimately this allows to determine the possible executions of concurrent code,
and if that code is free from data races.

KCSAN is aware of *atomic* accesses (``READ_ONCE``, ``WRITE_ONCE``,
``atomic_*``, etc.), but is oblivious of any ordering guarantees. In other
words, KCSAN assumes that as long as a plain access is not observed to race
with another conflicting access, memory operations are correctly ordered.

This means that KCSAN will not report *potential* data races due to missing
memory ordering. If, however, missing memory ordering (that is observable with
a particular compiler and architecture) leads to an observable data race (e.g.
entering a critical section erroneously), KCSAN would report the resulting
data race.

Race conditions vs. data races
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Race conditions are logic bugs, where unexpected interleaving of racing
concurrent operations result in an erroneous state.

Data races on the other hand are defined at the *memory model/language level*.
Many data races are also harmful race conditions, which a tool like KCSAN
reports!  However, not all data races are race conditions and vice-versa.
KCSAN's intent is to report data races according to the LKMM. A data race
detector can only work at the memory model/language level.

Deeper analysis, to find high-level race conditions only, requires conveying
the intended kernel logic to a tool. This requires (1) the developer writing a
specification or model of their code, and then (2) the tool verifying that the
implementation matches. This has been done for small bits of code using model
checkers and other formal methods, but does not scale to the level of what can
be covered with a dynamic analysis based data race detector such as KCSAN.

For reasons outlined in this `article <https://lwn.net/Articles/793253/>`_,
data races can be much more subtle, but can cause no less harm than high-level
race conditions.

Implementation Details
----------------------

The general approach is inspired by `DataCollider
<http://usenix.org/legacy/events/osdi10/tech/full_papers/Erickson.pdf>`_.
Unlike DataCollider, KCSAN does not use hardware watchpoints, but instead
relies on compiler instrumentation. Watchpoints are implemented using an
efficient encoding that stores access type, size, and address in a long; the
benefits of using "soft watchpoints" are portability and greater flexibility in
limiting which accesses trigger a watchpoint.

More specifically, KCSAN requires instrumenting plain (unmarked, non-atomic)
memory operations; for each instrumented plain access:

1. Check if a matching watchpoint exists; if yes, and at least one access is a
   write, then we encountered a racing access.

2. Periodically, if no matching watchpoint exists, set up a watchpoint and
   stall for a small delay.

3. Also check the data value before the delay, and re-check the data value
   after delay; if the values mismatch, we infer a race of unknown origin.

To detect data races between plain and atomic memory operations, KCSAN also
annotates atomic accesses, but only to check if a watchpoint exists
(``kcsan_check_atomic_*``); i.e.  KCSAN never sets up a watchpoint on atomic
accesses.

Key Properties
~~~~~~~~~~~~~~

1. **Memory Overhead:**  The current implementation uses a small array of longs
   to encode watchpoint information, which is negligible.

2. **Performance Overhead:** KCSAN's runtime aims to be minimal, using an
   efficient watchpoint encoding that does not require acquiring any shared
   locks in the fast-path. For kernel boot on a system with 8 CPUs:

   - 5.0x slow-down with the default KCSAN config;
   - 2.8x slow-down from runtime fast-path overhead only (set very large
     ``KCSAN_SKIP_WATCH`` and unset ``KCSAN_SKIP_WATCH_RANDOMIZE``).

3. **Annotation Overheads:** Minimal annotations are required outside the KCSAN
   runtime. As a result, maintenance overheads are minimal as the kernel
   evolves.

4. **Detects Racy Writes from Devices:** Due to checking data values upon
   setting up watchpoints, racy writes from devices can also be detected.

5. **Memory Ordering:** KCSAN is *not* explicitly aware of the LKMM's ordering
   rules; this may result in missed data races (false negatives).

6. **Analysis Accuracy:** For observed executions, due to using a sampling
   strategy, the analysis is *unsound* (false negatives possible), but aims to
   be complete (no false positives).

Alternatives Considered
-----------------------

An alternative data race detection approach for the kernel can be found in
`Kernel Thread Sanitizer (KTSAN) <https://github.com/google/ktsan/wiki>`_.
KTSAN is a happens-before data race detector, which explicitly establishes the
happens-before order between memory operations, which can then be used to
determine data races as defined in `Data Races`_. To build a correct
happens-before relation, KTSAN must be aware of all ordering rules of the LKMM
and synchronization primitives. Unfortunately, any omission leads to false
positives, which is especially important in the context of the kernel which
includes numerous custom synchronization mechanisms. Furthermore, KTSAN's
implementation requires metadata for each memory location (shadow memory);
currently, for each page, KTSAN requires 4 pages of shadow memory.
