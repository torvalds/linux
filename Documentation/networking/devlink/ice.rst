.. SPDX-License-Identifier: GPL-2.0

===================
ice devlink support
===================

This document describes the devlink features implemented by the ``ice``
device driver.

Parameters
==========

.. list-table:: Generic parameters implemented

   * - Name
     - Mode
     - Notes
   * - ``enable_roce``
     - runtime
     - mutually exclusive with ``enable_iwarp``
   * - ``enable_iwarp``
     - runtime
     - mutually exclusive with ``enable_roce``

Info versions
=============

The ``ice`` driver reports the following versions

.. list-table:: devlink info versions implemented
    :widths: 5 5 5 90

    * - Name
      - Type
      - Example
      - Description
    * - ``board.id``
      - fixed
      - K65390-000
      - The Product Board Assembly (PBA) identifier of the board.
    * - ``fw.mgmt``
      - running
      - 2.1.7
      - 3-digit version number of the management firmware running on the
        Embedded Management Processor of the device. It controls the PHY,
        link, access to device resources, etc. Intel documentation refers to
        this as the EMP firmware.
    * - ``fw.mgmt.api``
      - running
      - 1.5.1
      - 3-digit version number (major.minor.patch) of the API exported over
        the AdminQ by the management firmware. Used by the driver to
        identify what commands are supported. Historical versions of the
        kernel only displayed a 2-digit version number (major.minor).
    * - ``fw.mgmt.build``
      - running
      - 0x305d955f
      - Unique identifier of the source for the management firmware.
    * - ``fw.undi``
      - running
      - 1.2581.0
      - Version of the Option ROM containing the UEFI driver. The version is
        reported in ``major.minor.patch`` format. The major version is
        incremented whenever a major breaking change occurs, or when the
        minor version would overflow. The minor version is incremented for
        non-breaking changes and reset to 1 when the major version is
        incremented. The patch version is normally 0 but is incremented when
        a fix is delivered as a patch against an older base Option ROM.
    * - ``fw.psid.api``
      - running
      - 0.80
      - Version defining the format of the flash contents.
    * - ``fw.bundle_id``
      - running
      - 0x80002ec0
      - Unique identifier of the firmware image file that was loaded onto
        the device. Also referred to as the EETRACK identifier of the NVM.
    * - ``fw.app.name``
      - running
      - ICE OS Default Package
      - The name of the DDP package that is active in the device. The DDP
        package is loaded by the driver during initialization. Each
        variation of the DDP package has a unique name.
    * - ``fw.app``
      - running
      - 1.3.1.0
      - The version of the DDP package that is active in the device. Note
        that both the name (as reported by ``fw.app.name``) and version are
        required to uniquely identify the package.
    * - ``fw.app.bundle_id``
      - running
      - 0xc0000001
      - Unique identifier for the DDP package loaded in the device. Also
        referred to as the DDP Track ID. Can be used to uniquely identify
        the specific DDP package.
    * - ``fw.netlist``
      - running
      - 1.1.2000-6.7.0
      - The version of the netlist module. This module defines the device's
        Ethernet capabilities and default settings, and is used by the
        management firmware as part of managing link and device
        connectivity.
    * - ``fw.netlist.build``
      - running
      - 0xee16ced7
      - The first 4 bytes of the hash of the netlist module contents.

Flash Update
============

The ``ice`` driver implements support for flash update using the
``devlink-flash`` interface. It supports updating the device flash using a
combined flash image that contains the ``fw.mgmt``, ``fw.undi``, and
``fw.netlist`` components.

.. list-table:: List of supported overwrite modes
   :widths: 5 95

   * - Bits
     - Behavior
   * - ``DEVLINK_FLASH_OVERWRITE_SETTINGS``
     - Do not preserve settings stored in the flash components being
       updated. This includes overwriting the port configuration that
       determines the number of physical functions the device will
       initialize with.
   * - ``DEVLINK_FLASH_OVERWRITE_SETTINGS`` and ``DEVLINK_FLASH_OVERWRITE_IDENTIFIERS``
     - Do not preserve either settings or identifiers. Overwrite everything
       in the flash with the contents from the provided image, without
       performing any preservation. This includes overwriting device
       identifying fields such as the MAC address, VPD area, and device
       serial number. It is expected that this combination be used with an
       image customized for the specific device.

