.. contents::
.. sectnum::

==========================
Clang implementation notes
==========================

This document provides more details specific to the Clang/LLVM implementation of the eBPF instruction set.

Versions
========

Clang defined "CPU" versions, where a CPU version of 3 corresponds to the current eBPF ISA.

Clang can select the eBPF ISA version using ``-mcpu=v3`` for example to select version 3.

Atomic operations
=================

Clang can generate atomic instructions by default when ``-mcpu=v3`` is
enabled. If a lower version for ``-mcpu`` is set, the only atomic instruction
Clang can generate is ``BPF_ADD`` *without* ``BPF_FETCH``. If you need to enable
the atomics features, while keeping a lower ``-mcpu`` version, you can use
``-Xclang -target-feature -Xclang +alu32``.
