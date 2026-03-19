.. SPDX-License-Identifier: GPL-2.0

====================================================
ARM Virtual Generic Interrupt Controller v5 (VGICv5)
====================================================


Device types supported:
  - KVM_DEV_TYPE_ARM_VGIC_V5     ARM Generic Interrupt Controller v5.0

Only one VGIC instance may be instantiated through this API.  The created VGIC
will act as the VM interrupt controller, requiring emulated user-space devices
to inject interrupts to the VGIC instead of directly to CPUs.

Creating a guest GICv5 device requires a host GICv5 host.  The current VGICv5
device only supports PPI interrupts.  These can either be injected from emulated
in-kernel devices (such as the Arch Timer, or PMU), or via the KVM_IRQ_LINE
ioctl.

Groups:
  KVM_DEV_ARM_VGIC_GRP_CTRL
   Attributes:

    KVM_DEV_ARM_VGIC_CTRL_INIT
      request the initialization of the VGIC, no additional parameter in
      kvm_device_attr.addr. Must be called after all VCPUs have been created.

  Errors:

    =======  ========================================================
    -ENXIO   VGIC not properly configured as required prior to calling
             this attribute
    -ENODEV  no online VCPU
    -ENOMEM  memory shortage when allocating vgic internal data
    -EFAULT  Invalid guest ram access
    -EBUSY   One or more VCPUS are running
    =======  ========================================================
