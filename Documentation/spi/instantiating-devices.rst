.. SPDX-License-Identifier: GPL-2.0

==============================
How to instantiate SPI devices
==============================

SPI devices are normally declared statically via device-tree, ACPI, or
board files. When the SPI controller is registered, these devices are
instantiated automatically by the SPI core. This is the preferred method
for any device with a proper kernel driver.

Instantiate from user-space
---------------------------

In certain cases a SPI device cannot be declared statically:

* The ``spidev`` driver, which provides raw userspace access to SPI
  buses, explicitly rejects the bare ``"spidev"`` compatible string in
  device-tree because spidev is a Linux implementation detail, not a
  hardware description. Vendor-specific compatible strings for spidev
  (e.g. ``"vendor,board-spidev"``) are also generally not accepted
  upstream. Device-tree overlays do not help here either, since the
  spidev driver performs the same compatible check regardless of how
  the DT node was loaded.

* You are developing or testing a SPI device on a development board
  where the SPI bus is exposed on expansion headers, and the connected
  device may change frequently.

For these cases, a sysfs interface is provided on each SPI host controller
(similar to the I2C ``new_device``/``delete_device`` interface described
in Documentation/i2c/instantiating-devices.rst). Two write-only
attribute files are created in every SPI host controller directory:
``new_device`` and ``delete_device``.

File ``new_device`` takes 2 to 4 parameters: the name of the SPI
device (a string), the chip select number, and optionally
``max_speed_hz`` and ``mode``::

  <modalias> <chip_select> [<max_speed_hz> [<mode>]]

The modalias is set both as the device's ``modalias`` field and as its
``driver_override``. This ensures that the device binds to the named
driver directly, bypassing the normal bus matching logic (OF, ACPI,
and ``id_table``). This is necessary because drivers like ``spidev``
deliberately exclude generic names from their ``id_table``.

If ``max_speed_hz`` is omitted or 0, ``spi_setup()`` clamps it to
the controller's maximum speed. If ``mode`` is omitted, SPI mode 0
(CPOL=0, CPHA=0) is used.

File ``delete_device`` takes a single parameter: the chip select
number. As no two devices can share a chip select on a given SPI bus,
the chip select is sufficient to uniquely identify the device.

Examples::

  # Create a spidev device on SPI bus 0, chip select 0
  echo spidev 0 > /sys/class/spi_master/spi0/new_device

  # Create with explicit clock rate and SPI mode
  echo spidev 0 10000000 3 > /sys/class/spi_master/spi0/new_device

  # Remove the device
  echo 0 > /sys/class/spi_master/spi0/delete_device

The attributes are added after the host controller and its firmware-described
devices have been registered. Their addition emits a ``change`` uevent,
allowing a udev rule to write to ``new_device`` when the interface is ready.

Limitations
^^^^^^^^^^^

Devices created through this interface have the following limitations
compared to devices declared via device-tree:

* No interrupt (IRQ) support.
* No additional properties such as ``spi-max-frequency`` DT bindings
  or controller-specific configuration.
* No platform data or software nodes.

For ``spidev`` usage these limitations are not relevant, since spidev
provides a raw byte-level interface that does not require any of these
features.

Only devices created via ``new_device`` can be removed through
``delete_device``. Devices declared via device-tree, ACPI, or board
files are not affected by this interface.
