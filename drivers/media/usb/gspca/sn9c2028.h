/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SN9C2028 common functions
 *
 * Copyright (C) 2009 Theodore Kilgore <kilgota@auburn,edu>
 *
 * Based closely upon the file gspca/pac_common.h
 */

static const unsigned char sn9c2028_sof_marker[] = {
	0xff, 0xff, 0x00, 0xc4, 0xc4, 0x96,
	0x00,
	0x00, /* seq */
	0x00,
	0x00,
	0x00, /* avg luminance lower 8 bit */
	0x00, /* avg luminance higher 8 bit */
};

static unsigned char *sn9c2028_find_sof(struct gspca_dev *gspca_dev,
					unsigned char *m, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;

	/* Search for the SOF marker (fixed part) in the header */
	for (i = 0; i < len; i++) {
		if ((m[i] == sn9c2028_sof_marker[sd->sof_read]) ||
		    (sd->sof_read > 5)) {
			sd->sof_read++;
			if (sd->sof_read == 11)
				sd->avg_lum_l = m[i];
			if (sd->sof_read == 12)
				sd->avg_lum = (m[i] << 8) + sd->avg_lum_l;
			if (sd->sof_read == sizeof(sn9c2028_sof_marker)) {
				gspca_dbg(gspca_dev, D_FRAM,
					  "SOF found, bytes to analyze: %u - Frame starts at byte #%u\n",
					  len, i + 1);
				sd->sof_read = 0;
				return m + i + 1;
			}
		} else {
			sd->sof_read = 0;
		}
	}

	return NULL;
}
