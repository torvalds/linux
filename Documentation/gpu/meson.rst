=============================================
drm/meson AmLogic Meson Video Processing Unit
=============================================

.. kernel-doc:: drivers/gpu/drm/meson/meson_drv.c
   :doc: Video Processing Unit

Video Processing Unit
=====================

The Amlogic Meson Display controller is composed of several components
that are going to be documented below:

.. code::

  DMC|---------------VPU (Video Processing Unit)----------------|------HHI------|
     | vd1   _______     _____________    _________________     |               |
  D  |-------|      |----|            |   |                |    |   HDMI PLL    |
  D  | vd2   | VIU  |    | Video Post |   | Video Encoders |<---|-----VCLK      |
  R  |-------|      |----| Processing |   |                |    |               |
     | osd2  |      |    |            |---| Enci ----------|----|-----VDAC------|
  R  |-------| CSC  |----| Scalers    |   | Encp ----------|----|----HDMI-TX----|
  A  | osd1  |      |    | Blenders   |   | Encl ----------|----|---------------|
  M  |-------|______|----|____________|   |________________|    |               |
  ___|__________________________________________________________|_______________|

Video Input Unit
================

.. kernel-doc:: drivers/gpu/drm/meson/meson_viu.c
   :doc: Video Input Unit

Video Post Processing
=====================

.. kernel-doc:: drivers/gpu/drm/meson/meson_vpp.c
   :doc: Video Post Processing

Video Encoder
=============

.. kernel-doc:: drivers/gpu/drm/meson/meson_venc.c
   :doc: Video Encoder

Video Canvas Management
=======================

.. kernel-doc:: drivers/gpu/drm/meson/meson_canvas.c
   :doc: Canvas

Video Clocks
============

.. kernel-doc:: drivers/gpu/drm/meson/meson_vclk.c
   :doc: Video Clocks

HDMI Video Output
=================

.. kernel-doc:: drivers/gpu/drm/meson/meson_dw_hdmi.c
   :doc: HDMI Output
