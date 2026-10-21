.. SPDX-License-Identifier: GPL-2.0

===================================================
FSP (Foundation Security Processor) and Secure Boot
===================================================
This document describes the role of the FSP in the GPU boot sequence on
Hopper and Blackwell GPUs, and how it differs from the earlier Ampere boot
flow. It also provides a brief overview of the PRC (Product Reconfiguration
Control) protocol used to query device configuration through FSP. As with
other documents in this directory, the information is subject to change and
is intended to help developers understand the corresponding kernel code.

What is FSP?
============
The Foundation Security Processor (FSP) is the GPU's Internal Root of Trust
(IROT). It is a dedicated security processor that boots from immutable ROM
(Boot ROM) inside the GPU and is responsible for establishing the Chain of
Trust before any other firmware is allowed to run.

FSP runs independently of the host CPU and starts executing as soon as the
GPU is powered on. By the time the nova-core driver is loaded, FSP has
already completed its own secure boot and is ready to accept commands from
the driver.

Simplified boot flow (Hopper/Blackwell)
=======================================
Starting with Hopper, the boot flow is significantly simplified compared to
earlier GPU generations like Ampere.

On an **Ampere** GPU, the boot verification chain involves multiple Falcon
engines and multiple ucode stages (see falcon.rst for details)::

     Hardware BROM (SEC2)
          -> HS Booter (SEC2)
               -> LS GSP-RM (GSP)

The driver must extract ucode from VBIOS, manage SEC2 and GSP, and
orchestrate the Booter to load GSP-RM. This involves FWSEC-FRTS, devinit,
and the Booter stages.

On **Hopper/Blackwell** GPUs, FSP replaces this multi-stage process with a
single message-driven interface::

     FSP (hardware root of trust, boots from ROM)
          -> FMC (Falcon Microcontroller, verified by FSP)
               -> GSP-RM (verified and loaded by FMC)

The driver only needs to:

1. Wait for FSP to complete its own secure boot (polling a scratch register).
2. Send a Chain of Trust (COT) message to FSP with the FMC firmware location,
   cryptographic signatures, and GSP boot parameters.
3. FSP authenticates the FMC firmware and boots it, FMC in turn loads GSP-RM.

There is no SEC2 involvement, no Booter ucode, and no FWSEC-FRTS stage. The
entire secure boot is driven by a single FSP message exchange.

Chain of Trust (COT) protocol
=============================
The Chain of Trust establishes a cryptographically enforced boot sequence,
ensuring the GPU reaches a known, trusted state.

The driver communicates with FSP using a message queue (Falcon MSGQ
interface). Each message consists of an MCTP (Management Component Transport
Protocol) transport header and an NVDM (NVIDIA Vendor Defined Message) header,
followed by a protocol-specific payload.

For Chain of Trust, the payload includes:

- The system memory address of the FMC firmware image.
- Cryptographic material: a SHA-384 hash, RSA-3K public key, and RSA-3K
  signature extracted from the FMC ELF firmware.
- FRTS (Firmware Runtime Services) region information (vidmem offset and size).
- The system memory address of the GSP boot arguments structure.

FSP verifies the signature against the provided public key and hash, and if
verification succeeds, boots the FMC. The FMC then authenticates and launches
GSP-RM.

The message flow is::

     nova-core                          FSP
        |                                |
        |  1. Poll scratch register      |
        |  (wait for FSP boot complete)  |
        |                                |
        |  2. COT message  ------------> |
        |     (FMC addr, signatures,     |
        |      boot params)              |
        |                                |
        |                                |--- Verify FMC signature
        |                                |--- Boot FMC
        |                                |--- FMC loads GSP-RM
        |                                |
        |  3. COT response <------------ |
        |     (success/error)            |
        |                                |

FSP message format
==================
All FSP messages share a common header format consisting of two 32-bit words:

**MCTP header** (Management Component Transport Protocol):

- Bit 31: SOM (Start of Message)
- Bit 30: EOM (End of Message)
- Bits 29:28: Packet sequence number
- Bits 23:16: Source Endpoint ID

**NVDM header** (NVIDIA Vendor Defined Message):

- Bits 6:0: MCTP message type (0x7e = vendor-defined PCI)
- Bits 23:8: PCI vendor ID (0x10de = NVIDIA)
- Bits 31:24: NVDM type (0x14 = COT, 0x13 = PRC, 0x15 = FSP response)

PRC (Product Reconfiguration Control) protocol
===============================================
PRC is an API system exposed through FSP's Management Partition that allows
querying and modifying device configuration without firmware updates.

Configuration parameters are called "knobs". Each knob has a unique object
ID and controls a specific device behavior. Examples include vGPU mode, ECC
enable, confidential computing mode, and NVLINK configuration.

Each knob has two values:

- **Active**: the currently effective value for this boot cycle.
- **Persistent**: the value stored in InfoROM, applied on subsequent boots.

The nova-core driver uses PRC to read the vGPU mode knob (object ID 0x29)
during early boot, before firmware loading, to determine whether the GPU
should operate in vGPU mode.

The PRC message format follows the same MCTP/NVDM header structure as COT,
with NVDM type 0x13. The payload contains:

- A sub-command (e.g., 0x0c for read).
- Flags indicating which value to read (bit 0 = persistent, bit 1 = active).
- The knob object ID.

The response includes the common FSP response header (with error status)
followed by the knob's 16-bit state value.
