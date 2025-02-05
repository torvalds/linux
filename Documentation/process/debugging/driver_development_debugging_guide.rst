.. SPDX-License-Identifier: GPL-2.0

========================================
Debugging advice for driver development
========================================

This document serves as a general starting point and lookup for debugging
device drivers.
While this guide focuses on debugging that requires re-compiling the
module/kernel, the :doc:`userspace debugging guide
</process/debugging/userspace_debugging_guide>` will guide
you through tools like dynamic debug, ftrace and other tools useful for
debugging issues and behavior.
For general debugging advice, see the :doc:`general advice document
</process/debugging/index>`.

.. contents::
    :depth: 3

The following sections show you the available tools.

printk() & friends
------------------

These are derivatives of printf() with varying destinations and support for
being dynamically turned on or off, or lack thereof.

Simple printk()
~~~~~~~~~~~~~~~

The classic, can be used to great effect for quick and dirty development
of new modules or to extract arbitrary necessary data for troubleshooting.

Prerequisite: ``CONFIG_PRINTK`` (usually enabled by default)

**Pros**:

- No need to learn anything, simple to use
- Easy to modify exactly to your needs (formatting of the data (See:
  :doc:`/core-api/printk-formats`), visibility in the log)
- Can cause delays in the execution of the code (beneficial to confirm whether
  timing is a factor)

**Cons**:

- Requires rebuilding the kernel/module
- Can cause delays in the execution of the code (which can cause issues to be
  not reproducible)

For the full documentation see :doc:`/core-api/printk-basics`

Trace_printk
~~~~~~~~~~~~

Prerequisite: ``CONFIG_DYNAMIC_FTRACE`` & ``#include <linux/ftrace.h>``

It is a tiny bit less comfortable to use than printk(), because you will have
to read the messages from the trace file (See: :ref:`read_ftrace_log`
instead of from the kernel log, but very useful when printk() adds unwanted
delays into the code execution, causing issues to be flaky or hidden.)

If the processing of this still causes timing issues then you can try
trace_puts().

For the full Documentation see trace_printk()

dev_dbg
~~~~~~~

Print statement, which can be targeted by
:ref:`process/debugging/userspace_debugging_guide:dynamic debug` that contains
additional information about the device used within the context.

**When is it appropriate to leave a debug print in the code?**

Permanent debug statements have to be useful for a developer to troubleshoot
driver misbehavior. Judging that is a bit more of an art than a science, but
some guidelines are in the :ref:`Coding style guidelines
<process/coding-style:13) printing kernel messages>`. In almost all cases the
debug statements shouldn't be upstreamed, as a working driver is supposed to be
silent.

Custom printk
~~~~~~~~~~~~~

Example::

  #define core_dbg(fmt, arg...) do { \
	  if (core_debug) \
		  printk(KERN_DEBUG pr_fmt("core: " fmt), ## arg); \
	  } while (0)

**When should you do this?**

It is better to just use a pr_debug(), which can later be turned on/off with
dynamic debug. Additionally, a lot of drivers activate these prints via a
variable like ``core_debug`` set by a module parameter. However, Module
parameters `are not recommended anymore
<https://lore.kernel.org/all/2024032757-surcharge-grime-d3dd@gregkh>`_.

Ftrace
------

Creating a custom Ftrace tracepoint
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A tracepoint adds a hook into your code that will be called and logged when the
tracepoint is enabled. This can be used, for example, to trace hitting a
conditional branch or to dump the internal state at specific points of the code
flow during a debugging session.

Here is a basic description of :ref:`how to implement new tracepoints
<trace/tracepoints:usage>`.

For the full event tracing documentation see :doc:`/trace/events`

For the full Ftrace documentation see :doc:`/trace/ftrace`

DebugFS
-------

Prerequisite: ``CONFIG_DEBUG_FS` & `#include <linux/debugfs.h>``

DebugFS differs from the other approaches of debugging, as it doesn't write
messages to the kernel log nor add traces to the code. Instead it allows the
developer to handle a set of files.
With these files you can either store values of variables or make
register/memory dumps or you can make these files writable and modify
values/settings in the driver.

Possible use-cases among others:

- Store register values
- Keep track of variables
- Store errors
- Store settings
- Toggle a setting like debug on/off
- Error injection

This is especially useful, when the size of a data dump would be hard to digest
as part of the general kernel log (for example when dumping raw bitstream data)
or when you are not interested in all the values all the time, but with the
possibility to inspect them.

The general idea is:

- Create a directory during probe (``struct dentry *parent =
  debugfs_create_dir("my_driver", NULL);``)
- Create a file (``debugfs_create_u32("my_value", 444, parent, &my_variable);``)

  - In this example the file is found in
    ``/sys/kernel/debug/my_driver/my_value`` (with read permissions for
    user/group/all)
  - any read of the file will return the current contents of the variable
    ``my_variable``

- Clean up the directory when removing the device
  (``debugfs_remove_recursive(parent);``)

For the full documentation see :doc:`/filesystems/debugfs`.

KASAN, UBSAN, lockdep and other error checkers
----------------------------------------------

KASAN (Kernel Address Sanitizer)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prerequisite: ``CONFIG_KASAN``

KASAN is a dynamic memory error detector that helps to find use-after-free and
out-of-bounds bugs. It uses compile-time instrumentation to check every memory
access.

For the full documentation see :doc:`/dev-tools/kasan`.

UBSAN (Undefined Behavior Sanitizer)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prerequisite: ``CONFIG_UBSAN``

UBSAN relies on compiler instrumentation and runtime checks to detect undefined
behavior. It is designed to find a variety of issues, including signed integer
overflow, array index out of bounds, and more.

For the full documentation see :doc:`/dev-tools/ubsan`

lockdep (Lock Dependency Validator)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prerequisite: ``CONFIG_DEBUG_LOCKDEP``

lockdep is a runtime lock dependency validator that detects potential deadlocks
and other locking-related issues in the kernel.
It tracks lock acquisitions and releases, building a dependency graph that is
analyzed for potential deadlocks.
lockdep is especially useful for validating the correctness of lock ordering in
the kernel.

PSI (Pressure stall information tracking)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prerequisite: ``CONFIG_PSI``

PSI is a measurement tool to identify excessive overcommits on hardware
resources, that can cause performance disruptions or even OOM kills.

device coredump
---------------

Prerequisite: ``CONFIG_DEV_COREDUMP`` & ``#include <linux/devcoredump.h>``

Provides the infrastructure for a driver to provide arbitrary data to userland.
It is most often used in conjunction with udev or similar userland application
to listen for kernel uevents, which indicate that the dump is ready. Udev has
rules to copy that file somewhere for long-term storage and analysis, as by
default, the data for the dump is automatically cleaned up after a default
5 minutes. That data is analyzed with driver-specific tools or GDB.

A device coredump can be created with a vmalloc area, with read/free
methods, or as a scatter/gather list.

You can find an example implementation at:
`drivers/media/platform/qcom/venus/core.c
<https://elixir.bootlin.com/linux/v6.11.6/source/drivers/media/platform/qcom/venus/core.c#L30>`__,
in the Bluetooth HCI layer, in several wireless drivers, and in several
DRM drivers.

devcoredump interfaces
~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: include/linux/devcoredump.h

.. kernel-doc:: drivers/base/devcoredump.c

**Copyright** ©2024 : Collabora
