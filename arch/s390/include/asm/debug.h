/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   S/390 debug facility
 *
 *    Copyright IBM Corp. 1999, 2020
 */
#ifndef _ASM_S390_DEBUG_H
#define _ASM_S390_DEBUG_H

#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/refcount.h>
#include <linux/fs.h>
#include <linux/init.h>

#define DEBUG_MAX_LEVEL		   6  /* debug levels range from 0 to 6 */
#define DEBUG_OFF_LEVEL		   -1 /* level where debug is switched off */
#define DEBUG_FLUSH_ALL		   -1 /* parameter to flush all areas */
#define DEBUG_MAX_VIEWS		   10 /* max number of views in proc fs */
#define DEBUG_MAX_NAME_LEN	   64 /* max length for a debugfs file name */
#define DEBUG_DEFAULT_LEVEL	   3  /* initial debug level */

#define DEBUG_DIR_ROOT "s390dbf" /* name of debug root directory in proc fs */

#define DEBUG_DATA(entry) (char *)(entry + 1) /* data is stored behind */
					      /* the entry information */

#define __DEBUG_FEATURE_VERSION	   3  /* version of debug feature */

struct __debug_entry {
	unsigned long clock	: 60;
	unsigned long exception	:  1;
	unsigned long level	:  3;
	void *caller;
	unsigned short cpu;
} __packed;

typedef struct __debug_entry debug_entry_t;

struct debug_view;

typedef struct debug_info {
	struct debug_info *next;
	struct debug_info *prev;
	refcount_t ref_count;
	spinlock_t lock;
	int level;
	int nr_areas;
	int pages_per_area;
	int buf_size;
	int entry_size;
	debug_entry_t ***areas;
	int active_area;
	int *active_pages;
	int *active_entries;
	struct dentry *debugfs_root_entry;
	struct dentry *debugfs_entries[DEBUG_MAX_VIEWS];
	struct debug_view *views[DEBUG_MAX_VIEWS];
	char name[DEBUG_MAX_NAME_LEN];
	umode_t mode;
} debug_info_t;

typedef int (debug_header_proc_t) (debug_info_t *id,
				   struct debug_view *view,
				   int area,
				   debug_entry_t *entry,
				   char *out_buf);

typedef int (debug_format_proc_t) (debug_info_t *id,
				   struct debug_view *view, char *out_buf,
				   const char *in_buf);
typedef int (debug_prolog_proc_t) (debug_info_t *id,
				   struct debug_view *view,
				   char *out_buf);
typedef int (debug_input_proc_t) (debug_info_t *id,
				  struct debug_view *view,
				  struct file *file,
				  const char __user *user_buf,
				  size_t in_buf_size, loff_t *offset);

int debug_dflt_header_fn(debug_info_t *id, struct debug_view *view,
			 int area, debug_entry_t *entry, char *out_buf);

struct debug_view {
	char name[DEBUG_MAX_NAME_LEN];
	debug_prolog_proc_t *prolog_proc;
	debug_header_proc_t *header_proc;
	debug_format_proc_t *format_proc;
	debug_input_proc_t  *input_proc;
	void		    *private_data;
};

extern struct debug_view debug_hex_ascii_view;
extern struct debug_view debug_sprintf_view;

/* do NOT use the _common functions */

debug_entry_t *debug_event_common(debug_info_t *id, int level,
				  const void *data, int length);

debug_entry_t *debug_exception_common(debug_info_t *id, int level,
				      const void *data, int length);

/* Debug Feature API: */

debug_info_t *debug_register(const char *name, int pages, int nr_areas,
			     int buf_size);

debug_info_t *debug_register_mode(const char *name, int pages, int nr_areas,
				  int buf_size, umode_t mode, uid_t uid,
				  gid_t gid);

void debug_unregister(debug_info_t *id);

void debug_set_level(debug_info_t *id, int new_level);

void debug_set_critical(void);

void debug_stop_all(void);

/**
 * debug_level_enabled() - Returns true if debug events for the specified
 *			   level would be logged. Otherwise returns false.
 *
 * @id:		handle for debug log
 * @level:	debug level
 *
 * Return:
 * - %true if level is less or equal to the current debug level.
 */
static inline bool debug_level_enabled(debug_info_t *id, int level)
{
	return level <= id->level;
}

