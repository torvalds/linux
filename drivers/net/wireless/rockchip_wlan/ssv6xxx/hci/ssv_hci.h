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

#ifndef _SSV_HCI_H_
#define _SSV_HCI_H_ 
#define SSV_HW_TXQ_NUM 5
#define SSV_HW_TXQ_MAX_SIZE 64
#define SSV_HW_TXQ_RESUME_THRES ((SSV_HW_TXQ_MAX_SIZE >> 2) *3)
#define HCI_FLAGS_ENQUEUE_HEAD 0x00000001
#define HCI_FLAGS_NO_FLOWCTRL 0x00000002
struct ssv_hw_txq {
    u32 txq_no;
    struct sk_buff_head qhead;
    int max_qsize;
    int resum_thres;
    bool paused;
    u32 tx_pkt;
    u32 tx_flags;
};
struct ssv6xxx_hci_ops {
    int (*hci_start)(void);
    int (*hci_stop)(void);
    int (*hci_read_word)(u32 addr, u32 *regval);
    int (*hci_write_word)(u32 addr, u32 regval);
    int (*hci_load_fw)(u8 *firmware_name, u8 openfile);
    int (*hci_tx)(struct sk_buff *, int, u32);
#if 0
    int (*hci_rx)(struct sk_buff *);
#endif
    int (*hci_tx_pause)(u32 txq_mask);
    int (*hci_tx_resume)(u32 txq_mask);
    int (*hci_txq_flush)(u32 txq_mask);
    int (*hci_txq_flush_by_sta)(int aid);
    bool (*hci_txq_empty)(int txqid);
    int (*hci_pmu_wakeup)(void);
    int (*hci_send_cmd)(struct sk_buff *);
#ifdef CONFIG_SSV6XXX_DEBUGFS
    bool (*hci_init_debugfs)(struct dentry *dev_deugfs_dir);
    void (*hci_deinit_debugfs)(void);
#endif
    int (*hci_write_sram)(u32 addr, u8* data, u32 size);
    int (*hci_interface_reset)(void);
};
struct ssv6xxx_hci_info {
    struct device *dev;
    struct ssv6xxx_hwif_ops *if_ops;
    struct ssv6xxx_hci_ops *hci_ops;
    #if !defined(USE_THREAD_RX) || defined(USE_BATCH_RX)
    int (*hci_rx_cb)(struct sk_buff_head *, void *);
    #else
    int (*hci_rx_cb)(struct sk_buff *, void *);
    #endif
    void *rx_cb_args;
    void (*hci_tx_cb)(struct sk_buff_head *, void *);
    void *tx_cb_args;
    int (*hci_tx_flow_ctrl_cb)(void *, int, bool, int debug);
    void *tx_fctrl_cb_args;
    void (*hci_tx_buf_free_cb)(struct sk_buff *, void *);
    void *tx_buf_free_args;
    void (*hci_skb_update_cb)(struct sk_buff *, void *);
    void *skb_update_args;
    void (*hci_tx_q_empty_cb)(u32 txq_no, void *);
    void *tx_q_empty_args;
};
int ssv6xxx_hci_deregister(void);
int ssv6xxx_hci_register(struct ssv6xxx_hci_info *);
#endif
