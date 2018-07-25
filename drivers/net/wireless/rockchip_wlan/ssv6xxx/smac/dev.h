/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DEV_H_
#define _DEV_H_ 
#include <linux/version.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#ifdef CONFIG_SSV_SUPPORT_ANDROID
#include <linux/wakelock.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#endif
#include <net/mac80211.h>
#include "ampdu.h"
#include "ssv_rc_common.h"
#include "drv_comm.h"
#include "sec.h"
#include "p2p.h"
#include <linux/kthread.h>
#define SSV6200_MAX_HW_MAC_ADDR 2
#define SSV6200_MAX_VIF 2
#define SSV6200_RX_BA_MAX_SESSIONS 1
#define SSV6200_OPMODE_STA 0
#define SSV6200_OPMODE_AP 1
#define SSV6200_OPMODE_IBSS 2
#define SSV6200_OPMODE_WDS 3
#define SSV6200_USE_HW_WSID(_sta_idx) ((_sta_idx == 0) || (_sta_idx == 1))
#define HW_MAX_RATE_TRIES 7
#define MAC_DECITBL1_SIZE 16
#define MAC_DECITBL2_SIZE 9
#define RX_11B_CCA_IN_SCAN 0x20230050
//#define WATCHDOG_TIMEOUT (10*HZ)
#define WATCHDOG_TIMEOUT (99999*HZ)
#ifndef USE_GENERIC_DECI_TBL
extern u16 ap_deci_tbl[];
extern u16 sta_deci_tbl[];
#else
extern u16 generic_deci_tbl[];
#define ap_deci_tbl generic_deci_tbl
#define sta_deci_tbl generic_deci_tbl
#endif
#define HT_SIGNAL_EXT 6
#define HT_SIFS_TIME 10
#define BITS_PER_BYTE 8
#define HT_RC_2_STREAMS(_rc) ((((_rc) & 0x78) >> 3) + 1)
#define ACK_LEN (14)
#define BA_LEN (32)
#define RTS_LEN (20)
#define CTS_LEN (14)
#define L_STF 8
#define L_LTF 8
#define L_SIG 4
#define HT_SIG 8
#define HT_STF 4
#define HT_LTF(_ns) (4 * (_ns))
#define SYMBOL_TIME(_ns) ((_ns) << 2)
#define SYMBOL_TIME_HALFGI(_ns) (((_ns) * 18 + 4) / 5)
#define CCK_SIFS_TIME 10
#define CCK_PREAMBLE_BITS 144
#define CCK_PLCP_BITS 48
#define OFDM_SIFS_TIME 16
#define OFDM_PREAMBLE_TIME 20
#define OFDM_PLCP_BITS 22
#define OFDM_SYMBOL_TIME 4
#define WMM_AC_VO 0
#define WMM_AC_VI 1
#define WMM_AC_BE 2
#define WMM_AC_BK 3
#define WMM_NUM_AC 4
#define WMM_TID_NUM 8
#define TXQ_EDCA_0 0x01
#define TXQ_EDCA_1 0x02
#define TXQ_EDCA_2 0x04
#define TXQ_EDCA_3 0x08
#define TXQ_MGMT 0x10
#define IS_SSV_HT(dsc) ((dsc)->rate_idx >= 15)
#define IS_SSV_SHORT_GI(dsc) ((dsc)->rate_idx>=23 && (dsc)->rate_idx<=30)
#define IS_SSV_HT_GF(dsc) ((dsc)->rate_idx >= 31)
#define IS_SSV_SHORT_PRE(dsc) ((dsc)->rate_idx>=4 && (dsc)->rate_idx<=14)
#define SMAC_REG_WRITE(_s,_r,_v) \
        (_s)->hci.hci_ops->hci_write_word(_r,_v)
#define SMAC_REG_READ(_s,_r,_v) \
        (_s)->hci.hci_ops->hci_read_word(_r, _v)
#define SMAC_LOAD_FW(_s,_r,_v) \
        (_s)->hci.hci_ops->hci_load_fw(_r, _v)
