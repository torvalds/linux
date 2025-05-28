.. SPDX-License-Identifier: GPL-2.0

=============================
Industrial IIO device buffers
=============================

1. Overview
===========

The Industrial I/O core offers a way for continuous data capture based on a
trigger source. Multiple data channels can be read at once from
``/dev/iio:deviceX`` character device node, thus reducing the CPU load.

Devices with buffer support feature an additional sub-directory in the
``/sys/bus/iio/devices/iio:deviceX/`` directory hierarchy, called bufferY, where
Y defaults to 0, for devices with a single buffer.

2. Buffer attributes
====================

An IIO buffer has an associated attributes directory under
``/sys/bus/iio/iio:deviceX/bufferY/``. The attributes are described below.

``length``
----------

Read / Write attribute which states the total number of data samples (capacity)
that can be stored by the buffer.

``enable``
----------

Read / Write attribute which starts / stops the buffer capture. This file should
be written last, after length and selection of scan elements. Writing a non-zero
value may result in an error, such as EINVAL, if, for example, an unsupported
combination of channels is given.

``watermark``
-------------

Read / Write positive integer attribute specifying the maximum number of scan
elements to wait for.

Poll will block until the watermark is reached.

Blocking read will wait until the minimum between the requested read amount or
the low watermark is available.

Non-blocking read will retrieve the available samples from the buffer even if
there are less samples than the watermark level. This allows the application to
block on poll with a timeout and read the available samples after the timeout
expires and thus have a maximum delay guarantee.

Data available
--------------

Read-only attribute indicating the bytes of data available in the buffer. In the
case of an output buffer, this indicates the amount of empty space available to
write data to. In the case of an input buffer, this indicates the amount of data
available for reading.

Scan elements
-------------

The meta information associated with a channel data placed in a buffer is called
a scan element. The scan elements attributes are presented below.

**_en**

Read / Write attribute used for enabling a channel. If and only if its value
is non-zero, then a triggered capture will contain data samples for this
channel.

**_index**

Read-only unsigned integer attribute specifying the position of the channel in
the buffer. Note these are not dependent on what is enabled and may not be
contiguous. Thus for userspace to establish the full layout these must be used
in conjunction with all _en attributes to establish which channels are present,
and the relevant _type attributes to establish the data storage format.

**_type**

Read-only attribute containing the description of the scan element data storage
within the buffer and hence the form in which it is read from userspace. Format
is [be|le]:[s|u]bits/storagebits[Xrepeat][>>shift], where:

- **be** or **le** specifies big or little-endian.
- **s** or **u** specifies if signed (2's complement) or unsigned.
- **bits** is the number of valid data bits.
- **storagebits** is the number of bits (after padding) that it occupies in the
  buffer.
- **repeat** specifies the number of bits/storagebits repetitions. When the
  repeat element is 0 or 1, then the repeat value is omitted.
- **shift** if specified, is the shift that needs to be applied prior to
  masking out unused bits.

For example, a driver for a 3-axis accelerometer with 12-bit resolution where
data is stored in two 8-bit registers is as follows::

          7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
        |D3 |D2 |D1 |D0 | X | X | X | X | (LOW byte, address 0x06)
        +---+---+---+---+---+---+---+---+

          7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
        |D11|D10|D9 |D8 |D7 |D6 |D5 |D4 | (HIGH byte, address 0x07)
        +---+---+---+---+---+---+---+---+

will have the following scan element type for each axis:

.. code-block:: bash

        $ cat /sys/bus/iio/devices/iio:device0/buffer0/in_accel_y_type
        le:s12/16>>4

A userspace application will interpret data samples read from the buffer as
two-byte little-endian signed data, that needs a 4 bits right shift before
masking out the 12 valid bits of data.

It is also worth mentioning that the data in the buffer will be naturally
aligned, so the userspace application has to handle the buffers accordingly.

Take for example, a driver with four channels with the following description:
- channel0: index: 0, type: be:u16/16>>0
- channel1: index: 1, type: be:u32/32>>0
- channel2: index: 2, type: be:u32/32>>0
- channel3: index: 3, type: be:u64/64>>0

If all channels are enabled, the data will be aligned in the buffer as follows::

          0-1   2   3   4-7  8-11  12  13  14  15  16-23   -> buffer byte number
        +-----+---+---+-----+-----+---+---+---+---+-----+
        |CHN_0|PAD|PAD|CHN_1|CHN_2|PAD|PAD|PAD|PAD|CHN_3|  -> buffer content
        +-----+---+---+-----+-----+---+---+---+---+-----+

If only channel0 and channel3 are enabled, the data will be aligned in the
buffer as follows::

          0-1   2   3   4   5   6   7  8-15    -> buffer byte number
        +-----+---+---+---+---+---+---+-----+
        |CHN_0|PAD|PAD|PAD|PAD|PAD|PAD|CHN_3|  -> buffer content
        +-----+---+---+---+---+---+---+-----+

Typically the buffered data is found in raw format (unscaled with no offset
applied), however there are corner cases in which the buffered data may be found
in a processed form. Please note that these corner cases are not addressed by
this documentation.

Please see Documentation/ABI/testing/sysfs-bus-iio for a complete
description of the attributes.
