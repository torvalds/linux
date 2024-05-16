.. SPDX-License-Identifier: GPL-2.0

=========================
octeontx2 devlink support
=========================

This document describes the devlink features implemented by the ``octeontx2 CPT``
device drivers.

Parameters
==========

The ``octeontx2`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``t106_mode``
     - u8
     - runtime
     - Used to configure CN10KA B0/CN10KB CPT to work as CN10KA A0/A1.
