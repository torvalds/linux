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
<https://www.chromium.org/chromium-os>`_, `OpenMandriva
<https://www.openmandriva.org/>`_, and `Chimera Linux
<https://chimera-linux.org/>`_ use Clang built kernels. Google's and Meta's
datacenter fleets also run kernels built with Clang.

`LLVM is a collection of toolchain components implemented in terms of C++
objects <https://www.aosabook.org/en/llvm.html>`_. Clang is a front-end to LLVM
that supports C and the GNU C extensions required by the kernel, and is
pronounced "klang," not "see-lang."

Building with LLVM
------------------

Invoke ``make`` via::

	make LLVM=1

to compile for the host target. For cross compiling::

	make LLVM=1 ARCH=arm64

The LLVM= argument
------------------

LLVM has substitutes for GNU binutils utilities. They can be enabled
individually. The full list of supported make variables::

	make CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm STRIP=llvm-strip \
	  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
	  HOSTCC=clang HOSTCXX=clang++ HOSTAR=llvm-ar HOSTLD=ld.lld

``LLVM=1`` expands to the above.

If your LLVM tools are not available in your PATH, you can supply their
location using the LLVM variable with a trailing slash::

	make LLVM=/path/to/llvm/

which will use ``/path/to/llvm/clang``, ``/path/to/llvm/ld.lld``, etc. The
following may also be used::

	PATH=/path/to/llvm:$PATH make LLVM=1

If your LLVM tools have a version suffix and you want to test with that
explicit version rather than the unsuffixed executables like ``LLVM=1``, you
can pass the suffix using the ``LLVM`` variable::

	make LLVM=-14

which will use ``clang-14``, ``ld.lld-14``, etc.

To support combinations of out of tree paths with version suffixes, we
recommend::

	PATH=/path/to/llvm/:$PATH make LLVM=-14

``LLVM=0`` is not the same as omitting ``LLVM`` altogether, it will behave like
``LLVM=1``. If you only wish to use certain LLVM utilities, use their
respective make variables.

The same value used for ``LLVM=`` should be set for each invocation of ``make``
if configuring and building via distinct commands. ``LLVM=`` should also be set
as an environment variable when running scripts that will eventually run
``make``.

Cross Compiling
---------------

A single Clang compiler binary (and corresponding LLVM utilities) will
typically contain all supported back ends, which can help simplify cross
compiling especially when ``LLVM=1`` is used. If you use only LLVM tools,
``CROSS_COMPILE`` or target-triple-prefixes become unnecessary. Example::

	make LLVM=1 ARCH=arm64

As an example of mixing LLVM and GNU utilities, for a target like ``ARCH=s390``
which does not yet have ``ld.lld`` or ``llvm-objcopy`` support, you could
invoke ``make`` via::

	make LLVM=1 ARCH=s390 LD=s390x-linux-gnu-ld.bfd \
	  OBJCOPY=s390x-linux-gnu-objcopy

This example will invoke ``s390x-linux-gnu-ld.bfd`` as the linker and
``s390x-linux-gnu-objcopy``, so ensure those are reachable in your ``$PATH``.

``CROSS_COMPILE`` is not used to prefix the Clang compiler binary (or
corresponding LLVM utilities) as is the case for GNU utilities when ``LLVM=1``
is not set.

The LLVM_IAS= argument
----------------------

Clang can assemble assembler code. You can pass ``LLVM_IAS=0`` to disable this
behavior and have Clang invoke the corresponding non-integrated assembler
instead. Example::

	make LLVM=1 LLVM_IAS=0

``CROSS_COMPILE`` is necessary when cross compiling and ``LLVM_IAS=0``
is used in order to set ``--prefix=`` for the compiler to find the
corresponding non-integrated assembler (typically, you don't want to use the
system assembler when targeting another architecture). Example::

	make LLVM=1 ARCH=arm LLVM_IAS=0 CROSS_COMPILE=arm-linux-gnueabi-


Ccache
------

``ccache`` can be used with ``clang`` to improve subsequent builds, (though
KBUILD_BUILD_TIMESTAMP_ should be set to a deterministic value between builds
in order to avoid 100% cache misses, see Reproducible_builds_ for more info):

	KBUILD_BUILD_TIMESTAMP='' make LLVM=1 CC="ccache clang"

.. _KBUILD_BUILD_TIMESTAMP: kbuild.html#kbuild-build-timestamp
.. _Reproducible_builds: reproducible-builds.html#timestamps

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
   * - hexagon
     - Maintained
     - ``LLVM=1``
   * - loongarch
     - Maintained
     - ``LLVM=1``
   * - mips
     - Maintained
     - ``LLVM=1``
   * - powerpc
     - Maintained
     - ``LLVM=1``
   * - riscv
     - Supported
     - ``LLVM=1``
   * - s390
     - Maintained
     - ``LLVM=1`` (LLVM >= 18.1.0), ``CC=clang`` (LLVM < 18.1.0)
   * - um (User Mode)
     - Maintained
     - ``LLVM=1``
   * - x86
     - Supported
     - ``LLVM=1``

Getting Help
------------

- `Website <https://clangbuiltlinux.github.io/>`_
- `Mailing List <https://lore.kernel.org/llvm/>`_: <llvm@lists.linux.dev>
- `Old Mailing List Archives <https://groups.google.com/g/clang-built-linux>`_
- `Issue Tracker <https://github.com/ClangBuiltLinux/linux/issues>`_
- IRC: #clangbuiltlinux on irc.libera.chat
- `Telegram <https://t.me/ClangBuiltLinux>`_: @ClangBuiltLinux
- `Wiki <https://github.com/ClangBuiltLinux/linux/wiki>`_
- `Beginner Bugs <https://github.com/ClangBuiltLinux/linux/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22>`_

.. _getting_llvm:

Getting LLVM
-------------

We provide prebuilt stable versions of LLVM on `kernel.org
<https://kernel.org/pub/tools/llvm/>`_. These have been optimized with profile
data for building Linux kernels, which should improve kernel build times
relative to other distributions of LLVM.

Below are links that may be useful for building LLVM from source or procuring
it through a distribution's package manager.

- https://releases.llvm.org/download.html
- https://github.com/llvm/llvm-project
- https://llvm.org/docs/GettingStarted.html
- https://llvm.org/docs/CMake.html
- https://apt.llvm.org/
- https://www.archlinux.org/packages/extra/x86_64/llvm/
- https://github.com/ClangBuiltLinux/tc-build
- https://github.com/ClangBuiltLinux/linux/wiki/Building-Clang-from-source
- https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/
