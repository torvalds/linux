.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver asus_wmi_sensors
=================================

Supported boards:
 * PRIME X399-A,
 * PRIME X470-PRO,
 * ROG CROSSHAIR VI EXTREME,
 * ROG CROSSHAIR VI HERO,
 * ROG CROSSHAIR VI HERO (WI-FI AC),
 * ROG CROSSHAIR VII HERO,
 * ROG CROSSHAIR VII HERO (WI-FI),
 * ROG STRIX B450-E GAMING,
 * ROG STRIX B450-F GAMING,
 * ROG STRIX B450-I GAMING,
 * ROG STRIX X399-E GAMING,
 * ROG STRIX X470-F GAMING,
 * ROG STRIX X470-I GAMING,
 * ROG ZENITH EXTREME,
 * ROG ZENITH EXTREME ALPHA.

Authors:
    - Ed Brindley <kernel@maidavale.org>

Description:
------------
ASUS mainboards publish hardware monitoring information via WMI interface.

ASUS WMI interface provides a methods to get list of sensors and values of
such, which is utilized by this driver to publish those sensor readings to the
HWMON system.

The driver is aware of and reads the following sensors:
 * CPU Core Voltage,
 * CPU SOC Voltage,
 * DRAM Voltage,
 * VDDP Voltage,
 * 1.8V PLL Voltage,
 * +12V Voltage,
 * +5V Voltage,
 * 3VSB Voltage,
 * VBAT Voltage,
 * AVCC3 Voltage,
 * SB 1.05V Voltage,
 * CPU Core Voltage,
 * CPU SOC Voltage,
 * DRAM Voltage,
 * CPU Fan RPM,
 * Chassis Fan 1 RPM,
 * Chassis Fan 2 RPM,
 * Chassis Fan 3 RPM,
 * HAMP Fan RPM,
 * Water Pump RPM,
 * CPU OPT RPM,
 * Water Flow RPM,
 * AIO Pump RPM,
 * CPU Temperature,
 * CPU Socket Temperature,
 * Motherboard Temperature,
 * Chipset Temperature,
 * Tsensor 1 Temperature,
 * CPU VRM Temperature,
 * Water In,
 * Water Out,
 * CPU VRM Output Current.

Known Issues:
 * The WMI implementation in some of Asus' BIOSes is buggy. This can result in
   fans stopping, fans getting stuck at max speed, or temperature readouts
   getting stuck. This is not an issue with the driver, but the BIOS. The Prime
   X470 Pro seems particularly bad for this. The more frequently the WMI
   interface is polled the greater the potential for this to happen. Until you
   have subjected your computer to an extended soak test while polling the
   sensors frequently, don't leave you computer unattended. Upgrading to new
   BIOS version with method version greater than or equal to two should
   rectify the issue.
 * A few boards report 12v voltages to be ~10v.
