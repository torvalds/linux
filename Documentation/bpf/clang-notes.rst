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

Arithmetic instructions
=======================

For CPU versions prior to 3, Clang v7.0 and later can enable ``BPF_ALU`` support with
``-Xclang -target-feature -Xclang +alu32``.  In CPU version 3, support is automatically included.

Jump instructions
=================

Clang generates the ``BPF_CALL | BPF_X | BPF_JMP`` (0x8d) instruction for calls
through a function pointer. The Linux kernel verifier accepts it only when it
can prove that the register holds the address of a static BPF function, see
Documentation/bpf/linux-notes.rst. If ``-O0`` is used, Clang will generate this
instruction for helper calls as well, which is not supported.

Atomic operations
=================

Clang can generate atomic instructions by default when ``-mcpu=v3`` is
enabled. If a lower version for ``-mcpu`` is set, the only atomic instruction
Clang can generate is ``BPF_ADD`` *without* ``BPF_FETCH``. If you need to enable
the atomics features, while keeping a lower ``-mcpu`` version, you can use
``-Xclang -target-feature -Xclang +alu32``.
