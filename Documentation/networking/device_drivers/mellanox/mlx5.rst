.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

=================================================
Mellanox ConnectX(R) mlx5 core VPI Network Driver
=================================================

Copyright (c) 2019, Mellanox Technologies LTD.

Contents
========

- `Enabling the driver and kconfig options`_
- `Devlink info`_
- `Devlink parameters`_
- `Devlink health reporters`_
- `mlx5 tracepoints`_

Enabling the driver and kconfig options
================================================

| mlx5 core is modular and most of the major mlx5 core driver features can be selected (compiled in/out)
| at build time via kernel Kconfig flags.
| Basic features, ethernet net device rx/tx offloads and XDP, are available with the most basic flags
| CONFIG_MLX5_CORE=y/m and CONFIG_MLX5_CORE_EN=y.
| For the list of advanced features please see below.

**CONFIG_MLX5_CORE=(y/m/n)** (module mlx5_core.ko)

|    The driver can be enabled by choosing CONFIG_MLX5_CORE=y/m in kernel config.
|    This will provide mlx5 core driver for mlx5 ulps to interface with (mlx5e, mlx5_ib).


**CONFIG_MLX5_CORE_EN=(y/n)**

|    Choosing this option will allow basic ethernet netdevice support with all of the standard rx/tx offloads.
|    mlx5e is the mlx5 ulp driver which provides netdevice kernel interface, when chosen, mlx5e will be
|    built-in into mlx5_core.ko.


**CONFIG_MLX5_EN_ARFS=(y/n)**

|     Enables Hardware-accelerated receive flow steering (arfs) support, and ntuple filtering.
|     https://community.mellanox.com/s/article/howto-configure-arfs-on-connectx-4


**CONFIG_MLX5_EN_RXNFC=(y/n)**

|    Enables ethtool receive network flow classification, which allows user defined
|    flow rules to direct traffic into arbitrary rx queue via ethtool set/get_rxnfc API.


**CONFIG_MLX5_CORE_EN_DCB=(y/n)**:

|    Enables `Data Center Bridging (DCB) Support <https://community.mellanox.com/s/article/howto-auto-config-pfc-and-ets-on-connectx-4-via-lldp-dcbx>`_.


**CONFIG_MLX5_MPFS=(y/n)**

|    Ethernet Multi-Physical Function Switch (MPFS) support in ConnectX NIC.
|    MPFs is required for when `Multi-Host <http://www.mellanox.com/page/multihost>`_ configuration is enabled to allow passing
|    user configured unicast MAC addresses to the requesting PF.


**CONFIG_MLX5_ESWITCH=(y/n)**

|    Ethernet SRIOV E-Switch support in ConnectX NIC. E-Switch provides internal SRIOV packet steering
|    and switching for the enabled VFs and PF in two available modes:
|           1) `Legacy SRIOV mode (L2 mac vlan steering based) <https://community.mellanox.com/s/article/howto-configure-sr-iov-for-connectx-4-connectx-5-with-kvm--ethernet-x>`_.
|           2) `Switchdev mode (eswitch offloads) <https://www.mellanox.com/related-docs/prod_software/ASAP2_Hardware_Offloading_for_vSwitches_User_Manual_v4.4.pdf>`_.


**CONFIG_MLX5_CORE_IPOIB=(y/n)**

|    IPoIB offloads & acceleration support.
|    Requires CONFIG_MLX5_CORE_EN to provide an accelerated interface for the rdma
|    IPoIB ulp netdevice.


**CONFIG_MLX5_FPGA=(y/n)**

|    Build support for the Innova family of network cards by Mellanox Technologies.
|    Innova network cards are comprised of a ConnectX chip and an FPGA chip on one board.
|    If you select this option, the mlx5_core driver will include the Innova FPGA core and allow
|    building sandbox-specific client drivers.


**CONFIG_MLX5_EN_IPSEC=(y/n)**

|    Enables `IPSec XFRM cryptography-offload accelaration <http://www.mellanox.com/related-docs/prod_software/Mellanox_Innova_IPsec_Ethernet_Adapter_Card_User_Manual.pdf>`_.

**CONFIG_MLX5_EN_TLS=(y/n)**

|   TLS cryptography-offload accelaration.


**CONFIG_MLX5_INFINIBAND=(y/n/m)** (module mlx5_ib.ko)

|   Provides low-level InfiniBand/RDMA and `RoCE <https://community.mellanox.com/s/article/recommended-network-configuration-examples-for-roce-deployment>`_ support.


**External options** ( Choose if the corresponding mlx5 feature is required )

- CONFIG_PTP_1588_CLOCK: When chosen, mlx5 ptp support will be enabled
- CONFIG_VXLAN: When chosen, mlx5 vxaln support will be enabled.
- CONFIG_MLXFW: When chosen, mlx5 firmware flashing support will be enabled (via devlink and ethtool).

Devlink info
============

The devlink info reports the running and stored firmware versions on device.
It also prints the device PSID which represents the HCA board type ID.

User command example::

   $ devlink dev info pci/0000:00:06.0
      pci/0000:00:06.0:
      driver mlx5_core
      versions:
         fixed:
            fw.psid MT_0000000009
         running:
            fw.version 16.26.0100
         stored:
            fw.version 16.26.0100

Devlink parameters
==================

flow_steering_mode: Device flow steering mode
---------------------------------------------
The flow steering mode parameter controls the flow steering mode of the driver.
Two modes are supported:
1. 'dmfs' - Device managed flow steering.
2. 'smfs  - Software/Driver managed flow steering.

In DMFS mode, the HW steering entities are created and managed through the
Firmware.
In SMFS mode, the HW steering entities are created and managed though by
the driver directly into Hardware without firmware intervention.

SMFS mode is faster and provides better rule inserstion rate compared to default DMFS mode.

User command examples:

- Set SMFS flow steering mode::

    $ devlink dev param set pci/0000:06:00.0 name flow_steering_mode value "smfs" cmode runtime

- Read device flow steering mode::

    $ devlink dev param show pci/0000:06:00.0 name flow_steering_mode
      pci/0000:06:00.0:
      name flow_steering_mode type driver-specific
      values:
         cmode runtime value smfs


Devlink health reporters
========================

tx reporter
-----------
The tx reporter is responsible for reporting and recovering of the following two error scenarios:

- TX timeout
    Report on kernel tx timeout detection.
    Recover by searching lost interrupts.
- TX error completion
    Report on error tx completion.
    Recover by flushing the TX queue and reset it.

TX reporter also support on demand diagnose callback, on which it provides
real time information of its send queues status.

User commands examples:

- Diagnose send queues status::

    $ devlink health diagnose pci/0000:82:00.0 reporter tx

NOTE: This command has valid output only when interface is up, otherwise the command has empty output.

- Show number of tx errors indicated, number of recover flows ended successfully,
  is autorecover enabled and graceful period from last recover::

    $ devlink health show pci/0000:82:00.0 reporter tx

rx reporter
-----------
The rx reporter is responsible for reporting and recovering of the following two error scenarios:

- RX queues initialization (population) timeout
    RX queues descriptors population on ring initialization is done in
    napi context via triggering an irq, in case of a failure to get
    the minimum amount of descriptors, a timeout would occur and it
    could be recoverable by polling the EQ (Event Queue).
- RX completions with errors (reported by HW on interrupt context)
    Report on rx completion error.
    Recover (if needed) by flushing the related queue and reset it.

RX reporter also supports on demand diagnose callback, on which it
provides real time information of its receive queues status.

- Diagnose rx queues status, and corresponding completion queue::

    $ devlink health diagnose pci/0000:82:00.0 reporter rx

NOTE: This command has valid output only when interface is up, otherwise the command has empty output.

- Show number of rx errors indicated, number of recover flows ended successfully,
  is autorecover enabled and graceful period from last recover::

    $ devlink health show pci/0000:82:00.0 reporter rx

