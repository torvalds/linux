.. SPDX-License-Identifier: GPL-2.0

========================
Devlink Shared Instances
========================

Overview
========

Shared devlink instances allow multiple physical functions (PFs) on the same
chip to share a devlink instance for chip-wide operations.

Multiple PFs may reside on the same physical chip, running a single firmware.
Some of the resources and configurations may be shared among these PFs. The
shared devlink instance provides an object to pin configuration knobs on.

There are two possible usage models:

1. The shared devlink instance is used alongside individual PF devlink
   instances, providing chip-wide configuration in addition to per-PF
   configuration.
2. The shared devlink instance is the only devlink instance, without
   per-PF instances.

It is up to the driver to decide which usage model to use.

The shared devlink instance is not backed by any struct *device*.

Implementation
==============

Architecture
------------

The implementation uses:

* **Chip identification**: PFs are grouped by chip using a driver-specific identifier
* **Shared instance management**: Global list of shared instances with reference counting

API Functions
-------------

The following functions are provided for managing shared devlink instances:

* ``devlink_shd_get()``: Get or create a shared devlink instance identified by a string ID
* ``devlink_shd_put()``: Release a reference on a shared devlink instance
* ``devlink_shd_get_priv()``: Get private data from shared devlink instance

Initialization Flow
-------------------

1. **PF calls shared devlink init** during driver probe
2. **Chip identification** using driver-specific method to determine device identity
3. **Get or create shared instance** using ``devlink_shd_get()``:

   * The function looks up existing instance by identifier
   * If none exists, creates new instance:
     - Allocates and registers devlink instance
     - Adds to global shared instances list
     - Increments reference count

4. **Set nested devlink instance** for the PF devlink instance using
   ``devl_nested_devlink_set()`` before registering the PF devlink instance

Cleanup Flow
------------

1. **Cleanup** when PF is removed
2. **Call** ``devlink_shd_put()`` to release reference (decrements reference count)
3. **Shared instance is automatically destroyed** when the last PF removes (reference count reaches zero)

Chip Identification
-------------------

PFs belonging to the same chip are identified using a driver-specific method.
The driver is free to choose any identifier that is suitable for determining
whether two PFs are part of the same device. Examples include:

* **PCI VPD serial numbers**: Extract from PCI VPD
* **Device tree properties**: Read chip identifier from device tree
* **Other hardware-specific identifiers**: Any unique identifier that groups PFs by chip

Locking
-------

A global mutex (``shd_mutex``) protects the shared instances list during registration/deregistration.

Similarly to other nested devlink instance relationships, devlink lock of
the shared instance should be always taken after the devlink lock of PF.

Reference Counting
------------------

Each shared devlink instance maintains a reference count (``refcount_t refcount``).
The reference count is incremented when ``devlink_shd_get()`` is called and decremented
when ``devlink_shd_put()`` is called. When the reference count reaches zero, the shared
instance is automatically destroyed.
