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
   * - ``phc_coarse_adj``
     - Boolean
     - runtime
     - Enable the Coarse timestamping mode, as defined in the DWMAC TRM.
       A detailed explanation of this timestamping mode can be found in the
       Socfpga Functionnal Description [1].

       In Coarse mode, the ptp clock is expected to be fed by a high-precision
       clock that is externally adjusted, and the subsecond increment used for
       timestamping is set to 1/ptp_clock_rate.

       In Fine mode (i.e. Coarse mode == false), the ptp clock frequency is
       continuously adjusted, but the subsecond increment is set to
       2/ptp_clock_rate.

       Coarse mode is suitable for PTP Grand Master operation. If unsure, leave
       the parameter to False.

       [1] https://www.intel.com/content/www/us/en/docs/programmable/683126/21-2/functional-description-of-the-emac.html
