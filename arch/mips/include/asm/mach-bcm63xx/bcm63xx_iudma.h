#ifndef BCM63XX_IUDMA_H_
#define BCM63XX_IUDMA_H_

#include <linux/types.h>

/*
 * rx/tx dma descriptor
 */
struct bcm_enet_desc {
	u32 len_stat;
	u32 address;
};

/* control */
#define DMADESC_LENGTH_SHIFT	16
#define DMADESC_LENGTH_MASK	(0xfff << DMADESC_LENGTH_SHIFT)
#define DMADESC_OWNER_MASK	(1 << 15)
#define DMADESC_EOP_MASK	(1 << 14)
#define DMADESC_SOP_MASK	(1 << 13)
#define DMADESC_ESOP_MASK	(DMADESC_EOP_MASK | DMADESC_SOP_MASK)
#define DMADESC_WRAP_MASK	(1 << 12)
#define DMADESC_USB_NOZERO_MASK	(1 << 1)
#define DMADESC_USB_ZERO_MASK	(1 << 0)

/* status */
#define DMADESC_UNDER_MASK	(1 << 9)
#define DMADESC_APPEND_CRC	(1 << 8)
#define DMADESC_OVSIZE_MASK	(1 << 4)
#define DMADESC_RXER_MASK	(1 << 2)
#define DMADESC_CRC_MASK	(1 << 1)
#define DMADESC_OV_MASK		(1 << 0)
#define DMADESC_ERR_MASK	(DMADESC_UNDER_MASK | \
				DMADESC_OVSIZE_MASK | \
				DMADESC_RXER_MASK | \
				DMADESC_CRC_MASK | \
				DMADESC_OV_MASK)

#endif /* ! BCM63XX_IUDMA_H_ */
