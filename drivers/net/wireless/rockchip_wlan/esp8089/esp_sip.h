/*
 *  Copyright (c) 2009- 2012 Espressif System.
 *
 *    Serial Interconnctor Protocol
 */

#ifndef _ESP_SIP_H
#define _ESP_SIP_H

#include "sip2_common.h"

#define SIP_PKT_MAX_LEN (1024*16)

/* 16KB on normal X86 system, should check before porting to orhters */

#define SIP_TX_AGGR_BUF_SIZE (4 * PAGE_SIZE)
#define SIP_RX_AGGR_BUF_SIZE (4 * PAGE_SIZE)

struct sk_buff;

struct sip_pkt {
        struct list_head list;

        u8 * buf_begin;
        u32  buf_len;
        u8 * buf;
        u32  payload_len;
#if 0
        union {
                struct sip_tx_pkt_info tx;
                struct sip_rx_pkt_info rx;
        } info;
#endif
        void *context;
        int status;
        //void (* completion)(struct esp_sip *sip, struct sip_pkt *pkt);
};

typedef enum SIP_STATE {
        SIP_INIT = 0,
	SIP_PREPARE_BOOT,
        SIP_BOOT,
	SIP_SEND_INIT,
	SIP_WAIT_BOOTUP,
        SIP_RUN,
        SIP_SUSPEND,
        SIP_STOP
} SIP_STATE;

enum sip_notifier {
        SIP_TX_DONE = 1,
        SIP_RX_DONE = 2,
};

#define SIP_CREDITS_LOW_THRESHOLD  64  //i.e. 4k

struct esp_sip {
        struct list_head free_ctrl_txbuf;
        struct list_head free_ctrl_rxbuf;

        u32 rxseq; /* sip pkt seq, should match target side */
        u32 txseq;
	u32 txdataseq;

        atomic_t state;
        spinlock_t lock;
        int boot_credits;
        atomic_t tx_credits;

        atomic_t tx_ask_credit_update;

        u8 * rawbuf;  /* used in boot stage, free once chip is fully up */
        u8 * tx_aggr_buf;
        u8 * tx_aggr_write_ptr;  /* update after insertion of each pkt */
        u8 * tx_aggr_lastpkt_ptr;
#if 0
        u8 * rx_aggr_buf;
        u8 * rx_aggr_write_ptr;
        u8 * rx_aggr_lastpkt_ptr;
#endif

        spinlock_t rx_lock;
        struct sk_buff_head rxq;
#ifndef RX_SYNC
        struct work_struct rx_process_work;
#endif/* RX_SYNC */

        u16 tx_blksz;
        u16 rx_blksz;

        bool dump_rpbm_err;
        bool sendup_rpbm_pkt;
        bool rxabort_fixed;
        bool support_bgscan;
        u8 credit_to_reserve;
	s16 noise_floor;

        u32 tx_tot_len; /* total len for one transaction */
        u32 rx_tot_len;

        atomic_t rx_handling;
        atomic_t tx_data_pkt_queued;

#ifndef FAST_TX_STATUS
        atomic_t pending_tx_status;
#endif /* !FAST_TX_STATUS */

        atomic_t data_tx_stopped;
        atomic_t tx_stopped;

        struct esp_pub *epub;
};

int sip_rx(struct esp_pub * epub);
//int sip_download_fw(struct esp_sip *sip, u32 load_addr, u32 boot_addr);

/* tx must pad as 4-byte aligned */
int sip_tx(struct esp_pub * epub, struct sip_pkt *pkt);

int sip_get_raw_credits(struct esp_sip *);

int sip_write_memory(struct esp_sip *, u32 addr, u8* buf, u16 len);

void sip_credit_process(struct esp_pub *, u8 credits);

int sip_prepare_boot(struct esp_sip *sip);

int sip_send_cmd(struct esp_sip *sip, int cid, u32 cmdlen, void * cmd);

struct esp_sip * sip_attach(struct esp_pub *);

int sip_post_init(struct esp_sip *sip, struct sip_evt_bootup2 *bevt);

void sip_detach(struct esp_sip *sip);

void sip_txq_process(struct esp_pub *epub);

struct sk_buff * sip_alloc_ctrl_skbuf(struct esp_sip *sip, u16 len, u32 cid);

void sip_free_ctrl_skbuff(struct esp_sip *sip, struct sk_buff* skb);

bool sip_queue_need_stop(struct esp_sip *sip);
bool sip_queue_may_resume(struct esp_sip *sip);
bool sip_tx_data_need_stop(struct esp_sip *sip);
bool sip_tx_data_may_resume(struct esp_sip *sip);

void sip_tx_data_pkt_enqueue(struct esp_pub *epub, struct sk_buff *skb);
void sip_rx_data_pkt_enqueue(struct esp_pub *epub, struct sk_buff *skb);

int sip_cmd_enqueue(struct esp_sip *sip, struct sk_buff *skb);

void sip_dump_pending_data(struct esp_pub *epub);

int sip_poll_bootup_event(struct esp_sip *sip);

int sip_poll_resetting_event(struct esp_sip *sip);

void sip_trigger_txq_process(struct esp_sip *sip);

void sip_send_chip_init(struct esp_sip *sip);

bool mod_support_no_txampdu(void);

bool mod_support_no_rxampdu(void);

void mod_support_no_txampdu_set(bool value);

#ifdef FPGA_DEBUG
int sip_send_bootup(struct esp_sip *sip);
#endif /* FPGA_DEBUG */
void sip_debug_show(struct esp_sip *sip);
#endif
