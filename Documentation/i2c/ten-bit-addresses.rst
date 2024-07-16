=====================
I2C Ten-bit Addresses
=====================

The I2C protocol knows about two kinds of device addresses: normal 7 bit
addresses, and an extended set of 10 bit addresses. The sets of addresses
do not intersect: the 7 bit address 0x10 is not the same as the 10 bit
address 0x10 (though a single device could respond to both of them).
To avoid ambiguity, the user sees 10 bit addresses mapped to a different
address space, namely 0xa000-0xa3ff. The leading 0xa (= 10) represents the
10 bit mode. This is used for creating device names in sysfs. It is also
needed when instantiating 10 bit devices via the new_device file in sysfs.

I2C messages to and from 10-bit address devices have a different format.
See the I2C specification for the details.

The current 10 bit address support is minimal. It should work, however
you can expect some problems along the way:

* Not all bus drivers support 10-bit addresses. Some don't because the
  hardware doesn't support them (SMBus doesn't require 10-bit address
  support for example), some don't because nobody bothered adding the
  code (or it's there but not working properly.) Software implementation
  (i2c-algo-bit) is known to work.
* Some optional features do not support 10-bit addresses. This is the
  case of automatic detection and instantiation of devices by their,
  drivers, for example.
* Many user-space packages (for example i2c-tools) lack support for
  10-bit addresses.

Note that 10-bit address devices are still pretty rare, so the limitations
listed above could stay for a long time, maybe even forever if nobody
needs them to be fixed.
