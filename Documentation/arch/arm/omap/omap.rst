============
OMAP history
============

This file contains documentation for running mainline
kernel on omaps.

======		======================================================
KERNEL		NEW DEPENDENCIES
======		======================================================
v4.3+		Update is needed for custom .config files to make sure
		CONFIG_REGULATOR_PBIAS is enabled for MMC1 to work
		properly.

v4.18+		Update is needed for custom .config files to make sure
		CONFIG_MMC_SDHCI_OMAP is enabled for all MMC instances
		to work in DRA7 and K2G based boards.
======		======================================================
