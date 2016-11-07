=============================
Mode Setting Helper Functions
=============================

The DRM subsystem aims for a strong separation between core code and helper
libraries. Core code takes care of general setup and teardown and decoding
userspace requests to kernel internal objects. Everything else is handled by a
large set of helper libraries, which can be combined freely to pick and choose
for each driver what fits, and avoid shared code where special behaviour is
needed.

This distinction between core code and helpers is especially strong in the
modesetting code, where there's a shared userspace ABI for all drivers. This is
in contrast to the render side, where pretty much everything (with very few
exceptions) can be considered optional helper code.

There are a few areas these helpers can grouped into:

* Helpers to implement modesetting. The important ones here are the atomic
  helpers. Old drivers still often use the legacy CRTC helpers. They both share
  the same set of common helper vtables. For really simple drivers (anything
  that would have been a great fit in the deprecated fbdev subsystem) there's
  also the simple display pipe helpers.

* There's a big pile of helpers for handling outputs. First the generic bridge
  helpers for handling encoder and transcoder IP blocks. Second the panel helpers
  for handling panel-related information and logic. Plus then a big set of
  helpers for the various sink standards (DisplayPort, HDMI, MIPI DSI). Finally
  there's also generic helpers for handling output probing, and for dealing with
  EDIDs.

* The last group of helpers concerns itself with the frontend side of a display
  pipeline: Planes, handling rectangles for visibility checking and scissoring,
  flip queues and assorted bits.

Modeset Helper Reference for Common Vtables
===========================================

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :internal:

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :doc: overview

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

Legacy CRTC/Modeset Helper Functions Reference
==============================================

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :export:

Simple KMS Helper Reference
===========================

.. kernel-doc:: include/drm/drm_simple_kms_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :doc: overview

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


Bridge Helper Reference
-------------------------

.. kernel-doc:: include/drm/drm_bridge.h
   :internal:

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

Output Probing Helper Functions Reference
=========================================

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :doc: output probing helper overview

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :export:

EDID Helper Functions Reference
===============================

.. kernel-doc:: include/drm/drm_edid.h
   :internal:

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

Flip-work Helper Reference
==========================

.. kernel-doc:: include/drm/drm_flip_work.h
   :doc: flip utils

.. kernel-doc:: include/drm/drm_flip_work.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_flip_work.c
   :export:

Plane Helper Reference
======================

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :export:

Tile group
==========

# FIXME: This should probably be moved into a property documentation section

.. kernel-doc:: drivers/gpu/drm/drm_crtc.c
   :doc: Tile group

Auxiliary Modeset Helpers
=========================

.. kernel-doc:: drivers/gpu/drm/drm_modeset_helper.c
   :doc: aux kms helpers

.. kernel-doc:: drivers/gpu/drm/drm_modeset_helper.c
   :export:
