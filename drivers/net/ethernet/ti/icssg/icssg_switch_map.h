/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_ICSSG_SWITCH_MAP_H
#define __NET_TI_ICSSG_SWITCH_MAP_H

/************************* Ethernet Switch Constants *********************/

/* if bucket size is changed in firmware then this too should be changed
 * because it directly impacts FDB ageing calculation
 */
#define NUMBER_OF_FDB_BUCKET_ENTRIES            (4)

/* This is fixed in ICSSG */
#define SIZE_OF_FDB                             (2048)

#define FW_LINK_SPEED_1G                           (0x00)
#define FW_LINK_SPEED_100M                         (0x01)
#define FW_LINK_SPEED_10M                          (0x02)
#define FW_LINK_SPEED_HD                           (0x80)

/* Time after which FDB entries are checked for aged out values.
 * Values are in nanoseconds
 */
#define FDB_AGEING_TIMEOUT_OFFSET                          0x0014

/* Default VLAN tag for Host Port */
#define HOST_PORT_DF_VLAN_OFFSET                           0x001C

/* Same as HOST_PORT_DF_VLAN_OFFSET */
#define EMAC_ICSSG_SWITCH_PORT0_DEFAULT_VLAN_OFFSET        HOST_PORT_DF_VLAN_OFFSET

/* Default VLAN tag for P1 Port */
#define P1_PORT_DF_VLAN_OFFSET                             0x0020

/* Same as P1_PORT_DF_VLAN_OFFSET */
#define EMAC_ICSSG_SWITCH_PORT1_DEFAULT_VLAN_OFFSET        P1_PORT_DF_VLAN_OFFSET

/* default VLAN tag for P2 Port */
#define P2_PORT_DF_VLAN_OFFSET                             0x0024

/* Same as P2_PORT_DF_VLAN_OFFSET */
#define EMAC_ICSSG_SWITCH_PORT2_DEFAULT_VLAN_OFFSET        P2_PORT_DF_VLAN_OFFSET

/* VLAN-FID Table offset. 4096 VIDs. 2B per VID = 8KB = 0x2000 */
#define VLAN_STATIC_REG_TABLE_OFFSET                       0x0100

/* VLAN-FID Table offset for EMAC  */
#define EMAC_ICSSG_SWITCH_DEFAULT_VLAN_TABLE_OFFSET        VLAN_STATIC_REG_TABLE_OFFSET

/* Packet descriptor Q reserved memory */
#define PORT_DESC0_HI                                      0x2104

/* Packet descriptor Q reserved memory */
#define PORT_DESC0_LO                                      0x2F6C

/* Packet descriptor Q reserved memory */
#define PORT_DESC1_HI                                      0x3DD4

/* Packet descriptor Q reserved memory */
#define PORT_DESC1_LO                                      0x4C3C

/* Packet descriptor Q reserved memory */
#define HOST_DESC0_HI                                      0x5AA4

/* Packet descriptor Q reserved memory */
#define HOST_DESC0_LO                                      0x5F0C

/* Packet descriptor Q reserved memory */
#define HOST_DESC1_HI                                      0x6374

/* Packet descriptor Q reserved memory */
#define HOST_DESC1_LO                                      0x67DC

/* Special packet descriptor Q reserved memory */
#define HOST_SPPD0                                         0x7AAC

/* Special acket descriptor Q reserved memory */
#define HOST_SPPD1                                         0x7EAC

/* IEP count cycle counter*/
#define TIMESYNC_FW_WC_CYCLECOUNT_OFFSET                   0x83EC

/* IEP count hi roll over count */
#define TIMESYNC_FW_WC_HI_ROLLOVER_COUNT_OFFSET            0x83F4

/* IEP count hi sw counter */
#define TIMESYNC_FW_WC_COUNT_HI_SW_OFFSET_OFFSET           0x83F8

/* Set clock descriptor */
#define TIMESYNC_FW_WC_SETCLOCK_DESC_OFFSET                0x83FC

/* IEP count syncout reduction factor */
#define TIMESYNC_FW_WC_SYNCOUT_REDUCTION_FACTOR_OFFSET     0x843C

/* IEP count syncout reduction counter */
#define TIMESYNC_FW_WC_SYNCOUT_REDUCTION_COUNT_OFFSET      0x8440

/* IEP count syncout start time cycle counter */
#define TIMESYNC_FW_WC_SYNCOUT_START_TIME_CYCLECOUNT_OFFSET 0x8444

/* Control variable to generate SYNC1 */
#define TIMESYNC_FW_WC_ISOM_PIN_SIGNAL_EN_OFFSET           0x844C

/* SystemTime Sync0 periodicity */
#define TIMESYNC_FW_ST_SYNCOUT_PERIOD_OFFSET               0x8450

/* pktTxDelay for P1 = link speed dependent p1 mac delay + p1 phy delay */
#define TIMESYNC_FW_WC_PKTTXDELAY_P1_OFFSET                0x8454

/* pktTxDelay for P2 = link speed dependent p2 mac delay + p2 phy delay */
#define TIMESYNC_FW_WC_PKTTXDELAY_P2_OFFSET                0x8458

/* Set clock operation done signal for next task */
#define TIMESYNC_FW_SIG_PNFW_OFFSET                        0x845C

