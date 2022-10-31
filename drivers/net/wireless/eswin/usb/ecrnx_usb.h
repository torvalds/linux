/**
******************************************************************************
*
* @file ecrnx_usb.h
*
* @brief ecrnx usb header file
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#ifndef _ECRNX_USB_H_
#define _ECRNX_USB_H_

#include "ecrnx_rx.h"
#include "eswin_utils.h"

enum{
    USB_FRM_TYPE_RXDESC =1,
    USB_FRM_TYPE_MSG,
    USB_FRM_TYPE_DBG,
    USB_FRM_TYPE_UNSUP_RX_VEC,
    USB_FRM_TYPE_RADAR,
    USB_FRM_TYPE_TBTT_SEC,
    USB_FRM_TYPE_TBTT_PRIM,
    USB_FRM_TYPE_MSG_ACK,
    USB_FRM_TYPE_TXCFM,
    USB_FRM_TYPE_IWPRIV,
    USB_FRM_DEBUG_FS,
};


#define SKB_DATA_COM_HD_OFFSET      sizeof(dispatch_hdr_t)

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

/*adjust phy info,pattern, these infomation should be put to another buffer to trnasfer*/
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



/**
 * @brief: ecrnx_usb_init  Initialize usb interface.
 * @param {ecrnx_hw} Main driver data
 * @return: u8
 */
u8 ecrnx_usb_init(struct ecrnx_hw *ecrnx_hw);

/**
 * @brief: ecrnx_usb_deinit  DeInitialize usb interface.
 * @param {ecrnx_hw} Main driver data
 * @return: none
 */
void ecrnx_usb_deinit(struct ecrnx_hw *ecrnx_hw);

#ifdef CONFIG_ECRNX_WIFO_CAIL
/**
 * @brief: usb_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {skb} received skb data
 * @return: none
 */
void usb_host_amt_rx_handler(uint32_t frm_type, struct sk_buff *skb);
#endif /* CONFIG_ECRNX_WIFO_CAIL */

/**
 * @brief: usb_host_rx_handler  Handle the reception of rx frame
 * @param {frm_type} received frame type
 * @param {env} pointer to the usb Host environment
 * @param {skb} received skb data
 * @return: none
 */
void usb_host_rx_handler(uint32_t frm_type, struct ipc_host_env_tag *env, struct sk_buff *skb);

#endif /* __ECRNX_USB_H */
