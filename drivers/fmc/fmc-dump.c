// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/fmc.h>
#include <linux/fmc-sdb.h>

static int fmc_must_dump_eeprom;
module_param_named(dump_eeprom, fmc_must_dump_eeprom, int, 0644);

#define LINELEN 16

/* Dumping 8k takes oh so much: avoid duplicate lines */
static const uint8_t *dump_line(int addr, const uint8_t *line,
				const uint8_t *prev)
{
	int i;

	if (!prev || memcmp(line, prev, LINELEN)) {
		pr_info("%04x: ", addr);
		for (i = 0; i < LINELEN; ) {
			printk(KERN_CONT "%02x", line[i]);
			i++;
			printk(i & 3 ? " " : i & (LINELEN - 1) ? "  " : "\n");
		}
		return line;
	}
	/* repeated line */
	if (line == prev + LINELEN)
		pr_info("[...]\n");
	return prev;
}

void fmc_dump_eeprom(const struct fmc_device *fmc)
{
	const uint8_t *line, *prev;
	int i;

	if (!fmc_must_dump_eeprom)
		return;

	pr_info("FMC: %s (%s), slot %i, device %s\n", dev_name(fmc->hwdev),
		fmc->carrier_name, fmc->slot_id, dev_name(&fmc->dev));
	pr_info("FMC: dumping eeprom 0x%x (%i) bytes\n", fmc->eeprom_len,
	       fmc->eeprom_len);

	line = fmc->eeprom;
	prev = NULL;
	for (i = 0; i < fmc->eeprom_len; i += LINELEN, line += LINELEN)
		prev = dump_line(i, line, prev);
}