The ice hardware does not support overwriting only identifiers while
preserving settings, and thus ``DEVLINK_FLASH_OVERWRITE_IDENTIFIERS`` on its
own will be rejected. If no overwrite mask is provided, the firmware will be
instructed to preserve all settings and identifying fields when updating.

Reload
======

The ``ice`` driver supports activating new firmware after a flash update
using ``DEVLINK_CMD_RELOAD`` with the ``DEVLINK_RELOAD_ACTION_FW_ACTIVATE``
action.

.. code:: shell

    $ devlink dev reload pci/0000:01:00.0 reload action fw_activate

The new firmware is activated by issuing a device specific Embedded
Management Processor reset which requests the device to reset and reload the
EMP firmware image.

The driver does not currently support reloading the driver via
``DEVLINK_RELOAD_ACTION_DRIVER_REINIT``.

Port split
==========

The ``ice`` driver supports port splitting only for port 0, as the FW has
a predefined set of available port split options for the whole device.

A system reboot is required for port split to be applied.

The following command will select the port split option with 4 ports:

.. code:: shell

    $ devlink port split pci/0000:16:00.0/0 count 4

The list of all available port options will be printed to dynamic debug after
each ``split`` and ``unsplit`` command. The first option is the default.

.. code:: shell

    ice 0000:16:00.0: Available port split options and max port speeds (Gbps):
    ice 0000:16:00.0: Status  Split      Quad 0          Quad 1
    ice 0000:16:00.0:         count  L0  L1  L2  L3  L4  L5  L6  L7
    ice 0000:16:00.0: Active  2     100   -   -   - 100   -   -   -
    ice 0000:16:00.0:         2      50   -  50   -   -   -   -   -
    ice 0000:16:00.0: Pending 4      25  25  25  25   -   -   -   -
    ice 0000:16:00.0:         4      25  25   -   -  25  25   -   -
    ice 0000:16:00.0:         8      10  10  10  10  10  10  10  10
    ice 0000:16:00.0:         1     100   -   -   -   -   -   -   -

There could be multiple FW port options with the same port split count. When
the same port split count request is issued again, the next FW port option with
the same port split count will be selected.

``devlink port unsplit`` will select the option with a split count of 1. If
there is no FW option available with split count 1, you will receive an error.

Regions
=======

The ``ice`` driver implements the following regions for accessing internal
device data.

.. list-table:: regions implemented
    :widths: 15 85

    * - Name
      - Description
    * - ``nvm-flash``
      - The contents of the entire flash chip, sometimes referred to as
        the device's Non Volatile Memory.
    * - ``shadow-ram``
      - The contents of the Shadow RAM, which is loaded from the beginning
        of the flash. Although the contents are primarily from the flash,
        this area also contains data generated during device boot which is
        not stored in flash.
    * - ``device-caps``
      - The contents of the device firmware's capabilities buffer. Useful to
        determine the current state and configuration of the device.

Both the ``nvm-flash`` and ``shadow-ram`` regions can be accessed without a
snapshot. The ``device-caps`` region requires a snapshot as the contents are
sent by firmware and can't be split into separate reads.

Users can request an immediate capture of a snapshot for all three regions
via the ``DEVLINK_CMD_REGION_NEW`` command.

