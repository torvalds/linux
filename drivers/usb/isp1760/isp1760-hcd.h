/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ISP1760_HCD_H_
#define _ISP1760_HCD_H_

#include <linux/spinlock.h>
#include <linux/regmap.h>

#include "isp1760-regs.h"

struct isp1760_qh;
struct isp1760_qtd;
struct resource;
struct usb_hcd;

struct isp1760_slotinfo {
	struct isp1760_qh *qh;
	struct isp1760_qtd *qtd;
	unsigned long timestamp;
};

/* chip memory management */
#define ISP176x_BLOCK_MAX (32 + 20 + 4)
#define ISP176x_BLOCK_NUM 3

struct isp1760_memory_layout {
	unsigned int blocks[ISP176x_BLOCK_NUM];
	unsigned int blocks_size[ISP176x_BLOCK_NUM];

	unsigned int slot_num;
	unsigned int payload_blocks;
	unsigned int payload_area_size;
};

struct isp1760_memory_chunk {
	unsigned int start;
	unsigned int size;
	unsigned int free;
};

enum isp1760_queue_head_types {
	QH_CONTROL,
	QH_BULK,
	QH_INTERRUPT,
	QH_END
};

struct isp1760_hcd {
	struct usb_hcd		*hcd;

	void __iomem		*base;

	struct regmap		*regs;
	struct regmap_field	*fields[HC_FIELD_MAX];

	bool			is_isp1763;
	const struct isp1760_memory_layout	*memory_layout;

	spinlock_t		lock;
	struct isp1760_slotinfo	*atl_slots;
	int			atl_done_map;
	struct isp1760_slotinfo	*int_slots;
	int			int_done_map;
	struct isp1760_memory_chunk memory_pool[ISP176x_BLOCK_MAX];
	struct list_head	qh_list[QH_END];

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024
	unsigned		periodic_size;
	unsigned		i_thresh;
	unsigned long		reset_done;
	unsigned long		next_statechange;
};

#ifdef CONFIG_USB_ISP1760_HCD
int isp1760_hcd_register(struct isp1760_hcd *priv, struct resource *mem,
			 int irq, unsigned long irqflags, struct device *dev);
void isp1760_hcd_unregister(struct isp1760_hcd *priv);

int isp1760_init_kmem_once(void);
void isp1760_deinit_kmem_cache(void);
#else
static inline int isp1760_hcd_register(struct isp1760_hcd *priv,
				       struct resource *mem,
				       int irq, unsigned long irqflags,
				       struct device *dev)
{
	return 0;
}

static inline void isp1760_hcd_unregister(struct isp1760_hcd *priv)
{
}

static inline int isp1760_init_kmem_once(void)
{
	return 0;
}

static inline void isp1760_deinit_kmem_cache(void)
{
}
#endif

#endif /* _ISP1760_HCD_H_ */
