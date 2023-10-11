.. SPDX-License-Identifier: GPL-2.0

===================
VFIO virtual device
===================

Device types supported:

  - KVM_DEV_TYPE_VFIO

Only one VFIO instance may be created per VM.  The created device
tracks VFIO files (group or device) in use by the VM and features
of those groups/devices important to the correctness and acceleration
of the VM.  As groups/devices are enabled and disabled for use by the
VM, KVM should be updated about their presence.  When registered with
KVM, a reference to the VFIO file is held by KVM.

Groups:
  KVM_DEV_VFIO_FILE
	alias: KVM_DEV_VFIO_GROUP

KVM_DEV_VFIO_FILE attributes:
  KVM_DEV_VFIO_FILE_ADD: Add a VFIO file (group/device) to VFIO-KVM device
	tracking

	kvm_device_attr.addr points to an int32_t file descriptor for the
	VFIO file.

  KVM_DEV_VFIO_FILE_DEL: Remove a VFIO file (group/device) from VFIO-KVM
	device tracking

	kvm_device_attr.addr points to an int32_t file descriptor for the
	VFIO file.

KVM_DEV_VFIO_GROUP (legacy kvm device group restricted to the handling of VFIO group fd):
  KVM_DEV_VFIO_GROUP_ADD: same as KVM_DEV_VFIO_FILE_ADD for group fd only

  KVM_DEV_VFIO_GROUP_DEL: same as KVM_DEV_VFIO_FILE_DEL for group fd only

  KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE: attaches a guest visible TCE table
	allocated by sPAPR KVM.
	kvm_device_attr.addr points to a struct::

		struct kvm_vfio_spapr_tce {
			__s32	groupfd;
			__s32	tablefd;
		};

	where:

	- @groupfd is a file descriptor for a VFIO group;
	- @tablefd is a file descriptor for a TCE table allocated via
	  KVM_CREATE_SPAPR_TCE.

The FILE/GROUP_ADD operation above should be invoked prior to accessing the
device file descriptor via VFIO_GROUP_GET_DEVICE_FD in order to support
drivers which require a kvm pointer to be set in their .open_device()
callback.  It is the same for device file descriptor via character device
open which gets device access via VFIO_DEVICE_BIND_IOMMUFD.  For such file
descriptors, FILE_ADD should be invoked before VFIO_DEVICE_BIND_IOMMUFD
to support the drivers mentioned in prior sentence as well.
