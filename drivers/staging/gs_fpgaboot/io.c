// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/firmware.h>
#include <linux/io.h>

#include "io.h"

static inline void byte0_out(unsigned char data);
static inline void byte1_out(unsigned char data);
static inline void xl_cclk_b(int32_t i);

/* Assert and Deassert CCLK */
void xl_shift_cclk(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		xl_cclk_b(1);
		xl_cclk_b(0);
	}
}

int xl_supported_prog_bus_width(enum wbus bus_bytes)
{
	switch (bus_bytes) {
	case bus_1byte:
		break;
	case bus_2byte:
		break;
	default:
		pr_err("unsupported program bus width %d\n", bus_bytes);
		return 0;
	}

	return 1;
}

/* Serialize byte and clock each bit on target's DIN and CCLK pins */
void xl_shift_bytes_out(enum wbus bus_byte, unsigned char *pdata)
{
	/*
	 * supports 1 and 2 bytes programming mode
	 */
	if (likely(bus_byte == bus_2byte))
		byte0_out(pdata[0]);

	byte1_out(pdata[1]);
	xl_shift_cclk(1);
}

/*
 * generic bit swap for xilinx SYSTEMMAP FPGA programming
 */
void xl_program_b(int32_t i)
{
}

void xl_rdwr_b(int32_t i)
{
}

void xl_csi_b(int32_t i)
{
}

int xl_get_init_b(void)
{
	return -1;
}

int xl_get_done_b(void)
{
	return -1;
}

static inline void byte0_out(unsigned char data)
{
}

static inline void byte1_out(unsigned char data)
{
}

static inline void xl_cclk_b(int32_t i)
{
}

/*
 * configurable per device type for different I/O config
 */
int xl_init_io(void)
{
	return -1;
}
