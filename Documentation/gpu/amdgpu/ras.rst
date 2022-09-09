====================
 AMDGPU RAS Support
====================

The AMDGPU RAS interfaces are exposed via sysfs (for informational queries) and
debugfs (for error injection).

RAS debugfs/sysfs Control and Error Injection Interfaces
========================================================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS debugfs control interface

RAS Reboot Behavior for Unrecoverable Errors
============================================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS Reboot Behavior for Unrecoverable Errors

RAS Error Count sysfs Interface
===============================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS sysfs Error Count Interface

RAS EEPROM debugfs Interface
============================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS debugfs EEPROM table reset interface

RAS VRAM Bad Pages sysfs Interface
==================================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS sysfs gpu_vram_bad_pages Interface

Sample Code
===========
Sample code for testing error injection can be found here:
https://cgit.freedesktop.org/mesa/drm/tree/tests/amdgpu/ras_tests.c

This is part of the libdrm amdgpu unit tests which cover several areas of the GPU.
There are four sets of tests:

RAS Basic Test

The test verifies the RAS feature enabled status and makes sure the necessary sysfs and debugfs files
are present.

RAS Query Test

This test checks the RAS availability and enablement status for each supported IP block as well as
the error counts.

RAS Inject Test

This test injects errors for each IP.

RAS Disable Test

This test tests disabling of RAS features for each IP block.