/**
 * debug_event() - writes binary debug entry to active debug area
 *		   (if level <= actual debug level)
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @data:	pointer to data for debug entry
 * @length:	length of data in bytes
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_event(debug_info_t *id, int level,
					 void *data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_event_common(id, level, data, length);
}

/**
 * debug_int_event() - writes unsigned integer debug entry to active debug area
 *		       (if level <= actual debug level)
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @tag:	integer value for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_int_event(debug_info_t *id, int level,
					     unsigned int tag)
{
	unsigned int t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_event_common(id, level, &t, sizeof(unsigned int));
}

/**
 * debug_long_event() - writes unsigned long debug entry to active debug area
 *		       (if level <= actual debug level)
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @tag:	long integer value for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_long_event(debug_info_t *id, int level,
					      unsigned long tag)
{
	unsigned long t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_event_common(id, level, &t, sizeof(unsigned long));
}

/**
 * debug_text_event() - writes string debug entry in ascii format to active
 *			debug area (if level <= actual debug level)
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @txt:	string for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_text_event(debug_info_t *id, int level,
					      const char *txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_event_common(id, level, txt, strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.rst for more details!
 */
extern debug_entry_t *
__debug_sprintf_event(debug_info_t *id, int level, char *string, ...)
	__attribute__ ((format(printf, 3, 4)));

/**
 * debug_sprintf_event() - writes debug entry with format string
 *			   and varargs (longs) to active debug area
 *			   (if level $<=$ actual debug level).
 *
 * @_id:	handle for debug log
 * @_level:	debug level
 * @_fmt:	format string for debug entry
 * @...:	varargs used as in sprintf()
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 *
 * floats and long long datatypes cannot be used as varargs.
 */
