.. SPDX-License-Identifier: GPL-2.0

===================================
Intel Trust Domain Extensions (TDX)
===================================

Overview
========
Intel's Trust Domain Extensions (TDX) protect confidential guest VMs from the
host and physical attacks.  A CPU-attested software module called 'the TDX
module' runs inside a new CPU isolated range to provide the functionalities to
manage and run protected VMs, a.k.a, TDX guests or TDs.

Please refer to [1] for the whitepaper, specifications and other resources.

This documentation describes TDX-specific KVM ABIs.  The TDX module needs to be
initialized before it can be used by KVM to run any TDX guests.  The host
core-kernel provides the support of initializing the TDX module, which is
described in the Documentation/arch/x86/tdx.rst.

API description
===============

KVM_MEMORY_ENCRYPT_OP
---------------------
:Type: vm ioctl, vcpu ioctl

For TDX operations, KVM_MEMORY_ENCRYPT_OP is re-purposed to be generic
ioctl with TDX specific sub-ioctl() commands.

::

  /* Trust Domain Extensions sub-ioctl() commands. */
  enum kvm_tdx_cmd_id {
          KVM_TDX_CAPABILITIES = 0,
          KVM_TDX_INIT_VM,
          KVM_TDX_INIT_VCPU,
          KVM_TDX_INIT_MEM_REGION,
          KVM_TDX_FINALIZE_VM,
          KVM_TDX_GET_CPUID,

          KVM_TDX_CMD_NR_MAX,
  };

  struct kvm_tdx_cmd {
        /* enum kvm_tdx_cmd_id */
        __u32 id;
        /* flags for sub-command. If sub-command doesn't use this, set zero. */
        __u32 flags;
        /*
         * data for each sub-command. An immediate or a pointer to the actual
         * data in process virtual address.  If sub-command doesn't use it,
         * set zero.
         */
        __u64 data;
        /*
         * Auxiliary error code.  The sub-command may return TDX SEAMCALL
         * status code in addition to -Exxx.
         */
        __u64 hw_error;
  };

KVM_TDX_CAPABILITIES
--------------------
:Type: vm ioctl
:Returns: 0 on success, <0 on error

Return the TDX capabilities that current KVM supports with the specific TDX
module loaded in the system.  It reports what features/capabilities are allowed
to be configured to the TDX guest.

- id: KVM_TDX_CAPABILITIES
- flags: must be 0
- data: pointer to struct kvm_tdx_capabilities
- hw_error: must be 0

::

  struct kvm_tdx_capabilities {
        __u64 supported_attrs;
        __u64 supported_xfam;
        __u64 reserved[254];

        /* Configurable CPUID bits for userspace */
        struct kvm_cpuid2 cpuid;
  };


KVM_TDX_INIT_VM
---------------
:Type: vm ioctl
:Returns: 0 on success, <0 on error

Perform TDX specific VM initialization.  This needs to be called after
KVM_CREATE_VM and before creating any VCPUs.

- id: KVM_TDX_INIT_VM
- flags: must be 0
- data: pointer to struct kvm_tdx_init_vm
- hw_error: must be 0

::

  struct kvm_tdx_init_vm {
          __u64 attributes;
          __u64 xfam;
          __u64 mrconfigid[6];          /* sha384 digest */
          __u64 mrowner[6];             /* sha384 digest */
          __u64 mrownerconfig[6];       /* sha384 digest */

          /* The total space for TD_PARAMS before the CPUIDs is 256 bytes */
          __u64 reserved[12];

        /*
         * Call KVM_TDX_INIT_VM before vcpu creation, thus before
         * KVM_SET_CPUID2.
         * This configuration supersedes KVM_SET_CPUID2s for VCPUs because the
         * TDX module directly virtualizes those CPUIDs without VMM.  The user
         * space VMM, e.g. qemu, should make KVM_SET_CPUID2 consistent with
         * those values.  If it doesn't, KVM may have wrong idea of vCPUIDs of
         * the guest, and KVM may wrongly emulate CPUIDs or MSRs that the TDX
         * module doesn't virtualize.
         */
          struct kvm_cpuid2 cpuid;
  };


