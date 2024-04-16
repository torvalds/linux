.. SPDX-License-Identifier: GPL-2.0

========================
Device Frequency Scaling
========================

Introduction
------------

This framework provides a standard kernel interface for Dynamic Voltage and
Frequency Switching on arbitrary devices.

It exposes controls for adjusting frequency through sysfs files which are
similar to the cpufreq subsystem.

Devices for which current usage can be measured can have their frequency
automatically adjusted by governors.

API
---

Device drivers need to initialize a :c:type:`devfreq_profile` and call the
:c:func:`devfreq_add_device` function to create a :c:type:`devfreq` instance.

.. kernel-doc:: include/linux/devfreq.h
.. kernel-doc:: include/linux/devfreq-event.h
.. kernel-doc:: drivers/devfreq/devfreq.c
        :export:
.. kernel-doc:: drivers/devfreq/devfreq-event.c
        :export:
