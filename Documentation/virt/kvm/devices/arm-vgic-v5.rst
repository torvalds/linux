.. SPDX-License-Identifier: GPL-2.0

====================================================
ARM Virtual Generic Interrupt Controller v5 (VGICv5)
====================================================


Device types supported:
  - KVM_DEV_TYPE_ARM_VGIC_V5     ARM Generic Interrupt Controller v5.0

Only one VGIC instance may be instantiated through this API.  The created VGIC
will act as the VM interrupt controller, requiring emulated user-space devices
to inject interrupts to the VGIC instead of directly to CPUs.

Creating a guest GICv5 device requires a GICv5 host.  The VGICv5 device supports
PPI, SPI, and LPI interrupts.  The PPI and SPI interrupts can either be injected
from emulated in-kernel devices (such as the Arch Timer, or PMU), or via the
KVM_IRQ_LINE ioctl.  LPIs are not externally injected, but are handled in
hardware via the LPI IST.  Their pending state is driven directly by the guest.

Groups:
  KVM_DEV_ARM_VGIC_GRP_ADDR
   Attributes:

    KVM_VGIC_V5_ADDR_TYPE_IRS (rw, 64-bit)
      Base address in the guest physical address space of the GICv5 IRS
      (Interrupt Routing Service) register mappings. Only valid for
      KVM_DEV_TYPE_ARM_VGIC_V5.  This address needs to be 64K aligned and the
      region covers 128 KByte - the IRS has a CONFIG_FRAME and a SETLPI_FRAME,
      each of which is 64 KBytes in size.

      Setting the address of the IRS in GPA space is mandatory before VGIC
      resources are mapped, as the IRS is responsible for handling SPIs and
      LPIs. Failure to set the IRS address before the first vCPU run results in
      an error.

  KVM_DEV_ARM_VGIC_GRP_NR_IRQS
   Attributes:

    A value describing the number of SPIs for this GIC instance. This is
    GICv5-specific: unlike GICv2/v3, the value does not include SGIs or PPIs.
    The value ranges from 32 to KVM's VGICv5 maximum of 1024 SPIs, in
    increments of 32. If userspace does not set this attribute, KVM uses 32
    SPIs by default.

    kvm_device_attr.addr points to a __u32 value.

  KVM_DEV_ARM_VGIC_GRP_CTRL
   Attributes:

    KVM_DEV_ARM_VGIC_CTRL_INIT
      request the initialization of the VGIC, no additional parameter in
      kvm_device_attr.addr. Must be called after all VCPUs have been created.

   KVM_DEV_ARM_VGIC_USERSPACE_PPIS
      request the mask of userspace-drivable PPIs. Only a subset of the PPIs can
      be directly driven from userspace with GICv5, and the returned mask
      informs userspace of which it is allowed to drive via KVM_IRQ_LINE.

      Userspace must allocate and point to __u64[2] of data in
      kvm_device_attr.addr. When this call returns, the provided memory will be
      populated with the userspace PPI mask. The lower __u64 contains the mask
      for the lower 64 PPIS, with the remaining 64 being in the second __u64.

      This is a read-only attribute, and cannot be set. Attempts to set it are
      rejected.

  Errors:

    =======  ========================================================
    -ENXIO   VGIC not properly configured as required prior to calling
             this attribute
    -ENODEV  no online VCPU
    -ENOMEM  memory shortage when allocating vgic internal data
    -EFAULT  Invalid guest ram access
    -EBUSY   One or more VCPUS are running
    =======  ========================================================

  KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS
   Attributes:

    The attr field of kvm_device_attr encodes two values::

      bits:     | 63      ....       32 | 31  ....  16 | 15  ....  0 |
      values:   |         mpidr         |      RES     |    instr    |

    The mpidr field encodes the CPU ID based on the affinity information in the
    architecture defined MPIDR, and the field is encoded as follows::

      | 63 .... 56 | 55 .... 48 | 47 .... 40 | 39 .... 32 |
      |    Aff3    |    Aff2    |    Aff1    |    Aff0    |

    The instr field encodes the system register to access based on the fields
    defined in the A64 instruction set encoding for system register access
    (RES means the bits are reserved for future use and should be zero)::

      | 15 ... 14 | 13 ... 11 | 10 ... 7 | 6 ... 3 | 2 ... 0 |
      |   Op 0    |    Op1    |    CRn   |   CRm   |   Op2   |

    All system regs accessed through this API are (rw, 64-bit) and
    kvm_device_attr.addr points to a __u64 value.

    KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS accesses the CPU interface registers for the
    CPU specified by the mpidr field.

    The available registers are:

    =======================  ===================================================
    ICC_ICSR_EL1
    ICC_PPI_ENABLER0_EL1
    ICC_PPI_ENABLER1_EL1
    ICC_PPI_SACTIVER0_EL1    ICC_PPI_CACTIVER0_EL1 is not supported. Writes to
                             ICC_PPI_SACTIVER0_EL1 are treated as RAW writes of
                             the underlying state.
    ICC_PPI_SACTIVER1_EL1    ICC_PPI_CACTIVER1_EL1 is not supported. Writes to
                             ICC_PPI_SACTIVER1_EL1 are treated as RAW writes of
                             the underlying state.
    ICC_PPI_SPENDR0_EL1      ICC_PPI_CPENDR0_EL1 is not supported. Writes to
                             ICC_PPI_SPENDR0_EL1 are treated as RAW writes of
                             the underlying state.
    ICC_PPI_SPENDR1_EL1      ICC_PPI_CPENDR1_EL1 is not supported. Writes to
                             ICC_PPI_SPENDR1_EL1 are treated as RAW writes of
                             the underlying state.
    ICC_PPI_PRIORITYR0_EL1
    ICC_PPI_PRIORITYR1_EL1
    ICC_PPI_PRIORITYR2_EL1
    ICC_PPI_PRIORITYR3_EL1
    ICC_PPI_PRIORITYR4_EL1
    ICC_PPI_PRIORITYR5_EL1
    ICC_PPI_PRIORITYR6_EL1
    ICC_PPI_PRIORITYR7_EL1
    ICC_PPI_PRIORITYR8_EL1
    ICC_PPI_PRIORITYR9_EL1
    ICC_PPI_PRIORITYR10_EL1
    ICC_PPI_PRIORITYR11_EL1
    ICC_PPI_PRIORITYR12_EL1
    ICC_PPI_PRIORITYR13_EL1
    ICC_PPI_PRIORITYR14_EL1
    ICC_PPI_PRIORITYR15_EL1
    ICC_APR_EL1
    ICC_CR0_EL1
    ICC_PCR_EL1
    =======================  ===================================================

  Errors:

    =======  =============================================================
    -ENXIO   Getting or setting this register is not supported
    -EBUSY   VCPU is running, or write attempted after a VCPU has run
    -EINVAL  Invalid mpidr or register value supplied
    =======  =============================================================


  KVM_DEV_ARM_VGIC_GRP_IRS_REGS
    Attributes:
      The attr field of kvm_device_attr encodes the offset of the IRS register,
      relative to the IRS CONFIG_FRAME base address. This is the address that
      was provided via KVM_VGIC_V5_ADDR_TYPE_IRS when creating VGICv5 in the
      first place.

      kvm_device_attr.addr points to a __u64 value whatever the width
      of the addressed register (32/64 bits). 64 bit registers can only
      be accessed with full length.

      Writes to read-only registers are ignored by the kernel except for
      IRS_IDR0 - IRS_IDR2 and IRS_IDR5 - IRS_IDR7, which are sanity checked to
      ensure that they match a sane config.

      IRS_IDR3 and IRS_IDR4 are RAZ/WI, as nested virtualization is not
      supported.

      For registers without dedicated userspace accessors, getting or setting a
      register uses the same emulated MMIO handlers as guest reads/writes.
      Dedicated userspace accessors may instead save or restore migration state
      without triggering guest-visible side effects. For example, restoring
      IRS_IST_BASER only restores the emulated register state; any host LPI IST
      allocation based on the restored IRS_IST_CFGR and IRS_IST_BASER state
      happens when KVM_DEV_ARM_VGIC_GRP_IST is restored.

      Once an LPI IST has been allocated, IRS_IST_CFGR is immutable. Userspace
      may write back its current value, but setting a different value is
      rejected with -EINVAL.

      Once an LPI IST has been allocated, IRS_IST_BASER is immutable.
      Userspace may write back its current value, but setting a different value
      is rejected with -EINVAL.

  Errors:

    =======  =================================================================
    -ENXIO   Offset does not correspond to any supported register
    -EFAULT  Invalid user pointer for attr->addr
    -EINVAL  Offset is not 32-bit aligned for 32-bit MMIO registers, not
             64-bit aligned for 64-bit registers, or the supplied register
             value is not compatible with the configured VGICv5 IRS state
    -EBUSY   VGIC is not initialized, one or more VCPUs are running, or a
             write is attempted after a VCPU has run
    =======  =================================================================

  KVM_DEV_ARM_VGIC_GRP_IST
    Attributes:
      This interface is used to either save the state of the IRS's Interrupt
      State Tables (ISTs), or to restore them. A get operation saves IST state,
      and a set operation restores IST state. kvm_device_attr.attr is reserved
      and must be zero.

      The VGIC must be initialized before using this interface. Restore must be
      performed before the VM has run. For restore, userspace must have already
      restored the IRS state needed to describe any guest LPI IST.

      Saving first asks the IRS to save and quiesce the VM so that interrupt
      state has been written back to the ISTs. KVM checks that the VM remains
      quiesced while copying out the SPI and LPI IST state.

      kvm_device_attr.addr points to a struct kvm_vgic_v5_ist::

        struct kvm_vgic_v5_ist {
                __u64   spi_ist_addr;
                __u64   spi_ist_size;
                __u64   lpi_ist_addr;
                __u64   lpi_ist_size;
        };

      The SPI and LPI IST buffers contain one little-endian 32-bit IST entry per
      interrupt, in interrupt number order. Only the architected 32-bit ISTE
      state is exposed to userspace; host IST layout and metadata are private to
      KVM.

      The SPI IST buffer is required and its size must be:

        nr_spis * sizeof(__u32)

      where nr_spis is the value returned by KVM_DEV_ARM_VGIC_GRP_NR_IRQS for
      the VGICv5 device. For VGICv5 this value is the number of SPIs, not the
      total number of interrupts.

      The LPI IST buffer is required if the guest's IRS_IST_CFGR and
      IRS_IST_BASER state describes a valid LPI IST, and its size must be:

        BIT(lpi_id_bits) * sizeof(__u32)

      where lpi_id_bits is the LPI ID width configured in IRS_IST_CFGR. If no
      LPI IST is configured, lpi_ist_addr and lpi_ist_size must both be zero.

    Errors:

      ===========  ============================================================
      -EBUSY       One or more VCPUs are running, the VGIC is not initialized,
                   restore was requested after the VM has run, an LPI IST
                   already exists, or the save operation completed but the VM
                   did not remain quiesced
      -EINVAL      attr->addr is NULL, the userspace IST descriptor is missing
                   a required buffer, supplies a non-zero LPI buffer when no
                   LPI IST is configured, has a buffer size that does not match
                   the configured VGICv5 IST state, or an internal VM table
                   operation rejected the VM state
      -EFAULT      Invalid user pointer for attr->addr, spi_ist_addr, or
                   lpi_ist_addr
      -ENXIO       Required per-VM VGICv5/IST backing state is missing or
                   inconsistent
      -ENOMEM      Restoring IST state failed while allocating the host LPI IST
                   or tracking pending interrupts
      -ETIMEDOUT   An IRS save/VM operation timed out
      ===========  ============================================================

