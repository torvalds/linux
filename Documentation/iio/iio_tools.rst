.. SPDX-License-Identifier: GPL-2.0

=====================
IIO Interfacing Tools
=====================

1. Linux Kernel Tools
=====================

Linux Kernel provides some userspace tools that can be used to retrieve data
from IIO sysfs:

* lsiio: example application that provides a list of IIO devices and triggers
* iio_event_monitor: example application that reads events from an IIO device
  and prints them
* iio_generic_buffer: example application that reads data from buffer
* iio_utils: set of APIs, typically used to access sysfs files.

2. LibIIO
=========

LibIIO is a C/C++ library that provides generic access to IIO devices. The
library abstracts the low-level details of the hardware, and provides a simple
yet complete programming interface that can be used for advanced projects.

For more information about LibIIO, please see:
https://github.com/analogdevicesinc/libiio
