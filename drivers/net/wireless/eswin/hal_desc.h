/**
 ******************************************************************************
 *
 * @file hal_desc.h
 *
 * @brief File containing the definition of HW descriptors.
 *
 * Contains the definition and structures used by HW
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _HAL_DESC_H_
#define _HAL_DESC_H_

#include "lmac_types.h"

#define ECRNX_MACHW_NX 1
#define ECRNX_MACHW_HE 2
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

struct ecrnx_legrate {
    s16 idx;
    u16 rate;  // in 100Kbps
};
extern const struct ecrnx_legrate legrates_lut[];
/* Values for formatModTx */
#define FORMATMOD_NON_HT          0
#define FORMATMOD_NON_HT_DUP_OFDM 1
#define FORMATMOD_HT_MF           2
#define FORMATMOD_HT_GF           3
#define FORMATMOD_VHT             4
#define FORMATMOD_HE_SU           5
#define FORMATMOD_HE_MU           6
#define FORMATMOD_HE_ER           7
#define FORMATMOD_HE_TB           8

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

union ecrnx_mcs_index {
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

union ecrnx_rate_ctrl_info {
    struct {
        u32 mcsIndexTx      : 7;
        u32 bwTx            : 2;
        u32 giAndPreTypeTx  : 2;
        u32 formatModTx     : 3;
        #ifdef CONFIG_ECRNX_FULLMAC
        u32 dcmTx           : 1;
        #else
        u32 navProtFrmEx    : 3;
        u32 mcsIndexProtTx  : 7;
        u32 bwProtTx        : 2;
        u32 formatModProtTx : 3;
        u32 nRetry          : 3;
        #endif
    };
    u32 value;
};

struct ecrnx_power_ctrl_info {
    u32 txPwrLevelPT          : 8;
    u32 txPwrLevelProtPT      : 8;
    u32 reserved              :16;
};

union ecrnx_pol_phy_ctrl_info_1 {
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

union ecrnx_pol_phy_ctrl_info_2 {
    struct {
        u32 antennaSet : 8;
        u32 smmIndex   : 8;
        u32 beamFormed : 1;
    };
    u32 value;
};

union ecrnx_pol_mac_ctrl_info_1 {
    struct {
        u32 keySRamIndex   : 10;
        u32 keySRamIndexRA : 10;
    };
    u32 value;
};

union ecrnx_pol_mac_ctrl_info_2 {
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
    union ecrnx_pol_phy_ctrl_info_1 phyctrlinfo_1;
    /* PHY Control 2 Information used by MAC HW */
    union ecrnx_pol_phy_ctrl_info_2 phyctrlinfo_2;
    /* MAC Control 1 Information used by MAC HW */
    union ecrnx_pol_mac_ctrl_info_1 macctrlinfo_1;
    /* MAC Control 2 Information used by MAC HW */
    union ecrnx_pol_mac_ctrl_info_2 macctrlinfo_2;

