=========================
 drm/amdgpu AMDgpu driver
=========================

The drm/amdgpu driver supports all AMD Radeon GPUs based on the Graphics Core
Next (GCN) architecture.

Core Driver Infrastructure
==========================

This section covers core driver infrastructure.

.. _amdgpu_memory_domains:

Memory Domains
--------------

.. kernel-doc:: include/uapi/drm/amdgpu_drm.h
   :doc: memory domains

Buffer Objects
--------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_object.c
   :doc: amdgpu_object

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_object.c
   :internal:

PRIME Buffer Sharing
--------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_prime.c
   :doc: PRIME Buffer Sharing

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_prime.c
   :internal:

MMU Notifier
------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_mn.c
   :doc: MMU Notifier

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_mn.c
   :internal:

AMDGPU Virtual Memory
---------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c
   :doc: GPUVM

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c
   :internal:
