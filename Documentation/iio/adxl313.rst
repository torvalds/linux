.. SPDX-License-Identifier: GPL-2.0

===============
ADXL313 driver
===============

This driver supports Analog Device's ADXL313 on SPI/I2C bus.

1. Supported devices
====================

* `ADXL313 <https://www.analog.com/ADXL313>`_

The ADXL313is a low noise density, low power, 3-axis accelerometer with
selectable measurement ranges. The ADXL313 supports the ±0.5 g, ±1 g, ±2 g and
±4 g ranges.

2. Device attributes
====================

Accelerometer measurements are always provided.

Each IIO device, has a device folder under ``/sys/bus/iio/devices/iio:deviceX``,
where X is the IIO index of the device. Under these folders reside a set of
device files, depending on the characteristics and features of the hardware
device in questions. These files are consistently generalized and documented in
the IIO ABI documentation.

The following tables show the adxl313 related device files, found in the
specific device folder path ``/sys/bus/iio/devices/iio:deviceX``.

+---------------------------------------------------+----------------------------------------------------------+
| 3-Axis Accelerometer related device files         | Description                                              |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_scale                                    | Scale for the accelerometer channels.                    |
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

+---------------------------------------+----------------------------------------------+
| Miscellaneous device files            | Description                                  |
+---------------------------------------+----------------------------------------------+
| name                                  | Name of the IIO device.                      |
+---------------------------------------+----------------------------------------------+
| in_accel_sampling_frequency           | Currently selected sample rate.              |
+---------------------------------------+----------------------------------------------+
| in_accel_sampling_frequency_available | Available sampling frequency configurations. |
+---------------------------------------+----------------------------------------------+

The iio event related settings, found in ``/sys/bus/iio/devices/iio:deviceX/events``.

+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_adaptive_falling_period              | AC coupled inactivity time.                              |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_adaptive_falling_value               | AC coupled inactivity threshold.                         |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_adaptive_rising_value                | AC coupled activity threshold.                           |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_falling_period                       | Inactivity time.                                         |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_falling_value                        | Inactivity threshold.                                    |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_mag_rising_value                         | Activity threshold.                                      |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x\&y\&z_mag_adaptive_falling_en          | Enable or disable AC coupled inactivity events.          |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x\|y\|z_mag_adaptive_rising_en           | Enable or disable AC coupled activity events.            |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x\&y\&z_mag_falling_en                   | Enable or disable inactivity events.                     |
+---------------------------------------------------+----------------------------------------------------------+
| in_accel_x\|y\|z_mag_rising_en                    | Enable or disable activity events.                       |
+---------------------------------------------------+----------------------------------------------------------+

The default coupling is DC coupled events. In this case the threshold will
be in place as such, where for the AC coupled case an adaptive threshold
(described in the datasheet) will be applied by the sensor. In general activity,
i.e. ``ACTIVITY`` or ``ACTIVITY_AC`` and inactivity i.e. ``INACTIVITY`` or
``INACTIVITY_AC``, will be linked with auto-sleep enabled when both are enabled.
This means in particular ``ACTIVITY`` can also be linked to ``INACTIVITY_AC``
and vice versa, without problem.

Note here, that ``ACTIVITY`` and ``ACTIVITY_AC`` are mutually exclusive. This
means, that the most recent configuration will be set. For instance, if
``ACTIVITY`` is enabled, and ``ACTIVITY_AC`` will be enabled, the sensor driver
will have ``ACTIVITY`` disabled, but ``ACTIVITY_AC`` enabled. The same is valid
for inactivity. In case of turning off an event, it has to match to what is
actually enabled, i.e. enabling ``ACTIVITY_AC`` and then disabling ``ACTIVITY``
is simply ignored as it is already disabled. Or, as if it was any other not
enabled event, too.

Channels processed values
-------------------------

A channel value can be read from its _raw attribute. The value returned is the
raw value as reported by the devices. To get the processed value of the channel,
apply the following formula:

.. code-block::

        processed value = (_raw + _offset) * _scale

Where _offset and _scale are device attributes. If no _offset attribute is
present, simply assume its value is 0.

The ADXL313 driver offers data for a single types of channels, the table below
shows the measurement units for the processed value, which are defined by the
IIO framework:

+-------------------------------------+---------------------------+
| Channel type                        | Measurement unit          |
+-------------------------------------+---------------------------+
| Acceleration on X, Y, and Z axis    | Meters per Second squared |
+-------------------------------------+---------------------------+

Usage examples
--------------

Show device name:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat name
        adxl313

Show accelerometer channels value:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_raw
        2
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_y_raw
        -57
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_z_raw
        2
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_scale
        0.009576806

The accelerometer values will be:

- X-axis acceleration = in_accel_x_raw * in_accel_scale = 0.0191536 m/s^2
- Y-axis acceleration = in_accel_y_raw * in_accel_scale = -0.5458779 m/s^2
- Z-axis acceleration = in_accel_z_raw * in_accel_scale = 0.0191536 m/s^2

Set calibration offset for accelerometer channels. Note, that the calibration
will be rounded according to the graduation of LSB units:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_calibbias
        0

        root:/sys/bus/iio/devices/iio:device0> echo 50 > in_accel_x_calibbias
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_x_calibbias
        48

