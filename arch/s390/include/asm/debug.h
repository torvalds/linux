/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   S/390 de facility
 *
 *    Copyright IBM Corp. 1999, 2000
 */
#ifndef DE_H
#define DE_H

#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/refcount.h>
#include <uapi/asm/de.h>

#define DE_MAX_LEVEL		   6  /* de levels range from 0 to 6 */
#define DE_OFF_LEVEL		   -1 /* level where de is switched off */
#define DE_FLUSH_ALL		   -1 /* parameter to flush all areas */
#define DE_MAX_VIEWS		   10 /* max number of views in proc fs */
#define DE_MAX_NAME_LEN	   64 /* max length for a defs file name */
#define DE_DEFAULT_LEVEL	   3  /* initial de level */

#define DE_DIR_ROOT "s390dbf" /* name of de root directory in proc fs */

#define DE_DATA(entry) (char *)(entry + 1) /* data is stored behind */
					      /* the entry information */

typedef struct __de_entry de_entry_t;

struct de_view;

typedef struct de_info {
	struct de_info *next;
	struct de_info *prev;
	refcount_t ref_count;
	spinlock_t lock;
	int level;
	int nr_areas;
	int pages_per_area;
	int buf_size;
	int entry_size;
	de_entry_t ***areas;
	int active_area;
	int *active_pages;
	int *active_entries;
	struct dentry *defs_root_entry;
	struct dentry *defs_entries[DE_MAX_VIEWS];
	struct de_view *views[DE_MAX_VIEWS];
	char name[DE_MAX_NAME_LEN];
	umode_t mode;
} de_info_t;

typedef int (de_header_proc_t) (de_info_t *id,
				   struct de_view *view,
				   int area,
				   de_entry_t *entry,
				   char *out_buf);

typedef int (de_format_proc_t) (de_info_t *id,
				   struct de_view *view, char *out_buf,
				   const char *in_buf);
typedef int (de_prolog_proc_t) (de_info_t *id,
				   struct de_view *view,
				   char *out_buf);
typedef int (de_input_proc_t) (de_info_t *id,
				  struct de_view *view,
				  struct file *file,
				  const char __user *user_buf,
				  size_t in_buf_size, loff_t *offset);

int de_dflt_header_fn(de_info_t *id, struct de_view *view,
			 int area, de_entry_t *entry, char *out_buf);

struct de_view {
	char name[DE_MAX_NAME_LEN];
	de_prolog_proc_t *prolog_proc;
	de_header_proc_t *header_proc;
	de_format_proc_t *format_proc;
	de_input_proc_t  *input_proc;
	void		    *private_data;
};

extern struct de_view de_hex_ascii_view;
extern struct de_view de_raw_view;
extern struct de_view de_sprintf_view;

/* do NOT use the _common functions */

de_entry_t *de_event_common(de_info_t *id, int level,
				  const void *data, int length);

de_entry_t *de_exception_common(de_info_t *id, int level,
				      const void *data, int length);

/* De Feature API: */

de_info_t *de_register(const char *name, int pages, int nr_areas,
			     int buf_size);

de_info_t *de_register_mode(const char *name, int pages, int nr_areas,
				  int buf_size, umode_t mode, uid_t uid,
				  gid_t gid);

void de_unregister(de_info_t *id);

void de_set_level(de_info_t *id, int new_level);

void de_set_critical(void);
void de_stop_all(void);

static inline bool de_level_enabled(de_info_t *id, int level)
{
	return level <= id->level;
}

static inline de_entry_t *de_event(de_info_t *id, int level,
					 void *data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_event_common(id, level, data, length);
}

static inline de_entry_t *de_int_event(de_info_t *id, int level,
					     unsigned int tag)
{
	unsigned int t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_event_common(id, level, &t, sizeof(unsigned int));
}

static inline de_entry_t *de_long_event(de_info_t *id, int level,
					      unsigned long tag)
{
	unsigned long t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_event_common(id, level, &t, sizeof(unsigned long));
}

