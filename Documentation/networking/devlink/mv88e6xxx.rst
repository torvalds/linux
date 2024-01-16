.. SPDX-License-Identifier: GPL-2.0

=========================
mv88e6xxx devlink support
=========================

This document describes the devlink features implemented by the ``mv88e6xxx``
device driver.

Parameters
==========

The ``mv88e6xxx`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``ATU_hash``
     - u8
     - runtime
     - Select one of four possible hashing algorithms for MAC addresses in
       the Address Translation Unit. A value of 3 may work better than the
       default of 1 when many MAC addresses have the same OUI. Only the
       values 0 to 3 are valid for this parameter.
