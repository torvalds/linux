.. SPDX-License-Identifier: GPL-2.0

===================
qed devlink support
===================

This document describes the devlink features implemented by the ``qed`` core
device driver.

Parameters
==========

The ``qed`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``iwarp_cmt``
     - Boolean
     - runtime
     - Enable iWARP functionality for 100g devices. Notee that this impacts
       L2 performance, and is therefor not enabled by default.
