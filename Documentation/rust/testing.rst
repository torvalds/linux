.. SPDX-License-Identifier: GPL-2.0

Testing
=======

There are the tests that come from the examples in the Rust documentation
and get transformed into KUnit tests. These can be run via KUnit. For example
via ``kunit_tool`` (``kunit.py``) on the command line::

	./tools/testing/kunit/kunit.py run --make_options LLVM=1 --arch x86_64 --kconfig_add CONFIG_RUST=y

Alternatively, KUnit can run them as kernel built-in at boot. Refer to
Documentation/dev-tools/kunit/index.rst for the general KUnit documentation
and Documentation/dev-tools/kunit/architecture.rst for the details of kernel
built-in vs. command line testing.

Additionally, there are the ``#[test]`` tests. These can be run using
the ``rusttest`` Make target::

	make LLVM=1 rusttest

This requires the kernel ``.config`` and downloads external repositories.
It runs the ``#[test]`` tests on the host (currently) and thus is fairly
limited in what these tests can test.
