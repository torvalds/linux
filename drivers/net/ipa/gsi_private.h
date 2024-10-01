/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
 */
#ifndef _GSI_PRIVATE_H_
#define _GSI_PRIVATE_H_

/* === Only "gsi.c" and "gsi_trans.c" should include this file === */

#include <linux/types.h>

struct gsi_trans;
struct gsi_ring;
struct gsi_channel;

#define GSI_RING_ELEMENT_SIZE	16	/* bytes; must be a power of 2 */

/**
 * gsi_trans_move_complete() - Mark a GSI transaction completed
 * @trans:	Transaction whose state is to be updated
 */
void gsi_trans_move_complete(struct gsi_trans *trans);

/**
 * gsi_trans_move_polled() - Mark a transaction polled
 * @trans:	Transaction whose state is to be updated
 */
void gsi_trans_move_polled(struct gsi_trans *trans);

/**
 * gsi_trans_complete() - Complete a GSI transaction
 * @trans:	Transaction to complete
 *
 * Marks a transaction complete (including freeing it).
 */
void gsi_trans_complete(struct gsi_trans *trans);

/**
 * gsi_channel_trans_mapped() - Return a transaction mapped to a TRE index
 * @channel:	Channel associated with the transaction
 * @index:	Index of the TRE having a transaction
 *
 * Return:	The GSI transaction pointer associated with the TRE index
 */
struct gsi_trans *gsi_channel_trans_mapped(struct gsi_channel *channel,
					   u32 index);

/**
 * gsi_channel_trans_complete() - Return a channel's next completed transaction
 * @channel:	Channel whose next transaction is to be returned
 *
 * Return:	The next completed transaction, or NULL if nothing new
 */
struct gsi_trans *gsi_channel_trans_complete(struct gsi_channel *channel);

/**
 * gsi_channel_trans_cancel_pending() - Cancel pending transactions
 * @channel:	Channel whose pending transactions should be cancelled
 *
 * Cancel all pending transactions on a channel.  These are transactions
 * that have been committed but not yet completed.  This is required when
 * the channel gets reset.  At that time all pending transactions will be
 * marked as cancelled.
 *
 * NOTE:  Transactions already complete at the time of this call are
 *	  unaffected.
 */
void gsi_channel_trans_cancel_pending(struct gsi_channel *channel);

/**
 * gsi_channel_trans_init() - Initialize a channel's GSI transaction info
 * @gsi:	GSI pointer
 * @channel_id:	Channel number
 *
 * Return:	0 if successful, or -ENOMEM on allocation failure
 *
 * Creates and sets up information for managing transactions on a channel
 */
int gsi_channel_trans_init(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_trans_exit() - Inverse of gsi_channel_trans_init()
 * @channel:	Channel whose transaction information is to be cleaned up
 */
void gsi_channel_trans_exit(struct gsi_channel *channel);

/**
 * gsi_channel_doorbell() - Ring a channel's doorbell
 * @channel:	Channel whose doorbell should be rung
 *
 * Rings a channel's doorbell to inform the GSI hardware that new
 * transactions (TREs, really) are available for it to process.
 */
void gsi_channel_doorbell(struct gsi_channel *channel);

/* gsi_channel_update() - Update knowledge of channel hardware state
 * @channel:	Channel to be updated
 *
 * Consult hardware, change the state of any newly-completed transactions
 * on a channel.
 */
void gsi_channel_update(struct gsi_channel *channel);

/**
 * gsi_ring_virt() - Return virtual address for a ring entry
 * @ring:	Ring whose address is to be translated
 * @index:	Index (slot number) of entry
 */
void *gsi_ring_virt(struct gsi_ring *ring, u32 index);

/**
 * gsi_trans_tx_committed() - Record bytes committed for transmit
 * @trans:	TX endpoint transaction being committed
 *
 * Report that a TX transaction has been committed.  It updates some
 * statistics used to manage transmit rates.
 */
void gsi_trans_tx_committed(struct gsi_trans *trans);

/**
 * gsi_trans_tx_queued() - Report a queued TX channel transaction
 * @trans:	Transaction being passed to hardware
 *
 * Report to the network stack that a TX transaction is being supplied
 * to the hardware.
 */
void gsi_trans_tx_queued(struct gsi_trans *trans);

#endif /* _GSI_PRIVATE_H_ */
