/*
 * MCE grading rules.
 * Copyright 2008, 2009 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Author: Andi Kleen
 */
#include <linux/kernel.h>
#include <asm/mce.h>

#include "mce-internal.h"

/*
 * Grade an mce by severity. In general the most severe ones are processed
 * first. Since there are quite a lot of combinations test the bits in a
 * table-driven way. The rules are simply processed in order, first
 * match wins.
 */

static struct severity {
	u64 mask;
	u64 result;
	unsigned char sev;
	unsigned char mcgmask;
	unsigned char mcgres;
	char *msg;
} severities[] = {
#define SEV(s) .sev = MCE_ ## s ## _SEVERITY
#define BITCLR(x, s, m, r...) { .mask = x, .result = 0, SEV(s), .msg = m, ## r }
#define BITSET(x, s, m, r...) { .mask = x, .result = x, SEV(s), .msg = m, ## r }
#define MCGMASK(x, res, s, m, r...) \
	{ .mcgmask = x, .mcgres = res, SEV(s), .msg = m, ## r }
	BITCLR(MCI_STATUS_VAL, NO, "Invalid"),
	BITCLR(MCI_STATUS_EN, NO, "Not enabled"),
	BITSET(MCI_STATUS_PCC, PANIC, "Processor context corrupt"),
	MCGMASK(MCG_STATUS_RIPV, 0, PANIC, "No restart IP"),
	BITSET(MCI_STATUS_UC|MCI_STATUS_OVER, PANIC, "Overflowed uncorrected"),
	BITSET(MCI_STATUS_UC, UC, "Uncorrected"),
	BITSET(0, SOME, "No match")	/* always matches. keep at end */
};

int mce_severity(struct mce *a, int tolerant, char **msg)
{
	struct severity *s;
	for (s = severities;; s++) {
		if ((a->status & s->mask) != s->result)
			continue;
		if ((a->mcgstatus & s->mcgmask) != s->mcgres)
			continue;
		if (s->sev > MCE_NO_SEVERITY && (a->status & MCI_STATUS_UC) &&
			tolerant < 1)
			return MCE_PANIC_SEVERITY;
		if (msg)
			*msg = s->msg;
		return s->sev;
	}
}
