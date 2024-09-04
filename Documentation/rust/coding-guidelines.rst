.. SPDX-License-Identifier: GPL-2.0

Coding Guidelines
=================

This document describes how to write Rust code in the kernel.


Style & formatting
------------------

The code should be formatted using ``rustfmt``. In this way, a person
contributing from time to time to the kernel does not need to learn and
remember one more style guide. More importantly, reviewers and maintainers
do not need to spend time pointing out style issues anymore, and thus
less patch roundtrips may be needed to land a change.

.. note:: Conventions on comments and documentation are not checked by
  ``rustfmt``. Thus those are still needed to be taken care of.

The default settings of ``rustfmt`` are used. This means the idiomatic Rust
style is followed. For instance, 4 spaces are used for indentation rather
than tabs.

It is convenient to instruct editors/IDEs to format while typing,
when saving or at commit time. However, if for some reason reformatting
the entire kernel Rust sources is needed at some point, the following can be
run::

	make LLVM=1 rustfmt

It is also possible to check if everything is formatted (printing a diff
otherwise), for instance for a CI, with::

	make LLVM=1 rustfmtcheck

Like ``clang-format`` for the rest of the kernel, ``rustfmt`` works on
individual files, and does not require a kernel configuration. Sometimes it may
even work with broken code.


Comments
--------

