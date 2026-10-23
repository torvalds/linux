.. contents::
.. sectnum::

==========================
Linux implementation notes
==========================

This document provides more details specific to the Linux kernel implementation of the eBPF instruction set.

Byte swap instructions
======================

``BPF_FROM_LE`` and ``BPF_FROM_BE`` exist as aliases for ``BPF_TO_LE`` and ``BPF_TO_BE`` respectively.

Jump instructions
=================

``BPF_CALL | BPF_X | BPF_JMP`` (0x8d), ``callx dst``, performs an indirect
call of a BPF function whose address is held in the ``dst`` register. The
``src``, ``offset`` and ``imm`` fields are reserved and must be zero.

The address of a BPF function gets into a register in one of two ways:

* it is loaded by a 64-bit immediate instruction with ``src`` =
  ``BPF_PSEUDO_FUNC``;
* it is read, with a 64-bit load, from a frozen read-only array map, that no
  other program uses, that holds its read-only data: tables of functions,
  structures of operations, vtables, where pointers to functions may be mixed
  with other data. In the map a pointer to a function is the offset in bytes
  of its first instruction in the program, and that is how the verifier
  recognizes it. It is replaced with the address of the function when
  the program is loaded. The program reads it from there, which requires
  ``CAP_PERFMON``.

In both cases only static functions can be referenced. Therefore all functions
that can be called indirectly are known to the verifier before it starts to
analyze the program, and ``callx`` is verified as a direct call of every
function that ``dst`` may point to at that instruction. The same rules apply:
the calls can not be recursive, and the depth of the call chain and its
combined stack size are limited.

Calling helper or kernel functions through a register, indirect calls of global
functions, and tail calls in functions that are called via ``callx`` are not
supported. ``callx`` requires the BPF JIT.

Maps
====

Linux only supports the 'map_val(map)' operation on array maps with a single element.

Linux uses an fd_array to store maps associated with a BPF program. Thus,
map_by_idx(imm) uses the fd at that index in the array.

Variables
=========

The following 64-bit immediate instruction specifies that a variable address,
which corresponds to some integer stored in the 'imm' field, should be loaded:

=========================  ======  ===  =========================================  ===========  ==============
opcode construction        opcode  src  pseudocode                                 imm type     dst type
=========================  ======  ===  =========================================  ===========  ==============
BPF_IMM | BPF_DW | BPF_LD  0x18    0x3  dst = var_addr(imm)                        variable id  data pointer
=========================  ======  ===  =========================================  ===========  ==============

On Linux, this integer is a BTF ID.

Legacy BPF Packet access instructions
=====================================

As mentioned in the `ISA standard documentation
<instruction-set.html#legacy-bpf-packet-access-instructions>`_,
Linux has special eBPF instructions for access to packet data that have been
carried over from classic BPF to retain the performance of legacy socket
filters running in the eBPF interpreter.

The instructions come in two forms: ``BPF_ABS | <size> | BPF_LD`` and
``BPF_IND | <size> | BPF_LD``.

These instructions are used to access packet data and can only be used when
the program context is a pointer to a networking packet.  ``BPF_ABS``
accesses packet data at an absolute offset specified by the immediate data
and ``BPF_IND`` access packet data at an offset that includes the value of
a register in addition to the immediate data.

These instructions have seven implicit operands:

* Register R6 is an implicit input that must contain a pointer to a
  struct sk_buff.
* Register R0 is an implicit output which contains the data fetched from
  the packet.
* Registers R1-R5 are scratch registers that are clobbered by the
  instruction.

These instructions have an implicit program exit condition as well. If an
eBPF program attempts access data beyond the packet boundary, the
program execution will be aborted.

``BPF_ABS | BPF_W | BPF_LD`` (0x20) means::

  R0 = ntohl(*(u32 *) ((struct sk_buff *) R6->data + imm))

where ``ntohl()`` converts a 32-bit value from network byte order to host byte order.

``BPF_IND | BPF_W | BPF_LD`` (0x40) means::

  R0 = ntohl(*(u32 *) ((struct sk_buff *) R6->data + src + imm))
