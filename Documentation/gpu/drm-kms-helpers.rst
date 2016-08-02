=============================
Mode Setting Helper Functions
=============================

The plane, CRTC, encoder and connector functions provided by the drivers
implement the DRM API. They're called by the DRM core and ioctl handlers
to handle device state changes and configuration request. As
implementing those functions often requires logic not specific to
drivers, mid-layer helper functions are available to avoid duplicating
boilerplate code.

The DRM core contains one mid-layer implementation. The mid-layer
provides implementations of several plane, CRTC, encoder and connector
functions (called from the top of the mid-layer) that pre-process
requests and call lower-level functions provided by the driver (at the
bottom of the mid-layer). For instance, the
:c:func:`drm_crtc_helper_set_config()` function can be used to
fill the :c:type:`struct drm_crtc_funcs <drm_crtc_funcs>`
set_config field. When called, it will split the set_config operation
in smaller, simpler operations and call the driver to handle them.

To use the mid-layer, drivers call
:c:func:`drm_crtc_helper_add()`,
:c:func:`drm_encoder_helper_add()` and
:c:func:`drm_connector_helper_add()` functions to install their
mid-layer bottom operations handlers, and fill the :c:type:`struct
drm_crtc_funcs <drm_crtc_funcs>`, :c:type:`struct
drm_encoder_funcs <drm_encoder_funcs>` and :c:type:`struct
drm_connector_funcs <drm_connector_funcs>` structures with
pointers to the mid-layer top API functions. Installing the mid-layer
bottom operation handlers is best done right after registering the
corresponding KMS object.

The mid-layer is not split between CRTC, encoder and connector
operations. To use it, a driver must provide bottom functions for all of
the three KMS entities.

Atomic Modeset Helper Functions Reference
=========================================

Overview
--------

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: overview

Implementing Asynchronous Atomic Commit
---------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: implementing nonblocking commit

Atomic State Reset and Initialization
-------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: atomic state reset and initialization

.. kernel-doc:: include/drm/drm_atomic_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :export:

Modeset Helper Reference for Common Vtables
===========================================

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :internal:

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :doc: overview

Legacy CRTC/Modeset Helper Functions Reference
==============================================

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :doc: overview

Output Probing Helper Functions Reference
=========================================

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :doc: output probing helper overview

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :export:

fbdev Helper Functions Reference
================================

.. kernel-doc:: drivers/gpu/drm/drm_fb_helper.c
   :doc: fbdev helpers

.. kernel-doc:: drivers/gpu/drm/drm_fb_helper.c
   :export:

.. kernel-doc:: include/drm/drm_fb_helper.h
   :internal:

Framebuffer CMA Helper Functions Reference
==========================================

.. kernel-doc:: drivers/gpu/drm/drm_fb_cma_helper.c
   :doc: framebuffer cma helper functions

.. kernel-doc:: drivers/gpu/drm/drm_fb_cma_helper.c
   :export:

Display Port Helper Functions Reference
=======================================

.. kernel-doc:: drivers/gpu/drm/drm_dp_helper.c
   :doc: dp helpers

.. kernel-doc:: include/drm/drm_dp_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_helper.c
   :export:

Display Port Dual Mode Adaptor Helper Functions Reference
=========================================================

.. kernel-doc:: drivers/gpu/drm/drm_dp_dual_mode_helper.c
   :doc: dp dual mode helpers

.. kernel-doc:: include/drm/drm_dp_dual_mode_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_dual_mode_helper.c
   :export:

Display Port MST Helper Functions Reference
===========================================

.. kernel-doc:: drivers/gpu/drm/drm_dp_mst_topology.c
   :doc: dp mst helper

.. kernel-doc:: include/drm/drm_dp_mst_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_mst_topology.c
   :export:

MIPI DSI Helper Functions Reference
===================================

.. kernel-doc:: drivers/gpu/drm/drm_mipi_dsi.c
   :doc: dsi helpers

.. kernel-doc:: include/drm/drm_mipi_dsi.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_mipi_dsi.c
   :export:

EDID Helper Functions Reference
===============================

.. kernel-doc:: drivers/gpu/drm/drm_edid.c
   :export:

Rectangle Utilities Reference
=============================

.. kernel-doc:: include/drm/drm_rect.h
   :doc: rect utils

.. kernel-doc:: include/drm/drm_rect.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_rect.c
   :export:

Flip-work Helper Reference
==========================

.. kernel-doc:: include/drm/drm_flip_work.h
   :doc: flip utils

.. kernel-doc:: include/drm/drm_flip_work.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_flip_work.c
   :export:

HDMI Infoframes Helper Reference
================================

Strictly speaking this is not a DRM helper library but generally useable
by any driver interfacing with HDMI outputs like v4l or alsa drivers.
But it nicely fits into the overall topic of mode setting helper
libraries and hence is also included here.

.. kernel-doc:: include/linux/hdmi.h
   :internal:

.. kernel-doc:: drivers/video/hdmi.c
   :export:

Plane Helper Reference
======================

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :doc: overview

Tile group
----------

.. kernel-doc:: drivers/gpu/drm/drm_crtc.c
   :doc: Tile group

Bridges
=======

Overview
--------

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :doc: overview

Default bridge callback sequence
--------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :doc: bridge callbacks

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :export:

Panel Helper Reference
======================

.. kernel-doc:: include/drm/drm_panel.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_panel.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_panel.c
   :doc: drm panel

Simple KMS Helper Reference
===========================

.. kernel-doc:: include/drm/drm_simple_kms_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :doc: overview