    union ecrnx_rate_ctrl_info  ratectrlinfos[NX_TX_MAX_RATES];
    struct ecrnx_power_ctrl_info powerctrlinfos[NX_TX_MAX_RATES];
};

#ifdef CONFIG_ECRNX_SOFTMAC

union ecrnx_hw_txstatus {
    struct {
        u32 num_rts_retries      : 8;
        u32 num_mpdu_retries     : 8;
        u32 retry_limit_reached  : 1;
        u32 lifetime_expired     : 1;
        u32 baFrameReceived      : 1;
        u32 reserved2            : 4;
        u32 frm_successful_tx    : 1;
        u32 transmission_bw      : 2;
        u32 which_descriptor_sw  : 4;
        u32 descriptor_done_swtx : 1;
        u32 descriptor_done_hwtx : 1;
    };
    u32 value;
};

// WhichDescriptor for AMPDUs (_under BA Policy_)
#define __WD_AMPDU_BAPOL                     0xC
#define __WD_AMPDU_EXTRA                     0xC
#define __WD_AMPDU_FIRST                     0xD
#define __WD_AMPDU_INT                       0xE
#define __WD_AMPDU_LAST                      0xF

#define ECRNX_WD_IS_AMPDU(whichdesc) \
    (((whichdesc) & __WD_AMPDU_BAPOL) == __WD_AMPDU_BAPOL)
#define ECRNX_WD_IS_FIRST(whichdesc) \
    ((whichdesc) == __WD_AMPDU_FIRST)
#define ECRNX_WD_IS_LAST(whichdesc) \
    ((whichdesc) == __WD_AMPDU_LAST)

union ecrnx_thd_phy_ctrl_info {
    struct {
        u32 soundingTx       : 1;
        u32 smoothingTx      : 1;
        u32 smoothingProtTx  : 1;
        u32 useBWSignalingTx : 1;
        u32 dynBWTx          : 1;
        u32 dozeNotAllowedTx : 1;
        u32 continuousTx     : 1;
        u32 rsvd             : 1;
        u32 PTITx            : 4;
        u32 userPositionTx   : 2;
        u32 useRIFSTx        : 1;
        u32 muMIMOTx         : 1;
        u32 groupIDTx        : 6;
        u32 partialAIDTx     : 9;
    };
    u32 value;
};

#define EXPECTED_ACK_NO_ACK               0
#define EXPECTED_ACK_NORMAL_ACK           1
#define EXPECTED_ACK_BLOCK_ACK            2
#define EXPECTED_ACK_COMPRESSED_BLOCK_ACK 3

union ecrnx_thd_mac_ctrl_info_1 {
    struct {
        u32 rsvd1        : 9;
        u32 expectedAck  : 2;
        u32 lstp         : 1;
        u32 lstpProt     : 1;
        u32 lowRateRetry : 1;
        u32 writeACK     : 1;
        u32 rsvd2        : 1;
        u32 protFrmDur   : 16;
    };
    u32 value;
};

/**
 * struct ecrnx_hw_txhdr - Hardware part of tx header
 *
 * @policy: Policy table to use for transmission
 * @mac_ctrl_info: MAC configuration to use for transmission
 * @phy_ctrl_info: PHY configuration to use for transmission
 *
 * @status: Status updated by fw/hardware after transmission
 */
struct ecrnx_hw_txhdr {
    struct tx_policy_tbl policy;
    union ecrnx_thd_mac_ctrl_info_1 mac_ctrl_info;
    union ecrnx_thd_phy_ctrl_info phy_ctrl_info;
    union ecrnx_hw_txstatus status;
};

#else /* !CONFIG_ECRNX_SOFTMAC */

/**
 * struct ecrnx_hw_txstatus - Bitfield of confirmation status
 *
 * @tx_done: packet has been processed by the firmware.
 * @retry_required: packet has been transmitted but not acknoledged.
 * Driver must repush it.
 * @sw_retry_required: packet has not been transmitted (FW wasn't able to push
 * it when it received it: not active channel ...). Driver must repush it.
 * @acknowledged: packet has been acknowledged by peer
 */
union ecrnx_hw_txstatus {
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
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    u16_l amsdu_size;
#endif

#ifdef CONFIG_ECRNX_ESWIN
    uint32_t  hostid[2];
#endif

