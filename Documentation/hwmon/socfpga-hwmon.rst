.. SPDX-License-Identifier: GPL-2.0

Kernel driver socfpga-hwmon
=============================

Supported chips:

 * Altera Stratix 10 SoC FPGA
 * Altera Agilex SoC FPGA
 * Altera Agilex 5 SoC FPGA

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
  * intel,agilex5-svc

Channel mappings are fixed in the driver (not described in DT). The tables
below list the SDM page/channel encodings used for each family.

Temperature channels
~~~~~~~~~~~~~~~~~~~~

==========  ====  =======  =================================
Family      Page  Channel  Label
==========  ====  =======  =================================
Stratix 10  0     0        Main Die SDM
Agilex      0     0        Main Die SDM
Agilex      1     0        Main Die corner bottom left max
Agilex      2     0        Main Die corner top left max
Agilex      3     0        Main Die corner bottom right max
Agilex      4     0        Main Die corner top right max
Agilex 5    0     0        Main Die SDM
Agilex 5    1     0        Main Die corner bottom left max
Agilex 5    3     0        Main Die corner bottom right max
Agilex 5    4     0        Main Die corner top right max
==========  ====  =======  =================================

Agilex 5 omits SDM temperature channel 2 (top-left corner on Agilex)
because that sensor is not present in hardware. The remaining sensors keep
the same channel numbers as Agilex.

Voltage channels
~~~~~~~~~~~~~~~~

==========  ====  =======  =================
Family      Page  Channel  Label
==========  ====  =======  =================
Stratix 10  0     2        0.8V VCC
Stratix 10  0     3        1.8V VCCIO_SDM
Stratix 10  0     6        0.9V VCCERAM
Agilex      0     2        0.8V VCC
Agilex      0     3        1.8V VCCIO_SDM
Agilex      0     4        1.8V VCCPT
Agilex      0     5        1.2V VCCCRCORE
Agilex      0     6        0.9V VCCH
Agilex      0     7        0.8V VCCL
Agilex 5    0     2        0.8V VCC
Agilex 5    0     3        1.8V VCCIO_SDM
Agilex 5    0     4        1.8V VCCPT
Agilex 5    0     5        1.2V VCCCRCORE
Agilex 5    0     6        0.9V VCCH
Agilex 5    0     7        0.8V VCCL
==========  ====  =======  =================

Agilex 5 reuses the Agilex voltage SDM page/channel layout and labels.
