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

BUG() and BUG_ON()
------------------
Use WARN() and WARN_ON() instead, and handle the "impossible"
error condition as gracefully as possible. While the BUG()-family
of APIs were originally designed to act as an "impossible situation"
assert and to kill a kernel thread "safely", they turn out to just be
too risky. (e.g. "In what order do locks need to be released? Have
various states been restored?") Very commonly, using BUG() will
destabilize a system or entirely break it, which makes it impossible
to debug or even get viable crash reports. Linus has `very strong
<https://lore.kernel.org/lkml/CA+55aFy6jNLsywVYdGp83AMrXBo_P-pkjkphPGrO=82SPKCpLQ@mail.gmail.com/>`_
feelings `about this
<https://lore.kernel.org/lkml/CAHk-=whDHsbK3HTOpTF=ue_o04onRwTEaK_ZoJp_fjbqq4+=Jw@mail.gmail.com/>`_.

Note that the WARN()-family should only be used for "expected to
be unreachable" situations. If you want to warn about "reachable
but undesirable" situations, please use the pr_warn()-family of
functions. System owners may have set the *panic_on_warn* sysctl,
to make sure their systems do not continue running in the face of
"unreachable" conditions. (For example, see commits like `this one
<https://git.kernel.org/linus/d4689846881d160a4d12a514e991a740bcb5d65a>`_.)

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

See array_size(), array3_size(), and struct_size(),
for more details as well as the related check_add_overflow() and
check_mul_overflow() family of functions.

simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
The simple_strtol(), simple_strtoll(),
simple_strtoul(), and simple_strtoull() functions
explicitly ignore overflows, which may lead to unexpected results
in callers. The respective kstrtol(), kstrtoll(),
kstrtoul(), and kstrtoull() functions tend to be the
correct replacements, though note that those require the string to be
NUL or newline terminated.

strcpy()
--------
strcpy() performs no bounds checking on the destination
buffer. This could result in linear overflows beyond the
end of the buffer, leading to all kinds of misbehaviors. While
`CONFIG_FORTIFY_SOURCE=y` and various compiler flags help reduce the
risk of using this function, there is no good reason to add new uses of
this function. The safe replacement is strscpy().

strncpy() on NUL-terminated strings
-----------------------------------
Use of strncpy() does not guarantee that the destination buffer
will be NUL terminated. This can lead to various linear read overflows
and other misbehavior due to the missing termination. It also NUL-pads the
destination buffer if the source contents are shorter than the destination
buffer size, which may be a needless performance penalty for callers using
only NUL-terminated strings. The safe replacement is strscpy().
(Users of strscpy() still needing NUL-padding should instead
use strscpy_pad().)

If a caller is using non-NUL-terminated strings, strncpy()() can
still be used, but destinations should be marked with the `__nonstring
<https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>`_
attribute to avoid future compiler warnings.

strlcpy()
---------
strlcpy() reads the entire source buffer first, possibly exceeding
the given limit of bytes to copy. This is inefficient and can lead to
linear read overflows if a source string is not NUL-terminated. The
safe replacement is strscpy().

%p format specifier
-------------------
Traditionally, using "%p" in format strings would lead to regular address
exposure flaws in dmesg, proc, sysfs, etc. Instead of leaving these to
be exploitable, all "%p" uses in the kernel are being printed as a hashed
value, rendering them unusable for addressing. New uses of "%p" should not
be added to the kernel. For text addresses, using "%pS" is likely better,
as it produces the more useful symbol name instead. For nearly everything
else, just do not add "%p" at all.

Paraphrasing Linus's current `guidance <https://lore.kernel.org/lkml/CA+55aFwQEd_d40g4mUCSsVRZzrFPUJt74vc6PPpb675hYNXcKw@mail.gmail.com/>`_:

- If the hashed "%p" value is pointless, ask yourself whether the pointer
  itself is important. Maybe it should be removed entirely?
- If you really think the true pointer value is important, why is some
  system state or user privilege level considered "special"? If you think
  you can justify it (in comments and commit log) well enough to stand
  up to Linus's scrutiny, maybe you can use "%px", along with making sure
  you have sensible permissions.

And finally, know that a toggle for "%p" hashing will `not be accepted <https://lore.kernel.org/lkml/CA+55aFwieC1-nAs+NFq9RTwaR8ef9hWa4MjNBWL41F-8wM49eA@mail.gmail.com/>`_.

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
The C language allows switch cases to fall through to the next case
when a "break" statement is missing at the end of a case. This, however,
introduces ambiguity in the code, as it's not always clear if the missing
break is intentional or a bug. For example, it's not obvious just from
looking at the code if `STATE_ONE` is intentionally designed to fall
through into `STATE_TWO`::

	switch (value) {
	case STATE_ONE:
		do_something();
	case STATE_TWO:
		do_other();
		break;
	default:
		WARN("unknown state");
	}

As there have been a long list of flaws `due to missing "break" statements
<https://cwe.mitre.org/data/definitions/484.html>`_, we no longer allow
implicit fall-through. In order to identify intentional fall-through
cases, we have adopted a pseudo-keyword macro "fallthrough" which
expands to gcc's extension `__attribute__((__fallthrough__))
<https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html>`_.
(When the C17/C18  `[[fallthrough]]` syntax is more commonly supported by
C compilers, static analyzers, and IDEs, we can switch to using that syntax
for the macro pseudo-keyword.)

All switch/case blocks must end in one of:

* break;
* fallthrough;
* continue;
* goto <label>;
* return [expression];
