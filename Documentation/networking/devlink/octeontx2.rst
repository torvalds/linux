.. SPDX-License-Identifier: GPL-2.0

=========================
octeontx2 devlink support
=========================

This document describes the devlink features implemented by the ``octeontx2 AF, PF and VF``
device drivers.

Parameters
==========

The ``octeontx2 PF and VF`` drivers implement the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``mcam_count``
     - u16
     - runtime
     - Select number of match CAM entries to be allocated for an interface.
       The same is used for ntuple filters of the interface. Supported by
       PF and VF drivers.

The ``octeontx2 AF`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``dwrr_mtu``
     - u32
     - runtime
     - Use to set the quantum which hardware uses for scheduling among transmit queues.
       Hardware uses weighted DWRR algorithm to schedule among all transmit queues.

The ``octeontx2 PF`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``unicast_filter_count``
     - u8
     - runtime
     - Set the maximum number of unicast filters that can be programmed for
       the device. This can be used to achieve better device resource
       utilization, avoiding over consumption of unused MCAM table entries.
