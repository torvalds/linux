/*
 * omap-mbox.h: OMAP mailbox internal definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OMAP_MBOX_H
#define OMAP_MBOX_H

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/omap-mailbox.h>

typedef int __bitwise omap_mbox_type_t;
#define OMAP_MBOX_TYPE1 ((__force omap_mbox_type_t) 1)
#define OMAP_MBOX_TYPE2 ((__force omap_mbox_type_t) 2)

struct omap_mbox_ops {
	omap_mbox_type_t	type;
	int		(*startup)(struct omap_mbox *mbox);
	void		(*shutdown)(struct omap_mbox *mbox);
	/* fifo */
	mbox_msg_t	(*fifo_read)(struct omap_mbox *mbox);
	void		(*fifo_write)(struct omap_mbox *mbox, mbox_msg_t msg);
	int		(*fifo_empty)(struct omap_mbox *mbox);
	int		(*fifo_full)(struct omap_mbox *mbox);
	/* irq */
	void		(*enable_irq)(struct omap_mbox *mbox,
						omap_mbox_irq_t irq);
	void		(*disable_irq)(struct omap_mbox *mbox,
						omap_mbox_irq_t irq);
	void		(*ack_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
	int		(*is_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
	/* ctx */
	void		(*save_ctx)(struct omap_mbox *mbox);
	void		(*restore_ctx)(struct omap_mbox *mbox);
};

struct omap_mbox_queue {
	spinlock_t		lock;
	struct kfifo		fifo;
	struct work_struct	work;
	struct tasklet_struct	tasklet;
	struct omap_mbox	*mbox;
	bool full;
};

struct omap_mbox {
	const char		*name;
	int			irq;
	struct omap_mbox_queue	*txq, *rxq;
	struct omap_mbox_ops	*ops;
	struct device		*dev;
	void			*priv;
	int			use_count;
	struct blocking_notifier_head	notifier;
};

int omap_mbox_register(struct device *parent, struct omap_mbox **);
int omap_mbox_unregister(void);

#endif /* OMAP_MBOX_H */