"Normal" comments (i.e. ``//``, rather than code documentation which starts
with ``///`` or ``//!``) are written in Markdown the same way as documentation
comments are, even though they will not be rendered. This improves consistency,
simplifies the rules and allows to move content between the two kinds of
comments more easily. For instance:

.. code-block:: rust

	// `object` is ready to be handled now.
	f(object);

Furthermore, just like documentation, comments are capitalized at the beginning
of a sentence and ended with a period (even if it is a single sentence). This
includes ``// SAFETY:``, ``// TODO:`` and other "tagged" comments, e.g.:

.. code-block:: rust

	// FIXME: The error should be handled properly.

Comments should not be used for documentation purposes: comments are intended
for implementation details, not users. This distinction is useful even if the
reader of the source file is both an implementor and a user of an API. In fact,
sometimes it is useful to use both comments and documentation at the same time.
For instance, for a ``TODO`` list or to comment on the documentation itself.
For the latter case, comments can be inserted in the middle; that is, closer to
the line of documentation to be commented. For any other case, comments are
written after the documentation, e.g.:

.. code-block:: rust

	/// Returns a new [`Foo`].
	///
	/// # Examples
	///
	// TODO: Find a better example.
	/// ```
	/// let foo = f(42);
	/// ```
	// FIXME: Use fallible approach.
	pub fn f(x: i32) -> Foo {
	    // ...
	}

One special kind of comments are the ``// SAFETY:`` comments. These must appear
before every ``unsafe`` block, and they explain why the code inside the block is
correct/sound, i.e. why it cannot trigger undefined behavior in any case, e.g.:

.. code-block:: rust

	// SAFETY: `p` is valid by the safety requirements.
	unsafe { *p = 0; }

``// SAFETY:`` comments are not to be confused with the ``# Safety`` sections
in code documentation. ``# Safety`` sections specify the contract that callers
(for functions) or implementors (for traits) need to abide by. ``// SAFETY:``
comments show why a call (for functions) or implementation (for traits) actually
respects the preconditions stated in a ``# Safety`` section or the language
reference.


Code documentation
------------------

Rust kernel code is not documented like C kernel code (i.e. via kernel-doc).
Instead, the usual system for documenting Rust code is used: the ``rustdoc``
tool, which uses Markdown (a lightweight markup language).

To learn Markdown, there are many guides available out there. For instance,
the one at:

	https://commonmark.org/help/

This is how a well-documented Rust function may look like:

.. code-block:: rust

	/// Returns the contained [`Some`] value, consuming the `self` value,
	/// without checking that the value is not [`None`].
	///
	/// # Safety
	///
	/// Calling this method on [`None`] is *[undefined behavior]*.
	///
	/// [undefined behavior]: https://doc.rust-lang.org/reference/behavior-considered-undefined.html
	///
	/// # Examples
	///
	/// ```
	/// let x = Some("air");
	/// assert_eq!(unsafe { x.unwrap_unchecked() }, "air");
	/// ```
	pub unsafe fn unwrap_unchecked(self) -> T {
	    match self {
	        Some(val) => val,

	        // SAFETY: The safety contract must be upheld by the caller.
	        None => unsafe { hint::unreachable_unchecked() },
	    }
	}

This example showcases a few ``rustdoc`` features and some conventions followed
in the kernel:

- The first paragraph must be a single sentence briefly describing what
  the documented item does. Further explanations must go in extra paragraphs.

- Unsafe functions must document their safety preconditions under
  a ``# Safety`` section.

- While not shown here, if a function may panic, the conditions under which
  that happens must be described under a ``# Panics`` section.

  Please note that panicking should be very rare and used only with a good
  reason. In almost all cases, a fallible approach should be used, typically
  returning a ``Result``.

- If providing examples of usage would help readers, they must be written in
  a section called ``# Examples``.

- Rust items (functions, types, constants...) must be linked appropriately
  (``rustdoc`` will create a link automatically).

- Any ``unsafe`` block must be preceded by a ``// SAFETY:`` comment
  describing why the code inside is sound.

  While sometimes the reason might look trivial and therefore unneeded,
  writing these comments is not just a good way of documenting what has been
  taken into account, but most importantly, it provides a way to know that
  there are no *extra* implicit constraints.

To learn more about how to write documentation for Rust and extra features,
please take a look at the ``rustdoc`` book at:

	https://doc.rust-lang.org/rustdoc/how-to-write-documentation.html

In addition, the kernel supports creating links relative to the source tree by
prefixing the link destination with ``srctree/``. For instance:

.. code-block:: rust

	//! C header: [`include/linux/printk.h`](srctree/include/linux/printk.h)

or:

.. code-block:: rust

	/// [`struct mutex`]: srctree/include/linux/mutex.h


Naming
------

Rust kernel code follows the usual Rust naming conventions:

	https://rust-lang.github.io/api-guidelines/naming.html

When existing C concepts (e.g. macros, functions, objects...) are wrapped into
a Rust abstraction, a name as close as reasonably possible to the C side should
be used in order to avoid confusion and to improve readability when switching
back and forth between the C and Rust sides. For instance, macros such as
``pr_info`` from C are named the same in the Rust side.

Having said that, casing should be adjusted to follow the Rust naming
conventions, and namespacing introduced by modules and types should not be
repeated in the item names. For instance, when wrapping constants like:

.. code-block:: c

	#define GPIO_LINE_DIRECTION_IN	0
	#define GPIO_LINE_DIRECTION_OUT	1

The equivalent in Rust may look like (ignoring documentation):

.. code-block:: rust

	pub mod gpio {
	    pub enum LineDirection {
	        In = bindings::GPIO_LINE_DIRECTION_IN as _,
	        Out = bindings::GPIO_LINE_DIRECTION_OUT as _,
	    }
	}

That is, the equivalent of ``GPIO_LINE_DIRECTION_IN`` would be referred to as
``gpio::LineDirection::In``. In particular, it should not be named
``gpio::gpio_line_direction::GPIO_LINE_DIRECTION_IN``.


Lints
-----

In Rust, it is possible to ``allow`` particular warnings (diagnostics, lints)
locally, making the compiler ignore instances of a given warning within a given
function, module, block, etc.

It is similar to ``#pragma GCC diagnostic push`` + ``ignored`` + ``pop`` in C
[#]_:

.. code-block:: c

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-function"
	static void f(void) {}
	#pragma GCC diagnostic pop

.. [#] In this particular case, the kernel's ``__{always,maybe}_unused``
       attributes (C23's ``[[maybe_unused]]``) may be used; however, the example
       is meant to reflect the equivalent lint in Rust discussed afterwards.

But way less verbose:

.. code-block:: rust

	#[allow(dead_code)]
	fn f() {}

By that virtue, it makes it possible to comfortably enable more diagnostics by
default (i.e. outside ``W=`` levels). In particular, those that may have some
false positives but that are otherwise quite useful to keep enabled to catch
potential mistakes.

For more information about diagnostics in Rust, please see:

	https://doc.rust-lang.org/stable/reference/attributes/diagnostics.html
