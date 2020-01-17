// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/gct.c
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/erryes.h>

#include <asm/hwrpb.h>
#include <asm/gct.h>

int
gct6_find_yesdes(gct6_yesde *yesde, gct6_search_struct *search)
{
	gct6_search_struct *wanted;
	int status = 0;

	/* First check the magic number.  */
	if (yesde->magic != GCT_NODE_MAGIC) {
		printk(KERN_ERR "GCT Node MAGIC incorrect - GCT invalid\n");
		return -EINVAL;
	}

	/* Check against the search struct.  */
	for (wanted = search; 
	     wanted && (wanted->type | wanted->subtype); 
	     wanted++) {
		if (yesde->type != wanted->type)
			continue;
		if (yesde->subtype != wanted->subtype)
			continue;

		/* Found it -- call out.  */
		if (wanted->callout)
			wanted->callout(yesde);
	}

	/* Now walk the tree, siblings first.  */
	if (yesde->next) 
		status |= gct6_find_yesdes(GCT_NODE_PTR(yesde->next), search);

	/* Then the children.  */
	if (yesde->child) 
		status |= gct6_find_yesdes(GCT_NODE_PTR(yesde->child), search);

	return status;
}
