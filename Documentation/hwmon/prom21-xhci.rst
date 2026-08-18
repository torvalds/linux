.. SPDX-License-Identifier: GPL-2.0

Kernel driver prom21-xhci
=========================

Supported chips:

  * AMD Promontory 21 (PROM21) xHCI USB host controller

    Prefix: 'prom21_xhci'

    PCI IDs: 1022:43fc, 1022:43fd

Author:

  - Jihong Min <hurryman2212@gmail.com>

Description
-----------

This driver exposes the temperature sensor in AMD PROM21 xHCI controllers.

The driver binds to an auxiliary device created by the xHCI PCI driver for
supported controllers. The sensor value is accessed through a vendor-specific
index/data register pair in the controller's PCI MMIO BAR.
The auxiliary device is created by the ``xhci-pci-prom21`` PCI glue driver.
USB host operation is otherwise delegated to the common ``xhci-pci`` code.

PROM21 is an AMD chipset IP used in single-chip or daisy-chained configurations
to build AMD 6xx/8xx series chipsets. Since the xHCI controllers are
integrated in PROM21, this temperature can also be used as a monitor for a
temperature close to the AMD chipset temperature.

Register access
---------------

The temperature value is read through a vendor-specific index/data register
pair in the xHCI PCI MMIO BAR. The driver uses the following byte offsets from
the MMIO BAR base:

======================= =====================================================
0x3000			Vendor index register
0x3008			Vendor data register
======================= =====================================================

The driver saves the current vendor index register value, writes the
temperature selector ``0x0001e520`` to the vendor index register, reads the
vendor data register, and restores the previous vendor index value before
returning. The raw temperature value is the low 8 bits of the vendor data
register value.

The hwmon core serializes this driver's callbacks, and the driver restores the
previous index value after each read. This does not provide synchronization
with firmware, SMM, ACPI AML, or any other user outside this driver.

No public AMD reference is available for the register pair or the raw value.
The register pair was identified on an X870E system with two PROM21 xHCI
controllers. One controller was passed through to a Windows VM, and the same
controller's PCI MMIO BAR was observed from the Linux host while HWiNFO64 was
reporting the PROM21 xHCI temperature. In the test environment, the reported
temperature was very stable at idle and the displayed sensor resolution was
low, which made it possible to look for a consistently repeating MMIO response
for the same reported temperature. During observation, offset 0x3000 repeatedly
contained selector ``0x0001e520``. Writing the same selector to offset 0x3000
from Linux and then reading offset 0x3008 reproduced the same raw value, so the
offsets are treated as a vendor index/data register pair.

The conversion formula was empirically inferred by matching observed raw
8-bit values against HWiNFO64's reported PROM21 xHCI temperature for the same
controller. The observed mapping is:

  temp[C] = raw * 0.9066 - 78.624

Runtime PM
----------

The driver does not wake the xHCI PCI device for hwmon reads. It reads the
temperature only when the parent device is already active. A read from a
suspended device returns ``-ENODATA``. After a successful read, the driver
drops its active-only runtime PM reference and lets the PM core re-evaluate the
idle state.

Sysfs entries
-------------

======================= =====================================================
temp1_input		Temperature in millidegrees Celsius
======================= =====================================================

The hwmon device name is ``prom21_xhci``. The sysfs path depends on the hwmon
device number assigned by the kernel. Userspace can locate the device by
matching the ``name`` attribute:

.. code-block:: sh

   for hwmon in /sys/class/hwmon/hwmon*; do
           [ "$(cat "$hwmon/name")" = "prom21_xhci" ] || continue
           cat "$hwmon/temp1_input"
   done

If the raw register value is invalid, ``temp1_input`` returns ``-ENODATA``.