#define SMAC_IFC_RESET(_s) (_s)->hci.hci_ops->hci_interface_reset()
#define SMAC_REG_CONFIRM(_s,_r,_v) \
{ \
    u32 _regval; \
    SMAC_REG_READ(_s, _r, &_regval); \
    if (_regval != (_v)) { \
        printk("ERROR!!Please check interface!\n"); \
        printk("[0x%08x]: 0x%08x!=0x%08x\n", \
        (_r), (_v), _regval); \
        printk("SOS!SOS!\n"); \
        return -1; \
    } \
}
#define SMAC_REG_SET_BITS(_sh,_reg,_set,_clr) \
({ \
    int ret; \
    u32 _regval; \
    ret = SMAC_REG_READ(_sh, _reg, &_regval); \
    _regval &= ~(_clr); \
    _regval |= (_set); \
    if (ret == 0) \
        ret = SMAC_REG_WRITE(_sh, _reg, _regval); \
    ret; \
})
#define HCI_START(_sh) \
    (_sh)->hci.hci_ops->hci_start()
#define HCI_STOP(_sh) \
    (_sh)->hci.hci_ops->hci_stop()
#define HCI_SEND(_sh,_sk,_q) \
    (_sh)->hci.hci_ops->hci_tx(_sk, _q, 0)
#define HCI_PAUSE(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_tx_pause(_mk)
#define HCI_RESUME(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_tx_resume(_mk)
#define HCI_TXQ_FLUSH(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_txq_flush(_mk)
#define HCI_TXQ_FLUSH_BY_STA(_sh,_aid) \
  (_sh)->hci.hci_ops->hci_txq_flush_by_sta(_aid)
#define HCI_TXQ_EMPTY(_sh,_txqid) \
  (_sh)->hci.hci_ops->hci_txq_empty(_txqid)
#define HCI_WAKEUP_PMU(_sh) \
    (_sh)->hci.hci_ops->hci_pmu_wakeup()
#define HCI_SEND_CMD(_sh,_sk) \
        (_sh)->hci.hci_ops->hci_send_cmd(_sk)