    union ecrnx_hw_txstatus status;
};

/**
 * struct ecrnx_hw_txhdr - Hardware part of tx header
 *
 * @cfm: Information updated by fw/hardware after sending a frame
 */
struct ecrnx_hw_txhdr {
    struct tx_cfm_tag cfm;
};

#endif /* CONFIG_ECRNX_SOFTMAC */

#define ECRNX_RX_HD_NX_DECR_UNENC           0 // Frame unencrypted
#define ECRNX_RX_HD_NX_DECR_ICVFAIL         1 // WEP/TKIP ICV failure
#define ECRNX_RX_HD_NX_DECR_CCMPFAIL        2 // CCMP failure
#define ECRNX_RX_HD_NX_DECR_AMSDUDISCARD    3 // A-MSDU discarded at HW
#define ECRNX_RX_HD_NX_DECR_NULLKEY         4 // NULL key found
#define ECRNX_RX_HD_NX_DECR_WEPSUCCESS      5 // Security type WEP
#define ECRNX_RX_HD_NX_DECR_TKIPSUCCESS     6 // Security type TKIP
#define ECRNX_RX_HD_NX_DECR_CCMPSUCCESS     7 // Security type CCMP (or WPI)
#define ECRNX_RX_HD_DECR_UNENC     0 // Frame unencrypted
#define ECRNX_RX_HD_DECR_WEP       1 // Security type WEP
#define ECRNX_RX_HD_DECR_TKIP      2 // Security type TKIP
#define ECRNX_RX_HD_DECR_CCMP128   3 // Security type CCMP (128 bits)
#define ECRNX_RX_HD_DECR_CCMP256   4 // Security type CCMP (256 bits)
#define ECRNX_RX_HD_DECR_GCMP128   5 // Security type GCMP (128 bits)
#define ECRNX_RX_HD_DECR_GCMP256   6 // Security type GCMP (256 bits)
#define ECRNX_RX_HD_DECR_WAPI      7 // Security type WAPI
#define ECRNX_RX_HD_DECR_NULLKEY  15 // NULL key found
struct rx_vector_1_nx {
    u32    leg_length         :12;
    u32    leg_rate           : 4;
    u32    ht_length          :16;
    u32    _ht_length         : 4; // FIXME
    u32    short_gi           : 1;
    u32    stbc               : 2;
    u32    smoothing          : 1;
    u32    mcs                : 7;
    u32    pre_type           : 1;
    u32    format_mod         : 3;
    u32    ch_bw              : 2;
    u32    n_sts              : 3;
    u32    lsig_valid         : 1;
    u32    sounding           : 1;
    u32    num_extn_ss        : 2;
    u32    aggregation        : 1;
    u32    fec_coding         : 1;
    u32    dyn_bw             : 1;
    u32    doze_not_allowed   : 1;
    u32    antenna_set        : 8;
    u32    partial_aid        : 9;
    u32    group_id           : 6;
    u32    first_user         : 1;
    s32    rssi1              : 8;
    s32    rssi2              : 8;
    s32    rssi3              : 8;
    s32    rssi4              : 8;
    u32    reserved_1d        : 8;
};
struct rx_vector_2_nx {
    u32    rcpi               : 8;
    u32    evm1               : 8;
    u32    evm2               : 8;
    u32    evm3               : 8;
    u32    evm4               : 8;
    u32    reserved2b_1       : 8;
    u32    reserved2b_2       : 8;
    u32    reserved2b_3       : 8;
};
struct mpdu_status_nx {
    u32    rx_vect2_valid     : 1;
    u32    resp_frame         : 1;
    u32    decr_status        : 3;
    u32    rx_fifo_oflow      : 1;
    u32    undef_err          : 1;
    u32    phy_err            : 1;
    u32    fcs_err            : 1;
    u32    addr_mismatch      : 1;
    u32    ga_frame           : 1;
    u32    current_ac         : 2;
    u32    frm_successful_rx  : 1;
    u32    desc_done_rx       : 1;
    u32    key_sram_index     : 10;
    u32    key_sram_valid     : 1;
    u32    type               : 2;
    u32    subtype            : 4;
};
struct rx_leg_vect
{
    u8    dyn_bw_in_non_ht     : 1;
    u8    chn_bw_in_non_ht     : 2;
    u8    rsvd_nht             : 4;
    u8    lsig_valid           : 1;
} __packed;
struct rx_ht_vect
{
    u16   sounding             : 1;
    u16   smoothing            : 1;
    u16   short_gi             : 1;
    u16   aggregation          : 1;
    u16   stbc                 : 1;
    u16   num_extn_ss          : 2;
    u16   lsig_valid           : 1;
    u16   mcs                  : 7;
    u16   fec                  : 1;
    u16   length               :16;
} __packed;
struct rx_vht_vect
{
    u8   sounding              : 1;
    u8   beamformed            : 1;
    u8   short_gi              : 1;
    u8   rsvd_vht1             : 1;
    u8   stbc                  : 1;
    u8   doze_not_allowed      : 1;
    u8   first_user            : 1;
    u8   rsvd_vht2             : 1;
    u16  partial_aid           : 9;
    u16  group_id              : 6;
    u16  rsvd_vht3             : 1;
    u32  mcs                   : 4;
    u32  nss                   : 3;
    u32  fec                   : 1;
    u32  length                :20;
    u32  rsvd_vht4             : 4;
} __packed;
struct rx_he_vect
{
    u8   sounding              : 1;
    u8   beamformed            : 1;
    u8   gi_type               : 2;
    u8   stbc                  : 1;
    u8   rsvd_he1              : 3;
    u8   uplink_flag           : 1;
    u8   beam_change           : 1;
    u8   dcm                   : 1;
    u8   he_ltf_type           : 2;
    u8   doppler               : 1;
    u8   rsvd_he2              : 2;
    u8   bss_color             : 6;
    u8   rsvd_he3              : 2;
    u8   txop_duration         : 7;
    u8   rsvd_he4              : 1;
    u8   pe_duration           : 4;
    u8   spatial_reuse         : 4;
    u8   sig_b_comp_mode       : 1;
    u8   dcm_sig_b             : 1;
    u8   mcs_sig_b             : 3;
    u8   ru_size               : 3;
    u32  mcs                   : 4;
    u32  nss                   : 3;
    u32  fec                   : 1;
    u32  length                :20;
    u32  rsvd_he6              : 4;
} __packed;
struct rx_vector_1 {
    u8     format_mod         : 4;
    u8     ch_bw              : 3;
    u8     pre_type           : 1;
    u8     antenna_set        : 8;
    s32    rssi_leg           : 8;
    u32    leg_length         :12;
    u32    leg_rate           : 4;
    s32    rssi1              : 8;
    union
    {
        struct rx_leg_vect leg;
        struct rx_ht_vect ht;
        struct rx_vht_vect vht;
        struct rx_he_vect he;
    };
} __packed;
struct rx_vector_2 {
    u32    rcpi1              : 8;
    u32    rcpi2              : 8;
    u32    rcpi3              : 8;
    u32    rcpi4              : 8;
    u32    evm1               : 8;
    u32    evm2               : 8;
    u32    evm3               : 8;
    u32    evm4               : 8;
};
struct mpdu_status {
    u32    rx_vect2_valid     : 1;
    u32    resp_frame         : 1;
    u32    decr_type          : 4;
    u32    decr_err           : 1;
    u32    undef_err          : 1;
    u32    fcs_err            : 1;
    u32    addr_mismatch      : 1;
    u32    ga_frame           : 1;
    u32    current_ac         : 2;
    u32    frm_successful_rx  : 1;
    u32    desc_done_rx       : 1;
    u32    key_sram_index     : 10;
    u32    key_sram_v         : 1;
    u32    type               : 2;
    u32    subtype            : 4;
};
struct hw_vect {
    u32 len                   :16;
    u32 reserved              : 8;
    u32 mpdu_cnt              : 6;
    u32 ampdu_cnt             : 2;
    __le32 tsf_lo;
    /** TSF High */
    __le32 tsf_hi;

    /** Receive Vector 1 */
    struct rx_vector_1 rx_vect1;
    /** Receive Vector 2 */
    struct rx_vector_2 rx_vect2;

    /** MPDU status information */
    struct mpdu_status status;
};

struct phy_channel_info_desc {
    /** PHY channel information 1 */
    u32    phy_band           : 8;
    u32    phy_channel_type   : 8;
    u32    phy_prim20_freq    : 16;

    /** PHY channel information 2 */
    u32    phy_center1_freq   : 16;
    u32    phy_center2_freq   : 16;
};

int ecrnx_machw_type(uint32_t machw_version_2);
void ecrnx_rx_vector_convert(int machw_type, struct rx_vector_1 *rx_vect1,
                            struct rx_vector_2 *rx_vect2);
void ecrnx_rx_status_convert(int machw_type, struct mpdu_status *status);

/******************************************************************************
 * Modem
 ******************************************************************************/
#define MDM_PHY_CONFIG_TRIDENT     0
#define MDM_PHY_CONFIG_CATAXIA     1
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
#define __MDM_VERSION(v)       ((__MDM_MAJOR_VERSION(v) + 2) * 10 + __MDM_MINOR_VERSION(v))


#endif // _HAL_DESC_H_
