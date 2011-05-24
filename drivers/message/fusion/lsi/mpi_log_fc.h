/*
 *  Copyright (c) 2000-2008 LSI Corporation. All rights reserved.
 *
 *  NAME:           fc_log.h
 *  SUMMARY:        MPI IocLogInfo definitions for the SYMFC9xx chips
 *  DESCRIPTION:    Contains the enumerated list of values that may be returned
 *                  in the IOCLogInfo field of a MPI Default Reply Message.
 *
 *  CREATION DATE:  6/02/2000
 *  ID:             $Id: fc_log.h,v 4.6 2001/07/26 14:41:33 sschremm Exp $
 */


/*
 * MpiIocLogInfo_t enum
 *
 * These 32 bit values are used in the IOCLogInfo field of the MPI reply
 * messages.
 * The value is 0xabcccccc where
 *          a = The type of log info as per the MPI spec. Since these codes are
 *              all for Fibre Channel this value will always be 2.
 *          b = Specifies a subclass of the firmware where
 *                  0 = FCP Initiator
 *                  1 = FCP Target
 *                  2 = LAN
 *                  3 = MPI Message Layer
 *                  4 = FC Link
 *                  5 = Context Manager
 *                  6 = Invalid Field Offset
 *                  7 = State Change Info
 *                  all others are reserved for future use
 *          c = A specific value within the subclass.
 *
 * NOTE: Any new values should be added to the end of each subclass so that the
 *       codes remain consistent across firmware releases.
 */
typedef enum _MpiIocLogInfoFc
{
    MPI_IOCLOGINFO_FC_INIT_BASE                     = 0x20000000,
    MPI_IOCLOGINFO_FC_INIT_ERROR_OUT_OF_ORDER_FRAME = 0x20000001, /* received an out of order frame - unsupported */
    MPI_IOCLOGINFO_FC_INIT_ERROR_BAD_START_OF_FRAME = 0x20000002, /* Bad Rx Frame, bad start of frame primitive */
    MPI_IOCLOGINFO_FC_INIT_ERROR_BAD_END_OF_FRAME   = 0x20000003, /* Bad Rx Frame, bad end of frame primitive */
    MPI_IOCLOGINFO_FC_INIT_ERROR_OVER_RUN           = 0x20000004, /* Bad Rx Frame, overrun */
    MPI_IOCLOGINFO_FC_INIT_ERROR_RX_OTHER           = 0x20000005, /* Other errors caught by IOC which require retries */
    MPI_IOCLOGINFO_FC_INIT_ERROR_SUBPROC_DEAD       = 0x20000006, /* Main processor could not initialize sub-processor */
    MPI_IOCLOGINFO_FC_INIT_ERROR_RX_OVERRUN         = 0x20000007, /* Scatter Gather overrun  */
    MPI_IOCLOGINFO_FC_INIT_ERROR_RX_BAD_STATUS      = 0x20000008, /* Receiver detected context mismatch via invalid header */
    MPI_IOCLOGINFO_FC_INIT_ERROR_RX_UNEXPECTED_FRAME= 0x20000009, /* CtxMgr detected unsupported frame type  */
    MPI_IOCLOGINFO_FC_INIT_ERROR_LINK_FAILURE       = 0x2000000A, /* Link failure occurred  */
    MPI_IOCLOGINFO_FC_INIT_ERROR_TX_TIMEOUT         = 0x2000000B, /* Transmitter timeout error */

    MPI_IOCLOGINFO_FC_TARGET_BASE                   = 0x21000000,
    MPI_IOCLOGINFO_FC_TARGET_NO_PDISC               = 0x21000001, /* not sent because we are waiting for a PDISC from the initiator */
    MPI_IOCLOGINFO_FC_TARGET_NO_LOGIN               = 0x21000002, /* not sent because we are not logged in to the remote node */
    MPI_IOCLOGINFO_FC_TARGET_DOAR_KILLED_BY_LIP     = 0x21000003, /* Data Out, Auto Response, not sent due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_DIAR_KILLED_BY_LIP     = 0x21000004, /* Data In, Auto Response, not sent due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_DIAR_MISSING_DATA      = 0x21000005, /* Data In, Auto Response, missing data frames */
    MPI_IOCLOGINFO_FC_TARGET_DONR_KILLED_BY_LIP     = 0x21000006, /* Data Out, No Response, not sent due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_WRSP_KILLED_BY_LIP     = 0x21000007, /* Auto-response after a write not sent due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_DINR_KILLED_BY_LIP     = 0x21000008, /* Data In, No Response, not completed due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_DINR_MISSING_DATA      = 0x21000009, /* Data In, No Response, missing data frames */
    MPI_IOCLOGINFO_FC_TARGET_MRSP_KILLED_BY_LIP     = 0x2100000a, /* Manual Response not sent due to a LIP */
    MPI_IOCLOGINFO_FC_TARGET_NO_CLASS_3             = 0x2100000b, /* not sent because remote node does not support Class 3 */
    MPI_IOCLOGINFO_FC_TARGET_LOGIN_NOT_VALID        = 0x2100000c, /* not sent because login to remote node not validated */
    MPI_IOCLOGINFO_FC_TARGET_FROM_OUTBOUND          = 0x2100000e, /* cleared from the outbound queue after a logout */
    MPI_IOCLOGINFO_FC_TARGET_WAITING_FOR_DATA_IN    = 0x2100000f, /* cleared waiting for data after a logout */

    MPI_IOCLOGINFO_FC_LAN_BASE                      = 0x22000000,
    MPI_IOCLOGINFO_FC_LAN_TRANS_SGL_MISSING         = 0x22000001, /* Transaction Context Sgl Missing */
    MPI_IOCLOGINFO_FC_LAN_TRANS_WRONG_PLACE         = 0x22000002, /* Transaction Context found before an EOB */
    MPI_IOCLOGINFO_FC_LAN_TRANS_RES_BITS_SET        = 0x22000003, /* Transaction Context value has reserved bits set */
    MPI_IOCLOGINFO_FC_LAN_WRONG_SGL_FLAG            = 0x22000004, /* Invalid SGL Flags */

    MPI_IOCLOGINFO_FC_MSG_BASE                      = 0x23000000,

    MPI_IOCLOGINFO_FC_LINK_BASE                     = 0x24000000,
    MPI_IOCLOGINFO_FC_LINK_LOOP_INIT_TIMEOUT        = 0x24000001, /* Loop initialization timed out */
    MPI_IOCLOGINFO_FC_LINK_ALREADY_INITIALIZED      = 0x24000002, /* Another system controller already initialized the loop */
    MPI_IOCLOGINFO_FC_LINK_LINK_NOT_ESTABLISHED     = 0x24000003, /* Not synchronized to signal or still negotiating (possible cable problem) */
    MPI_IOCLOGINFO_FC_LINK_CRC_ERROR                = 0x24000004, /* CRC check detected error on received frame */

    MPI_IOCLOGINFO_FC_CTX_BASE                      = 0x25000000,

    MPI_IOCLOGINFO_FC_INVALID_FIELD_BYTE_OFFSET     = 0x26000000, /* The lower 24 bits give the byte offset of the field in the request message that is invalid */
    MPI_IOCLOGINFO_FC_INVALID_FIELD_MAX_OFFSET      = 0x26ffffff,

    MPI_IOCLOGINFO_FC_STATE_CHANGE                  = 0x27000000  /* The lower 24 bits give additional information concerning state change */

} MpiIocLogInfoFc_t;
