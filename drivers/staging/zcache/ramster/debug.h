#include <linux/bug.h>

#ifdef CONFIG_RAMSTER_DEBUG

extern long ramster_flnodes;
static atomic_t ramster_flnodes_atomic = ATOMIC_INIT(0);
static unsigned long ramster_flnodes_max;
static inline void inc_ramster_flnodes(void)
{
	ramster_flnodes = atomic_inc_return(&ramster_flnodes_atomic);
	if (ramster_flnodes > ramster_flnodes_max)
		ramster_flnodes_max = ramster_flnodes;
}
static inline void dec_ramster_flnodes(void)
{
	ramster_flnodes = atomic_dec_return(&ramster_flnodes_atomic);
}
extern ssize_t ramster_foreign_eph_pages;
static atomic_t ramster_foreign_eph_pages_atomic = ATOMIC_INIT(0);
static ssize_t ramster_foreign_eph_pages_max;
static inline void inc_ramster_foreign_eph_pages(void)
{
	ramster_foreign_eph_pages = atomic_inc_return(
		&ramster_foreign_eph_pages_atomic);
	if (ramster_foreign_eph_pages > ramster_foreign_eph_pages_max)
		ramster_foreign_eph_pages_max = ramster_foreign_eph_pages;
}
static inline void dec_ramster_foreign_eph_pages(void)
{
	ramster_foreign_eph_pages = atomic_dec_return(
		&ramster_foreign_eph_pages_atomic);
}
extern ssize_t ramster_foreign_pers_pages;
static atomic_t ramster_foreign_pers_pages_atomic = ATOMIC_INIT(0);
static ssize_t ramster_foreign_pers_pages_max;
static inline void inc_ramster_foreign_pers_pages(void)
{
	ramster_foreign_pers_pages = atomic_inc_return(
		&ramster_foreign_pers_pages_atomic);
	if (ramster_foreign_pers_pages > ramster_foreign_pers_pages_max)
		ramster_foreign_pers_pages_max = ramster_foreign_pers_pages;
}
static inline void dec_ramster_foreign_pers_pages(void)
{
	ramster_foreign_pers_pages = atomic_dec_return(
		&ramster_foreign_pers_pages_atomic);
}

extern ssize_t ramster_eph_pages_remoted;
extern ssize_t ramster_pers_pages_remoted;
extern ssize_t ramster_eph_pages_remote_failed;
extern ssize_t ramster_pers_pages_remote_failed;
extern ssize_t ramster_remote_eph_pages_succ_get;
extern ssize_t ramster_remote_pers_pages_succ_get;
extern ssize_t ramster_remote_eph_pages_unsucc_get;
extern ssize_t ramster_remote_pers_pages_unsucc_get;
extern ssize_t ramster_pers_pages_remote_nomem;
extern ssize_t ramster_remote_objects_flushed;
extern ssize_t ramster_remote_object_flushes_failed;
extern ssize_t ramster_remote_pages_flushed;
extern ssize_t ramster_remote_page_flushes_failed;

int ramster_debugfs_init(void);

static inline void inc_ramster_eph_pages_remoted(void)
{
	ramster_eph_pages_remoted++;
};
static inline void inc_ramster_pers_pages_remoted(void)
{
	ramster_pers_pages_remoted++;
};
static inline void inc_ramster_eph_pages_remote_failed(void)
{
	ramster_eph_pages_remote_failed++;
};
static inline void inc_ramster_pers_pages_remote_failed(void)
{
	ramster_pers_pages_remote_failed++;
};
static inline void inc_ramster_remote_eph_pages_succ_get(void)
{
	ramster_remote_eph_pages_succ_get++;
};
static inline void inc_ramster_remote_pers_pages_succ_get(void)
{
	ramster_remote_pers_pages_succ_get++;
};
static inline void inc_ramster_remote_eph_pages_unsucc_get(void)
{
	ramster_remote_eph_pages_unsucc_get++;
};
static inline void inc_ramster_remote_pers_pages_unsucc_get(void)
{
	ramster_remote_pers_pages_unsucc_get++;
};
static inline void inc_ramster_pers_pages_remote_nomem(void)
{
	ramster_pers_pages_remote_nomem++;
};
static inline void inc_ramster_remote_objects_flushed(void)
{
	ramster_remote_objects_flushed++;
};
static inline void inc_ramster_remote_object_flushes_failed(void)
{
	ramster_remote_object_flushes_failed++;
};
static inline void inc_ramster_remote_pages_flushed(void)
{
	ramster_remote_pages_flushed++;
};
static inline void inc_ramster_remote_page_flushes_failed(void)
{
	ramster_remote_page_flushes_failed++;
};

#else

static inline void inc_ramster_flnodes(void) { };
static inline void dec_ramster_flnodes(void) { };
static inline void inc_ramster_foreign_eph_pages(void) { };
static inline void dec_ramster_foreign_eph_pages(void) { };
static inline void inc_ramster_foreign_pers_pages(void) { };
static inline void dec_ramster_foreign_pers_pages(void) { };

static inline void inc_ramster_eph_pages_remoted(void) { };
static inline void inc_ramster_pers_pages_remoted(void) { };
static inline void inc_ramster_eph_pages_remote_failed(void) { };
static inline void inc_ramster_pers_pages_remote_failed(void) { };
static inline void inc_ramster_remote_eph_pages_succ_get(void) { };
static inline void inc_ramster_remote_pers_pages_succ_get(void) { };
static inline void inc_ramster_remote_eph_pages_unsucc_get(void) { };
static inline void inc_ramster_remote_pers_pages_unsucc_get(void) { };
static inline void inc_ramster_pers_pages_remote_nomem(void) { };
static inline void inc_ramster_remote_objects_flushed(void) { };
static inline void inc_ramster_remote_object_flushes_failed(void) { };
static inline void inc_ramster_remote_pages_flushed(void) { };
static inline void inc_ramster_remote_page_flushes_failed(void) { };

static inline int ramster_debugfs_init(void)
{
	return 0;
}
#endif
