.. _kbuild_llvm:

==============================
Building Linux with Clang/LLVM
==============================

This document covers how to build the Linux kernel with Clang and LLVM
utilities.

About
-----

The Linux kernel has always traditionally been compiled with GNU toolchains
such as GCC and binutils. Ongoing work has allowed for `Clang
<https://clang.llvm.org/>`_ and `LLVM <https://llvm.org/>`_ utilities to be
used as viable substitutes. Distributions such as `Android
<https://www.android.com/>`_, `ChromeOS
<https://www.chromium.org/chromium-os>`_, and `OpenMandriva
<https://www.openmandriva.org/>`_ use Clang built kernels.  `LLVM is a
collection of toolchain components implemented in terms of C++ objects
<https://www.aosabook.org/en/llvm.html>`_. Clang is a front-end to LLVM that
supports C and the GNU C extensions required by the kernel, and is pronounced
"klang," not "see-lang."

Clang
-----

The compiler used can be swapped out via ``CC=`` command line argument to ``make``.
``CC=`` should be set when selecting a config and during a build. ::

	make CC=clang defconfig

	make CC=clang

Cross Compiling
---------------

A single Clang compiler binary will typically contain all supported backends,
which can help simplify cross compiling. ::

	make ARCH=arm64 CC=clang CROSS_COMPILE=aarch64-linux-gnu-

``CROSS_COMPILE`` is not used to prefix the Clang compiler binary, instead
``CROSS_COMPILE`` is used to set a command line flag: ``--target=<triple>``. For
example: ::

	clang --target=aarch64-linux-gnu foo.c

LLVM Utilities
--------------

LLVM has substitutes for GNU binutils utilities. Kbuild supports ``LLVM=1``
to enable them. ::

	make LLVM=1

They can be enabled individually. The full list of the parameters: ::

	make CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm STRIP=llvm-strip \
	  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
	  HOSTCC=clang HOSTCXX=clang++ HOSTAR=llvm-ar HOSTLD=ld.lld

The integrated assembler is enabled by default. You can pass ``LLVM_IAS=0`` to
disable it.

Omitting CROSS_COMPILE
----------------------

As explained above, ``CROSS_COMPILE`` is used to set ``--target=<triple>``.

If ``CROSS_COMPILE`` is not specified, the ``--target=<triple>`` is inferred
from ``ARCH``.

That means if you use only LLVM tools, ``CROSS_COMPILE`` becomes unnecessary.

For example, to cross-compile the arm64 kernel::

	make ARCH=arm64 LLVM=1

If ``LLVM_IAS=0`` is specified, ``CROSS_COMPILE`` is also used to derive
``--prefix=<path>`` to search for the GNU assembler and linker. ::

	make ARCH=arm64 LLVM=1 LLVM_IAS=0 CROSS_COMPILE=aarch64-linux-gnu-

Supported Architectures
-----------------------

LLVM does not target all of the architectures that Linux supports and
just because a target is supported in LLVM does not mean that the kernel
will build or work without any issues. Below is a general summary of
architectures that currently work with ``CC=clang`` or ``LLVM=1``. Level
of support corresponds to "S" values in the MAINTAINERS files. If an
architecture is not present, it either means that LLVM does not target
it or there are known issues. Using the latest stable version of LLVM or
even the development tree will generally yield the best results.
An architecture's ``defconfig`` is generally expected to work well,
certain configurations may have problems that have not been uncovered
yet. Bug reports are always welcome at the issue tracker below!

.. list-table::
   :widths: 10 10 10
   :header-rows: 1

   * - Architecture
     - Level of support
     - ``make`` command
   * - arm
     - Supported
     - ``LLVM=1``
   * - arm64
     - Supported
     - ``LLVM=1``
   * - mips
     - Maintained
     - ``CC=clang``
   * - powerpc
     - Maintained
     - ``CC=clang``
   * - riscv
     - Maintained
     - ``CC=clang``
   * - s390
     - Maintained
     - ``CC=clang``
   * - x86
     - Supported
     - ``LLVM=1``

Getting Help
------------

- `Website <https://clangbuiltlinux.github.io/>`_
- `Mailing List <https://groups.google.com/forum/#!forum/clang-built-linux>`_: <clang-built-linux@googlegroups.com>
- `Issue Tracker <https://github.com/ClangBuiltLinux/linux/issues>`_
- IRC: #clangbuiltlinux on chat.freenode.net
- `Telegram <https://t.me/ClangBuiltLinux>`_: @ClangBuiltLinux
- `Wiki <https://github.com/ClangBuiltLinux/linux/wiki>`_
- `Beginner Bugs <https://github.com/ClangBuiltLinux/linux/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22>`_

.. _getting_llvm:

Getting LLVM
-------------

- https://releases.llvm.org/download.html
- https://github.com/llvm/llvm-project
- https://llvm.org/docs/GettingStarted.html
- https://llvm.org/docs/CMake.html
- https://apt.llvm.org/
- https://www.archlinux.org/packages/extra/x86_64/llvm/
- https://github.com/ClangBuiltLinux/tc-build
- https://github.com/ClangBuiltLinux/linux/wiki/Building-Clang-from-source
- https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/