Set sampling frequency:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> cat in_accel_sampling_frequency
        100.000000
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_sampling_frequency_available
        6.250000 12.500000 25.000000 50.000000 100.000000 200.000000 400.000000 800.000000 1600.000000 3200.000000

        root:/sys/bus/iio/devices/iio:device0> echo 400 > in_accel_sampling_frequency
        root:/sys/bus/iio/devices/iio:device0> cat in_accel_sampling_frequency
        400.000000

3. Device buffers and triggers
==============================

This driver supports IIO buffers.

All devices support retrieving the raw acceleration measurements using buffers.

Usage examples
--------------

Select channels for buffer read:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_x_en
        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_y_en
        root:/sys/bus/iio/devices/iio:device0> echo 1 > scan_elements/in_accel_z_en

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
        000000d0  01 fc 31 00 c7 ff 03 fc  31 00 c7 ff 04 fc 33 00  |..1.....1.....3.|
        000000e0  c8 ff 03 fc 32 00 c5 ff  ff fc 32 00 c7 ff 0a fc  |....2.....2.....|
        000000f0  30 00 c8 ff 06 fc 33 00  c7 ff 01 fc 2f 00 c8 ff  |0.....3...../...|
        00000100  02 fc 32 00 c6 ff 04 fc  33 00 c8 ff 05 fc 33 00  |..2.....3.....3.|
        00000110  ca ff 02 fc 31 00 c7 ff  02 fc 30 00 c9 ff 09 fc  |....1.....0.....|
        00000120  35 00 c9 ff 08 fc 35 00  c8 ff 02 fc 31 00 c5 ff  |5.....5.....1...|
        00000130  03 fc 32 00 c7 ff 04 fc  32 00 c7 ff 02 fc 31 00  |..2.....2.....1.|
        00000140  c7 ff 08 fc 30 00 c7 ff  02 fc 32 00 c5 ff ff fc  |....0.....2.....|
        00000150  31 00 c5 ff 04 fc 31 00  c8 ff 03 fc 32 00 c8 ff  |1.....1.....2...|
        00000160  01 fc 31 00 c7 ff 05 fc  31 00 c3 ff 04 fc 31 00  |..1.....1.....1.|
        00000170  c5 ff 04 fc 30 00 c7 ff  03 fc 31 00 c9 ff 03 fc  |....0.....1.....|
        ...

Enabling activity detection:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1.28125 > ./events/in_accel_mag_rising_value
        root:/sys/bus/iio/devices/iio:device0> echo 1 > ./events/in_accel_x\|y\|z_mag_rising_en

        root:/sys/bus/iio/devices/iio:device0> iio_event_monitor adxl313
        Found IIO device with name adxl313 with device number 0
        <only while moving the sensor>
        Event: time: 1748795762298351281, type: accel(x|y|z), channel: 0, evtype: mag, direction: rising
        Event: time: 1748795762302653704, type: accel(x|y|z), channel: 0, evtype: mag, direction: rising
        Event: time: 1748795762304340726, type: accel(x|y|z), channel: 0, evtype: mag, direction: rising
        ...

Disabling activity detection:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 0 > ./events/in_accel_x\|y\|z_mag_rising_en
        root:/sys/bus/iio/devices/iio:device0> iio_event_monitor adxl313
        <nothing>

Enabling inactivity detection:

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1.234375 > ./events/in_accel_mag_falling_value
        root:/sys/bus/iio/devices/iio:device0> echo 5 > ./events/in_accel_mag_falling_period
        root:/sys/bus/iio/devices/iio:device0> echo 1 > ./events/in_accel_x\&y\&z_mag_falling_en

        root:/sys/bus/iio/devices/iio:device0> iio_event_monitor adxl313
        Found IIO device with name adxl313 with device number 0
        Event: time: 1748796324115962975, type: accel(x&y&z), channel: 0, evtype: mag, direction: falling
        Event: time: 1748796329329981772, type: accel(x&y&z), channel: 0, evtype: mag, direction: falling
        Event: time: 1748796334543399706, type: accel(x&y&z), channel: 0, evtype: mag, direction: falling
        ...
        <every 5s now indicates inactivity>

Now, enabling activity, e.g. the AC coupled counter-part ``ACTIVITY_AC``

.. code-block:: bash

        root:/sys/bus/iio/devices/iio:device0> echo 1.28125 > ./events/in_accel_mag_rising_value
        root:/sys/bus/iio/devices/iio:device0> echo 1 > ./events/in_accel_x\|y\|z_mag_rising_en

        root:/sys/bus/iio/devices/iio:device0> iio_event_monitor adxl313
        Found IIO device with name adxl313 with device number 0
        <some activity with the sensor>
        Event: time: 1748796880354686777, type: accel(x|y|z), channel: 0, evtype: mag_adaptive, direction: rising
        <5s of inactivity, then>
        Event: time: 1748796885543252017, type: accel(x&y&z), channel: 0, evtype: mag, direction: falling
        <some other activity detected by accelerating the sensor>
        Event: time: 1748796887756634678, type: accel(x|y|z), channel: 0, evtype: mag_adaptive, direction: rising
        <again, 5s of inactivity>
        Event: time: 1748796892964368352, type: accel(x&y&z), channel: 0, evtype: mag, direction: falling
        <stays like this until next activity in auto-sleep>

Note, when AC coupling is in place, the event type will be of ``mag_adaptive``.
AC- or DC-coupled (the default) events are used similarly.

4. IIO Interfacing Tools
========================

See Documentation/iio/iio_tools.rst for the description of the available IIO
interfacing tools.
