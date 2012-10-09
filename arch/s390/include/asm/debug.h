/*
 *   S/390 debug facility
 *
 *    Copyright IBM Corp. 1999, 2000
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <linux/fs.h>

/* Note:
 * struct __debug_entry must be defined outside of #ifdef __KERNEL__ 
 * in order to allow a user program to analyze the 'raw'-view.
 */

struct __debug_entry{
        union {
                struct {
                        unsigned long long clock:52;
                        unsigned long long exception:1;
                        unsigned long long level:3;
                        unsigned long long cpuid:8;
                } fields;

                unsigned long long stck;
        } id;
        void* caller;
} __attribute__((packed));


#define __DEBUG_FEATURE_VERSION      2  /* version of debug feature */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/time.h>

#define DEBUG_MAX_LEVEL            6  /* debug levels range from 0 to 6 */
#define DEBUG_OFF_LEVEL            -1 /* level where debug is switched off */
#define DEBUG_FLUSH_ALL            -1 /* parameter to flush all areas */
#define DEBUG_MAX_VIEWS            10 /* max number of views in proc fs */
#define DEBUG_MAX_NAME_LEN         64 /* max length for a debugfs file name */
#define DEBUG_DEFAULT_LEVEL        3  /* initial debug level */

#define DEBUG_DIR_ROOT "s390dbf" /* name of debug root directory in proc fs */

#define DEBUG_DATA(entry) (char*)(entry + 1) /* data is stored behind */
                                             /* the entry information */

typedef struct __debug_entry debug_entry_t;

struct debug_view;

typedef struct debug_info {	
	struct debug_info* next;
	struct debug_info* prev;
	atomic_t ref_count;
	spinlock_t lock;			
	int level;
	int nr_areas;
	int pages_per_area;
	int buf_size;
	int entry_size;	
	debug_entry_t*** areas;
	int active_area;
	int *active_pages;
	int *active_entries;
	struct dentry* debugfs_root_entry;
	struct dentry* debugfs_entries[DEBUG_MAX_VIEWS];
	struct debug_view* views[DEBUG_MAX_VIEWS];	
	char name[DEBUG_MAX_NAME_LEN];
	umode_t mode;
} debug_info_t;

typedef int (debug_header_proc_t) (debug_info_t* id,
				   struct debug_view* view,
				   int area,
				   debug_entry_t* entry,
				   char* out_buf);

typedef int (debug_format_proc_t) (debug_info_t* id,
				   struct debug_view* view, char* out_buf,
				   const char* in_buf);
typedef int (debug_prolog_proc_t) (debug_info_t* id,
				   struct debug_view* view,
				   char* out_buf);
typedef int (debug_input_proc_t) (debug_info_t* id,
				  struct debug_view* view,
				  struct file* file,
				  const char __user *user_buf,
				  size_t in_buf_size, loff_t* offset);

int debug_dflt_header_fn(debug_info_t* id, struct debug_view* view,
		         int area, debug_entry_t* entry, char* out_buf);						
				
struct debug_view {
	char name[DEBUG_MAX_NAME_LEN];
	debug_prolog_proc_t* prolog_proc;
	debug_header_proc_t* header_proc;
	debug_format_proc_t* format_proc;
	debug_input_proc_t*  input_proc;
	void*                private_data;
};

extern struct debug_view debug_hex_ascii_view;
extern struct debug_view debug_raw_view;
extern struct debug_view debug_sprintf_view;

/* do NOT use the _common functions */

debug_entry_t* debug_event_common(debug_info_t* id, int level, 
                                  const void* data, int length);

debug_entry_t* debug_exception_common(debug_info_t* id, int level, 
                                      const void* data, int length);

/* Debug Feature API: */

debug_info_t *debug_register(const char *name, int pages, int nr_areas,
                             int buf_size);

debug_info_t *debug_register_mode(const char *name, int pages, int nr_areas,
				  int buf_size, umode_t mode, uid_t uid,
				  gid_t gid);

void debug_unregister(debug_info_t* id);

void debug_set_level(debug_info_t* id, int new_level);

void debug_set_critical(void);
void debug_stop_all(void);

static inline debug_entry_t*
debug_event(debug_info_t* id, int level, void* data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_event_common(id,level,data,length);
}

static inline debug_entry_t*
debug_int_event(debug_info_t* id, int level, unsigned int tag)
{
        unsigned int t=tag;
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_event_common(id,level,&t,sizeof(unsigned int));
}

static inline debug_entry_t *
debug_long_event (debug_info_t* id, int level, unsigned long tag)
{
        unsigned long t=tag;
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_event_common(id,level,&t,sizeof(unsigned long));
}

static inline debug_entry_t*
debug_text_event(debug_info_t* id, int level, const char* txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_event_common(id,level,txt,strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.txt for more details!
 */
extern debug_entry_t *
debug_sprintf_event(debug_info_t* id,int level,char *string,...)
	__attribute__ ((format(printf, 3, 4)));


static inline debug_entry_t*
debug_exception(debug_info_t* id, int level, void* data, int length)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_exception_common(id,level,data,length);
}

static inline debug_entry_t*
debug_int_exception(debug_info_t* id, int level, unsigned int tag)
{
        unsigned int t=tag;
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_exception_common(id,level,&t,sizeof(unsigned int));
}

static inline debug_entry_t *
debug_long_exception (debug_info_t* id, int level, unsigned long tag)
{
        unsigned long t=tag;
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_exception_common(id,level,&t,sizeof(unsigned long));
}

static inline debug_entry_t*
debug_text_exception(debug_info_t* id, int level, const char* txt)
{
	if ((!id) || (level > id->level) || (id->pages_per_area == 0))
		return NULL;
        return debug_exception_common(id,level,txt,strlen(txt));
}

/*
 * IMPORTANT: Use "%s" in sprintf format strings with care! Only pointers are
 * stored in the s390dbf. See Documentation/s390/s390dbf.txt for more details!
 */
extern debug_entry_t *
debug_sprintf_exception(debug_info_t* id,int level,char *string,...)
	__attribute__ ((format(printf, 3, 4)));

int debug_register_view(debug_info_t* id, struct debug_view* view);
int debug_unregister_view(debug_info_t* id, struct debug_view* view);

/*
   define the debug levels:
   - 0 No debugging output to console or syslog
   - 1 Log internal errors to syslog, ignore check conditions 
   - 2 Log internal errors and check conditions to syslog
   - 3 Log internal errors to console, log check conditions to syslog
   - 4 Log internal errors and check conditions to console
   - 5 panic on internal errors, log check conditions to console
   - 6 panic on both, internal errors and check conditions
 */

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 4
#endif

#define INTERNAL_ERRMSG(x,y...) "E" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_WRNMSG(x,y...) "W" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_INFMSG(x,y...) "I" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_DEBMSG(x,y...) "D" __FILE__ "%d: " x, __LINE__, y

#if DEBUG_LEVEL > 0
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_INFO PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_WARNING PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_ERR PRINTK_HEADER x )
#define PRINT_FATAL(x...) panic ( PRINTK_HEADER x )
#else
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_FATAL(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#endif				/* DASD_DEBUG */

#endif				/* __KERNEL__ */
#endif				/* DEBUG_H */
