.. SPDX-License-Identifier: GPL-2.0

===================================
Writing PCI Host Controller Drivers
===================================

:Author: Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>

Introduction
============

A PCI Host Controller driver controls PCI Root Complex (RC) hardware.  The
Root Complex hardware comprises a single PCI Host Bridge and one or more
Root Port or Root Complex Integrated Endpoint (RCiEP) devices::

                     +------------------+
                     |       CPU        |
                     +------------------+
                              |
        +--------------------------------------------+
        |                     |               Root   |
        |            +------------------+   Complex  |
        |            |   Host Bridge    |            |
        |            +------------------+            |
        |                     |                      |
        |               Bus 0 |                      |
        |        +------------|----------+           |
        |        |            |          |           |
        |  +----------+ +----------+ +-------+       |
        |  |  Root    | |  Root    | | RCiEP |       |
        |  |  Port    | |  Port    | +-------+       |
        |  +----------+ +----------+                 |
        |       |             |                      |
        | Bus 1 |       Bus 2 |                      |
        |       |             |                      |
        +-------|-------------|----------------------+
                |             |
          +-----------+ +-----------+
          | Endpoint  | | Endpoint  |
          +-----------+ +-----------+

Host Bridge: Used to connect CPU(s) to the PCI hierarchy.

Root Port: Virtual PCI-PCI bridge connecting the Host Bridge to a PCI bus.

RCiEP: Embedded PCIe Endpoint inside Root Complex connected to the Host
Bridge.

Endpoint: PCIe device connected to a Root Port through a Link.

Enumeration
===========

The Host Bridge device is not discoverable, so it is typically enumerated
with the help of firmware interfaces like ACPI or Devicetree. But the Root
Port and RCiEP devices are discoverable through the standard enumeration
process defined in the PCIe spec.

A Host Controller driver usually configures both Host Bridge and Root
Port(s) based on platform requirements. In the case of ACPI on standardized
platforms (e.g. x86), no platform-specific host controller driver is
required as the firmware configures the Root Complex before OS boot and
exposes the resource information through ACPI tables. For more info, refer
to :doc:`../acpi-info`.

For Devicetree platforms, a dedicated host controller driver is often
required because the Root Complex hardware typically needs vendor-specific
initialization like PHY, clocks, power domains, and there is no standard
mechanism equivalent to ACPI/MCFG to convey resource information to the OS.
So on these platforms, Root Complex hardware is enumerated through
Devicetree nodes as below::

        pcie@10000000 {
            compatible = "vendor,soc-pcie";
            reg = <0x0 0x10000000 0x0 0x1000>,
                  <0x0 0x10001000 0x0 0x1000>;
            reg-names = "cfg", "app";
            device_type = "pci";
            bus-range = <0x00 0xff>;
            linux,pci-domain = <0>;
            num-lanes = <4>;

            #address-cells = <3>;
            #size-cells = <2>;

            ranges = <0x01000000 0x0 0x00000000 0x0 0x20000000 0x0 0x00100000>,
                     <0x02000000 0x0 0x20100000 0x0 0x20100000 0x0 0x1ff00000>;
            dma-ranges = <0x02000000 0x0 0x0 0x0 0x0 0x0 0x80000000>;

            clocks = <&clkc PCIE_CORE_CLK>,
                     <&clkc PCIE_AUX_CLK>;
            clock-names = "core", "aux";
            resets = <&reset PCIE_RESET>;
            power-domains = <&power PCIE_PD>;

            #interrupt-cells = <1>;
            interrupt-map-mask = <0 0 0 0x7>;
            interrupt-map = <0 0 0 1 &gic 0 0 GIC_SPI 100 IRQ_TYPE_LEVEL_HIGH>,
                            <0 0 0 2 &gic 0 0 GIC_SPI 101 IRQ_TYPE_LEVEL_HIGH>,
                            <0 0 0 3 &gic 0 0 GIC_SPI 102 IRQ_TYPE_LEVEL_HIGH>,
                            <0 0 0 4 &gic 0 0 GIC_SPI 103 IRQ_TYPE_LEVEL_HIGH>;
            interrupts = <GIC_SPI 104 IRQ_TYPE_LEVEL_HIGH>;
            interrupt-names = "msi";

            pcie@0 {
                compatible = "pciclass,0604";
                device_type = "pci";
                reg = <0x0 0x0 0x0 0x0 0x0>;
                bus-range = <0x01 0xff>;

                #address-cells = <3>;
                #size-cells = <2>;
                ranges;

                phys = <&pcie_phy>;
                reset-gpios = <&gpio 10 GPIO_ACTIVE_LOW>;
                wake-gpios = <&gpio 11 GPIO_ACTIVE_LOW>;
            };
        };


