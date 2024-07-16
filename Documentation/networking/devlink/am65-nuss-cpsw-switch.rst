.. SPDX-License-Identifier: GPL-2.0

==============================
am65-cpsw-nuss devlink support
==============================

This document describes the devlink features implemented by the ``am65-cpsw-nuss``
device driver.

Parameters
==========

The ``am65-cpsw-nuss`` driver implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``switch_mode``
     - Boolean
     - runtime
     - Enable switch mode
