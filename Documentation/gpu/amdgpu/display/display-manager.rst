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

Color Management Properties
===========================

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_color.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_color.c
   :internal:


DC Color Capabilities between DCN generations
---------------------------------------------

DRM/KMS framework defines three CRTC color correction properties: degamma,
color transformation matrix (CTM) and gamma, and two properties for degamma and
gamma LUT sizes. AMD DC programs some of the color correction features
pre-blending but DRM/KMS has not per-plane color correction properties.

In general, the DRM CRTC color properties are programmed to DC, as follows:
CRTC gamma after blending, and CRTC degamma pre-blending. Although CTM is
programmed after blending, it is mapped to DPP hw blocks (pre-blending). Other
color caps available in the hw is not currently exposed by DRM interface and
are bypassed.

.. kernel-doc:: drivers/gpu/drm/amd/display/dc/dc.h
   :doc: color-management-caps

.. kernel-doc:: drivers/gpu/drm/amd/display/dc/dc.h
   :internal:

The color pipeline has undergone major changes between DCN hardware
generations. What's possible to do before and after blending depends on
hardware capabilities, as illustrated below by the DCN 2.0 and DCN 3.0 families
schemas.

**DCN 2.0 family color caps and mapping**

.. kernel-figure:: dcn2_cm_drm_current.svg

**DCN 3.0 family color caps and mapping**

.. kernel-figure:: dcn3_cm_drm_current.svg
