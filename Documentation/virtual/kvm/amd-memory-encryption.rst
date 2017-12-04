======================================
Secure Encrypted Virtualization (SEV)
======================================

Overview
========

Secure Encrypted Virtualization (SEV) is a feature found on AMD processors.

SEV is an extension to the AMD-V architecture which supports running
virtual machines (VMs) under the control of a hypervisor. When enabled,
the memory contents of a VM will be transparently encrypted with a key
unique to that VM.

The hypervisor can determine the SEV support through the CPUID
instruction. The CPUID function 0x8000001f reports information related
to SEV::

	0x8000001f[eax]:
			Bit[1] 	indicates support for SEV
	    ...
		  [ecx]:
			Bits[31:0]  Number of encrypted guests supported simultaneously

If support for SEV is present, MSR 0xc001_0010 (MSR_K8_SYSCFG) and MSR 0xc001_0015
(MSR_K7_HWCR) can be used to determine if it can be enabled::

	0xc001_0010:
		Bit[23]	   1 = memory encryption can be enabled
			   0 = memory encryption can not be enabled

	0xc001_0015:
		Bit[0]	   1 = memory encryption can be enabled
			   0 = memory encryption can not be enabled

When SEV support is available, it can be enabled in a specific VM by
setting the SEV bit before executing VMRUN.::

	VMCB[0x90]:
		Bit[1]	    1 = SEV is enabled
			    0 = SEV is disabled

SEV hardware uses ASIDs to associate a memory encryption key with a VM.
Hence, the ASID for the SEV-enabled guests must be from 1 to a maximum value
defined in the CPUID 0x8000001f[ecx] field.