fw reporter
-----------
The fw reporter implements diagnose and dump callbacks.
It follows symptoms of fw error such as fw syndrome by triggering
fw core dump and storing it into the dump buffer.
The fw reporter diagnose command can be triggered any time by the user to check
current fw status.

User commands examples:

- Check fw heath status::

    $ devlink health diagnose pci/0000:82:00.0 reporter fw

- Read FW core dump if already stored or trigger new one::

    $ devlink health dump show pci/0000:82:00.0 reporter fw

NOTE: This command can run only on the PF which has fw tracer ownership,
running it on other PF or any VF will return "Operation not permitted".

fw fatal reporter
-----------------
The fw fatal reporter implements dump and recover callbacks.
It follows fatal errors indications by CR-space dump and recover flow.
The CR-space dump uses vsc interface which is valid even if the FW command
interface is not functional, which is the case in most FW fatal errors.
The recover function runs recover flow which reloads the driver and triggers fw
reset if needed.

User commands examples:

- Run fw recover flow manually::

    $ devlink health recover pci/0000:82:00.0 reporter fw_fatal

- Read FW CR-space dump if already strored or trigger new one::

    $ devlink health dump show pci/0000:82:00.1 reporter fw_fatal

NOTE: This command can run only on PF.

mlx5 tracepoints
================

mlx5 driver provides internal trace points for tracking and debugging using
kernel tracepoints interfaces (refer to Documentation/trace/ftrase.rst).

For the list of support mlx5 events check /sys/kernel/debug/tracing/events/mlx5/

tc and eswitch offloads tracepoints:

- mlx5e_configure_flower: trace flower filter actions and cookies offloaded to mlx5::

    $ echo mlx5:mlx5e_configure_flower >> /sys/kernel/debug/tracing/set_event
    $ cat /sys/kernel/debug/tracing/trace
    ...
    tc-6535  [019] ...1  2672.404466: mlx5e_configure_flower: cookie=0000000067874a55 actions= REDIRECT

- mlx5e_delete_flower: trace flower filter actions and cookies deleted from mlx5::

    $ echo mlx5:mlx5e_delete_flower >> /sys/kernel/debug/tracing/set_event
    $ cat /sys/kernel/debug/tracing/trace
    ...
    tc-6569  [010] .N.1  2686.379075: mlx5e_delete_flower: cookie=0000000067874a55 actions= NULL

- mlx5e_stats_flower: trace flower stats request::

    $ echo mlx5:mlx5e_stats_flower >> /sys/kernel/debug/tracing/set_event
    $ cat /sys/kernel/debug/tracing/trace
    ...
    tc-6546  [010] ...1  2679.704889: mlx5e_stats_flower: cookie=0000000060eb3d6a bytes=0 packets=0 lastused=4295560217

- mlx5e_tc_update_neigh_used_value: trace tunnel rule neigh update value offloaded to mlx5::

    $ echo mlx5:mlx5e_tc_update_neigh_used_value >> /sys/kernel/debug/tracing/set_event
    $ cat /sys/kernel/debug/tracing/trace
    ...
    kworker/u48:4-8806  [009] ...1 55117.882428: mlx5e_tc_update_neigh_used_value: netdev: ens1f0 IPv4: 1.1.1.10 IPv6: ::ffff:1.1.1.10 neigh_used=1

- mlx5e_rep_neigh_update: trace neigh update tasks scheduled due to neigh state change events::

    $ echo mlx5:mlx5e_rep_neigh_update >> /sys/kernel/debug/tracing/set_event
    $ cat /sys/kernel/debug/tracing/trace
    ...
    kworker/u48:7-2221  [009] ...1  1475.387435: mlx5e_rep_neigh_update: netdev: ens1f0 MAC: 24:8a:07:9a:17:9a IPv4: 1.1.1.10 IPv6: ::ffff:1.1.1.10 neigh_connected=1
