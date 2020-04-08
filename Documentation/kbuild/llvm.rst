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

The compiler used can be swapped out via `CC=` command line argument to `make`.
`CC=` should be set when selecting a config and during a build.

	make CC=clang defconfig

	make CC=clang

Cross Compiling
---------------

A single Clang compiler binary will typically contain all supported backends,
which can help simplify cross compiling.

	ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make CC=clang

`CROSS_COMPILE` is not used to prefix the Clang compiler binary, instead
`CROSS_COMPILE` is used to set a command line flag: `--target <triple>`. For
example:

	clang --target aarch64-linux-gnu foo.c

LLVM Utilities
--------------

LLVM has substitutes for GNU binutils utilities. Kbuild supports `LLVM=1`
to enable them.

	make LLVM=1

They can be enabled individually. The full list of the parameters:

	make CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm STRIP=llvm-strip \\
	  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump OBJSIZE=llvm-size \\
	  READELF=llvm-readelf HOSTCC=clang HOSTCXX=clang++ HOSTAR=llvm-ar \\
	  HOSTLD=ld.lld

Currently, the integrated assembler is disabled by default. You can pass
`LLVM_IAS=1` to enable it.

Getting Help
------------

- `Website <https://clangbuiltlinux.github.io/>`_
- `Mailing List <https://groups.google.com/forum/#!forum/clang-built-linux>`_: <clang-built-linux@googlegroups.com>
- `Issue Tracker <https://github.com/ClangBuiltLinux/linux/issues>`_
- IRC: #clangbuiltlinux on chat.freenode.net
- `Telegram <https://t.me/ClangBuiltLinux>`_: @ClangBuiltLinux
- `Wiki <https://github.com/ClangBuiltLinux/linux/wiki>`_
- `Beginner Bugs <https://github.com/ClangBuiltLinux/linux/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22>`_

Getting LLVM
-------------

- http://releases.llvm.org/download.html
- https://github.com/llvm/llvm-project
- https://llvm.org/docs/GettingStarted.html
- https://llvm.org/docs/CMake.html
- https://apt.llvm.org/
- https://www.archlinux.org/packages/extra/x86_64/llvm/
- https://github.com/ClangBuiltLinux/tc-build
- https://github.com/ClangBuiltLinux/linux/wiki/Building-Clang-from-source
- https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/
