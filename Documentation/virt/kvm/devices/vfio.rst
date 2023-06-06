.. SPDX-License-Identifier: GPL-2.0

===================
VFIO virtual device
===================

Device types supported:

  - KVM_DEV_TYPE_VFIO

Only one VFIO instance may be created per VM.  The created device
tracks VFIO groups in use by the VM and features of those groups
important to the correctness and acceleration of the VM.  As groups
are enabled and disabled for use by the VM, KVM should be updated
about their presence.  When registered with KVM, a reference to the
VFIO-group is held by KVM.

Groups:
  KVM_DEV_VFIO_GROUP

KVM_DEV_VFIO_GROUP attributes:
  KVM_DEV_VFIO_GROUP_ADD: Add a VFIO group to VFIO-KVM device tracking
	kvm_device_attr.addr points to an int32_t file descriptor
	for the VFIO group.
  KVM_DEV_VFIO_GROUP_DEL: Remove a VFIO group from VFIO-KVM device tracking
	kvm_device_attr.addr points to an int32_t file descriptor
	for the VFIO group.
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

The GROUP_ADD operation above should be invoked prior to accessing the
device file descriptor via VFIO_GROUP_GET_DEVICE_FD in order to support
drivers which require a kvm pointer to be set in their .open_device()
callback.
