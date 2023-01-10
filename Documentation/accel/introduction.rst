.. SPDX-License-Identifier: GPL-2.0

============
Introduction
============

The Linux compute accelerators subsystem is designed to expose compute
accelerators in a common way to user-space and provide a common set of
functionality.

These devices can be either stand-alone ASICs or IP blocks inside an SoC/GPU.
Although these devices are typically designed to accelerate
Machine-Learning (ML) and/or Deep-Learning (DL) computations, the accel layer
is not limited to handling these types of accelerators.

Typically, a compute accelerator will belong to one of the following
categories:

- Edge AI - doing inference at an edge device. It can be an embedded ASIC/FPGA,
  or an IP inside a SoC (e.g. laptop web camera). These devices
  are typically configured using registers and can work with or without DMA.

- Inference data-center - single/multi user devices in a large server. This
  type of device can be stand-alone or an IP inside a SoC or a GPU. It will
  have on-board DRAM (to hold the DL topology), DMA engines and
  command submission queues (either kernel or user-space queues).
  It might also have an MMU to manage multiple users and might also enable
  virtualization (SR-IOV) to support multiple VMs on the same device. In
  addition, these devices will usually have some tools, such as profiler and
  debugger.

- Training data-center - Similar to Inference data-center cards, but typically
  have more computational power and memory b/w (e.g. HBM) and will likely have
  a method of scaling-up/out, i.e. connecting to other training cards inside
  the server or in other servers, respectively.

All these devices typically have different runtime user-space software stacks,
that are tailored-made to their h/w. In addition, they will also probably
include a compiler to generate programs to their custom-made computational
engines. Typically, the common layer in user-space will be the DL frameworks,
such as PyTorch and TensorFlow.

Sharing code with DRM
=====================

Because this type of devices can be an IP inside GPUs or have similar
characteristics as those of GPUs, the accel subsystem will use the
DRM subsystem's code and functionality. i.e. the accel core code will
be part of the DRM subsystem and an accel device will be a new type of DRM
device.

This will allow us to leverage the extensive DRM code-base and
collaborate with DRM developers that have experience with this type of
devices. In addition, new features that will be added for the accelerator
drivers can be of use to GPU drivers as well.

Differentiation from GPUs
=========================

Because we want to prevent the extensive user-space graphic software stack
from trying to use an accelerator as a GPU, the compute accelerators will be
differentiated from GPUs by using a new major number and new device char files.

Furthermore, the drivers will be located in a separate place in the kernel
tree - drivers/accel/.

The accelerator devices will be exposed to the user space with the dedicated
261 major number and will have the following convention:

- device char files - /dev/accel/accel*
- sysfs             - /sys/class/accel/accel*/
- debugfs           - /sys/kernel/debug/accel/accel*/

Getting Started
===============

First, read the DRM documentation at Documentation/gpu/index.rst.
Not only it will explain how to write a new DRM driver but it will also
contain all the information on how to contribute, the Code Of Conduct and
what is the coding style/documentation. All of that is the same for the
accel subsystem.

Second, make sure the kernel is configured with CONFIG_DRM_ACCEL.

To expose your device as an accelerator, two changes are needed to
be done in your driver (as opposed to a standard DRM driver):

- Add the DRIVER_COMPUTE_ACCEL feature flag in your drm_driver's
  driver_features field. It is important to note that this driver feature is
  mutually exclusive with DRIVER_RENDER and DRIVER_MODESET. Devices that want
  to expose both graphics and compute device char files should be handled by
  two drivers that are connected using the auxiliary bus framework.

- Change the open callback in your driver fops structure to accel_open().
  Alternatively, your driver can use DEFINE_DRM_ACCEL_FOPS macro to easily
  set the correct function operations pointers structure.

External References
===================

email threads
-------------

* `Initial discussion on the New subsystem for acceleration devices <https://lkml.org/lkml/2022/7/31/83>`_ - Oded Gabbay (2022)
* `patch-set to add the new subsystem <https://lkml.org/lkml/2022/10/22/544>`_ - Oded Gabbay (2022)

Conference talks
----------------

* `LPC 2022 Accelerators BOF outcomes summary <https://airlied.blogspot.com/2022/09/accelerators-bof-outcomes-summary.html>`_ - Dave Airlie (2022)
