EDT ft5x06 based Polytouch devices
----------------------------------

The edt-ft5x06 driver is useful for the EDT "Polytouch" family of capacitive
touch screens. Note that it is *not* suitable for other devices based on the
focaltec ft5x06 devices, since they contain vendor-specific firmware. In
particular this driver is not suitable for the Nook tablet.

It has been tested with the following devices:
  * EP0350M06
  * EP0430M06
  * EP0570M06
  * EP0700M06

The driver allows configuration of the touch screen via a set of sysfs files:

/sys/class/input/eventX/device/device/threshold:
    allows setting the "click"-threshold in the range from 20 to 80.

/sys/class/input/eventX/device/device/gain:
    allows setting the sensitivity in the range from 0 to 31. Note that
    lower values indicate higher sensitivity.

/sys/class/input/eventX/device/device/offset:
    allows setting the edge compensation in the range from 0 to 31.

/sys/class/input/eventX/device/device/report_rate:
    allows setting the report rate in the range from 3 to 14.


For debugging purposes the driver provides a few files in the debug
filesystem (if available in the kernel). In /sys/kernel/debug/edt_ft5x06
you'll find the following files:

num_x, num_y:
    (readonly) contains the number of sensor fields in X- and
    Y-direction.

mode:
    allows switching the sensor between "factory mode" and "operation
    mode" by writing "1" or "0" to it. In factory mode (1) it is
    possible to get the raw data from the sensor. Note that in factory
    mode regular events don't get delivered and the options described
    above are unavailable.

raw_data:
    contains num_x * num_y big endian 16 bit values describing the raw
    values for each sensor field. Note that each read() call on this
    files triggers a new readout. It is recommended to provide a buffer
    big enough to contain num_x * num_y * 2 bytes.

Note that reading raw_data gives a I/O error when the device is not in factory
mode. The same happens when reading/writing to the parameter files when the
device is not in regular operation mode.
