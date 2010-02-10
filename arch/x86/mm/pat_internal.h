#ifndef __PAT_INTERNAL_H_
#define __PAT_INTERNAL_H_

extern int pat_debug_enable;

#define dprintk(fmt, arg...) \
	do { if (pat_debug_enable) printk(KERN_INFO fmt, ##arg); } while (0)

struct memtype {
	u64			start;
	u64			end;
	unsigned long		type;
	struct list_head	nd;
	struct rb_node		rb;
};

static inline char *cattr_name(unsigned long flags)
{
	switch (flags & _PAGE_CACHE_MASK) {
	case _PAGE_CACHE_UC:		return "uncached";
	case _PAGE_CACHE_UC_MINUS:	return "uncached-minus";
	case _PAGE_CACHE_WB:		return "write-back";
	case _PAGE_CACHE_WC:		return "write-combining";
	default:			return "broken";
	}
}

#endif /* __PAT_INTERNAL_H_ */
