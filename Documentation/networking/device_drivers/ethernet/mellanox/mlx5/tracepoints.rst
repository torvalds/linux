.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
.. include:: <isonum.txt>

===========
Tracepoints
===========

:Copyright: |copy| 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

mlx5 driver provides internal tracepoints for tracking and debugging using
kernel tracepoints interfaces (refer to Documentation/trace/ftrace.rst).

For the list of support mlx5 events, check /sys/kernel/tracing/events/mlx5/.

tc and eswitch offloads tracepoints:

- mlx5e_configure_flower: trace flower filter actions and cookies offloaded to mlx5::

    $ echo mlx5:mlx5e_configure_flower >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    tc-6535  [019] ...1  2672.404466: mlx5e_configure_flower: cookie=0000000067874a55 actions= REDIRECT

- mlx5e_delete_flower: trace flower filter actions and cookies deleted from mlx5::

    $ echo mlx5:mlx5e_delete_flower >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    tc-6569  [010] .N.1  2686.379075: mlx5e_delete_flower: cookie=0000000067874a55 actions= NULL

- mlx5e_stats_flower: trace flower stats request::

    $ echo mlx5:mlx5e_stats_flower >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    tc-6546  [010] ...1  2679.704889: mlx5e_stats_flower: cookie=0000000060eb3d6a bytes=0 packets=0 lastused=4295560217

- mlx5e_tc_update_neigh_used_value: trace tunnel rule neigh update value offloaded to mlx5::

    $ echo mlx5:mlx5e_tc_update_neigh_used_value >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u48:4-8806  [009] ...1 55117.882428: mlx5e_tc_update_neigh_used_value: netdev: ens1f0 IPv4: 1.1.1.10 IPv6: ::ffff:1.1.1.10 neigh_used=1

- mlx5e_rep_neigh_update: trace neigh update tasks scheduled due to neigh state change events::

    $ echo mlx5:mlx5e_rep_neigh_update >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u48:7-2221  [009] ...1  1475.387435: mlx5e_rep_neigh_update: netdev: ens1f0 MAC: 24:8a:07:9a:17:9a IPv4: 1.1.1.10 IPv6: ::ffff:1.1.1.10 neigh_connected=1

Bridge offloads tracepoints:

- mlx5_esw_bridge_fdb_entry_init: trace bridge FDB entry offloaded to mlx5::

    $ echo mlx5:mlx5_esw_bridge_fdb_entry_init >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u20:9-2217    [003] ...1   318.582243: mlx5_esw_bridge_fdb_entry_init: net_device=enp8s0f0_0 addr=e4:fd:05:08:00:02 vid=0 flags=0 used=0

- mlx5_esw_bridge_fdb_entry_cleanup: trace bridge FDB entry deleted from mlx5::

    $ echo mlx5:mlx5_esw_bridge_fdb_entry_cleanup >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    ip-2581    [005] ...1   318.629871: mlx5_esw_bridge_fdb_entry_cleanup: net_device=enp8s0f0_1 addr=e4:fd:05:08:00:03 vid=0 flags=0 used=16

- mlx5_esw_bridge_fdb_entry_refresh: trace bridge FDB entry offload refreshed in
  mlx5::

    $ echo mlx5:mlx5_esw_bridge_fdb_entry_refresh >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u20:8-3849    [003] ...1       466716: mlx5_esw_bridge_fdb_entry_refresh: net_device=enp8s0f0_0 addr=e4:fd:05:08:00:02 vid=3 flags=0 used=0

- mlx5_esw_bridge_vlan_create: trace bridge VLAN object add on mlx5
  representor::

    $ echo mlx5:mlx5_esw_bridge_vlan_create >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    ip-2560    [007] ...1   318.460258: mlx5_esw_bridge_vlan_create: vid=1 flags=6

- mlx5_esw_bridge_vlan_cleanup: trace bridge VLAN object delete from mlx5
  representor::

    $ echo mlx5:mlx5_esw_bridge_vlan_cleanup >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    bridge-2582    [007] ...1   318.653496: mlx5_esw_bridge_vlan_cleanup: vid=2 flags=8

- mlx5_esw_bridge_vport_init: trace mlx5 vport assigned with bridge upper
  device::

    $ echo mlx5:mlx5_esw_bridge_vport_init >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    ip-2560    [007] ...1   318.458915: mlx5_esw_bridge_vport_init: vport_num=1

- mlx5_esw_bridge_vport_cleanup: trace mlx5 vport removed from bridge upper
  device::

    $ echo mlx5:mlx5_esw_bridge_vport_cleanup >> set_event
    $ cat /sys/kernel/tracing/trace
    ...
    ip-5387    [000] ...1       573713: mlx5_esw_bridge_vport_cleanup: vport_num=1

Eswitch QoS tracepoints:

- mlx5_esw_vport_qos_create: trace creation of transmit scheduler arbiter for vport::

    $ echo mlx5:mlx5_esw_vport_qos_create >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-23496   [018] .... 73136.838831: mlx5_esw_vport_qos_create: (0000:82:00.0) vport=2 tsar_ix=4 bw_share=0, max_rate=0 group=000000007b576bb3

