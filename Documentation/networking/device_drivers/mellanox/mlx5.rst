.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

=================================================
Mellanox ConnectX(R) mlx5 core VPI Network Driver
=================================================

Copyright (c) 2019, Mellanox Technologies LTD.

Contents
========

- `Enabling the driver and kconfig options`_
- `Devlink info`_
- `Devlink health reporters`_

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

Devlink health reporters
========================

tx reporter
-----------
The tx reporter is responsible of two error scenarios:

- TX timeout
    Report on kernel tx timeout detection.
    Recover by searching lost interrupts.
- TX error completion
    Report on error tx completion.
    Recover by flushing the TX queue and reset it.

TX reporter also support Diagnose callback, on which it provides
real time information of its send queues status.

User commands examples:

- Diagnose send queues status::

    $ devlink health diagnose pci/0000:82:00.0 reporter tx

- Show number of tx errors indicated, number of recover flows ended successfully,
  is autorecover enabled and graceful period from last recover::

    $ devlink health show pci/0000:82:00.0 reporter tx

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
