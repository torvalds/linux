/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  fs/partitions/sun.h
 */

#define SUN_LABEL_MAGIC          0xDABE
#define SUN_VTOC_SANITY          0x600DDEEE

int sun_partition(struct parsed_partitions *state);
