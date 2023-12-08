.. SPDX-License-Identifier: GPL-2.0+

===============================
Client Driver API Documentation
===============================

.. contents::
    :depth: 2


Serial Hub Communication
========================

.. kernel-doc:: include/linux/surface_aggregator/serial_hub.h

.. kernel-doc:: drivers/platform/surface/aggregator/ssh_packet_layer.c
    :export:


Controller and Core Interface
=============================

.. kernel-doc:: include/linux/surface_aggregator/controller.h

.. kernel-doc:: drivers/platform/surface/aggregator/controller.c
    :export:

.. kernel-doc:: drivers/platform/surface/aggregator/core.c
    :export:


Client Bus and Client Device API
================================

.. kernel-doc:: include/linux/surface_aggregator/device.h

.. kernel-doc:: drivers/platform/surface/aggregator/bus.c
    :export:
