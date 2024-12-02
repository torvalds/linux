.. SPDX-License-Identifier: GPL-2.0

==============================
ti-cpsw-switch devlink support
==============================

This document describes the devlink features implemented by the ``ti-cpsw-switch``
device driver.

Parameters
==========

The ``ti-cpsw-switch`` driver implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``ale_bypass``
     - Boolean
     - runtime
     - Enables ALE_CONTROL(4).BYPASS mode for debugging purposes. In this
       mode, all packets will be sent to the host port only.
   * - ``switch_mode``
     - Boolean
     - runtime
     - Enable switch mode
