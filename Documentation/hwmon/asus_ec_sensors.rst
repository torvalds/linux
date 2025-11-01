.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver asus_ec_sensors
=================================

Supported boards:
 * MAXIMUS VI HERO
 * PRIME X470-PRO
 * PRIME X570-PRO
 * PRIME X670E-PRO WIFI
 * PRIME Z270-A
 * Pro WS X570-ACE
 * Pro WS WRX90E-SAGE SE
 * ProArt X570-CREATOR WIFI
 * ProArt X670E-CREATOR WIFI
 * ProArt X870E-CREATOR WIFI
 * ProArt B550-CREATOR
 * ROG CROSSHAIR VIII DARK HERO
 * ROG CROSSHAIR VIII HERO (WI-FI)
 * ROG CROSSHAIR VIII FORMULA
 * ROG CROSSHAIR VIII HERO
 * ROG CROSSHAIR VIII IMPACT
 * ROG CROSSHAIR X670E HERO
 * ROG CROSSHAIR X670E GENE
 * ROG MAXIMUS XI HERO
 * ROG MAXIMUS XI HERO (WI-FI)
 * ROG MAXIMUS Z690 FORMULA
 * ROG STRIX B550-E GAMING
 * ROG STRIX B550-I GAMING
 * ROG STRIX B650E-I GAMING WIFI
 * ROG STRIX B850-I GAMING WIFI
 * ROG STRIX X570-E GAMING
 * ROG STRIX X570-E GAMING WIFI II
 * ROG STRIX X570-F GAMING
 * ROG STRIX X570-I GAMING
 * ROG STRIX X670E-E GAMING WIFI
 * ROG STRIX X670E-I GAMING WIFI
 * ROG STRIX X870-I GAMING WIFI
 * ROG STRIX X870E-E GAMING WIFI
 * ROG STRIX Z390-F GAMING
 * ROG STRIX Z490-F GAMING
 * ROG STRIX Z690-A GAMING WIFI D4
 * ROG STRIX Z690-E GAMING WIFI
 * ROG STRIX Z790-E GAMING WIFI II
 * ROG STRIX Z790-I GAMING WIFI
 * ROG ZENITH II EXTREME
 * ROG ZENITH II EXTREME ALPHA
 * TUF GAMING X670E PLUS
 * TUF GAMING X670E PLUS WIFI

Authors:
    - Eugene Shalygin <eugene.shalygin@gmail.com>

Description:
------------
ASUS mainboards publish hardware monitoring information via Super I/O
chip and the ACPI embedded controller (EC) registers. Some of the sensors
are only available via the EC.

The driver is aware of and reads the following sensors:

1. Chipset (PCH) temperature
2. CPU package temperature
3. Motherboard temperature
4. Readings from the T_Sensor header
5. VRM temperature
6. CPU_Opt fan RPM
7. VRM heatsink fan RPM
8. Chipset fan RPM
9. Readings from the "Water flow meter" header (RPM)
10. Readings from the "Water In" and "Water Out" temperature headers
11. CPU current
12. CPU core voltage

Sensor values are read from EC registers, and to avoid race with the board
firmware the driver acquires ACPI mutex, the one used by the WMI when its
methods access the EC.

Module Parameters
-----------------
 * mutex_path: string
		The driver holds path to the ACPI mutex for each board (actually,
		the path is mostly identical for them). If ASUS changes this path
		in a future BIOS update, this parameter can be used to override
		the stored in the driver value until it gets updated.
		A special string ":GLOBAL_LOCK" can be passed to use the ACPI
		global lock instead of a dedicated mutex.
