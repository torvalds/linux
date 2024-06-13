.. _programming_language:

Programming Language
====================

The kernel is written in the C programming language [c-language]_.
More precisely, the kernel is typically compiled with ``gcc`` [gcc]_
under ``-std=gnu11`` [gcc-c-dialect-options]_: the GNU dialect of ISO C11.
``clang`` [clang]_ is also supported, see docs on
:ref:`Building Linux with Clang/LLVM <kbuild_llvm>`.

This dialect contains many extensions to the language [gnu-extensions]_,
and many of them are used within the kernel as a matter of course.

There is some support for compiling the kernel with ``icc`` [icc]_ for several
of the architectures, although at the time of writing it is not completed,
requiring third-party patches.

Attributes
----------

One of the common extensions used throughout the kernel are attributes
[gcc-attribute-syntax]_. Attributes allow to introduce
implementation-defined semantics to language entities (like variables,
functions or types) without having to make significant syntactic changes
to the language (e.g. adding a new keyword) [n2049]_.

In some cases, attributes are optional (i.e. a compiler not supporting them
should still produce proper code, even if it is slower or does not perform
as many compile-time checks/diagnostics).

The kernel defines pseudo-keywords (e.g. ``__pure``) instead of using
directly the GNU attribute syntax (e.g. ``__attribute__((__pure__))``)
in order to feature detect which ones can be used and/or to shorten the code.

Please refer to ``include/linux/compiler_attributes.h`` for more information.

.. [c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [gcc] https://gcc.gnu.org
.. [clang] https://clang.llvm.org
.. [icc] https://software.intel.com/en-us/c-compilers
.. [gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf

