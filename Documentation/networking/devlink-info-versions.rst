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
