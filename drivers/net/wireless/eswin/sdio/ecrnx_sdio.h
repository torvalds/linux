 /**
 ******************************************************************************
 *
 * @file ecrnx_sdio.h
 *
 * @brief ecrnx sdio header file
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
 
#ifndef _ECRNX_SDIO_H_
#define _ECRNX_SDIO_H_

#include "ecrnx_rx.h"
#include "eswin_utils.h"
#include "ecrnx_defs.h"
/*
 * Number of Host buffers available for Data Rx handling
 */
#define SDIO_RXBUF_CNT       32
#define SDIO_RXBUF_SIZE      12288

enum{
    SDIO_FRM_TYPE_RXDESC =1,
    SDIO_FRM_TYPE_MSG,
    SDIO_FRM_TYPE_DBG,
    SDIO_FRM_TYPE_UNSUP_RX_VEC,
    SDIO_FRM_TYPE_RADAR,
    SDIO_FRM_TYPE_TBTT_SEC,
    SDIO_FRM_TYPE_TBTT_PRIM,
    SDIO_FRM_TYPE_MSG_ACK,
    SDIO_FRM_TYPE_TXCFM,
    SDIO_FRM_TYPE_IWPRIV,
    SDIO_FRM_TYPE_UPDATE
};


#define SKB_DATA_COM_HD_OFFSET      sizeof(dispatch_hdr_t)
//#define SKB_DATA_TAG_OFFSET         offsetof(struct rxu_stat_mm, phy_info) //two byte msdu flag
//#define SKB_DATA_HD_OFFSET          sizeof(struct rxu_stat_mm) + sizeof(struct rx_hd)


struct sdio_rx_skb{
    bool flag;
    u16 data_len;
    struct sk_buff *skb;
};

typedef struct{
    atomic_t suspend;
    uint8_t sdio_host_rx_buff_used;
    struct  sdio_rx_skb ecrnx_sdio_rx_skb[SDIO_RXBUF_CNT];
}sdio_rx_buf_t;

/// Structure containing the information about the PHY channel that is used
// typedef uint32_t u32_l;
struct phy_channel_info_sdio
{
    /// PHY channel information 1
    uint32_t info1;
    /// PHY channel information 2
    uint32_t info2;
};

/*调整phy info、 pattern，因为这些信息必须要放到另外一个buff中传输*/
/// Element in the pool of RX header descriptor.
struct rx_hd
{
    /// Total length of the received MPDU
    uint16_t            frmlen;
    /// AMPDU status information
    uint16_t            ampdu_stat_info;
    /// TSF Low
    uint32_t            tsflo;
    /// TSF High
    uint32_t            tsfhi;
    /// Rx Vector 1
    struct rx_vector_1  rx_vec_1;
    /// Rx Vector 2
    struct rx_vector_2  rx_vec_2;
    /// MPDU status information
    uint32_t            statinfo;
};

/*调整phy info、 pattern，因为这些信息必须要放到另外一个buff中传输*/
struct rxu_stat_mm
{
	/*type of msg/dbg/frm etc.*/
	dispatch_hdr_t  frm_type;
    uint32_t fragment_flag     : 1;
    uint32_t is_qos            : 1;
    uint32_t need_reord        : 1;
    uint32_t need_pn_check     : 1;
    uint32_t is_ga             : 1;
    uint32_t flags_rsvd0       : 3;
    uint32_t tid               : 8;
    uint16_t sn;
    /// Length
    uint16_t frame_len;
    /// Status (@ref rx_status_bits)
    uint16_t status;
    uint16_t real_offset; /* dma address align 4, real data - real_offset  */
};


extern sdio_rx_buf_t sdio_rx_buff;

/**
 * @brief: ecrnx_sdio_init  Initialize sdio interface.
 * @param {ecrnx_hw} Main driver data
 * @return: u8
 */
u8 ecrnx_sdio_init(struct ecrnx_hw *ecrnx_hw);

/**
 * @brief: ecrnx_sdio_deinit  DeInitialize sdio interface.
 * @param {ecrnx_hw} Main driver data
 * @return: none
 */
void ecrnx_sdio_deinit(struct ecrnx_hw *ecrnx_hw);

/**
 * @brief: sdio_rx_buf_push: push a skb element to sdio rx buffer
 * @param {skb}  skb data need to push
 * @param {recv_len}  skb data length
 * @return: sk_buff
 */
struct sk_buff * sdio_rx_buf_push(struct sk_buff *skb, uint16_t recv_len);


/**
 * @brief: sdio_rx_buf_pop: pop a skb element from sdio rx buffer
 * @param {void} 
 * @return: sk_buff
 */
struct sk_buff* sdio_rx_buf_pop(void);

#ifdef CONFIG_ECRNX_WIFO_CAIL
/**
 * @brief: sdio_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {skb} received skb data
 * @return: none
 */
void sdio_host_amt_rx_handler(uint32_t frm_type, struct sk_buff *skb);
#endif

/**
 * @brief: sdio_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {env} pointer to the sdio Host environment
 * @param {skb} received skb data
 * @return: none
 */
void sdio_host_rx_handler(uint32_t frm_type, struct ipc_host_env_tag *env, struct sk_buff *skb);

/**
 * @brief: rcv_skb_convert  convert the sdio received skb to the ecrnx handled skb.
 * @param {src_rxu_state} received rxu state in skb
 * @param {src_rxu_state} received rx header in skb
 * @param {src_rxu_state} handled hw rxhdr in skb
 * @param {src_rxu_state} handled rx desc tag  in skb
 * @return: u8
 */
u8 rcv_skb_convert(struct rxu_stat_mm* src_rxu_state, \
                struct rx_hd* src_rx_hd, \
                struct sk_buff *skb, \
                struct hw_rxhdr* dst_hw_rxhdr,\
                struct rxdesc_tag* dst_rxdesc_tag);

#endif /* __ECRNX_SDIO_H */
