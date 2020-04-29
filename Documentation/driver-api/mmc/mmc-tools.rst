======================
MMC tools introduction
======================

There is one MMC test tools called mmc-utils, which is maintained by Chris Ball,
you can find it at the below public git repository:

	http://git.kernel.org/cgit/linux/kernel/git/cjb/mmc-utils.git/

Functions
=========

The mmc-utils tools can do the following:

 - Print and parse extcsd data.
 - Determine the eMMC writeprotect status.
 - Set the eMMC writeprotect status.
 - Set the eMMC data sector size to 4KB by disabling emulation.
 - Create general purpose partition.
 - Enable the enhanced user area.
 - Enable write reliability per partition.
 - Print the response to STATUS_SEND (CMD13).
 - Enable the boot partition.
 - Set Boot Bus Conditions.
 - Enable the eMMC BKOPS feature.
 - Permanently enable the eMMC H/W Reset feature.
 - Permanently disable the eMMC H/W Reset feature.
 - Send Sanitize command.
 - Program authentication key for the device.
 - Counter value for the rpmb device will be read to stdout.
 - Read from rpmb device to output.
 - Write to rpmb device from data file.
 - Enable the eMMC cache feature.
 - Disable the eMMC cache feature.
 - Print and parse CID data.
 - Print and parse CSD data.
 - Print and parse SCR data.
