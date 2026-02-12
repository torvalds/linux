.. SPDX-License-Identifier: GPL-2.0

===========================
Subsystem Trace Points: PCI
===========================

Overview
========
The PCI tracing system provides tracepoints to monitor critical hardware events
that can impact system performance and reliability. These events normally show
up here:

	/sys/kernel/tracing/events/pci

Cf. include/trace/events/pci.h for the events definitions.

Available Tracepoints
=====================

pci_hp_event
------------

Monitors PCI hotplug events including card insertion/removal and link
state changes.
::

    pci_hp_event  "%s slot:%s, event:%s\n"

**Event Types**:

* ``LINK_UP`` - PCIe link established
* ``LINK_DOWN`` - PCIe link lost
* ``CARD_PRESENT`` - Card detected in slot
* ``CARD_NOT_PRESENT`` - Card removed from slot

**Example Usage**::

    # Enable the tracepoint
    echo 1 > /sys/kernel/debug/tracing/events/pci/pci_hp_event/enable

    # Monitor events (the following output is generated when a device is hotplugged)
    cat /sys/kernel/debug/tracing/trace_pipe
       irq/51-pciehp-88      [001] .....  1311.177459: pci_hp_event: 0000:00:02.0 slot:10, event:CARD_PRESENT

       irq/51-pciehp-88      [001] .....  1311.177566: pci_hp_event: 0000:00:02.0 slot:10, event:LINK_UP

pcie_link_event
---------------

Monitors PCIe link speed changes and provides detailed link status information.
::

    pcie_link_event  "%s type:%d, reason:%d, cur_bus_speed:%d, max_bus_speed:%d, width:%u, flit_mode:%u, status:%s\n"

**Parameters**:

* ``type`` - PCIe device type (4=Root Port, etc.)
* ``reason`` - Reason for link change:

  - ``0`` - Link retrain
  - ``1`` - Bus enumeration
  - ``2`` - Bandwidth notification enable
  - ``3`` - Bandwidth notification IRQ
  - ``4`` - Hotplug event


**Example Usage**::

    # Enable the tracepoint
    echo 1 > /sys/kernel/debug/tracing/events/pci/pcie_link_event/enable

    # Monitor events (the following output is generated when a device is hotplugged)
    cat /sys/kernel/debug/tracing/trace_pipe
       irq/51-pciehp-88      [001] .....   381.545386: pcie_link_event: 0000:00:02.0 type:4, reason:4, cur_bus_speed:20, max_bus_speed:23, width:1, flit_mode:0, status:DLLLA
