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

Blend Mode Properties
=====================

Pixel blend mode is a DRM plane composition property of :c:type:`drm_plane` used to
describes how pixels from a foreground plane (fg) are composited with the
background plane (bg). Here, we present main concepts of DRM blend mode to help
to understand how this property is mapped to AMD DC interface. See more about
this DRM property and the alpha blending equations in :ref:`DRM Plane
Composition Properties <plane_composition_properties>`.

Basically, a blend mode sets the alpha blending equation for plane
composition that fits the mode in which the alpha channel affects the state of
pixel color values and, therefore, the resulted pixel color. For
example, consider the following elements of the alpha blending equation:

- *fg.rgb*: Each of the RGB component values from the foreground's pixel.
- *fg.alpha*: Alpha component value from the foreground's pixel.
- *bg.rgb*: Each of the RGB component values from the background.
- *plane_alpha*: Plane alpha value set by the **plane "alpha" property**, see
  more in :ref:`DRM Plane Composition Properties <plane_composition_properties>`.

in the basic alpha blending equation::

   out.rgb = alpha * fg.rgb + (1 - alpha) * bg.rgb

the alpha channel value of each pixel in a plane is ignored and only the plane
alpha affects the resulted pixel color values.

DRM has three blend mode to define the blend formula in the plane composition:

* **None**: Blend formula that ignores the pixel alpha.

* **Pre-multiplied**: Blend formula that assumes the pixel color values in a
  plane was already pre-multiplied by its own alpha channel before storage.

* **Coverage**: Blend formula that assumes the pixel color values were not
  pre-multiplied with the alpha channel values.

and pre-multiplied is the default pixel blend mode, that means, when no blend
mode property is created or defined, DRM considers the plane's pixels has
pre-multiplied color values. On IGT GPU tools, the kms_plane_alpha_blend test
provides a set of subtests to verify plane alpha and blend mode properties.

The DRM blend mode and its elements are then mapped by AMDGPU display manager
(DM) to program the blending configuration of the Multiple Pipe/Plane Combined
(MPC), as follows:

.. kernel-doc:: drivers/gpu/drm/amd/display/dc/inc/hw/mpc.h
   :doc: mpc-overview

.. kernel-doc:: drivers/gpu/drm/amd/display/dc/inc/hw/mpc.h
   :functions: mpcc_blnd_cfg

Therefore, the blending configuration for a single MPCC instance on the MPC
tree is defined by :c:type:`mpcc_blnd_cfg`, where
:c:type:`pre_multiplied_alpha` is the alpha pre-multiplied mode flag used to
set :c:type:`MPCC_ALPHA_MULTIPLIED_MODE`. It controls whether alpha is
multiplied (true/false), being only true for DRM pre-multiplied blend mode.
:c:type:`mpcc_alpha_blend_mode` defines the alpha blend mode regarding pixel
alpha and plane alpha values. It sets one of the three modes for
:c:type:`MPCC_ALPHA_BLND_MODE`, as described below.

.. kernel-doc:: drivers/gpu/drm/amd/display/dc/inc/hw/mpc.h
   :functions: mpcc_alpha_blend_mode

DM then maps the elements of `enum mpcc_alpha_blend_mode` to those in the DRM
blend formula, as follows:

* *MPC pixel alpha* matches *DRM fg.alpha* as the alpha component value
  from the plane's pixel
* *MPC global alpha* matches *DRM plane_alpha* when the pixel alpha should
  be ignored and, therefore, pixel values are not pre-multiplied
* *MPC global gain* assumes *MPC global alpha* value when both *DRM
  fg.alpha* and *DRM plane_alpha* participate in the blend equation

In short, *fg.alpha* is ignored by selecting
:c:type:`MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA`. On the other hand, (plane_alpha *
fg.alpha) component becomes available by selecting
:c:type:`MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN`. And the
:c:type:`MPCC_ALPHA_MULTIPLIED_MODE` defines if the pixel color values are
pre-multiplied by alpha or not.

Blend configuration flow
------------------------

The alpha blending equation is configured from DRM to DC interface by the
following path:

1. When updating a :c:type:`drm_plane_state <drm_plane_state>`, DM calls
   :c:type:`amdgpu_dm_plane_fill_blending_from_plane_state()` that maps
   :c:type:`drm_plane_state <drm_plane_state>` attributes to
   :c:type:`dc_plane_info <dc_plane_info>` struct to be handled in the
   OS-agnostic component (DC).

2. On DC interface, :c:type:`struct mpcc_blnd_cfg <mpcc_blnd_cfg>` programs the
   MPCC blend configuration considering the :c:type:`dc_plane_info
   <dc_plane_info>` input from DPP.
