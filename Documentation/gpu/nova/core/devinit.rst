.. SPDX-License-Identifier: GPL-2.0

==================================
Device Initialization (devinit)
==================================
The devinit process is complex and subject to change. This document provides a high-level
overview using the Ampere GPU family as an example. The goal is to provide a conceptual
overview of the process to aid in understanding the corresponding kernel code.

Device initialization (devinit) is a crucial sequence of register read/write operations
that occur after a GPU reset. The devinit sequence is essential for properly configuring
the GPU hardware before it can be used.

The devinit engine is an interpreter program that typically runs on the PMU (Power Management
Unit) microcontroller of the GPU. This interpreter executes a "script" of initialization
commands. The devinit engine itself is part of the VBIOS ROM in the same ROM image as the
FWSEC (Firmware Security) image (see fwsec.rst and vbios.rst) and it runs before the
nova-core driver is even loaded. On an Ampere GPU, the devinit ucode is separate from the
FWSEC ucode. It is launched by FWSEC, which runs on the GSP in 'heavy-secure' mode, while
devinit runs on the PMU in 'light-secure' mode.

Key Functions of devinit
------------------------
devinit performs several critical tasks:

1. Programming VRAM memory controller timings
2. Power sequencing
3. Clock and PLL (Phase-Locked Loop) configuration
4. Thermal management

Low-level Firmware Initialization Flow
--------------------------------------
Upon reset, several microcontrollers on the GPU (such as PMU, SEC2, GSP, etc.) run GPU
firmware (gfw) code to set up the GPU and its core parameters. Most of the GPU is
considered unusable until this initialization process completes.

These low-level GPU firmware components are typically:

1. Located in the VBIOS ROM in the same ROM partition (see vbios.rst and fwsec.rst).
2. Executed in sequence on different microcontrollers:

  - The devinit engine typically but not necessarily runs on the PMU.
  - On an Ampere GPU, the FWSEC typically runs on the GSP (GPU System Processor) in
    heavy-secure mode.

Before the driver can proceed with further initialization, it must wait for a signal
indicating that core initialization is complete (known as GFW_BOOT). This signal is
asserted by the FWSEC running on the GSP in heavy-secure mode.

Runtime Considerations
----------------------
It's important to note that the devinit sequence also needs to run during suspend/resume
operations at runtime, not just during initial boot, as it is critical to power management.

Security and Access Control
---------------------------
The initialization process involves careful privilege management. For example, before
accessing certain completion status registers, the driver must check privilege level
masks. Some registers are only accessible after secure firmware (FWSEC) lowers the
privilege level to allow CPU (LS/low-secure) access. This is the case, for example,
when receiving the GFW_BOOT signal.