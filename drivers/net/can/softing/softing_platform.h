/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/platform_device.h>

#ifndef _SOFTING_DEVICE_H_
#define _SOFTING_DEVICE_H_

/* softing firmware directory prefix */
#define fw_dir "softing-4.6/"

struct softing_platform_data {
	unsigned int manf;
	unsigned int prod;
	/*
	 * generation
	 * 1st with NEC or SJA1000
	 * 8bit, exclusive interrupt, ...
	 * 2nd only SJA1000
	 * 16bit, shared interrupt
	 */
	int generation;
	int nbus; /* # buses on device */
	unsigned int freq; /* operating frequency in Hz */
	unsigned int max_brp;
	unsigned int max_sjw;
	unsigned long dpram_size;
	const char *name;
	struct {
		unsigned long offs;
		unsigned long addr;
		const char *fw;
	} boot, load, app;
	/*
	 * reset() function
	 * bring pdev in or out of reset, depending on value
	 */
	int (*reset)(struct platform_device *pdev, int value);
	int (*enable_irq)(struct platform_device *pdev, int value);
};

#endif
