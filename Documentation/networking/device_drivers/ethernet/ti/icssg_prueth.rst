.. SPDX-License-Identifier: GPL-2.0

==============================================
Texas Instruments ICSSG PRUETH ethernet driver
==============================================

:Version: 1.0

ICSSG Firmware
==============

Every ICSSG core has two Programmable Real-Time Unit(PRUs), two auxiliary
Real-Time Transfer Unit (RTUs), and two Transmit Real-Time Transfer Units
(TX_PRUs). Each one of these runs its own firmware. The firmwares combnined are
referred as ICSSG Firmware.

Firmware Statistics
===================

The ICSSG firmware maintains certain statistics which are dumped by the driver
via ``ethtool -S <interface>``

These statistics are as follows,

 - ``FW_RTU_PKT_DROP``: Diagnostic error counter which increments when RTU drops a locally injected packet due to port being disabled or rule violation.
 - ``FW_Q0_OVERFLOW``: TX overflow counter for queue0
 - ``FW_Q1_OVERFLOW``: TX overflow counter for queue1
 - ``FW_Q2_OVERFLOW``: TX overflow counter for queue2
 - ``FW_Q3_OVERFLOW``: TX overflow counter for queue3
 - ``FW_Q4_OVERFLOW``: TX overflow counter for queue4
 - ``FW_Q5_OVERFLOW``: TX overflow counter for queue5
 - ``FW_Q6_OVERFLOW``: TX overflow counter for queue6
 - ``FW_Q7_OVERFLOW``: TX overflow counter for queue7
 - ``FW_DROPPED_PKT``: This counter is incremented when a packet is dropped at PRU because of rule violation.
 - ``FW_RX_ERROR``: Incremented if there was a CRC error or Min/Max frame error at PRU
 - ``FW_RX_DS_INVALID``: Incremented when RTU detects Data Status invalid condition
 - ``FW_TX_DROPPED_PACKET``: Counter for packets dropped via TX Port
 - ``FW_TX_TS_DROPPED_PACKET``: Counter for packets with TS flag dropped via TX Port
 - ``FW_INF_PORT_DISABLED``: Incremented when RX frame is dropped due to port being disabled
 - ``FW_INF_SAV``: Incremented when RX frame is dropped due to Source Address violation
 - ``FW_INF_SA_DL``: Incremented when RX frame is dropped due to Source Address being in the denylist
 - ``FW_INF_PORT_BLOCKED``: Incremented when RX frame is dropped due to port being blocked and frame being a special frame
 - ``FW_INF_DROP_TAGGED`` : Incremented when RX frame is dropped for being tagged
 - ``FW_INF_DROP_PRIOTAGGED``: Incremented when RX frame is dropped for being priority tagged
 - ``FW_INF_DROP_NOTAG``: Incremented when RX frame is dropped for being untagged
 - ``FW_INF_DROP_NOTMEMBER``: Incremented when RX frame is dropped for port not being member of VLAN
 - ``FW_RX_EOF_SHORT_FRMERR``: Incremented if End Of Frame (EOF) task is scheduled without seeing RX_B1
 - ``FW_RX_B0_DROP_EARLY_EOF``: Incremented when frame is dropped due to Early EOF
 - ``FW_TX_JUMBO_FRM_CUTOFF``: Incremented when frame is cut off to prevent packet size > 2000 Bytes
 - ``FW_RX_EXP_FRAG_Q_DROP``: Incremented when express frame is received in the same queue as the previous fragment
 - ``FW_RX_FIFO_OVERRUN``: RX fifo overrun counter
 - ``FW_CUT_THR_PKT``: Incremented when a packet is forwarded using Cut-Through forwarding method
 - ``FW_HOST_RX_PKT_CNT``: Number of valid packets sent by Rx PRU to Host on PSI
 - ``FW_HOST_TX_PKT_CNT``: Number of valid packets copied by RTU0 to Tx queues
 - ``FW_HOST_EGRESS_Q_PRE_OVERFLOW``: Host Egress Q (Pre-emptible) Overflow Counter
 - ``FW_HOST_EGRESS_Q_EXP_OVERFLOW``: Host Egress Q (Pre-emptible) Overflow Counter
