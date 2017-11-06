// SPDX-License-Identifier: GPL-2.0+
/*
 * Console via Blackfin JTAG Communication
 *
 * Copyright 2008-2011 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#include "hvc_console.h"

/* See the Debug/Emulation chapter in the HRM */
#define EMUDOF   0x00000001	/* EMUDAT_OUT full & valid */
#define EMUDIF   0x00000002	/* EMUDAT_IN full & valid */
#define EMUDOOVF 0x00000004	/* EMUDAT_OUT overflow */
#define EMUDIOVF 0x00000008	/* EMUDAT_IN overflow */

/* Helper functions to glue the register API to simple C operations */
static inline uint32_t bfin_write_emudat(uint32_t emudat)
{
	__asm__ __volatile__("emudat = %0;" : : "d"(emudat));
	return emudat;
}

static inline uint32_t bfin_read_emudat(void)
{
	uint32_t emudat;
	__asm__ __volatile__("%0 = emudat;" : "=d"(emudat));
	return emudat;
}

/* Send data to the host */
static int hvc_bfin_put_chars(uint32_t vt, const char *buf, int count)
{
	static uint32_t outbound_len;
	uint32_t emudat;
	int ret;

	if (bfin_read_DBGSTAT() & EMUDOF)
		return 0;

	if (!outbound_len) {
		outbound_len = count;
		bfin_write_emudat(outbound_len);
		return 0;
	}

	ret = min(outbound_len, (uint32_t)4);
	memcpy(&emudat, buf, ret);
	bfin_write_emudat(emudat);
	outbound_len -= ret;

	return ret;
}

/* Receive data from the host */
static int hvc_bfin_get_chars(uint32_t vt, char *buf, int count)
{
	static uint32_t inbound_len;
	uint32_t emudat;
	int ret;

	if (!(bfin_read_DBGSTAT() & EMUDIF))
		return 0;
	emudat = bfin_read_emudat();

	if (!inbound_len) {
		inbound_len = emudat;
		return 0;
	}

	ret = min(inbound_len, (uint32_t)4);
	memcpy(buf, &emudat, ret);
	inbound_len -= ret;

	return ret;
}

/* Glue the HVC layers to the Blackfin layers */
static const struct hv_ops hvc_bfin_get_put_ops = {
	.get_chars = hvc_bfin_get_chars,
	.put_chars = hvc_bfin_put_chars,
};

static int __init hvc_bfin_console_init(void)
{
	hvc_instantiate(0, 0, &hvc_bfin_get_put_ops);
	return 0;
}
console_initcall(hvc_bfin_console_init);

static int __init hvc_bfin_init(void)
{
	hvc_alloc(0, 0, &hvc_bfin_get_put_ops, 128);
	return 0;
}
device_initcall(hvc_bfin_init);
