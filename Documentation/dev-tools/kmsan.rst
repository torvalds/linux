.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2022, Google LLC.

===============================
Kernel Memory Sanitizer (KMSAN)
===============================

KMSAN is a dynamic error detector aimed at finding uses of uninitialized
values. It is based on compiler instrumentation, and is quite similar to the
userspace `MemorySanitizer tool`_.

An important note is that KMSAN is not intended for production use, because it
drastically increases kernel memory footprint and slows the whole system down.

Usage
=====

Building the kernel
-------------------

In order to build a kernel with KMSAN you will need a fresh Clang (14.0.6+).
Please refer to `LLVM documentation`_ for the instructions on how to build Clang.

Now configure and build the kernel with CONFIG_KMSAN enabled.

Example report
--------------

Here is an example of a KMSAN report::

  =====================================================
  BUG: KMSAN: uninit-value in test_uninit_kmsan_check_memory+0x1be/0x380 [kmsan_test]
   test_uninit_kmsan_check_memory+0x1be/0x380 mm/kmsan/kmsan_test.c:273
   kunit_run_case_internal lib/kunit/test.c:333
   kunit_try_run_case+0x206/0x420 lib/kunit/test.c:374
   kunit_generic_run_threadfn_adapter+0x6d/0xc0 lib/kunit/try-catch.c:28
   kthread+0x721/0x850 kernel/kthread.c:327
   ret_from_fork+0x1f/0x30 ??:?

  Uninit was stored to memory at:
   do_uninit_local_array+0xfa/0x110 mm/kmsan/kmsan_test.c:260
   test_uninit_kmsan_check_memory+0x1a2/0x380 mm/kmsan/kmsan_test.c:271
   kunit_run_case_internal lib/kunit/test.c:333
   kunit_try_run_case+0x206/0x420 lib/kunit/test.c:374
   kunit_generic_run_threadfn_adapter+0x6d/0xc0 lib/kunit/try-catch.c:28
   kthread+0x721/0x850 kernel/kthread.c:327
   ret_from_fork+0x1f/0x30 ??:?

  Local variable uninit created at:
   do_uninit_local_array+0x4a/0x110 mm/kmsan/kmsan_test.c:256
   test_uninit_kmsan_check_memory+0x1a2/0x380 mm/kmsan/kmsan_test.c:271

  Bytes 4-7 of 8 are uninitialized
  Memory access of size 8 starts at ffff888083fe3da0

  CPU: 0 PID: 6731 Comm: kunit_try_catch Tainted: G    B       E     5.16.0-rc3+ #104
  Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.14.0-2 04/01/2014
  =====================================================

The report says that the local variable ``uninit`` was created uninitialized in
``do_uninit_local_array()``. The third stack trace corresponds to the place
where this variable was created.

The first stack trace shows where the uninit value was used (in
``test_uninit_kmsan_check_memory()``). The tool shows the bytes which were left
uninitialized in the local variable, as well as the stack where the value was
copied to another memory location before use.

A use of uninitialized value ``v`` is reported by KMSAN in the following cases:

 - in a condition, e.g. ``if (v) { ... }``;
 - in an indexing or pointer dereferencing, e.g. ``array[v]`` or ``*v``;
 - when it is copied to userspace or hardware, e.g. ``copy_to_user(..., &v, ...)``;
 - when it is passed as an argument to a function, and
   ``CONFIG_KMSAN_CHECK_PARAM_RETVAL`` is enabled (see below).

The mentioned cases (apart from copying data to userspace or hardware, which is
a security issue) are considered undefined behavior from the C11 Standard point
of view.

Disabling the instrumentation
-----------------------------

A function can be marked with ``__no_kmsan_checks``. Doing so makes KMSAN
ignore uninitialized values in that function and mark its output as initialized.
As a result, the user will not get KMSAN reports related to that function.

Another function attribute supported by KMSAN is ``__no_sanitize_memory``.
Applying this attribute to a function will result in KMSAN not instrumenting
it, which can be helpful if we do not want the compiler to interfere with some
low-level code (e.g. that marked with ``noinstr`` which implicitly adds
``__no_sanitize_memory``).

