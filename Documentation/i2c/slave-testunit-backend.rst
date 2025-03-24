.. SPDX-License-Identifier: GPL-2.0

================================
Linux I2C slave testunit backend
================================

by Wolfram Sang <wsa@sang-engineering.com> in 2020

This backend can be used to trigger test cases for I2C bus masters which
require a remote device with certain capabilities (and which are usually not so
easy to obtain). Examples include multi-master testing, and SMBus Host Notify
testing. For some tests, the I2C slave controller must be able to switch
between master and slave mode because it needs to send data, too.

Note that this is a device for testing and debugging. It should not be enabled
in a production build. And while there is some versioning and we try hard to
keep backward compatibility, there is no stable ABI guaranteed!

Instantiating the device is regular. Example for bus 0, address 0x30::

  # echo "slave-testunit 0x1030" > /sys/bus/i2c/devices/i2c-0/new_device

Or using firmware nodes. Here is a devicetree example (note this is only a
debug device, so there are no official DT bindings)::

  &i2c0	{
        ...

	testunit@30 {
		compatible = "slave-testunit";
		reg = <(0x30 | I2C_OWN_SLAVE_ADDRESS)>;
	};
  };

After that, you will have the device listening. Reading will return a single
byte. Its value is 0 if the testunit is idle, otherwise the command number of
the currently running command.

When writing, the device consists of 4 8-bit registers and, except for some
"partial" commands, all registers must be written to start a testcase, i.e. you
usually write 4 bytes to the device. The registers are:

.. csv-table::
  :header: "Offset", "Name", "Description"

  0x00, CMD, which test to trigger
  0x01, DATAL, configuration byte 1 for the test
  0x02, DATAH, configuration byte 2 for the test
  0x03, DELAY, delay in n * 10ms until test is started

Using 'i2cset' from the i2c-tools package, the generic command looks like::

  # i2cset -y <bus_num> <testunit_address> <CMD> <DATAL> <DATAH> <DELAY> i

DELAY is a generic parameter which will delay the execution of the test in CMD.
While a command is running (including the delay), new commands will not be
acknowledged. You need to wait until the old one is completed.

The commands are described in the following section. An invalid command will
result in the transfer not being acknowledged.

Commands
--------

0x00 NOOP
~~~~~~~~~

Reserved for future use.

0x01 READ_BYTES
~~~~~~~~~~~~~~~

.. list-table::
  :header-rows: 1

  * - CMD
    - DATAL
    - DATAH
    - DELAY

  * - 0x01
    - address to read data from (lower 7 bits, highest bit currently unused)
    - number of bytes to read
    - n * 10ms

Also needs master mode. This is useful to test if your bus master driver is
handling multi-master correctly. You can trigger the testunit to read bytes
from another device on the bus. If the bus master under test also wants to
access the bus at the same time, the bus will be busy. Example to read 128
bytes from device 0x50 after 50ms of delay::

  # i2cset -y 0 0x30 1 0x50 0x80 5 i

0x02 SMBUS_HOST_NOTIFY
~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
  :header-rows: 1

  * - CMD
    - DATAL
    - DATAH
    - DELAY

  * - 0x02
    - low byte of the status word to send
    - high byte of the status word to send
    - n * 10ms

Also needs master mode. This test will send an SMBUS_HOST_NOTIFY message to the
host. Note that the status word is currently ignored in the Linux Kernel.
Example to send a notification with status word 0x6442 after 10ms::

  # i2cset -y 0 0x30 2 0x42 0x64 1 i

If the host controller supports HostNotify, this message with debug level
should appear (Linux 6.11 and later)::

  Detected HostNotify from address 0x30

0x03 SMBUS_BLOCK_PROC_CALL
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
  :header-rows: 1

  * - CMD
    - DATAL
    - DATAH
    - DELAY

  * - 0x03
    - 0x01 (i.e. one further byte will be written)
    - number of bytes to be sent back
    - leave out, partial command!

Partial command. This test will respond to a block process call as defined by
the SMBus specification. The one data byte written specifies how many bytes
will be sent back in the following read transfer. Note that in this read
transfer, the testunit will prefix the length of the bytes to follow. So, if
your host bus driver emulates SMBus calls like the majority does, it needs to
support the I2C_M_RECV_LEN flag of an i2c_msg. This is a good testcase for it.
The returned data consists of the length first, and then of an array of bytes
from length-1 to 0. Here is an example which emulates
i2c_smbus_block_process_call() using i2ctransfer (you need i2c-tools v4.2 or
later)::

  # i2ctransfer -y 0 w3@0x30 3 1 0x10 r?
  0x10 0x0f 0x0e 0x0d 0x0c 0x0b 0x0a 0x09 0x08 0x07 0x06 0x05 0x04 0x03 0x02 0x01 0x00

0x04 GET_VERSION_WITH_REP_START
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
  :header-rows: 1

  * - CMD
    - DATAL
    - DATAH
    - DELAY

  * - 0x04
    - currently unused
    - currently unused
    - leave out, partial command!

Partial command. After sending this command, the testunit will reply to a read
message with a NUL terminated version string based on UTS_RELEASE. The first
character is always a 'v' and the length of the version string is at maximum
128 bytes. However, it will only respond if the read message is connected to
the write message via repeated start. If your controller driver handles
repeated start correctly, this will work::

  # i2ctransfer -y 0 w3@0x30 4 0 0 r128
  0x76 0x36 0x2e 0x31 0x31 0x2e 0x30 0x2d 0x72 0x63 0x31 0x2d 0x30 0x30 0x30 0x30 ...

If you have i2c-tools 4.4 or later, you can print out the data right away::

  # i2ctransfer -y -b 0 w3@0x30 4 0 0 r128
  v6.11.0-rc1-00009-gd37a1b4d3fd0

STOP/START combinations between the two messages will *not* work because they
are not equivalent to a REPEATED START. As an example, this returns just the
default response::

  # i2cset -y 0 0x30 4 0 0 i; i2cget -y 0 0x30
  0x00

0x05 SMBUS_ALERT_REQUEST
~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
  :header-rows: 1

  * - CMD
    - DATAL
    - DATAH
    - DELAY

  * - 0x05
    - response value (7 MSBs interpreted as I2C address)
    - currently unused
    - n * 10ms

This test raises an interrupt via the SMBAlert pin which the host controller
must handle. The pin must be connected to the testunit as a GPIO. GPIO access
is not allowed to sleep. Currently, this can only be described using firmware
nodes. So, for devicetree, you would add something like this to the testunit
node::

  gpios = <&gpio1 24 GPIO_ACTIVE_LOW>;

The following command will trigger the alert with a response of 0xc9 after 1
second of delay::

  # i2cset -y 0 0x30 5 0xc9 0x00 100 i

If the host controller supports SMBusAlert, this message with debug level
should appear::

  smbus_alert 0-000c: SMBALERT# from dev 0x64, flag 1

This message may appear more than once because the testunit is software not
hardware and, thus, may not be able to react to the response of the host fast
enough. The interrupt count should increase only by one, though::

  # cat /proc/interrupts | grep smbus_alert
   93:          1  gpio-rcar  26 Edge      smbus_alert

If the host does not respond to the alert within 1 second, the test will be
aborted and the testunit will report an error.

For this test, the testunit will shortly drop its assigned address and listen
on the SMBus Alert Response Address (0x0c). It will reassign its original
address afterwards.