Note the presence of two nodes in the above example. The ``pcie@10000000``
node represents a PCI Host Bridge device, and ``pcie@0`` represents a
single Root Port device. The Host Bridge node should contain properties
associated with the Host Bridge device such as ranges, interrupts, clocks,
power-domains etc... and the Root Port node should contain port-specific
properties such as phys, reset-gpios, wake-gpios etc...

NOTE: Legacy Devicetrees used a single node to describe both Host Bridge
and Root Port devices, but that design is now deprecated.

Driver Design
=============

Prerequisites
-------------

Before starting to write a new Host Controller driver, check if any of the
existing drivers can be reused. For example, if the Root Complex supports
the Enhanced Configuration Access Mechanism (ECAM) and the bootloader has
configured the ECAM mapping before OS boot, the ``CONFIG_PCI_HOST_GENERIC``
driver can be used.

If the Root Complex hardware (IP) is from IP vendors such as Synopsys or
Cadence, the existing ``CONFIG_PCIE_DW_PLAT_HOST`` and
``CONFIG_PCIE_CADENCE_PLAT_HOST`` drivers can be reused. If not, then check
if any of the existing glue drivers available for these IPs could be
reused.

If the Root Complex hardware is designed in-house by the SoC vendor, check
if there is an existing driver from the vendor for their previous
generation Root Complex hardware. Often, the existing driver can be
reused with minimal modifications.

Only if the Root Complex doesn't satisfy above prerequisites should a new
Host Controller driver be written.

Probe
-----

During the Host Controller driver probe(), it initializes the Root Complex
hardware and registers the Host Bridge with the PCI core. The typical steps
are described below.

Initialize Resources
~~~~~~~~~~~~~~~~~~~~

At the start of the probe(), initialize Host Bridge-specific resources such
as clocks, PHYs, regulators, and resets. These resources are described in
the Host Bridge Devicetree node and should be brought up before accessing
the controller hardware.

NOTE: Use the devm_*() managed APIs wherever possible so the resources are
released automatically on probe failure and on driver detach.

Configuration Space Access
~~~~~~~~~~~~~~~~~~~~~~~~~~

The PCI core accesses the Configuration Space of the enumerated devices
through the callbacks provided by the driver in struct pci_ops. These
callbacks abstract how the Root Complex generates a Configuration Request
for a given Bus, Device and Function number.

If the Root Complex supports ECAM, the generic accessors can be reused by
using pci_ecam_map_bus() along with pci_generic_config_read() and
pci_generic_config_write(). Such drivers can often be built on top of
pci_host_common_probe() without providing any custom accessors.

Setup Address Translation
~~~~~~~~~~~~~~~~~~~~~~~~~

The Host Bridge translates accesses between the CPU address domain and the
PCI address domain in both directions:

- Outbound: CPU addresses are translated to PCI bus addresses for the
  Memory and I/O accesses initiated by the CPU towards the downstream
  devices. These windows are derived from the ``ranges`` property of the
  Host Bridge Devicetree node.

- Inbound: PCI bus addresses are translated to system memory addresses for
  the accesses (such as DMA) initiated by the downstream devices. These
  windows are derived from the ``dma-ranges`` property.

