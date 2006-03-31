
/***************************************************************************
 *                                                                         *
 *  Copyright 2003 LSI Logic Corporation.  All rights reserved.            *
 *                                                                         *
 * Description                                                             *
 * ------------                                                            *
 * This include file contains SAS firmware interface IOC Log Info codes    *
 *                                                                         *
 *-------------------------------------------------------------------------*
 */

#ifndef IOPI_IOCLOGINFO_H_INCLUDED
#define IOPI_IOCLOGINFO_H_INCLUDED


/****************************************************************************/
/*  IOC LOGINFO defines, 0x00000000 - 0x0FFFFFFF                            */
/*  Format:                                                                 */
/*      Bits 31-28: MPI_IOCLOGINFO_TYPE_SAS (3)                             */
/*      Bits 27-24: IOC_LOGINFO_ORIGINATOR: 0=IOP, 1=PL, 2=IR               */
/*      Bits 23-16: LOGINFO_CODE                                            */
/*      Bits 15-0:  LOGINFO_CODE Specific                                   */
/****************************************************************************/

/****************************************************************************/
/* IOC_LOGINFO_ORIGINATOR defines                                           */
/****************************************************************************/
#define IOC_LOGINFO_ORIGINATOR_IOP                      (0x00000000)
#define IOC_LOGINFO_ORIGINATOR_PL                       (0x01000000)
#define IOC_LOGINFO_ORIGINATOR_IR                       (0x02000000)

#define IOC_LOGINFO_ORIGINATOR_MASK                     (0x0F000000)

/****************************************************************************/
/* LOGINFO_CODE defines                                                     */
/****************************************************************************/
#define IOC_LOGINFO_CODE_MASK                           (0x00FF0000)
#define IOC_LOGINFO_CODE_SHIFT                          (16)

/****************************************************************************/
/* IOP LOGINFO_CODE defines, valid if IOC_LOGINFO_ORIGINATOR = IOP          */
/****************************************************************************/
#define IOP_LOGINFO_CODE_INVALID_SAS_ADDRESS            (0x00010000)
#define IOP_LOGINFO_CODE_UNUSED2                        (0x00020000)
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE            (0x00030000)
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_RT         (0x00030100) /* Route Table Entry not found */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_PN         (0x00030200) /* Invalid Page Number */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_FORM       (0x00030300) /* Invalid FORM */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_PT         (0x00030400) /* Invalid Page Type */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_DNM        (0x00030500) /* Device Not Mapped */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_PERSIST    (0x00030600) /* Persistent Page not found */
#define IOP_LOGINFO_CODE_CONFIG_INVALID_PAGE_DEFAULT    (0x00030700) /* Default Page not found */
#define IOP_LOGINFO_CODE_TASK_TERMINATED                (0x00050000)

#define IOP_LOGINFO_CODE_ENCL_MGMT_READ_ACTION_ERR0R    (0x00060001) /* Read Action not supported for SEP msg */
#define IOP_LOGINFO_CODE_ENCL_MGMT_INVALID_BUS_ID_ERR0R (0x00060002) /* Invalid Bus/ID in SEP msg */

#define IOP_LOGINFO_CODE_TARGET_ASSIST_TERMINATED       (0x00070001)
#define IOP_LOGINFO_CODE_TARGET_STATUS_SEND_TERMINATED  (0x00070002)
#define IOP_LOGINFO_CODE_TARGET_MODE_ABORT_ALL_IO       (0x00070003)
#define IOP_LOGINFO_CODE_TARGET_MODE_ABORT_EXACT_IO     (0x00070004)
#define IOP_LOGINFO_CODE_TARGET_MODE_ABORT_EXACT_IO_REQ (0x00070005)

