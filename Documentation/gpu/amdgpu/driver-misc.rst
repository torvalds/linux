================================
 Misc AMDGPU driver information
================================

GPU Product Information
=======================

Information about the GPU can be obtained on certain cards
via sysfs

product_name
------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_fru_eeprom.c
   :doc: product_name

product_number
--------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_fru_eeprom.c
   :doc: product_number

serial_number
-------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_fru_eeprom.c
   :doc: serial_number

fru_id
-------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_fru_eeprom.c
   :doc: fru_id

manufacturer
-------------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_fru_eeprom.c
   :doc: manufacturer

unique_id
---------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: unique_id

board_info
----------

.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_device.c
   :doc: board_info

Accelerated Processing Units (APU) Info
---------------------------------------

.. csv-table::
   :header-rows: 1
   :widths: 3, 2, 2, 1, 1, 1, 1
   :file: ./apu-asic-info-table.csv

Discrete GPU Info
-----------------

.. csv-table::
   :header-rows: 1
   :widths: 3, 2, 2, 1, 1, 1
   :file: ./dgpu-asic-info-table.csv


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

GPU SmartShift Information
==========================

GPU SmartShift information via sysfs

smartshift_apu_power
--------------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: smartshift_apu_power

smartshift_dgpu_power
---------------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: smartshift_dgpu_power

smartshift_bias
---------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: smartshift_bias