The PCI core parses ``ranges`` and ``dma-ranges`` into the Host Bridge
resource lists, and the driver programs one translation window per entry.
Note that the CPU address and the PCI bus address of a window may differ,
so the offset between them has to be accounted for while programming the
windows.

NOTE: If the hardware supports ECAM, it is strongly recommended to use ECAM
for the Configuration Space so a translation window need not be
reprogrammed for every Configuration access.

Interrupt Handling
~~~~~~~~~~~~~~~~~~

Downstream devices can signal interrupts either through INTx or through
Message Signaled Interrupts (MSI/MSI-X). The driver has to enable the
mechanisms supported by the Root Complex.

INTx interrupts are conveyed to the Root Complex through the Assert_INTx
and Deassert_INTx messages and are then reported as system interrupts. The
driver typically creates an IRQ domain for the four interrupts (INTA to
INTD) and demultiplexes an incoming interrupt to the corresponding virtual
IRQ.

An MSI/MSI-X is signaled by the downstream device as a Memory Write to a
Root Complex-specific address. There are two ways to handle them:

- If the Root Complex integrates its own MSI controller, the driver has to
  create an MSI IRQ domain, program the MSI target address and demultiplex
  the incoming MSIs to the corresponding virtual IRQs. MSI-X is handled
  through the same domain.

- If the MSIs are handled by an external interrupt controller (such as the
  GIC ITS), the Root Complex Devicetree node needs to have an
  ``msi-parent`` property and the driver need not implement an MSI
  controller.


Powering up the Slot/Endpoint
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Power ON any slots or Endpoints connected to the bus with the help of the
PWRCTRL subsystem APIs such as pci_pwrctrl_create_devices() and
pci_pwrctrl_power_on_devices(). Note that this requires defining the
supplies in the Root Port or Endpoint Devicetree node.

Link Training
~~~~~~~~~~~~~

Once the resources are initialized, the driver has to initiate Link
training by enabling the LTSSM (Link Training and Status State Machine) of
the Root Port. If a PERST# signal is present, it should be deasserted to
bring the downstream device out of fundamental reset before enabling the
LTSSM.

Before PERST# is deasserted, the driver must satisfy the power sequencing
delays defined by the PCI Express Card Electromechanical (CEM)
Specification.  The power supplies must be stable for at least T_PVPERL
(``PCIE_T_PVPERL_MS``, 100 ms) and the reference clock must be stable for
at least T_PERST-CLK (``PCIE_T_PERST_CLK_US``, 100 us) before PERST# is
deasserted.

After the LTSSM is enabled, the driver should wait (with a timeout) for the
LTSSM to reach the L0 state, indicating that the Link is up.

