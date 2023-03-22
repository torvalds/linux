/**
 ******************************************************************************
 *
 * @file hal_desc.h
 *
 * @brief File containing the definition of HW descriptors.
 *
 * Contains the definition and structures used by HW
 *
 * Copyright (C) RivieraWaves 2011-2019
 *
 ******************************************************************************
 */

#ifndef _HAL_DESC_H_
#define _HAL_DESC_H_

#include "lmac_types.h"

/* Rate and policy table */

#define N_CCK  8
#define N_OFDM 8
#define N_HT   (8 * 2 * 2 * 4)
#define N_VHT  (10 * 4 * 2 * 8)
#define N_HE_SU (12 * 4 * 3 * 8)
#define N_HE_MU (12 * 6 * 3 * 8)

/* conversion table from NL80211 to MACHW enum */
extern const int chnl2bw[];

/* conversion table from MACHW to NL80211 enum */
extern const int bw2chnl[];

/* Rate cntrl info */
#define MCS_INDEX_TX_RCX_OFT    0
#define MCS_INDEX_TX_RCX_MASK   (0x7F << MCS_INDEX_TX_RCX_OFT)
#define BW_TX_RCX_OFT           7
#define BW_TX_RCX_MASK          (0x3 << BW_TX_RCX_OFT)
#define SHORT_GI_TX_RCX_OFT     9
#define SHORT_GI_TX_RCX_MASK    (0x1 << SHORT_GI_TX_RCX_OFT)
#define PRE_TYPE_TX_RCX_OFT     10
#define PRE_TYPE_TX_RCX_MASK    (0x1 << PRE_TYPE_TX_RCX_OFT)
#define FORMAT_MOD_TX_RCX_OFT   11
#define FORMAT_MOD_TX_RCX_MASK  (0x7 << FORMAT_MOD_TX_RCX_OFT)

/* Values for formatModTx */
#define FORMATMOD_NON_HT          0
#define FORMATMOD_NON_HT_DUP_OFDM 1
#define FORMATMOD_HT_MF           2
#define FORMATMOD_HT_GF           3
#define FORMATMOD_VHT             4
#define FORMATMOD_HE_SU           5
#define FORMATMOD_HE_MU           6
#define FORMATMOD_HE_ER           7

/* Values for navProtFrmEx */
#define NAV_PROT_NO_PROT_BIT                 0
#define NAV_PROT_SELF_CTS_BIT                1
#define NAV_PROT_RTS_CTS_BIT                 2
#define NAV_PROT_RTS_CTS_WITH_QAP_BIT        3
#define NAV_PROT_STBC_BIT                    4

/* THD MACCTRLINFO2 fields, used in  struct umacdesc umac.flags */
/// WhichDescriptor definition - contains aMPDU bit and position value
/// Offset of WhichDescriptor field in the MAC CONTROL INFO 2 word
#define WHICHDESC_OFT                     19
/// Mask of the WhichDescriptor field
#define WHICHDESC_MSK                     (0x07 << WHICHDESC_OFT)
/// Only 1 THD possible, describing an unfragmented MSDU
#define WHICHDESC_UNFRAGMENTED_MSDU       (0x00 << WHICHDESC_OFT)
/// THD describing the first MPDU of a fragmented MSDU
#define WHICHDESC_FRAGMENTED_MSDU_FIRST   (0x01 << WHICHDESC_OFT)
/// THD describing intermediate MPDUs of a fragmented MSDU
#define WHICHDESC_FRAGMENTED_MSDU_INT     (0x02 << WHICHDESC_OFT)
/// THD describing the last MPDU of a fragmented MSDU
#define WHICHDESC_FRAGMENTED_MSDU_LAST    (0x03 << WHICHDESC_OFT)
/// THD for extra descriptor starting an AMPDU
#define WHICHDESC_AMPDU_EXTRA             (0x04 << WHICHDESC_OFT)
/// THD describing the first MPDU of an A-MPDU
#define WHICHDESC_AMPDU_FIRST             (0x05 << WHICHDESC_OFT)
/// THD describing intermediate MPDUs of an A-MPDU
#define WHICHDESC_AMPDU_INT               (0x06 << WHICHDESC_OFT)
/// THD describing the last MPDU of an A-MPDU
#define WHICHDESC_AMPDU_LAST              (0x07 << WHICHDESC_OFT)

/// aMPDU bit offset
#define AMPDU_OFT                         21
/// aMPDU bit
#define AMPDU_BIT                         CO_BIT(AMPDU_OFT)

enum {
    HW_RATE_1MBPS   = 0,
    HW_RATE_2MBPS   = 1,
    HW_RATE_5_5MBPS = 2,
    HW_RATE_11MBPS  = 3,
    HW_RATE_6MBPS   = 4,
    HW_RATE_9MBPS   = 5,
    HW_RATE_12MBPS  = 6,
    HW_RATE_18MBPS  = 7,
    HW_RATE_24MBPS  = 8,
    HW_RATE_36MBPS  = 9,
    HW_RATE_48MBPS  = 10,
    HW_RATE_54MBPS  = 11,
    HW_RATE_MAX
};

