.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

==========
VBIOS
==========
This document describes the layout of the VBIOS image which is a series of concatenated
images in the ROM of the GPU. The VBIOS is mirrored onto the BAR 0 space and is read
by both Boot ROM firmware (also known as IFR or init-from-rom firmware) on the GPU to
bootstrap various microcontrollers (PMU, SEC, GSP) with critical initialization before
the driver loads, as well as by the nova-core driver in the kernel to boot the GSP.

The format of the images in the ROM follow the "BIOS Specification" part of the
PCI specification, with Nvidia-specific extensions. The ROM images of type FwSec
are the ones that contain Falcon ucode and what we are mainly looking for.

As an example, the following are the different image types that can be found in the
VBIOS of an Ampere GA102 GPU which is supported by the nova-core driver.

- PciAt Image (Type 0x00) - This is the standard PCI BIOS image, whose name
  likely comes from the "IBM PC/AT" architecture.

- EFI Image (Type 0x03) - This is the EFI BIOS image. It contains the UEFI GOP
  driver that is used to display UEFI graphics output.

- First FwSec Image (Type 0xE0) - The first FwSec image (Secure Firmware)

- Second FwSec Image (Type 0xE0) - The second FwSec image (Secure Firmware)
  contains various  microcodes (also known as an applications) that do a range
  of different functions. The FWSEC ucode is run in heavy-secure mode and
  typically runs directly on the GSP (it could be running on a different
  designated processor in future generations but as of Ampere, it is the GSP).
  This firmware then loads other firmware ucodes onto the PMU and SEC2
  microcontrollers for gfw initialization after GPU reset and before the driver
  loads (see devinit.rst). The DEVINIT ucode is itself another ucode that is
  stored in this ROM partition.

Once located, the Falcon ucodes have "Application Interfaces" in their data
memory (DMEM). For FWSEC, the application interface we use for FWSEC is the
"DMEM mapper" interface which is configured to run the "FRTS" command. This
command carves out the WPR2 (Write-Protected Region) in VRAM. It then places
important power-management data, called 'FRTS', into this region. The WPR2
region is only accessible to heavy-secure ucode.

.. note::
   It is not clear why FwSec has 2 different partitions in the ROM, but they both
   are of type 0xE0 and can be identified as such. This could be subject to change
   in future generations.

VBIOS ROM Layout
----------------
The VBIOS layout is roughly a series of concatenated images laid out as follows::

    +----------------------------------------------------------------------------+
    | VBIOS (Starting at ROM_OFFSET: 0x300000)                                   |
    +----------------------------------------------------------------------------+
    | +-----------------------------------------------+                          |
    | | PciAt Image (Type 0x00)                       |                          |
    | +-----------------------------------------------+                          |
    | | +-------------------+                         |                          |
    | | | ROM Header        |                         |                          |
    | | | (Signature 0xAA55)|                         |                          |
    | | +-------------------+                         |                          |
    | |         | rom header's pci_data_struct_offset |                          |
    | |         | points to the PCIR structure        |                          |
    | |         V                                     |                          |
    | | +-------------------+                         |                          |
    | | | PCIR Structure    |                         |                          |
    | | | (Signature "PCIR")|                         |                          |
    | | | last_image: 0x80  |                         |                          |
    | | | image_len: size   |                         |                          |
    | | | in 512-byte units |                         |                          |
    | | +-------------------+                         |                          |
    | |         |                                     |                          |
    | |         | NPDE immediately follows PCIR       |                          |
    | |         V                                     |                          |
    | | +-------------------+                         |                          |
    | | | NPDE Structure    |                         |                          |
    | | | (Signature "NPDE")|                         |                          |
    | | | last_image: 0x00  |                         |                          |
    | | +-------------------+                         |                          |
    | |                                               |                          |
    | | +-------------------+                         |                          |
    | | | BIT Header        | (Signature scanning     |                          |
    | | | (Signature "BIT") |  provides the location  |                          |
    | | +-------------------+  of the BIT table)      |                          |
    | |         | header is                           |                          |
    | |         | followed by a table of tokens       |                          |
    | |         V one of which is for falcon data.    |                          |
    | | +-------------------+                         |                          |
    | | | BIT Tokens        |                         |                          |
    | | |  ______________   |                         |                          |
    | | | | Falcon Data |   |                         |                          |
    | | | | Token (0x70)|---+------------>------------+--+                       |
    | | | +-------------+   |  falcon_data_ptr()      |  |                       |
    | | +-------------------+                         |  V                       |
    | +-----------------------------------------------+  |                       |
    |              (no gap between images)               |                       |
    | +-----------------------------------------------+  |                       |
    | | EFI Image (Type 0x03)                         |  |                       |
    | +-----------------------------------------------+  |                       |
    | | Contains the UEFI GOP driver (Graphics Output)|  |                       |
    | | +-------------------+                         |  |                       |
    | | | ROM Header        |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | PCIR Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | NPDE Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | Image data        |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | +-----------------------------------------------+  |                       |
    |              (no gap between images)               |                       |
    | +-----------------------------------------------+  |                       |
    | | First FwSec Image (Type 0xE0)                 |  |                       |
    | +-----------------------------------------------+  |                       |
    | | +-------------------+                         |  |                       |
    | | | ROM Header        |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | PCIR Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | NPDE Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | Image data        |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | +-----------------------------------------------+  |                       |
    |              (no gap between images)               |                       |
    | +-----------------------------------------------+  |                       |
    | | Second FwSec Image (Type 0xE0)                |  |                       |
    | +-----------------------------------------------+  |                       |
    | | +-------------------+                         |  |                       |
    | | | ROM Header        |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | PCIR Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | | | NPDE Structure    |                         |  |                       |
    | | +-------------------+                         |  |                       |
    | |                                               |  |                       |
    | | +-------------------+                         |  |                       |
    | | | PMU Lookup Table  | <- falcon_data_offset <----+                       |
    | | | +-------------+   |    pmu_lookup_table     |                          |
    | | | | Entry 0x85  |   |                         |                          |
    | | | | FWSEC_PROD  |   |                         |                          |
    | | | +-------------+   |                         |                          |
    | | +-------------------+                         |                          |
    | |         |                                     |                          |
    | |         | points to                           |                          |
    | |         V                                     |                          |
    | | +-------------------+                         |                          |
    | | | FalconUCodeDescV3 | <- falcon_ucode_offset  |                          |
    | | | (FWSEC Firmware)  |    fwsec_header()       |                          |
    | | +-------------------+                         |                          |
    | |         |   immediately followed  by...       |                          |
    | |         V                                     |                          |
    | | +----------------------------+                |                          |
    | | | Signatures + FWSEC Ucode   |                |                          |
    | | | fwsec_sigs(), fwsec_ucode()|                |                          |
    | | +----------------------------+                |                          |
    | +-----------------------------------------------+                          |
    |                                                                            |
    +----------------------------------------------------------------------------+

.. note::
   This diagram is created based on an GA-102 Ampere GPU as an example and could
   vary for future or other GPUs.

.. note::
   For more explanations of acronyms, see the detailed descriptions in `vbios.rs`.

Falcon data Lookup
------------------
A key part of the VBIOS extraction code (vbios.rs) is to find the location of the
Falcon data in the VBIOS which contains the PMU lookup table. This lookup table is
used to find the required Falcon ucode based on an application ID.

The location of the PMU lookup table is found by scanning the BIT (`BIOS Information Table`_)
tokens for a token with the id `BIT_TOKEN_ID_FALCON_DATA` (0x70) which indicates the
offset of the same from the start of the VBIOS image. Unfortunately, the offset
does not account for the EFI image located between the PciAt and FwSec images.
The `vbios.rs` code compensates for this with appropriate arithmetic.

.. _`BIOS Information Table`: https://download.nvidia.com/open-gpu-doc/BIOS-Information-Table/1/BIOS-Information-Table.html
