// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2012
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include "idset.h"
#include "css.h"

struct idset {
	int num_ssid;
	int num_id;
	unsigned long bitmap[];
};

static inline unsigned long idset_bitmap_size(int num_ssid, int num_id)
{
	return BITS_TO_LONGS(num_ssid * num_id) * sizeof(unsigned long);
}

static struct idset *idset_new(int num_ssid, int num_id)
{
	struct idset *set;

	set = vmalloc(sizeof(struct idset) +
		      idset_bitmap_size(num_ssid, num_id));
	if (set) {
		set->num_ssid = num_ssid;
		set->num_id = num_id;
		memset(set->bitmap, 0, idset_bitmap_size(num_ssid, num_id));
	}
	return set;
}

void idset_free(struct idset *set)
{
	vfree(set);
}

void idset_fill(struct idset *set)
{
	memset(set->bitmap, 0xff,
	       idset_bitmap_size(set->num_ssid, set->num_id));
}

static inline void idset_add(struct idset *set, int ssid, int id)
{
	set_bit(ssid * set->num_id + id, set->bitmap);
}

static inline void idset_del(struct idset *set, int ssid, int id)
{
	clear_bit(ssid * set->num_id + id, set->bitmap);
}

static inline int idset_contains(struct idset *set, int ssid, int id)
{
	return test_bit(ssid * set->num_id + id, set->bitmap);
}

struct idset *idset_sch_new(void)
{
	return idset_new(max_ssid + 1, __MAX_SUBCHANNEL + 1);
}

void idset_sch_add(struct idset *set, struct subchannel_id schid)
{
	idset_add(set, schid.ssid, schid.sch_no);
}

void idset_sch_del(struct idset *set, struct subchannel_id schid)
{
	idset_del(set, schid.ssid, schid.sch_no);
}

/* Clear ids starting from @schid up to end of subchannel set. */
void idset_sch_del_subseq(struct idset *set, struct subchannel_id schid)
{
	int pos = schid.ssid * set->num_id + schid.sch_no;

	bitmap_clear(set->bitmap, pos, set->num_id - schid.sch_no);
}

int idset_sch_contains(struct idset *set, struct subchannel_id schid)
{
	return idset_contains(set, schid.ssid, schid.sch_no);
}

int idset_is_empty(struct idset *set)
{
	return bitmap_empty(set->bitmap, set->num_ssid * set->num_id);
}

void idset_add_set(struct idset *to, struct idset *from)
{
	int len = min(to->num_ssid * to->num_id, from->num_ssid * from->num_id);

	bitmap_or(to->bitmap, to->bitmap, from->bitmap, len);
}
