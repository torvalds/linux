.. SPDX-License-Identifier: GPL-2.0

=========================
netdevsim devlink support
=========================

This document describes the ``devlink`` features supported by the
``netdevsim`` device driver.

Parameters
==========

.. list-table:: Generic parameters implemented

   * - Name
     - Mode
   * - ``max_macs``
     - driverinit

The ``netdevsim`` driver also implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``test1``
     - Boolean
     - driverinit
     - Test parameter used to show how a driver-specific devlink parameter
       can be implemented.

The ``netdevsim`` driver supports reloading via ``DEVLINK_CMD_RELOAD``

Regions
=======

The ``netdevsim`` driver exposes a ``dummy`` region as an example of how the
devlink-region interfaces work. A snapshot is taken whenever the
``take_snapshot`` debugfs file is written to.

Resources
=========

The ``netdevsim`` driver exposes resources to control the number of FIB
entries, FIB rule entries and nexthops that the driver will allow.

.. code:: shell

    $ devlink resource set netdevsim/netdevsim0 path /IPv4/fib size 96
    $ devlink resource set netdevsim/netdevsim0 path /IPv4/fib-rules size 16
    $ devlink resource set netdevsim/netdevsim0 path /IPv6/fib size 64
    $ devlink resource set netdevsim/netdevsim0 path /IPv6/fib-rules size 16
    $ devlink resource set netdevsim/netdevsim0 path /nexthops size 16
    $ devlink dev reload netdevsim/netdevsim0

Driver-specific Traps
=====================

.. list-table:: List of Driver-specific Traps Registered by ``netdevsim``
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fid_miss``
     - ``exception``
     - When a packet enters the device it is classified to a filtering
       indentifier (FID) based on the ingress port and VLAN. This trap is used
       to trap packets for which a FID could not be found