#define debug_sprintf_event(_id, _level, _fmt, ...)			\
({									\
	debug_entry_t *__ret;						\
	debug_info_t *__id = _id;					\
	int __level = _level;						\
									\
	if ((!__id) || (__level > __id->level))				\
		__ret = NULL;						\
	else								\
		__ret = __debug_sprintf_event(__id, __level,		\
					      _fmt, ## __VA_ARGS__);	\
	__ret;								\
})

/**
 * debug_exception() - writes binary debug entry to active debug area
 *		       (if level <= actual debug level)
 *		       and switches to next debug area
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @data:	pointer to data for debug entry
 * @length:	length of data in bytes
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_exception(debug_info_t *id, int level,
					     void *data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_exception_common(id, level, data, length);
}

/**
 * debug_int_exception() - writes unsigned int debug entry to active debug area
 *			   (if level <= actual debug level)
 *			   and switches to next debug area
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @tag:	integer value for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_int_exception(debug_info_t *id, int level,
						 unsigned int tag)
{
	unsigned int t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_exception_common(id, level, &t, sizeof(unsigned int));
}

/**
 * debug_long_exception() - writes long debug entry to active debug area
 *			   (if level <= actual debug level)
 *			   and switches to next debug area
 *
 * @id:		handle for debug log
 * @level:	debug level
 * @tag:	long integer value for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_long_exception (debug_info_t *id, int level,
						   unsigned long tag)
{
	unsigned long t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_exception_common(id, level, &t, sizeof(unsigned long));
}

/**
 * debug_text_exception() - writes string debug entry in ascii format to active
 *			    debug area (if level <= actual debug level)
 *			    and switches to next debug area
 * area
 *
 * @id:	handle for debug log
 * @level:	debug level
 * @txt:	string for debug entry
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 */
static inline debug_entry_t *debug_text_exception(debug_info_t *id, int level,
						  const char *txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return debug_exception_common(id, level, txt, strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.rst for more details!
 */
extern debug_entry_t *
__debug_sprintf_exception(debug_info_t *id, int level, char *string, ...)
	__attribute__ ((format(printf, 3, 4)));


/**
 * debug_sprintf_exception() - writes debug entry with format string and
 *			       varargs (longs) to active debug area
 *			       (if level <= actual debug level)
 *			       and switches to next debug area.
 *
 * @_id:	handle for debug log
 * @_level:	debug level
 * @_fmt:	format string for debug entry
 * @...:	varargs used as in sprintf()
 *
 * Return:
 * - Address of written debug entry
 * - %NULL if error
 *
 * floats and long long datatypes cannot be used as varargs.
 */
#define debug_sprintf_exception(_id, _level, _fmt, ...)			\
({									\
	debug_entry_t *__ret;						\
	debug_info_t *__id = _id;					\
	int __level = _level;						\
									\
	if ((!__id) || (__level > __id->level))				\
		__ret = NULL;						\
	else								\
		__ret = __debug_sprintf_exception(__id, __level,	\
						  _fmt, ## __VA_ARGS__);\
	__ret;								\
})

int debug_register_view(debug_info_t *id, struct debug_view *view);

int debug_unregister_view(debug_info_t *id, struct debug_view *view);

#ifndef MODULE

/*
 * Note: Initial page and area numbers must be fixed to allow static
 * initialization. This enables very early tracing. Changes to these values
 * must be reflected in __DEFINE_STATIC_AREA.
 */
#define EARLY_PAGES		8
#define EARLY_AREAS		1

#define VNAME(var, suffix)	__##var##_##suffix

/*
 * Define static areas for early trace data. During boot debug_register_static()
 * will replace these with dynamically allocated areas to allow custom page and
 * area sizes, and dynamic resizing.
 */
#define __DEFINE_STATIC_AREA(var)					\
static char VNAME(var, data)[EARLY_PAGES][PAGE_SIZE] __initdata;	\
static debug_entry_t *VNAME(var, pages)[EARLY_PAGES] __initdata = {	\
	(debug_entry_t *)VNAME(var, data)[0],				\
	(debug_entry_t *)VNAME(var, data)[1],				\
	(debug_entry_t *)VNAME(var, data)[2],				\
	(debug_entry_t *)VNAME(var, data)[3],				\
	(debug_entry_t *)VNAME(var, data)[4],				\
	(debug_entry_t *)VNAME(var, data)[5],				\
	(debug_entry_t *)VNAME(var, data)[6],				\
	(debug_entry_t *)VNAME(var, data)[7],				\
};									\
static debug_entry_t **VNAME(var, areas)[EARLY_AREAS] __initdata = {	\
	(debug_entry_t **)VNAME(var, pages),				\
};									\
static int VNAME(var, active_pages)[EARLY_AREAS] __initdata;		\
static int VNAME(var, active_entries)[EARLY_AREAS] __initdata

#define __DEBUG_INFO_INIT(var, _name, _buf_size) {			\
	.next = NULL,							\
	.prev = NULL,							\
	.ref_count = REFCOUNT_INIT(1),					\
	.lock = __SPIN_LOCK_UNLOCKED(var.lock),				\
	.level = DEBUG_DEFAULT_LEVEL,					\
	.nr_areas = EARLY_AREAS,					\
	.pages_per_area = EARLY_PAGES,					\
	.buf_size = (_buf_size),					\
	.entry_size = sizeof(debug_entry_t) + (_buf_size),		\
	.areas = VNAME(var, areas),					\
	.active_area = 0,						\
	.active_pages = VNAME(var, active_pages),			\
	.active_entries = VNAME(var, active_entries),			\
	.debugfs_root_entry = NULL,					\
	.debugfs_entries = { NULL },					\
	.views = { NULL },						\
	.name = (_name),						\
	.mode = 0600,							\
}

#define __REGISTER_STATIC_DEBUG_INFO(var, name, pages, areas, view)	\
static int __init VNAME(var, reg)(void)					\
{									\
	debug_register_static(&var, (pages), (areas));			\
	debug_register_view(&var, (view));				\
	return 0;							\
}									\
arch_initcall(VNAME(var, reg))

/**
 * DEFINE_STATIC_DEBUG_INFO - Define static debug_info_t
 *
 * @var: Name of debug_info_t variable
 * @name: Name of debug log (e.g. used for debugfs entry)
 * @pages: Number of pages per area
 * @nr_areas: Number of debug areas
 * @buf_size: Size of data area in each debug entry
 * @view: Pointer to debug view struct
 *
 * Define a static debug_info_t for early tracing. The associated debugfs log
 * is automatically registered with the specified debug view.
 *
 * Important: Users of this macro must not call any of the
 * debug_register/_unregister() functions for this debug_info_t!
 *
 * Note: Tracing will start with a fixed number of initial pages and areas.
 * The debug area will be changed to use the specified numbers during
 * arch_initcall.
 */
#define DEFINE_STATIC_DEBUG_INFO(var, name, pages, nr_areas, buf_size, view) \
__DEFINE_STATIC_AREA(var);						\
static debug_info_t __refdata var =					\
	__DEBUG_INFO_INIT(var, (name), (buf_size));			\
__REGISTER_STATIC_DEBUG_INFO(var, name, pages, nr_areas, view)

void debug_register_static(debug_info_t *id, int pages_per_area, int nr_areas);

#endif /* MODULE */

#endif /* _ASM_S390_DEBUG_H */
