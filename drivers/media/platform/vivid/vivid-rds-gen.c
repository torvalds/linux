// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-rds-gen.c - rds (radio data system) generator support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include "vivid-rds-gen.h"

static u8 vivid_get_di(const struct vivid_rds_gen *rds, unsigned grp)
{
	switch (grp) {
	case 0:
		return (rds->dyn_pty << 2) | (grp & 3);
	case 1:
		return (rds->compressed << 2) | (grp & 3);
	case 2:
		return (rds->art_head << 2) | (grp & 3);
	case 3:
		return (rds->mono_stereo << 2) | (grp & 3);
	}
	return 0;
}

/*
 * This RDS generator creates 57 RDS groups (one group == four RDS blocks).
 * Groups 0-3, 22-25 and 44-47 (spaced 22 groups apart) are filled with a
 * standard 0B group containing the PI code and PS name.
 *
 * Groups 4-19 and 26-41 use group 2A for the radio text.
 *
 * Group 56 contains the time (group 4A).
 *
 * All remaining groups use a filler group 15B block that just repeats
 * the PI and PTY codes.
 */
void vivid_rds_generate(struct vivid_rds_gen *rds)
{
	struct v4l2_rds_data *data = rds->data;
	unsigned grp;
	unsigned idx;
	struct tm tm;
	unsigned date;
	unsigned time;
	int l;

	for (grp = 0; grp < VIVID_RDS_GEN_GROUPS; grp++, data += VIVID_RDS_GEN_BLKS_PER_GRP) {
		data[0].lsb = rds->picode & 0xff;
		data[0].msb = rds->picode >> 8;
		data[0].block = V4L2_RDS_BLOCK_A | (V4L2_RDS_BLOCK_A << 3);
		data[1].lsb = rds->pty << 5;
		data[1].msb = (rds->pty >> 3) | (rds->tp << 2);
		data[1].block = V4L2_RDS_BLOCK_B | (V4L2_RDS_BLOCK_B << 3);
		data[3].block = V4L2_RDS_BLOCK_D | (V4L2_RDS_BLOCK_D << 3);

		switch (grp) {
		case 0 ... 3:
		case 22 ... 25:
		case 44 ... 47: /* Group 0B */
			idx = (grp % 22) % 4;
			data[1].lsb |= (rds->ta << 4) | (rds->ms << 3);
			data[1].lsb |= vivid_get_di(rds, idx);
			data[1].msb |= 1 << 3;
			data[2].lsb = rds->picode & 0xff;
			data[2].msb = rds->picode >> 8;
			data[2].block = V4L2_RDS_BLOCK_C_ALT | (V4L2_RDS_BLOCK_C_ALT << 3);
			data[3].lsb = rds->psname[2 * idx + 1];
			data[3].msb = rds->psname[2 * idx];
			break;
		case 4 ... 19:
		case 26 ... 41: /* Group 2A */
			idx = ((grp - 4) % 22) % 16;
			data[1].lsb |= idx;
			data[1].msb |= 4 << 3;
			data[2].msb = rds->radiotext[4 * idx];
			data[2].lsb = rds->radiotext[4 * idx + 1];
			data[2].block = V4L2_RDS_BLOCK_C | (V4L2_RDS_BLOCK_C << 3);
			data[3].msb = rds->radiotext[4 * idx + 2];
			data[3].lsb = rds->radiotext[4 * idx + 3];
			break;
		case 56:
			/*
			 * Group 4A
			 *
			 * Uses the algorithm from Annex G of the RDS standard
			 * EN 50067:1998 to convert a UTC date to an RDS Modified
			 * Julian Day.
			 */
			time64_to_tm(ktime_get_real_seconds(), 0, &tm);
			l = tm.tm_mon <= 1;
			date = 14956 + tm.tm_mday + ((tm.tm_year - l) * 1461) / 4 +
				((tm.tm_mon + 2 + l * 12) * 306001) / 10000;
			time = (tm.tm_hour << 12) |
			       (tm.tm_min << 6) |
			       (sys_tz.tz_minuteswest >= 0 ? 0x20 : 0) |
			       (abs(sys_tz.tz_minuteswest) / 30);
			data[1].lsb &= ~3;
			data[1].lsb |= date >> 15;
			data[1].msb |= 8 << 3;
			data[2].lsb = (date << 1) & 0xfe;
			data[2].lsb |= (time >> 16) & 1;
			data[2].msb = (date >> 7) & 0xff;
			data[2].block = V4L2_RDS_BLOCK_C | (V4L2_RDS_BLOCK_C << 3);
			data[3].lsb = time & 0xff;
			data[3].msb = (time >> 8) & 0xff;
			break;
		default: /* Group 15B */
			data[1].lsb |= (rds->ta << 4) | (rds->ms << 3);
			data[1].lsb |= vivid_get_di(rds, grp % 22);
			data[1].msb |= 0x1f << 3;
			data[2].lsb = rds->picode & 0xff;
			data[2].msb = rds->picode >> 8;
			data[2].block = V4L2_RDS_BLOCK_C_ALT | (V4L2_RDS_BLOCK_C_ALT << 3);
			data[3].lsb = rds->pty << 5;
			data[3].lsb |= (rds->ta << 4) | (rds->ms << 3);
			data[3].lsb |= vivid_get_di(rds, grp % 22);
			data[3].msb |= rds->pty >> 3;
			data[3].msb |= 0x1f << 3;
			break;
		}
	}
}

void vivid_rds_gen_fill(struct vivid_rds_gen *rds, unsigned freq,
			  bool alt)
{
	/* Alternate PTY between Info and Weather */
	if (rds->use_rbds) {
		rds->picode = 0x2e75; /* 'KLNX' call sign */
		rds->pty = alt ? 29 : 2;
	} else {
		rds->picode = 0x8088;
		rds->pty = alt ? 16 : 3;
	}
	rds->mono_stereo = true;
	rds->art_head = false;
	rds->compressed = false;
	rds->dyn_pty = false;
	rds->tp = true;
	rds->ta = alt;
	rds->ms = true;
	snprintf(rds->psname, sizeof(rds->psname), "%6d.%1d",
		 freq / 16, ((freq & 0xf) * 10) / 16);
	if (alt)
		strlcpy(rds->radiotext,
			" The Radio Data System can switch between different Radio Texts ",
			sizeof(rds->radiotext));
	else
		strlcpy(rds->radiotext,
			"An example of Radio Text as transmitted by the Radio Data System",
			sizeof(rds->radiotext));
}
