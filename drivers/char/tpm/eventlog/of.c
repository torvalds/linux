// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 IBM Corporation
 *
 * Author: Ashley Lai <ashleydlai@gmail.com>
 *         Nayna Jain <nayna@linux.vnet.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Read the event log created by the firmware on PPC64
 */

#include <linux/slab.h>
#include <linux/of.h>
#include <linux/tpm_eventlog.h>

#include "../tpm.h"
#include "common.h"

int tpm_read_log_of(struct tpm_chip *chip)
{
	struct device_node *np;
	const u32 *sizep;
	const u64 *basep;
	struct tpm_bios_log *log;
	u32 size;
	u64 base;

	log = &chip->log;
	if (chip->dev.parent && chip->dev.parent->of_node)
		np = chip->dev.parent->of_node;
	else
		return -ENODEV;

	if (of_property_read_bool(np, "powered-while-suspended"))
		chip->flags |= TPM_CHIP_FLAG_ALWAYS_POWERED;

	sizep = of_get_property(np, "linux,sml-size", NULL);
	basep = of_get_property(np, "linux,sml-base", NULL);
	if (sizep == NULL && basep == NULL)
		return -ENODEV;
	if (sizep == NULL || basep == NULL)
		return -EIO;

	/*
	 * For both vtpm/tpm, firmware has log addr and log size in big
	 * endian format. But in case of vtpm, there is a method called
	 * sml-handover which is run during kernel init even before
	 * device tree is setup. This sml-handover function takes care
	 * of endianness and writes to sml-base and sml-size in little
	 * endian format. For this reason, vtpm doesn't need conversion
	 * but physical tpm needs the conversion.
	 */
	if (of_property_match_string(np, "compatible", "IBM,vtpm") < 0 &&
	    of_property_match_string(np, "compatible", "IBM,vtpm20") < 0) {
		size = be32_to_cpup((__force __be32 *)sizep);
		base = be64_to_cpup((__force __be64 *)basep);
	} else {
		size = *sizep;
		base = *basep;
	}

	if (size == 0) {
		dev_warn(&chip->dev, "%s: Event log area empty\n", __func__);
		return -EIO;
	}

	log->bios_event_log = kmemdup(__va(base), size, GFP_KERNEL);
	if (!log->bios_event_log)
		return -ENOMEM;

	log->bios_event_log_end = log->bios_event_log + size;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return EFI_TCG2_EVENT_LOG_FORMAT_TCG_2;
	return EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2;
}
