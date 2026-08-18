.. SPDX-License-Identifier: GPL-2.0

======================================
Motorcomm yt8xxx PHY properties (_DSD)
======================================

This document describes ACPI _DSD device properties for Motorcomm yt8xxx
Ethernet PHYs supported by the in-kernel driver in
``drivers/net/phy/motorcomm.c``.

The properties are exposed on the PHY device object under the MDIO bus ACPI
device (the same objects that are registered via
``fwnode_mdiobus_register_phy()``). MAC-side connection properties such as
``phy-handle`` and ``phy-mode`` are documented in [acpi-mdio-phy]_.

Property names and semantics are intentionally aligned with the Devicetree
binding [motorcomm-yt8xxx]_ so that the same driver code path
(``device_property_*`` on ``&phydev->mdio.dev``) can consume firmware
described either as Devicetree or ACPI _DSD.

UUID and placement
==================

Per [acpi-dsd-properties-rules]_ and [acpi-mdio-phy]_, properties must be
placed in an _DSD package using the standard Device Properties UUID::

	ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301")

Properties
==========

Unless noted otherwise, integer properties use the same allowed values and
defaults as [motorcomm-yt8xxx]_.

``rx-internal-delay-ps`` (u32, optional)
  RGMII RX internal delay in picoseconds. Only meaningful when the link is
  using RGMII modes with RX internal delay; see [motorcomm-yt8xxx]_.

``tx-internal-delay-ps`` (u32, optional)
  RGMII TX internal delay in picoseconds. Only meaningful when the link is
  using RGMII modes with TX internal delay; see [motorcomm-yt8xxx]_.

``motorcomm,clk-out-frequency-hz`` (u32, optional)
  Clock output frequency on the PHY clock output pin. Allowed values and
  default are defined in [motorcomm-yt8xxx]_.

``motorcomm,keep-pll-enabled`` (boolean, optional)
  If true, keep the PLL enabled even when there is no link (useful for using
  the clock output without an Ethernet link). See [motorcomm-yt8xxx]_.

``motorcomm,auto-sleep-disabled`` (boolean, optional)
  If true, disable the PHY auto-sleep behavior described in
  [motorcomm-yt8xxx]_.

``motorcomm,rx-clk-drv-microamp`` (u32, optional)
  Drive strength for the ``rx_clk`` RGMII pad in microamps. Allowed values
  depend on the configured RGMII LDO voltage; see [motorcomm-yt8xxx]_.

``motorcomm,rx-data-drv-microamp`` (u32, optional)
  Drive strength for the ``rx_data`` and ``rx_ctl`` RGMII pads in microamps.
  See [motorcomm-yt8xxx]_.

``motorcomm,tx-clk-adj-enabled`` (boolean, optional)
  Enables adjustments related to ``motorcomm,tx-clk-*-inverted`` usage; see
  [motorcomm-yt8xxx]_.

``motorcomm,tx-clk-10-inverted`` (boolean, optional)
``motorcomm,tx-clk-100-inverted`` (boolean, optional)
``motorcomm,tx-clk-1000-inverted`` (boolean, optional)
  Per-speed TX clock inversion options; see [motorcomm-yt8xxx]_.

ASL example (illustrative)
==========================

The example below only shows PHY-local _DSD properties. A real platform
still needs a MAC ``phy-handle`` and ``phy-mode`` package as in
[acpi-mdio-phy]_.

.. code-block:: none

	Scope (\_SB.MDI0)
	{
		Device (PHY4)
		{
			Name (_ADR, 0x4)

			Name (_DSD, Package () {
				ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
				Package () {
					Package (2) { "rx-internal-delay-ps", 2100 },
					Package (2) { "tx-internal-delay-ps", 150 },
					Package (2) { "motorcomm,clk-out-frequency-hz", 0 },
					Package (2) { "motorcomm,keep-pll-enabled", 1 },
					Package (2) { "motorcomm,auto-sleep-disabled", 1 },
				}
			})
		}
	}

References
==========

.. [acpi-mdio-phy] Documentation/firmware-guide/acpi/dsd/phy.rst
.. [acpi-dsd-properties-rules]
   Documentation/firmware-guide/acpi/DSD-properties-rules.rst
.. [motorcomm-yt8xxx]
   Documentation/devicetree/bindings/net/motorcomm,yt8xxx.yaml
