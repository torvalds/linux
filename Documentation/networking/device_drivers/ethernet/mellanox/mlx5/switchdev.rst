.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
.. include:: <isonum.txt>

=========
Switchdev
=========

:Copyright: |copy| 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

.. _mlx5_bridge_offload:

Bridge offload
==============

The mlx5 driver implements support for offloading bridge rules when in switchdev
mode. Linux bridge FDBs are automatically offloaded when mlx5 switchdev
representor is attached to bridge.

- Change device to switchdev mode::

    $ devlink dev eswitch set pci/0000:06:00.0 mode switchdev

- Attach mlx5 switchdev representor 'enp8s0f0' to bridge netdev 'bridge1'::

    $ ip link set enp8s0f0 master bridge1

VLANs
-----

Following bridge VLAN functions are supported by mlx5:

- VLAN filtering (including multiple VLANs per port)::

    $ ip link set bridge1 type bridge vlan_filtering 1
    $ bridge vlan add dev enp8s0f0 vid 2-3

- VLAN push on bridge ingress::

    $ bridge vlan add dev enp8s0f0 vid 3 pvid

- VLAN pop on bridge egress::

    $ bridge vlan add dev enp8s0f0 vid 3 untagged

Subfunction
===========

mlx5 supports subfunction management using devlink port (see :ref:`Documentation/networking/devlink/devlink-port.rst <devlink_port>`) interface.

A subfunction has its own function capabilities and its own resources. This
means a subfunction has its own dedicated queues (txq, rxq, cq, eq). These
queues are neither shared nor stolen from the parent PCI function.

When a subfunction is RDMA capable, it has its own QP1, GID table, and RDMA
resources neither shared nor stolen from the parent PCI function.

A subfunction has a dedicated window in PCI BAR space that is not shared
with the other subfunctions or the parent PCI function. This ensures that all
devices (netdev, rdma, vdpa, etc.) of the subfunction accesses only assigned
PCI BAR space.

A subfunction supports eswitch representation through which it supports tc
offloads. The user configures eswitch to send/receive packets from/to
the subfunction port.

Subfunctions share PCI level resources such as PCI MSI-X IRQs with
other subfunctions and/or with its parent PCI function.

Example mlx5 software, system, and device view::

       _______
      | admin |
      | user  |----------
      |_______|         |
          |             |
      ____|____       __|______            _________________
     |         |     |         |          |                 |
     | devlink |     | tc tool |          |    user         |
     | tool    |     |_________|          | applications    |
     |_________|         |                |_________________|
           |             |                   |          |
           |             |                   |          |         Userspace
 +---------|-------------|-------------------|----------|--------------------+
           |             |           +----------+   +----------+   Kernel
           |             |           |  netdev  |   | rdma dev |
           |             |           +----------+   +----------+
   (devlink port add/del |              ^               ^
    port function set)   |              |               |
           |             |              +---------------|
      _____|___          |              |        _______|_______
     |         |         |              |       | mlx5 class    |
     | devlink |   +------------+       |       |   drivers     |
     | kernel  |   | rep netdev |       |       |(mlx5_core,ib) |
     |_________|   +------------+       |       |_______________|
           |             |              |               ^
   (devlink ops)         |              |          (probe/remove)
  _________|________     |              |           ____|________
 | subfunction      |    |     +---------------+   | subfunction |
 | management driver|-----     | subfunction   |---|  driver     |
 | (mlx5_core)      |          | auxiliary dev |   | (mlx5_core) |
 |__________________|          +---------------+   |_____________|
           |                                            ^
  (sf add/del, vhca events)                             |
           |                                      (device add/del)
      _____|____                                    ____|________
     |          |                                  | subfunction |
     |  PCI NIC |--- activate/deactivate events--->| host driver |
     |__________|                                  | (mlx5_core) |
                                                   |_____________|

Subfunction is created using devlink port interface.

- Change device to switchdev mode::

    $ devlink dev eswitch set pci/0000:06:00.0 mode switchdev

- Add a devlink port of subfunction flavour::

    $ devlink port add pci/0000:06:00.0 flavour pcisf pfnum 0 sfnum 88
    pci/0000:06:00.0/32768: type eth netdev eth6 flavour pcisf controller 0 pfnum 0 sfnum 88 external false splittable false
      function:
        hw_addr 00:00:00:00:00:00 state inactive opstate detached