/* Set clock operation done signal for next task */
#define TIMESYNC_FW_SIG_TIMESYNCFW_OFFSET                  0x8460

/* New list is copied at this time */
#define TAS_CONFIG_CHANGE_TIME                             0x000C

/* config change error counter */
#define TAS_CONFIG_CHANGE_ERROR_COUNTER                    0x0014

/* TAS List update pending flag */
#define TAS_CONFIG_PENDING                                 0x0018

/* TAS list update trigger flag */
#define TAS_CONFIG_CHANGE                                  0x0019

/* List length for new TAS schedule */
#define TAS_ADMIN_LIST_LENGTH                              0x001A

/* Currently active TAS list index */
#define TAS_ACTIVE_LIST_INDEX                              0x001B

/* Cycle time for the new TAS schedule */
#define TAS_ADMIN_CYCLE_TIME                               0x001C

/* Cycle counts remaining till the TAS list update */
#define TAS_CONFIG_CHANGE_CYCLE_COUNT                      0x0020

/* Base Flow ID for sending  Packets to Host for Slice0 */
#define PSI_L_REGULAR_FLOW_ID_BASE_OFFSET                  0x0024

/* Same as PSI_L_REGULAR_FLOW_ID_BASE_OFFSET */
#define EMAC_ICSSG_SWITCH_PSI_L_REGULAR_FLOW_ID_BASE_OFFSET PSI_L_REGULAR_FLOW_ID_BASE_OFFSET

/* Base Flow ID for sending mgmt and Tx TS to Host for Slice0 */
#define PSI_L_MGMT_FLOW_ID_OFFSET                          0x0026

/* Same as PSI_L_MGMT_FLOW_ID_OFFSET */
#define EMAC_ICSSG_SWITCH_PSI_L_MGMT_FLOW_ID_BASE_OFFSET   PSI_L_MGMT_FLOW_ID_OFFSET

/* Queue number for Special  Packets written here */
#define SPL_PKT_DEFAULT_PRIORITY                           0x0028

/* Express Preemptible Queue Mask */
#define EXPRESS_PRE_EMPTIVE_Q_MASK                         0x0029

/* Port1/Port2 Default Queue number for untagged  Packets, only 1B is used */
#define QUEUE_NUM_UNTAGGED                                 0x002A

/* Stores the table used for priority regeneration. 1B per PCP/Queue */
#define PORT_Q_PRIORITY_REGEN_OFFSET                       0x002C

/* For marking Packet as priority/express (this feature is disabled) or
 * cut-through/S&F.
 */
#define EXPRESS_PRE_EMPTIVE_Q_MAP                          0x0034

/* Stores the table used for priority mapping. 1B per PCP/Queue */
#define PORT_Q_PRIORITY_MAPPING_OFFSET                     0x003C

/* Used to notify the FW of the current link speed */
#define PORT_LINK_SPEED_OFFSET                             0x00A8

/* TAS gate mask for windows list0 */
#define TAS_GATE_MASK_LIST0                                0x0100

/* TAS gate mask for windows list1 */
#define TAS_GATE_MASK_LIST1                                0x0350

/* Memory to Enable/Disable Preemption on TX side */
#define PRE_EMPTION_ENABLE_TX                              0x05A0

/* Active State of Preemption on TX side */
#define PRE_EMPTION_ACTIVE_TX                              0x05A1

/* Memory to Enable/Disable Verify State Machine Preemption */
#define PRE_EMPTION_ENABLE_VERIFY                          0x05A2

/* Verify Status of State Machine */
#define PRE_EMPTION_VERIFY_STATUS                          0x05A3

/* Non Final Fragment Size supported by Link Partner */
#define PRE_EMPTION_ADD_FRAG_SIZE_REMOTE                   0x05A4

/* Non Final Fragment Size supported by Firmware */
#define PRE_EMPTION_ADD_FRAG_SIZE_LOCAL                    0x05A6

/* Time in ms the State machine waits for respond Packet */
#define PRE_EMPTION_VERIFY_TIME                            0x05A8

/* Memory used for R30 related management commands */
#define MGR_R30_CMD_OFFSET                                 0x05AC

/* HW Buffer Pool0 base address */
#define BUFFER_POOL_0_ADDR_OFFSET                          0x05BC

/* 16B for Host Egress MSMC Q (Pre-emptible) context */
#define HOST_RX_Q_PRE_CONTEXT_OFFSET                       0x0684

/* Buffer for 8 FDB entries to be added by 'Add Multiple FDB entries IOCTL' */
#define FDB_CMD_BUFFER                                     0x0894

/* TAS queue max sdu length list */
#define TAS_QUEUE_MAX_SDU_LIST                             0x08FA

/* Used by FW to generate random number with the SEED value */
#define HD_RAND_SEED_OFFSET                                0x0934

/* 16B for Host Egress MSMC Q (Express) context */
#define HOST_RX_Q_EXP_CONTEXT_OFFSET                       0x0940

/* Start of 32 bits PA_STAT counters */
#define PA_STAT_32b_START_OFFSET                           0x0080

#endif /* __NET_TI_ICSSG_SWITCH_MAP_H  */
