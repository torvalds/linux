.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

============
Devlink Info
============

The ``devlink-info`` mechanism enables device drivers to report device
information in a generic fashion. It is extensible, and enables exporting
even device or driver specific information.

devlink supports representing the following types of versions

.. list-table:: List of version types
   :widths: 5 95

   * - Type
     - Description
   * - ``fixed``
     - Represents fixed versions, which cannot change. For example,
       component identifiers or the board version reported in the PCI VPD.
   * - ``running``
     - Represents the version of the currently running component. For
       example the running version of firmware. These versions generally
       only update after a reboot.
   * - ``stored``
     - Represents the version of a component as stored, such as after a
       flash update. Stored values should update to reflect changes in the
       flash even if a reboot has not yet occurred.

Generic Versions
================

It is expected that drivers use the following generic names for exporting
version information. Other information may be exposed using driver-specific
names, but these should be documented in the driver-specific file.

board.id
--------

Unique identifier of the board design.

board.rev
---------

Board design revision.

asic.id
-------

ASIC design identifier.

asic.rev
--------

ASIC design revision.

board.manufacture
-----------------

An identifier of the company or the facility which produced the part.

fw
--

Overall firmware version, often representing the collection of
fw.mgmt, fw.app, etc.

fw.mgmt
-------

Control unit firmware version. This firmware is responsible for house
keeping tasks, PHY control etc. but not the packet-by-packet data path
operation.

fw.app
------

Data path microcode controlling high-speed packet processing.

fw.undi
-------

UNDI software, may include the UEFI driver, firmware or both.

fw.ncsi
-------

Version of the software responsible for supporting/handling the
Network Controller Sideband Interface.

fw.psid
-------

Unique identifier of the firmware parameter set.

fw.roce
-------

RoCE firmware version which is responsible for handling roce
management.
