.. SPDX-License-Identifier: GPL-2.0

==============
Devlink Region
==============

``devlink`` regions enable access to driver defined address regions using
devlink.

Each device can create and register its own supported address regions. The
region can then be accessed via the devlink region interface.

Region snapshots are collected by the driver, and can be accessed via read
or dump commands. This allows future analysis on the created snapshots.
Regions may optionally support triggering snapshots on demand.

The major benefit to creating a region is to provide access to internal
address regions that are otherwise inaccessible to the user.

Regions may also be used to provide an additional way to debug complex error
states, but see also :doc:`devlink-health`

example usage
-------------

.. code:: shell

    $ devlink region help
    $ devlink region show [ DEV/REGION ]
    $ devlink region del DEV/REGION snapshot SNAPSHOT_ID
    $ devlink region dump DEV/REGION [ snapshot SNAPSHOT_ID ]
    $ devlink region read DEV/REGION [ snapshot SNAPSHOT_ID ]
            address ADDRESS length length

    # Show all of the exposed regions with region sizes:
    $ devlink region show
    pci/0000:00:05.0/cr-space: size 1048576 snapshot [1 2]
    pci/0000:00:05.0/fw-health: size 64 snapshot [1 2]

    # Delete a snapshot using:
    $ devlink region del pci/0000:00:05.0/cr-space snapshot 1

    # Trigger (request) a snapshot be taken:
    $ devlink region trigger pci/0000:00:05.0/cr-space

    # Dump a snapshot:
    $ devlink region dump pci/0000:00:05.0/fw-health snapshot 1
    0000000000000000 0014 95dc 0014 9514 0035 1670 0034 db30
    0000000000000010 0000 0000 ffff ff04 0029 8c00 0028 8cc8
    0000000000000020 0016 0bb8 0016 1720 0000 0000 c00f 3ffc
    0000000000000030 bada cce5 bada cce5 bada cce5 bada cce5

    # Read a specific part of a snapshot:
    $ devlink region read pci/0000:00:05.0/fw-health snapshot 1 address 0
            length 16
    0000000000000000 0014 95dc 0014 9514 0035 1670 0034 db30

As regions are likely very device or driver specific, no generic regions are
defined. See the driver-specific documentation files for information on the
specific regions a driver supports.
