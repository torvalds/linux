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

PRIME Buffer Sharing
--------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_prime.c
   :doc: PRIME Buffer Sharing

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_prime.c
   :internal:
