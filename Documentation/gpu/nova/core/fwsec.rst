.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

=========================
FWSEC (Firmware Security)
=========================
This document briefly/conceptually describes the FWSEC (Firmware Security) image
and its role in the GPU boot sequence. As such, this information is subject to
change in the future and is only current as of the Ampere GPU family. However,
hopefully the concepts described will be useful for understanding the kernel code
that deals with it. All the information is derived from publicly available
sources such as public drivers and documentation.

The role of FWSEC is to provide a secure boot process. It runs in
'Heavy-secure' mode, and performs firmware verification after a GPU reset
before loading various ucode images onto other microcontrollers on the GPU,
such as the PMU and GSP.

FWSEC itself is an application stored in the VBIOS ROM in the FWSEC partition of
ROM (see vbios.rst for more details). It contains different commands like FRTS
(Firmware Runtime Services) and SB (Secure Booting other microcontrollers after
reset and loading them with other non-FWSEC ucode). The kernel driver only needs
to perform FRTS, since Secure Boot (SB) has already completed by the time the driver
is loaded.

The FRTS command carves out the WPR2 region (Write protected region) which contains
data required for power management. Once setup, only HS mode ucode can access it
(see falcon.rst for privilege levels).

The FWSEC image is located in the VBIOS ROM in the partition of the ROM that contains
various ucode images (also known as applications) -- one of them being FWSEC. For how
it is extracted, see vbios.rst and the vbios.rs source code.

The Falcon data for each ucode images (including the FWSEC image) is a combination
of headers, data sections (DMEM) and instruction code sections (IMEM). All these
ucode images are stored in the same ROM partition and the PMU table is used to look
up the application to load it based on its application ID (see vbios.rs).

For the nova-core driver, the FWSEC contains an 'application interface' called
DMEMMAPPER. This interface is used to execute the 'FWSEC-FRTS' command, among others.
For Ampere, FWSEC is running on the GSP in Heavy-secure mode and runs FRTS.

