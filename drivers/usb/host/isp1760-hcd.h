#ifndef _ISP1760_HCD_H_
#define _ISP1760_HCD_H_

#include <linux/spinlock.h>

struct gpio_desc;
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

/*
 * Device flags that can vary from board to board.  All of these
 * indicate the most "atypical" case, so that a devflags of 0 is
 * a sane default configuration.
 */
#define ISP1760_FLAG_BUS_WIDTH_16	0x00000002 /* 16-bit data bus width */
#define ISP1760_FLAG_OTG_EN		0x00000004 /* Port 1 supports OTG */
#define ISP1760_FLAG_ANALOG_OC		0x00000008 /* Analog overcurrent */
#define ISP1760_FLAG_DACK_POL_HIGH	0x00000010 /* DACK active high */
#define ISP1760_FLAG_DREQ_POL_HIGH	0x00000020 /* DREQ active high */
#define ISP1760_FLAG_ISP1761		0x00000040 /* Chip is ISP1761 */
#define ISP1760_FLAG_INTR_POL_HIGH	0x00000080 /* Interrupt polarity active high */
#define ISP1760_FLAG_INTR_EDGE_TRIG	0x00000100 /* Interrupt edge triggered */

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
	unsigned int		devflags;

	struct gpio_desc	*rst_gpio;
};

/* exports for if */
int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags);
void isp1760_unregister(struct device *dev);

int isp1760_init_kmem_once(void);
void isp1760_deinit_kmem_cache(void);

#endif /* _ISP1760_HCD_H_ */
