.. SPDX-License-Identifier: GPL-2.0-only

==============================
WMI embedded Binary MOF driver
==============================

Introduction
============

Many machines embed WMI Binary MOF (Managed Object Format) metadata used to
describe the details of their ACPI WMI interfaces. The data can be decoded
with tools like `bmfdec <https://github.com/pali/bmfdec>`_ to obtain a
human readable WMI interface description, which is useful for developing
new WMI drivers.

The Binary MOF data can be retrieved from the ``bmof`` sysfs attribute of the
associated WMI device. Please note that multiple WMI devices containing Binary
MOF data can exist on a given system.

WMI interface
=============

The Binary MOF WMI device is identified by the WMI GUID ``05901221-D566-11D1-B2F0-00A0C9062910``.
The Binary MOF can be obtained by doing a WMI data block query. The result is
then returned as an ACPI buffer with a variable size.
