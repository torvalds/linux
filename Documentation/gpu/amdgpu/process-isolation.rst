.. SPDX-License-Identifier: GPL-2.0

=========================
 AMDGPU Process Isolation
=========================

The AMDGPU driver includes a feature that enables automatic process isolation on the graphics engine. This feature serializes access to the graphics engine and adds a cleaner shader which clears the Local Data Store (LDS) and General Purpose Registers (GPRs) between jobs. All processes using the GPU, including both graphics and compute workloads, are serialized when this feature is enabled. On GPUs that support partitionable graphics engines, this feature can be enabled on a per-partition basis.

In addition, there is an interface to manually run the cleaner shader when the use of the GPU is complete. This may be preferable in some use cases, such as a single-user system where the login manager triggers the cleaner shader when the user logs out.

Process Isolation
=================

The `run_cleaner_shader` and `enforce_isolation` sysfs interfaces allow users to manually execute the cleaner shader and control the process isolation feature, respectively.

Partition Handling
------------------

The `enforce_isolation` file in sysfs can be used to enable process isolation and automatic shader cleanup between processes. On GPUs that support graphics engine partitioning, this can be enabled per partition. The partition and its current setting (0 disabled, 1 enabled) can be read from sysfs. On GPUs that do not support graphics engine partitioning, only a single partition will be present. Writing 1 to the partition position enables enforce isolation, writing 0 disables it.

Example of enabling enforce isolation on a GPU with multiple partitions:

.. code-block:: console

    $ echo 1 0 1 0 > /sys/class/drm/card0/device/enforce_isolation
    $ cat /sys/class/drm/card0/device/enforce_isolation
    1 0 1 0

The output indicates that enforce isolation is enabled on zeroth and second partition and disabled on first and third partition.

For devices with a single partition or those that do not support partitions, there will be only one element:

.. code-block:: console

    $ echo 1 > /sys/class/drm/card0/device/enforce_isolation
    $ cat /sys/class/drm/card0/device/enforce_isolation
    1

Cleaner Shader Execution
========================

The driver can trigger a cleaner shader to clean up the LDS and GPR state on the graphics engine. When process isolation is enabled, this happens automatically between processes. In addition, there is a sysfs file to manually trigger cleaner shader execution.

To manually trigger the execution of the cleaner shader, write `0` to the `run_cleaner_shader` sysfs file:

.. code-block:: console

    $ echo 0 > /sys/class/drm/card0/device/run_cleaner_shader

For multi-partition devices, you can specify the partition index when triggering the cleaner shader:

.. code-block:: console

    $ echo 0 > /sys/class/drm/card0/device/run_cleaner_shader # For partition 0
    $ echo 1 > /sys/class/drm/card0/device/run_cleaner_shader # For partition 1
    $ echo 2 > /sys/class/drm/card0/device/run_cleaner_shader # For partition 2
    # ... and so on for each partition

This command initiates the cleaner shader, which will run and complete before any new tasks are scheduled on the GPU.
