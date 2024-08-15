.. SPDX-License-Identifier: GPL-2.0

General Information
===================

This document contains useful information to know when working with
the Rust support in the kernel.


``no_std``
----------

The Rust support in the kernel can link only `core <https://doc.rust-lang.org/core/>`_,
but not `std <https://doc.rust-lang.org/std/>`_. Crates for use in the
kernel must opt into this behavior using the ``#![no_std]`` attribute.


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

	xdg-open Documentation/output/rust/rustdoc/kernel/index.html

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

.. code-block::

	                                                rust/bindings/
	                                               (rust/helpers/)

	                                                   include/ -----+ <-+
	                                                                 |   |
	  drivers/              rust/kernel/              +----------+ <-+   |
	    fs/                                           | bindgen  |       |
	   .../            +-------------------+          +----------+ --+   |
	                   |    Abstractions   |                         |   |
	+---------+        | +------+ +------+ |          +----------+   |   |
	| my_foo  | -----> | | foo  | | bar  | | -------> | Bindings | <-+   |
	| driver  |  Safe  | | sub- | | sub- | |  Unsafe  |          |       |
	+---------+        | |system| |system| |          | bindings | <-----+
	     |             | +------+ +------+ |          |  crate   |       |
	     |             |   kernel crate    |          +----------+       |
	     |             +-------------------+                             |
	     |                                                               |
	     +------------------# FORBIDDEN #--------------------------------+

The main idea is to encapsulate all direct interaction with the kernel's C APIs
into carefully reviewed and documented abstractions. Then users of these
abstractions cannot introduce undefined behavior (UB) as long as:

#. The abstractions are correct ("sound").
#. Any ``unsafe`` blocks respect the safety contract necessary to call the
   operations inside the block. Similarly, any ``unsafe impl``\ s respect the
   safety contract necessary to implement the trait.

Bindings
~~~~~~~~

By including a C header from ``include/`` into
``rust/bindings/bindings_helper.h``, the ``bindgen`` tool will auto-generate the
bindings for the included subsystem. After building, see the ``*_generated.rs``
output files in the ``rust/bindings/`` directory.

For parts of the C header that ``bindgen`` does not auto generate, e.g. C
``inline`` functions or non-trivial macros, it is acceptable to add a small
wrapper function to ``rust/helpers/`` to make it available for the Rust side as
well.

Abstractions
~~~~~~~~~~~~

Abstractions are the layer between the bindings and the in-kernel users. They
are located in ``rust/kernel/`` and their role is to encapsulate the unsafe
access to the bindings into an as-safe-as-possible API that they expose to their
users. Users of the abstractions include things like drivers or file systems
written in Rust.

Besides the safety aspect, the abstractions are supposed to be "ergonomic", in
the sense that they turn the C interfaces into "idiomatic" Rust code. Basic
examples are to turn the C resource acquisition and release into Rust
constructors and destructors or C integer error codes into Rust's ``Result``\ s.


Conditional compilation
-----------------------

Rust code has access to conditional compilation based on the kernel
configuration:

.. code-block:: rust

	#[cfg(CONFIG_X)]       // Enabled               (`y` or `m`)
	#[cfg(CONFIG_X="y")]   // Enabled as a built-in (`y`)
	#[cfg(CONFIG_X="m")]   // Enabled as a module   (`m`)
	#[cfg(not(CONFIG_X))]  // Disabled
