/*
 *  fs/partitions/check.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/genhd.h>

#include "check.h"

#include "rk.h"
#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "ldm.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"
#include "ibm.h"
#include "ultrix.h"
#include "efi.h"
#include "karma.h"
#include "sysv68.h"
#include "cmdline.h"

int warn_no_part = 1; /*This is ugly: should make genhd removable media aware*/

static int (*check_part[])(struct parsed_partitions *) = {
	/*
	 * Probe partition formats with tables at disk address 0
	 * that also have an ADFS boot block at 0xdc0.
	 */
#ifdef CONFIG_ACORN_PARTITION_ICS
	adfspart_check_ICS,
#endif
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
	adfspart_check_POWERTEC,
#endif
#ifdef CONFIG_ACORN_PARTITION_EESOX
	adfspart_check_EESOX,
#endif

	/*
	 * Now move on to formats that only have partition info at
	 * disk address 0xdc0.  Since these may also have stale
	 * PC/BIOS partition tables, they need to come before
	 * the msdos entry.
	 */
#ifdef CONFIG_ACORN_PARTITION_CUMANA
	adfspart_check_CUMANA,
#endif
#ifdef CONFIG_ACORN_PARTITION_ADFS
	adfspart_check_ADFS,
#endif

#ifdef CONFIG_RK_PARTITION
	rkpart_partition,
#endif
#ifdef CONFIG_CMDLINE_PARTITION
	cmdline_partition,
#endif
#ifdef CONFIG_EFI_PARTITION
	efi_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_LDM_PARTITION
	ldm_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
#ifdef CONFIG_IBM_PARTITION
	ibm_partition,
#endif
#ifdef CONFIG_KARMA_PARTITION
	karma_partition,
#endif
#ifdef CONFIG_SYSV68_PARTITION
	sysv68_partition,
#endif
	NULL
};

static struct parsed_partitions *allocate_partitions(struct gendisk *hd)
{
	struct parsed_partitions *state;
	int nr;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	nr = disk_max_parts(hd);
	state->parts = vzalloc(nr * sizeof(state->parts[0]));
	if (!state->parts) {
		kfree(state);
		return NULL;
	}

	state->limit = nr;

	return state;
}

void free_partitions(struct parsed_partitions *state)
{
	vfree(state->parts);
	kfree(state);
}

struct parsed_partitions *
check_partition(struct gendisk *hd, struct block_device *bdev)
{
	struct parsed_partitions *state;
	int i, res, err;

	state = allocate_partitions(hd);
	if (!state)
		return NULL;
	state->pp_buf = (char *)__get_free_page(GFP_KERNEL);
	if (!state->pp_buf) {
		free_partitions(state);
		return NULL;
	}
	state->pp_buf[0] = '\0';

	state->bdev = bdev;
	disk_name(hd, 0, state->name);
	snprintf(state->pp_buf, PAGE_SIZE, " %s:", state->name);
	if (isdigit(state->name[strlen(state->name)-1]))
		sprintf(state->name, "p");

	i = res = err = 0;
	while (!res && check_part[i]) {
		memset(state->parts, 0, state->limit * sizeof(state->parts[0]));
		res = check_part[i++](state);
		if (res < 0) {
			/* We have hit an I/O error which we don't report now.
		 	* But record it, and let the others do their job.
		 	*/
			err = res;
			res = 0;
		}

	}
	if (res > 0) {
		printk(KERN_INFO "%s", state->pp_buf);

		free_page((unsigned long)state->pp_buf);
		return state;
	}
	if (state->access_beyond_eod)
		err = -ENOSPC;
	if (err)
	/* The partition is unrecognized. So report I/O errors if there were any */
		res = err;
	if (res) {
		if (warn_no_part)
			strlcat(state->pp_buf,
				" unable to read partition table\n", PAGE_SIZE);
		printk(KERN_INFO "%s", state->pp_buf);
	}

	free_page((unsigned long)state->pp_buf);
	free_partitions(state);
	return ERR_PTR(res);
}
