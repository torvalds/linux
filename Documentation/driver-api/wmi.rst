.. SPDX-License-Identifier: GPL-2.0-or-later

==============
WMI Driver API
==============

The WMI driver core supports a more modern bus-based interface for interacting
with WMI devices, and an older GUID-based interface. The latter interface is
considered to be deprecated, so new WMI drivers should generally avoid it since
it has some issues with multiple WMI devices sharing the same GUID.
The modern bus-based interface instead maps each WMI device to a
:c:type:`struct wmi_device <wmi_device>`, so it supports WMI devices sharing the
same GUID. Drivers can then register a :c:type:`struct wmi_driver <wmi_driver>`
which will be bound to compatible WMI devices by the driver core.

.. kernel-doc:: include/linux/wmi.h
   :internal:

.. kernel-doc:: drivers/platform/x86/wmi.c
   :export:
