=====================================
 drm/vc4 Broadcom VC4 Graphics Driver
=====================================

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_drv.c
   :doc: Broadcom VC4 Graphics Driver

Display Hardware Handling
=========================

This section covers everything related to the display hardware including
the mode setting infrastructure, plane, sprite and cursor handling and
display, output probing and related topics.

Pixel Valve (DRM CRTC)
----------------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_crtc.c
   :doc: VC4 CRTC module

HVS
---

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_hvs.c
   :doc: VC4 HVS module.

HVS planes
----------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_plane.c
   :doc: VC4 plane module

HDMI encoder
------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_hdmi.c
   :doc: VC4 Falcon HDMI module

DSI encoder
-----------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_dsi.c
   :doc: VC4 DSI0/DSI1 module

DPI encoder
-----------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_dpi.c
   :doc: VC4 DPI module

VEC (Composite TV out) encoder
------------------------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_vec.c
   :doc: VC4 SDTV module

Memory Management and 3D Command Submission
===========================================

This section covers the GEM implementation in the vc4 driver.

GPU buffer object (BO) management
---------------------------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_bo.c
   :doc: VC4 GEM BO management support

V3D binner command list (BCL) validation
----------------------------------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_validate.c
   :doc: Command list validator for VC4.

V3D render command list (RCL) generation
----------------------------------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_render_cl.c
   :doc: Render command list generation

Shader validator for VC4
---------------------------
.. kernel-doc:: drivers/gpu/drm/vc4/vc4_validate_shaders.c
   :doc: Shader validator for VC4.

V3D Interrupts
--------------

.. kernel-doc:: drivers/gpu/drm/vc4/vc4_irq.c
   :doc: Interrupt management for the V3D engine
