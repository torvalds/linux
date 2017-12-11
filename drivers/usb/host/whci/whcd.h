// SPDX-License-Identifier: GPL-2.0
/*
 * Wireless Host Controller (WHC) private header.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 */
#ifndef __WHCD_H
#define __WHCD_H

#include <linux/uwb/whci.h>
#include <linux/uwb/umc.h>
#include <linux/workqueue.h>

#include "whci-hc.h"

/* Generic command timeout. */
#define WHC_GENCMD_TIMEOUT_MS 100

struct whc_dbg;

struct whc {
	struct wusbhc wusbhc;
	struct umc_dev *umc;

	resource_size_t base_phys;
	void __iomem *base;
	int irq;

	u8 n_devices;
	u8 n_keys;
	u8 n_mmc_ies;

	u64 *pz_list;
	struct dn_buf_entry *dn_buf;
	struct di_buf_entry *di_buf;
	dma_addr_t pz_list_dma;
	dma_addr_t dn_buf_dma;
	dma_addr_t di_buf_dma;

	spinlock_t   lock;
	struct mutex mutex;

	void *            gen_cmd_buf;
	dma_addr_t        gen_cmd_buf_dma;
	wait_queue_head_t cmd_wq;

	struct workqueue_struct *workqueue;
	struct work_struct       dn_work;

	struct dma_pool *qset_pool;

	struct list_head async_list;
	struct list_head async_removed_list;
	wait_queue_head_t async_list_wq;
	struct work_struct async_work;

	struct list_head periodic_list[5];
	struct list_head periodic_removed_list;
	wait_queue_head_t periodic_list_wq;
	struct work_struct periodic_work;

	struct whc_dbg *dbg;
};

#define wusbhc_to_whc(w) (container_of((w), struct whc, wusbhc))

/**
 * struct whc_std - a software TD.
 * @urb: the URB this sTD is for.
 * @offset: start of the URB's data for this TD.
 * @len: the length of data in the associated TD.
 * @ntds_remaining: number of TDs (starting from this one) in this transfer.
 *
 * @bounce_buf: a bounce buffer if the std was from an urb with a sg
 * list that could not be mapped to qTDs directly.
 * @bounce_sg: the first scatterlist element bounce_buf is for.
 * @bounce_offset: the offset into bounce_sg for the start of bounce_buf.
 *
 * Queued URBs may require more TDs than are available in a qset so we
 * use a list of these "software TDs" (sTDs) to hold per-TD data.
 */
struct whc_std {
	struct urb *urb;
	size_t len;
	int    ntds_remaining;
	struct whc_qtd *qtd;

	struct list_head list_node;
	int num_pointers;
	dma_addr_t dma_addr;
	struct whc_page_list_entry *pl_virt;

	void *bounce_buf;
	struct scatterlist *bounce_sg;
	unsigned bounce_offset;
};

/**
 * struct whc_urb - per URB host controller structure.
 * @urb: the URB this struct is for.
 * @qset: the qset associated to the URB.
 * @dequeue_work: the work to remove the URB when dequeued.
 * @is_async: the URB belongs to async sheduler or not.
 * @status: the status to be returned when calling wusbhc_giveback_urb.
 */
struct whc_urb {
	struct urb *urb;
	struct whc_qset *qset;
	struct work_struct dequeue_work;
	bool is_async;
	int status;
};

/**
 * whc_std_last - is this sTD the URB's last?
 * @std: the sTD to check.
 */
static inline bool whc_std_last(struct whc_std *std)
{
	return std->ntds_remaining <= 1;
}

enum whc_update {
	WHC_UPDATE_ADDED   = 0x01,
	WHC_UPDATE_REMOVED = 0x02,
	WHC_UPDATE_UPDATED = 0x04,
};

/* init.c */
int whc_init(struct whc *whc);
void whc_clean_up(struct whc *whc);

