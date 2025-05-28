.. SPDX-License-Identifier: GPL-2.0

===============
ADXL380 driver
===============

This driver supports Analog Device's ADXL380/382 on SPI/I2C bus.

1. Supported devices
====================

* `ADXL380 <https://www.analog.com/ADXL380>`_
* `ADXL382 <https://www.analog.com/ADXL382>`_

The ADXL380/ADXL382 is a low noise density, low power, 3-axis accelerometer with
selectable measurement ranges. The ADXL380 supports the ±4 g, ±8 g, and ±16 g
ranges, and the ADXL382 supports ±15 g, ±30 g, and ±60 g ranges.

2. Device attributes
====================

Accelerometer measurements are always provided.

Temperature data are also provided. This data can be used to monitor the
internal system temperature or to improve the temperature stability of the
device via calibration.

Each IIO device, has a device folder under ``/sys/bus/iio/devices/iio:deviceX``,
where X is the IIO index of the device. Under these folders reside a set of
device files, depending on the characteristics and features of the hardware
device in questions. These files are consistently generalized and documented in
the IIO ABI documentation.

The following tables show the adxl380 related device files, found in the
specific device folder path ``/sys/bus/iio/devices/iio:deviceX``.

+---------------------------------------------------+----------------------------------------------------------+
| 3-Axis Accelerometer related device files         | Description                                              |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_scale                                    | Scale for the accelerometer channels.                    |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_filter_high_pass_3db_frequency           | Low pass filter bandwidth.                               |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_filter_high_pass_3db_frequency_available | Available low pass filter bandwidth configurations.      |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_filter_low_pass_3db_frequency            | High pass filter bandwidth.                              |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_filter_low_pass_3db_frequency_available  | Available high pass filter bandwidth configurations.     |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x_calibbias                              | Calibration offset for the X-axis accelerometer channel. |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x_raw                                    | Raw X-axis accelerometer channel value.                  |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_y_calibbias                              | y-axis acceleration offset correction                    |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_y_raw                                    | Raw Y-axis accelerometer channel value.                  |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_z_calibbias                              | Calibration offset for the Z-axis accelerometer channel. |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_z_raw                                    | Raw Z-axis accelerometer channel value.                  |
+---------------------------------------------------+----------------------------------------------------------+

+----------------------------------+--------------------------------------------+
| Temperature sensor related files | Description                                |
+----------------------------------+--------------------------------------------+
| in_temp_raw                      | Raw temperature channel value.             |
+----------------------------------+--------------------------------------------+
| in_temp_offset                   | Offset for the temperature sensor channel. |
+----------------------------------+--------------------------------------------+
| in_temp_scale                    | Scale for the temperature sensor channel.  |
+----------------------------------+--------------------------------------------+

+------------------------------+----------------------------------------------+
| Miscellaneous device files   | Description                                  |
+------------------------------+----------------------------------------------+
| name                         | Name of the IIO device.                      |
+------------------------------+----------------------------------------------+
| sampling_frequency           | Currently selected sample rate.              |
+------------------------------+----------------------------------------------+
| sampling_frequency_available | Available sampling frequency configurations. |
+------------------------------+----------------------------------------------+

Channels processed values
-------------------------

A channel value can be read from its _raw attribute. The value returned is the
raw value as reported by the devices. To get the processed value of the channel,
apply the following formula:

.. code-block:: bash

        processed value = (_raw + _offset) * _scale

Where _offset and _scale are device attributes. If no _offset attribute is
present, simply assume its value is 0.

The ADXL380 driver offers data for 2 types of channels, the table below shows
the measurement units for the processed value, which are defined by the IIO
framework:

+-------------------------------------+---------------------------+
| Channel type                        | Measurement unit          |
+-------------------------------------+---------------------------+
| Acceleration on X, Y, and Z axis    | Meters per Second squared |
+-------------------------------------+---------------------------+
| Temperature                         | Millidegrees Celsius      |
+-------------------------------------+---------------------------+

Usage examples
--------------

Show device name:

