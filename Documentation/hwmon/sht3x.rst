Kernel driver sht3x
===================

Supported chips:

  * Sensirion SHT3x-DIS

    Prefix: 'sht3x'

    Addresses scanned: none

    Datasheets:
        - https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf
        - https://sensirion.com/media/documents/051DF50B/639C8101/Sensirion_Humidity_and_Temperature_Sensors_Datasheet_SHT33.pdf

  * Sensirion STS3x-DIS

    Prefix: 'sts3x'

    Addresses scanned: none

    Datasheets:
        - https://sensirion.com/media/documents/1DA31AFD/61641F76/Sensirion_Temperature_Sensors_STS3x_Datasheet.pdf
        - https://sensirion.com/media/documents/292A335C/65537BAF/Sensirion_Datasheet_STS32_STS33.pdf

Author:

  - David Frey <david.frey@sensirion.com>
  - Pascal Sachs <pascal.sachs@sensirion.com>

Description
-----------

This driver implements support for the Sensirion SHT3x-DIS and STS3x-DIS
series of humidity and temperature sensors. Temperature is measured in degrees
celsius, relative humidity is expressed as a percentage. In the sysfs interface,
all values are scaled by 1000, i.e. the value for 31.5 degrees celsius is 31500.

The device communicates with the I2C protocol. Sensors can have the I2C
addresses 0x44 or 0x45 (0x4a or 0x4b for sts3x), depending on the wiring. See
Documentation/i2c/instantiating-devices.rst for methods to instantiate the
device.

Even if sht3x sensor supports clock-stretch (blocking mode) and non-stretch
(non-blocking mode) in single-shot mode, this driver only supports the latter.

The sht3x sensor supports a single shot mode as well as 5 periodic measure
modes, which can be controlled with the update_interval sysfs interface.
The allowed update_interval in milliseconds are as follows:

    ===== ======= ====================
       0          single shot mode
    2000   0.5 Hz periodic measurement
    1000   1   Hz periodic measurement
     500   2   Hz periodic measurement
     250   4   Hz periodic measurement
     100  10   Hz periodic measurement
    ===== ======= ====================

In the periodic measure mode, the sensor automatically triggers a measurement
with the configured update interval on the chip. When a temperature or humidity
reading exceeds the configured limits, the alert attribute is set to 1 and
the alert pin on the sensor is set to high.
When the temperature and humidity readings move back between the hysteresis
values, the alert bit is set to 0 and the alert pin on the sensor is set to
low.

The serial number exposed to debugfs allows for unique identification of the
sensors. For sts32, sts33 and sht33, the manufacturer provides calibration
certificates through an API.

sysfs-Interface
---------------

=================== ============================================================
temp1_input:        temperature input
humidity1_input:    humidity input
temp1_max:          temperature max value
temp1_max_hyst:     temperature hysteresis value for max limit
humidity1_max:      humidity max value
humidity1_max_hyst: humidity hysteresis value for max limit
temp1_min:          temperature min value
temp1_min_hyst:     temperature hysteresis value for min limit
humidity1_min:      humidity min value
humidity1_min_hyst: humidity hysteresis value for min limit
temp1_alarm:        alarm flag is set to 1 if the temperature is outside the
		    configured limits. Alarm only works in periodic measure mode
humidity1_alarm:    alarm flag is set to 1 if the humidity is outside the
		    configured limits. Alarm only works in periodic measure mode
heater_enable:      heater enable, heating element removes excess humidity from
		    sensor:

			- 0: turned off
			- 1: turned on
update_interval:    update interval, 0 for single shot, interval in msec
		    for periodic measurement. If the interval is not supported
		    by the sensor, the next faster interval is chosen
repeatability:      write or read repeatability, higher repeatability means
                    longer measurement duration, lower noise level and
                    larger energy consumption:

                        - 0: low repeatability
                        - 1: medium repeatability
                        - 2: high repeatability
=================== ============================================================

debugfs-Interface
-----------------

=================== ============================================================
serial_number:      unique serial number of the sensor in decimal
=================== ============================================================
