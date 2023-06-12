.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
.. include:: <isonum.txt>

=======
Devlink
=======

:Copyright: |copy| 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

Contents
========

- `Info`_
- `Parameters`_
- `Health reporters`_

Info
====

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

Parameters
==========

flow_steering_mode: Device flow steering mode
---------------------------------------------
The flow steering mode parameter controls the flow steering mode of the driver.
Two modes are supported:
1. 'dmfs' - Device managed flow steering.
2. 'smfs' - Software/Driver managed flow steering.

In DMFS mode, the HW steering entities are created and managed through the
Firmware.
In SMFS mode, the HW steering entities are created and managed though by
the driver directly into hardware without firmware intervention.

SMFS mode is faster and provides better rule insertion rate compared to default DMFS mode.

User command examples:

- Set SMFS flow steering mode::

    $ devlink dev param set pci/0000:06:00.0 name flow_steering_mode value "smfs" cmode runtime

- Read device flow steering mode::

    $ devlink dev param show pci/0000:06:00.0 name flow_steering_mode
      pci/0000:06:00.0:
      name flow_steering_mode type driver-specific
      values:
         cmode runtime value smfs

enable_roce: RoCE enablement state
----------------------------------
If the device supports RoCE disablement, RoCE enablement state controls device
support for RoCE capability. Otherwise, the control occurs in the driver stack.
When RoCE is disabled at the driver level, only raw ethernet QPs are supported.

To change RoCE enablement state, a user must change the driverinit cmode value
and run devlink reload.

User command examples:

- Disable RoCE::

    $ devlink dev param set pci/0000:06:00.0 name enable_roce value false cmode driverinit
    $ devlink dev reload pci/0000:06:00.0

- Read RoCE enablement state::

    $ devlink dev param show pci/0000:06:00.0 name enable_roce
      pci/0000:06:00.0:
      name enable_roce type generic
      values:
         cmode driverinit value true

esw_port_metadata: Eswitch port metadata state
----------------------------------------------
When applicable, disabling eswitch metadata can increase packet rate
up to 20% depending on the use case and packet sizes.

Eswitch port metadata state controls whether to internally tag packets with
metadata. Metadata tagging must be enabled for multi-port RoCE, failover
between representors and stacked devices.
By default metadata is enabled on the supported devices in E-switch.
Metadata is applicable only for E-switch in switchdev mode and
users may disable it when NONE of the below use cases will be in use:
1. HCA is in Dual/multi-port RoCE mode.
2. VF/SF representor bonding (Usually used for Live migration)
3. Stacked devices

When metadata is disabled, the above use cases will fail to initialize if
users try to enable them.

- Show eswitch port metadata::

    $ devlink dev param show pci/0000:06:00.0 name esw_port_metadata
      pci/0000:06:00.0:
        name esw_port_metadata type driver-specific
          values:
            cmode runtime value true

- Disable eswitch port metadata::

    $ devlink dev param set pci/0000:06:00.0 name esw_port_metadata value false cmode runtime

- Change eswitch mode to switchdev mode where after choosing the metadata value::

    $ devlink dev eswitch set pci/0000:06:00.0 mode switchdev

hairpin_num_queues: Number of hairpin queues
--------------------------------------------
We refer to a TC NIC rule that involves forwarding as "hairpin".

Hairpin queues are mlx5 hardware specific implementation for hardware
forwarding of such packets.

- Show the number of hairpin queues::

    $ devlink dev param show pci/0000:06:00.0 name hairpin_num_queues
      pci/0000:06:00.0:
        name hairpin_num_queues type driver-specific
          values:
            cmode driverinit value 2

- Change the number of hairpin queues::

    $ devlink dev param set pci/0000:06:00.0 name hairpin_num_queues value 4 cmode driverinit

hairpin_queue_size: Size of the hairpin queues
----------------------------------------------
Control the size of the hairpin queues.

- Show the size of the hairpin queues::

    $ devlink dev param show pci/0000:06:00.0 name hairpin_queue_size
      pci/0000:06:00.0:
        name hairpin_queue_size type driver-specific
          values:
            cmode driverinit value 1024

- Change the size (in packets) of the hairpin queues::

    $ devlink dev param set pci/0000:06:00.0 name hairpin_queue_size value 512 cmode driverinit

Health reporters
================

tx reporter
-----------
The tx reporter is responsible for reporting and recovering of the following two error scenarios:

- tx timeout
    Report on kernel tx timeout detection.
    Recover by searching lost interrupts.
- tx error completion
    Report on error tx completion.
    Recover by flushing the tx queue and reset it.

tx reporter also support on demand diagnose callback, on which it provides
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

- rx queues' initialization (population) timeout
    Population of rx queues' descriptors on ring initialization is done
    in napi context via triggering an irq. In case of a failure to get
    the minimum amount of descriptors, a timeout would occur, and
    descriptors could be recovered by polling the EQ (Event Queue).
- rx completions with errors (reported by HW on interrupt context)
    Report on rx completion error.
    Recover (if needed) by flushing the related queue and reset it.

rx reporter also supports on demand diagnose callback, on which it
provides real time information of its receive queues' status.

- Diagnose rx queues' status and corresponding completion queue::

    $ devlink health diagnose pci/0000:82:00.0 reporter rx

NOTE: This command has valid output only when interface is up. Otherwise, the command has empty output.

- Show number of rx errors indicated, number of recover flows ended successfully,
  is autorecover enabled, and graceful period from last recover::

    $ devlink health show pci/0000:82:00.0 reporter rx

fw reporter
-----------
The fw reporter implements `diagnose` and `dump` callbacks.
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
The fw fatal reporter implements `dump` and `recover` callbacks.
It follows fatal errors indications by CR-space dump and recover flow.
The CR-space dump uses vsc interface which is valid even if the FW command
interface is not functional, which is the case in most FW fatal errors.
The recover function runs recover flow which reloads the driver and triggers fw
reset if needed.
On firmware error, the health buffer is dumped into the dmesg. The log
level is derived from the error's severity (given in health buffer).

User commands examples:

- Run fw recover flow manually::

    $ devlink health recover pci/0000:82:00.0 reporter fw_fatal

- Read FW CR-space dump if already stored or trigger new one::

    $ devlink health dump show pci/0000:82:00.1 reporter fw_fatal

NOTE: This command can run only on PF.

vnic reporter
-------------
The vnic reporter implements only the `diagnose` callback.
It is responsible for querying the vnic diagnostic counters from fw and displaying
them in realtime.

Description of the vnic counters:
total_q_under_processor_handle: number of queues in an error state due to
an async error or errored command.
send_queue_priority_update_flow: number of QP/SQ priority/SL update
events.
cq_overrun: number of times CQ entered an error state due to an
overflow.
async_eq_overrun: number of times an EQ mapped to async events was
overrun.
comp_eq_overrun: number of times an EQ mapped to completion events was
overrun.
quota_exceeded_command: number of commands issued and failed due to quota
exceeded.
invalid_command: number of commands issued and failed dues to any reason
other than quota exceeded.
nic_receive_steering_discard: number of packets that completed RX flow
steering but were discarded due to a mismatch in flow table.

User commands examples:
- Diagnose PF/VF vnic counters
        $ devlink health diagnose pci/0000:82:00.1 reporter vnic
- Diagnose representor vnic counters (performed by supplying devlink port of the
  representor, which can be obtained via devlink port command)
        $ devlink health diagnose pci/0000:82:00.1/65537 reporter vnic

NOTE: This command can run over all interfaces such as PF/VF and representor ports.
