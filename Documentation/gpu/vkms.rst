.. _vkms:

==========================================
 drm/vkms Virtual Kernel Modesetting
==========================================

.. kernel-doc:: drivers/gpu/drm/vkms/vkms_drv.c
   :doc: vkms (Virtual Kernel Modesetting)

TODO
====

If you want to do any of the items listed below, please share your interest
with VKMS maintainers.

IGT better support
------------------

- Investigate: (1) test cases on kms_plane that are failing due to timeout on
  capturing CRC; (2) when running kms_flip test cases in sequence, some
  successful individual test cases are failing randomly.

- VKMS already has support for vblanks simulated via hrtimers, which can be
  tested with kms_flip test; in some way, we can say that VKMS already mimics
  the real hardware vblank. However, we also have virtual hardware that does
  not support vblank interrupt and completes page_flip events right away; in
  this case, compositor developers may end up creating a busy loop on virtual
  hardware. It would be useful to support Virtual Hardware behavior in VKMS
  because this can help compositor developers to test their features in
  multiple scenarios.

Add Plane Features
------------------

There's lots of plane features we could add support for:

- Real overlay planes, not just cursor.

- Full alpha blending on all planes.

- Rotation, scaling.

- Additional buffer formats, especially YUV formats for video like NV12.
  Low/high bpp RGB formats would also be interesting.

- Async updates (currently only possible on cursor plane using the legacy
  cursor api).

For all of these, we also want to review the igt test coverage and make sure
all relevant igt testcases work on vkms.

Prime Buffer Sharing
--------------------

- Syzbot report - WARNING in vkms_gem_free_object:
  https://syzkaller.appspot.com/bug?extid=e7ad70d406e74d8fc9d0

Runtime Configuration
---------------------

We want to be able to reconfigure vkms instance without having to reload the
module. Use/Test-cases:

- Hotplug/hotremove connectors on the fly (to be able to test DP MST handling
  of compositors).

- Configure planes/crtcs/connectors (we'd need some code to have more than 1 of
  them first).

- Change output configuration: Plug/unplug screens, change EDID, allow changing
  the refresh rate.

The currently proposed solution is to expose vkms configuration through
configfs.  All existing module options should be supported through configfs
too.

Writeback support
-----------------

- The writeback and CRC capture operations share the use of composer_enabled
  boolean to ensure vblanks. Probably, when these operations work together,
  composer_enabled needs to refcounting the composer state to proper work.

- Add support for cloned writeback outputs and related test cases using a
  cloned output in the IGT kms_writeback.

- As a v4l device. This is useful for debugging compositors on special vkms
  configurations, so that developers see what's really going on.

Output Features
---------------

- Variable refresh rate/freesync support. This probably needs prime buffer
  sharing support, so that we can use vgem fences to simulate rendering in
  testing. Also needs support to specify the EDID.

- Add support for link status, so that compositors can validate their runtime
  fallbacks when e.g. a Display Port link goes bad.

CRC API Improvements
--------------------

- Optimize CRC computation ``compute_crc()`` and plane blending ``blend()``

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
