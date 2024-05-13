.. SPDX-License-Identifier: GPL-2.0

======================
VFIO AP Locks Overview
======================
This document describes the locks that are pertinent to the secure operation
of the vfio_ap device driver. Throughout this document, the following variables
will be used to denote instances of the structures herein described:

.. code-block:: c

  struct ap_matrix_dev *matrix_dev;
  struct ap_matrix_mdev *matrix_mdev;
  struct kvm *kvm;

The Matrix Devices Lock (drivers/s390/crypto/vfio_ap_private.h)
---------------------------------------------------------------

.. code-block:: c

  struct ap_matrix_dev {
  	...
  	struct list_head mdev_list;
  	struct mutex mdevs_lock;
  	...
  }

The Matrix Devices Lock (matrix_dev->mdevs_lock) is implemented as a global
mutex contained within the single object of struct ap_matrix_dev. This lock
controls access to all fields contained within each matrix_mdev
(matrix_dev->mdev_list). This lock must be held while reading from, writing to
or using the data from a field contained within a matrix_mdev instance
representing one of the vfio_ap device driver's mediated devices.

The KVM Lock (include/linux/kvm_host.h)
---------------------------------------

.. code-block:: c

  struct kvm {
  	...
  	struct mutex lock;
  	...
  }

The KVM Lock (kvm->lock) controls access to the state data for a KVM guest. This
lock must be held by the vfio_ap device driver while one or more AP adapters,
domains or control domains are being plugged into or unplugged from the guest.

The KVM pointer is stored in the in the matrix_mdev instance
(matrix_mdev->kvm = kvm) containing the state of the mediated device that has
been attached to the KVM guest.

The Guests Lock (drivers/s390/crypto/vfio_ap_private.h)
-----------------------------------------------------------

.. code-block:: c

  struct ap_matrix_dev {
  	...
  	struct list_head mdev_list;
  	struct mutex guests_lock;
  	...
  }

The Guests Lock (matrix_dev->guests_lock) controls access to the
matrix_mdev instances (matrix_dev->mdev_list) that represent mediated devices
that hold the state for the mediated devices that have been attached to a
KVM guest. This lock must be held:

1. To control access to the KVM pointer (matrix_mdev->kvm) while the vfio_ap
   device driver is using it to plug/unplug AP devices passed through to the KVM
   guest.

2. To add matrix_mdev instances to or remove them from matrix_dev->mdev_list.
   This is necessary to ensure the proper locking order when the list is perused
   to find an ap_matrix_mdev instance for the purpose of plugging/unplugging
   AP devices passed through to a KVM guest.

   For example, when a queue device is removed from the vfio_ap device driver,
   if the adapter is passed through to a KVM guest, it will have to be
   unplugged. In order to figure out whether the adapter is passed through,
   the matrix_mdev object to which the queue is assigned will have to be
   found. The KVM pointer (matrix_mdev->kvm) can then be used to determine if
   the mediated device is passed through (matrix_mdev->kvm != NULL) and if so,
   to unplug the adapter.

It is not necessary to take the Guests Lock to access the KVM pointer if the
pointer is not used to plug/unplug devices passed through to the KVM guest;
however, in this case, the Matrix Devices Lock (matrix_dev->mdevs_lock) must be
held in order to access the KVM pointer since it is set and cleared under the
protection of the Matrix Devices Lock. A case in point is the function that
handles interception of the PQAP(AQIC) instruction sub-function. This handler
needs to access the KVM pointer only for the purposes of setting or clearing IRQ
resources, so only the matrix_dev->mdevs_lock needs to be held.

The PQAP Hook Lock (arch/s390/include/asm/kvm_host.h)
-----------------------------------------------------

.. code-block:: c

  typedef int (*crypto_hook)(struct kvm_vcpu *vcpu);

  struct kvm_s390_crypto {
  	...
  	struct rw_semaphore pqap_hook_rwsem;
  	crypto_hook *pqap_hook;
  	...
  };

The PQAP Hook Lock is a r/w semaphore that controls access to the function
pointer of the handler ``(*kvm->arch.crypto.pqap_hook)`` to invoke when the
PQAP(AQIC) instruction sub-function is intercepted by the host. The lock must be
held in write mode when pqap_hook value is set, and in read mode when the
pqap_hook function is called.
