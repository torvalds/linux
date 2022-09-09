=========================
Kernel driver i2c-amd-mp2
=========================

Supported adapters:
  * AMD MP2 PCIe interface

Datasheet: not publicly available.

Authors:
	- Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
	- Nehal Shah <nehal-bakulchandra.shah@amd.com>
	- Elie Morisse <syniurge@gmail.com>

Description
-----------

The MP2 is an ARM processor programmed as an I2C controller and communicating
with the x86 host through PCI.

If you see something like this::

  03:00.7 MP2 I2C controller: Advanced Micro Devices, Inc. [AMD] Device 15e6

in your ``lspci -v``, then this driver is for your device.
