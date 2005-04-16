/*
 *	linux/arch/alpha/kernel/gct.c
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>

#include <asm/hwrpb.h>
#include <asm/gct.h>

int
gct6_find_nodes(gct6_node *node, gct6_search_struct *search)
{
	gct6_search_struct *wanted;
	int status = 0;

	/* First check the magic number.  */
	if (node->magic != GCT_NODE_MAGIC) {
		printk(KERN_ERR "GCT Node MAGIC incorrect - GCT invalid\n");
		return -EINVAL;
	}

	/* Check against the search struct.  */
	for (wanted = search; 
	     wanted && (wanted->type | wanted->subtype); 
	     wanted++) {
		if (node->type != wanted->type)
			continue;
		if (node->subtype != wanted->subtype)
			continue;

		/* Found it -- call out.  */
		if (wanted->callout)
			wanted->callout(node);
	}

	/* Now walk the tree, siblings first.  */
	if (node->next) 
		status |= gct6_find_nodes(GCT_NODE_PTR(node->next), search);

	/* Then the children.  */
	if (node->child) 
		status |= gct6_find_nodes(GCT_NODE_PTR(node->child), search);

	return status;
}
