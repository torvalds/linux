======================
AMDgpu Display Manager
======================

.. contents:: Table of Contents
    :depth: 3

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.h
   :internal:

Lifecycle
=========

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: DM Lifecycle

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: dm_hw_init dm_hw_fini

Interrupts
==========

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_irq.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_irq.c
   :internal:

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: register_hpd_handlers dm_crtc_high_irq dm_pflip_high_irq

Atomic Implementation
=====================

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: atomic

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: amdgpu_dm_atomic_check amdgpu_dm_atomic_commit_tail
