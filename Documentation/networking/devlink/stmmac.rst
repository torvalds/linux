.. SPDX-License-Identifier: GPL-2.0

=======================================
stmmac (synopsys dwmac) devlink support
=======================================

This document describes the devlink features implemented by the ``stmmac``
device driver.

Parameters
==========

The ``stmmac`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``ts_coarse``
     - Boolean
     - runtime
     - Enable the Coarse timestamping mode. In Coarse mode, the ptp clock is
       expected to be updated through an external PPS input, but the subsecond
       increment used for timestamping is set to 1/ptp_clock_rate. In Fine mode
       (i.e. Coarse mode == false), the ptp clock frequency is adjusted more
       frequently, but the subsecond increment is set to 2/ptp_clock_rate.
       Coarse mode is suitable for PTP Grand Master operation. If unsure, leave
       the parameter to False.