- Show a devlink port of the subfunction::

    $ devlink port show pci/0000:06:00.0/32768
    pci/0000:06:00.0/32768: type eth netdev enp6s0pf0sf88 flavour pcisf pfnum 0 sfnum 88
      function:
        hw_addr 00:00:00:00:00:00 state inactive opstate detached

- Delete a devlink port of subfunction after use::

    $ devlink port del pci/0000:06:00.0/32768

Function attributes
===================

The mlx5 driver provides a mechanism to setup PCI VF/SF function attributes in
a unified way for SmartNIC and non-SmartNIC.

This is supported only when the eswitch mode is set to switchdev. Port function
configuration of the PCI VF/SF is supported through devlink eswitch port.

Port function attributes should be set before PCI VF/SF is enumerated by the
driver.

MAC address setup
-----------------

mlx5 driver support devlink port function attr mechanism to setup MAC
address. (refer to Documentation/networking/devlink/devlink-port.rst)

RoCE capability setup
~~~~~~~~~~~~~~~~~~~~~
Not all mlx5 PCI devices/SFs require RoCE capability.

When RoCE capability is disabled, it saves 1 Mbytes worth of system memory per
PCI devices/SF.

mlx5 driver support devlink port function attr mechanism to setup RoCE
capability. (refer to Documentation/networking/devlink/devlink-port.rst)

migratable capability setup
~~~~~~~~~~~~~~~~~~~~~~~~~~~
User who wants mlx5 PCI VFs to be able to perform live migration need to
explicitly enable the VF migratable capability.

mlx5 driver support devlink port function attr mechanism to setup migratable
capability. (refer to Documentation/networking/devlink/devlink-port.rst)

SF state setup
--------------

To use the SF, the user must activate the SF using the SF function state
attribute.

- Get the state of the SF identified by its unique devlink port index::

   $ devlink port show ens2f0npf0sf88
   pci/0000:06:00.0/32768: type eth netdev ens2f0npf0sf88 flavour pcisf controller 0 pfnum 0 sfnum 88 external false splittable false
     function:
       hw_addr 00:00:00:00:88:88 state inactive opstate detached

- Activate the function and verify its state is active::

   $ devlink port function set ens2f0npf0sf88 state active

   $ devlink port show ens2f0npf0sf88
   pci/0000:06:00.0/32768: type eth netdev ens2f0npf0sf88 flavour pcisf controller 0 pfnum 0 sfnum 88 external false splittable false
     function:
       hw_addr 00:00:00:00:88:88 state active opstate detached

Upon function activation, the PF driver instance gets the event from the device
that a particular SF was activated. It's the cue to put the device on bus, probe
it and instantiate the devlink instance and class specific auxiliary devices
for it.

- Show the auxiliary device and port of the subfunction::

    $ devlink dev show
    devlink dev show auxiliary/mlx5_core.sf.4

    $ devlink port show auxiliary/mlx5_core.sf.4/1
    auxiliary/mlx5_core.sf.4/1: type eth netdev p0sf88 flavour virtual port 0 splittable false

    $ rdma link show mlx5_0/1
    link mlx5_0/1 state ACTIVE physical_state LINK_UP netdev p0sf88

    $ rdma dev show
    8: rocep6s0f1: node_type ca fw 16.29.0550 node_guid 248a:0703:00b3:d113 sys_image_guid 248a:0703:00b3:d112
    13: mlx5_0: node_type ca fw 16.29.0550 node_guid 0000:00ff:fe00:8888 sys_image_guid 248a:0703:00b3:d112

- Subfunction auxiliary device and class device hierarchy::

                 mlx5_core.sf.4
          (subfunction auxiliary device)
                       /\
                      /  \
                     /    \
                    /      \
                   /        \
      mlx5_core.eth.4     mlx5_core.rdma.4
     (sf eth aux dev)     (sf rdma aux dev)
         |                      |
         |                      |
      p0sf88                  mlx5_0
     (sf netdev)          (sf rdma device)

Additionally, the SF port also gets the event when the driver attaches to the
auxiliary device of the subfunction. This results in changing the operational
state of the function. This provides visibility to the user to decide when is it
safe to delete the SF port for graceful termination of the subfunction.

- Show the SF port operational state::

    $ devlink port show ens2f0npf0sf88
    pci/0000:06:00.0/32768: type eth netdev ens2f0npf0sf88 flavour pcisf controller 0 pfnum 0 sfnum 88 external false splittable false
      function:
        hw_addr 00:00:00:00:88:88 state active opstate attached
