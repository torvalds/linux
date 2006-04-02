/*
 * General bootinfo record utilities
 * Author: Randy Vinson <rvinson@mvista.com>
 *
 * 2002 (c) MontaVista Software, Inc. This file is licensed under the terms
 * of the GNU General Public License version 2. This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <asm/bootinfo.h>

#include "nonstdio.h"

static struct bi_record * birec = NULL;

static struct bi_record *
__bootinfo_build(struct bi_record *rec, unsigned long tag, unsigned long size,
		 void *data)
{
	/* set the tag */
	rec->tag = tag;

	/* if the caller has any data, copy it */
	if (size)
		memcpy(rec->data, (char *)data, size);

	/* set the record size */
	rec->size = sizeof(struct bi_record) + size;

	/* advance to the next available space */
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	return rec;
}

void
bootinfo_init(struct bi_record *rec)
{

	/* save start of birec area */
	birec = rec;

	/* create an empty list */
	rec = __bootinfo_build(rec, BI_FIRST, 0, NULL);
	(void) __bootinfo_build(rec, BI_LAST, 0, NULL);

}

void
bootinfo_append(unsigned long tag, unsigned long size, void * data)
{

	struct bi_record *rec = birec;

	/* paranoia */
	if ((rec == NULL) || (rec->tag != BI_FIRST))
		return;

	/* find the last entry in the list */
	while (rec->tag != BI_LAST)
		rec = (struct bi_record *)((ulong)rec + rec->size);

	/* overlay BI_LAST record with new one and tag on a new BI_LAST */
	rec = __bootinfo_build(rec, tag, size, data);
	(void) __bootinfo_build(rec, BI_LAST, 0, NULL);
}
