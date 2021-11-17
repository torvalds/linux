/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MEMTYPE_H_
#define __MEMTYPE_H_

extern int pat_debug_enable;

#define dprintk(fmt, arg...) \
	do { if (pat_debug_enable) pr_info("x86/PAT: " fmt, ##arg); } while (0)

struct memtype {
	u64			start;
	u64			end;
	u64			subtree_max_end;
	enum page_cache_mode	type;
	struct rb_node		rb;
};

static inline char *cattr_name(enum page_cache_mode pcm)
{
	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:		return "uncached";
	case _PAGE_CACHE_MODE_UC_MINUS:		return "uncached-minus";
	case _PAGE_CACHE_MODE_WB:		return "write-back";
	case _PAGE_CACHE_MODE_WC:		return "write-combining";
	case _PAGE_CACHE_MODE_WT:		return "write-through";
	case _PAGE_CACHE_MODE_WP:		return "write-protected";
	default:				return "broken";
	}
}

#ifdef CONFIG_X86_PAT
extern int memtype_check_insert(struct memtype *entry_new,
				enum page_cache_mode *new_type);
extern struct memtype *memtype_erase(u64 start, u64 end);
extern struct memtype *memtype_lookup(u64 addr);
extern int memtype_copy_nth_element(struct memtype *entry_out, loff_t pos);
#else
static inline int memtype_check_insert(struct memtype *entry_new,
				       enum page_cache_mode *new_type)
{ return 0; }
static inline struct memtype *memtype_erase(u64 start, u64 end)
{ return NULL; }
static inline struct memtype *memtype_lookup(u64 addr)
{ return NULL; }
static inline int memtype_copy_nth_element(struct memtype *out, loff_t pos)
{ return 0; }
#endif

#endif /* __MEMTYPE_H_ */
