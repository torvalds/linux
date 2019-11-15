The Kernel Address Sanitizer (KASAN)
====================================

Overview
--------

KernelAddressSANitizer (KASAN) is a dynamic memory error detector designed to
find out-of-bound and use-after-free bugs. KASAN has two modes: generic KASAN
(similar to userspace ASan) and software tag-based KASAN (similar to userspace
HWASan).

KASAN uses compile-time instrumentation to insert validity checks before every
memory access, and therefore requires a compiler version that supports that.

Generic KASAN is supported in both GCC and Clang. With GCC it requires version
4.9.2 or later for basic support and version 5.0 or later for detection of
out-of-bounds accesses for stack and global variables and for inline
instrumentation mode (see the Usage section). With Clang it requires version
7.0.0 or later and it doesn't support detection of out-of-bounds accesses for
global variables yet.

Tag-based KASAN is only supported in Clang and requires version 7.0.0 or later.

Currently generic KASAN is supported for the x86_64, arm64, xtensa and s390
architectures, and tag-based KASAN is supported only for arm64.

Usage
-----

To enable KASAN configure kernel with::

	  CONFIG_KASAN = y

and choose between CONFIG_KASAN_GENERIC (to enable generic KASAN) and
CONFIG_KASAN_SW_TAGS (to enable software tag-based KASAN).

You also need to choose between CONFIG_KASAN_OUTLINE and CONFIG_KASAN_INLINE.
Outline and inline are compiler instrumentation types. The former produces
smaller binary while the latter is 1.1 - 2 times faster.

Both KASAN modes work with both SLUB and SLAB memory allocators.
For better bug detection and nicer reporting, enable CONFIG_STACKTRACE.

To augment reports with last allocation and freeing stack of the physical page,
it is recommended to enable also CONFIG_PAGE_OWNER and boot with page_owner=on.

To disable instrumentation for specific files or directories, add a line
similar to the following to the respective kernel Makefile:

- For a single file (e.g. main.o)::

    KASAN_SANITIZE_main.o := n

- For all files in one directory::

    KASAN_SANITIZE := n

Error reports
~~~~~~~~~~~~~

A typical out-of-bounds access generic KASAN report looks like this::

    ==================================================================
    BUG: KASAN: slab-out-of-bounds in kmalloc_oob_right+0xa8/0xbc [test_kasan]
    Write of size 1 at addr ffff8801f44ec37b by task insmod/2760

    CPU: 1 PID: 2760 Comm: insmod Not tainted 4.19.0-rc3+ #698
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.10.2-1 04/01/2014
    Call Trace:
     dump_stack+0x94/0xd8
     print_address_description+0x73/0x280
     kasan_report+0x144/0x187
     __asan_report_store1_noabort+0x17/0x20
     kmalloc_oob_right+0xa8/0xbc [test_kasan]
     kmalloc_tests_init+0x16/0x700 [test_kasan]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9
    RIP: 0033:0x7f96443109da
    RSP: 002b:00007ffcf0b51b08 EFLAGS: 00000202 ORIG_RAX: 00000000000000af
    RAX: ffffffffffffffda RBX: 000055dc3ee521a0 RCX: 00007f96443109da
    RDX: 00007f96445cff88 RSI: 0000000000057a50 RDI: 00007f9644992000
    RBP: 000055dc3ee510b0 R08: 0000000000000003 R09: 0000000000000000
    R10: 00007f964430cd0a R11: 0000000000000202 R12: 00007f96445cff88
    R13: 000055dc3ee51090 R14: 0000000000000000 R15: 0000000000000000

    Allocated by task 2760:
     save_stack+0x43/0xd0
     kasan_kmalloc+0xa7/0xd0
     kmem_cache_alloc_trace+0xe1/0x1b0
     kmalloc_oob_right+0x56/0xbc [test_kasan]
     kmalloc_tests_init+0x16/0x700 [test_kasan]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    Freed by task 815:
     save_stack+0x43/0xd0
     __kasan_slab_free+0x135/0x190
     kasan_slab_free+0xe/0x10
     kfree+0x93/0x1a0
     umh_complete+0x6a/0xa0
     call_usermodehelper_exec_async+0x4c3/0x640
     ret_from_fork+0x35/0x40

    The buggy address belongs to the object at ffff8801f44ec300
     which belongs to the cache kmalloc-128 of size 128
    The buggy address is located 123 bytes inside of
     128-byte region [ffff8801f44ec300, ffff8801f44ec380)
    The buggy address belongs to the page:
    page:ffffea0007d13b00 count:1 mapcount:0 mapping:ffff8801f7001640 index:0x0
    flags: 0x200000000000100(slab)
    raw: 0200000000000100 ffffea0007d11dc0 0000001a0000001a ffff8801f7001640
    raw: 0000000000000000 0000000080150015 00000001ffffffff 0000000000000000
    page dumped because: kasan: bad access detected

    Memory state around the buggy address:
     ffff8801f44ec200: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec280: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    >ffff8801f44ec300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03
                                                                    ^
     ffff8801f44ec380: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec400: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    ==================================================================

