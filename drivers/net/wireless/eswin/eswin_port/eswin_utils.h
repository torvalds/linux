/**
 ******************************************************************************
 *
 * @file eswin_utils.h
 *
 * @brief File containing the definition of tx/rx info and debug descriptors.
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ESWIN_UTILS_H_
#define _ESWIN_UTILS_H_

#if defined(CONFIG_ECRNX_ESWIN_SDIO)
// for param in sdio irq flag
#define FLAG_MSG_TYPE_MASK       0x0f

// queue in sdio irq flag
#define FLAG_TXDESC_QUEUE_SHIFT  5
#define FLAG_TXDESC_QUEUE_MASK   0xe0
#define FLAG_TXDESC_USER_SHIFT   4
#define FLAG_TXDESC_USER_MASK    0x10

// tag in tx payload param
//#define FLAG_TXPAYLOAD_PARAM_SHIFT 6
//#define FLAG_TXPAYLOAD_PARAM_MASK  0xffc0   //10 bits
#define TX_DESC_TAG_LEN            492  //SLAVE fhost_tx_desc_tag_len

#elif defined(CONFIG_ECRNX_ESWIN_USB)
// for param in sdio irq flag
#define FLAG_MSG_TYPE_MASK       0x00ff

// queue in sdio irq flag
#define FLAG_TXDESC_USER_SHIFT   8
#define FLAG_TXDESC_USER_MASK    0x0f00
#define FLAG_TXDESC_QUEUE_SHIFT  12
#define FLAG_TXDESC_QUEUE_MASK   0xf000
#endif

typedef enum {
    TX_FLAG_INVALID,
    TX_FLAG_MSG_E,
    TX_FLAG_MSG_SCAN_IE,
    TX_FLAG_MSG_SCANU_IE,
    TX_FLAG_MSG_BEACON_IE,
    TX_FLAG_MSG_PROBE_IE,
    TX_FLAG_MSG_SOFTAP_IE,
    TX_FLAG_TX_DESC,
    TX_FLAG_MSG_DEBUGFS_IE,
    TX_FLAG_IWPRIV_IE,
#ifdef CONFIG_ECRNX_WIFO_CAIL
	TX_FLAG_AMT_IWPRIV_IE,	//10
#endif

    TX_FLAG_MAX,
}host_tx_flag_e;

typedef enum {
    DBG_TYPE_D,      //debug msg type
    DBG_TYPE_I,      //info msg type
    DBG_TYPE_W,      //warning msg type
    DBG_TYPE_E,      //error msg type
    DBG_TYPE_O,      //this type means always output
    DBG_TYPE_MAX
}dbg_print_type_e;

typedef struct {
    uint8_t direct; // slave log director, 0 is print by uart, 1 is send to host
    dbg_print_type_e dbg_level; //slave debug level
}dbg_req_t;

typedef struct {
    uint32_t  hostid;
    uint32_t  tag;
}data_req_msg_t;

typedef struct {
#ifdef CONFIG_ECRNX_ESWIN_SDIO
    uint32_t resvd; /* for sdio */
    uint32_t rlen;  /* for sdio */
#endif
    uint32_t frm_type;
}dispatch_hdr_t;

/**
 * struct pulse_elem - elements in pulse queue
 * @ts: time stamp in usecs
 */
struct agg_elem {
    struct list_head head;
    struct sk_buff* skb;
    /// Traffic Index
    uint8_t tid;
    uint16_t real_offset; /* dma address align 4, real data - real_offset  */
    u32 sn;
    /*bitmaps of agg frame to upaold*/
    uint64_t agg_upload_idx;
    /*bitmaps of agg frame upaold status, 0 is forward, 1 is delete*/
    uint64_t agg_upload_status;
};

/**
 * struct pulse_elem - elements in pulse queue
 * @ts: time stamp in usecs
 */
struct defrag_elem {
    struct list_head head;
    struct sk_buff* skb;
    /// Traffic Index
    uint8_t tid;
    uint16_t real_offset; /* dma address align 4, real data - real_offset  */
    u32 sn;
};

void ecrnx_frame_send(struct ecrnx_hw *ecrnx_hw, struct txdesc_api *tx_desc,
                          struct sk_buff *skb, int hw_queue, int user);
void ecrnx_msg_send(struct ecrnx_cmd *cmd, uint16_t len);
int host_send(void *buff, int len, int flag);
const struct ecrnx_element *cfg80211_find_ecrnx_elem(u8 eid, const u8 *ies, int len);

#endif
