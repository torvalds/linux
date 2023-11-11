// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef BRCMFMAC_COMMONRING_H
#define BRCMFMAC_COMMONRING_H


struct brcmf_commonring {
	u16 r_ptr;
	u16 w_ptr;
	u16 f_ptr;
	u16 depth;
	u16 item_len;

	void *buf_addr;

	int (*cr_ring_bell)(void *ctx);
	int (*cr_update_rptr)(void *ctx);
	int (*cr_update_wptr)(void *ctx);
	int (*cr_write_rptr)(void *ctx);
	int (*cr_write_wptr)(void *ctx);

	void *cr_ctx;

	spinlock_t lock;
	unsigned long flags;
	bool inited;
	bool was_full;

	atomic_t outstanding_tx;
};


void brcmf_commonring_register_cb(struct brcmf_commonring *commonring,
				  int (*cr_ring_bell)(void *ctx),
				  int (*cr_update_rptr)(void *ctx),
				  int (*cr_update_wptr)(void *ctx),
				  int (*cr_write_rptr)(void *ctx),
				  int (*cr_write_wptr)(void *ctx), void *ctx);
void brcmf_commonring_config(struct brcmf_commonring *commonring, u16 depth,
			     u16 item_len, void *buf_addr);
void brcmf_commonring_lock(struct brcmf_commonring *commonring);
void brcmf_commonring_unlock(struct brcmf_commonring *commonring);
bool brcmf_commonring_write_available(struct brcmf_commonring *commonring);
void *brcmf_commonring_reserve_for_write(struct brcmf_commonring *commonring);
void *
brcmf_commonring_reserve_for_write_multiple(struct brcmf_commonring *commonring,
					    u16 n_items, u16 *alloced);
int brcmf_commonring_write_complete(struct brcmf_commonring *commonring);
void brcmf_commonring_write_cancel(struct brcmf_commonring *commonring,
				   u16 n_items);
void *brcmf_commonring_get_read_ptr(struct brcmf_commonring *commonring,
				    u16 *n_items);
int brcmf_commonring_read_complete(struct brcmf_commonring *commonring,
				   u16 n_items);

#define brcmf_commonring_n_items(commonring) (commonring->depth)
#define brcmf_commonring_len_item(commonring) (commonring->item_len)


#endif /* BRCMFMAC_COMMONRING_H */
