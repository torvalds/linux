.. -*- coding: utf-8; mode: rst -*-

.. _lirc_write:

**************
LIRC write fop
**************

The data written to the chardev is a pulse/space sequence of integer
values. Pulses and spaces are only marked implicitly by their position.
The data must start and end with a pulse, therefore, the data must
always include an uneven number of samples. The write function must
block until the data has been transmitted by the hardware. If more data
is provided than the hardware can send, the driver returns ``EINVAL``.
