#include <linux/dma-mapping.h>
#include <asm/iommu_table.h>
#include <linux/string.h>
#include <linux/kallsyms.h>


#define DEBUG 1

static struct iommu_table_entry * __init
find_dependents_of(struct iommu_table_entry *start,
		   struct iommu_table_entry *finish,
		   struct iommu_table_entry *q)
{
	struct iommu_table_entry *p;

	if (!q)
		return NULL;

	for (p = start; p < finish; p++)
		if (p->detect == q->depend)
			return p;

	return NULL;
}


void __init sort_iommu_table(struct iommu_table_entry *start,
			     struct iommu_table_entry *finish) {

	struct iommu_table_entry *p, *q, tmp;

	for (p = start; p < finish; p++) {
again:
		q = find_dependents_of(start, finish, p);
		/* We are bit sneaky here. We use the memory address to figure
		 * out if the node we depend on is past our point, if so, swap.
		 */
		if (q > p) {
			tmp = *p;
			memmove(p, q, sizeof(*p));
			*q = tmp;
			goto again;
		}
	}

}

#ifdef DEBUG
void __init check_iommu_entries(struct iommu_table_entry *start,
				struct iommu_table_entry *finish)
{
	struct iommu_table_entry *p, *q, *x;
	char sym_p[KSYM_SYMBOL_LEN];
	char sym_q[KSYM_SYMBOL_LEN];

	/* Simple cyclic dependency checker. */
	for (p = start; p < finish; p++) {
		q = find_dependents_of(start, finish, p);
		x = find_dependents_of(start, finish, q);
		if (p == x) {
			sprint_symbol(sym_p, (unsigned long)p->detect);
			sprint_symbol(sym_q, (unsigned long)q->detect);

			printk(KERN_ERR "CYCLIC DEPENDENCY FOUND! %s depends" \
					" on %s and vice-versa. BREAKING IT.\n",
					sym_p, sym_q);
			/* Heavy handed way..*/
			x->depend = 0;
		}
	}

	for (p = start; p < finish; p++) {
		q = find_dependents_of(p, finish, p);
		if (q && q > p) {
			sprint_symbol(sym_p, (unsigned long)p->detect);
			sprint_symbol(sym_q, (unsigned long)q->detect);

			printk(KERN_ERR "EXECUTION ORDER INVALID! %s "\
					"should be called before %s!\n",
					sym_p, sym_q);
		}
	}
}
#else
inline void check_iommu_entries(struct iommu_table_entry *start,
				       struct iommu_table_entry *finish)
{
}
#endif
