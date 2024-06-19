.. SPDX-License-Identifier: GPL-2.0

=========================
TPM FIFO interface driver
=========================

TCG PTP Specification defines two interface types: FIFO and CRB. The former is
based on sequenced read and write operations,  and the latter is based on a
buffer containing the full command or response.

FIFO (First-In-First-Out) interface is used by the tpm_tis_core dependent
drivers. Originally Linux had only a driver called tpm_tis, which covered
memory mapped (aka MMIO) interface but it was later on extended to cover other
physical interfaces supported by the TCG standard.

For historical reasons above the original MMIO driver is called tpm_tis and the
framework for FIFO drivers is named as tpm_tis_core. The postfix "tis" in
tpm_tis comes from the TPM Interface Specification, which is the hardware
interface specification for TPM 1.x chips.

Communication is based on a 20 KiB buffer shared by the TPM chip through a
hardware bus or memory map, depending on the physical wiring. The buffer is
further split into five equal-size 4 KiB buffers, which provide equivalent
sets of registers for communication between the CPU and TPM. These
communication endpoints are called localities in the TCG terminology.

When the kernel wants to send commands to the TPM chip, it first reserves
locality 0 by setting the requestUse bit in the TPM_ACCESS register. The bit is
cleared by the chip when the access is granted. Once it completes its
communication, the kernel writes the TPM_ACCESS.activeLocality bit. This
informs the chip that the locality has been relinquished.

Pending localities are served in order by the chip in descending order, one at
a time:

- Locality 0 has the lowest priority.
- Locality 5 has the highest priority.

Further information on the purpose and meaning of the localities can be found
in section 3.2 of the TCG PC Client Platform TPM Profile Specification.

References
==========

TCG PC Client Platform TPM Profile (PTP) Specification
https://trustedcomputinggroup.org/resource/pc-client-platform-tpm-profile-ptp-specification/
