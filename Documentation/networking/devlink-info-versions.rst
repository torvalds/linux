.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

=====================
Devlink info versions
=====================

board.id
========

Unique identifier of the board design.

board.rev
=========

Board design revision.

asic.id
=======

ASIC design identifier.

asic.rev
========

ASIC design revision.

board.manufacture
=================

An identifier of the company or the facility which produced the part.

fw
==

Overall firmware version, often representing the collection of
fw.mgmt, fw.app, etc.

fw.mgmt
=======

Control unit firmware version. This firmware is responsible for house
keeping tasks, PHY control etc. but not the packet-by-packet data path
operation.

fw.app
======

Data path microcode controlling high-speed packet processing.

fw.undi
=======

UNDI software, may include the UEFI driver, firmware or both.

fw.ncsi
=======

Version of the software responsible for supporting/handling the
Network Controller Sideband Interface.

fw.psid
=======

Unique identifier of the firmware parameter set.
