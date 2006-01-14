
/***************************************************************************
 *                                                                         *
 *  Copyright 2003 LSI Logic Corporation.  All rights reserved.            *
 *                                                                         *
 *  This file is confidential and a trade secret of LSI Logic.  The        *
 *  receipt of or possession of this file does not convey any rights to    *
 *  reproduce or disclose its contents or to manufacture, use, or sell     *
 *  anything it may describe, in whole, or in part, without the specific   *
 *  written consent of LSI Logic Corporation.                              *
 *                                                                         *
 ***************************************************************************
 *
 *           Name:  iopiIocLogInfo.h
 *          Title:  SAS Firmware IOP Interface IOC Log Info Definitions
 *     Programmer:  Guy Kendall
 *  Creation Date:  September 24, 2003
 *
 *  Version History
 *  ---------------
 *
 *  Last Updated
 *  -------------
 *  Version         %version: 22 %
 *  Date Updated    %date_modified: %
 *  Programmer      %created_by: nperucca %
 *
 *  Date      Who   Description
 *  --------  ---   -------------------------------------------------------
 *  09/24/03  GWK   Initial version
 *
 *
 * Description
 * ------------
 * This include file contains SAS firmware interface IOC Log Info codes
 *
 *-------------------------------------------------------------------------
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


/****************************************************************************/
/* PL LOGINFO_CODE defines, valid if IOC_LOGINFO_ORIGINATOR = PL            */
/****************************************************************************/
#define PL_LOGINFO_CODE_OPEN_FAILURE                        (0x00010000)
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
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_PT              (0x000F0100) /* Invalid Page Type */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NUM_PHYS        (0x000F0200) /* Invalid Number of Phys */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NOT_IMP         (0x000F0300) /* Case Not Handled */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NO_DEV          (0x000F0400) /* No Device Found */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_FORM            (0x000F0500) /* Invalid FORM */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_PHY             (0x000F0600) /* Invalid Phy */
#define PL_LOGINFO_CODE_CONFIG_INVALID_PAGE_NO_OWNER        (0x000F0700) /* No Owner Found */
#define PL_LOGINFO_CODE_DSCVRY_SATA_INIT_TIMEOUT            (0x00100000)
#define PL_LOGINFO_CODE_RESET                               (0x00110000)
#define PL_LOGINFO_CODE_ABORT                               (0x00120000)
#define PL_LOGINFO_CODE_IO_NOT_YET_EXECUTED                 (0x00130000)
#define PL_LOGINFO_CODE_IO_EXECUTED                         (0x00140000)
#define PL_LOGINFO_SUB_CODE_OPEN_FAILURE                    (0x00000100)
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
#define PL_LOGINFO_SUB_CODE_DSCVRY_SATA_INIT_TIMEOUT        (0x00001000)


#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_FRAME_FAILURE         (0x00200000) /* Can't get SMP Frame */
#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_READ_ERROR            (0x00200001) /* Error occured on SMP Read */
#define PL_LOGINFO_CODE_ENCL_MGMT_SMP_WRITE_ERROR           (0x00200002) /* Error occured on SMP Write */
#define PL_LOGINFO_CODE_ENCL_MGMT_NOT_SUPPORTED_ON_ENCL     (0x00200004) /* Encl Mgmt services not available for this WWID */
#define PL_LOGINFO_CODE_ENCL_MGMT_ADDR_MODE_NOT_SUPPORTED   (0x00200005) /* Address Mode not suppored */
#define PL_LOGINFO_CODE_ENCL_MGMT_BAD_SLOT_NUM              (0x00200006) /* Invalid Slot Number in SEP Msg */
#define PL_LOGINFO_CODE_ENCL_MGMT_SGPIO_NOT_PRESENT         (0x00200007) /* SGPIO not present/enabled */

#define PL_LOGINFO_DA_SEP_NOT_PRESENT                       (0x00200100) /* SEP not present when msg received */
#define PL_LOGINFO_DA_SEP_SINGLE_THREAD_ERROR               (0x00200101) /* Can only accept 1 msg at a time */
#define PL_LOGINFO_DA_SEP_ISTWI_INTR_IN_IDLE_STATE          (0x00200102) /* ISTWI interrupt recvd. while IDLE */
#define PL_LOGINFO_DA_SEP_RECEIVED_NACK_FROM_SLAVE          (0x00200103) /* SEP NACK'd, it is busy */
#define PL_LOGINFO_DA_SEP_BAD_STATUS_HDR_CHKSUM             (0x00200104) /* SEP stopped or sent bad chksum in Hdr */
#define PL_LOGINFO_DA_SEP_UNSUPPORTED_SCSI_STATUS_1         (0x00200105) /* SEP returned unknown scsi status */
#define PL_LOGINFO_DA_SEP_UNSUPPORTED_SCSI_STATUS_2         (0x00200106) /* SEP returned unknown scsi status */
#define PL_LOGINFO_DA_SEP_CHKSUM_ERROR_AFTER_STOP           (0x00200107) /* SEP returned bad chksum after STOP */
#define PL_LOGINFO_DA_SEP_CHKSUM_ERROR_AFTER_STOP_GETDATA   (0x00200108) /* SEP returned bad chksum after STOP while gettin data*/


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

