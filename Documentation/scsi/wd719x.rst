.. SPDX-License-Identifier: GPL-2.0

===============================================================
Driver for Western Digital WD7193, WD7197 and WD7296 SCSI cards
===============================================================

The card requires firmware that can be cut out of the Windows NT driver that
can be downloaded from WD at:
http://support.wdc.com/product/download.asp?groupid=801&sid=27&lang=en

There is no license anywhere in the file or on the page - so the firmware
probably cannot be added to linux-firmware.

This script downloads and extracts the firmware, creating wd719x-risc.bin and
d719x-wcs.bin files. Put them in /lib/firmware/::

	#!/bin/sh
	wget http://support.wdc.com/download/archive/pciscsi.exe
	lha xi pciscsi.exe pci-scsi.exe
	lha xi pci-scsi.exe nt/wd7296a.sys
	rm pci-scsi.exe
	dd if=wd7296a.sys of=wd719x-risc.bin bs=1 skip=5760 count=14336
	dd if=wd7296a.sys of=wd719x-wcs.bin bs=1 skip=20096 count=514
	rm wd7296a.sys