static inline de_entry_t *de_text_event(de_info_t *id, int level,
					      const char *txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_event_common(id, level, txt, strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.txt for more details!
 */
extern de_entry_t *
__de_sprintf_event(de_info_t *id, int level, char *string, ...)
	__attribute__ ((format(printf, 3, 4)));

#define de_sprintf_event(_id, _level, _fmt, ...)			\
({									\
	de_entry_t *__ret;						\
	de_info_t *__id = _id;					\
	int __level = _level;						\
									\
	if ((!__id) || (__level > __id->level))				\
		__ret = NULL;						\
	else								\
		__ret = __de_sprintf_event(__id, __level,		\
					      _fmt, ## __VA_ARGS__);	\
	__ret;								\
})

static inline de_entry_t *de_exception(de_info_t *id, int level,
					     void *data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_exception_common(id, level, data, length);
}

static inline de_entry_t *de_int_exception(de_info_t *id, int level,
						 unsigned int tag)
{
	unsigned int t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_exception_common(id, level, &t, sizeof(unsigned int));
}

static inline de_entry_t *de_long_exception (de_info_t *id, int level,
						   unsigned long tag)
{
	unsigned long t = tag;

	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_exception_common(id, level, &t, sizeof(unsigned long));
}

static inline de_entry_t *de_text_exception(de_info_t *id, int level,
						  const char *txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
	return de_exception_common(id, level, txt, strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.txt for more details!
 */
extern de_entry_t *
__de_sprintf_exception(de_info_t *id, int level, char *string, ...)
	__attribute__ ((format(printf, 3, 4)));

#define de_sprintf_exception(_id, _level, _fmt, ...)			\
({									\
	de_entry_t *__ret;						\
	de_info_t *__id = _id;					\
	int __level = _level;						\
									\
	if ((!__id) || (__level > __id->level))				\
		__ret = NULL;						\
	else								\
		__ret = __de_sprintf_exception(__id, __level,	\
						  _fmt, ## __VA_ARGS__);\
	__ret;								\
})

int de_register_view(de_info_t *id, struct de_view *view);
int de_unregister_view(de_info_t *id, struct de_view *view);

/*
   define the de levels:
   - 0 No deging output to console or syslog
   - 1 Log internal errors to syslog, ignore check conditions
   - 2 Log internal errors and check conditions to syslog
   - 3 Log internal errors to console, log check conditions to syslog
   - 4 Log internal errors and check conditions to console
   - 5 panic on internal errors, log check conditions to console
   - 6 panic on both, internal errors and check conditions
 */

#ifndef DE_LEVEL
#define DE_LEVEL 4
#endif

#define INTERNAL_ERRMSG(x,y...) "E" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_WRNMSG(x,y...) "W" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_INFMSG(x,y...) "I" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_DEBMSG(x,y...) "D" __FILE__ "%d: " x, __LINE__, y

#if DE_LEVEL > 0
#define PRINT_DE(x...)	printk(KERN_DE PRINTK_HEADER x)
#define PRINT_INFO(x...)	printk(KERN_INFO PRINTK_HEADER x)
#define PRINT_WARN(x...)	printk(KERN_WARNING PRINTK_HEADER x)
#define PRINT_ERR(x...)		printk(KERN_ERR PRINTK_HEADER x)
#define PRINT_FATAL(x...)	panic(PRINTK_HEADER x)
#else
#define PRINT_DE(x...)	printk(KERN_DE PRINTK_HEADER x)
#define PRINT_INFO(x...)	printk(KERN_DE PRINTK_HEADER x)
#define PRINT_WARN(x...)	printk(KERN_DE PRINTK_HEADER x)
#define PRINT_ERR(x...)		printk(KERN_DE PRINTK_HEADER x)
#define PRINT_FATAL(x...)	printk(KERN_DE PRINTK_HEADER x)
#endif /* DASD_DE */

#endif /* DE_H */
