.. SPDX-License-Identifier: GPL-2.0

Testing
=======

This document contains useful information how to test the Rust code in the
kernel.

There are three sorts of tests:

- The KUnit tests.
- The ``#[test]`` tests.
- The Kselftests.

The KUnit tests
---------------

These are the tests that come from the examples in the Rust documentation. They
get transformed into KUnit tests.

Usage
*****

These tests can be run via KUnit. For example via ``kunit_tool`` (``kunit.py``)
on the command line::

	./tools/testing/kunit/kunit.py run --make_options LLVM=1 --arch x86_64 --kconfig_add CONFIG_RUST=y

Alternatively, KUnit can run them as kernel built-in at boot. Refer to
Documentation/dev-tools/kunit/index.rst for the general KUnit documentation
and Documentation/dev-tools/kunit/architecture.rst for the details of kernel
built-in vs. command line testing.

To use these KUnit doctests, the following must be enabled::

	CONFIG_KUNIT
	   Kernel hacking -> Kernel Testing and Coverage -> KUnit - Enable support for unit tests
	CONFIG_RUST_KERNEL_DOCTESTS
	   Kernel hacking -> Rust hacking -> Doctests for the `kernel` crate

in the kernel config system.

KUnit tests are documentation tests
***********************************

These documentation tests are typically examples of usage of any item (e.g.
function, struct, module...).

They are very convenient because they are just written alongside the
documentation. For instance:

.. code-block:: rust

	/// Sums two numbers.
	///
	/// ```
	/// assert_eq!(mymod::f(10, 20), 30);
	/// ```
	pub fn f(a: i32, b: i32) -> i32 {
	    a + b
	}

In userspace, the tests are collected and run via ``rustdoc``. Using the tool
as-is would be useful already, since it allows verifying that examples compile
(thus enforcing they are kept in sync with the code they document) and as well
as running those that do not depend on in-kernel APIs.

For the kernel, however, these tests get transformed into KUnit test suites.
This means that doctests get compiled as Rust kernel objects, allowing them to
run against a built kernel.

A benefit of this KUnit integration is that Rust doctests get to reuse existing
testing facilities. For instance, the kernel log would look like::

	KTAP version 1
	1..1
	    KTAP version 1
	    # Subtest: rust_doctests_kernel
	    1..59
	    # rust_doctest_kernel_build_assert_rs_0.location: rust/kernel/build_assert.rs:13
	    ok 1 rust_doctest_kernel_build_assert_rs_0
	    # rust_doctest_kernel_build_assert_rs_1.location: rust/kernel/build_assert.rs:56
	    ok 2 rust_doctest_kernel_build_assert_rs_1
	    # rust_doctest_kernel_init_rs_0.location: rust/kernel/init.rs:122
	    ok 3 rust_doctest_kernel_init_rs_0
	    ...
	    # rust_doctest_kernel_types_rs_2.location: rust/kernel/types.rs:150
	    ok 59 rust_doctest_kernel_types_rs_2
	# rust_doctests_kernel: pass:59 fail:0 skip:0 total:59
	# Totals: pass:59 fail:0 skip:0 total:59
	ok 1 rust_doctests_kernel

Tests using the `? <https://doc.rust-lang.org/reference/expressions/operator-expr.html#the-question-mark-operator>`_
operator are also supported as usual, e.g.:

.. code-block:: rust

	/// ```
	/// # use kernel::{spawn_work_item, workqueue};
	/// spawn_work_item!(workqueue::system(), || pr_info!("x\n"))?;
	/// # Ok::<(), Error>(())
	/// ```

The tests are also compiled with Clippy under ``CLIPPY=1``, just like normal
code, thus also benefitting from extra linting.

In order for developers to easily see which line of doctest code caused a
failure, a KTAP diagnostic line is printed to the log. This contains the
location (file and line) of the original test (i.e. instead of the location in
the generated Rust file)::

	# rust_doctest_kernel_types_rs_2.location: rust/kernel/types.rs:150

Rust tests appear to assert using the usual ``assert!`` and ``assert_eq!``
macros from the Rust standard library (``core``). We provide a custom version
that forwards the call to KUnit instead. Importantly, these macros do not
require passing context, unlike those for KUnit testing (i.e.
``struct kunit *``). This makes them easier to use, and readers of the
documentation do not need to care about which testing framework is used. In
addition, it may allow us to test third-party code more easily in the future.

A current limitation is that KUnit does not support assertions in other tasks.
Thus, we presently simply print an error to the kernel log if an assertion
actually failed. Additionally, doctests are not run for nonpublic functions.

The ``#[test]`` tests
---------------------

Additionally, there are the ``#[test]`` tests. These can be run using the
``rusttest`` Make target::

	make LLVM=1 rusttest

This requires the kernel ``.config``. It runs the ``#[test]`` tests on the host
(currently) and thus is fairly limited in what these tests can test.

The Kselftests
--------------

Kselftests are also available in the ``tools/testing/selftests/rust`` folder.

The kernel config options required for the tests are listed in the
``tools/testing/selftests/rust/config`` file and can be included with the aid
of the ``merge_config.sh`` script::

	./scripts/kconfig/merge_config.sh .config tools/testing/selftests/rust/config

The kselftests are built within the kernel source tree and are intended to
be executed on a system that is running the same kernel.

Once a kernel matching the source tree has been installed and booted, the
tests can be compiled and executed using the following command::

	make TARGETS="rust" kselftest

Refer to Documentation/dev-tools/kselftest.rst for the general Kselftest
documentation.
