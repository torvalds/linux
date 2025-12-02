.. SPDX-License-Identifier: GPL-2.0-only

================================================
Fujitsu Uncore Performance Monitoring Unit (PMU)
================================================

This driver supports the Uncore MAC PMUs and the Uncore PCI PMUs found
in Fujitsu chips.
Each MAC PMU on these chips is exposed as a uncore perf PMU with device name
mac_iod<iod>_mac<mac>_ch<ch>.
And each PCI PMU on these chips is exposed as a uncore perf PMU with device name
pci_iod<iod>_pci<pci>.

The driver provides a description of its available events and configuration
options in sysfs, see /sys/bus/event_sources/devices/mac_iod<iod>_mac<mac>_ch<ch>/
and /sys/bus/event_sources/devices/pci_iod<iod>_pci<pci>/.
This driver exports:

- formats, used by perf user space and other tools to configure events
- events, used by perf user space and other tools to create events
  symbolically, e.g.::

    perf stat -a -e mac_iod0_mac0_ch0/event=0x21/ ls
    perf stat -a -e pci_iod0_pci0/event=0x24/ ls

- cpumask, used by perf user space and other tools to know on which CPUs
  to open the events

This driver supports the following events for MAC:

- cycles
  This event counts MAC cycles at MAC frequency.
- read-count
  This event counts the number of read requests to MAC.
- read-count-request
  This event counts the number of read requests including retry to MAC.
- read-count-return
  This event counts the number of responses to read requests to MAC.
- read-count-request-pftgt
  This event counts the number of read requests including retry with PFTGT
  flag.
- read-count-request-normal
  This event counts the number of read requests including retry without PFTGT
  flag.
- read-count-return-pftgt-hit
  This event counts the number of responses to read requests which hit the
  PFTGT buffer.
- read-count-return-pftgt-miss
  This event counts the number of responses to read requests which miss the
  PFTGT buffer.
- read-wait
  This event counts outstanding read requests issued by DDR memory controller
  per cycle.
- write-count
  This event counts the number of write requests to MAC (including zero write,
  full write, partial write, write cancel).
- write-count-write
  This event counts the number of full write requests to MAC (not including
  zero write).
- write-count-pwrite
  This event counts the number of partial write requests to MAC.
- memory-read-count
  This event counts the number of read requests from MAC to memory.
- memory-write-count
  This event counts the number of full write requests from MAC to memory.
- memory-pwrite-count
  This event counts the number of partial write requests from MAC to memory.
- ea-mac
  This event counts energy consumption of MAC.
- ea-memory
  This event counts energy consumption of memory.
- ea-memory-mac-write
  This event counts the number of write requests from MAC to memory.
- ea-ha
  This event counts energy consumption of HA.

  'ea' is the abbreviation for 'Energy Analyzer'.

Examples for use with perf::

  perf stat -e mac_iod0_mac0_ch0/ea-mac/ ls

And, this driver supports the following events for PCI:

- pci-port0-cycles
  This event counts PCI cycles at PCI frequency in port0.
- pci-port0-read-count
  This event counts read transactions for data transfer in port0.
- pci-port0-read-count-bus
  This event counts read transactions for bus usage in port0.
- pci-port0-write-count
  This event counts write transactions for data transfer in port0.
- pci-port0-write-count-bus
  This event counts write transactions for bus usage in port0.
- pci-port1-cycles
  This event counts PCI cycles at PCI frequency in port1.
- pci-port1-read-count
  This event counts read transactions for data transfer in port1.
- pci-port1-read-count-bus
  This event counts read transactions for bus usage in port1.
- pci-port1-write-count
  This event counts write transactions for data transfer in port1.
- pci-port1-write-count-bus
  This event counts write transactions for bus usage in port1.
- ea-pci
  This event counts energy consumption of PCI.

  'ea' is the abbreviation for 'Energy Analyzer'.

Examples for use with perf::

  perf stat -e pci_iod0_pci0/ea-pci/ ls

Given that these are uncore PMUs the driver does not support sampling, therefore
"perf record" will not work. Per-task perf sessions are not supported.
