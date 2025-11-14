.. SPDX-License-Identifier: GPL-2.0

=========================================
Rockchip Camera Interface (CIF)
=========================================

Introduction
============

The Rockchip Camera Interface (CIF) is featured in many Rockchip SoCs in
different variants.
The different variants are combinations of common building blocks, such as

* INTERFACE blocks of different types, namely

  * the Digital Video Port (DVP, a parallel data interface)
  * the interface block for the MIPI CSI-2 receiver

* CROP units

* MIPI CSI-2 receiver (not available on all variants): This unit is referred
  to as MIPI CSI HOST in the Rockchip documentation.
  Technically, it is a separate hardware block, but it is strongly coupled to
  the CIF and therefore included here.

* MUX units (not available on all variants) that pass the video data to an
  image signal processor (ISP)

* SCALE units (not available on all variants)

* DMA engines that transfer video data into system memory using a
  double-buffering mechanism called ping-pong mode

* Support for four streams per INTERFACE block (not available on all
  variants), e.g., for MIPI CSI-2 Virtual Channels (VCs)

This document describes the different variants of the CIF, their hardware
layout, as well as their representation in the media controller centric rkcif
device driver, which is located under drivers/media/platform/rockchip/rkcif.

Variants
========

Rockchip PX30 Video Input Processor (VIP)
-----------------------------------------

The PX30 Video Input Processor (VIP) features a digital video port that accepts
parallel video data or BT.656.
Since these protocols do not feature multiple streams, the VIP has one DMA
engine that transfers the input video data into system memory.

The rkcif driver represents this hardware variant by exposing one V4L2 subdevice
(the DVP INTERFACE/CROP block) and one V4L2 device (the DVP DMA engine).

Rockchip RK3568 Video Capture (VICAP)
-------------------------------------

The RK3568 Video Capture (VICAP) unit features a digital video port and a MIPI
CSI-2 receiver that can receive video data independently.
The DVP accepts parallel video data, BT.656 and BT.1120.
Since the BT.1120 protocol may feature more than one stream, the RK3568 VICAP
DVP features four DMA engines that can capture different streams.
Similarly, the RK3568 VICAP MIPI CSI-2 receiver features four DMA engines to
handle different Virtual Channels (VCs).

The rkcif driver represents this hardware variant by exposing up the following
V4L2 subdevices:

* rkcif-dvp0: INTERFACE/CROP block for the DVP

and the following video devices:

* rkcif-dvp0-id0: The support for multiple streams on the DVP is not yet
  implemented, as it is hard to find test hardware. Thus, this video device
  represents the first DMA engine of the RK3568 DVP.

.. kernel-figure:: rkcif-rk3568-vicap.dot
    :alt:   Topology of the RK3568 Video Capture (VICAP) unit
    :align: center