union rwnx_mcs_index {
    struct {
        u32 mcs : 3;
        u32 nss : 2;
    } ht;
    struct {
        u32 mcs : 4;
        u32 nss : 3;
    } vht;
    struct {
        u32 mcs : 4;
        u32 nss : 3;
    } he;
    u32 legacy : 7;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_rate_ctrl_info {
    struct {
        u32 mcsIndexTx      : 7;
        u32 bwTx            : 2;
        u32 giAndPreTypeTx  : 2;
        u32 formatModTx     : 3;
        u32 navProtFrmEx    : 3;
        u32 mcsIndexProtTx  : 7;
        u32 bwProtTx        : 2;
        u32 formatModProtTx : 3;
        u32 nRetry          : 3;
    };
    u32 value;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
struct rwnx_power_ctrl_info {
    u32 txPwrLevelPT          : 8;
    u32 txPwrLevelProtPT      : 8;
    u32 reserved              :16;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_pol_phy_ctrl_info_1 {
    struct {
        u32 rsvd1     : 3;
        u32 bfFrmEx   : 1;
        u32 numExtnSS : 2;
        u32 fecCoding : 1;
        u32 stbc      : 2;
        u32 rsvd2     : 5;
        u32 nTx       : 3;
        u32 nTxProt   : 3;
    };
    u32 value;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_pol_phy_ctrl_info_2 {
    struct {
        u32 antennaSet : 8;
        u32 smmIndex   : 8;
        u32 beamFormed : 1;
    };
    u32 value;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_pol_mac_ctrl_info_1 {
    struct {
        u32 keySRamIndex   : 10;
        u32 keySRamIndexRA : 10;
    };
    u32 value;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_pol_mac_ctrl_info_2 {
    struct {
        u32 longRetryLimit  : 8;
        u32 shortRetryLimit : 8;
        u32 rtsThreshold    : 12;
    };
    u32 value;
};

#define POLICY_TABLE_PATTERN    0xBADCAB1E

struct tx_policy_tbl {
    /* Unique Pattern at the start of Policy Table */
    u32 upatterntx;
    /* PHY Control 1 Information used by MAC HW */
    union rwnx_pol_phy_ctrl_info_1 phyctrlinfo_1;
    /* PHY Control 2 Information used by MAC HW */
    union rwnx_pol_phy_ctrl_info_2 phyctrlinfo_2;
    /* MAC Control 1 Information used by MAC HW */
    union rwnx_pol_mac_ctrl_info_1 macctrlinfo_1;
    /* MAC Control 2 Information used by MAC HW */
    union rwnx_pol_mac_ctrl_info_2 macctrlinfo_2;

    union rwnx_rate_ctrl_info  ratectrlinfos[NX_TX_MAX_RATES];
    struct rwnx_power_ctrl_info powerctrlinfos[NX_TX_MAX_RATES];
};

#ifdef CONFIG_RWNX_FULLMAC

/**
 * struct rwnx_hw_txstatus - Bitfield of confirmation status
 *
 * @tx_done: packet has been processed by the firmware.
 * @retry_required: packet has been transmitted but not acknoledged.
 * Driver must repush it.
 * @sw_retry_required: packet has not been transmitted (FW wasn't able to push
 * it when it received it: not active channel ...). Driver must repush it.
 * @acknowledged: packet has been acknowledged by peer
 */
union rwnx_hw_txstatus {
    struct {
        u32 tx_done            : 1;
        u32 retry_required     : 1;
        u32 sw_retry_required  : 1;
        u32 acknowledged       : 1;
        u32 reserved           :28;
    };
    u32 value;
};

/**
 * struct tx_cfm_tag - Structure indicating the status and other
 * information about the transmission
 *
 * @pn: PN that was used for the transmission
 * @sn: Sequence number of the packet
 * @timestamp: Timestamp of first transmission of this MPDU
 * @credits: Number of credits to be reallocated for the txq that push this
 * buffer (can be 0 or 1)
 * @ampdu_size: Size of the ampdu in which the frame has been transmitted if
 * this was the last frame of the a-mpdu, and 0 if the frame is not the last
 * frame on a a-mdpu.
 * 1 means that the frame has been transmitted as a singleton.
 * @amsdu_size: Size, in bytes, allowed to create a-msdu.
 * @status: transmission status
 */
struct tx_cfm_tag
{
    u16_l pn[4];
    u16_l sn;
    u16_l timestamp;
    s8_l credits;
    u8_l ampdu_size;
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    u16_l amsdu_size;
#endif
    union rwnx_hw_txstatus status;
};

/**
 * struct rwnx_hw_txhdr - Hardware part of tx header
 *
 * @cfm: Information updated by fw/hardware after sending a frame
 */
struct rwnx_hw_txhdr {
    struct tx_cfm_tag cfm;
};

#endif /* CONFIG_RWNX_FULLMAC */

/* Modem */

#define MDM_PHY_CONFIG_TRIDENT     0
#define MDM_PHY_CONFIG_ELMA        1
#define MDM_PHY_CONFIG_KARST       2

// MODEM features (from reg_mdm_stat.h)
/// MUMIMOTX field bit
#define MDM_MUMIMOTX_BIT    ((u32)0x80000000)
/// MUMIMOTX field position
#define MDM_MUMIMOTX_POS    31
/// MUMIMORX field bit
#define MDM_MUMIMORX_BIT    ((u32)0x40000000)
/// MUMIMORX field position
#define MDM_MUMIMORX_POS    30
/// BFMER field bit
#define MDM_BFMER_BIT       ((u32)0x20000000)
/// BFMER field position
#define MDM_BFMER_POS       29
/// BFMEE field bit
#define MDM_BFMEE_BIT       ((u32)0x10000000)
/// BFMEE field position
#define MDM_BFMEE_POS       28
/// LDPCDEC field bit
#define MDM_LDPCDEC_BIT     ((u32)0x08000000)
/// LDPCDEC field position
#define MDM_LDPCDEC_POS     27
/// LDPCENC field bit
#define MDM_LDPCENC_BIT     ((u32)0x04000000)
/// LDPCENC field position
#define MDM_LDPCENC_POS     26
/// CHBW field mask
#define MDM_CHBW_MASK       ((u32)0x03000000)
/// CHBW field LSB position
#define MDM_CHBW_LSB        24
/// CHBW field width
#define MDM_CHBW_WIDTH      ((u32)0x00000002)
/// DSSSCCK field bit
#define MDM_DSSSCCK_BIT     ((u32)0x00800000)
/// DSSSCCK field position
#define MDM_DSSSCCK_POS     23
/// VHT field bit
#define MDM_VHT_BIT         ((u32)0x00400000)
/// VHT field position
#define MDM_VHT_POS         22
/// HE field bit
#define MDM_HE_BIT          ((u32)0x00200000)
/// HE field position
#define MDM_HE_POS          21
/// ESS field bit
#define MDM_ESS_BIT         ((u32)0x00100000)
/// ESS field position
#define MDM_ESS_POS         20
/// RFMODE field mask
#define MDM_RFMODE_MASK     ((u32)0x000F0000)
/// RFMODE field LSB position
#define MDM_RFMODE_LSB      16
/// RFMODE field width
#define MDM_RFMODE_WIDTH    ((u32)0x00000004)
/// NSTS field mask
#define MDM_NSTS_MASK       ((u32)0x0000F000)
/// NSTS field LSB position
#define MDM_NSTS_LSB        12
/// NSTS field width
#define MDM_NSTS_WIDTH      ((u32)0x00000004)
/// NSS field mask
#define MDM_NSS_MASK        ((u32)0x00000F00)
/// NSS field LSB position
#define MDM_NSS_LSB         8
/// NSS field width
#define MDM_NSS_WIDTH       ((u32)0x00000004)
/// NTX field mask
#define MDM_NTX_MASK        ((u32)0x000000F0)
/// NTX field LSB position
#define MDM_NTX_LSB         4
/// NTX field width
#define MDM_NTX_WIDTH       ((u32)0x00000004)
/// NRX field mask
#define MDM_NRX_MASK        ((u32)0x0000000F)
/// NRX field LSB position
#define MDM_NRX_LSB         0
/// NRX field width
#define MDM_NRX_WIDTH       ((u32)0x00000004)

#define __MDM_PHYCFG_FROM_VERS(v)  (((v) & MDM_RFMODE_MASK) >> MDM_RFMODE_LSB)

#define RIU_FCU_PRESENT_MASK       ((u32)0xFF000000)
#define RIU_FCU_PRESENT_LSB        24

#define __RIU_FCU_PRESENT(v)  (((v) & RIU_FCU_PRESENT_MASK) >> RIU_FCU_PRESENT_LSB == 5)

/// AGC load version field mask
#define RIU_AGC_LOAD_MASK          ((u32)0x00C00000)
/// AGC load version field LSB position
#define RIU_AGC_LOAD_LSB           22

#define __RIU_AGCLOAD_FROM_VERS(v) (((v) & RIU_AGC_LOAD_MASK) >> RIU_AGC_LOAD_LSB)

#define __FPGA_TYPE(v)             (((v) & 0xFFFF0000) >> 16)

#define __MDM_MAJOR_VERSION(v)     (((v) & 0xFF000000) >> 24)
#define __MDM_MINOR_VERSION(v)     (((v) & 0x00FF0000) >> 16)


#endif // _HAL_DESC_H_
