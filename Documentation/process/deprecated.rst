.. SPDX-License-Identifier: GPL-2.0

.. _deprecated:

=====================================================================
Deprecated Interfaces, Language Features, Attributes, and Conventions
=====================================================================

In a perfect world, it would be possible to convert all instances of
some deprecated API into the new API and entirely remove the old API in
a single development cycle. However, due to the size of the kernel, the
maintainership hierarchy, and timing, it's not always feasible to do these
kinds of conversions at once. This means that new instances may sneak into
the kernel while old ones are being removed, only making the amount of
work to remove the API grow. In order to educate developers about what
has been deprecated and why, this list has been created as a place to
point when uses of deprecated things are proposed for inclusion in the
kernel.

__deprecated
------------
While this attribute does visually mark an interface as deprecated,
it `does not produce warnings during builds any more
<https://git.kernel.org/linus/771c035372a036f83353eef46dbb829780330234>`_
because one of the standing goals of the kernel is to build without
warnings and no one was actually doing anything to remove these deprecated
interfaces. While using `__deprecated` is nice to note an old API in
a header file, it isn't the full solution. Such interfaces must either
be fully removed from the kernel, or added to this file to discourage
others from using them in the future.

open-coded arithmetic in allocator arguments
--------------------------------------------
Dynamic size calculations (especially multiplication) should not be
performed in memory allocator (or similar) function arguments due to the
risk of them overflowing. This could lead to values wrapping around and a
smaller allocation being made than the caller was expecting. Using those
allocations could lead to linear overflows of heap memory and other
misbehaviors. (One exception to this is literal values where the compiler
can warn if they might overflow. Though using literals for arguments as
suggested below is also harmless.)

For example, do not use ``count * size`` as an argument, as in::

	foo = kmalloc(count * size, GFP_KERNEL);

Instead, the 2-factor form of the allocator should be used::

	foo = kmalloc_array(count, size, GFP_KERNEL);

If no 2-factor form is available, the saturate-on-overflow helpers should
be used::

	bar = vmalloc(array_size(count, size));

Another common case to avoid is calculating the size of a structure with
a trailing array of others structures, as in::

	header = kzalloc(sizeof(*header) + count * sizeof(*header->item),
			 GFP_KERNEL);

Instead, use the helper::

	header = kzalloc(struct_size(header, item, count), GFP_KERNEL);

See :c:func:`array_size`, :c:func:`array3_size`, and :c:func:`struct_size`,
for more details as well as the related :c:func:`check_add_overflow` and
:c:func:`check_mul_overflow` family of functions.

simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
The :c:func:`simple_strtol`, :c:func:`simple_strtoll`,
:c:func:`simple_strtoul`, and :c:func:`simple_strtoull` functions
explicitly ignore overflows, which may lead to unexpected results
in callers. The respective :c:func:`kstrtol`, :c:func:`kstrtoll`,
:c:func:`kstrtoul`, and :c:func:`kstrtoull` functions tend to be the
correct replacements, though note that those require the string to be
NUL or newline terminated.

strcpy()
--------
:c:func:`strcpy` performs no bounds checking on the destination
buffer. This could result in linear overflows beyond the
end of the buffer, leading to all kinds of misbehaviors. While
`CONFIG_FORTIFY_SOURCE=y` and various compiler flags help reduce the
risk of using this function, there is no good reason to add new uses of
this function. The safe replacement is :c:func:`strscpy`.

strncpy() on NUL-terminated strings
-----------------------------------
Use of :c:func:`strncpy` does not guarantee that the destination buffer
will be NUL terminated. This can lead to various linear read overflows
and other misbehavior due to the missing termination. It also NUL-pads the
destination buffer if the source contents are shorter than the destination
buffer size, which may be a needless performance penalty for callers using
only NUL-terminated strings. The safe replacement is :c:func:`strscpy`.
(Users of :c:func:`strscpy` still needing NUL-padding will need an
explicit :c:func:`memset` added.)

If a caller is using non-NUL-terminated strings, :c:func:`strncpy()` can
still be used, but destinations should be marked with the `__nonstring
<https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>`_
attribute to avoid future compiler warnings.

strlcpy()
---------
:c:func:`strlcpy` reads the entire source buffer first, possibly exceeding
the given limit of bytes to copy. This is inefficient and can lead to
linear read overflows if a source string is not NUL-terminated. The
safe replacement is :c:func:`strscpy`.

Variable Length Arrays (VLAs)
-----------------------------
Using stack VLAs produces much worse machine code than statically
sized stack arrays. While these non-trivial `performance issues
<https://git.kernel.org/linus/02361bc77888>`_ are reason enough to
eliminate VLAs, they are also a security risk. Dynamic growth of a stack
array may exceed the remaining memory in the stack segment. This could
lead to a crash, possible overwriting sensitive contents at the end of the
stack (when built without `CONFIG_THREAD_INFO_IN_TASK=y`), or overwriting
memory adjacent to the stack (when built without `CONFIG_VMAP_STACK=y`)

Implicit switch case fall-through
---------------------------------
The C language allows switch cases to "fall through" when
a "break" statement is missing at the end of a case. This,
however, introduces ambiguity in the code, as it's not always
clear if the missing break is intentional or a bug. As there
have been a long list of flaws `due to missing "break" statements
<https://cwe.mitre.org/data/definitions/484.html>`_, we no longer allow
"implicit fall-through". In order to identify an intentional fall-through
case, we have adopted the marking used by static analyzers: a comment
saying `/* Fall through */`. Once the C++17 `__attribute__((fallthrough))`
is more widely handled by C compilers, static analyzers, and IDEs, we can
switch to using that instead.
