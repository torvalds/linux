.. SPDX-License-Identifier: GPL-2.0

=====================
AMD Memory Encryption
=====================

Secure Memory Encryption (SME) and Secure Encrypted Virtualization (SEV) are
features found on AMD processors.

SME provides the ability to mark individual pages of memory as encrypted using
the standard x86 page tables.  A page that is marked encrypted will be
automatically decrypted when read from DRAM and encrypted when written to
DRAM.  SME can therefore be used to protect the contents of DRAM from physical
attacks on the system.

SEV enables running encrypted virtual machines (VMs) in which the code and data
of the guest VM are secured so that a decrypted version is available only
within the VM itself. SEV guest VMs have the concept of private and shared
memory. Private memory is encrypted with the guest-specific key, while shared
memory may be encrypted with hypervisor key. When SME is enabled, the hypervisor
key is the same key which is used in SME.

A page is encrypted when a page table entry has the encryption bit set (see
below on how to determine its position).  The encryption bit can also be
specified in the cr3 register, allowing the PGD table to be encrypted. Each
successive level of page tables can also be encrypted by setting the encryption
bit in the page table entry that points to the next table. This allows the full
page table hierarchy to be encrypted. Note, this means that just because the
encryption bit is set in cr3, doesn't imply the full hierarchy is encrypted.
Each page table entry in the hierarchy needs to have the encryption bit set to
achieve that. So, theoretically, you could have the encryption bit set in cr3
so that the PGD is encrypted, but not set the encryption bit in the PGD entry
for a PUD which results in the PUD pointed to by that entry to not be
encrypted.

When SEV is enabled, instruction pages and guest page tables are always treated
as private. All the DMA operations inside the guest must be performed on shared
memory. Since the memory encryption bit is controlled by the guest OS when it
is operating in 64-bit or 32-bit PAE mode, in all other modes the SEV hardware
forces the memory encryption bit to 1.

Support for SME and SEV can be determined through the CPUID instruction. The
CPUID function 0x8000001f reports information related to SME::

	0x8000001f[eax]:
		Bit[0] indicates support for SME
		Bit[1] indicates support for SEV
	0x8000001f[ebx]:
		Bits[5:0]  pagetable bit number used to activate memory
			   encryption
		Bits[11:6] reduction in physical address space, in bits, when
			   memory encryption is enabled (this only affects
			   system physical addresses, not guest physical
			   addresses)

If support for SME is present, MSR 0xc00100010 (MSR_AMD64_SYSCFG) can be used to
determine if SME is enabled and/or to enable memory encryption::

	0xc0010010:
		Bit[23]   0 = memory encryption features are disabled
			  1 = memory encryption features are enabled

If SEV is supported, MSR 0xc0010131 (MSR_AMD64_SEV) can be used to determine if
SEV is active::

	0xc0010131:
		Bit[0]	  0 = memory encryption is not active
			  1 = memory encryption is active

Linux relies on BIOS to set this bit if BIOS has determined that the reduction
in the physical address space as a result of enabling memory encryption (see
CPUID information above) will not conflict with the address space resource
requirements for the system.  If this bit is not set upon Linux startup then
Linux itself will not set it and memory encryption will not be possible.

The state of SME in the Linux kernel can be documented as follows:

	- Supported:
	  The CPU supports SME (determined through CPUID instruction).

	- Enabled:
	  Supported and bit 23 of MSR_AMD64_SYSCFG is set.

	- Active:
	  Supported, Enabled and the Linux kernel is actively applying
	  the encryption bit to page table entries (the SME mask in the
	  kernel is non-zero).

SME can also be enabled and activated in the BIOS. If SME is enabled and
activated in the BIOS, then all memory accesses will be encrypted and it
will not be necessary to activate the Linux memory encryption support.

If the BIOS merely enables SME (sets bit 23 of the MSR_AMD64_SYSCFG),
then memory encryption can be enabled by supplying mem_encrypt=on on the
kernel command line.  However, if BIOS does not enable SME, then Linux
will not be able to activate memory encryption, even if configured to do
so by default or the mem_encrypt=on command line parameter is specified.

Secure Nested Paging (SNP)
==========================

SEV-SNP introduces new features (SEV_FEATURES[1:63]) which can be enabled
by the hypervisor for security enhancements. Some of these features need
guest side implementation to function correctly. The below table lists the
expected guest behavior with various possible scenarios of guest/hypervisor
SNP feature support.

+-----------------+---------------+---------------+------------------+
| Feature Enabled | Guest needs   | Guest has     | Guest boot       |
| by the HV       | implementation| implementation| behaviour        |
+=================+===============+===============+==================+
|      No         |      No       |      No       |     Boot         |
|                 |               |               |                  |
+-----------------+---------------+---------------+------------------+
|      No         |      Yes      |      No       |     Boot         |
|                 |               |               |                  |
+-----------------+---------------+---------------+------------------+
|      No         |      Yes      |      Yes      |     Boot         |
|                 |               |               |                  |
+-----------------+---------------+---------------+------------------+
|      Yes        |      No       |      No       | Boot with        |
|                 |               |               | feature enabled  |
+-----------------+---------------+---------------+------------------+
|      Yes        |      Yes      |      No       | Graceful boot    |
|                 |               |               | failure          |
+-----------------+---------------+---------------+------------------+
|      Yes        |      Yes      |      Yes      | Boot with        |
|                 |               |               | feature enabled  |
+-----------------+---------------+---------------+------------------+

More details in AMD64 APM[1] Vol 2: 15.34.10 SEV_STATUS MSR

Reverse Map Table (RMP)
=======================

The RMP is a structure in system memory that is used to ensure a one-to-one
mapping between system physical addresses and guest physical addresses. Each
page of memory that is potentially assignable to guests has one entry within
the RMP.

The RMP table can be either contiguous in memory or a collection of segments
in memory.

Contiguous RMP
--------------

Support for this form of the RMP is present when support for SEV-SNP is
present, which can be determined using the CPUID instruction::

	0x8000001f[eax]:
		Bit[4] indicates support for SEV-SNP

The location of the RMP is identified to the hardware through two MSRs::

        0xc0010132 (RMP_BASE):
                System physical address of the first byte of the RMP

        0xc0010133 (RMP_END):
                System physical address of the last byte of the RMP

Hardware requires that RMP_BASE and (RPM_END + 1) be 8KB aligned, but SEV
firmware increases the alignment requirement to require a 1MB alignment.

The RMP consists of a 16KB region used for processor bookkeeping followed
by the RMP entries, which are 16 bytes in size. The size of the RMP
determines the range of physical memory that the hypervisor can assign to
SEV-SNP guests. The RMP covers the system physical address from::

        0 to ((RMP_END + 1 - RMP_BASE - 16KB) / 16B) x 4KB.

The current Linux support relies on BIOS to allocate/reserve the memory for
the RMP and to set RMP_BASE and RMP_END appropriately. Linux uses the MSR
values to locate the RMP and determine the size of the RMP. The RMP must
cover all of system memory in order for Linux to enable SEV-SNP.

Segmented RMP
-------------

Segmented RMP support is a new way of representing the layout of an RMP.
Initial RMP support required the RMP table to be contiguous in memory.
RMP accesses from a NUMA node on which the RMP doesn't reside
can take longer than accesses from a NUMA node on which the RMP resides.
Segmented RMP support allows the RMP entries to be located on the same
node as the memory the RMP is covering, potentially reducing latency
associated with accessing an RMP entry associated with the memory. Each
RMP segment covers a specific range of system physical addresses.

Support for this form of the RMP can be determined using the CPUID
instruction::

        0x8000001f[eax]:
                Bit[23] indicates support for segmented RMP

If supported, segmented RMP attributes can be found using the CPUID
instruction::

        0x80000025[eax]:
                Bits[5:0]  minimum supported RMP segment size
                Bits[11:6] maximum supported RMP segment size

        0x80000025[ebx]:
                Bits[9:0]  number of cacheable RMP segment definitions
                Bit[10]    indicates if the number of cacheable RMP segments
                           is a hard limit

To enable a segmented RMP, a new MSR is available::

        0xc0010136 (RMP_CFG):
                Bit[0]     indicates if segmented RMP is enabled
                Bits[13:8] contains the size of memory covered by an RMP
                           segment (expressed as a power of 2)

The RMP segment size defined in the RMP_CFG MSR applies to all segments
of the RMP. Therefore each RMP segment covers a specific range of system
physical addresses. For example, if the RMP_CFG MSR value is 0x2401, then
the RMP segment coverage value is 0x24 => 36, meaning the size of memory
covered by an RMP segment is 64GB (1 << 36). So the first RMP segment
covers physical addresses from 0 to 0xF_FFFF_FFFF, the second RMP segment
covers physical addresses from 0x10_0000_0000 to 0x1F_FFFF_FFFF, etc.

When a segmented RMP is enabled, RMP_BASE points to the RMP bookkeeping
area as it does today (16K in size). However, instead of RMP entries
beginning immediately after the bookkeeping area, there is a 4K RMP
segment table (RST). Each entry in the RST is 8-bytes in size and represents
an RMP segment::

        Bits[19:0]  mapped size (in GB)
                    The mapped size can be less than the defined segment size.
                    A value of zero, indicates that no RMP exists for the range
                    of system physical addresses associated with this segment.
        Bits[51:20] segment physical address
                    This address is left shift 20-bits (or just masked when
                    read) to form the physical address of the segment (1MB
                    alignment).

The RST can hold 512 segment entries but can be limited in size to the number
of cacheable RMP segments (CPUID 0x80000025_EBX[9:0]) if the number of cacheable
RMP segments is a hard limit (CPUID 0x80000025_EBX[10]).

The current Linux support relies on BIOS to allocate/reserve the memory for
the segmented RMP (the bookkeeping area, RST, and all segments), build the RST
and to set RMP_BASE, RMP_END, and RMP_CFG appropriately. Linux uses the MSR
values to locate the RMP and determine the size and location of the RMP
segments. The RMP must cover all of system memory in order for Linux to enable
SEV-SNP.

More details in the AMD64 APM Vol 2, section "15.36.3 Reverse Map Table",
docID: 24593.

Secure VM Service Module (SVSM)
===============================

SNP provides a feature called Virtual Machine Privilege Levels (VMPL) which
defines four privilege levels at which guest software can run. The most
privileged level is 0 and numerically higher numbers have lesser privileges.
More details in the AMD64 APM Vol 2, section "15.35.7 Virtual Machine
Privilege Levels", docID: 24593.

When using that feature, different services can run at different protection
levels, apart from the guest OS but still within the secure SNP environment.
They can provide services to the guest, like a vTPM, for example.

When a guest is not running at VMPL0, it needs to communicate with the software
running at VMPL0 to perform privileged operations or to interact with secure
services. An example fur such a privileged operation is PVALIDATE which is
*required* to be executed at VMPL0.

In this scenario, the software running at VMPL0 is usually called a Secure VM
Service Module (SVSM). Discovery of an SVSM and the API used to communicate
with it is documented in "Secure VM Service Module for SEV-SNP Guests", docID:
58019.

(Latest versions of the above-mentioned documents can be found by using
a search engine like duckduckgo.com and typing in:

  site:amd.com "Secure VM Service Module for SEV-SNP Guests", docID: 58019

for example.)
