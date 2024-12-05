======================================================================
Synopsys DesignWare Cores (DWC) PCIe Performance Monitoring Unit (PMU)
======================================================================

DesignWare Cores (DWC) PCIe PMU
===============================

The PMU is a PCIe configuration space register block provided by each PCIe Root
Port in a Vendor-Specific Extended Capability named RAS D.E.S (Debug, Error
injection, and Statistics).

As the name indicates, the RAS DES capability supports system level
debugging, AER error injection, and collection of statistics. To facilitate
collection of statistics, Synopsys DesignWare Cores PCIe controller
provides the following two features:

- one 64-bit counter for Time Based Analysis (RX/TX data throughput and
  time spent in each low-power LTSSM state) and
- one 32-bit counter for Event Counting (error and non-error events for
  a specified lane)

Note: There is no interrupt for counter overflow.

Time Based Analysis
-------------------

Using this feature you can obtain information regarding RX/TX data
throughput and time spent in each low-power LTSSM state by the controller.
The PMU measures data in two categories:

- Group#0: Percentage of time the controller stays in LTSSM states.
- Group#1: Amount of data processed (Units of 16 bytes).

Lane Event counters
-------------------

Using this feature you can obtain Error and Non-Error information in
specific lane by the controller. The PMU event is selected by all of:

- Group i
- Event j within the Group i
- Lane k

Some of the events only exist for specific configurations.

DesignWare Cores (DWC) PCIe PMU Driver
=======================================

This driver adds PMU devices for each PCIe Root Port named based on the SBDF of
the Root Port. For example,

    0001:30:03.0 PCI bridge: Device 1ded:8000 (rev 01)

the PMU device name for this Root Port is dwc_rootport_13018.

The DWC PCIe PMU driver registers a perf PMU driver, which provides
description of available events and configuration options in sysfs, see
/sys/bus/event_source/devices/dwc_rootport_{sbdf}.

The "format" directory describes format of the config fields of the
perf_event_attr structure. The "events" directory provides configuration
templates for all documented events.  For example,
"rx_pcie_tlp_data_payload" is an equivalent of "eventid=0x21,type=0x0".

The "perf list" command shall list the available events from sysfs, e.g.::

    $# perf list | grep dwc_rootport
    <...>
    dwc_rootport_13018/Rx_PCIe_TLP_Data_Payload/        [Kernel PMU event]
    <...>
    dwc_rootport_13018/rx_memory_read,lane=?/               [Kernel PMU event]

Time Based Analysis Event Usage
-------------------------------

Example usage of counting PCIe RX TLP data payload (Units of bytes)::

    $# perf stat -a -e dwc_rootport_13018/Rx_PCIe_TLP_Data_Payload/

The average RX/TX bandwidth can be calculated using the following formula:

    PCIe RX Bandwidth = rx_pcie_tlp_data_payload / Measure_Time_Window
    PCIe TX Bandwidth = tx_pcie_tlp_data_payload / Measure_Time_Window

Lane Event Usage
-------------------------------

Each lane has the same event set and to avoid generating a list of hundreds
of events, the user need to specify the lane ID explicitly, e.g.::

    $# perf stat -a -e dwc_rootport_13018/rx_memory_read,lane=4/

The driver does not support sampling, therefore "perf record" will not
work. Per-task (without "-a") perf sessions are not supported.
