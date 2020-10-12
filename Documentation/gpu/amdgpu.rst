=========================
 drm/amdgpu AMDgpu driver
=========================

The drm/amdgpu driver supports all AMD Radeon GPUs based on the Graphics Core
Next (GCN) architecture.

Module Parameters
=================

The amdgpu driver supports the following module parameters:

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c

Core Driver Infrastructure
==========================

This section covers core driver infrastructure.

.. _amdgpu_memory_domains:

Memory Domains
--------------

.. kernel-doc:: include/uapi/drm/amdgpu_drm.h
   :doc: memory domains

Buffer Objects
--------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_object.c
   :doc: amdgpu_object

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_object.c
   :internal:

PRIME Buffer Sharing
--------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_dma_buf.c
   :doc: PRIME Buffer Sharing

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_dma_buf.c
   :internal:

MMU Notifier
------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_mn.c
   :doc: MMU Notifier

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_mn.c
   :internal:

AMDGPU Virtual Memory
---------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c
   :doc: GPUVM

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c
   :internal:

Interrupt Handling
------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_irq.c
   :doc: Interrupt Handling

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_irq.c
   :internal:

IP Blocks
------------------

.. kernel-doc:: drivers/gpu/drm/amd/include/amd_shared.h
   :doc: IP Blocks

.. kernel-doc:: drivers/gpu/drm/amd/include/amd_shared.h
   :identifiers: amd_ip_block_type amd_ip_funcs

AMDGPU XGMI Support
===================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_xgmi.c
   :doc: AMDGPU XGMI Support

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_xgmi.c
   :internal:

AMDGPU RAS Support
==================

The AMDGPU RAS interfaces are exposed via sysfs (for informational queries) and
debugfs (for error injection).

RAS debugfs/sysfs Control and Error Injection Interfaces
--------------------------------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS debugfs control interface

RAS Reboot Behavior for Unrecoverable Errors
--------------------------------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS Reboot Behavior for Unrecoverable Errors

RAS Error Count sysfs Interface
-------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS sysfs Error Count Interface

RAS EEPROM debugfs Interface
----------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS debugfs EEPROM table reset interface

RAS VRAM Bad Pages sysfs Interface
----------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS sysfs gpu_vram_bad_pages Interface

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :internal:

Sample Code
-----------
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


GPU Power/Thermal Controls and Monitoring
=========================================

This section covers hwmon and power/thermal controls.

HWMON Interfaces
----------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: hwmon

GPU sysfs Power State Interfaces
--------------------------------

GPU power controls are exposed via sysfs files.

power_dpm_state
~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: power_dpm_state

power_dpm_force_performance_level
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: power_dpm_force_performance_level

pp_table
~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_table

pp_od_clk_voltage
~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_od_clk_voltage

pp_dpm_*
~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_dpm_sclk pp_dpm_mclk pp_dpm_socclk pp_dpm_fclk pp_dpm_dcefclk pp_dpm_pcie

pp_power_profile_mode
~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_power_profile_mode

*_busy_percent
~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: gpu_busy_percent

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: mem_busy_percent

gpu_metrics
~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: gpu_metrics

GPU Product Information
=======================

Information about the GPU can be obtained on certain cards
via sysfs

product_name
------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_device.c
   :doc: product_name

product_number
--------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_device.c
   :doc: product_name

serial_number
-------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_device.c
   :doc: serial_number

unique_id
---------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: unique_id

GPU Memory Usage Information
============================

Various memory accounting can be accessed via sysfs

mem_info_vram_total
-------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c
   :doc: mem_info_vram_total

mem_info_vram_used
------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c
   :doc: mem_info_vram_used

mem_info_vis_vram_total
-----------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c
   :doc: mem_info_vis_vram_total

mem_info_vis_vram_used
----------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_vram_mgr.c
   :doc: mem_info_vis_vram_used

mem_info_gtt_total
------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_gtt_mgr.c
   :doc: mem_info_gtt_total

mem_info_gtt_used
-----------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_gtt_mgr.c
   :doc: mem_info_gtt_used

PCIe Accounting Information
===========================

pcie_bw
-------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pcie_bw

pcie_replay_count
-----------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_device.c
   :doc: pcie_replay_count


