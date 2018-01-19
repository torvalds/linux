/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BCM63XX_PCMCIA_H_
#define BCM63XX_PCMCIA_H_

#include <linux/types.h>
#include <linux/timer.h>
#include <pcmcia/ss.h>
#include <bcm63xx_dev_pcmcia.h>

/* socket polling rate in ms */
#define BCM63XX_PCMCIA_POLL_RATE	500

enum {
	CARD_CARDBUS = (1 << 0),
	CARD_PCCARD = (1 << 1),
	CARD_5V = (1 << 2),
	CARD_3V = (1 << 3),
	CARD_XV = (1 << 4),
	CARD_YV = (1 << 5),
};

struct bcm63xx_pcmcia_socket {
	struct pcmcia_socket socket;

	/* platform specific data */
	struct bcm63xx_pcmcia_platform_data *pd;

	/* all regs access are protected by this spinlock */
	spinlock_t lock;

	/* pcmcia registers resource */
	struct resource *reg_res;

	/* base remapped address of registers */
	void __iomem *base;

	/* whether a card is detected at the moment */
	int card_detected;

	/* type of detected card (mask of above enum) */
	u8 card_type;

	/* keep last socket status to implement event reporting */
	unsigned int old_status;

	/* backup of requested socket state */
	socket_state_t requested_state;

	/* timer used for socket status polling */
	struct timer_list timer;

	/* attribute/common memory resources */
	struct resource *attr_res;
	struct resource *common_res;
	struct resource *io_res;

	/* base address of io memory */
	void __iomem *io_base;
};

#endif /* BCM63XX_PCMCIA_H_ */
