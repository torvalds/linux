.. SPDX-License-Identifier: GPL-2.0

======================================
Subsystem Trace Points: PCI Controller
======================================

Overview
========
The PCI controller tracing system provides tracepoints to monitor controller
level information for debugging purpose. The events normally show up here:

	/sys/kernel/tracing/events/pci_controller

Cf. include/trace/events/pci_controller.h for the events definitions.

Available Tracepoints
=====================

pcie_ltssm_state_transition
---------------------------

Monitors PCIe LTSSM state transition including state and rate information
::

    pcie_ltssm_state_transition  "dev: %s state: %s rate: %s\n"

**Parameters**:

* ``dev`` - PCIe controller instance
* ``state`` - PCIe LTSSM state
* ``rate`` - PCIe date rate

**Example Usage**:

.. code-block:: shell

    # Enable the tracepoint
    echo 1 > /sys/kernel/debug/tracing/events/pci_controller/pcie_ltssm_state_transition/enable

    # Monitor events (the following output is generated when a device is linking)
    cat /sys/kernel/debug/tracing/trace_pipe
       kworker/0:0-9       [000] .....     5.600221: pcie_ltssm_state_transition: dev: a40000000.pcie state: RCVRY_EQ2 rate: 8.0 GT/s
