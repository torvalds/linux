===========================================
 GPU Power/Thermal Controls and Monitoring
===========================================

HWMON Interfaces
================

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: hwmon

GPU sysfs Power State Interfaces
================================

GPU power controls are exposed via sysfs files.

power_dpm_state
---------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: power_dpm_state

power_dpm_force_performance_level
---------------------------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: power_dpm_force_performance_level

pp_table
--------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_table

pp_od_clk_voltage
-----------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_od_clk_voltage

pp_dpm_*
--------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_dpm_sclk pp_dpm_mclk pp_dpm_socclk pp_dpm_fclk pp_dpm_dcefclk pp_dpm_pcie

pp_power_profile_mode
---------------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: pp_power_profile_mode

\*_busy_percent
---------------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: gpu_busy_percent

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: mem_busy_percent

gpu_metrics
-----------

.. kernel-doc:: drivers/gpu/drm/amd/pm/amdgpu_pm.c
   :doc: gpu_metrics