.. code-block:: bash

	root:/sys/bus/iio/devices/iio:device0> cat name
        adxl382

Show accelerometer channels value:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_raw
        -1771
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_y_raw
        282
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_z_raw
        -1523
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_scale
        0.004903325

- X-axis acceleration = in_accel_x_raw * in_accel_scale = −8.683788575 m/s^2
- Y-axis acceleration = in_accel_y_raw * in_accel_scale = 1.38273765 m/s^2
- Z-axis acceleration = in_accel_z_raw * in_accel_scale = -7.467763975 m/s^2

Set calibration offset for accelerometer channels:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_calibbias
        0

        root:/sys/bus/iio/devices/iio:device0> echo 50 > in_accel_x_calibbias
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_calibbias
        50

Set sampling frequency:

.. code-block:: bash

	root:/sys/bus/iio/devices/iio:device0> cat sampling_frequency
        16000
        root:/sys/bus/iio/devices/iio:device0> cat sampling_frequency_available
        16000 32000 64000

        root:/sys/bus/iio/devices/iio:device0> echo 32000 > sampling_frequency
        root:/sys/bus/iio/devices/iio:device0> cat sampling_frequency
        32000

Set low pass filter bandwidth for accelerometer channels:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_filter_low_pass_3db_frequency
        32000
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_filter_low_pass_3db_frequency_available
        32000 8000 4000 2000

        root:/sys/bus/iio/devices/iio:device0> echo 2000 > in_accel_filter_low_pass_3db_frequency
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_filter_low_pass_3db_frequency
        2000

3. Device buffers
=================

This driver supports IIO buffers.

All devices support retrieving the raw acceleration and temperature measurements
using buffers.

Usage examples
--------------

Select channels for buffer read:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_x_en
        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_y_en
        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_z_en
        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_temp_en

Set the number of samples to be stored in the buffer:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 10 > buffer/length

Enable buffer readings:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1 > buffer/enable

Obtain buffered data:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> hexdump -C /dev/iio\:device0
        ...
        002bc300  f7 e7 00 a8 fb c5 24 80  f7 e7 01 04 fb d6 24 80  |......$.......$.|
        002bc310  f7 f9 00 ab fb dc 24 80  f7 c3 00 b8 fb e2 24 80  |......$.......$.|
        002bc320  f7 fb 00 bb fb d1 24 80  f7 b1 00 5f fb d1 24 80  |......$...._..$.|
        002bc330  f7 c4 00 c6 fb a6 24 80  f7 a6 00 68 fb f1 24 80  |......$....h..$.|
        002bc340  f7 b8 00 a3 fb e7 24 80  f7 9a 00 b1 fb af 24 80  |......$.......$.|
        002bc350  f7 b1 00 67 fb ee 24 80  f7 96 00 be fb 92 24 80  |...g..$.......$.|
        002bc360  f7 ab 00 7a fc 1b 24 80  f7 b6 00 ae fb 76 24 80  |...z..$......v$.|
        002bc370  f7 ce 00 a3 fc 02 24 80  f7 c0 00 be fb 8b 24 80  |......$.......$.|
        002bc380  f7 c3 00 93 fb d0 24 80  f7 ce 00 d8 fb c8 24 80  |......$.......$.|
        002bc390  f7 bd 00 c0 fb 82 24 80  f8 00 00 e8 fb db 24 80  |......$.......$.|
        002bc3a0  f7 d8 00 d3 fb b4 24 80  f8 0b 00 e5 fb c3 24 80  |......$.......$.|
        002bc3b0  f7 eb 00 c8 fb 92 24 80  f7 e7 00 ea fb cb 24 80  |......$.......$.|
        002bc3c0  f7 fd 00 cb fb 94 24 80  f7 e3 00 f2 fb b8 24 80  |......$.......$.|
        ...

See ``Documentation/iio/iio_devbuf.rst`` for more information about how buffered
data is structured.

4. IIO Interfacing Tools
========================

See ``Documentation/iio/iio_tools.rst`` for the description of the available IIO
interfacing tools.