Once the Link is up, the PCI Express Base Specification (Conventional
Reset) requires software to wait for at least ``PCIE_RESET_CONFIG_WAIT_MS``
(100 ms) before sending the first Configuration Request to the downstream
device. For a Link operating up to 5.0 GT/s, this delay is counted from the
exit of the Conventional Reset (PERST# deassertion), while for a Link
operating above 5.0 GT/s it is counted from the completion of Link
training. The driver should honor this delay before the bus is scanned.

NOTE: A failure to establish the Link should NOT be treated as a probe
failure unless the Root Port is not Hotplug capable. If the Root Port is
Hotplug capable, then the driver should still register the Host Bridge and
scan the bus, so that the downstream device can be discovered later when
the Link comes up.

Register the Host Bridge
~~~~~~~~~~~~~~~~~~~~~~~~

Finally, allocate the Host Bridge device with devm_pci_alloc_host_bridge(),
assign the Configuration Space accessors (struct pci_ops) to it, and start
the bus scan by calling pci_host_probe(). This is the last step of the
probe().  pci_host_probe() creates the Root bus for the Host Bridge and
scans/enumerates all the Root Port, RCiEP and Endpoint devices connected to
the bus.

If the Root Complex IP is from a known IP vendor, the IP specific helpers
should be reused for the above operations wherever applicable.

Power Management
----------------

A Host Controller driver participates in both runtime and system-wide power
management. In both cases, the driver is responsible for the power state of
the Root Complex hardware, while the PCI core manages the power state of
the enumerated devices.

Runtime PM
~~~~~~~~~~

Runtime PM allows the Root Complex hardware to be powered down when it is
idle.  The driver typically enables runtime PM with pm_runtime_enable() and
takes a reference with pm_runtime_get_sync() during probe(), so that the
controller stays powered while it is in use. The reference is dropped in
remove().

If the Root Complex can be powered down when idle, the driver implements
the runtime_suspend and runtime_resume callbacks to disable and enable the
controller resources such as the clocks, PHYs, and power domain. These
callbacks should manage only the controller resources and must not touch
the state of the enumerated devices, which is handled by the PCI core.

System PM
~~~~~~~~~

During system suspend and resume, the driver has to save and restore the
state of the Root Complex and put the Link into a low power state.

These operations are performed in the _noirq() PM callbacks (for example,
using NOIRQ_SYSTEM_SLEEP_PM_OPS()), because the controller resources such
as the clocks and PHY are shared by all the child devices. Suspending them
earlier would break the child devices whose own suspend callbacks may still
access their Configuration Space.

In the suspend callback, the driver should:

- Broadcast a PME_Turn_Off message and wait for the PME_TO_Ack, so that the
  Link can transition to the L2/L3 state.
- Stop the LTSSM and disable the controller resources such as the clocks,
  PHY and power domain.
- Save any controller state that is not retained across the low power
  state.
- Power off the downstream devices using pci_pwrctrl_power_off_devices().

In the resume callback, the driver should reverse the above by enabling the
controller resources, restoring the saved state, re-initializing the Root
Complex and re-establishing the Link as done during probe().

NOTE: If the Link is in the ASPM L1 (or L1 substates) state, some drivers
keep the Link in L1 across suspend for a faster resume, instead of
transitioning it to L2/L3. This is a driver policy decision based on the
platform and the devices connected.

Shutdown
--------

The shutdown() callback is invoked during system reboot or when
transitioning to a new kernel through kexec. Its purpose is to quiesce the
Root Complex so that the downstream devices cannot corrupt the memory or
interrupt the new kernel.

The driver should:

- Disable the interrupts (INTx and MSI) reported by the Root Complex so
  that no spurious interrupt is delivered to the new kernel.
- Broadcast a PME_Turn_Off message and stop the LTSSM to bring the Link
  down so that any in-flight DMA from the downstream devices is stopped
  before the reset.
- Power down the controller resources.

Unlike remove(), shutdown() does not need to tear down the software state
such as the Root bus, since the system is going down anyway.

NOTE: shutdown() is optional. It is mainly required on platforms where the
downstream devices could perform DMA or raise interrupts during the
transition to reboot or kexec.

Remove
------

remove() is called when the driver is detached, and it should undo
everything done in probe() in the reverse order.

The first step is to remove the enumerated devices and the Root bus, by
calling pci_stop_root_bus() followed by pci_remove_root_bus() under the
pci_lock_rescan_remove() lock. This detaches all the child devices before
the controller resources are released.

After the bus is removed, the driver should:

- Disable the interrupts reported by the Root Complex.
- Stop the LTSSM to bring the Link down.
- Power down the PHY and disable the clocks, regulators and resets.
- Drop the runtime PM reference with pm_runtime_put_sync() and disable
  runtime PM with pm_runtime_disable().

Resources allocated through the devm_*() APIs are released automatically
after remove() returns and need not be freed explicitly.

NOTE: A Host Controller driver is encouraged to be built as a loadable
module, but it should not be removed at runtime if it implements its own
IRQ domains such as MSI or INTx controllers. The IRQ mappings created for
such domains can persist even after the interrupts are released and cannot
be disposed of safely, so tearing down the IRQ domains on removal is
fragile. Such drivers should therefore prevent their removal. See the
following thread for more details:
https://lore.kernel.org/linux-pci/87k085xekg.wl-maz@kernel.org/
