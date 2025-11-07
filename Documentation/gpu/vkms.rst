.. _vkms:

==========================================
 drm/vkms Virtual Kernel Modesetting
==========================================

.. kernel-doc:: drivers/gpu/drm/vkms/vkms_drv.c
   :doc: vkms (Virtual Kernel Modesetting)

Setup
=====

The VKMS driver can be setup with the following steps:

To check if VKMS is loaded, run::

  lsmod | grep vkms

This should list the VKMS driver. If no output is obtained, then
you need to enable and/or load the VKMS driver.
Ensure that the VKMS driver has been set as a loadable module in your
kernel config file. Do::

  make nconfig

  Go to `Device Drivers> Graphics support`

  Enable `Virtual KMS (EXPERIMENTAL)`

Compile and build the kernel for the changes to get reflected.
Now, to load the driver, use::

  sudo modprobe vkms

On running the lsmod command now, the VKMS driver will appear listed.
You can also observe the driver being loaded in the dmesg logs.

The VKMS driver has optional features to simulate different kinds of hardware,
which are exposed as module options. You can use the `modinfo` command
to see the module options for vkms::

  modinfo vkms

Module options are helpful when testing, and enabling modules
can be done while loading vkms. For example, to load vkms with cursor enabled,
use::

  sudo modprobe vkms enable_cursor=1

To disable the driver, use ::

  sudo modprobe -r vkms

Configuring With Configfs
=========================

It is possible to create and configure multiple VKMS instances via configfs.

Start by mounting configfs and loading VKMS::

  sudo mount -t configfs none /config
  sudo modprobe vkms

Once VKMS is loaded, ``/config/vkms`` is created automatically. Each directory
under ``/config/vkms`` represents a VKMS instance, create a new one::

  sudo mkdir /config/vkms/my-vkms

By default, the instance is disabled::

  cat /config/vkms/my-vkms/enabled
  0

And directories are created for each configurable item of the display pipeline::

  tree /config/vkms/my-vkms
  ├── connectors
  ├── crtcs
  ├── enabled
  ├── encoders
  └── planes

To add items to the display pipeline, create one or more directories under the
available paths.

Start by creating one or more planes::

  sudo mkdir /config/vkms/my-vkms/planes/plane0

Planes have 1 configurable attribute:

- type: Plane type: 0 overlay, 1 primary, 2 cursor (same values as those
  exposed by the "type" property of a plane)

Continue by creating one or more CRTCs::

  sudo mkdir /config/vkms/my-vkms/crtcs/crtc0

CRTCs have 1 configurable attribute:

- writeback: Enable or disable writeback connector support by writing 1 or 0

Next, create one or more encoders::

  sudo mkdir /config/vkms/my-vkms/encoders/encoder0

Last but not least, create one or more connectors::

  sudo mkdir /config/vkms/my-vkms/connectors/connector0

Connectors have 1 configurable attribute:

- status: Connection status: 1 connected, 2 disconnected, 3 unknown (same values
  as those exposed by the "status" property of a connector)

To finish the configuration, link the different pipeline items::

  sudo ln -s /config/vkms/my-vkms/crtcs/crtc0 /config/vkms/my-vkms/planes/plane0/possible_crtcs
  sudo ln -s /config/vkms/my-vkms/crtcs/crtc0 /config/vkms/my-vkms/encoders/encoder0/possible_crtcs
  sudo ln -s /config/vkms/my-vkms/encoders/encoder0 /config/vkms/my-vkms/connectors/connector0/possible_encoders

Since at least one primary plane is required, make sure to set the right type::

  echo "1" | sudo tee /config/vkms/my-vkms/planes/plane0/type

Once you are done configuring the VKMS instance, enable it::

  echo "1" | sudo tee /config/vkms/my-vkms/enabled

Finally, you can remove the VKMS instance disabling it::

  echo "0" | sudo tee /config/vkms/my-vkms/enabled

And removing the top level directory and its subdirectories::

  sudo rm /config/vkms/my-vkms/planes/*/possible_crtcs/*
  sudo rm /config/vkms/my-vkms/encoders/*/possible_crtcs/*
  sudo rm /config/vkms/my-vkms/connectors/*/possible_encoders/*
  sudo rmdir /config/vkms/my-vkms/planes/*
  sudo rmdir /config/vkms/my-vkms/crtcs/*
  sudo rmdir /config/vkms/my-vkms/encoders/*
  sudo rmdir /config/vkms/my-vkms/connectors/*
  sudo rmdir /config/vkms/my-vkms

Testing With IGT
================

The IGT GPU Tools is a test suite used specifically for debugging and
development of the DRM drivers.
The IGT Tools can be installed from
`here <https://gitlab.freedesktop.org/drm/igt-gpu-tools>`_ .

The tests need to be run without a compositor, so you need to switch to text
only mode. You can do this by::

  sudo systemctl isolate multi-user.target

To return to graphical mode, do::

  sudo systemctl isolate graphical.target

Once you are in text only mode, you can run tests using the IGT_FORCE_DRIVER
variable to specify the device filter for the driver we want to test.
IGT_FORCE_DRIVER can also be used with the run-tests.sh script to run the
tests for a specific driver::

  sudo IGT_FORCE_DRIVER="vkms" ./build/tests/<name of test>
  sudo IGT_FORCE_DRIVER="vkms" ./scripts/run-tests.sh -t <name of test>

For example, to test the functionality of the writeback library,
we can run the kms_writeback test::

  sudo IGT_FORCE_DRIVER="vkms" ./build/tests/kms_writeback
  sudo IGT_FORCE_DRIVER="vkms" ./scripts/run-tests.sh -t kms_writeback

You can also run subtests if you do not want to run the entire test::

  sudo IGT_FORCE_DRIVER="vkms" ./build/tests/kms_flip --run-subtest basic-plain-flip

Testing With KUnit
==================

KUnit (Kernel unit testing framework) provides a common framework for unit tests
within the Linux kernel.
More information in ../dev-tools/kunit/index.rst .

To run the VKMS KUnit tests::

  tools/testing/kunit/kunit.py run --kunitconfig=drivers/gpu/drm/vkms/tests

TODO
====

If you want to do any of the items listed below, please share your interest
with VKMS maintainers.

IGT better support
------------------

Debugging:

- kms_plane: some test cases are failing due to timeout on capturing CRC;

Virtual hardware (vblank-less) mode:

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

- Add background color KMS property[Good to get started].

- Scaling.

- Additional buffer formats. Low/high bpp RGB formats would be interesting
  [Good to get started].

- Async updates (currently only possible on cursor plane using the legacy
  cursor api).

For all of these, we also want to review the igt test coverage and make sure
all relevant igt testcases work on vkms. They are good options for internship
project.

Runtime Configuration
---------------------

We want to be able to reconfigure vkms instance without having to reload the
module through configfs. Use/Test-cases:

- Hotplug/hotremove connectors on the fly (to be able to test DP MST handling
  of compositors).

- Change output configuration: Plug/unplug screens, change EDID, allow changing
  the refresh rate.

Writeback support
-----------------

- The writeback and CRC capture operations share the use of composer_enabled
  boolean to ensure vblanks. Probably, when these operations work together,
  composer_enabled needs to refcounting the composer state to proper work.
  [Good to get started]

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
