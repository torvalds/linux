.. SPDX-License-Identifier: GPL-2.0+

Floating-point API
==================

Kernel code is normally prohibited from using floating-point (FP) registers or
instructions, including the C float and double data types. This rule reduces
system call overhead, because the kernel does not need to save and restore the
userspace floating-point register state.

However, occasionally drivers or library functions may need to include FP code.
This is supported by isolating the functions containing FP code to a separate
translation unit (a separate source file), and saving/restoring the FP register
state around calls to those functions. This creates "critical sections" of
floating-point usage.

The reason for this isolation is to prevent the compiler from generating code
touching the FP registers outside these critical sections. Compilers sometimes
use FP registers to optimize inlined ``memcpy`` or variable assignment, as
floating-point registers may be wider than general-purpose registers.

Usability of floating-point code within the kernel is architecture-specific.
Additionally, because a single kernel may be configured to support platforms
both with and without a floating-point unit, FPU availability must be checked
both at build time and at run time.

Several architectures implement the generic kernel floating-point API from
``linux/fpu.h``, as described below. Some other architectures implement their
own unique APIs, which are documented separately.

Build-time API
--------------

Floating-point code may be built if the option ``ARCH_HAS_KERNEL_FPU_SUPPORT``
is enabled. For C code, such code must be placed in a separate file, and that
file must have its compilation flags adjusted using the following pattern::

    CFLAGS_foo.o += $(CC_FLAGS_FPU)
    CFLAGS_REMOVE_foo.o += $(CC_FLAGS_NO_FPU)

Architectures are expected to define one or both of these variables in their
top-level Makefile as needed. For example::

    CC_FLAGS_FPU := -mhard-float

or::

    CC_FLAGS_NO_FPU := -msoft-float

Normal kernel code is assumed to use the equivalent of ``CC_FLAGS_NO_FPU``.

Runtime API
-----------

The runtime API is provided in ``linux/fpu.h``. This header cannot be included
from files implementing FP code (those with their compilation flags adjusted as
above). Instead, it must be included when defining the FP critical sections.

.. c:function:: bool kernel_fpu_available( void )

        This function reports if floating-point code can be used on this CPU or
        platform. The value returned by this function is not expected to change
        at runtime, so it only needs to be called once, not before every
        critical section.

.. c:function:: void kernel_fpu_begin( void )
                void kernel_fpu_end( void )

        These functions create a floating-point critical section. It is only
        valid to call ``kernel_fpu_begin()`` after a previous call to
        ``kernel_fpu_available()`` returned ``true``. These functions are only
        guaranteed to be callable from (preemptible or non-preemptible) process
        context.

        Preemption may be disabled inside critical sections, so their size
        should be minimized. They are *not* required to be reentrant. If the
        caller expects to nest critical sections, it must implement its own
        reference counting.
