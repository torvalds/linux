.. _vkms:

==========================================
 drm/vkms Virtual Kernel Modesetting
==========================================

.. kernel-doc:: drivers/gpu/drm/vkms/vkms_drv.c
   :doc: vkms (Virtual Kernel Modesetting)

TODO
====

CRC API Improvements
--------------------

- Optimize CRC computation ``compute_crc()`` and plane blending ``blend()``

- Use the alpha value to blend vaddr_src with vaddr_dst instead of
  overwriting it in ``blend()``.

- Add igt test to check cleared alpha value for XRGB plane format.

- Add igt test to check extreme alpha values i.e. fully opaque and fully
  transparent (intermediate values are affected by hw-specific rounding modes).

Vblank issues
-------------

Some IGT test cases are failing. Need to analyze why and fix the issues:

- plain-flip-fb-recreate
- plain-flip-ts-check
- flip-vs-blocking-wf-vblank
- plain-flip-fb-recreate-interruptible
- flip-vs-wf_vblank-interruptible

Runtime Configuration
---------------------

We want to be able to reconfigure vkms instance without having to reload the
module. Use/Test-cases:

- Hotplug/hotremove connectors on the fly (to be able to test DP MST handling of
  compositors).

- Configure planes/crtcs/connectors (we'd need some code to have more than 1 of
  them first).

- Change output configuration: Plug/unplug screens, change EDID, allow changing
  the refresh rate.

The currently proposed solution is to expose vkms configuration through
configfs.  All existing module options should be supported through configfs too.

Add Plane Features
------------------

There's lots of plane features we could add support for:

- Real overlay planes, not just cursor.

- Full alpha blending on all planes.

- Rotation, scaling.

- Additional buffer formats, especially YUV formats for video like NV12.
  Low/high bpp RGB formats would also be interesting.

- Async updates (currently only possible on cursor plane using the legacy cursor
  api).

For all of these, we also want to review the igt test coverage and make sure all
relevant igt testcases work on vkms.

Writeback support
-----------------

Currently vkms only computes a CRC for each frame. Once we have additional plane
features, we could write back the entire composited frame, and expose it as:

- Writeback connector. This is useful for testing compositors if you don't have
  hardware with writeback support.

- As a v4l device. This is useful for debugging compositors on special vkms
  configurations, so that developers see what's really going on.

Prime Buffer Sharing
--------------------

We already have vgem, which is a gem driver for testing rendering, similar to
how vkms is for testing the modeset side. Adding buffer sharing support to vkms
allows us to test them together, to test synchronization and lots of other
features. Also, this allows compositors to test whether they work correctly on
SoC chips, where the display and rendering is very often split between 2
drivers.

Output Features
---------------

- Variable refresh rate/freesync support. This probably needs prime buffer
  sharing support, so that we can use vgem fences to simulate rendering in
  testing. Also needs support to specify the EDID.

- Add support for link status, so that compositors can validate their runtime
  fallbacks when e.g. a Display Port link goes bad.

- All the hotplug handling describe under "Runtime Configuration".

Atomic Check using eBPF
-----------------------

Atomic drivers have lots of restrictions which are not exposed to userspace in
any explicit form through e.g. possible property values. Userspace can only
inquiry about these limits through the atomic IOCTL, possibly using the
TEST_ONLY flag. Trying to add configurable code for all these limits, to allow
compositors to be tested against them, would be rather futile exercise. Instead
we could add support for eBPF to validate any kind of atomic state, and
implement a library of different restrictions.

This needs a bunch of features (plane compositing, multiple outputs, ...)
enabled already to make sense.
