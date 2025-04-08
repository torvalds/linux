.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===============
Multi-PF Netdev
===============

Contents
========

- `Background`_
- `Overview`_
- `mlx5 implementation`_
- `Channels distribution`_
- `Observability`_
- `Steering`_
- `Mutually exclusive features`_

Background
==========

The Multi-PF NIC technology enables several CPUs within a multi-socket server to connect directly to
the network, each through its own dedicated PCIe interface. Through either a connection harness that
splits the PCIe lanes between two cards or by bifurcating a PCIe slot for a single card. This
results in eliminating the network traffic traversing over the internal bus between the sockets,
significantly reducing overhead and latency, in addition to reducing CPU utilization and increasing
network throughput.

Overview
========

The feature adds support for combining multiple PFs of the same port in a Multi-PF environment under
one netdev instance. It is implemented in the netdev layer. Lower-layer instances like pci func,
sysfs entry, and devlink are kept separate.
Passing traffic through different devices belonging to different NUMA sockets saves cross-NUMA
traffic and allows apps running on the same netdev from different NUMAs to still feel a sense of
proximity to the device and achieve improved performance.

mlx5 implementation
===================

Multi-PF or Socket-direct in mlx5 is achieved by grouping PFs together which belong to the same
NIC and has the socket-direct property enabled, once all PFs are probed, we create a single netdev
to represent all of them, symmetrically, we destroy the netdev whenever any of the PFs is removed.

The netdev network channels are distributed between all devices, a proper configuration would utilize
the correct close NUMA node when working on a certain app/CPU.

We pick one PF to be a primary (leader), and it fills a special role. The other devices
(secondaries) are disconnected from the network at the chip level (set to silent mode). In silent
mode, no south <-> north traffic flowing directly through a secondary PF. It needs the assistance of
the leader PF (east <-> west traffic) to function. All Rx/Tx traffic is steered through the primary
to/from the secondaries.

Currently, we limit the support to PFs only, and up to two PFs (sockets).

Channels distribution
=====================

We distribute the channels between the different PFs to achieve local NUMA node performance
on multiple NUMA nodes.

Each combined channel works against one specific PF, creating all its datapath queues against it. We
distribute channels to PFs in a round-robin policy.

::

        Example for 2 PFs and 5 channels:
        +--------+--------+
        | ch idx | PF idx |
        +--------+--------+
        |    0   |    0   |
        |    1   |    1   |
        |    2   |    0   |
        |    3   |    1   |
        |    4   |    0   |
        +--------+--------+


The reason we prefer round-robin is, it is less influenced by changes in the number of channels. The
mapping between a channel index and a PF is fixed, no matter how many channels the user configures.
As the channel stats are persistent across channel's closure, changing the mapping every single time
would turn the accumulative stats less representing of the channel's history.

This is achieved by using the correct core device instance (mdev) in each channel, instead of them
all using the same instance under "priv->mdev".

Observability
=============
The relation between PF, irq, napi, and queue can be observed via netlink spec::

  $ ./tools/net/ynl/pyynl/cli.py --spec Documentation/netlink/specs/netdev.yaml --dump queue-get --json='{"ifindex": 13}'
  [{'id': 0, 'ifindex': 13, 'napi-id': 539, 'type': 'rx'},
   {'id': 1, 'ifindex': 13, 'napi-id': 540, 'type': 'rx'},
   {'id': 2, 'ifindex': 13, 'napi-id': 541, 'type': 'rx'},
   {'id': 3, 'ifindex': 13, 'napi-id': 542, 'type': 'rx'},
   {'id': 4, 'ifindex': 13, 'napi-id': 543, 'type': 'rx'},
   {'id': 0, 'ifindex': 13, 'napi-id': 539, 'type': 'tx'},
   {'id': 1, 'ifindex': 13, 'napi-id': 540, 'type': 'tx'},
   {'id': 2, 'ifindex': 13, 'napi-id': 541, 'type': 'tx'},
   {'id': 3, 'ifindex': 13, 'napi-id': 542, 'type': 'tx'},
   {'id': 4, 'ifindex': 13, 'napi-id': 543, 'type': 'tx'}]

  $ ./tools/net/ynl/pyynl/cli.py --spec Documentation/netlink/specs/netdev.yaml --dump napi-get --json='{"ifindex": 13}'
  [{'id': 543, 'ifindex': 13, 'irq': 42},
   {'id': 542, 'ifindex': 13, 'irq': 41},
   {'id': 541, 'ifindex': 13, 'irq': 40},
   {'id': 540, 'ifindex': 13, 'irq': 39},
   {'id': 539, 'ifindex': 13, 'irq': 36}]

Here you can clearly observe our channels distribution policy::

  $ ls /proc/irq/{36,39,40,41,42}/mlx5* -d -1
  /proc/irq/36/mlx5_comp0@pci:0000:08:00.0
  /proc/irq/39/mlx5_comp0@pci:0000:09:00.0
  /proc/irq/40/mlx5_comp1@pci:0000:08:00.0
  /proc/irq/41/mlx5_comp1@pci:0000:09:00.0
  /proc/irq/42/mlx5_comp2@pci:0000:08:00.0

Steering
========
Secondary PFs are set to "silent" mode, meaning they are disconnected from the network.

In Rx, the steering tables belong to the primary PF only, and it is its role to distribute incoming
traffic to other PFs, via cross-vhca steering capabilities. Still maintain a single default RSS table,
that is capable of pointing to the receive queues of a different PF.

In Tx, the primary PF creates a new Tx flow table, which is aliased by the secondaries, so they can
go out to the network through it.

In addition, we set default XPS configuration that, based on the CPU, selects an SQ belonging to the
PF on the same node as the CPU.

XPS default config example:

NUMA node(s):          2
NUMA node0 CPU(s):     0-11
NUMA node1 CPU(s):     12-23

PF0 on node0, PF1 on node1.

- /sys/class/net/eth2/queues/tx-0/xps_cpus:000001
- /sys/class/net/eth2/queues/tx-1/xps_cpus:001000
- /sys/class/net/eth2/queues/tx-2/xps_cpus:000002
- /sys/class/net/eth2/queues/tx-3/xps_cpus:002000
- /sys/class/net/eth2/queues/tx-4/xps_cpus:000004
- /sys/class/net/eth2/queues/tx-5/xps_cpus:004000
- /sys/class/net/eth2/queues/tx-6/xps_cpus:000008
- /sys/class/net/eth2/queues/tx-7/xps_cpus:008000
- /sys/class/net/eth2/queues/tx-8/xps_cpus:000010
- /sys/class/net/eth2/queues/tx-9/xps_cpus:010000
- /sys/class/net/eth2/queues/tx-10/xps_cpus:000020
- /sys/class/net/eth2/queues/tx-11/xps_cpus:020000
- /sys/class/net/eth2/queues/tx-12/xps_cpus:000040
- /sys/class/net/eth2/queues/tx-13/xps_cpus:040000
- /sys/class/net/eth2/queues/tx-14/xps_cpus:000080
- /sys/class/net/eth2/queues/tx-15/xps_cpus:080000
- /sys/class/net/eth2/queues/tx-16/xps_cpus:000100
- /sys/class/net/eth2/queues/tx-17/xps_cpus:100000
- /sys/class/net/eth2/queues/tx-18/xps_cpus:000200
- /sys/class/net/eth2/queues/tx-19/xps_cpus:200000
- /sys/class/net/eth2/queues/tx-20/xps_cpus:000400
- /sys/class/net/eth2/queues/tx-21/xps_cpus:400000
- /sys/class/net/eth2/queues/tx-22/xps_cpus:000800
- /sys/class/net/eth2/queues/tx-23/xps_cpus:800000

Mutually exclusive features
===========================

The nature of Multi-PF, where different channels work with different PFs, conflicts with
stateful features where the state is maintained in one of the PFs.
For example, in the TLS device-offload feature, special context objects are created per connection
and maintained in the PF.  Transitioning between different RQs/SQs would break the feature. Hence,
we disable this combination for now.
