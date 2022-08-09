/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __LAN966X_IFH_H__
#define __LAN966X_IFH_H__

/* Fields with description (*) should just be cleared upon injection
 * IFH is transmitted MSByte first (Highest bit pos sent as MSB of first byte)
 */

#define IFH_LEN                      7

/* Timestamp for frame */
#define IFH_POS_TIMESTAMP            192

/* Bypass analyzer with a prefilled IFH */
#define IFH_POS_BYPASS               191

/* Masqueraded injection with masq_port defining logical source port */
#define IFH_POS_MASQ                 190

/* Masqueraded port number for injection */
#define IFH_POS_MASQ_PORT            186

/* Frame length (*) */
#define IFH_POS_LEN                  178

/* Cell filling mode. Full(0),Etype(1), LlctOpt(2), Llct(3) */
#define IFH_POS_WRDMODE              176

/* Frame has 16 bits rtag removed compared to line data */
#define IFH_POS_RTAG48               175

/* Frame has a redundancy tag */
#define IFH_POS_HAS_RED_TAG          174

/* Frame has been cut through forwarded (*) */
#define IFH_POS_CUTTHRU              173

/* Rewriter command */
#define IFH_POS_REW_CMD              163

/* Enable OAM-related rewriting. PDU_TYPE encodes OAM type. */
#define IFH_POS_REW_OAM              162

/* PDU type. Encoding: (0-NONE, 1-Y1731_CCM, 2-MRP_TST, 3-MRP_ITST, 4-DLR_BCN,
 * 5-DLR_ADV, 6-RTE_NULL_INJ, 7-IPV4, 8-IPV6, 9-Y1731_NON_CCM).
 */
#define IFH_POS_PDU_TYPE             158

/* Update FCS before transmission */
#define IFH_POS_FCS_UPD              157

/* Classified DSCP value of frame */
#define IFH_POS_DSCP                 151

/* Yellow indication */
#define IFH_POS_DP                   150

/* Process in RTE/inbound */
#define IFH_POS_RTE_INB_UPDATE       149

/* Number of tags to pop from frame */
#define IFH_POS_POP_CNT              147

/* Number of tags in front of the ethertype */
#define IFH_POS_ETYPE_OFS            145

/* Logical source port of frame (*) */
#define IFH_POS_SRCPORT              141

/* Sequence number in redundancy tag */
#define IFH_POS_SEQ_NUM              120

/* Stagd flag and classified TCI of frame (PCP/DEI/VID) */
#define IFH_POS_TCI                  103

/* Classified internal priority for queuing */
#define IFH_POS_QOS_CLASS            100

/* Bit mask with eight cpu copy classses */
#define IFH_POS_CPUQ                 92

/* Relearn + learn flags (*) */
#define IFH_POS_LEARN_FLAGS          90

/* SFLOW identifier for frame (0-8: Tx port, 9: Rx sampling, 15: No sampling) */
#define IFH_POS_SFLOW_ID             86

/* Set if an ACL/S2 rule was hit (*).
 * Super priority: acl_hit=0 and acl_hit(4)=1.
 */
#define IFH_POS_ACL_HIT              85

/* S2 rule index hit (*) */
#define IFH_POS_ACL_IDX              79

/* ISDX as classified by S1 */
#define IFH_POS_ISDX                 71

/* Destination ports for frame */
#define IFH_POS_DSTS                 62

/* Storm policer to be applied: None/Uni/Multi/Broad (*) */
#define IFH_POS_FLOOD                60

/* Redundancy tag operation */
#define IFH_POS_SEQ_OP               58

/* Classified internal priority for resourcemgt, tagging etc */
#define IFH_POS_IPV                  55

/* Frame is for AFI use */
#define IFH_POS_AFI                  54

/* Internal aging value (*) */
#define IFH_POS_AGED                 52

/* RTP Identifier */
#define IFH_POS_RTP_ID               42

/* RTP MRPD flow */
#define IFH_POS_RTP_SUBID            41

/* Profinet DataStatus or opcua GroupVersion MSB */
#define IFH_POS_PN_DATA_STATUS       33

/* Profinet transfer status (1 iff the status is 0) */
#define IFH_POS_PN_TRANSF_STATUS_ZERO 32

/* Profinet cycle counter or opcua NetworkMessageNumber */
#define IFH_POS_PN_CC                16

#define IFH_WID_TIMESTAMP            32
#define IFH_WID_BYPASS               1
#define IFH_WID_MASQ                 1
#define IFH_WID_MASQ_PORT            4
#define IFH_WID_LEN                  14
#define IFH_WID_WRDMODE              2
#define IFH_WID_RTAG48               1
#define IFH_WID_HAS_RED_TAG          1
#define IFH_WID_CUTTHRU              1
#define IFH_WID_REW_CMD              10
#define IFH_WID_REW_OAM              1
#define IFH_WID_PDU_TYPE             4
#define IFH_WID_FCS_UPD              1
#define IFH_WID_DSCP                 6
#define IFH_WID_DP                   1
#define IFH_WID_RTE_INB_UPDATE       1
#define IFH_WID_POP_CNT              2
#define IFH_WID_ETYPE_OFS            2
#define IFH_WID_SRCPORT              4
#define IFH_WID_SEQ_NUM              16
#define IFH_WID_TCI                  17
#define IFH_WID_QOS_CLASS            3
#define IFH_WID_CPUQ                 8
#define IFH_WID_LEARN_FLAGS          2
#define IFH_WID_SFLOW_ID             4
#define IFH_WID_ACL_HIT              1
#define IFH_WID_ACL_IDX              6
#define IFH_WID_ISDX                 8
#define IFH_WID_DSTS                 9
#define IFH_WID_FLOOD                2
#define IFH_WID_SEQ_OP               2
#define IFH_WID_IPV                  3
#define IFH_WID_AFI                  1
#define IFH_WID_AGED                 2
#define IFH_WID_RTP_ID               10
#define IFH_WID_RTP_SUBID            1
#define IFH_WID_PN_DATA_STATUS       8
#define IFH_WID_PN_TRANSF_STATUS_ZERO 1
#define IFH_WID_PN_CC                16

#endif /* __LAN966X_IFH_H__ */
