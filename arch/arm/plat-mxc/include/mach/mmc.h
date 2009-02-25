#ifndef ASMARM_ARCH_MMC_H
#define ASMARM_ARCH_MMC_H

#include <linux/mmc/host.h>

struct device;

/* board specific SDHC data, optional.
 * If not present, a writable card with 3,3V is assumed.
 */
struct imxmmc_platform_data {
	/* Return values for the get_ro callback should be:
	 *   0 for a read/write card
	 *   1 for a read-only card
	 *   -ENOSYS when not supported (equal to NULL callback)
	 *   or a negative errno value when something bad happened
	 */
	int (*get_ro)(struct device *);

	/* board specific hook to (de)initialize the SD slot.
	 * The board code can call 'handler' on a card detection
	 * change giving data as argument.
	 */
	int (*init)(struct device *dev, irq_handler_t handler, void *data);
	void (*exit)(struct device *dev, void *data);

	/* available voltages. If not given, assume
	 * MMC_VDD_32_33 | MMC_VDD_33_34
	 */
	unsigned int ocr_avail;

	/* adjust slot voltage */
	void (*setpower)(struct device *, unsigned int vdd);
};

#endif