#define SSV6XXX_SET_HW_TABLE(sh_,tbl_) \
({ \
    int ret = 0; \
    u32 i=0; \
    for(; i<sizeof(tbl_)/sizeof(struct ssv6xxx_dev_table); i++) { \
        ret = SMAC_REG_WRITE(sh_, tbl_[i].address, tbl_[i].data); \
        if (ret) break; \
    } \
    ret; \
})
#define SSV6XXX_USE_HW_DECRYPT(_priv) (_priv->has_hw_decrypt)
#define SSV6XXX_USE_SW_DECRYPT(_priv) (SSV6XXX_USE_LOCAL_SW_DECRYPT(_priv) || SSV6XXX_USE_MAC80211_DECRYPT(_priv))
#define SSV6XXX_USE_LOCAL_SW_DECRYPT(_priv) (_priv->need_sw_decrypt)
#define SSV6XXX_USE_MAC80211_DECRYPT(_priv) (_priv->use_mac80211_decrypt)
struct ssv_softc;
#ifdef CONFIG_P2P_NOA
struct ssv_p2p_noa;
#endif
#define SSV6200_HT_TX_STREAMS 1
#define SSV6200_HT_RX_STREAMS 1
#define SSV6200_RX_HIGHEST_RATE 72
enum PWRSV_STATUS{
    PWRSV_DISABLE,
    PWRSV_ENABLE,
    PWRSV_PREPARE,
};
#ifdef CONFIG_SSV_RSSI
struct rssi_res_st {
    struct list_head rssi_list;
    unsigned long cache_jiffies;
    s32 rssi;
    s32 timeout;
    u8 bssid[ETH_ALEN];
};
#endif
struct ssv_hw {
    struct ssv_softc *sc;
    struct ssv6xxx_platform_data *priv;
    struct ssv6xxx_hci_info hci;
    char chip_id[24];
    u64 chip_tag;
    u32 tx_desc_len;
    u32 rx_desc_len;
    u32 rx_pinfo_pad;
    u32 tx_page_available;
    u32 ampdu_divider;
    u8 page_count[SSV6200_ID_NUMBER];
#ifdef SSV6200_ECO
    u32 hw_buf_ptr[SSV_RC_MAX_STA];
    u32 hw_sec_key[SSV_RC_MAX_STA];
#else
    u32 hw_buf_ptr;
    u32 hw_sec_key;
#endif
    u32 hw_pinfo;
    struct ssv6xxx_cfg cfg;
    u32 n_addresses;
    struct mac_address maddr[SSV6200_MAX_HW_MAC_ADDR];
#if defined CONFIG_SSV_CABRIO_E
    u8 ipd_channel_touch;
    struct ssv6xxx_ch_cfg *p_ch_cfg;
    u32 ch_cfg_size;
#endif
};
struct ssv_tx {
    u16 seq_no;
    int hw_txqid[WMM_NUM_AC];
    int ac_txqid[WMM_NUM_AC];
    u32 flow_ctrl_status;
    u32 tx_pkt[SSV_HW_TXQ_NUM];
    u32 tx_frag[SSV_HW_TXQ_NUM];
    struct list_head ampdu_tx_que;
    spinlock_t ampdu_tx_que_lock;
    u16 ampdu_tx_group_id;
};
struct ssv_rx {
    struct sk_buff *rx_buf;
    spinlock_t rxq_lock;
    struct sk_buff_head rxq_head;
    u32 rxq_count;
};
#ifdef MULTI_THREAD_ENCRYPT
struct ssv_encrypt_task_list {
    struct task_struct* encrypt_task;
    wait_queue_head_t encrypt_wait_q;
    volatile int started;
    volatile int running;
    volatile int paused;
    volatile int cpu_offline;
    u32 cpu_no;
    struct list_head list;
};
#endif
#define SSV6XXX_GET_STA_INFO(_sc,_s) \
    &(_sc)->sta_info[((struct ssv_sta_priv_data *)((_s)->drv_priv))->sta_idx]
