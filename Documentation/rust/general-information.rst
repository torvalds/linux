.. SPDX-License-Identifier: GPL-2.0

General Information
===================

This document contains useful information to know when working with
the Rust support in the kernel.


Code documentation
------------------

Rust kernel code is documented using ``rustdoc``, its built-in documentation
generator.

The generated HTML docs include integrated search, linked items (e.g. types,
functions, constants), source code, etc. They may be read at (TODO: link when
in mainline and generated alongside the rest of the documentation):

	http://kernel.org/

The docs can also be easily generated and read locally. This is quite fast
(same order as compiling the code itself) and no special tools or environment
are needed. This has the added advantage that they will be tailored to
the particular kernel configuration used. To generate them, use the ``rustdoc``
target with the same invocation used for compilation, e.g.::

	make LLVM=1 rustdoc

To read the docs locally in your web browser, run e.g.::

	xdg-open rust/doc/kernel/index.html

To learn about how to write the documentation, please see coding-guidelines.rst.


Extra lints
-----------

While ``rustc`` is a very helpful compiler, some extra lints and analyses are
available via ``clippy``, a Rust linter. To enable it, pass ``CLIPPY=1`` to
the same invocation used for compilation, e.g.::

	make LLVM=1 CLIPPY=1

Please note that Clippy may change code generation, thus it should not be
enabled while building a production kernel.


Abstractions vs. bindings
-------------------------

Abstractions are Rust code wrapping kernel functionality from the C side.

In order to use functions and types from the C side, bindings are created.
Bindings are the declarations for Rust of those functions and types from
the C side.

For instance, one may write a ``Mutex`` abstraction in Rust which wraps
a ``struct mutex`` from the C side and calls its functions through the bindings.

Abstractions are not available for all the kernel internal APIs and concepts,
but it is intended that coverage is expanded as time goes on. "Leaf" modules
(e.g. drivers) should not use the C bindings directly. Instead, subsystems
should provide as-safe-as-possible abstractions as needed.


Conditional compilation
-----------------------

Rust code has access to conditional compilation based on the kernel
configuration:

.. code-block:: rust

	#[cfg(CONFIG_X)]       // Enabled               (`y` or `m`)
	#[cfg(CONFIG_X="y")]   // Enabled as a built-in (`y`)
	#[cfg(CONFIG_X="m")]   // Enabled as a module   (`m`)
	#[cfg(not(CONFIG_X))]  // Disabled