/****************************************************************************/
/* PL LOGINFO_CODE defines, valid if IOC_LOGINFO_ORIGINATOR = PL            */
/****************************************************************************/
#define PL_LOGINFO_CODE_OPEN_FAILURE                        (0x00010000)
#define PL_LOG_INFO_CODE_OPEN_FAILURE_NO_DEST_TIME_OUT      (0x00010001)
#define PL_LOGINFO_CODE_OPEN_FAILURE_BAD_DESTINATION        (0x00010011)
#define PL_LOGINFO_CODE_OPEN_FAILURE_PROTOCOL_NOT_SUPPORTED (0x00010013)
#define PL_LOGINFO_CODE_OPEN_FAILURE_STP_RESOURCES_BSY      (0x00010018)
#define PL_LOGINFO_CODE_OPEN_FAILURE_WRONG_DESTINATION      (0x00010019)
#define PL_LOGINFO_CODE_OPEN_FAILURE_ORR_TIMEOUT            (0X0001001A)
#define PL_LOGINFO_CODE_OPEN_FAILURE_PATHWAY_BLOCKED        (0x0001001B)
#define PL_LOGINFO_CODE_OPEN_FAILURE_AWT_MAXED              (0x0001001C)
#define PL_LOGINFO_CODE_INVALID_SGL                         (0x00020000)
#define PL_LOGINFO_CODE_WRONG_REL_OFF_OR_FRAME_LENGTH       (0x00030000)
#define PL_LOGINFO_CODE_FRAME_XFER_ERROR                    (0x00040000)
#define PL_LOGINFO_CODE_TX_FM_CONNECTED_LOW                 (0x00050000)
#define PL_LOGINFO_CODE_SATA_NON_NCQ_RW_ERR_BIT_SET         (0x00060000)
#define PL_LOGINFO_CODE_SATA_READ_LOG_RECEIVE_DATA_ERR      (0x00070000)
#define PL_LOGINFO_CODE_SATA_NCQ_FAIL_ALL_CMDS_AFTR_ERR     (0x00080000)
#define PL_LOGINFO_CODE_SATA_ERR_IN_RCV_SET_DEV_BIT_FIS     (0x00090000)
#define PL_LOGINFO_CODE_RX_FM_INVALID_MESSAGE               (0x000A0000)
#define PL_LOGINFO_CODE_RX_CTX_MESSAGE_VALID_ERROR          (0x000B0000)
#define PL_LOGINFO_CODE_RX_FM_CURRENT_FRAME_ERROR           (0x000C0000)
#define PL_LOGINFO_CODE_SATA_LINK_DOWN                      (0x000D0000)
#define PL_LOGINFO_CODE_DISCOVERY_SATA_INIT_W_IOS           (0x000E0000)
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE                 (0x000F0000)
#define PL_LOGINFO_CODE_CONFIG_PL_NOT_INITIALIZED           (0x000F0001) /* PL not yet initialized, can't do config page req. */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_PT              (0x000F0100) /* Invalid Page Type */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NUM_PHYS        (0x000F0200) /* Invalid Number of Phys */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NOT_IMP         (0x000F0300) /* Case Not Handled */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NO_DEV          (0x000F0400) /* No Device Found */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_FORM            (0x000F0500) /* Invalid FORM */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_PHY             (0x000F0600) /* Invalid Phy */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NO_OWNER        (0x000F0700) /* No Owner Found */
#define PL_LOGINFO_CODE_DSCVRY_SATA_INIT_TIMEOUT            (0x00100000)
#define PL_LOGINFO_CODE_RESET                               (0x00110000) /* See Sub-Codes below */
#define PL_LOGINFO_CODE_ABORT                               (0x00120000) /* See Sub-Codes below */
#define PL_LOGINFO_CODE_IO_NOT_YET_EXECUTED                 (0x00130000)
#define PL_LOGINFO_CODE_IO_EXECUTED                         (0x00140000)
#define PL_LOGINFO_CODE_PERS_RESV_OUT_NOT_AFFIL_OWNER       (0x00150000)
#define PL_LOGINFO_CODE_OPEN_TXDMA_ABORT                    (0x00160000)
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE                    (0x00000100)
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE_NO_DEST_TIMEOUT    (0x00000101)
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE_ORR_TIMEOUT        (0x0000011A) /* Open Reject (Retry) Timeout */
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE_PATHWAY_BLOCKED    (0x0000011B)
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE_AWT_MAXED          (0x0000011C) /* Arbitration Wait Timer Maxed */

#define PL_LOGINFO_SUB_CODE_TARGET_BUS_RESET                (0x00000120)
#define PL_LOGINFO_SUB_CODE_TRANSPORT_LAYER                 (0x00000130)  /* Leave lower nibble (1-f) reserved. */
#define PL_LOGINFO_SUB_CODE_PORT_LAYER                      (0x00000140)  /* Leave lower nibble (1-f) reserved. */


#define PL_LOGINFO_SUB_CODE_INVALID_SGL                     (0x00000200)
#define PL_LOGINFO_SUB_CODE_WRONG_REL_OFF_OR_FRAME_LENGTH   (0x00000300)
#define PL_LOGINFO_SUB_CODE_FRAME_XFER_ERROR                (0x00000400)
#define PL_LOGINFO_SUB_CODE_TX_FM_CONNECTED_LOW             (0x00000500)
#define PL_LOGINFO_SUB_CODE_SATA_NON_NCQ_RW_ERR_BIT_SET     (0x00000600)
#define PL_LOGINFO_SUB_CODE_SATA_READ_LOG_RECEIVE_DATA_ERR  (0x00000700)
#define PL_LOGINFO_SUB_CODE_SATA_NCQ_FAIL_ALL_CMDS_AFTR_ERR (0x00000800)
#define PL_LOGINFO_SUB_CODE_SATA_ERR_IN_RCV_SET_DEV_BIT_FIS (0x00000900)
#define PL_LOGINFO_SUB_CODE_RX_FM_INVALID_MESSAGE           (0x00000A00)
#define PL_LOGINFO_SUB_CODE_RX_CTX_MESSAGE_VALID_ERROR      (0x00000B00)
#define PL_LOGINFO_SUB_CODE_RX_FM_CURRENT_FRAME_ERROR       (0x00000C00)
#define PL_LOGINFO_SUB_CODE_SATA_LINK_DOWN                  (0x00000D00)
#define PL_LOGINFO_SUB_CODE_DISCOVERY_SATA_INIT_W_IOS       (0x00000E00)
#define PL_LOGINFO_SUB_CODE_DISCOVERY_REMOTE_SEP_RESET      (0x00000E01)
#define PL_LOGINFO_SUB_CODE_SECOND_OPEN                     (0x00000F00)
#define PL_LOGINFO_SUB_CODE_DSCVRY_SATA_INIT_TIMEOUT        (0x00001000)


#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_FRAME_FAILURE         (0x00200000) /* Can't get SMP Frame */
#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_READ_ERROR            (0x00200010) /* Error occured on SMP Read */
#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_WRITE_ERROR           (0x00200020) /* Error occured on SMP Write */
#define PL_LOGINFO_CODE_ENCL_MGMT_NOT_SUPPORTED_ON_ENCL     (0x00200040) /* Encl Mgmt services not available for this WWID */
#define PL_LOGINFO_CODE_ENCL_MGMT_ADDR_MODE_NOT_SUPPORTED   (0x00200050) /* Address Mode not suppored */
#define PL_LOGINFO_CODE_ENCL_MGMT_BAD_SLOT_NUM              (0x00200060) /* Invalid Slot Number in SEP Msg */
#define PL_LOGINFO_CODE_ENCL_MGMT_SGPIO_NOT_PRESENT         (0x00200070) /* SGPIO not present/enabled */
#define PL_LOGINFO_CODE_ENCL_MGMT_GPIO_NOT_CONFIGURED       (0x00200080) /* GPIO not configured */
#define PL_LOGINFO_CODE_ENCL_MGMT_GPIO_FRAME_ERROR          (0x00200090) /* GPIO can't allocate a frame */
#define PL_LOGINFO_CODE_ENCL_MGMT_GPIO_CONFIG_PAGE_ERROR    (0x002000A0) /* GPIO failed config page request */
#define PL_LOGINFO_CODE_ENCL_MGMT_SES_FRAME_ALLOC_ERROR     (0x002000B0) /* Can't get frame for SES command */
#define PL_LOGINFO_CODE_ENCL_MGMT_SES_IO_ERROR              (0x002000C0) /* I/O execution error */
#define PL_LOGINFO_CODE_ENCL_MGMT_SES_RETRIES_EXHAUSTED     (0x002000D0) /* SEP I/O retries exhausted */
#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_FRAME_ALLOC_ERROR     (0x002000E0) /* Can't get frame for SMP command */

#define PL_LOGINFO_DA_SEP_NOT_PRESENT                       (0x00200100) /* SEP not present when msg received */
#define PL_LOGINFO_DA_SEP_SINGLE_THREAD_ERROR               (0x00200101) /* Can only accept 1 msg at a time */
#define PL_LOGINFO_DA_SEP_ISTWI_INTR_IN_IDLE_STATE          (0x00200102) /* ISTWI interrupt recvd. while IDLE */
#define PL_LOGINFO_DA_SEP_RECEIVED_NACK_FROM_SLAVE          (0x00200103) /* SEP NACK'd, it is busy */
#define PL_LOGINFO_DA_SEP_DID_NOT_RECEIVE_ACK               (0x00200104) /* SEP didn't rcv. ACK (Last Rcvd Bit = 1) */
#define PL_LOGINFO_DA_SEP_BAD_STATUS_HDR_CHKSUM             (0x00200105) /* SEP stopped or sent bad chksum in Hdr */
#define PL_LOGINFO_DA_SEP_STOP_ON_DATA                      (0x00200106) /* SEP stopped while transfering data */
#define PL_LOGINFO_DA_SEP_STOP_ON_SENSE_DATA                (0x00200107) /* SEP stopped while transfering sense data */
#define PL_LOGINFO_DA_SEP_UNSUPPORTED_SCSI_STATUS_1         (0x00200108) /* SEP returned unknown scsi status */
#define PL_LOGINFO_DA_SEP_UNSUPPORTED_SCSI_STATUS_2         (0x00200109) /* SEP returned unknown scsi status */
#define PL_LOGINFO_DA_SEP_CHKSUM_ERROR_AFTER_STOP           (0x0020010A) /* SEP returned bad chksum after STOP */
#define PL_LOGINFO_DA_SEP_CHKSUM_ERROR_AFTER_STOP_GETDATA   (0x0020010B) /* SEP returned bad chksum after STOP while gettin data*/
#define PL_LOGINFO_DA_SEP_UNSUPPORTED_COMMAND               (0x0020010C) /* SEP doesn't support CDB opcode */


/****************************************************************************/
/* IR LOGINFO_CODE defines, valid if IOC_LOGINFO_ORIGINATOR = IR            */
/****************************************************************************/
#define IR_LOGINFO_CODE_UNUSED1                         (0x00010000)
#define IR_LOGINFO_CODE_UNUSED2                         (0x00020000)

/****************************************************************************/
/* Defines for convienence                                                  */
/****************************************************************************/
#define IOC_LOGINFO_PREFIX_IOP                          ((MPI_IOCLOGINFO_TYPE_SAS << MPI_IOCLOGINFO_TYPE_SHIFT) | IOC_LOGINFO_ORIGINATOR_IOP)
#define IOC_LOGINFO_PREFIX_PL                           ((MPI_IOCLOGINFO_TYPE_SAS << MPI_IOCLOGINFO_TYPE_SHIFT) | IOC_LOGINFO_ORIGINATOR_PL)
#define IOC_LOGINFO_PREFIX_IR                           ((MPI_IOCLOGINFO_TYPE_SAS << MPI_IOCLOGINFO_TYPE_SHIFT) | IOC_LOGINFO_ORIGINATOR_IR)

#endif /* end of file */

