.. SPDX-License-Identifier: GPL-2.0

Kernel driver socfpga-hwmon
=============================

Supported chips:

 * Altera Stratix 10 SoC FPGA
 * Altera Agilex SoC FPGA

Authors:
      - Nazim Amirul <muhammad.nazim.amirul.nazle.asmade@altera.com>
      - Tze Yee Ng <tze.yee.ng@altera.com>

Description
-----------

This driver supports hardware monitoring for Altera SoC
FPGA devices through the Secure Device Manager and Stratix 10 service layer.

The following sensor types are supported:

  * temperature
  * voltage

Usage Notes
-----------

The stratix10-svc driver registers a socfpga-hwmon platform device when
hardware monitor support is enabled. Sensor channels are selected in the
driver based on the service layer compatible string:

  * intel,stratix10-svc
  * intel,agilex-svc
