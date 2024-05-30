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
   * - ``DEVLINK_PORT_FLAVOUR_PCI_SF``
     - This indicates an eswitch port representing a port of PCI
       subfunction (SF).
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
potentially multiple physical, virtual functions and subfunctions. A function
consists of one or more ports. This port is represented by the devlink eswitch
port.

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

Users can configure one or more function attributes before enumerating the PCI
function. Usually it means, user should configure function attribute
before a bus specific device for the function is created. However, when
SRIOV is enabled, virtual function devices are created on the PCI bus.
Hence, function attribute should be configured before binding virtual
function device to the driver. For subfunctions, this means user should
configure port function attribute before activating the port function.

A user may set the hardware address of the function using
`devlink port function set hw_addr` command. For Ethernet port function
this means a MAC address.

Users may also set the RoCE capability of the function using
`devlink port function set roce` command.

Users may also set the function as migratable using
`devlink port function set migratable` command.

Users may also set the IPsec crypto capability of the function using
`devlink port function set ipsec_crypto` command.

Users may also set the IPsec packet capability of the function using
`devlink port function set ipsec_packet` command.

Users may also set the maximum IO event queues of the function
using `devlink port function set max_io_eqs` command.

Function attributes
===================

MAC address setup
-----------------
The configured MAC address of the PCI VF/SF will be used by netdevice and rdma
device created for the PCI VF/SF.

- Get the MAC address of the VF identified by its unique devlink port index::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
      function:
        hw_addr 00:00:00:00:00:00

- Set the MAC address of the VF identified by its unique devlink port index::

    $ devlink port function set pci/0000:06:00.0/2 hw_addr 00:11:22:33:44:55

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
      function:
        hw_addr 00:11:22:33:44:55

- Get the MAC address of the SF identified by its unique devlink port index::

    $ devlink port show pci/0000:06:00.0/32768
    pci/0000:06:00.0/32768: type eth netdev enp6s0pf0sf88 flavour pcisf pfnum 0 sfnum 88
      function:
        hw_addr 00:00:00:00:00:00

- Set the MAC address of the SF identified by its unique devlink port index::

    $ devlink port function set pci/0000:06:00.0/32768 hw_addr 00:00:00:00:88:88

    $ devlink port show pci/0000:06:00.0/32768
    pci/0000:06:00.0/32768: type eth netdev enp6s0pf0sf88 flavour pcisf pfnum 0 sfnum 88
      function:
        hw_addr 00:00:00:00:88:88

RoCE capability setup
---------------------
Not all PCI VFs/SFs require RoCE capability.

When RoCE capability is disabled, it saves system memory per PCI VF/SF.

When user disables RoCE capability for a VF/SF, user application cannot send or
receive any RoCE packets through this VF/SF and RoCE GID table for this PCI
will be empty.

When RoCE capability is disabled in the device using port function attribute,
VF/SF driver cannot override it.

- Get RoCE capability of the VF device::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 roce enable

- Set RoCE capability of the VF device::

    $ devlink port function set pci/0000:06:00.0/2 roce disable

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 roce disable

migratable capability setup
---------------------------
Live migration is the process of transferring a live virtual machine
from one physical host to another without disrupting its normal
operation.

User who want PCI VFs to be able to perform live migration need to
explicitly enable the VF migratable capability.

When user enables migratable capability for a VF, and the HV binds the VF to VFIO driver
with migration support, the user can migrate the VM with this VF from one HV to a
different one.

However, when migratable capability is enable, device will disable features which cannot
be migrated. Thus migratable cap can impose limitations on a VF so let the user decide.

Example of LM with migratable function configuration:
- Get migratable capability of the VF device::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 migratable disable

- Set migratable capability of the VF device::

    $ devlink port function set pci/0000:06:00.0/2 migratable enable

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 migratable enable

- Bind VF to VFIO driver with migration support::

    $ echo <pci_id> > /sys/bus/pci/devices/0000:08:00.0/driver/unbind
    $ echo mlx5_vfio_pci > /sys/bus/pci/devices/0000:08:00.0/driver_override
    $ echo <pci_id> > /sys/bus/pci/devices/0000:08:00.0/driver/bind

Attach VF to the VM.
Start the VM.
Perform live migration.

IPsec crypto capability setup
-----------------------------
When user enables IPsec crypto capability for a VF, user application can offload
XFRM state crypto operation (Encrypt/Decrypt) to this VF.

When IPsec crypto capability is disabled (default) for a VF, the XFRM state is
processed in software by the kernel.

- Get IPsec crypto capability of the VF device::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_crypto disabled

- Set IPsec crypto capability of the VF device::

    $ devlink port function set pci/0000:06:00.0/2 ipsec_crypto enable

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_crypto enabled

IPsec packet capability setup
-----------------------------
When user enables IPsec packet capability for a VF, user application can offload
XFRM state and policy crypto operation (Encrypt/Decrypt) to this VF, as well as
IPsec encapsulation.

When IPsec packet capability is disabled (default) for a VF, the XFRM state and
policy is processed in software by the kernel.

- Get IPsec packet capability of the VF device::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_packet disabled

- Set IPsec packet capability of the VF device::

    $ devlink port function set pci/0000:06:00.0/2 ipsec_packet enable

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_packet enabled

Maximum IO events queues setup
------------------------------
When user sets maximum number of IO event queues for a SF or
a VF, such function driver is limited to consume only enforced
number of IO event queues.

IO event queues deliver events related to IO queues, including network
device transmit and receive queues (txq and rxq) and RDMA Queue Pairs (QPs).
For example, the number of netdevice channels and RDMA device completion
vectors are derived from the function's IO event queues. Usually, the number
of interrupt vectors consumed by the driver is limited by the number of IO
event queues per device, as each of the IO event queues is connected to an
interrupt vector.

- Get maximum IO event queues of the VF device::

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_packet disabled max_io_eqs 10

- Set maximum IO event queues of the VF device::

    $ devlink port function set pci/0000:06:00.0/2 max_io_eqs 32

    $ devlink port show pci/0000:06:00.0/2
    pci/0000:06:00.0/2: type eth netdev enp6s0pf0vf1 flavour pcivf pfnum 0 vfnum 1
        function:
            hw_addr 00:00:00:00:00:00 ipsec_packet disabled max_io_eqs 32

Subfunction
============

Subfunction is a lightweight function that has a parent PCI function on which
it is deployed. Subfunction is created and deployed in unit of 1. Unlike
SRIOV VFs, a subfunction doesn't require its own PCI virtual function.
A subfunction communicates with the hardware through the parent PCI function.

To use a subfunction, 3 steps setup sequence is followed:

1) create - create a subfunction;
2) configure - configure subfunction attributes;
3) deploy - deploy the subfunction;

Subfunction management is done using devlink port user interface.
User performs setup on the subfunction management device.

(1) Create
----------
A subfunction is created using a devlink port interface. A user adds the
subfunction by adding a devlink port of subfunction flavour. The devlink
kernel code calls down to subfunction management driver (devlink ops) and asks
it to create a subfunction devlink port. Driver then instantiates the
subfunction port and any associated objects such as health reporters and
representor netdevice.

(2) Configure
-------------
A subfunction devlink port is created but it is not active yet. That means the
entities are created on devlink side, the e-switch port representor is created,
but the subfunction device itself is not created. A user might use e-switch port
representor to do settings, putting it into bridge, adding TC rules, etc. A user
might as well configure the hardware address (such as MAC address) of the
subfunction while subfunction is inactive.

(3) Deploy
----------
Once a subfunction is configured, user must activate it to use it. Upon
activation, subfunction management driver asks the subfunction management
device to instantiate the subfunction device on particular PCI function.
A subfunction device is created on the :ref:`Documentation/driver-api/auxiliary_bus.rst <auxiliary_bus>`.
At this point a matching subfunction driver binds to the subfunction's auxiliary device.

Rate object management
======================

Devlink provides API to manage tx rates of single devlink port or a group.
This is done through rate objects, which can be one of the two types:

``leaf``
  Represents a single devlink port; created/destroyed by the driver. Since leaf
  have 1to1 mapping to its devlink port, in user space it is referred as
  ``pci/<bus_addr>/<port_index>``;

``node``
  Represents a group of rate objects (leafs and/or nodes); created/deleted by
  request from the userspace; initially empty (no rate objects added). In
  userspace it is referred as ``pci/<bus_addr>/<node_name>``, where
  ``node_name`` can be any identifier, except decimal number, to avoid
  collisions with leafs.

API allows to configure following rate object's parameters:

``tx_share``
  Minimum TX rate value shared among all other rate objects, or rate objects
  that parts of the parent group, if it is a part of the same group.

``tx_max``
  Maximum TX rate value.

``tx_priority``
  Allows for usage of strict priority arbiter among siblings. This
  arbitration scheme attempts to schedule nodes based on their priority
  as long as the nodes remain within their bandwidth limit. The higher the
  priority the higher the probability that the node will get selected for
  scheduling.

``tx_weight``
  Allows for usage of Weighted Fair Queuing arbitration scheme among
  siblings. This arbitration scheme can be used simultaneously with the
  strict priority. As a node is configured with a higher rate it gets more
  BW relative to its siblings. Values are relative like a percentage
  points, they basically tell how much BW should node take relative to
  its siblings.

``parent``
  Parent node name. Parent node rate limits are considered as additional limits
  to all node children limits. ``tx_max`` is an upper limit for children.
  ``tx_share`` is a total bandwidth distributed among children.

``tx_priority`` and ``tx_weight`` can be used simultaneously. In that case
nodes with the same priority form a WFQ subgroup in the sibling group
and arbitration among them is based on assigned weights.

Arbitration flow from the high level:

#. Choose a node, or group of nodes with the highest priority that stays
   within the BW limit and are not blocked. Use ``tx_priority`` as a
   parameter for this arbitration.

#. If group of nodes have the same priority perform WFQ arbitration on
   that subgroup. Use ``tx_weight`` as a parameter for this arbitration.

#. Select the winner node, and continue arbitration flow among its children,
   until leaf node is reached, and the winner is established.

#. If all the nodes from the highest priority sub-group are satisfied, or
   overused their assigned BW, move to the lower priority nodes.

Driver implementations are allowed to support both or either rate object types
and setting methods of their parameters. Additionally driver implementation
may export nodes/leafs and their child-parent relationships.

Terms and Definitions
=====================

.. list-table:: Terms and Definitions
   :widths: 22 90

   * - Term
     - Definitions
   * - ``PCI device``
     - A physical PCI device having one or more PCI buses consists of one or
       more PCI controllers.
   * - ``PCI controller``
     -  A controller consists of potentially multiple physical functions,
        virtual functions and subfunctions.
   * - ``Port function``
     -  An object to manage the function of a port.
   * - ``Subfunction``
     -  A lightweight function that has parent PCI function on which it is
        deployed.
   * - ``Subfunction device``
     -  A bus device of the subfunction, usually on a auxiliary bus.
   * - ``Subfunction driver``
     -  A device driver for the subfunction auxiliary device.
   * - ``Subfunction management device``
     -  A PCI physical function that supports subfunction management.
   * - ``Subfunction management driver``
     -  A device driver for PCI physical function that supports
        subfunction management using devlink port interface.
   * - ``Subfunction host driver``
     -  A device driver for PCI physical function that hosts subfunction
        devices. In most cases it is same as subfunction management driver. When
        subfunction is used on external controller, subfunction management and
        host drivers are different.