KVM_TDX_INIT_VCPU
-----------------
:Type: vcpu ioctl
:Returns: 0 on success, <0 on error

Perform TDX specific VCPU initialization.

- id: KVM_TDX_INIT_VCPU
- flags: must be 0
- data: initial value of the guest TD VCPU RCX
- hw_error: must be 0

KVM_TDX_INIT_MEM_REGION
-----------------------
:Type: vcpu ioctl
:Returns: 0 on success, <0 on error

Initialize @nr_pages TDX guest private memory starting from @gpa with userspace
provided data from @source_addr.

Note, before calling this sub command, memory attribute of the range
[gpa, gpa + nr_pages] needs to be private.  Userspace can use
KVM_SET_MEMORY_ATTRIBUTES to set the attribute.

If KVM_TDX_MEASURE_MEMORY_REGION flag is specified, it also extends measurement.

- id: KVM_TDX_INIT_MEM_REGION
- flags: currently only KVM_TDX_MEASURE_MEMORY_REGION is defined
- data: pointer to struct kvm_tdx_init_mem_region
- hw_error: must be 0

::

  #define KVM_TDX_MEASURE_MEMORY_REGION   (1UL << 0)

  struct kvm_tdx_init_mem_region {
          __u64 source_addr;
          __u64 gpa;
          __u64 nr_pages;
  };


KVM_TDX_FINALIZE_VM
-------------------
:Type: vm ioctl
:Returns: 0 on success, <0 on error

Complete measurement of the initial TD contents and mark it ready to run.

- id: KVM_TDX_FINALIZE_VM
- flags: must be 0
- data: must be 0
- hw_error: must be 0


KVM_TDX_GET_CPUID
-----------------
:Type: vcpu ioctl
:Returns: 0 on success, <0 on error

Get the CPUID values that the TDX module virtualizes for the TD guest.
When it returns -E2BIG, the user space should allocate a larger buffer and
retry. The minimum buffer size is updated in the nent field of the
struct kvm_cpuid2.

- id: KVM_TDX_GET_CPUID
- flags: must be 0
- data: pointer to struct kvm_cpuid2 (in/out)
- hw_error: must be 0 (out)

::

  struct kvm_cpuid2 {
	  __u32 nent;
	  __u32 padding;
	  struct kvm_cpuid_entry2 entries[0];
  };

  struct kvm_cpuid_entry2 {
	  __u32 function;
	  __u32 index;
	  __u32 flags;
	  __u32 eax;
	  __u32 ebx;
	  __u32 ecx;
	  __u32 edx;
	  __u32 padding[3];
  };

KVM TDX creation flow
=====================
In addition to the standard KVM flow, new TDX ioctls need to be called.  The
control flow is as follows:

#. Check system wide capability

   * KVM_CAP_VM_TYPES: Check if VM type is supported and if KVM_X86_TDX_VM
     is supported.

#. Create VM

   * KVM_CREATE_VM
   * KVM_TDX_CAPABILITIES: Query TDX capabilities for creating TDX guests.
   * KVM_CHECK_EXTENSION(KVM_CAP_MAX_VCPUS): Query maximum VCPUs the TD can
     support at VM level (TDX has its own limitation on this).
   * KVM_SET_TSC_KHZ: Configure TD's TSC frequency if a different TSC frequency
     than host is desired.  This is Optional.
   * KVM_TDX_INIT_VM: Pass TDX specific VM parameters.

#. Create VCPU

   * KVM_CREATE_VCPU
   * KVM_TDX_INIT_VCPU: Pass TDX specific VCPU parameters.
   * KVM_SET_CPUID2: Configure TD's CPUIDs.
   * KVM_SET_MSRS: Configure TD's MSRs.

#. Initialize initial guest memory

   * Prepare content of initial guest memory.
   * KVM_TDX_INIT_MEM_REGION: Add initial guest memory.
   * KVM_TDX_FINALIZE_VM: Finalize the measurement of the TDX guest.

#. Run VCPU

References
==========

https://www.intel.com/content/www/us/en/developer/tools/trust-domain-extensions/documentation.html
