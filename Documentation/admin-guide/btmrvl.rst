=============
btmrvl driver
=============

All commands are used via debugfs interface.

Set/get driver configurations
=============================

Path:	/debug/btmrvl/config/

gpiogap=[n], hscfgcmd
	These commands are used to configure the host sleep parameters::
	bit 8:0  -- Gap
	bit 16:8 -- GPIO

	where GPIO is the pin number of GPIO used to wake up the host.
	It could be any valid GPIO pin# (e.g. 0-7) or 0xff (SDIO interface
	wakeup will be used instead).

	where Gap is the gap in milli seconds between wakeup signal and
	wakeup event, or 0xff for special host sleep setting.

	Usage::

		# Use SDIO interface to wake up the host and set GAP to 0x80:
		echo 0xff80 > /debug/btmrvl/config/gpiogap
		echo 1 > /debug/btmrvl/config/hscfgcmd

		# Use GPIO pin #3 to wake up the host and set GAP to 0xff:
		echo 0x03ff >  /debug/btmrvl/config/gpiogap
		echo 1 > /debug/btmrvl/config/hscfgcmd

psmode=[n], pscmd
	These commands are used to enable/disable auto sleep mode

	where the option is::

			1 	-- Enable auto sleep mode
			0 	-- Disable auto sleep mode

	Usage::

		# Enable auto sleep mode
		echo 1 > /debug/btmrvl/config/psmode
		echo 1 > /debug/btmrvl/config/pscmd

		# Disable auto sleep mode
		echo 0 > /debug/btmrvl/config/psmode
		echo 1 > /debug/btmrvl/config/pscmd


hsmode=[n], hscmd
	These commands are used to enable host sleep or wake up firmware

	where the option is::

			1	-- Enable host sleep
			0	-- Wake up firmware

	Usage::

		# Enable host sleep
		echo 1 > /debug/btmrvl/config/hsmode
		echo 1 > /debug/btmrvl/config/hscmd

		# Wake up firmware
		echo 0 > /debug/btmrvl/config/hsmode
		echo 1 > /debug/btmrvl/config/hscmd


Get driver status
=================

Path:	/debug/btmrvl/status/

Usage::

	cat /debug/btmrvl/status/<args>

where the args are:

curpsmode
	This command displays current auto sleep status.

psstate
	This command display the power save state.

hsstate
	This command display the host sleep state.

txdnldrdy
	This command displays the value of Tx download ready flag.

Issuing a raw hci command
=========================

Use hcitool to issue raw hci command, refer to hcitool manual

Usage::

	Hcitool cmd <ogf> <ocf> [Parameters]

Interface Control Command::

	hcitool cmd 0x3f 0x5b 0xf5 0x01 0x00    --Enable All interface
	hcitool cmd 0x3f 0x5b 0xf5 0x01 0x01    --Enable Wlan interface
	hcitool cmd 0x3f 0x5b 0xf5 0x01 0x02    --Enable BT interface
	hcitool cmd 0x3f 0x5b 0xf5 0x00 0x00    --Disable All interface
	hcitool cmd 0x3f 0x5b 0xf5 0x00 0x01    --Disable Wlan interface
	hcitool cmd 0x3f 0x5b 0xf5 0x00 0x02    --Disable BT interface

SD8688 firmware
===============

Images:

- /lib/firmware/sd8688_helper.bin
- /lib/firmware/sd8688.bin


The images can be downloaded from:

git.infradead.org/users/dwmw2/linux-firmware.git/libertas/
