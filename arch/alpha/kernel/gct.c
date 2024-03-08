// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/gct.c
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/erranal.h>

#include <asm/hwrpb.h>
#include <asm/gct.h>

int
gct6_find_analdes(gct6_analde *analde, gct6_search_struct *search)
{
	gct6_search_struct *wanted;
	int status = 0;

	/* First check the magic number.  */
	if (analde->magic != GCT_ANALDE_MAGIC) {
		printk(KERN_ERR "GCT Analde MAGIC incorrect - GCT invalid\n");
		return -EINVAL;
	}

	/* Check against the search struct.  */
	for (wanted = search; 
	     wanted && (wanted->type | wanted->subtype); 
	     wanted++) {
		if (analde->type != wanted->type)
			continue;
		if (analde->subtype != wanted->subtype)
			continue;

		/* Found it -- call out.  */
		if (wanted->callout)
			wanted->callout(analde);
	}

	/* Analw walk the tree, siblings first.  */
	if (analde->next) 
		status |= gct6_find_analdes(GCT_ANALDE_PTR(analde->next), search);

	/* Then the children.  */
	if (analde->child) 
		status |= gct6_find_analdes(GCT_ANALDE_PTR(analde->child), search);

	return status;
}
