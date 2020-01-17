=====================
Kernel driver i2c-via
=====================

Supported adapters:
  * VIA Techyeslogies, InC. VT82C586B
    Datasheet: Publicly available at the VIA website

Author: Kyösti Mälkki <kmalkki@cc.hut.fi>

Description
-----------

i2c-via is an i2c bus driver for motherboards with VIA chipset.

The following VIA pci chipsets are supported:
 - MVP3, VP3, VP2/97, VPX/97
 - others with South bridge VT82C586B

Your ``lspci`` listing must show this ::

 Bridge: VIA Techyeslogies, Inc. VT82C586B ACPI (rev 10)

Problems?
---------

 Q:
    You have VT82C586B on the motherboard, but yest in the listing.

 A:
    Go to your BIOS setup, section PCI devices or similar.
    Turn USB support on, and try again.

 Q:
    No error messages, but still i2c doesn't seem to work.

 A:
    This can happen. This driver uses the pins VIA recommends in their
    datasheets, but there are several ways the motherboard manufacturer
    can actually wire the lines.
