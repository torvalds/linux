.. SPDX-License-Identifier: GPL-2.0

====================
iosm devlink support
====================

This document describes the devlink features implemented by the ``iosm``
device driver.

Parameters
==========

The ``iosm`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``erase_full_flash``
     - u8
     - runtime
     - erase_full_flash parameter is used to check if full erase is required for
       the device during firmware flashing.
       If set, Full nand erase command will be sent to the device. By default,
       only conditional erase support is enabled.


Flash Update
============

The ``iosm`` driver implements support for flash update using the
``devlink-flash`` interface.

It supports updating the device flash using a combined flash image which contains
the Bootloader images and other modem software images.

The driver uses DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT to identify type of
firmware image that need to be flashed as requested by user space application.
Supported firmware image types.

.. list-table:: Firmware Image types
    :widths: 15 85

    * - Name
      - Description
    * - ``PSI RAM``
      - Primary Signed Image
    * - ``EBL``
      - External Bootloader
    * - ``FLS``
      - Modem Software Image

PSI RAM and EBL are the RAM images which are injected to the device when the
device is in BOOT ROM stage. Once this is successful, the actual modem firmware
image is flashed to the device. The modem software image contains multiple files
each having one secure bin file and at least one Loadmap/Region file. For flashing
these files, appropriate commands are sent to the modem device along with the
data required for flashing. The data like region count and address of each region
has to be passed to the driver using the devlink param command.

If the device has to be fully erased before firmware flashing, user application
need to set the erase_full_flash parameter using devlink param command.
By default, conditional erase feature is supported.

Flash Commands:
===============
1) When modem is in Boot ROM stage, user can use below command to inject PSI RAM
image using devlink flash command.

$ devlink dev flash pci/0000:02:00.0 file <PSI_RAM_File_name>

2) If user want to do a full erase, below command need to be issued to set the
erase full flash param (To be set only if full erase required).

$ devlink dev param set pci/0000:02:00.0 name erase_full_flash value true cmode runtime

3) Inject EBL after the modem is in PSI stage.

$ devlink dev flash pci/0000:02:00.0 file <EBL_File_name>

4) Once EBL is injected successfully, then the actual firmware flashing takes
place. Below is the sequence of commands used for each of the firmware images.

a) Flash secure bin file.

$ devlink dev flash pci/0000:02:00.0 file <Secure_bin_file_name>

b) Flashing the Loadmap/Region file

$ devlink dev flash pci/0000:02:00.0 file <Load_map_file_name>

Regions
=======

The ``iosm`` driver supports dumping the coredump logs.

In case a firmware encounters an exception, a snapshot will be taken by the
driver. Following regions are accessed for device internal data.

.. list-table:: Regions implemented
    :widths: 15 85

    * - Name
      - Description
    * - ``report.json``
      - The summary of exception details logged as part of this region.
    * - ``coredump.fcd``
      - This region contains the details related to the exception occurred in the
        device (RAM dump).
    * - ``cdd.log``
      - This region contains the logs related to the modem CDD driver.
    * - ``eeprom.bin``
      - This region contains the eeprom logs.
    * - ``bootcore_trace.bin``
      -  This region contains the current instance of bootloader logs.
    * - ``bootcore_prev_trace.bin``
      - This region contains the previous instance of bootloader logs.


Region commands
===============

$ devlink region show

$ devlink region new pci/0000:02:00.0/report.json

$ devlink region dump pci/0000:02:00.0/report.json snapshot 0

$ devlink region del pci/0000:02:00.0/report.json snapshot 0

$ devlink region new pci/0000:02:00.0/coredump.fcd

$ devlink region dump pci/0000:02:00.0/coredump.fcd snapshot 1

$ devlink region del pci/0000:02:00.0/coredump.fcd snapshot 1

$ devlink region new pci/0000:02:00.0/cdd.log

$ devlink region dump pci/0000:02:00.0/cdd.log snapshot 2

$ devlink region del pci/0000:02:00.0/cdd.log snapshot 2

$ devlink region new pci/0000:02:00.0/eeprom.bin

$ devlink region dump pci/0000:02:00.0/eeprom.bin snapshot 3

$ devlink region del pci/0000:02:00.0/eeprom.bin snapshot 3

$ devlink region new pci/0000:02:00.0/bootcore_trace.bin

$ devlink region dump pci/0000:02:00.0/bootcore_trace.bin snapshot 4

$ devlink region del pci/0000:02:00.0/bootcore_trace.bin snapshot 4

$ devlink region new pci/0000:02:00.0/bootcore_prev_trace.bin

$ devlink region dump pci/0000:02:00.0/bootcore_prev_trace.bin snapshot 5

$ devlink region del pci/0000:02:00.0/bootcore_prev_trace.bin snapshot 5
