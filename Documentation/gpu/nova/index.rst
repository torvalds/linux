.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

=======================
nova NVIDIA GPU drivers
=======================

The nova driver project consists out of two separate drivers nova-core and
nova-drm and intends to supersede the nouveau driver for NVIDIA GPUs based on
the GPU System Processor (GSP).

The following documents apply to both nova-core and nova-drm.

.. toctree::
   :titlesonly:

   guidelines

nova-core
=========

The nova-core driver is the core driver for NVIDIA GPUs based on GSP. nova-core,
as the 1st level driver, provides an abstraction around the GPUs hard- and
firmware interfaces providing a common base for 2nd level drivers, such as the
vGPU manager VFIO driver and the nova-drm driver.

.. toctree::
   :titlesonly:

   core/guidelines
   core/todo
   core/vbios
   core/devinit
   core/fwsec
   core/falcon
