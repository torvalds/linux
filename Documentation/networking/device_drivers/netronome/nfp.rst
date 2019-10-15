.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

=============================================
Netronome Flow Processor (NFP) Kernel Drivers
=============================================

Copyright (c) 2019, Netronome Systems, Inc.

Contents
========

- `Overview`_
- `Acquiring Firmware`_

Overview
========

This driver supports Netronome's line of Flow Processor devices,
including the NFP4000, NFP5000, and NFP6000 models, which are also
incorporated in the company's family of Agilio SmartNICs. The SR-IOV
physical and virtual functions for these devices are supported by
the driver.

Acquiring Firmware
==================

The NFP4000 and NFP6000 devices require application specific firmware
to function.  Application firmware can be located either on the host file system
or in the device flash (if supported by management firmware).

Firmware files on the host filesystem contain card type (`AMDA-*` string), media
config etc.  They should be placed in `/lib/firmware/netronome` directory to
load firmware from the host file system.

Firmware for basic NIC operation is available in the upstream
`linux-firmware.git` repository.

Firmware in NVRAM
-----------------

Recent versions of management firmware supports loading application
firmware from flash when the host driver gets probed.  The firmware loading
policy configuration may be used to configure this feature appropriately.

Devlink or ethtool can be used to update the application firmware on the device
flash by providing the appropriate `nic_AMDA*.nffw` file to the respective
command.  Users need to take care to write the correct firmware image for the
card and media configuration to flash.

Available storage space in flash depends on the card being used.

Dealing with multiple projects
------------------------------

NFP hardware is fully programmable therefore there can be different
firmware images targeting different applications.

When using application firmware from host, we recommend placing
actual firmware files in application-named subdirectories in
`/lib/firmware/netronome` and linking the desired files, e.g.::

    $ tree /lib/firmware/netronome/
    /lib/firmware/netronome/
    ├── bpf
    │   ├── nic_AMDA0081-0001_1x40.nffw
    │   └── nic_AMDA0081-0001_4x10.nffw
    ├── flower
    │   ├── nic_AMDA0081-0001_1x40.nffw
    │   └── nic_AMDA0081-0001_4x10.nffw
    ├── nic
    │   ├── nic_AMDA0081-0001_1x40.nffw
    │   └── nic_AMDA0081-0001_4x10.nffw
    ├── nic_AMDA0081-0001_1x40.nffw -> bpf/nic_AMDA0081-0001_1x40.nffw
    └── nic_AMDA0081-0001_4x10.nffw -> bpf/nic_AMDA0081-0001_4x10.nffw

    3 directories, 8 files

You may need to use hard instead of symbolic links on distributions
which use old `mkinitrd` command instead of `dracut` (e.g. Ubuntu).

After changing firmware files you may need to regenerate the initramfs
image.  Initramfs contains drivers and firmware files your system may
need to boot.  Refer to the documentation of your distribution to find
out how to update initramfs.  Good indication of stale initramfs
is system loading wrong driver or firmware on boot, but when driver is
later reloaded manually everything works correctly.

Selecting firmware per device
-----------------------------

Most commonly all cards on the system use the same type of firmware.
If you want to load specific firmware image for a specific card, you
can use either the PCI bus address or serial number.  Driver will print
which files it's looking for when it recognizes a NFP device::

    nfp: Looking for firmware file in order of priority:
    nfp:  netronome/serial-00-12-34-aa-bb-cc-10-ff.nffw: not found
    nfp:  netronome/pci-0000:02:00.0.nffw: not found
    nfp:  netronome/nic_AMDA0081-0001_1x40.nffw: found, loading...

In this case if file (or link) called *serial-00-12-34-aa-bb-5d-10-ff.nffw*
or *pci-0000:02:00.0.nffw* is present in `/lib/firmware/netronome` this
firmware file will take precedence over `nic_AMDA*` files.

Note that `serial-*` and `pci-*` files are **not** automatically included
in initramfs, you will have to refer to documentation of appropriate tools
to find out how to include them.

Firmware loading policy
-----------------------

Firmware loading policy is controlled via three HWinfo parameters
stored as key value pairs in the device flash:

app_fw_from_flash
    Defines which firmware should take precedence, 'Disk' (0), 'Flash' (1) or
    the 'Preferred' (2) firmware. When 'Preferred' is selected, the management
    firmware makes the decision over which firmware will be loaded by comparing
    versions of the flash firmware and the host supplied firmware.
    This variable is configurable using the 'fw_load_policy'
    devlink parameter.

abi_drv_reset
    Defines if the driver should reset the firmware when
    the driver is probed, either 'Disk' (0) if firmware was found on disk,
    'Always' (1) reset or 'Never' (2) reset. Note that the device is always
    reset on driver unload if firmware was loaded when the driver was probed.
    This variable is configurable using the 'reset_dev_on_drv_probe'
    devlink parameter.

abi_drv_load_ifc
    Defines a list of PF devices allowed to load FW on the device.
    This variable is not currently user configurable.
