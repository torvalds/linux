.. SPDX-License-Identifier: GPL-2.0-only

==========================
Bash completion for Kbuild
==========================

The kernel build system is written using Makefiles, and Bash completion
for the `make` command is available through the `bash-completion`_ project.

However, the Makefiles for the kernel build are complex. The generic completion
rules for the `make` command do not provide meaningful suggestions for the
kernel build system, except for the options of the `make` command itself.

To enhance completion for various variables and targets, the kernel source
includes its own completion script at `scripts/bash-completion/make`.

This script provides additional completions when working within the kernel tree.
Outside the kernel tree, it defaults to the generic completion rules for the
`make` command.

Prerequisites
=============

The script relies on helper functions provided by `bash-completion`_ project.
Please ensure it is installed on your system. On most distributions, you can
install the `bash-completion` package through the standard package manager.

How to use
==========

You can source the script directly::

  $ source scripts/bash-completion/make

Or, you can copy it into the search path for Bash completion scripts.
For example::

  $ mkdir -p ~/.local/share/bash-completion/completions
  $ cp scripts/bash-completion/make ~/.local/share/bash-completion/completions/

Details
=======

The additional completion for Kbuild is enabled in the following cases:

 - You are in the root directory of the kernel source.
 - You are in the top-level build directory created by the O= option
   (checked via the `source` symlink pointing to the kernel source).
 - The -C make option specifies the kernel source or build directory.
 - The -f make option specifies a file in the kernel source or build directory.

If none of the above are met, it falls back to the generic completion rules.

The completion supports:

  - Commonly used targets, such as `all`, `menuconfig`, `dtbs`, etc.
  - Make (or environment) variables, such as `ARCH`, `LLVM`, etc.
  - Single-target builds (`foo/bar/baz.o`)
  - Configuration files (`*_defconfig` and `*.config`)

Some variables offer intelligent behavior. For instance, `CROSS_COMPILE=`
followed by a TAB displays installed toolchains. The list of defconfig files
shown depends on the value of the `ARCH=` variable.

.. _bash-completion: https://github.com/scop/bash-completion/
