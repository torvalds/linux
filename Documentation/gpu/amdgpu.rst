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

AMDGPU XGMI Support
===================

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_xgmi.c
   :doc: AMDGPU XGMI Support

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_xgmi.c
   :internal:

AMDGPU RAS Support
==================

RAS debugfs/sysfs Control and Error Injection Interfaces
--------------------------------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_ras.c
   :doc: AMDGPU RAS debugfs control interface

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


GPU Power/Thermal Controls and Monitoring
=========================================

This section covers hwmon and power/thermal controls.

HWMON Interfaces
----------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: hwmon

GPU sysfs Power State Interfaces
--------------------------------

GPU power controls are exposed via sysfs files.

power_dpm_state
~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: power_dpm_state

power_dpm_force_performance_level
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: power_dpm_force_performance_level

pp_table
~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: pp_table

pp_od_clk_voltage
~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: pp_od_clk_voltage

pp_dpm_*
~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: pp_dpm_sclk pp_dpm_mclk pp_dpm_socclk pp_dpm_fclk pp_dpm_dcefclk pp_dpm_pcie

pp_power_profile_mode
~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: pp_power_profile_mode

busy_percent
~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_pm.c
   :doc: busy_percent