IRS Save Sequence:
------------------

The following operations are required when saving the virtual GICv5 IRS:

a) Save the ISTs by issuing KVM_GET_DEVICE_ATTR on KVM_DEV_ARM_VGIC_GRP_IST.
b) Save the IRS MMIO register state by issuing KVM_GET_DEVICE_ATTR on
   KVM_DEV_ARM_VGIC_GRP_IRS_REGS.

These two steps may be performed in either order. Saving the ISTs writes all
IST state to userspace-provided buffers and does not update guest memory.

IRS Restore Sequence:
---------------------

The following ordering must be followed when restoring the virtual GICv5 and
IRS:

a) Create vCPUs.
b) Provide the IRS base address by issuing KVM_SET_DEVICE_ATTR on
   KVM_DEV_ARM_VGIC_GRP_ADDR
c) Restore the number of SPIs by issuing KVM_SET_DEVICE_ATTR on
   KVM_DEV_ARM_VGIC_GRP_NR_IRQS.
d) Initialise the GIC - this sets up the default state and creates the SPI
   IST - by issuing KVM_SET_DEVICE_ATTR on KVM_DEV_ARM_VGIC_GRP_CTRL with
   KVM_DEV_ARM_VGIC_CTRL_INIT
e) Restore the IRS MMIO register state by issuing KVM_SET_DEVICE_ATTR on
   KVM_DEV_ARM_VGIC_GRP_IRS_REGS. The IRS registers may be restored in any
   order, but any IRS_IDR* state and the IRS_IST_CFGR/IRS_IST_BASER state
   describing an LPI IST must be restored before restoring the ISTs. KVM uses
   the restored IRS_IST_CFGR and IRS_IST_BASER state to allocate the LPI IST
   during the following step.
f) Restore the ISTs by issuing KVM_SET_DEVICE_ATTR on
   KVM_DEV_ARM_VGIC_GRP_IST.

The number of SPIs must be restored before VGIC initialization because
initialization allocates the SPI state and fixes the SPI range exposed by the
IRS ID registers.

The IRS's MMIO registers must be restored prior to restoring the ISTs as these
are used to convey the number of LPIs the guest has configured to KVM.

The various ``*_STATUSR`` registers are observational state in the current KVM
implementation. Userspace may save them for validation or debugging purposes,
but they are not required as restore input and do not need to be replayed during
restore.

Then vCPUs can be started.
