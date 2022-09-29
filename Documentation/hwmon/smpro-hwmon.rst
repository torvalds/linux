.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver Ampere(R)'s Altra(R) SMpro hwmon
==============================================

Supported chips:

  * Ampere(R) Altra(R)

    Prefix: 'smpro'

    Reference: Altra SoC BMC Interface Specification

Author: Thu Nguyen <thu@os.amperecomputing.com>

Description
-----------
This driver supports hardware monitoring for Ampere(R) Altra(R) SoC's based on the
SMpro co-processor (SMpro).
The following sensor types are supported by the driver:

  * temperature
  * voltage
  * current
  * power

The SMpro interface provides the registers to query the various sensors and
their values which are then exported to userspace by this driver.

Usage Notes
-----------

SMpro hwmon driver creates at least two sysfs files for each sensor.

* File ``<sensor_type><idx>_label`` reports the sensor label.
* File ``<sensor_type><idx>_input`` returns the sensor value.

The sysfs files are allocated in the SMpro root fs folder.
There is one root folder for each SMpro instance.

When the SoC is turned off, the driver will fail to read registers
and return -ENXIO.

Sysfs entries
-------------

The following sysfs files are supported:

* Ampere(R) Altra(R):

============    =============   ======  ===============================================
Name            Unit            Perm    Description
temp1_input     milli Celsius   RO      SoC temperature
temp2_input     milli Celsius   RO      Max temperature reported among SoC VRDs
temp2_crit      milli Celsius   RO      SoC VRD HOT Threshold temperature
temp3_input     milli Celsius   RO      Max temperature reported among DIMM VRDs
temp4_input     milli Celsius   RO      Max temperature reported among Core VRDs
temp5_input     milli Celsius   RO      Temperature of DIMM0 on CH0
temp5_crit      milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp6_input     milli Celsius   RO      Temperature of DIMM0 on CH1
temp6_crit      milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp7_input     milli Celsius   RO      Temperature of DIMM0 on CH2
temp7_crit      milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp8_input     milli Celsius   RO      Temperature of DIMM0 on CH3
temp8_crit      milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp9_input     milli Celsius   RO      Temperature of DIMM0 on CH4
temp9_crit      milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp10_input    milli Celsius   RO      Temperature of DIMM0 on CH5
temp10_crit     milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp11_input    milli Celsius   RO      Temperature of DIMM0 on CH6
temp11_crit     milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp12_input    milli Celsius   RO      Temperature of DIMM0 on CH7
temp12_crit     milli Celsius   RO      MEM HOT Threshold for all DIMMs
temp13_input    milli Celsius   RO      Max temperature reported among RCA VRDs
in0_input       milli Volts     RO      Core voltage
in1_input       milli Volts     RO      SoC voltage
in2_input       milli Volts     RO      DIMM VRD1 voltage
in3_input       milli Volts     RO      DIMM VRD2 voltage
in4_input       milli Volts     RO      RCA VRD voltage
cur1_input      milli Amperes   RO      Core VRD current
cur2_input      milli Amperes   RO      SoC VRD current
cur3_input      milli Amperes   RO      DIMM VRD1 current
cur4_input      milli Amperes   RO      DIMM VRD2 current
cur5_input      milli Amperes   RO      RCA VRD current
power1_input    micro Watts     RO      Core VRD power
power2_input    micro Watts     RO      SoC VRD power
power3_input    micro Watts     RO      DIMM VRD1 power
power4_input    micro Watts     RO      DIMM VRD2 power
power5_input    micro Watts     RO      RCA VRD power
============    =============   ======  ===============================================

Example::

    # cat in0_input
    830
    # cat temp1_input
    37000
    # cat curr1_input
    9000
    # cat power5_input
    19500000
