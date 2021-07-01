===================================
drm/amd/display - Display Core (DC)
===================================

*placeholder - general description of supported platforms, what dc is, etc.*

Because it is partially shared with other operating systems, the Display Core
Driver is divided in two pieces.

1. **Display Core (DC)** contains the OS-agnostic components. Things like
   hardware programming and resource management are handled here.
2. **Display Manager (DM)** contains the OS-dependent components. Hooks to the
   amdgpu base driver and DRM are implemented here.

It doesn't help that the entire package is frequently referred to as DC. But
with the context in mind, it should be clear.

When CONFIG_DRM_AMD_DC is enabled, DC will be initialized by default for
supported ASICs. To force disable, set `amdgpu.dc=0` on kernel command line.
Likewise, to force enable on unsupported ASICs, set `amdgpu.dc=1`.

To determine if DC is loaded, search dmesg for the following entry:

``Display Core initialized with <version number here>``

AMDgpu Display Manager
======================

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.h
   :internal:

Lifecycle
---------

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: DM Lifecycle

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: dm_hw_init dm_hw_fini

Interrupts
----------

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_irq.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_irq.c
   :internal:

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: register_hpd_handlers dm_crtc_high_irq dm_pflip_high_irq

Atomic Implementation
---------------------

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: atomic

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :functions: amdgpu_dm_atomic_check amdgpu_dm_atomic_commit_tail

Display Core
============

**WIP**

FreeSync Video
--------------

.. kernel-doc:: drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c
   :doc: FreeSync Video