This however comes at a cost: stack allocations from such functions will have
incorrect shadow/origin values, likely leading to false positives. Functions
called from non-instrumented code may also receive incorrect metadata for their
parameters.

As a rule of thumb, avoid using ``__no_sanitize_memory`` explicitly.

It is also possible to disable KMSAN for a single file (e.g. main.o)::

  KMSAN_SANITIZE_main.o := n

or for the whole directory::

  KMSAN_SANITIZE := n

in the Makefile. Think of this as applying ``__no_sanitize_memory`` to every
function in the file or directory. Most users won't need KMSAN_SANITIZE, unless
their code gets broken by KMSAN (e.g. runs at early boot time).

KMSAN checks can also be temporarily disabled for the current task using
``kmsan_disable_current()`` and ``kmsan_enable_current()`` calls. Each
``kmsan_enable_current()`` call must be preceded by a
``kmsan_disable_current()`` call; these call pairs may be nested. One needs to
be careful with these calls, keeping the regions short and preferring other
ways to disable instrumentation, where possible.

Support
=======

In order for KMSAN to work the kernel must be built with Clang, which so far is
the only compiler that has KMSAN support. The kernel instrumentation pass is
based on the userspace `MemorySanitizer tool`_.

The runtime library only supports x86_64 at the moment.

How KMSAN works
===============

KMSAN shadow memory
-------------------

KMSAN associates a metadata byte (also called shadow byte) with every byte of
kernel memory. A bit in the shadow byte is set iff the corresponding bit of the
kernel memory byte is uninitialized. Marking the memory uninitialized (i.e.
setting its shadow bytes to ``0xff``) is called poisoning, marking it
initialized (setting the shadow bytes to ``0x00``) is called unpoisoning.

When a new variable is allocated on the stack, it is poisoned by default by
instrumentation code inserted by the compiler (unless it is a stack variable
that is immediately initialized). Any new heap allocation done without
``__GFP_ZERO`` is also poisoned.

Compiler instrumentation also tracks the shadow values as they are used along
the code. When needed, instrumentation code invokes the runtime library in
``mm/kmsan/`` to persist shadow values.

The shadow value of a basic or compound type is an array of bytes of the same
length. When a constant value is written into memory, that memory is unpoisoned.
When a value is read from memory, its shadow memory is also obtained and
propagated into all the operations which use that value. For every instruction
that takes one or more values the compiler generates code that calculates the
shadow of the result depending on those values and their shadows.

Example::

  int a = 0xff;  // i.e. 0x000000ff
  int b;
  int c = a | b;

In this case the shadow of ``a`` is ``0``, shadow of ``b`` is ``0xffffffff``,
shadow of ``c`` is ``0xffffff00``. This means that the upper three bytes of
``c`` are uninitialized, while the lower byte is initialized.

Origin tracking
---------------

Every four bytes of kernel memory also have a so-called origin mapped to them.
This origin describes the point in program execution at which the uninitialized
value was created. Every origin is associated with either the full allocation
stack (for heap-allocated memory), or the function containing the uninitialized
variable (for locals).

When an uninitialized variable is allocated on stack or heap, a new origin
value is created, and that variable's origin is filled with that value. When a
value is read from memory, its origin is also read and kept together with the
shadow. For every instruction that takes one or more values, the origin of the
result is one of the origins corresponding to any of the uninitialized inputs.
If a poisoned value is written into memory, its origin is written to the
corresponding storage as well.

Example 1::

  int a = 42;
  int b;
  int c = a + b;

In this case the origin of ``b`` is generated upon function entry, and is
stored to the origin of ``c`` right before the addition result is written into
memory.

Several variables may share the same origin address, if they are stored in the
same four-byte chunk. In this case every write to either variable updates the
origin for all of them. We have to sacrifice precision in this case, because
storing origins for individual bits (and even bytes) would be too costly.

Example 2::

  int combine(short a, short b) {
    union ret_t {
      int i;
      short s[2];
    } ret;
    ret.s[0] = a;
    ret.s[1] = b;
    return ret.i;
  }

If ``a`` is initialized and ``b`` is not, the shadow of the result would be
0xffff0000, and the origin of the result would be the origin of ``b``.
``ret.s[0]`` would have the same origin, but it will never be used, because
that variable is initialized.

If both function arguments are uninitialized, only the origin of the second
argument is preserved.

Origin chaining
~~~~~~~~~~~~~~~

To ease debugging, KMSAN creates a new origin for every store of an
uninitialized value to memory. The new origin references both its creation stack
and the previous origin the value had. This may cause increased memory
consumption, so we limit the length of origin chains in the runtime.

Clang instrumentation API
-------------------------

Clang instrumentation pass inserts calls to functions defined in
``mm/kmsan/nstrumentation.c`` into the kernel code.

Shadow manipulation
~~~~~~~~~~~~~~~~~~~

For every memory access the compiler emits a call to a function that returns a
pair of pointers to the shadow and origin addresses of the given memory::

  typedef struct {
    void *shadow, *origin;
  } shadow_origin_ptr_t

  shadow_origin_ptr_t __msan_metadata_ptr_for_load_{1,2,4,8}(void *addr)
  shadow_origin_ptr_t __msan_metadata_ptr_for_store_{1,2,4,8}(void *addr)
  shadow_origin_ptr_t __msan_metadata_ptr_for_load_n(void *addr, uintptr_t size)
  shadow_origin_ptr_t __msan_metadata_ptr_for_store_n(void *addr, uintptr_t size)

The function name depends on the memory access size.

The compiler makes sure that for every loaded value its shadow and origin
values are read from memory. When a value is stored to memory, its shadow and
origin are also stored using the metadata pointers.

Handling locals
~~~~~~~~~~~~~~~

A special function is used to create a new origin value for a local variable and
set the origin of that variable to that value::

  void __msan_poison_alloca(void *addr, uintptr_t size, char *descr)

Access to per-task data
~~~~~~~~~~~~~~~~~~~~~~~

At the beginning of every instrumented function KMSAN inserts a call to
``__msan_get_context_state()``::

  kmsan_context_state *__msan_get_context_state(void)

``kmsan_context_state`` is declared in ``include/linux/kmsan.h``::

  struct kmsan_context_state {
    char param_tls[KMSAN_PARAM_SIZE];
    char retval_tls[KMSAN_RETVAL_SIZE];
    char va_arg_tls[KMSAN_PARAM_SIZE];
    char va_arg_origin_tls[KMSAN_PARAM_SIZE];
    u64 va_arg_overflow_size_tls;
    char param_origin_tls[KMSAN_PARAM_SIZE];
    depot_stack_handle_t retval_origin_tls;
  };

This structure is used by KMSAN to pass parameter shadows and origins between
instrumented functions (unless the parameters are checked immediately by
``CONFIG_KMSAN_CHECK_PARAM_RETVAL``).

Passing uninitialized values to functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Clang's MemorySanitizer instrumentation has an option,
``-fsanitize-memory-param-retval``, which makes the compiler check function
parameters passed by value, as well as function return values.

The option is controlled by ``CONFIG_KMSAN_CHECK_PARAM_RETVAL``, which is
enabled by default to let KMSAN report uninitialized values earlier.
Please refer to the `LKML discussion`_ for more details.

Because of the way the checks are implemented in LLVM (they are only applied to
parameters marked as ``noundef``), not all parameters are guaranteed to be
checked, so we cannot give up the metadata storage in ``kmsan_context_state``.

String functions
~~~~~~~~~~~~~~~~

The compiler replaces calls to ``memcpy()``/``memmove()``/``memset()`` with the
following functions. These functions are also called when data structures are
initialized or copied, making sure shadow and origin values are copied alongside
with the data::

  void *__msan_memcpy(void *dst, void *src, uintptr_t n)
  void *__msan_memmove(void *dst, void *src, uintptr_t n)
  void *__msan_memset(void *dst, int c, uintptr_t n)

Error reporting
~~~~~~~~~~~~~~~

For each use of a value the compiler emits a shadow check that calls
``__msan_warning()`` in the case that value is poisoned::

  void __msan_warning(u32 origin)

``__msan_warning()`` causes KMSAN runtime to print an error report.

Inline assembly instrumentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

KMSAN instruments every inline assembly output with a call to::

  void __msan_instrument_asm_store(void *addr, uintptr_t size)

, which unpoisons the memory region.

This approach may mask certain errors, but it also helps to avoid a lot of
false positives in bitwise operations, atomics etc.

Sometimes the pointers passed into inline assembly do not point to valid memory.
In such cases they are ignored at runtime.


Runtime library
---------------

The code is located in ``mm/kmsan/``.

Per-task KMSAN state
~~~~~~~~~~~~~~~~~~~~

Every task_struct has an associated KMSAN task state that holds the KMSAN
context (see above) and a per-task counter disallowing KMSAN reports::

  struct kmsan_context {
    ...
    unsigned int depth;
    struct kmsan_context_state cstate;
    ...
  }

  struct task_struct {
    ...
    struct kmsan_context kmsan;
    ...
  }

KMSAN contexts
~~~~~~~~~~~~~~

When running in a kernel task context, KMSAN uses ``current->kmsan.cstate`` to
hold the metadata for function parameters and return values.

But in the case the kernel is running in the interrupt, softirq or NMI context,
where ``current`` is unavailable, KMSAN switches to per-cpu interrupt state::

  DEFINE_PER_CPU(struct kmsan_ctx, kmsan_percpu_ctx);

Metadata allocation
~~~~~~~~~~~~~~~~~~~

There are several places in the kernel for which the metadata is stored.

1. Each ``struct page`` instance contains two pointers to its shadow and
origin pages::

  struct page {
    ...
    struct page *shadow, *origin;
    ...
  };

At boot-time, the kernel allocates shadow and origin pages for every available
kernel page. This is done quite late, when the kernel address space is already
fragmented, so normal data pages may arbitrarily interleave with the metadata
pages.

This means that in general for two contiguous memory pages their shadow/origin
pages may not be contiguous. Consequently, if a memory access crosses the
boundary of a memory block, accesses to shadow/origin memory may potentially
corrupt other pages or read incorrect values from them.

In practice, contiguous memory pages returned by the same ``alloc_pages()``
call will have contiguous metadata, whereas if these pages belong to two
different allocations their metadata pages can be fragmented.

For the kernel data (``.data``, ``.bss`` etc.) and percpu memory regions
there also are no guarantees on metadata contiguity.

In the case ``__msan_metadata_ptr_for_XXX_YYY()`` hits the border between two
pages with non-contiguous metadata, it returns pointers to fake shadow/origin regions::

  char dummy_load_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
  char dummy_store_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

``dummy_load_page`` is zero-initialized, so reads from it always yield zeroes.
All stores to ``dummy_store_page`` are ignored.

2. For vmalloc memory and modules, there is a direct mapping between the memory
range, its shadow and origin. KMSAN reduces the vmalloc area by 3/4, making only
the first quarter available to ``vmalloc()``. The second quarter of the vmalloc
area contains shadow memory for the first quarter, the third one holds the
origins. A small part of the fourth quarter contains shadow and origins for the
kernel modules. Please refer to ``arch/x86/include/asm/pgtable_64_types.h`` for
more details.

When an array of pages is mapped into a contiguous virtual memory space, their
shadow and origin pages are similarly mapped into contiguous regions.

References
==========

E. Stepanov, K. Serebryany. `MemorySanitizer: fast detector of uninitialized
memory use in C++
<https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/43308.pdf>`_.
In Proceedings of CGO 2015.

.. _MemorySanitizer tool: https://clang.llvm.org/docs/MemorySanitizer.html
.. _LLVM documentation: https://llvm.org/docs/GettingStarted.html
.. _LKML discussion: https://lore.kernel.org/all/20220614144853.3693273-1-glider@google.com/
