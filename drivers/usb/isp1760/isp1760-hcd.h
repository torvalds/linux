/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ISP1760_HCD_H_
#define _ISP1760_HCD_H_

#include <linux/spinlock.h>

struct isp1760_qh;
struct isp1760_qtd;
struct resource;
struct usb_hcd;

/*
 * 60kb divided in:
 * - 32 blocks @ 256  bytes
 * - 20 blocks @ 1024 bytes
 * -  4 blocks @ 8192 bytes
 */

#define BLOCK_1_NUM 32
#define BLOCK_2_NUM 20
#define BLOCK_3_NUM 4

#define BLOCK_1_SIZE 256
#define BLOCK_2_SIZE 1024
#define BLOCK_3_SIZE 8192
#define BLOCKS (BLOCK_1_NUM + BLOCK_2_NUM + BLOCK_3_NUM)
#define MAX_PAYLOAD_SIZE BLOCK_3_SIZE
#define PAYLOAD_AREA_SIZE 0xf000

struct isp1760_slotinfo {
	struct isp1760_qh *qh;
	struct isp1760_qtd *qtd;
	unsigned long timestamp;
};

/* chip memory management */
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
#ifdef CONFIG_USB_ISP1760_HCD
	struct usb_hcd		*hcd;

	u32 hcs_params;
	spinlock_t		lock;
	struct isp1760_slotinfo	atl_slots[32];
	int			atl_done_map;
	struct isp1760_slotinfo	int_slots[32];
	int			int_done_map;
	struct isp1760_memory_chunk memory_pool[BLOCKS];
	struct list_head	qh_list[QH_END];

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024
	unsigned		periodic_size;
	unsigned		i_thresh;
	unsigned long		reset_done;
	unsigned long		next_statechange;
#endif
};

#ifdef CONFIG_USB_ISP1760_HCD
int isp1760_hcd_register(struct isp1760_hcd *priv, void __iomem *regs,
			 struct resource *mem, int irq, unsigned long irqflags,
			 struct device *dev);
void isp1760_hcd_unregister(struct isp1760_hcd *priv);

int isp1760_init_kmem_once(void);
void isp1760_deinit_kmem_cache(void);
#else
static inline int isp1760_hcd_register(struct isp1760_hcd *priv,
				       void __iomem *regs, struct resource *mem,
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