- mlx5_esw_vport_qos_config: trace configuration of transmit scheduler arbiter for vport::

    $ echo mlx5:mlx5_esw_vport_qos_config >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-26548   [023] .... 75754.223823: mlx5_esw_vport_qos_config: (0000:82:00.0) vport=1 tsar_ix=3 bw_share=34, max_rate=10000 group=000000007b576bb3

- mlx5_esw_vport_qos_destroy: trace deletion of transmit scheduler arbiter for vport::

    $ echo mlx5:mlx5_esw_vport_qos_destroy >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-27418   [004] .... 76546.680901: mlx5_esw_vport_qos_destroy: (0000:82:00.0) vport=1 tsar_ix=3

- mlx5_esw_group_qos_create: trace creation of transmit scheduler arbiter for rate group::

    $ echo mlx5:mlx5_esw_group_qos_create >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-26578   [008] .... 75776.022112: mlx5_esw_group_qos_create: (0000:82:00.0) group=000000008dac63ea tsar_ix=5

- mlx5_esw_group_qos_config: trace configuration of transmit scheduler arbiter for rate group::

    $ echo mlx5:mlx5_esw_group_qos_config >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-27303   [020] .... 76461.455356: mlx5_esw_group_qos_config: (0000:82:00.0) group=000000008dac63ea tsar_ix=5 bw_share=100 max_rate=20000

- mlx5_esw_group_qos_destroy: trace deletion of transmit scheduler arbiter for group::

    $ echo mlx5:mlx5_esw_group_qos_destroy >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    <...>-27418   [006] .... 76547.187258: mlx5_esw_group_qos_destroy: (0000:82:00.0) group=000000007b576bb3 tsar_ix=1

SF tracepoints:

- mlx5_sf_add: trace addition of the SF port::

    $ echo mlx5:mlx5_sf_add >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-9363    [031] ..... 24610.188722: mlx5_sf_add: (0000:06:00.0) port_index=32768 controller=0 hw_id=0x8000 sfnum=88

- mlx5_sf_free: trace freeing of the SF port::

    $ echo mlx5:mlx5_sf_free >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-9830    [038] ..... 26300.404749: mlx5_sf_free: (0000:06:00.0) port_index=32768 controller=0 hw_id=0x8000

- mlx5_sf_activate: trace activation of the SF port::

    $ echo mlx5:mlx5_sf_activate >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-29841   [008] .....  3669.635095: mlx5_sf_activate: (0000:08:00.0) port_index=32768 controller=0 hw_id=0x8000

- mlx5_sf_deactivate: trace deactivation of the SF port::

    $ echo mlx5:mlx5_sf_deactivate >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-29994   [008] .....  4015.969467: mlx5_sf_deactivate: (0000:08:00.0) port_index=32768 controller=0 hw_id=0x8000

- mlx5_sf_hwc_alloc: trace allocating of the hardware SF context::

    $ echo mlx5:mlx5_sf_hwc_alloc >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-9775    [031] ..... 26296.385259: mlx5_sf_hwc_alloc: (0000:06:00.0) controller=0 hw_id=0x8000 sfnum=88

- mlx5_sf_hwc_free: trace freeing of the hardware SF context::

    $ echo mlx5:mlx5_sf_hwc_free >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u128:3-9093    [046] ..... 24625.365771: mlx5_sf_hwc_free: (0000:06:00.0) hw_id=0x8000

- mlx5_sf_hwc_deferred_free: trace deferred freeing of the hardware SF context::

    $ echo mlx5:mlx5_sf_hwc_deferred_free >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    devlink-9519    [046] ..... 24624.400271: mlx5_sf_hwc_deferred_free: (0000:06:00.0) hw_id=0x8000

- mlx5_sf_update_state: trace state updates for SF contexts::

    $ echo mlx5:mlx5_sf_update_state >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u20:3-29490   [009] .....  4141.453530: mlx5_sf_update_state: (0000:08:00.0) port_index=32768 controller=0 hw_id=0x8000 state=2

- mlx5_sf_vhca_event: trace SF vhca event and state::

    $ echo mlx5:mlx5_sf_vhca_event >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u128:3-9093    [046] ..... 24625.365525: mlx5_sf_vhca_event: (0000:06:00.0) hw_id=0x8000 sfnum=88 vhca_state=1

- mlx5_sf_dev_add: trace SF device add event::

    $ echo mlx5:mlx5_sf_dev_add>> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u128:3-9093    [000] ..... 24616.524495: mlx5_sf_dev_add: (0000:06:00.0) sfdev=00000000fc5d96fd aux_id=4 hw_id=0x8000 sfnum=88

- mlx5_sf_dev_del: trace SF device delete event::

    $ echo mlx5:mlx5_sf_dev_del >> /sys/kernel/tracing/set_event
    $ cat /sys/kernel/tracing/trace
    ...
    kworker/u128:3-9093    [044] ..... 24624.400749: mlx5_sf_dev_del: (0000:06:00.0) sfdev=00000000fc5d96fd aux_id=4 hw_id=0x8000 sfnum=88
