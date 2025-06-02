/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * irqfd: Allows an fd to be used to inject an interrupt to the guest.
 * ioeventfd: Allow an fd to be used to receive a signal from the guest.
 * All credit goes to kvm developers.
 */

#ifndef __LINUX_MSHV_EVENTFD_H
#define __LINUX_MSHV_EVENTFD_H

#include <linux/poll.h>

#include "mshv.h"
#include "mshv_root.h"

/* struct to contain list of irqfds sharing an irq. Updates are protected by
 * partition.irqfds.resampler_lock
 */
struct mshv_irqfd_resampler {
	struct mshv_partition	    *rsmplr_partn;
	struct hlist_head	     rsmplr_irqfd_list;
	struct mshv_irq_ack_notifier rsmplr_notifier;
	struct hlist_node	     rsmplr_hnode;
};

struct mshv_irqfd {
	struct mshv_partition		    *irqfd_partn;
	struct eventfd_ctx		    *irqfd_eventfd_ctx;
	struct mshv_guest_irq_ent	     irqfd_girq_ent;
	seqcount_spinlock_t		     irqfd_irqe_sc;
	u32				     irqfd_irqnum;
	struct mshv_lapic_irq		     irqfd_lapic_irq;
	struct hlist_node		     irqfd_hnode;
	poll_table			     irqfd_polltbl;
	wait_queue_head_t		    *irqfd_wqh;
	wait_queue_entry_t		     irqfd_wait;
	struct work_struct		     irqfd_shutdown;
	struct mshv_irqfd_resampler	    *irqfd_resampler;
	struct eventfd_ctx		    *irqfd_resamplefd;
	struct hlist_node		     irqfd_resampler_hnode;
};

void mshv_eventfd_init(struct mshv_partition *partition);
void mshv_eventfd_release(struct mshv_partition *partition);

void mshv_register_irq_ack_notifier(struct mshv_partition *partition,
				    struct mshv_irq_ack_notifier *mian);
void mshv_unregister_irq_ack_notifier(struct mshv_partition *partition,
				      struct mshv_irq_ack_notifier *mian);
bool mshv_notify_acked_gsi(struct mshv_partition *partition, int gsi);

int mshv_set_unset_irqfd(struct mshv_partition *partition,
			 struct mshv_user_irqfd *args);

int mshv_irqfd_wq_init(void);
void mshv_irqfd_wq_cleanup(void);

struct mshv_ioeventfd {
	struct hlist_node    iovntfd_hnode;
	u64		     iovntfd_addr;
	int		     iovntfd_length;
	struct eventfd_ctx  *iovntfd_eventfd;
	u64		     iovntfd_datamatch;
	int		     iovntfd_doorbell_id;
	bool		     iovntfd_wildcard;
};

int mshv_set_unset_ioeventfd(struct mshv_partition *pt,
			     struct mshv_user_ioeventfd *args);

#endif /* __LINUX_MSHV_EVENTFD_H */
