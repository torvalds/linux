.. SPDX-License-Identifier: GPL-2.0

==================================
Fprobe - Function entry/exit probe
==================================

.. Author: Masami Hiramatsu <mhiramat@kernel.org>

Introduction
============

Fprobe is a function entry/exit probe based on the function-graph tracing
feature in ftrace.
Instead of tracing all functions, if you want to attach callbacks on specific
function entry and exit, similar to the kprobes and kretprobes, you can
use fprobe. Compared with kprobes and kretprobes, fprobe gives faster
instrumentation for multiple functions with single handler. This document
describes how to use fprobe.

The usage of fprobe
===================

The fprobe is a wrapper of ftrace (+ kretprobe-like return callback) to
attach callbacks to multiple function entry and exit. User needs to set up
the `struct fprobe` and pass it to `register_fprobe()`.

Typically, `fprobe` data structure is initialized with the `entry_handler`
and/or `exit_handler` as below.

.. code-block:: c

 struct fprobe fp = {
        .entry_handler  = my_entry_callback,
        .exit_handler   = my_exit_callback,
 };

To enable the fprobe, call one of register_fprobe(), register_fprobe_ips(), and
register_fprobe_syms(). These functions register the fprobe with different types
of parameters.

The register_fprobe() enables a fprobe by function-name filters.
E.g. this enables @fp on "func*()" function except "func2()".::

  register_fprobe(&fp, "func*", "func2");

The register_fprobe_ips() enables a fprobe by ftrace-location addresses.
E.g.

.. code-block:: c

  unsigned long ips[] = { 0x.... };

  register_fprobe_ips(&fp, ips, ARRAY_SIZE(ips));

And the register_fprobe_syms() enables a fprobe by symbol names.
E.g.

.. code-block:: c

  char syms[] = {"func1", "func2", "func3"};

  register_fprobe_syms(&fp, syms, ARRAY_SIZE(syms));

To disable (remove from functions) this fprobe, call::

  unregister_fprobe(&fp);

You can temporally (soft) disable the fprobe by::

  disable_fprobe(&fp);

and resume by::

  enable_fprobe(&fp);

The above is defined by including the header::

  #include <linux/fprobe.h>

Same as ftrace, the registered callbacks will start being called some time
after the register_fprobe() is called and before it returns. See
:file:`Documentation/trace/ftrace.rst`.

Also, the unregister_fprobe() will guarantee that the both enter and exit
handlers are no longer being called by functions after unregister_fprobe()
returns as same as unregister_ftrace_function().

The fprobe entry/exit handler
=============================

The prototype of the entry/exit callback function are as follows:

.. code-block:: c

 int entry_callback(struct fprobe *fp, unsigned long entry_ip, unsigned long ret_ip, struct ftrace_regs *fregs, void *entry_data);

 void exit_callback(struct fprobe *fp, unsigned long entry_ip, unsigned long ret_ip, struct ftrace_regs *fregs, void *entry_data);

Note that the @entry_ip is saved at function entry and passed to exit
handler.
If the entry callback function returns !0, the corresponding exit callback
will be cancelled.

@fp
        This is the address of `fprobe` data structure related to this handler.
        You can embed the `fprobe` to your data structure and get it by
        container_of() macro from @fp. The @fp must not be NULL.

@entry_ip
        This is the ftrace address of the traced function (both entry and exit).
        Note that this may not be the actual entry address of the function but
        the address where the ftrace is instrumented.

@ret_ip
        This is the return address that the traced function will return to,
        somewhere in the caller. This can be used at both entry and exit.

@fregs
        This is the `ftrace_regs` data structure at the entry and exit. This
        includes the function parameters, or the return values. So user can
        access thos values via appropriate `ftrace_regs_*` APIs.

@entry_data
        This is a local storage to share the data between entry and exit handlers.
        This storage is NULL by default. If the user specify `exit_handler` field
        and `entry_data_size` field when registering the fprobe, the storage is
        allocated and passed to both `entry_handler` and `exit_handler`.

Entry data size and exit handlers on the same function
======================================================

Since the entry data is passed via per-task stack and it has limited size,
the entry data size per probe is limited to `15 * sizeof(long)`. You also need
to take care that the different fprobes are probing on the same function, this
limit becomes smaller. The entry data size is aligned to `sizeof(long)` and
each fprobe which has exit handler uses a `sizeof(long)` space on the stack,
you should keep the number of fprobes on the same function as small as
possible.

Share the callbacks with kprobes
================================

Since the recursion safeness of the fprobe (and ftrace) is a bit different
from the kprobes, this may cause an issue if user wants to run the same
code from the fprobe and the kprobes.

Kprobes has per-cpu 'current_kprobe' variable which protects the kprobe
handler from recursion in all cases. On the other hand, fprobe uses
only ftrace_test_recursion_trylock(). This allows interrupt context to
call another (or same) fprobe while the fprobe user handler is running.

This is not a matter if the common callback code has its own recursion
detection, or it can handle the recursion in the different contexts
(normal/interrupt/NMI.)
But if it relies on the 'current_kprobe' recursion lock, it has to check
kprobe_running() and use kprobe_busy_*() APIs.

Fprobe has FPROBE_FL_KPROBE_SHARED flag to do this. If your common callback
code will be shared with kprobes, please set FPROBE_FL_KPROBE_SHARED
*before* registering the fprobe, like:

.. code-block:: c

 fprobe.flags = FPROBE_FL_KPROBE_SHARED;

 register_fprobe(&fprobe, "func*", NULL);

This will protect your common callback from the nested call.

The missed counter
==================

The `fprobe` data structure has `fprobe::nmissed` counter field as same as
kprobes.
This counter counts up when;

 - fprobe fails to take ftrace_recursion lock. This usually means that a function
   which is traced by other ftrace users is called from the entry_handler.

 - fprobe fails to setup the function exit because of failing to allocate the
   data buffer from the per-task shadow stack.

The `fprobe::nmissed` field counts up in both cases. Therefore, the former
skips both of entry and exit callback and the latter skips the exit
callback, but in both case the counter will increase by 1.

Note that if you set the FTRACE_OPS_FL_RECURSION and/or FTRACE_OPS_FL_RCU to
`fprobe::ops::flags` (ftrace_ops::flags) when registering the fprobe, this
counter may not work correctly, because ftrace skips the fprobe function which
increase the counter.


Functions and structures
========================

.. kernel-doc:: include/linux/fprobe.h
.. kernel-doc:: kernel/trace/fprobe.c

