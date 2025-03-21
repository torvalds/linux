/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_CE_H
#define ATH12K_CE_H

#define CE_COUNT_MAX 16

/* Byte swap data words */
#define CE_ATTR_BYTE_SWAP_DATA 2

/* no interrupt on copy completion */
#define CE_ATTR_DIS_INTR		8

/* Host software's Copy Engine configuration. */
#define CE_ATTR_FLAGS 0

/* Threshold to poll for tx completion in case of Interrupt disabled CE's */
#define ATH12K_CE_USAGE_THRESHOLD 32

/* Directions for interconnect pipe configuration.
 * These definitions may be used during configuration and are shared
 * between Host and Target.
 *
 * Pipe Directions are relative to the Host, so PIPEDIR_IN means
 * "coming IN over air through Target to Host" as with a WiFi Rx operation.
 * Conversely, PIPEDIR_OUT means "going OUT from Host through Target over air"
 * as with a WiFi Tx operation. This is somewhat awkward for the "middle-man"
 * Target since things that are "PIPEDIR_OUT" are coming IN to the Target
 * over the interconnect.
 */
#define PIPEDIR_NONE		0
#define PIPEDIR_IN		1 /* Target-->Host, WiFi Rx direction */
#define PIPEDIR_OUT		2 /* Host->Target, WiFi Tx direction */
#define PIPEDIR_INOUT		3 /* bidirectional */
#define PIPEDIR_INOUT_H2H	4 /* bidirectional, host to host */

/* CE address/mask */
#define CE_HOST_IE_ADDRESS	0x75804C
#define CE_HOST_IE_2_ADDRESS	0x758050
#define CE_HOST_IE_3_ADDRESS	CE_HOST_IE_ADDRESS

#define CE_HOST_IE_3_SHIFT	0xC

#define CE_RING_IDX_INCR(nentries_mask, idx) (((idx) + 1) & (nentries_mask))

#define ATH12K_CE_RX_POST_RETRY_JIFFIES 50

struct ath12k_base;

/* Establish a mapping between a service/direction and a pipe.
 * Configuration information for a Copy Engine pipe and services.
 * Passed from Host to Target through QMI message and must be in
 * little endian format.
 */
struct service_to_pipe {
	__le32 service_id;
	__le32 pipedir;
	__le32 pipenum;
};

/* Configuration information for a Copy Engine pipe.
 * Passed from Host to Target through QMI message during startup (one per CE).
 *
 * NOTE: Structure is shared between Host software and Target firmware!
 */
struct ce_pipe_config {
	__le32 pipenum;
	__le32 pipedir;
	__le32 nentries;
	__le32 nbytes_max;
	__le32 flags;
	__le32 reserved;
};

struct ce_attr {
	/* CE_ATTR_* values */
	unsigned int flags;

	/* #entries in source ring - Must be a power of 2 */
	unsigned int src_nentries;

	/* Max source send size for this CE.
	 * This is also the minimum size of a destination buffer.
	 */
	unsigned int src_sz_max;

	/* #entries in destination ring - Must be a power of 2 */
	unsigned int dest_nentries;

	void (*recv_cb)(struct ath12k_base *ab, struct sk_buff *skb);
};

#define CE_DESC_RING_ALIGN 8

struct ath12k_ce_ring {
	/* Number of entries in this ring; must be power of 2 */
	unsigned int nentries;
	unsigned int nentries_mask;

	/* For dest ring, this is the next index to be processed
	 * by software after it was/is received into.
	 *
	 * For src ring, this is the last descriptor that was sent
	 * and completion processed by software.
	 *
	 * Regardless of src or dest ring, this is an invariant
	 * (modulo ring size):
	 *     write index >= read index >= sw_index
	 */
	unsigned int sw_index;
	/* cached copy */
	unsigned int write_index;

	/* Start of DMA-coherent area reserved for descriptors */
	/* Host address space */
	void *base_addr_owner_space_unaligned;
	/* CE address space */
	dma_addr_t base_addr_ce_space_unaligned;

	/* Actual start of descriptors.
	 * Aligned to descriptor-size boundary.
	 * Points into reserved DMA-coherent area, above.
	 */
	/* Host address space */
	void *base_addr_owner_space;

	/* CE address space */
	dma_addr_t base_addr_ce_space;

	/* HAL ring id */
	u32 hal_ring_id;

	/* keep last */
	struct sk_buff *skb[];
};

struct ath12k_ce_pipe {
	struct ath12k_base *ab;
	u16 pipe_num;
	unsigned int attr_flags;
	unsigned int buf_sz;
	unsigned int rx_buf_needed;

	void (*send_cb)(struct ath12k_ce_pipe *pipe);
	void (*recv_cb)(struct ath12k_base *ab, struct sk_buff *skb);

	struct work_struct intr_wq;
	struct ath12k_ce_ring *src_ring;
	struct ath12k_ce_ring *dest_ring;
	struct ath12k_ce_ring *status_ring;
	u64 timestamp;
};

struct ath12k_ce {
	struct ath12k_ce_pipe ce_pipe[CE_COUNT_MAX];
	/* Protects rings of all ce pipes */
	spinlock_t ce_lock;
	struct ath12k_hp_update_timer hp_timer[CE_COUNT_MAX];
};

extern const struct ce_attr ath12k_host_ce_config_qcn9274[];
extern const struct ce_attr ath12k_host_ce_config_wcn7850[];

void ath12k_ce_cleanup_pipes(struct ath12k_base *ab);
void ath12k_ce_rx_replenish_retry(struct timer_list *t);
void ath12k_ce_per_engine_service(struct ath12k_base *ab, u16 ce_id);
int ath12k_ce_send(struct ath12k_base *ab, struct sk_buff *skb, u8 pipe_id,
		   u16 transfer_id);
void ath12k_ce_rx_post_buf(struct ath12k_base *ab);
int ath12k_ce_init_pipes(struct ath12k_base *ab);
int ath12k_ce_alloc_pipes(struct ath12k_base *ab);
void ath12k_ce_free_pipes(struct ath12k_base *ab);
int ath12k_ce_get_attr_flags(struct ath12k_base *ab, int ce_id);
void ath12k_ce_poll_send_completed(struct ath12k_base *ab, u8 pipe_id);
void ath12k_ce_get_shadow_config(struct ath12k_base *ab,
				 u32 **shadow_cfg, u32 *shadow_cfg_len);
#endif