#define STA_FLAG_VALID 0x00001
#define STA_FLAG_QOS 0x00002
#define STA_FLAG_AMPDU 0x00004
#define STA_FLAG_ENCRYPT 0x00008
struct ssv_sta_info {
    u16 aid;
    u16 s_flags;
    int hw_wsid;
    struct ieee80211_sta *sta;
    struct ieee80211_vif *vif;
    bool sleeping;
    bool tim_set;
    #if 0
    struct ssv_crypto_ops *crypt;
    void *crypt_priv;
    u32 KeySelect;
    bool ampdu_ccmp_encrypt;
    #endif
    #ifdef CONFIG_SSV6XXX_DEBUGFS
    struct dentry *debugfs_dir;
 #endif
};
struct ssv_vif_info {
    struct ieee80211_vif *vif;
    struct ssv_vif_priv_data *vif_priv;
    enum nl80211_iftype if_type;
    struct ssv6xxx_hw_sec sramKey;
    #ifdef CONFIG_SSV6XXX_DEBUGFS
    struct dentry *debugfs_dir;
    #endif
};
struct ssv_sta_priv_data {
    int sta_idx;
    int rc_idx;
    int rx_data_rate;
    struct ssv_sta_info *sta_info;
    struct list_head list;
    u32 ampdu_mib_total_BA_counter;
    AMPDU_TID ampdu_tid[WMM_TID_NUM];
    bool has_hw_encrypt;
    bool need_sw_encrypt;
    bool has_hw_decrypt;
    bool need_sw_decrypt;
    bool use_mac80211_decrypt;
    u8 group_key_idx;
    #ifdef USE_LOCAL_CRYPTO
    struct ssv_crypto_data crypto_data;
    #endif
    u32 beacon_rssi;
};
struct ssv_vif_priv_data {
    int vif_idx;
    struct list_head sta_list;
    u32 sta_asleep_mask;
    u32 pair_cipher;
    u32 group_cipher;
    bool is_security_valid;
    bool has_hw_encrypt;
    bool need_sw_encrypt;
    bool has_hw_decrypt;
    bool need_sw_decrypt;
    bool use_mac80211_decrypt;
    bool force_sw_encrypt;
    u8 group_key_idx;
    #ifdef USE_LOCAL_CRYPTO
    struct ssv_crypto_data crypto_data;
    #endif
};
#define SC_OP_INVALID 0x00000001
#define SC_OP_HW_RESET 0x00000002
#define SC_OP_OFFCHAN 0x00000004
#define SC_OP_FIXED_RATE 0x00000008
#define SC_OP_SHORT_PREAMBLE 0x00000010
struct ssv6xxx_beacon_info {
 u32 pubf_addr;
 u16 len;
 u8 tim_offset;
 u8 tim_cnt;
};
#define SSV6200_MAX_BCAST_QUEUE_LEN 16
struct ssv6xxx_bcast_txq {
    spinlock_t txq_lock;
    struct sk_buff_head qhead;
    int cur_qsize;
};
#ifdef DEBUG_AMPDU_FLUSH
typedef struct AMPDU_TID_st AMPDU_TID;
#define MAX_TID (24)
#endif
struct ssv_softc {
    struct ieee80211_hw *hw;
    struct device *dev;
    u32 restart_counter;
    bool force_triger_reset;
    unsigned long sdio_throughput_timestamp;
    unsigned long sdio_rx_evt_size;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
    struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
#else
    struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
#endif
    struct ieee80211_channel *cur_channel;
    u16 hw_chan;
    struct mutex mutex;
    struct ssv_hw *sh;
    struct ssv_tx tx;
    struct ssv_rx rx;
    struct ssv_vif_info vif_info[SSV_NUM_VIF];
    struct ssv_sta_info sta_info[SSV_NUM_STA];
    struct ieee80211_vif *ap_vif;
    u8 nvif;
    u32 sc_flags;
    void *rc;
    int max_rate_idx;
    struct workqueue_struct *rc_sample_workqueue;
    struct sk_buff_head rc_report_queue;
    struct work_struct rc_sample_work;
    #ifdef DEBUG_AMPDU_FLUSH
    struct AMPDU_TID_st *tid[MAX_TID];
    #endif
    u16 rc_sample_sechedule;
    u16 *mac_deci_tbl;
    struct workqueue_struct *config_wq;
    bool bq4_dtim;
    struct work_struct set_tim_work;
    u8 enable_beacon;
    u8 beacon_interval;
    u8 beacon_dtim_cnt;
    u8 beacon_usage;
    struct ssv6xxx_beacon_info beacon_info[2];
    struct sk_buff *beacon_buf;
    struct work_struct bcast_start_work;
    struct delayed_work bcast_stop_work;
    struct delayed_work bcast_tx_work;
    struct delayed_work thermal_monitor_work;
    struct workqueue_struct *thermal_wq;
    int is_sar_enabled;
    bool aid0_bit_set;
    u8 hw_mng_used;
    struct ssv6xxx_bcast_txq bcast_txq;
    int bcast_interval;
    u8 bssid[6];
    struct mutex mem_mutex;
    spinlock_t ps_state_lock;
    u8 hw_wsid_bit;
    #if 0
    struct work_struct ampdu_tx_encry_work;
    bool ampdu_encry_work_scheduled;
    bool ampdu_ccmp_encrypt;
    struct work_struct sync_hwkey_work;
    bool sync_hwkey_write;
    struct ssv_sta_info *key_sync_sta_info;
    AMPDU_REKEY_PAUSE_STATE ampdu_rekey_pause;
    #endif
    int rx_ba_session_count;
    struct ieee80211_sta *rx_ba_sta;
    u8 rx_ba_bitmap;
    u8 ba_ra_addr[ETH_ALEN];
    u16 ba_tid;
    u16 ba_ssn;
    struct work_struct set_ampdu_rx_add_work;
 struct work_struct set_ampdu_rx_del_work;
    bool isAssoc;
    u16 channel_center_freq;
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
    bool bScanning;
//#endif
    int ps_status;
    u16 ps_aid;
#ifdef CONFIG_SSV_SUPPORT_ANDROID
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend early_suspend;
#endif
#ifdef CONFIG_HAS_WAKELOCK
    struct wake_lock ssv_wake_lock_;
#endif
#endif
    u16 tx_wait_q_woken;
    wait_queue_head_t tx_wait_q;
    struct sk_buff_head tx_skb_q;
#ifdef CONFIG_SSV6XXX_DEBUGFS
    u32 max_tx_skb_q_len;
#endif
    struct task_struct *tx_task;
    bool tx_q_empty;
    struct sk_buff_head tx_done_q;
    u16 rx_wait_q_woken;
    wait_queue_head_t rx_wait_q;
    struct sk_buff_head rx_skb_q;
    struct task_struct *rx_task;
#ifdef MULTI_THREAD_ENCRYPT
    struct list_head encrypt_task_head;
    struct notifier_block cpu_nfb;
    struct sk_buff_head preprocess_q;
    struct sk_buff_head crypted_q;
    spinlock_t crypt_st_lock;
#ifdef CONFIG_SSV6XXX_DEBUGFS
    u32 max_preprocess_q_len;
    u32 max_crypted_q_len;
#endif
#endif
    bool dbg_rx_frame;
    bool dbg_tx_frame;
#ifdef CONFIG_SSV6XXX_DEBUGFS
    struct dentry *debugfs_dir;
#endif
#ifdef CONFIG_P2P_NOA
    struct ssv_p2p_noa p2p_noa;
#endif
    struct timer_list watchdog_timeout;
    u32 watchdog_flag;
    wait_queue_head_t fw_wait_q;
    u32 iq_cali_done;
    u32 sr_bhvr;
};
enum {
    IQ_CALI_RUNNING,
    IQ_CALI_OK,
    IQ_CALI_FAILED
};
enum {
    WD_SLEEP,
    WD_BARKING,
    WD_KICKED,
    WD_MAX
};
void ssv6xxx_txbuf_free_skb(struct sk_buff *skb , void *args);
void ssv6200_rx_process(struct work_struct *work);
#if !defined(USE_THREAD_RX) || defined(USE_BATCH_RX)
int ssv6200_rx(struct sk_buff_head *rx_skb_q, void *args);
#else
int ssv6200_rx(struct sk_buff *rx_skb, void *args);
#endif
void ssv6xxx_tx_cb(struct sk_buff_head *skb_head, void *args);
#ifdef RATE_CONTROL_REALTIME_UPDATA
void ssv6xxx_tx_rate_update(struct sk_buff *skb, void *args);
#endif
int ssv6200_tx_flow_control(void *dev, int hw_txqid, bool fc_en, int debug);
void ssv6xxx_tx_q_empty_cb (u32 txq_no, void *);
int ssv6xxx_rf_disable(struct ssv_hw *sh);
int ssv6xxx_rf_enable(struct ssv_hw *sh);
int ssv6xxx_set_channel(struct ssv_softc *sc, int ch);
#ifdef CONFIG_SSV_SMARTLINK
int ssv6xxx_get_channel(struct ssv_softc *sc, int *pch);
int ssv6xxx_set_promisc(struct ssv_softc *sc, int accept);
int ssv6xxx_get_promisc(struct ssv_softc *sc, int *paccept);
#endif
int ssv6xxx_tx_task (void *data);
int ssv6xxx_rx_task (void *data);
u32 ssv6xxx_pbuf_alloc(struct ssv_softc *sc, int size, int type);
bool ssv6xxx_pbuf_free(struct ssv_softc *sc, u32 pbuf_addr);
void ssv6xxx_add_txinfo(struct ssv_softc *sc, struct sk_buff *skb);
void ssv6xxx_update_txinfo (struct ssv_softc *sc, struct sk_buff *skb);
int ssv6xxx_update_decision_table(struct ssv_softc *sc);
void ssv6xxx_ps_callback_func(unsigned long data);
void ssv6xxx_enable_ps(struct ssv_softc *sc);
void ssv6xxx_disable_ps(struct ssv_softc *sc);
int ssv6xxx_watchdog_controller(struct ssv_hw *sh ,u8 flag);
int ssv6xxx_skb_encrypt(struct sk_buff *mpdu,struct ssv_softc *sc);
int ssv6xxx_skb_decrypt(struct sk_buff *mpdu, struct ieee80211_sta *sta,struct ssv_softc *sc);
void ssv6200_sync_hw_key_sequence(struct ssv_softc *sc, struct ssv_sta_info* sta_info, bool bWrite);
struct ieee80211_sta *ssv6xxx_find_sta_by_rx_skb (struct ssv_softc *sc, struct sk_buff *skb);
struct ieee80211_sta *ssv6xxx_find_sta_by_addr (struct ssv_softc *sc, u8 addr[6]);
void ssv6xxx_foreach_sta (struct ssv_softc *sc,
                          void (*sta_func)(struct ssv_softc *,
                                           struct ssv_sta_info *,
                                           void *),
                          void *param);