FWSEC Memory Layout
-------------------
The memory layout of the FWSEC image is as follows::

   +---------------------------------------------------------------+
   |                         FWSEC ROM image (type 0xE0)           |
   |                                                               |
   |  +---------------------------------+                          |
   |  |     PMU Falcon Ucode Table      |                          |
   |  |     (PmuLookupTable)            |                          |
   |  |  +-------------------------+    |                          |
   |  |  | Table Header            |    |                          |
   |  |  | - version: 0x01         |    |                          |
   |  |  | - header_size: 6        |    |                          |
   |  |  | - entry_size: 6         |    |                          |
   |  |  | - entry_count: N        |    |                          |
   |  |  | - desc_version:3(unused)|    |                          |
   |  |  +-------------------------+    |                          |
   |  |         ...                     |                          |
   |  |  +-------------------------+    |                          |
   |  |  | Entry for FWSEC (0x85)  |    |                          |
   |  |  | (PmuLookupTableEntry)   |    |                          |
   |  |  | - app_id: 0x85 (FWSEC)  |----|----+                     |
   |  |  | - target_id: 0x01 (PMU) |    |    |                     |
   |  |  | - data: offset ---------|----|----|---+ look up FWSEC   |
   |  |  +-------------------------+    |    |   |                 |
   |  +---------------------------------+    |   |                 |
   |                                         |   |                 |
   |                                         |   |                 |
   |  +---------------------------------+    |   |                 |
   |  |     FWSEC Ucode Component       |<---+   |                 |
   |  |     (aka Falcon data)           |        |                 |
   |  |  +-------------------------+    |        |                 |
   |  |  | FalconUCodeDescV3       |<---|--------+                 |
   |  |  | - hdr                   |    |                          |
   |  |  | - stored_size           |    |                          |
   |  |  | - pkc_data_offset       |    |                          |
   |  |  | - interface_offset -----|----|----------------+         |
   |  |  | - imem_phys_base        |    |                |         |
   |  |  | - imem_load_size        |    |                |         |
   |  |  | - imem_virt_base        |    |                |         |
   |  |  | - dmem_phys_base        |    |                |         |
   |  |  | - dmem_load_size        |    |                |         |
   |  |  | - engine_id_mask        |    |                |         |
   |  |  | - ucode_id              |    |                |         |
   |  |  | - signature_count       |    |    look up sig |         |
   |  |  | - signature_versions --------------+          |         |
   |  |  +-------------------------+    |     |          |         |
   |  |         (no gap)                |     |          |         |
   |  |  +-------------------------+    |     |          |         |
   |  |  | Signatures Section      |<---|-----+          |         |
   |  |  | (384 bytes per sig)     |    |                |         |
   |  |  | - RSA-3K Signature 1    |    |                |         |
   |  |  | - RSA-3K Signature 2    |    |                |         |
   |  |  |   ...                   |    |                |         |
   |  |  +-------------------------+    |                |         |
   |  |                                 |                |         |
   |  |  +-------------------------+    |                |         |
   |  |  | IMEM Section (Code)     |    |                |         |
   |  |  |                         |    |                |         |
   |  |  | Contains instruction    |    |                |         |
   |  |  | code etc.               |    |                |         |
   |  |  +-------------------------+    |                |         |
   |  |                                 |                |         |
   |  |  +-------------------------+    |                |         |
   |  |  | DMEM Section (Data)     |    |                |         |
   |  |  |                         |    |                |         |
   |  |  | +---------------------+ |    |                |         |
   |  |  | | Application         | |<---|----------------+         |
   |  |  | | Interface Table     | |    |                          |
   |  |  | | (FalconAppifHdrV1)  | |    |                          |
   |  |  | | Header:             | |    |                          |
   |  |  | | - version: 0x01     | |    |                          |
   |  |  | | - header_size: 4    | |    |                          |
   |  |  | | - entry_size: 8     | |    |                          |
   |  |  | | - entry_count: N    | |    |                          |
   |  |  | |                     | |    |                          |
   |  |  | | Entries:            | |    |                          |
   |  |  | | +-----------------+ | |    |                          |
   |  |  | | | DEVINIT (ID 1)  | | |    |                          |
   |  |  | | | - id: 0x01      | | |    |                          |
   |  |  | | | - dmemOffset X -|-|-|----+                          |
   |  |  | | +-----------------+ | |    |                          |
   |  |  | | +-----------------+ | |    |                          |
   |  |  | | | DMEMMAPPER(ID 4)| | |    |                          |
   |  |  | | | - id: 0x04      | | |    | Used only for DevInit    |
   |  |  | | |  (NVFW_FALCON_  | | |    | application (not FWSEC)  |
   |  |  | | |   APPIF_ID_DMEMMAPPER)   |                          |
   |  |  | | | - dmemOffset Y -|-|-|----|-----+                    |
   |  |  | | +-----------------+ | |    |     |                    |
   |  |  | +---------------------+ |    |     |                    |
   |  |  |                         |    |     |                    |
   |  |  | +---------------------+ |    |     |                    |
   |  |  | | DEVINIT Engine      |<|----+     | Used by FWSEC      |
   |  |  | | Interface           | |    |     |         app.       |
   |  |  | +---------------------+ |    |     |                    |
   |  |  |                         |    |     |                    |
   |  |  | +---------------------+ |    |     |                    |
   |  |  | | DMEM Mapper (ID 4)  |<|----+-----+                    |
   |  |  | | (FalconAppifDmemmapperV3)  |                          |
   |  |  | | - signature: "DMAP" | |    |                          |
   |  |  | | - version: 0x0003   | |    |                          |
   |  |  | | - Size: 64 bytes    | |    |                          |
   |  |  | | - cmd_in_buffer_off | |----|------------+             |
   |  |  | | - cmd_in_buffer_size| |    |            |             |
   |  |  | | - cmd_out_buffer_off| |----|------------|-----+       |
   |  |  | | - cmd_out_buffer_sz | |    |            |     |       |
   |  |  | | - init_cmd          | |    |            |     |       |
   |  |  | | - features          | |    |            |     |       |
   |  |  | | - cmd_mask0/1       | |    |            |     |       |
   |  |  | +---------------------+ |    |            |     |       |
   |  |  |                         |    |            |     |       |
   |  |  | +---------------------+ |    |            |     |       |
   |  |  | | Command Input Buffer|<|----|------------+     |       |
   |  |  | | - Command data      | |    |                  |       |
   |  |  | | - Arguments         | |    |                  |       |
   |  |  | +---------------------+ |    |                  |       |
   |  |  |                         |    |                  |       |
   |  |  | +---------------------+ |    |                  |       |
   |  |  | | Command Output      |<|----|------------------+       |
   |  |  | | Buffer              | |    |                          |
   |  |  | | - Results           | |    |                          |
   |  |  | | - Status            | |    |                          |
   |  |  | +---------------------+ |    |                          |
   |  |  +-------------------------+    |                          |
   |  +---------------------------------+                          |
   |                                                               |
   +---------------------------------------------------------------+

.. note::
   This is using an GA-102 Ampere GPU as an example and could vary for future GPUs.

.. note::
   The FWSEC image also plays a role in memory scrubbing (ECC initialization) and VPR
   (Video Protected Region) initialization as well. Before the nova-core driver is even
   loaded, the FWSEC image is running on the GSP in heavy-secure mode. After the devinit
   sequence completes, it does VRAM memory scrubbing (ECC initialization). On consumer
   GPUs, it scrubs only part of memory and then initiates 'async scrubbing'. Before this
   async scrubbing completes, the unscrubbed VRAM cannot be used for allocation (thus DRM
   memory allocators need to wait for this scrubbing to complete).
