.. SPDX-License-Identifier: GPL-2.0

=========================
PRAMIN aperture mechanism
=========================

.. note::
   The following description is approximate and current as of the Ampere
   family. It may change for future generations and is intended to assist in
   understanding the driver code.

Introduction
============

PRAMIN is a hardware aperture mechanism that provides CPU access to GPU Video
RAM (VRAM) before the GPU's Memory Management Unit (MMU) and page tables are
initialized. This 1 MiB sliding window, located at a fixed offset within BAR0,
is essential for setting up page tables and other critical GPU data structures
without relying on the GPU's MMU.

Architecture Overview
=====================

The PRAMIN aperture mechanism is logically implemented by the GPU's PBUS (PCIe
Bus Controller Unit) and provides a CPU-accessible window into VRAM through the
PCIe interface::

    +-----------------+    PCIe     +------------------------------+
    |      CPU        |<----------->|           GPU                |
    +-----------------+             |                              |
                                    |  +----------------------+    |
                                    |  |       PBUS           |    |
                                    |  |  (Bus Controller)    |    |
                                    |  |                      |    |
                                    |  |  +--------------+ <------------ [1]
                                    |  |  |   PRAMIN     |    |    |
                                    |  |  |   Window     |    |    |
                                    |  |  |   (1 MiB)    |    |    |
                                    |  |  +--------------+    |    |
                                    |  |         |            |    |
                                    |  +---------|------------+    |
                                    |            |                 |
                                    |            v                 |
                                    |  +----------------------+ <------- [2]
                                    |  |       VRAM           |    |
                                    |  |    (Several GiB)     |    |
                                    |  |                      |    |
                                    |  |   FB[0x0000000000]   |    |
                                    |  |          ...         |    |
                                    |  |   FB[0xFFFFFFFFFF]   |    |
                                    |  +----------------------+    |
                                    +------------------------------+

    [1] Window starts at BAR0 + 0x700000.
    [2] Program PRAMIN to any 64 KiB-aligned VRAM boundary.

PBUS is responsible for, among other things, handling MMIO
accesses to the BAR registers.

PRAMIN Window Operation
=======================

The PRAMIN window provides a 1 MiB sliding aperture that can be repositioned
over the entire VRAM address space using the ``NV_PBUS_BAR0_WINDOW`` register.

Window Control Mechanism
-------------------------

::

    NV_PBUS_BAR0_WINDOW Register (0x1700):
    +-------+--------+--------------------------------------+
    | 31:26 | 25:24  |               23:0                   |
    | RSVD  | TARGET |            BASE_ADDR                 |
    |       |        |        (bits 39:16 of VRAM address)  |
    +-------+--------+--------------------------------------+

    The 24-bit BASE_ADDR field encodes bits [39:16] of the target VRAM address,
    providing 40-bit (1 TiB) address space coverage with 64 KiB alignment.

    TARGET field (bits 25:24):
    - 0x0: VRAM (Video Memory)
    - 0x1: Reserved (unused)
    - 0x2: SYS_MEM_COH (Coherent System Memory)
    - 0x3: SYS_MEM_NONCOH (Non-coherent System Memory)

.. note::
   Nova only uses TARGET=VRAM (0x0) for video memory access. The SYS_MEM
   target values are documented here for hardware completeness but are
   not used by the driver.

64 KiB Alignment Requirement
----------------------------

The PRAMIN window must be aligned to 64 KiB boundaries in VRAM. This is enforced
by the ``BASE_ADDR`` field representing bits [39:16] of the target address::

    VRAM Address Calculation:
    actual_vram_addr = (BASE_ADDR << 16) + pramin_offset
    Where:
    - BASE_ADDR: 24-bit value from NV_PBUS_BAR0_WINDOW[23:0]
    - pramin_offset: 20-bit offset within the PRAMIN window [0x00000-0xFFFFF]

    Example Window Positioning:
    +---------------------------------------------------------+
    |                    VRAM Space                           |
    |                                                         |
    |  0x0000000000 +-----------------+ <-- 64 KiB aligned    |
    |               | PRAMIN Window   |                       |
    |               |    (1 MiB)      |                       |
    |  0x00000FFFFF +-----------------+                       |
    |                                                         |
    |       |              ^                                  |
    |       |              | Window can slide                 |
    |       v              | to any 64 KiB-aligned boundary   |
    |                                                         |
    |  0x0123400000 +-----------------+ <-- 64 KiB aligned    |
    |               | PRAMIN Window   |                       |
    |               |    (1 MiB)      |                       |
    |  0x01234FFFFF +-----------------+                       |
    |                                                         |
    |                       ...                               |
    |                                                         |
    |  0xFFFFF00000 +-----------------+ <-- 64 KiB aligned    |
    |               | PRAMIN Window   |                       |
    |               |    (1 MiB)      |                       |
    |  0xFFFFFFFFFF +-----------------+                       |
    +---------------------------------------------------------+
