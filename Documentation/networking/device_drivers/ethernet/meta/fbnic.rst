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

Upgrading Firmware
------------------

fbnic supports updating firmware using signed PLDM images with devlink dev
flash. PLDM images are written into the flash. Flashing does not interrupt
the operation of the device.

On host boot the latest UEFI driver is always used, no explicit activation
is required. Firmware activation is required to run new control firmware. cmrt
firmware can only be activated by power cycling the NIC.

Statistics
----------

TX MAC Interface
~~~~~~~~~~~~~~~~

 - ``ptp_illegal_req``: packets sent to the NIC with PTP request bit set but routed to BMC/FW
 - ``ptp_good_ts``: packets successfully routed to MAC with PTP request bit set
 - ``ptp_bad_ts``: packets destined for MAC with PTP request bit set but aborted because of some error (e.g., DMA read error)

TX Extension (TEI) Interface (TTI)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

 - ``tti_cm_drop``: control messages dropped at the TX Extension (TEI) Interface because of credit starvation
 - ``tti_frame_drop``: packets dropped at the TX Extension (TEI) Interface because of credit starvation
 - ``tti_tbi_drop``: packets dropped at the TX BMC Interface (TBI) because of credit starvation

RXB (RX Buffer) Enqueue
~~~~~~~~~~~~~~~~~~~~~~~

 - ``rxb_integrity_err[i]``: frames enqueued with integrity errors (e.g., multi-bit ECC errors) on RXB input i
 - ``rxb_mac_err[i]``: frames enqueued with MAC end-of-frame errors (e.g., bad FCS) on RXB input i
 - ``rxb_parser_err[i]``: frames experienced RPC parser errors
 - ``rxb_frm_err[i]``: frames experienced signaling errors (e.g., missing end-of-packet/start-of-packet) on RXB input i
 - ``rxb_drbo[i]_frames``: frames received at RXB input i
 - ``rxb_drbo[i]_bytes``: bytes received at RXB input i

RXB (RX Buffer) FIFO
~~~~~~~~~~~~~~~~~~~~

 - ``rxb_fifo[i]_drop``: transitions into the drop state on RXB pool i
 - ``rxb_fifo[i]_dropped_frames``: frames dropped on RXB pool i
 - ``rxb_fifo[i]_ecn``: transitions into the ECN mark state on RXB pool i
 - ``rxb_fifo[i]_level``: current occupancy of RXB pool i

RXB (RX Buffer) Dequeue
~~~~~~~~~~~~~~~~~~~~~~~

   - ``rxb_intf[i]_frames``: frames sent to the output i
   - ``rxb_intf[i]_bytes``: bytes sent to the output i
   - ``rxb_pbuf[i]_frames``: frames sent to output i from the perspective of internal packet buffer
   - ``rxb_pbuf[i]_bytes``: bytes sent to output i from the perspective of internal packet buffer

RPC (Rx parser)
~~~~~~~~~~~~~~~

 - ``rpc_unkn_etype``: frames containing unknown EtherType
 - ``rpc_unkn_ext_hdr``: frames containing unknown IPv6 extension header
 - ``rpc_ipv4_frag``: frames containing IPv4 fragment
 - ``rpc_ipv6_frag``: frames containing IPv6 fragment
 - ``rpc_ipv4_esp``: frames with IPv4 ESP encapsulation
 - ``rpc_ipv6_esp``: frames with IPv6 ESP encapsulation
 - ``rpc_tcp_opt_err``: frames which encountered TCP option parsing error
 - ``rpc_out_of_hdr_err``: frames where header was larger than parsable region
 - ``ovr_size_err``: oversized frames

Hardware Queues
~~~~~~~~~~~~~~~

1. RX DMA Engine:

 - ``rde_[i]_pkt_err``: packets with MAC EOP, RPC parser, RXB truncation, or RDE frame truncation errors. These error are flagged in the packet metadata because of cut-through support but the actual drop happens once PCIE/RDE is reached.
 - ``rde_[i]_pkt_cq_drop``: packets dropped because RCQ is full
 - ``rde_[i]_pkt_bdq_drop``: packets dropped because HPQ or PPQ ran out of host buffer

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
