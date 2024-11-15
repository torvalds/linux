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

Statistics
----------

PCIe
~~~~

The fbnic driver exposes PCIe hardware performance statistics through debugfs
(``pcie_stats``). These statistics provide insights into PCIe transaction
behavior and potential performance bottlenecks.

1. PCIe Transaction Counters:

   These counters track PCIe transaction activity:
        - ``pcie_ob_rd_tlp``: Outbound read Transaction Layer Packets count
        - ``pcie_ob_rd_dword``: DWORDs transferred in outbound read transactions
        - ``pcie_ob_wr_tlp``: Outbound write Transaction Layer Packets count
        - ``pcie_ob_wr_dword``: DWORDs transferred in outbound write
	  transactions
        - ``pcie_ob_cpl_tlp``: Outbound completion TLP count
        - ``pcie_ob_cpl_dword``: DWORDs transferred in outbound completion TLPs

2. PCIe Resource Monitoring:

   These counters indicate PCIe resource exhaustion events:
        - ``pcie_ob_rd_no_tag``: Read requests dropped due to tag unavailability
        - ``pcie_ob_rd_no_cpl_cred``: Read requests dropped due to completion
	  credit exhaustion
        - ``pcie_ob_rd_no_np_cred``: Read requests dropped due to non-posted
	  credit exhaustion
