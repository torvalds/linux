.. SPDX-License-Identifier: GPL-2.0

========================
TPM CRB over FF-A Driver
========================

The TPM Command Response Buffer (CRB) interface is a standard TPM interface
defined in the TCG PC Client Platform TPM Profile (PTP) Specification [1]_.
The CRB provides a structured set of control registers a client uses when
interacting with a TPM as well as a data buffer for storing TPM commands and
responses. A CRB interface can be implemented in:

- hardware registers in a discrete TPM chip

- in memory for a TPM running in isolated environment where shared memory
  allows a client to interact with the TPM

The Firmware Framework for Arm A-profile (FF-A) [2]_ is a specification
that defines interfaces and protocols for the following purposes:

- Compartmentalize firmware into software partitions that run in the Arm
  Secure world environment (also know as TrustZone)

- Provide a standard interface for software components in the Non-secure
  state, for example OS and Hypervisors, to communicate with this firmware.

A TPM can be implemented as an FF-A secure service.  This could be a firmware
TPM or could potentially be a TPM service that acts as a proxy to a discrete
TPM chip. An FF-A based TPM abstracts hardware details (e.g. bus controller
and chip selects) away from the OS and can protect locality 4 from access
by an OS.  The TCG-defined CRB interface is used by clients to interact
with the TPM service.

The Arm TPM Service Command Response Buffer Interface Over FF-A [3]_
specification defines FF-A messages that can be used by a client to signal
when updates have been made to the CRB.

How the Linux CRB driver interacts with FF-A is summarized below:

- The tpm_crb_ffa driver registers with the FF-A subsystem in the kernel
  with an architected TPM service UUID defined in the CRB over FF-A spec.

- If a TPM service is discovered by FF-A, the probe() function in the
  tpm_crb_ffa driver runs, and the driver initializes.

- The probing and initialization of the Linux CRB driver is triggered
  by the discovery of a TPM advertised via ACPI.  The CRB driver can
  detect the type of TPM through the ACPI 'start' method.  The start
  method for Arm FF-A was defined in TCG ACPI v1.4 [4]_.

- When the CRB driver performs its normal functions such as signaling 'start'
  and locality request/relinquish it invokes the tpm_crb_ffa_start() funnction
  in the tpm_crb_ffa driver which handles the FF-A messaging to the TPM.

References
==========

.. [1] **TCG PC Client Platform TPM Profile (PTP) Specification**
   https://trustedcomputinggroup.org/resource/pc-client-platform-tpm-profile-ptp-specification/
.. [2] **Arm Firmware Framework for Arm A-profile (FF-A)**
   https://developer.arm.com/documentation/den0077/latest/
.. [3] **Arm TPM Service Command Response Buffer Interface Over FF-A**
   https://developer.arm.com/documentation/den0138/latest/
.. [4] **TCG ACPI Specification**
   https://trustedcomputinggroup.org/resource/tcg-acpi-specification/
