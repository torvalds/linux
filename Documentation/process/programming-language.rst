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

Rust
----

The kernel has experimental support for the Rust programming language
[rust-language]_ under ``CONFIG_RUST``. It is compiled with ``rustc`` [rustc]_
under ``--edition=2021`` [rust-editions]_. Editions are a way to introduce
small changes to the language that are not backwards compatible.

On top of that, some unstable features [rust-unstable-features]_ are used in
the kernel. Unstable features may change in the future, thus it is an important
goal to reach a point where only stable features are used.

Please refer to Documentation/rust/index.rst for more information.

.. [c-language] http://www.open-std.org/jtc1/sc22/wg14/www/standards
.. [gcc] https://gcc.gnu.org
.. [clang] https://clang.llvm.org
.. [gcc-c-dialect-options] https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html
.. [gnu-extensions] https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html
.. [gcc-attribute-syntax] https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html
.. [n2049] http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2049.pdf
.. [rust-language] https://www.rust-lang.org
.. [rustc] https://doc.rust-lang.org/rustc/
.. [rust-editions] https://doc.rust-lang.org/edition-guide/editions/
.. [rust-unstable-features] https://github.com/Rust-for-Linux/linux/issues/2
