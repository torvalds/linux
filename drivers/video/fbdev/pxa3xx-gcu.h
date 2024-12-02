/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PXA3XX_GCU_H__
#define __PXA3XX_GCU_H__

#include <linux/types.h>

/* Number of 32bit words in display list (ring buffer). */
#define PXA3XX_GCU_BUFFER_WORDS  ((256 * 1024 - 256) / 4)

/* To be increased when breaking the ABI */
#define PXA3XX_GCU_SHARED_MAGIC  0x30000001

#define PXA3XX_GCU_BATCH_WORDS   8192

struct pxa3xx_gcu_shared {
	u32            buffer[PXA3XX_GCU_BUFFER_WORDS];

	bool           hw_running;

	unsigned long  buffer_phys;

	unsigned int   num_words;
	unsigned int   num_writes;
	unsigned int   num_done;
	unsigned int   num_interrupts;
	unsigned int   num_wait_idle;
	unsigned int   num_wait_free;
	unsigned int   num_idle;

	u32            magic;
};

/* Initialization and synchronization.
 * Hardware is started upon write(). */
#define PXA3XX_GCU_IOCTL_RESET		_IO('G', 0)
#define PXA3XX_GCU_IOCTL_WAIT_IDLE	_IO('G', 2)

#endif /* __PXA3XX_GCU_H__ */

