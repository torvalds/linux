.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver asus_wmi_ec_sensors
=================================

Supported boards:
 * PRIME X570-PRO,
 * Pro WS X570-ACE,
 * ROG CROSSHAIR VIII DARK HERO,
 * ROG CROSSHAIR VIII FORMULA,
 * ROG CROSSHAIR VIII HERO,
 * ROG STRIX B550-E GAMING,
 * ROG STRIX B550-I GAMING,
 * ROG STRIX X570-E GAMING.

Authors:
    - Eugene Shalygin <eugene.shalygin@gmail.com>

Description:
------------
ASUS mainboards publish hardware monitoring information via Super I/O
chip and the ACPI embedded controller (EC) registers. Some of the sensors
are only available via the EC.

ASUS WMI interface provides a method (BREC) to read data from EC registers,
which is utilized by this driver to publish those sensor readings to the
HWMON system. The driver is aware of and reads the following sensors:

1. Chipset (PCH) temperature
2. CPU package temperature
3. Motherboard temperature
4. Readings from the T_Sensor header
5. VRM temperature
6. CPU_Opt fan RPM
7. Chipset fan RPM
8. Readings from the "Water flow meter" header (RPM)
9. Readings from the "Water In" and "Water Out" temperature headers
10. CPU current
