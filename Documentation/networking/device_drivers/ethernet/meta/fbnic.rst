.. SPDX-License-Identifier: GPL-2.0+

=====================================
Meta Platforms Host Network Interface
=====================================

Firmware Versions
-----------------

fbnic has three components stored on the flash which are provided in one PLDM
image:

1. fw - The control firmware used to view and modify firmware settings, request
   firmware actions, and retrieve firmware counters outside of the data path.
   This is the firmware which fbnic_fw.c interacts with.
2. bootloader - The firmware which validate firmware security and control basic
   operations including loading and updating the firmware. This is also known
   as the cmrt firmware.
3. undi - This is the UEFI driver which is based on the Linux driver.

fbnic stores two copies of these three components on flash. This allows fbnic
to fall back to an older version of firmware automatically in case firmware
fails to boot. Version information for both is provided as running and stored.
The undi is only provided in stored as it is not actively running once the Linux
driver takes over.

devlink dev info provides version information for all three components. In
addition to the version the hg commit hash of the build is included as a
separate entry.