The header of the report provides a short summary of what kind of bug happened
and what kind of access caused it. It's followed by a stack trace of the bad
access, a stack trace of where the accessed memory was allocated (in case bad
access happens on a slab object), and a stack trace of where the object was
freed (in case of a use-after-free bug report). Next comes a description of
the accessed slab object and information about the accessed memory page.

In the last section the report shows memory state around the accessed address.
Reading this part requires some understanding of how KASAN works.

The state of each 8 aligned bytes of memory is encoded in one shadow byte.
Those 8 bytes can be accessible, partially accessible, freed or be a redzone.
We use the following encoding for each shadow byte: 0 means that all 8 bytes
of the corresponding memory region are accessible; number N (1 <= N <= 7) means
that the first N bytes are accessible, and other (8 - N) bytes are not;
any negative value indicates that the entire 8-byte word is inaccessible.
We use different negative values to distinguish between different kinds of
inaccessible memory like redzones or freed memory (see mm/kasan/kasan.h).

In the report above the arrows point to the shadow byte 03, which means that
the accessed address is partially accessible.

For tag-based KASAN this last report section shows the memory tags around the
accessed address (see Implementation details section).


Implementation details
----------------------

Generic KASAN
~~~~~~~~~~~~~

From a high level, our approach to memory error detection is similar to that
of kmemcheck: use shadow memory to record whether each byte of memory is safe
to access, and use compile-time instrumentation to insert checks of shadow
memory on each memory access.

Generic KASAN dedicates 1/8th of kernel memory to its shadow memory (e.g. 16TB
to cover 128TB on x86_64) and uses direct mapping with a scale and offset to
translate a memory address to its corresponding shadow address.

Here is the function which translates an address to its corresponding shadow
address::

    static inline void *kasan_mem_to_shadow(const void *addr)
    {
	return ((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
    }

where ``KASAN_SHADOW_SCALE_SHIFT = 3``.

Compile-time instrumentation is used to insert memory access checks. Compiler
inserts function calls (__asan_load*(addr), __asan_store*(addr)) before each
memory access of size 1, 2, 4, 8 or 16. These functions check whether memory
access is valid or not by checking corresponding shadow memory.

GCC 5.0 has possibility to perform inline instrumentation. Instead of making
function calls GCC directly inserts the code to check the shadow memory.
This option significantly enlarges kernel but it gives x1.1-x2 performance
boost over outline instrumented kernel.

Software tag-based KASAN
~~~~~~~~~~~~~~~~~~~~~~~~

Tag-based KASAN uses the Top Byte Ignore (TBI) feature of modern arm64 CPUs to
store a pointer tag in the top byte of kernel pointers. Like generic KASAN it
uses shadow memory to store memory tags associated with each 16-byte memory
cell (therefore it dedicates 1/16th of the kernel memory for shadow memory).

On each memory allocation tag-based KASAN generates a random tag, tags the
allocated memory with this tag, and embeds this tag into the returned pointer.
Software tag-based KASAN uses compile-time instrumentation to insert checks
before each memory access. These checks make sure that tag of the memory that
is being accessed is equal to tag of the pointer that is used to access this
memory. In case of a tag mismatch tag-based KASAN prints a bug report.

Software tag-based KASAN also has two instrumentation modes (outline, that
emits callbacks to check memory accesses; and inline, that performs the shadow
memory checks inline). With outline instrumentation mode, a bug report is
simply printed from the function that performs the access check. With inline
instrumentation a brk instruction is emitted by the compiler, and a dedicated
brk handler is used to print bug reports.

A potential expansion of this mode is a hardware tag-based mode, which would
use hardware memory tagging support instead of compiler instrumentation and
manual shadow memory manipulation.
