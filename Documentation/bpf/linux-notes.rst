.. contents::
.. sectnum::

==========================
Linux implementation notes
==========================

This document provides more details specific to the Linux kernel implementation of the eBPF instruction set.

Byte swap instructions
======================

``BPF_FROM_LE`` and ``BPF_FROM_BE`` exist as aliases for ``BPF_TO_LE`` and ``BPF_TO_BE`` respectively.

Legacy BPF Packet access instructions
=====================================

As mentioned in the `ISA standard documentation <instruction-set.rst#legacy-bpf-packet-access-instructions>`_,
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

Appendix
========

For reference, the following table lists legacy Linux-specific opcodes in order by value.

======  ====  ===================================================  =============
opcode  imm   description                                          reference
======  ====  ===================================================  =============
0x20    any   dst = ntohl(\*(uint32_t \*)(R6->data + imm))         `Legacy BPF Packet access instructions`_
0x28    any   dst = ntohs(\*(uint16_t \*)(R6->data + imm))         `Legacy BPF Packet access instructions`_
0x30    any   dst = (\*(uint8_t \*)(R6->data + imm))               `Legacy BPF Packet access instructions`_
0x38    any   dst = ntohll(\*(uint64_t \*)(R6->data + imm))        `Legacy BPF Packet access instructions`_
0x40    any   dst = ntohl(\*(uint32_t \*)(R6->data + src + imm))   `Legacy BPF Packet access instructions`_
0x48    any   dst = ntohs(\*(uint16_t \*)(R6->data + src + imm))   `Legacy BPF Packet access instructions`_
0x50    any   dst = \*(uint8_t \*)(R6->data + src + imm))          `Legacy BPF Packet access instructions`_
0x58    any   dst = ntohll(\*(uint64_t \*)(R6->data + src + imm))  `Legacy BPF Packet access instructions`_
