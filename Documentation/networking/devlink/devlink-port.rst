.. SPDX-License-Identifier: GPL-2.0

.. _devlink_port:

============
Devlink Port
============

``devlink-port`` is a port that exists on the device. It has a logically
separate ingress/egress point of the device. A devlink port can be any one
of many flavours. A devlink port flavour along with port attributes
describe what a port represents.

A device driver that intends to publish a devlink port sets the
devlink port attributes and registers the devlink port.

Devlink port flavours are described below.

.. list-table:: List of devlink port flavours
   :widths: 33 90

   * - Flavour
     - Description
   * - ``DEVLINK_PORT_FLAVOUR_PHYSICAL``
     - Any kind of physical port. This can be an eswitch physical port or any
       other physical port on the device.
   * - ``DEVLINK_PORT_FLAVOUR_DSA``
     - This indicates a DSA interconnect port.
   * - ``DEVLINK_PORT_FLAVOUR_CPU``
     - This indicates a CPU port applicable only to DSA.
   * - ``DEVLINK_PORT_FLAVOUR_PCI_PF``
     - This indicates an eswitch port representing a port of PCI
       physical function (PF).
   * - ``DEVLINK_PORT_FLAVOUR_PCI_VF``
     - This indicates an eswitch port representing a port of PCI
       virtual function (VF).
   * - ``DEVLINK_PORT_FLAVOUR_VIRTUAL``
     - This indicates a virtual port for the PCI virtual function.

Devlink port can have a different type based on the link layer described below.

.. list-table:: List of devlink port types
   :widths: 23 90

   * - Type
     - Description
   * - ``DEVLINK_PORT_TYPE_ETH``
     - Driver should set this port type when a link layer of the port is
       Ethernet.
   * - ``DEVLINK_PORT_TYPE_IB``
     - Driver should set this port type when a link layer of the port is
       InfiniBand.
   * - ``DEVLINK_PORT_TYPE_AUTO``
     - This type is indicated by the user when driver should detect the port
       type automatically.

PCI controllers
---------------
In most cases a PCI device has only one controller. A controller consists of
potentially multiple physical and virtual functions. A function consists
of one or more ports. This port is represented by the devlink eswitch port.

A PCI device connected to multiple CPUs or multiple PCI root complexes or a
SmartNIC, however, may have multiple controllers. For a device with multiple
controllers, each controller is distinguished by a unique controller number.
An eswitch is on the PCI device which supports ports of multiple controllers.

An example view of a system with two controllers::

                 ---------------------------------------------------------
                 |                                                       |
                 |           --------- ---------         ------- ------- |
    -----------  |           | vf(s) | | sf(s) |         |vf(s)| |sf(s)| |
    | server  |  | -------   ----/---- ---/----- ------- ---/--- ---/--- |
    | pci rc  |=== | pf0 |______/________/       | pf1 |___/_______/     |
    | connect |  | -------                       -------                 |
    -----------  |     | controller_num=1 (no eswitch)                   |
                 ------|--------------------------------------------------
                 (internal wire)
                       |
                 ---------------------------------------------------------
                 | devlink eswitch ports and reps                        |
                 | ----------------------------------------------------- |
                 | |ctrl-0 | ctrl-0 | ctrl-0 | ctrl-0 | ctrl-0 |ctrl-0 | |
                 | |pf0    | pf0vfN | pf0sfN | pf1    | pf1vfN |pf1sfN | |
                 | ----------------------------------------------------- |
                 | |ctrl-1 | ctrl-1 | ctrl-1 | ctrl-1 | ctrl-1 |ctrl-1 | |
                 | |pf0    | pf0vfN | pf0sfN | pf1    | pf1vfN |pf1sfN | |
                 | ----------------------------------------------------- |
                 |                                                       |
                 |                                                       |
    -----------  |           --------- ---------         ------- ------- |
    | smartNIC|  |           | vf(s) | | sf(s) |         |vf(s)| |sf(s)| |
    | pci rc  |==| -------   ----/---- ---/----- ------- ---/--- ---/--- |
    | connect |  | | pf0 |______/________/       | pf1 |___/_______/     |
    -----------  | -------                       -------                 |
                 |                                                       |
                 |  local controller_num=0 (eswitch)                     |
                 ---------------------------------------------------------

In the above example, the external controller (identified by controller number = 1)
doesn't have the eswitch. Local controller (identified by controller number = 0)
has the eswitch. The Devlink instance on the local controller has eswitch
devlink ports for both the controllers.

Function configuration
======================

A user can configure the function attribute before enumerating the PCI
function. Usually it means, user should configure function attribute
before a bus specific device for the function is created. However, when
SRIOV is enabled, virtual function devices are created on the PCI bus.
Hence, function attribute should be configured before binding virtual
function device to the driver.

A user may set the hardware address of the function using
'devlink port function set hw_addr' command. For Ethernet port function
this means a MAC address.
