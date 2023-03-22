// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _AICWF_RX_PREALLOC_H_
#define _AICWF_RX_PREALLOC_H_

#ifdef CONFIG_PREALLOC_RX_SKB

struct rx_buff {
    struct list_head queue;
    unsigned char *data;
    u32 len;
    uint8_t *start;
    uint8_t *end;
    uint8_t *read;
};

struct aicwf_rx_buff_list {
    struct list_head rxbuff_list;
    atomic_t rxbuff_list_len;
};

struct rx_buff *aicwf_prealloc_rxbuff_alloc(spinlock_t *lock);
void aicwf_prealloc_rxbuff_free(struct rx_buff *rxbuff, spinlock_t *lock);
int aicwf_prealloc_init(void);
void aicwf_prealloc_exit(void);
int aicwf_rxbuff_size_get(void);
#endif
#endif /* _AICWF_RX_PREALLOC_H_ */