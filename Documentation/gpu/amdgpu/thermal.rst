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

GFXOFF
======

GFXOFF is a feature found in most recent GPUs that saves power at runtime. The
card's RLC (RunList Controller) firmware powers off the gfx engine
dynamically when there is no workload on gfx or compute pipes. GFXOFF is on by
default on supported GPUs.

Userspace can interact with GFXOFF through a debugfs interface (all values in
`uint32_t`, unless otherwise noted):

``amdgpu_gfxoff``
-----------------

Use it to enable/disable GFXOFF, and to check if it's current enabled/disabled::

  $ xxd -l1 -p /sys/kernel/debug/dri/0/amdgpu_gfxoff
  01

- Write 0 to disable it, and 1 to enable it.
- Read 0 means it's disabled, 1 it's enabled.

If it's enabled, that means that the GPU is free to enter into GFXOFF mode as
needed. Disabled means that it will never enter GFXOFF mode.

``amdgpu_gfxoff_status``
------------------------

Read it to check current GFXOFF's status of a GPU::

  $ xxd -l1 -p /sys/kernel/debug/dri/0/amdgpu_gfxoff_status
  02

- 0: GPU is in GFXOFF state, the gfx engine is powered down.
- 1: Transition out of GFXOFF state
- 2: Not in GFXOFF state
- 3: Transition into GFXOFF state

If GFXOFF is enabled, the value will be transitioning around [0, 3], always
getting into 0 when possible. When it's disabled, it's always at 2. Returns
``-EINVAL`` if it's not supported.

``amdgpu_gfxoff_count``
-----------------------

Read it to get the total GFXOFF entry count at the time of query since system
power-up. The value is an `uint64_t` type, however, due to firmware limitations,
it can currently overflow as an `uint32_t`. *Only supported in vangogh*

``amdgpu_gfxoff_residency``
---------------------------

Write 1 to amdgpu_gfxoff_residency to start logging, and 0 to stop. Read it to
get average GFXOFF residency % multiplied by 100 during the last logging
interval. E.g. a value of 7854 means 78.54% of the time in the last logging
interval the GPU was in GFXOFF mode. *Only supported in vangogh*