/* hw.c */
void whc_write_wusbcmd(struct whc *whc, u32 mask, u32 val);
int whc_do_gencmd(struct whc *whc, u32 cmd, u32 params, void *addr, size_t len);
void whc_hw_error(struct whc *whc, const char *reason);

/* wusb.c */
int whc_wusbhc_start(struct wusbhc *wusbhc);
void whc_wusbhc_stop(struct wusbhc *wusbhc, int delay);
int whc_mmcie_add(struct wusbhc *wusbhc, u8 interval, u8 repeat_cnt,
		  u8 handle, struct wuie_hdr *wuie);
int whc_mmcie_rm(struct wusbhc *wusbhc, u8 handle);
int whc_bwa_set(struct wusbhc *wusbhc, s8 stream_index, const struct uwb_mas_bm *mas_bm);
int whc_dev_info_set(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev);
int whc_set_num_dnts(struct wusbhc *wusbhc, u8 interval, u8 slots);
int whc_set_ptk(struct wusbhc *wusbhc, u8 port_idx, u32 tkid,
		const void *ptk, size_t key_size);
int whc_set_gtk(struct wusbhc *wusbhc, u32 tkid,
		const void *gtk, size_t key_size);
int whc_set_cluster_id(struct whc *whc, u8 bcid);

/* int.c */
irqreturn_t whc_int_handler(struct usb_hcd *hcd);
void whc_dn_work(struct work_struct *work);

/* asl.c */
void asl_start(struct whc *whc);
void asl_stop(struct whc *whc);
int  asl_init(struct whc *whc);
void asl_clean_up(struct whc *whc);
int  asl_urb_enqueue(struct whc *whc, struct urb *urb, gfp_t mem_flags);
int  asl_urb_dequeue(struct whc *whc, struct urb *urb, int status);
void asl_qset_delete(struct whc *whc, struct whc_qset *qset);
void scan_async_work(struct work_struct *work);

/* pzl.c */
int  pzl_init(struct whc *whc);
void pzl_clean_up(struct whc *whc);
void pzl_start(struct whc *whc);
void pzl_stop(struct whc *whc);
int  pzl_urb_enqueue(struct whc *whc, struct urb *urb, gfp_t mem_flags);
int  pzl_urb_dequeue(struct whc *whc, struct urb *urb, int status);
void pzl_qset_delete(struct whc *whc, struct whc_qset *qset);
void scan_periodic_work(struct work_struct *work);

/* qset.c */
struct whc_qset *qset_alloc(struct whc *whc, gfp_t mem_flags);
void qset_free(struct whc *whc, struct whc_qset *qset);
struct whc_qset *get_qset(struct whc *whc, struct urb *urb, gfp_t mem_flags);
void qset_delete(struct whc *whc, struct whc_qset *qset);
void qset_clear(struct whc *whc, struct whc_qset *qset);
void qset_reset(struct whc *whc, struct whc_qset *qset);
int qset_add_urb(struct whc *whc, struct whc_qset *qset, struct urb *urb,
		 gfp_t mem_flags);
void qset_free_std(struct whc *whc, struct whc_std *std);
void qset_remove_urb(struct whc *whc, struct whc_qset *qset,
			    struct urb *urb, int status);
void process_halted_qtd(struct whc *whc, struct whc_qset *qset,
			       struct whc_qtd *qtd);
void process_inactive_qtd(struct whc *whc, struct whc_qset *qset,
				 struct whc_qtd *qtd);
enum whc_update qset_add_qtds(struct whc *whc, struct whc_qset *qset);
void qset_remove_complete(struct whc *whc, struct whc_qset *qset);
void pzl_update(struct whc *whc, uint32_t wusbcmd);
void asl_update(struct whc *whc, uint32_t wusbcmd);

/* debug.c */
void whc_dbg_init(struct whc *whc);
void whc_dbg_clean_up(struct whc *whc);

#endif /* #ifndef __WHCD_H */
