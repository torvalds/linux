.. _amdgpu-mes:

=============================
 MicroEngine Scheduler (MES)
=============================

.. note::
   Queue and ring buffer are used as a synonymous.

.. note::
   This section assumes that you are familiar with the concept of Pipes, Queues, and GC.
   If not, check :ref:`GFX, Compute, and SDMA Overall Behavior<pipes-and-queues-description>`
   and :ref:`drm/amdgpu - Graphics and Compute (GC) <amdgpu-gc>`.

Every GFX has a pipe component with one or more hardware queues. Pipes can
switch between queues depending on certain conditions, and one of the
components that can request a queue switch to a pipe is the MicroEngine
Scheduler (MES). Whenever the driver is initialized, it creates one MQD per
hardware queue, and then the MQDs are handed to the MES firmware for mapping
to:

1. Kernel Queues (legacy): This queue is statically mapped to HQDs and never
   preempted. Even though this is a legacy feature, it is the current default, and
   most existing hardware supports it. When an application submits work to the
   kernel driver, it submits all of the application command buffers to the kernel
   queues. The CS IOCTL takes the command buffer from the applications and
   schedules them on the kernel queue.

2. User Queues: These queues are dynamically mapped to the HQDs. Regarding the
   utilization of User Queues, the userspace application will create its user
   queues and submit work directly to its user queues with no need to IOCTL for
   each submission and no need to share a single kernel queue.

In terms of User Queues, MES can dynamically map them to the HQD. If there are
more MQDs than HQDs, the MES firmware will preempt other user queues to make
sure each queues get a time slice; in other words, MES is a microcontroller
that handles the mapping and unmapping of MQDs into HQDs, as well as the
priorities and oversubscription of MQDs.
