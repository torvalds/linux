.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver Ampere(R)'s Altra(R) SMpro hwmon
==============================================

Supported chips:

  * Ampere(R) Altra(R)

    Prefix: ``smpro``

    Reference: `Altra SoC BMC Interface Specification`

Author: Thu Nguyen <thu@os.amperecomputing.com>

Description
-----------
The smpro-hwmon driver supports hardware monitoring for Ampere(R) Altra(R)
SoCs based on the SMpro co-processor (SMpro).  The following sensor metrics
are supported by the driver:

  * temperature
  * voltage
  * current
  * power

The interface provides the registers to query the various sensors and
their values which are then exported to userspace by this driver.

Usage Notes
-----------

The driver creates at least two sysfs files for each sensor.

* ``<sensor_type><idx>_label`` reports the sensor label.
* ``<sensor_type><idx>_input`` returns the sensor value.

The sysfs files are allocated in the SMpro rootfs folder, with one root
directory for each instance.

When the SoC is turned off, the driver will fail to read registers and
return ``-ENXIO``.

Sysfs entries
-------------

The following sysfs files are supported:

* Ampere(R) Altra(R):

  ============    =============  ======  ===============================================
  Name            Unit           Perm    Description
  ============    =============  ======  ===============================================
  temp1_input     millicelsius   RO      SoC temperature
  temp2_input     millicelsius   RO      Max temperature reported among SoC VRDs
  temp2_crit      millicelsius   RO      SoC VRD HOT Threshold temperature
  temp3_input     millicelsius   RO      Max temperature reported among DIMM VRDs
  temp4_input     millicelsius   RO      Max temperature reported among Core VRDs
  temp5_input     millicelsius   RO      Temperature of DIMM0 on CH0
  temp5_crit      millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp6_input     millicelsius   RO      Temperature of DIMM0 on CH1
  temp6_crit      millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp7_input     millicelsius   RO      Temperature of DIMM0 on CH2
  temp7_crit      millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp8_input     millicelsius   RO      Temperature of DIMM0 on CH3
  temp8_crit      millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp9_input     millicelsius   RO      Temperature of DIMM0 on CH4
  temp9_crit      millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp10_input    millicelsius   RO      Temperature of DIMM0 on CH5
  temp10_crit     millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp11_input    millicelsius   RO      Temperature of DIMM0 on CH6
  temp11_crit     millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp12_input    millicelsius   RO      Temperature of DIMM0 on CH7
  temp12_crit     millicelsius   RO      MEM HOT Threshold for all DIMMs
  temp13_input    millicelsius   RO      Max temperature reported among RCA VRDs
  in0_input       millivolts     RO      Core voltage
  in1_input       millivolts     RO      SoC voltage
  in2_input       millivolts     RO      DIMM VRD1 voltage
  in3_input       millivolts     RO      DIMM VRD2 voltage
  in4_input       millivolts     RO      RCA VRD voltage
  cur1_input      milliamperes   RO      Core VRD current
  cur2_input      milliamperes   RO      SoC VRD current
  cur3_input      milliamperes   RO      DIMM VRD1 current
  cur4_input      milliamperes   RO      DIMM VRD2 current
  cur5_input      milliamperes   RO      RCA VRD current
  power1_input    microwatts     RO      Core VRD power
  power2_input    microwatts     RO      SoC VRD power
  power3_input    microwatts     RO      DIMM VRD1 power
  power4_input    microwatts     RO      DIMM VRD2 power
  power5_input    microwatts     RO      RCA VRD power
  ============    =============  ======  ===============================================

  Example::

    # cat in0_input
    830
    # cat temp1_input
    37000
    # cat curr1_input
    9000
    # cat power5_input
    19500000
