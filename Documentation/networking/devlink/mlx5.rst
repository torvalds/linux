.. SPDX-License-Identifier: GPL-2.0

====================
mlx5 devlink support
====================

This document describes the devlink features implemented by the ``mlx5``
device driver.

Parameters
==========

.. list-table:: Generic parameters implemented

   * - Name
     - Mode
     - Validation
   * - ``enable_roce``
     - driverinit
     - Type: Boolean

       If the device supports RoCE disablement, RoCE enablement state controls
       device support for RoCE capability. Otherwise, the control occurs in the
       driver stack. When RoCE is disabled at the driver level, only raw
       ethernet QPs are supported.
   * - ``io_eq_size``
     - driverinit
     - The range is between 64 and 4096.
   * - ``event_eq_size``
     - driverinit
     - The range is between 64 and 4096.
   * - ``max_macs``
     - driverinit
     - The range is between 1 and 2^31. Only power of 2 values are supported.

The ``mlx5`` driver also implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``flow_steering_mode``
     - string
     - runtime
     - Controls the flow steering mode of the driver

       * ``dmfs`` Device managed flow steering. In DMFS mode, the HW
         steering entities are created and managed through firmware.
       * ``smfs`` Software managed flow steering. In SMFS mode, the HW
         steering entities are created and manage through the driver without
         firmware intervention.

       SMFS mode is faster and provides better rule insertion rate compared to
       default DMFS mode.
   * - ``fdb_large_groups``
     - u32
     - driverinit
     - Control the number of large groups (size > 1) in the FDB table.

       * The default value is 15, and the range is between 1 and 1024.
   * - ``esw_multiport``
     - Boolean
     - runtime
     - Control MultiPort E-Switch shared fdb mode.

       An experimental mode where a single E-Switch is used and all the vports
       and physical ports on the NIC are connected to it.

       An example is to send traffic from a VF that is created on PF0 to an
       uplink that is natively associated with the uplink of PF1

       Note: Future devices, ConnectX-8 and onward, will eventually have this
       as the default to allow forwarding between all NIC ports in a single
       E-switch environment and the dual E-switch mode will likely get
       deprecated.

       Default: disabled
   * - ``esw_port_metadata``
     - Boolean
     - runtime
     - When applicable, disabling eswitch metadata can increase packet rate up
       to 20% depending on the use case and packet sizes.

       Eswitch port metadata state controls whether to internally tag packets
       with metadata. Metadata tagging must be enabled for multi-port RoCE,
       failover between representors and stacked devices. By default metadata is
       enabled on the supported devices in E-switch. Metadata is applicable only
       for E-switch in switchdev mode and users may disable it when NONE of the
       below use cases will be in use:
       1. HCA is in Dual/multi-port RoCE mode.
       2. VF/SF representor bonding (Usually used for Live migration)
       3. Stacked devices

       When metadata is disabled, the above use cases will fail to initialize if
       users try to enable them.
   * - ``hairpin_num_queues``
     - u32
     - driverinit
     - We refer to a TC NIC rule that involves forwarding as "hairpin".
       Hairpin queues are mlx5 hardware specific implementation for hardware
       forwarding of such packets.

       Control the number of hairpin queues.
   * - ``hairpin_queue_size``
     - u32
     - driverinit
     - Control the size (in packets) of the hairpin queues.

The ``mlx5`` driver supports reloading via ``DEVLINK_CMD_RELOAD``

Info versions
=============

The ``mlx5`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw.psid``
     - fixed
     - Used to represent the board id of the device.
   * - ``fw.version``
     - stored, running
     - Three digit major.minor.subminor firmware version number.

Health reporters
================

tx reporter
-----------
The tx reporter is responsible for reporting and recovering of the following three error scenarios:

- tx timeout
    Report on kernel tx timeout detection.
    Recover by searching lost interrupts.
- tx error completion
    Report on error tx completion.
    Recover by flushing the tx queue and reset it.
- tx PTP port timestamping CQ unhealthy
    Report too many CQEs never delivered on port ts CQ.
    Recover by flushing and re-creating all PTP channels.

tx reporter also support on demand diagnose callback, on which it provides
real time information of its send queues status.

User commands examples:

- Diagnose send queues status::

    $ devlink health diagnose pci/0000:82:00.0 reporter tx

.. note::
   This command has valid output only when interface is up, otherwise the command has empty output.

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

.. note::
   This command has valid output only when interface is up. Otherwise, the command has empty output.

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

.. note::
   This command can run only on the PF which has fw tracer ownership,
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

.. note::
   This command can run only on PF.

vnic reporter
-------------
The vnic reporter implements only the `diagnose` callback.
It is responsible for querying the vnic diagnostic counters from fw and displaying
them in realtime.

Description of the vnic counters:

- total_q_under_processor_handle
        number of queues in an error state due to
        an async error or errored command.
- send_queue_priority_update_flow
        number of QP/SQ priority/SL update events.
- cq_overrun
        number of times CQ entered an error state due to an overflow.
- async_eq_overrun
        number of times an EQ mapped to async events was overrun.
        comp_eq_overrun number of times an EQ mapped to completion events was
        overrun.
- quota_exceeded_command
        number of commands issued and failed due to quota exceeded.
- invalid_command
        number of commands issued and failed dues to any reason other than quota
        exceeded.
- nic_receive_steering_discard
        number of packets that completed RX flow
        steering but were discarded due to a mismatch in flow table.
- generated_pkt_steering_fail
	number of packets generated by the VNIC experiencing unexpected steering
	failure (at any point in steering flow).
- handled_pkt_steering_fail
	number of packets handled by the VNIC experiencing unexpected steering
	failure (at any point in steering flow owned by the VNIC, including the FDB
	for the eswitch owner).

User commands examples:

- Diagnose PF/VF vnic counters::

        $ devlink health diagnose pci/0000:82:00.1 reporter vnic

- Diagnose representor vnic counters (performed by supplying devlink port of the
  representor, which can be obtained via devlink port command)::

        $ devlink health diagnose pci/0000:82:00.1/65537 reporter vnic

.. note::
   This command can run over all interfaces such as PF/VF and representor ports.
