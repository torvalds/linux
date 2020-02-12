.. SPDX-License-Identifier: GPL-2.0

Kernel driver drivetemp
=======================


References
----------

ANS T13/1699-D
Information technology - AT Attachment 8 - ATA/ATAPI Command Set (ATA8-ACS)

ANS Project T10/BSR INCITS 513
Information technology - SCSI Primary Commands - 4 (SPC-4)

ANS Project INCITS 557
Information technology - SCSI / ATA Translation - 5 (SAT-5)


Description
-----------

This driver supports reporting the temperature of disk and solid state
drives with temperature sensors.

If supported, it uses the ATA SCT Command Transport feature to read
the current drive temperature and, if available, temperature limits
as well as historic minimum and maximum temperatures. If SCT Command
Transport is not supported, the driver uses SMART attributes to read
the drive temperature.


Sysfs entries
-------------

Only the temp1_input attribute is always available. Other attributes are
available only if reported by the drive. All temperatures are reported in
milli-degrees Celsius.

=======================	=====================================================
temp1_input		Current drive temperature
temp1_lcrit		Minimum temperature limit. Operating the device below
			this temperature may cause physical damage to the
			device.
temp1_min		Minimum recommended continuous operating limit
temp1_max		Maximum recommended continuous operating temperature
temp1_crit		Maximum temperature limit. Operating the device above
			this temperature may cause physical damage to the
			device.
temp1_lowest		Minimum temperature seen this power cycle
temp1_highest		Maximum temperature seen this power cycle
=======================	=====================================================
