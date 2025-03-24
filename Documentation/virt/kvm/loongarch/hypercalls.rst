.. SPDX-License-Identifier: GPL-2.0

===================================
The LoongArch paravirtual interface
===================================

KVM hypercalls use the HVCL instruction with code 0x100 and the hypercall
number is put in a0. Up to five arguments may be placed in registers a1 - a5.
The return value is placed in v0 (an alias of a0).

Source code for this interface can be found in arch/loongarch/kvm*.

Querying for existence
======================

To determine if the host is running on KVM, we can utilize the cpucfg()
function at index CPUCFG_KVM_BASE (0x40000000).

The CPUCFG_KVM_BASE range, spanning from 0x40000000 to 0x400000FF, The
CPUCFG_KVM_BASE range between 0x40000000 - 0x400000FF is marked as reserved.
Consequently, all current and future processors will not implement any
feature within this range.

On a KVM-virtualized Linux system, a read operation on cpucfg() at index
CPUCFG_KVM_BASE (0x40000000) returns the magic string 'KVM\0'.

Once you have determined that your host is running on a paravirtualization-
capable KVM, you may now use hypercalls as described below.

KVM hypercall ABI
=================

The KVM hypercall ABI is simple, with one scratch register a0 (v0) and at most
five generic registers (a1 - a5) used as input parameters. The FP (Floating-
point) and vector registers are not utilized as input registers and must
remain unmodified during a hypercall.

Hypercall functions can be inlined as it only uses one scratch register.

The parameters are as follows:

	========	=================	================
	Register	IN			OUT
	========	=================	================
	a0		function number		Return	code
	a1		1st	parameter	-
	a2		2nd	parameter	-
	a3		3rd	parameter	-
	a4		4th	parameter	-
	a5		5th	parameter	-
	========	=================	================

The return codes may be one of the following:

	====		=========================
	Code		Meaning
	====		=========================
	0		Success
	-1		Hypercall not implemented
	-2		Bad Hypercall parameter
	====		=========================

KVM Hypercalls Documentation
============================

The template for each hypercall is as follows:

1. Hypercall name
2. Purpose

1. KVM_HCALL_FUNC_IPI
------------------------

:Purpose: Send IPIs to multiple vCPUs.

- a0: KVM_HCALL_FUNC_IPI
- a1: Lower part of the bitmap for destination physical CPUIDs
- a2: Higher part of the bitmap for destination physical CPUIDs
- a3: The lowest physical CPUID in the bitmap

The hypercall lets a guest send multiple IPIs (Inter-Process Interrupts) with
at most 128 destinations per hypercall. The destinations are represented in a
bitmap contained in the first two input registers (a1 and a2).

Bit 0 of a1 corresponds to the physical CPUID in the third input register (a3)
and bit 1 corresponds to the physical CPUID in a3+1, and so on.

PV IPI on LoongArch includes both PV IPI multicast sending and PV IPI receiving,
and SWI is used for PV IPI inject since there is no VM-exits accessing SWI registers.
