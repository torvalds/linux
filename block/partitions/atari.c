// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/partitions/atari.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/ctype.h>
#include "check.h"
#include "atari.h"

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

/* check if a partition entry looks valid -- Atari format is assumed if at
   least one of the primary entries is ok this way */
#define	VALID_PARTITION(pi,hdsiz)					     \
    (((pi)->flg & 1) &&							     \
     isalnum((pi)->id[0]) && isalnum((pi)->id[1]) && isalnum((pi)->id[2]) && \
     be32_to_cpu((pi)->st) <= (hdsiz) &&				     \
     be32_to_cpu((pi)->st) + be32_to_cpu((pi)->siz) <= (hdsiz))

static inline int OK_id(char *s)
{
	return  memcmp (s, "GEM", 3) == 0 || memcmp (s, "BGM", 3) == 0 ||
		memcmp (s, "LNX", 3) == 0 || memcmp (s, "SWP", 3) == 0 ||
		memcmp (s, "RAW", 3) == 0 ;
}

int atari_partition(struct parsed_partitions *state)
{
	Sector sect;
	struct rootsector *rs;
	struct partition_info *pi;
	u32 extensect;
	u32 hd_size;
	int slot;
#ifdef ICD_PARTS
	int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

	/*
	 * ATARI partition scheme supports 512 lba only.  If this is not
	 * the case, bail early to avoid miscalculating hd_size.
	 */
	if (queue_logical_block_size(state->disk->queue) != 512)
		return 0;

	rs = read_part_sector(state, 0, &sect);
	if (!rs)
		return -1;

	/* Verify this is an Atari rootsector: */
	hd_size = get_capacity(state->disk);
	if (!VALID_PARTITION(&rs->part[0], hd_size) &&
	    !VALID_PARTITION(&rs->part[1], hd_size) &&
	    !VALID_PARTITION(&rs->part[2], hd_size) &&
	    !VALID_PARTITION(&rs->part[3], hd_size)) {
		/*
		 * if there's no valid primary partition, assume that no Atari
		 * format partition table (there's no reliable magic or the like
	         * :-()
		 */
		put_dev_sector(sect);
		return 0;
	}

	pi = &rs->part[0];
	strlcat(state->pp_buf, " AHDI", PAGE_SIZE);
	for (slot = 1; pi < &rs->part[4] && slot < state->limit; slot++, pi++) {
		struct rootsector *xrs;
		Sector sect2;
		ulong partsect;

		if ( !(pi->flg & 1) )
			continue;
		/* active partition */
		if (memcmp (pi->id, "XGM", 3) != 0) {
			/* we don't care about other id's */
			put_partition (state, slot, be32_to_cpu(pi->st),
					be32_to_cpu(pi->siz));
			continue;
		}
		/* extension partition */
#ifdef ICD_PARTS
		part_fmt = 1;
#endif
		strlcat(state->pp_buf, " XGM<", PAGE_SIZE);
		partsect = extensect = be32_to_cpu(pi->st);
		while (1) {
			xrs = read_part_sector(state, partsect, &sect2);
			if (!xrs) {
				printk (" block %ld read failed\n", partsect);
				put_dev_sector(sect);
				return -1;
			}

			/* ++roman: sanity check: bit 0 of flg field must be set */
			if (!(xrs->part[0].flg & 1)) {
				printk( "\nFirst sub-partition in extended partition is not valid!\n" );
				put_dev_sector(sect2);
				break;
			}

			put_partition(state, slot,
				   partsect + be32_to_cpu(xrs->part[0].st),
				   be32_to_cpu(xrs->part[0].siz));

			if (!(xrs->part[1].flg & 1)) {
				/* end of linked partition list */
				put_dev_sector(sect2);
				break;
			}
			if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
				printk("\nID of extended partition is not XGM!\n");
				put_dev_sector(sect2);
				break;
			}

			partsect = be32_to_cpu(xrs->part[1].st) + extensect;
			put_dev_sector(sect2);
			if (++slot == state->limit) {
				printk( "\nMaximum number of partitions reached!\n" );
				break;
			}
		}
		strlcat(state->pp_buf, " >", PAGE_SIZE);
	}
#ifdef ICD_PARTS
	if ( part_fmt!=1 ) { /* no extended partitions -> test ICD-format */
		pi = &rs->icdpart[0];
		/* sanity check: no ICD format if first partition invalid */
		if (OK_id(pi->id)) {
			strlcat(state->pp_buf, " ICD<", PAGE_SIZE);
			for (; pi < &rs->icdpart[8] && slot < state->limit; slot++, pi++) {
				/* accept only GEM,BGM,RAW,LNX,SWP partitions */
				if (!((pi->flg & 1) && OK_id(pi->id)))
					continue;
				part_fmt = 2;
				put_partition (state, slot,
						be32_to_cpu(pi->st),
						be32_to_cpu(pi->siz));
			}
			strlcat(state->pp_buf, " >", PAGE_SIZE);
		}
	}
#endif
	put_dev_sector(sect);

	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	return 1;
}
