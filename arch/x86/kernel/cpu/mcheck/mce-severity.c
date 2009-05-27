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
 *
 * Note this is only used for machine check exceptions, the corrected
 * errors use much simpler rules. The exceptions still check for the corrected
 * errors, but only to leave them alone for the CMCI handler (except for
 * panic situations)
 */

enum context { IN_KERNEL = 1, IN_USER = 2 };
enum ser { SER_REQUIRED = 1, NO_SER = 2 };

static struct severity {
	u64 mask;
	u64 result;
	unsigned char sev;
	unsigned char mcgmask;
	unsigned char mcgres;
	unsigned char ser;
	unsigned char context;
	char *msg;
} severities[] = {
#define KERNEL .context = IN_KERNEL
#define USER .context = IN_USER
#define SER .ser = SER_REQUIRED
#define NOSER .ser = NO_SER
#define SEV(s) .sev = MCE_ ## s ## _SEVERITY
#define BITCLR(x, s, m, r...) { .mask = x, .result = 0, SEV(s), .msg = m, ## r }
#define BITSET(x, s, m, r...) { .mask = x, .result = x, SEV(s), .msg = m, ## r }
#define MCGMASK(x, res, s, m, r...) \
	{ .mcgmask = x, .mcgres = res, SEV(s), .msg = m, ## r }
#define MASK(x, y, s, m, r...) \
	{ .mask = x, .result = y, SEV(s), .msg = m, ## r }
#define MCI_UC_S (MCI_STATUS_UC|MCI_STATUS_S)
#define MCI_UC_SAR (MCI_STATUS_UC|MCI_STATUS_S|MCI_STATUS_AR)
#define MCACOD 0xffff

	BITCLR(MCI_STATUS_VAL, NO, "Invalid"),
	BITCLR(MCI_STATUS_EN, NO, "Not enabled"),
	BITSET(MCI_STATUS_PCC, PANIC, "Processor context corrupt"),
	/* When MCIP is not set something is very confused */
	MCGMASK(MCG_STATUS_MCIP, 0, PANIC, "MCIP not set in MCA handler"),
	/* Neither return not error IP -- no chance to recover -> PANIC */
	MCGMASK(MCG_STATUS_RIPV|MCG_STATUS_EIPV, 0, PANIC,
		"Neither restart nor error IP"),
	MCGMASK(MCG_STATUS_RIPV, 0, PANIC, "In kernel and no restart IP",
		KERNEL),
	BITCLR(MCI_STATUS_UC, KEEP, "Corrected error", NOSER),
	MASK(MCI_STATUS_OVER|MCI_STATUS_UC|MCI_STATUS_EN, MCI_STATUS_UC, SOME,
	     "Spurious not enabled", SER),

	/* ignore OVER for UCNA */
	MASK(MCI_UC_SAR, MCI_STATUS_UC, KEEP,
	     "Uncorrected no action required", SER),
	MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_STATUS_UC|MCI_STATUS_AR, PANIC,
	     "Illegal combination (UCNA with AR=1)", SER),
	MASK(MCI_STATUS_S, 0, KEEP, "Non signalled machine check", SER),

	/* AR add known MCACODs here */
	MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_STATUS_OVER|MCI_UC_SAR, PANIC,
	     "Action required with lost events", SER),
	MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCACOD, MCI_UC_SAR, PANIC,
	     "Action required; unknown MCACOD", SER),

	/* known AO MCACODs: */
	MASK(MCI_UC_SAR|MCI_STATUS_OVER|0xfff0, MCI_UC_S|0xc0, AO,
	     "Action optional: memory scrubbing error", SER),
	MASK(MCI_UC_SAR|MCI_STATUS_OVER|MCACOD, MCI_UC_S|0x17a, AO,
	     "Action optional: last level cache writeback error", SER),

	MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_UC_S, SOME,
	     "Action optional unknown MCACOD", SER),
	MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_UC_S|MCI_STATUS_OVER, SOME,
	     "Action optional with lost events", SER),
	BITSET(MCI_STATUS_UC|MCI_STATUS_OVER, PANIC, "Overflowed uncorrected"),
	BITSET(MCI_STATUS_UC, UC, "Uncorrected"),
	BITSET(0, SOME, "No match")	/* always matches. keep at end */
};

/*
 * If the EIPV bit is set, it means the saved IP is the
 * instruction which caused the MCE.
 */
static int error_context(struct mce *m)
{
	if (m->mcgstatus & MCG_STATUS_EIPV)
		return (m->ip && (m->cs & 3) == 3) ? IN_USER : IN_KERNEL;
	/* Unknown, assume kernel */
	return IN_KERNEL;
}

int mce_severity(struct mce *a, int tolerant, char **msg)
{
	enum context ctx = error_context(a);
	struct severity *s;

	for (s = severities;; s++) {
		if ((a->status & s->mask) != s->result)
			continue;
		if ((a->mcgstatus & s->mcgmask) != s->mcgres)
			continue;
		if (s->ser == SER_REQUIRED && !mce_ser)
			continue;
		if (s->ser == NO_SER && mce_ser)
			continue;
		if (s->context && ctx != s->context)
			continue;
		if (msg)
			*msg = s->msg;
		if (s->sev >= MCE_UC_SEVERITY && ctx == IN_KERNEL) {
			if (panic_on_oops || tolerant < 1)
				return MCE_PANIC_SEVERITY;
		}
		return s->sev;
	}
}