void ssv6xxx_foreach_vif_sta (struct ssv_softc *sc,
                              struct ssv_vif_info *vif_info,
                              void (*sta_func)(struct ssv_softc *,
                                               struct ssv_vif_info *,
                                               struct ssv_sta_info *,
                                               void *),
                              void *param);
#ifdef CONFIG_SSV_SUPPORT_ANDROID
#ifdef CONFIG_HAS_EARLYSUSPEND
void ssv6xxx_early_suspend(struct early_suspend *h);
void ssv6xxx_late_resume(struct early_suspend *h);
#endif
#endif
#ifdef USE_LOCAL_CRYPTO
#ifdef MULTI_THREAD_ENCRYPT
struct ssv_crypto_data *ssv6xxx_skb_get_tx_cryptops(struct sk_buff *mpdu);
struct ssv_crypto_data *ssv6xxx_skb_get_rx_cryptops(struct ssv_softc *sc,
                                                    struct ieee80211_sta *sta,
                                                    struct sk_buff *mpdu);
int ssv6xxx_skb_pre_encrypt(struct sk_buff *mpdu, struct ssv_softc *sc);
int ssv6xxx_skb_pre_decrypt(struct sk_buff *mpdu, struct ieee80211_sta *sta, struct ssv_softc *sc);
int ssv6xxx_encrypt_task (void *data);
#endif
#ifdef HAS_CRYPTO_LOCK
    #define INIT_WRITE_CRYPTO_DATA(data, init) \
        struct ssv_crypto_data *data = (init); \
        unsigned long data##_flags;
    #define START_WRITE_CRYPTO_DATA(data) \
        do { \
            write_lock_irqsave(&(data)->lock, data##_flags); \
        } while (0)
    #define END_WRITE_CRYPTO_DATA(data) \
        do { \
            write_unlock_irqrestore(&(data)->lock, data##_flags); \
        } while (0)
    #define START_READ_CRYPTO_DATA(data) \
        do { \
            read_lock(&(data)->lock); \
        } while (0)
    #define END_READ_CRYPTO_DATA(data) \
        do { \
            read_unlock(&(data)->lock); \
        } while (0)
#else
    #define INIT_WRITE_CRYPTO_DATA(data, init) \
        struct ssv_crypto_data *data = (init);
    #define START_WRITE_CRYPTO_DATA(data) do { } while (0)
    #define END_WRITE_CRYPTO_DATA(data) do { } while (0)
    #define START_READ_CRYPTO_DATA(data) do { } while (0)
    #define END_READ_CRYPTO_DATA(data) do { } while (0)
#endif
#endif
#ifdef CONFIG_SSV6XXX_DEBUGFS
ssize_t ssv6xxx_tx_queue_status_dump (struct ssv_softc *sc, char *status_buf,
                                      ssize_t buf_size);
#endif
#endif
