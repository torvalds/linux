=========================
Kernel driver i2c-adm8111
=========================

Supported adapters:
    * AMD-8111 SMBus 2.0 PCI interface

Datasheets:
	AMD datasheet not yet available, but almost everything can be found
	in the publicly available ACPI 2.0 specification, which the adapter
	follows.

Author: Vojtech Pavlik <vojtech@suse.cz>

Description
-----------

If you see something like this::

  00:07.2 SMBus: Advanced Micro Devices [AMD] AMD-8111 SMBus 2.0 (rev 02)
          Subsystem: Advanced Micro Devices [AMD] AMD-8111 SMBus 2.0
          Flags: medium devsel, IRQ 19
          I/O ports at d400 [size=32]

in your ``lspci -v``, then this driver is for your chipset.

Process Call Support
--------------------

Supported.

SMBus 2.0 Support
-----------------

Supported. Both PEC and block process call support is implemented. Slave
mode or host notification are not yet implemented.

Notes
-----

Note that for the 8111, there are two SMBus adapters. The SMBus 2.0 adapter
is supported by this driver, and the SMBus 1.0 adapter is supported by the
i2c-amd756 driver.
