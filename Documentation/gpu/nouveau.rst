.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

===============================
 drm/nouveau NVIDIA GPU Driver
===============================

The drm/nouveau driver provides support for a wide range of NVIDIA GPUs,
covering GeForce, Quadro, and Tesla series, from the NV04 architecture up
to the latest Turing, Ampere, Ada families.

NVKM: NVIDIA Kernel Manager
===========================

The NVKM component serves as the core abstraction layer within the nouveau
driver, responsible for managing NVIDIA GPU hardware at the kernel level.
NVKM provides a unified interface for handling various GPU  architectures.

It enables resource management, power control, memory handling, and command
submission required for the proper functioning of NVIDIA GPUs under the
nouveau driver.

NVKM plays a critical role in abstracting hardware complexities and
providing a consistent API to upper layers of the driver stack.

GSP Support
------------------------

.. kernel-doc:: drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/rpc.c
   :doc: GSP message queue element

.. kernel-doc:: drivers/gpu/drm/nouveau/include/nvkm/subdev/gsp.h
   :doc: GSP message handling policy