.. code:: shell

    $ devlink region show
    pci/0000:01:00.0/nvm-flash: size 10485760 snapshot [] max 1
    pci/0000:01:00.0/device-caps: size 4096 snapshot [] max 10

    $ devlink region new pci/0000:01:00.0/nvm-flash snapshot 1
    $ devlink region dump pci/0000:01:00.0/nvm-flash snapshot 1

    $ devlink region dump pci/0000:01:00.0/nvm-flash snapshot 1
    0000000000000000 0014 95dc 0014 9514 0035 1670 0034 db30
    0000000000000010 0000 0000 ffff ff04 0029 8c00 0028 8cc8
    0000000000000020 0016 0bb8 0016 1720 0000 0000 c00f 3ffc
    0000000000000030 bada cce5 bada cce5 bada cce5 bada cce5

    $ devlink region read pci/0000:01:00.0/nvm-flash snapshot 1 address 0 length 16
    0000000000000000 0014 95dc 0014 9514 0035 1670 0034 db30

    $ devlink region delete pci/0000:01:00.0/nvm-flash snapshot 1

    $ devlink region new pci/0000:01:00.0/device-caps snapshot 1
    $ devlink region dump pci/0000:01:00.0/device-caps snapshot 1
    0000000000000000 01 00 01 00 00 00 00 00 01 00 00 00 00 00 00 00
    0000000000000010 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000020 02 00 02 01 32 03 00 00 0a 00 00 00 25 00 00 00
    0000000000000030 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000040 04 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    0000000000000050 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000060 05 00 01 00 03 00 00 00 00 00 00 00 00 00 00 00
    0000000000000070 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000080 06 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    0000000000000090 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000000a0 08 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000000b0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000000c0 12 00 01 00 01 00 00 00 01 00 01 00 00 00 00 00
    00000000000000d0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000000e0 13 00 01 00 00 01 00 00 00 00 00 00 00 00 00 00
    00000000000000f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000100 14 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    0000000000000110 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000120 15 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    0000000000000130 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000140 16 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    0000000000000150 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000160 17 00 01 00 06 00 00 00 00 00 00 00 00 00 00 00
    0000000000000170 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000180 18 00 01 00 01 00 00 00 01 00 00 00 08 00 00 00
    0000000000000190 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000001a0 22 00 01 00 01 00 00 00 00 00 00 00 00 00 00 00
    00000000000001b0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000001c0 40 00 01 00 00 08 00 00 08 00 00 00 00 00 00 00
    00000000000001d0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00000000000001e0 41 00 01 00 00 08 00 00 00 00 00 00 00 00 00 00
    00000000000001f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0000000000000200 42 00 01 00 00 08 00 00 00 00 00 00 00 00 00 00
    0000000000000210 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    $ devlink region delete pci/0000:01:00.0/device-caps snapshot 1

Devlink Rate
============

The ``ice`` driver implements devlink-rate API. It allows for offload of
the Hierarchical QoS to the hardware. It enables user to group Virtual
Functions in a tree structure and assign supported parameters: tx_share,
tx_max, tx_priority and tx_weight to each node in a tree. So effectively
user gains an ability to control how much bandwidth is allocated for each
VF group. This is later enforced by the HW.

It is assumed that this feature is mutually exclusive with DCB performed
in FW and ADQ, or any driver feature that would trigger changes in QoS,
for example creation of the new traffic class. The driver will prevent DCB
or ADQ configuration if user started making any changes to the nodes using
devlink-rate API. To configure those features a driver reload is necessary.
Correspondingly if ADQ or DCB will get configured the driver won't export
hierarchy at all, or will remove the untouched hierarchy if those
features are enabled after the hierarchy is exported, but before any
changes are made.

This feature is also dependent on switchdev being enabled in the system.
It's required because devlink-rate requires devlink-port objects to be
present, and those objects are only created in switchdev mode.

If the driver is set to the switchdev mode, it will export internal
hierarchy the moment VF's are created. Root of the tree is always
represented by the node_0. This node can't be deleted by the user. Leaf
nodes and nodes with children also can't be deleted.

.. list-table:: Attributes supported
    :widths: 15 85

    * - Name
      - Description
    * - ``tx_max``
      - maximum bandwidth to be consumed by the tree Node. Rate Limit is
        an absolute number specifying a maximum amount of bytes a Node may
        consume during the course of one second. Rate limit guarantees
        that a link will not oversaturate the receiver on the remote end
        and also enforces an SLA between the subscriber and network
        provider.
    * - ``tx_share``
      - minimum bandwidth allocated to a tree node when it is not blocked.
        It specifies an absolute BW. While tx_max defines the maximum
        bandwidth the node may consume, the tx_share marks committed BW
        for the Node.
    * - ``tx_priority``
      - allows for usage of strict priority arbiter among siblings. This
        arbitration scheme attempts to schedule nodes based on their
        priority as long as the nodes remain within their bandwidth limit.
        Range 0-7. Nodes with priority 7 have the highest priority and are
        selected first, while nodes with priority 0 have the lowest
        priority. Nodes that have the same priority are treated equally.
    * - ``tx_weight``
      - allows for usage of Weighted Fair Queuing arbitration scheme among
        siblings. This arbitration scheme can be used simultaneously with
        the strict priority. Range 1-200. Only relative values matter for
        arbitration.

``tx_priority`` and ``tx_weight`` can be used simultaneously. In that case
nodes with the same priority form a WFQ subgroup in the sibling group
and arbitration among them is based on assigned weights.

.. code:: shell

    # enable switchdev
    $ devlink dev eswitch set pci/0000:4b:00.0 mode switchdev

    # at this point driver should export internal hierarchy
    $ echo 2 > /sys/class/net/ens785np0/device/sriov_numvfs

    $ devlink port function rate show
    pci/0000:4b:00.0/node_25: type node parent node_24
    pci/0000:4b:00.0/node_24: type node parent node_0
    pci/0000:4b:00.0/node_32: type node parent node_31
    pci/0000:4b:00.0/node_31: type node parent node_30
    pci/0000:4b:00.0/node_30: type node parent node_16
    pci/0000:4b:00.0/node_19: type node parent node_18
    pci/0000:4b:00.0/node_18: type node parent node_17
    pci/0000:4b:00.0/node_17: type node parent node_16
    pci/0000:4b:00.0/node_14: type node parent node_5
    pci/0000:4b:00.0/node_5: type node parent node_3
    pci/0000:4b:00.0/node_13: type node parent node_4
    pci/0000:4b:00.0/node_12: type node parent node_4
    pci/0000:4b:00.0/node_11: type node parent node_4
    pci/0000:4b:00.0/node_10: type node parent node_4
    pci/0000:4b:00.0/node_9: type node parent node_4
    pci/0000:4b:00.0/node_8: type node parent node_4
    pci/0000:4b:00.0/node_7: type node parent node_4
    pci/0000:4b:00.0/node_6: type node parent node_4
    pci/0000:4b:00.0/node_4: type node parent node_3
    pci/0000:4b:00.0/node_3: type node parent node_16
    pci/0000:4b:00.0/node_16: type node parent node_15
    pci/0000:4b:00.0/node_15: type node parent node_0
    pci/0000:4b:00.0/node_2: type node parent node_1
    pci/0000:4b:00.0/node_1: type node parent node_0
    pci/0000:4b:00.0/node_0: type node
    pci/0000:4b:00.0/1: type leaf parent node_25
    pci/0000:4b:00.0/2: type leaf parent node_25

    # let's create some custom node
    $ devlink port function rate add pci/0000:4b:00.0/node_custom parent node_0

    # second custom node
    $ devlink port function rate add pci/0000:4b:00.0/node_custom_1 parent node_custom

    # reassign second VF to newly created branch
    $ devlink port function rate set pci/0000:4b:00.0/2 parent node_custom_1

    # assign tx_weight to the VF
    $ devlink port function rate set pci/0000:4b:00.0/2 tx_weight 5

    # assign tx_share to the VF
    $ devlink port function rate set pci/0000:4b:00.0/2 tx_share 500Mbps
