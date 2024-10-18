// SPDX-License-Identifier: GPL-2.0
/*
 *   S/390 debug facility
 *
 *    Copyright IBM Corp. 1999, 2020
 *
 *    Author(s): Michael Holzheu (holzheu@de.ibm.com),
 *		 Holger Smolinski (Holger.Smolinski@de.ibm.com)
 *
 *    Bugreports to: <Linux390@de.ibm.com>
 */

#define KMSG_COMPONENT "s390dbf"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/debugfs.h>

#include <asm/debug.h>

#define DEBUG_PROLOG_ENTRY -1

#define ALL_AREAS 0 /* copy all debug areas */
#define NO_AREAS  1 /* copy no debug areas */

/* typedefs */

typedef struct file_private_info {
	loff_t offset;			/* offset of last read in file */
	int    act_area;		/* number of last formatted area */
	int    act_page;		/* act page in given area */
	int    act_entry;		/* last formatted entry (offset */
					/* relative to beginning of last */
					/* formatted page) */
	size_t act_entry_offset;	/* up to this offset we copied */
					/* in last read the last formatted */
					/* entry to userland */
	char   temp_buf[2048];		/* buffer for output */
	debug_info_t *debug_info_org;	/* original debug information */
	debug_info_t *debug_info_snap;	/* snapshot of debug information */
	struct debug_view *view;	/* used view of debug info */
} file_private_info_t;

typedef struct {
	char *string;
	/*
	 * This assumes that all args are converted into longs
	 * on L/390 this is the case for all types of parameter
	 * except of floats, and long long (32 bit)
	 *
	 */
	long args[];
} debug_sprintf_entry_t;

/* internal function prototypes */

static int debug_init(void);
static ssize_t debug_output(struct file *file, char __user *user_buf,
			    size_t user_len, loff_t *offset);
static ssize_t debug_input(struct file *file, const char __user *user_buf,
			   size_t user_len, loff_t *offset);
static int debug_open(struct inode *inode, struct file *file);
static int debug_close(struct inode *inode, struct file *file);
static debug_info_t *debug_info_create(const char *name, int pages_per_area,
				       int nr_areas, int buf_size, umode_t mode);
static void debug_info_get(debug_info_t *);
static void debug_info_put(debug_info_t *);
static int debug_prolog_level_fn(debug_info_t *id,
				 struct debug_view *view, char *out_buf);
static int debug_input_level_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int debug_prolog_pages_fn(debug_info_t *id,
				 struct debug_view *view, char *out_buf);
static int debug_input_pages_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int debug_input_flush_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int debug_hex_ascii_format_fn(debug_info_t *id, struct debug_view *view,
				     char *out_buf, const char *in_buf);
static int debug_sprintf_format_fn(debug_info_t *id, struct debug_view *view,
				   char *out_buf, const char *inbuf);
static void debug_areas_swap(debug_info_t *a, debug_info_t *b);
static void debug_events_append(debug_info_t *dest, debug_info_t *src);

/* globals */

struct debug_view debug_hex_ascii_view = {
	"hex_ascii",
	NULL,
	&debug_dflt_header_fn,
	&debug_hex_ascii_format_fn,
	NULL,
	NULL
};
EXPORT_SYMBOL(debug_hex_ascii_view);

static struct debug_view debug_level_view = {
	"level",
	&debug_prolog_level_fn,
	NULL,
	NULL,
	&debug_input_level_fn,
	NULL
};

static struct debug_view debug_pages_view = {
	"pages",
	&debug_prolog_pages_fn,
	NULL,
	NULL,
	&debug_input_pages_fn,
	NULL
};

static struct debug_view debug_flush_view = {
	"flush",
	NULL,
	NULL,
	NULL,
	&debug_input_flush_fn,
	NULL
};

struct debug_view debug_sprintf_view = {
	"sprintf",
	NULL,
	&debug_dflt_header_fn,
	&debug_sprintf_format_fn,
	NULL,
	NULL
};
EXPORT_SYMBOL(debug_sprintf_view);

/* used by dump analysis tools to determine version of debug feature */
static unsigned int __used debug_feature_version = __DEBUG_FEATURE_VERSION;

/* static globals */

static debug_info_t *debug_area_first;
static debug_info_t *debug_area_last;
static DEFINE_MUTEX(debug_mutex);

static int initialized;
static int debug_critical;

static const struct file_operations debug_file_ops = {
	.owner	 = THIS_MODULE,
	.read	 = debug_output,
	.write	 = debug_input,
	.open	 = debug_open,
	.release = debug_close,
};

static struct dentry *debug_debugfs_root_entry;

/* functions */

/*
 * debug_areas_alloc
 * - Debug areas are implemented as a threedimensonal array:
 *   areas[areanumber][pagenumber][pageoffset]
 */

static debug_entry_t ***debug_areas_alloc(int pages_per_area, int nr_areas)
{
	debug_entry_t ***areas;
	int i, j;

	areas = kmalloc_array(nr_areas, sizeof(debug_entry_t **), GFP_KERNEL);
	if (!areas)
		goto fail_malloc_areas;
	for (i = 0; i < nr_areas; i++) {
		/* GFP_NOWARN to avoid user triggerable WARN, we handle fails */
		areas[i] = kmalloc_array(pages_per_area,
					 sizeof(debug_entry_t *),
					 GFP_KERNEL | __GFP_NOWARN);
		if (!areas[i])
			goto fail_malloc_areas2;
		for (j = 0; j < pages_per_area; j++) {
			areas[i][j] = kzalloc(PAGE_SIZE, GFP_KERNEL);
			if (!areas[i][j]) {
				for (j--; j >= 0 ; j--)
					kfree(areas[i][j]);
				kfree(areas[i]);
				goto fail_malloc_areas2;
			}
		}
	}
	return areas;

fail_malloc_areas2:
	for (i--; i >= 0; i--) {
		for (j = 0; j < pages_per_area; j++)
			kfree(areas[i][j]);
		kfree(areas[i]);
	}
	kfree(areas);
fail_malloc_areas:
	return NULL;
}

/*
 * debug_info_alloc
 * - alloc new debug-info
 */
static debug_info_t *debug_info_alloc(const char *name, int pages_per_area,
				      int nr_areas, int buf_size, int level,
				      int mode)
{
	debug_info_t *rc;

	/* alloc everything */
	rc = kmalloc(sizeof(debug_info_t), GFP_KERNEL);
	if (!rc)
		goto fail_malloc_rc;
	rc->active_entries = kcalloc(nr_areas, sizeof(int), GFP_KERNEL);
	if (!rc->active_entries)
		goto fail_malloc_active_entries;
	rc->active_pages = kcalloc(nr_areas, sizeof(int), GFP_KERNEL);
	if (!rc->active_pages)
		goto fail_malloc_active_pages;
	if ((mode == ALL_AREAS) && (pages_per_area != 0)) {
		rc->areas = debug_areas_alloc(pages_per_area, nr_areas);
		if (!rc->areas)
			goto fail_malloc_areas;
	} else {
		rc->areas = NULL;
	}

	/* initialize members */
	spin_lock_init(&rc->lock);
	rc->pages_per_area = pages_per_area;
	rc->nr_areas	   = nr_areas;
	rc->active_area    = 0;
	rc->level	   = level;
	rc->buf_size	   = buf_size;
	rc->entry_size	   = sizeof(debug_entry_t) + buf_size;
	strscpy(rc->name, name, sizeof(rc->name));
	memset(rc->views, 0, DEBUG_MAX_VIEWS * sizeof(struct debug_view *));
	memset(rc->debugfs_entries, 0, DEBUG_MAX_VIEWS * sizeof(struct dentry *));
	refcount_set(&(rc->ref_count), 0);

	return rc;

fail_malloc_areas:
	kfree(rc->active_pages);
fail_malloc_active_pages:
	kfree(rc->active_entries);
fail_malloc_active_entries:
	kfree(rc);
fail_malloc_rc:
	return NULL;
}

/*
 * debug_areas_free
 * - free all debug areas
 */
static void debug_areas_free(debug_info_t *db_info)
{
	int i, j;

	if (!db_info->areas)
		return;
	for (i = 0; i < db_info->nr_areas; i++) {
		for (j = 0; j < db_info->pages_per_area; j++)
			kfree(db_info->areas[i][j]);
		kfree(db_info->areas[i]);
	}
	kfree(db_info->areas);
	db_info->areas = NULL;
}

/*
 * debug_info_free
 * - free memory debug-info
 */
static void debug_info_free(debug_info_t *db_info)
{
	debug_areas_free(db_info);
	kfree(db_info->active_entries);
	kfree(db_info->active_pages);
	kfree(db_info);
}

/*
 * debug_info_create
 * - create new debug-info
 */

static debug_info_t *debug_info_create(const char *name, int pages_per_area,
				       int nr_areas, int buf_size, umode_t mode)
{
	debug_info_t *rc;

	rc = debug_info_alloc(name, pages_per_area, nr_areas, buf_size,
			      DEBUG_DEFAULT_LEVEL, ALL_AREAS);
	if (!rc)
		goto out;

	rc->mode = mode & ~S_IFMT;
	refcount_set(&rc->ref_count, 1);
out:
	return rc;
}

/*
 * debug_info_copy
 * - copy debug-info
 */
static debug_info_t *debug_info_copy(debug_info_t *in, int mode)
{
	unsigned long flags;
	debug_info_t *rc;
	int i, j;

	/* get a consistent copy of the debug areas */
	do {
		rc = debug_info_alloc(in->name, in->pages_per_area,
			in->nr_areas, in->buf_size, in->level, mode);
		spin_lock_irqsave(&in->lock, flags);
		if (!rc)
			goto out;
		/* has something changed in the meantime ? */
		if ((rc->pages_per_area == in->pages_per_area) &&
		    (rc->nr_areas == in->nr_areas)) {
			break;
		}
		spin_unlock_irqrestore(&in->lock, flags);
		debug_info_free(rc);
	} while (1);

	if (mode == NO_AREAS)
		goto out;

	for (i = 0; i < in->nr_areas; i++) {
		for (j = 0; j < in->pages_per_area; j++)
			memcpy(rc->areas[i][j], in->areas[i][j], PAGE_SIZE);
	}
out:
	spin_unlock_irqrestore(&in->lock, flags);
	return rc;
}

/*
 * debug_info_get
 * - increments reference count for debug-info
 */
static void debug_info_get(debug_info_t *db_info)
{
	if (db_info)
		refcount_inc(&db_info->ref_count);
}

/*
 * debug_info_put:
 * - decreases reference count for debug-info and frees it if necessary
 */
static void debug_info_put(debug_info_t *db_info)
{
	if (!db_info)
		return;
	if (refcount_dec_and_test(&db_info->ref_count))
		debug_info_free(db_info);
}

/*
 * debug_format_entry:
 * - format one debug entry and return size of formatted data
 */
static int debug_format_entry(file_private_info_t *p_info)
{
	debug_info_t *id_snap	= p_info->debug_info_snap;
	struct debug_view *view = p_info->view;
	debug_entry_t *act_entry;
	size_t len = 0;

	if (p_info->act_entry == DEBUG_PROLOG_ENTRY) {
		/* print prolog */
		if (view->prolog_proc)
			len += view->prolog_proc(id_snap, view, p_info->temp_buf);
		goto out;
	}
	if (!id_snap->areas) /* this is true, if we have a prolog only view */
		goto out;    /* or if 'pages_per_area' is 0 */
	act_entry = (debug_entry_t *) ((char *)id_snap->areas[p_info->act_area]
				       [p_info->act_page] + p_info->act_entry);

	if (act_entry->clock == 0LL)
		goto out; /* empty entry */
	if (view->header_proc)
		len += view->header_proc(id_snap, view, p_info->act_area,
					 act_entry, p_info->temp_buf + len);
	if (view->format_proc)
		len += view->format_proc(id_snap, view, p_info->temp_buf + len,
					 DEBUG_DATA(act_entry));
out:
	return len;
}

/*
 * debug_next_entry:
 * - goto next entry in p_info
 */
static inline int debug_next_entry(file_private_info_t *p_info)
{
	debug_info_t *id;

	id = p_info->debug_info_snap;
	if (p_info->act_entry == DEBUG_PROLOG_ENTRY) {
		p_info->act_entry = 0;
		p_info->act_page  = 0;
		goto out;
	}
	if (!id->areas)
		return 1;
	p_info->act_entry += id->entry_size;
	/* switch to next page, if we reached the end of the page  */
	if (p_info->act_entry > (PAGE_SIZE - id->entry_size)) {
		/* next page */
		p_info->act_entry = 0;
		p_info->act_page += 1;
		if ((p_info->act_page % id->pages_per_area) == 0) {
			/* next area */
			p_info->act_area++;
			p_info->act_page = 0;
		}
		if (p_info->act_area >= id->nr_areas)
			return 1;
	}
out:
	return 0;
}

/*
 * debug_output:
 * - called for user read()
 * - copies formatted debug entries to the user buffer
 */
static ssize_t debug_output(struct file *file,		/* file descriptor */
			    char __user *user_buf,	/* user buffer */
			    size_t len,			/* length of buffer */
			    loff_t *offset)		/* offset in the file */
{
	size_t count = 0;
	size_t entry_offset;
	file_private_info_t *p_info;

	p_info = (file_private_info_t *) file->private_data;
	if (*offset != p_info->offset)
		return -EPIPE;
	if (p_info->act_area >= p_info->debug_info_snap->nr_areas)
		return 0;
	entry_offset = p_info->act_entry_offset;
	while (count < len) {
		int formatted_line_residue;
		int formatted_line_size;
		int user_buf_residue;
		size_t copy_size;

		formatted_line_size = debug_format_entry(p_info);
		formatted_line_residue = formatted_line_size - entry_offset;
		user_buf_residue = len-count;
		copy_size = min(user_buf_residue, formatted_line_residue);
		if (copy_size) {
			if (copy_to_user(user_buf + count, p_info->temp_buf
					 + entry_offset, copy_size))
				return -EFAULT;
			count += copy_size;
			entry_offset += copy_size;
		}
		if (copy_size == formatted_line_residue) {
			entry_offset = 0;
			if (debug_next_entry(p_info))
				goto out;
		}
	}
out:
	p_info->offset		 = *offset + count;
	p_info->act_entry_offset = entry_offset;
	*offset = p_info->offset;
	return count;
}

/*
 * debug_input:
 * - called for user write()
 * - calls input function of view
 */
static ssize_t debug_input(struct file *file, const char __user *user_buf,
			   size_t length, loff_t *offset)
{
	file_private_info_t *p_info;
	int rc = 0;

	mutex_lock(&debug_mutex);
	p_info = ((file_private_info_t *) file->private_data);
	if (p_info->view->input_proc) {
		rc = p_info->view->input_proc(p_info->debug_info_org,
					      p_info->view, file, user_buf,
					      length, offset);
	} else {
		rc = -EPERM;
	}
	mutex_unlock(&debug_mutex);
	return rc; /* number of input characters */
}

/*
 * debug_open:
 * - called for user open()
 * - copies formatted output to private_data area of the file
 *   handle
 */
static int debug_open(struct inode *inode, struct file *file)
{
	debug_info_t *debug_info, *debug_info_snapshot;
	file_private_info_t *p_info;
	int i, rc = 0;

	mutex_lock(&debug_mutex);
	debug_info = file_inode(file)->i_private;
	/* find debug view */
	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (!debug_info->views[i])
			continue;
		else if (debug_info->debugfs_entries[i] == file->f_path.dentry)
			goto found; /* found view ! */
	}
	/* no entry found */
	rc = -EINVAL;
	goto out;

found:

	/* Make snapshot of current debug areas to get it consistent.	  */
	/* To copy all the areas is only needed, if we have a view which  */
	/* formats the debug areas. */

	if (!debug_info->views[i]->format_proc && !debug_info->views[i]->header_proc)
		debug_info_snapshot = debug_info_copy(debug_info, NO_AREAS);
	else
		debug_info_snapshot = debug_info_copy(debug_info, ALL_AREAS);

	if (!debug_info_snapshot) {
		rc = -ENOMEM;
		goto out;
	}
	p_info = kmalloc(sizeof(file_private_info_t), GFP_KERNEL);
	if (!p_info) {
		debug_info_free(debug_info_snapshot);
		rc = -ENOMEM;
		goto out;
	}
	p_info->offset = 0;
	p_info->debug_info_snap = debug_info_snapshot;
	p_info->debug_info_org	= debug_info;
	p_info->view = debug_info->views[i];
	p_info->act_area = 0;
	p_info->act_page = 0;
	p_info->act_entry = DEBUG_PROLOG_ENTRY;
	p_info->act_entry_offset = 0;
	file->private_data = p_info;
	debug_info_get(debug_info);
	nonseekable_open(inode, file);
out:
	mutex_unlock(&debug_mutex);
	return rc;
}

/*
 * debug_close:
 * - called for user close()
 * - deletes  private_data area of the file handle
 */
static int debug_close(struct inode *inode, struct file *file)
{
	file_private_info_t *p_info;

	p_info = (file_private_info_t *) file->private_data;
	if (p_info->debug_info_snap)
		debug_info_free(p_info->debug_info_snap);
	debug_info_put(p_info->debug_info_org);
	kfree(file->private_data);
	return 0; /* success */
}

/* Create debugfs entries and add to internal list. */
static void _debug_register(debug_info_t *id)
{
	/* create root directory */
	id->debugfs_root_entry = debugfs_create_dir(id->name,
						    debug_debugfs_root_entry);

	/* append new element to linked list */
	if (!debug_area_first) {
		/* first element in list */
		debug_area_first = id;
		id->prev = NULL;
	} else {
		/* append element to end of list */
		debug_area_last->next = id;
		id->prev = debug_area_last;
	}
	debug_area_last = id;
	id->next = NULL;

	debug_register_view(id, &debug_level_view);
	debug_register_view(id, &debug_flush_view);
	debug_register_view(id, &debug_pages_view);
}

/**
 * debug_register_mode() - creates and initializes debug area.
 *
 * @name:	Name of debug log (e.g. used for debugfs entry)
 * @pages_per_area:	Number of pages, which will be allocated per area
 * @nr_areas:	Number of debug areas
 * @buf_size:	Size of data area in each debug entry
 * @mode:	File mode for debugfs files. E.g. S_IRWXUGO
 * @uid:	User ID for debugfs files. Currently only 0 is supported.
 * @gid:	Group ID for debugfs files. Currently only 0 is supported.
 *
 * Return:
 * - Handle for generated debug area
 * - %NULL if register failed
 *
 * Allocates memory for a debug log.
 * Must not be called within an interrupt handler.
 */
debug_info_t *debug_register_mode(const char *name, int pages_per_area,
				  int nr_areas, int buf_size, umode_t mode,
				  uid_t uid, gid_t gid)
{
	debug_info_t *rc = NULL;

	/* Since debugfs currently does not support uid/gid other than root, */
	/* we do not allow gid/uid != 0 until we get support for that. */
	if ((uid != 0) || (gid != 0))
		pr_warn("Root becomes the owner of all s390dbf files in sysfs\n");
	BUG_ON(!initialized);

	/* create new debug_info */
	rc = debug_info_create(name, pages_per_area, nr_areas, buf_size, mode);
	if (rc) {
		mutex_lock(&debug_mutex);
		_debug_register(rc);
		mutex_unlock(&debug_mutex);
	} else {
		pr_err("Registering debug feature %s failed\n", name);
	}
	return rc;
}
EXPORT_SYMBOL(debug_register_mode);

/**
 * debug_register() - creates and initializes debug area with default file mode.
 *
 * @name:	Name of debug log (e.g. used for debugfs entry)
 * @pages_per_area:	Number of pages, which will be allocated per area
 * @nr_areas:	Number of debug areas
 * @buf_size:	Size of data area in each debug entry
 *
 * Return:
 * - Handle for generated debug area
 * - %NULL if register failed
 *
 * Allocates memory for a debug log.
 * The debugfs file mode access permissions are read and write for user.
 * Must not be called within an interrupt handler.
 */
debug_info_t *debug_register(const char *name, int pages_per_area,
			     int nr_areas, int buf_size)
{
	return debug_register_mode(name, pages_per_area, nr_areas, buf_size,
				   S_IRUSR | S_IWUSR, 0, 0);
}
EXPORT_SYMBOL(debug_register);

/**
 * debug_register_static() - registers a static debug area
 *
 * @id: Handle for static debug area
 * @pages_per_area: Number of pages per area
 * @nr_areas: Number of debug areas
 *
 * Register debug_info_t defined using DEFINE_STATIC_DEBUG_INFO.
 *
 * Note: This function is called automatically via an initcall generated by
 *	 DEFINE_STATIC_DEBUG_INFO.
 */
void debug_register_static(debug_info_t *id, int pages_per_area, int nr_areas)
{
	unsigned long flags;
	debug_info_t *copy;

	if (!initialized) {
		pr_err("Tried to register debug feature %s too early\n",
		       id->name);
		return;
	}

	copy = debug_info_alloc("", pages_per_area, nr_areas, id->buf_size,
				id->level, ALL_AREAS);
	if (!copy) {
		pr_err("Registering debug feature %s failed\n", id->name);

		/* Clear pointers to prevent tracing into released initdata. */
		spin_lock_irqsave(&id->lock, flags);
		id->areas = NULL;
		id->active_pages = NULL;
		id->active_entries = NULL;
		spin_unlock_irqrestore(&id->lock, flags);

		return;
	}

	/* Replace static trace area with dynamic copy. */
	spin_lock_irqsave(&id->lock, flags);
	debug_events_append(copy, id);
	debug_areas_swap(id, copy);
	spin_unlock_irqrestore(&id->lock, flags);

	/* Clear pointers to initdata and discard copy. */
	copy->areas = NULL;
	copy->active_pages = NULL;
	copy->active_entries = NULL;
	debug_info_free(copy);

	mutex_lock(&debug_mutex);
	_debug_register(id);
	mutex_unlock(&debug_mutex);
}

/* Remove debugfs entries and remove from internal list. */
static void _debug_unregister(debug_info_t *id)
{
	int i;

	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (!id->views[i])
			continue;
		debugfs_remove(id->debugfs_entries[i]);
	}
	debugfs_remove(id->debugfs_root_entry);
	if (id == debug_area_first)
		debug_area_first = id->next;
	if (id == debug_area_last)
		debug_area_last = id->prev;
	if (id->prev)
		id->prev->next = id->next;
	if (id->next)
		id->next->prev = id->prev;
}

/**
 * debug_unregister() - give back debug area.
 *
 * @id:		handle for debug log
 *
 * Return:
 *    none
 */
void debug_unregister(debug_info_t *id)
{
	if (!id)
		return;
	mutex_lock(&debug_mutex);
	_debug_unregister(id);
	mutex_unlock(&debug_mutex);

	debug_info_put(id);
}
EXPORT_SYMBOL(debug_unregister);

/*
 * debug_set_size:
 * - set area size (number of pages) and number of areas
 */
static int debug_set_size(debug_info_t *id, int nr_areas, int pages_per_area)
{
	debug_info_t *new_id;
	unsigned long flags;

	if (!id || (nr_areas <= 0) || (pages_per_area < 0))
		return -EINVAL;

	new_id = debug_info_alloc("", pages_per_area, nr_areas, id->buf_size,
				  id->level, ALL_AREAS);
	if (!new_id) {
		pr_info("Allocating memory for %i pages failed\n",
			pages_per_area);
		return -ENOMEM;
	}

	spin_lock_irqsave(&id->lock, flags);
	debug_events_append(new_id, id);
	debug_areas_swap(new_id, id);
	debug_info_free(new_id);
	spin_unlock_irqrestore(&id->lock, flags);
	pr_info("%s: set new size (%i pages)\n", id->name, pages_per_area);

	return 0;
}

/**
 * debug_set_level() - Sets new actual debug level if new_level is valid.
 *
 * @id:		handle for debug log
 * @new_level:	new debug level
 *
 * Return:
 *    none
 */
void debug_set_level(debug_info_t *id, int new_level)
{
	unsigned long flags;

	if (!id)
		return;

	if (new_level == DEBUG_OFF_LEVEL) {
		pr_info("%s: switched off\n", id->name);
	} else if ((new_level > DEBUG_MAX_LEVEL) || (new_level < 0)) {
		pr_info("%s: level %i is out of range (%i - %i)\n",
			id->name, new_level, 0, DEBUG_MAX_LEVEL);
		return;
	}

	spin_lock_irqsave(&id->lock, flags);
	id->level = new_level;
	spin_unlock_irqrestore(&id->lock, flags);
}
EXPORT_SYMBOL(debug_set_level);

/*
 * proceed_active_entry:
 * - set active entry to next in the ring buffer
 */
static inline void proceed_active_entry(debug_info_t *id)
{
	if ((id->active_entries[id->active_area] += id->entry_size)
	    > (PAGE_SIZE - id->entry_size)) {
		id->active_entries[id->active_area] = 0;
		id->active_pages[id->active_area] =
			(id->active_pages[id->active_area] + 1) %
			id->pages_per_area;
	}
}

/*
 * proceed_active_area:
 * - set active area to next in the ring buffer
 */
static inline void proceed_active_area(debug_info_t *id)
{
	id->active_area++;
	id->active_area = id->active_area % id->nr_areas;
}

/*
 * get_active_entry:
 */
static inline debug_entry_t *get_active_entry(debug_info_t *id)
{
	return (debug_entry_t *) (((char *) id->areas[id->active_area]
				   [id->active_pages[id->active_area]]) +
				  id->active_entries[id->active_area]);
}

/* Swap debug areas of a and b. */
static void debug_areas_swap(debug_info_t *a, debug_info_t *b)
{
	swap(a->nr_areas, b->nr_areas);
	swap(a->pages_per_area, b->pages_per_area);
	swap(a->areas, b->areas);
	swap(a->active_area, b->active_area);
	swap(a->active_pages, b->active_pages);
	swap(a->active_entries, b->active_entries);
}

/* Append all debug events in active area from source to destination log. */
static void debug_events_append(debug_info_t *dest, debug_info_t *src)
{
	debug_entry_t *from, *to, *last;

	if (!src->areas || !dest->areas)
		return;

	/* Loop over all entries in src, starting with oldest. */
	from = get_active_entry(src);
	last = from;
	do {
		if (from->clock != 0LL) {
			to = get_active_entry(dest);
			memset(to, 0, dest->entry_size);
			memcpy(to, from, min(src->entry_size,
					     dest->entry_size));
			proceed_active_entry(dest);
		}

		proceed_active_entry(src);
		from = get_active_entry(src);
	} while (from != last);
}

/*
 * debug_finish_entry:
 * - set timestamp, caller address, cpu number etc.
 */

static inline void debug_finish_entry(debug_info_t *id, debug_entry_t *active,
				      int level, int exception)
{
	unsigned long timestamp;
	union tod_clock clk;

	store_tod_clock_ext(&clk);
	timestamp = clk.us;
	timestamp -= TOD_UNIX_EPOCH >> 12;
	active->clock = timestamp;
	active->cpu = smp_processor_id();
	active->caller = __builtin_return_address(0);
	active->exception = exception;
	active->level = level;
	proceed_active_entry(id);
	if (exception)
		proceed_active_area(id);
}

static int debug_stoppable = 1;
static int debug_active = 1;

#define CTL_S390DBF_STOPPABLE 5678
#define CTL_S390DBF_ACTIVE 5679

/*
 * proc handler for the running debug_active sysctl
 * always allow read, allow write only if debug_stoppable is set or
 * if debug_active is already off
 */
static int s390dbf_procactive(const struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!write || debug_stoppable || !debug_active)
		return proc_dointvec(table, write, buffer, lenp, ppos);
	else
		return 0;
}

static struct ctl_table s390dbf_table[] = {
	{
		.procname	= "debug_stoppable",
		.data		= &debug_stoppable,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "debug_active",
		.data		= &debug_active,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= s390dbf_procactive,
	},
};

static struct ctl_table_header *s390dbf_sysctl_header;

/**
 * debug_stop_all() - stops the debug feature if stopping is allowed.
 *
 * Return:
 * -   none
 *
 * Currently used in case of a kernel oops.
 */
void debug_stop_all(void)
{
	if (debug_stoppable)
		debug_active = 0;
}
EXPORT_SYMBOL(debug_stop_all);

/**
 * debug_set_critical() - event/exception functions try lock instead of spin.
 *
 * Return:
 * -   none
 *
 * Currently used in case of stopping all CPUs but the current one.
 * Once in this state, functions to write a debug entry for an
 * event or exception no longer spin on the debug area lock,
 * but only try to get it and fail if they do not get the lock.
 */
void debug_set_critical(void)
{
	debug_critical = 1;
}

/*
 * debug_event_common:
 * - write debug entry with given size
 */
debug_entry_t *debug_event_common(debug_info_t *id, int level, const void *buf,
				  int len)
{
	debug_entry_t *active;
	unsigned long flags;

	if (!debug_active || !id->areas)
		return NULL;
	if (debug_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	do {
		active = get_active_entry(id);
		memcpy(DEBUG_DATA(active), buf, min(len, id->buf_size));
		if (len < id->buf_size)
			memset((DEBUG_DATA(active)) + len, 0, id->buf_size - len);
		debug_finish_entry(id, active, level, 0);
		len -= id->buf_size;
		buf += id->buf_size;
	} while (len > 0);

	spin_unlock_irqrestore(&id->lock, flags);
	return active;
}
EXPORT_SYMBOL(debug_event_common);

/*
 * debug_exception_common:
 * - write debug entry with given size and switch to next debug area
 */
debug_entry_t *debug_exception_common(debug_info_t *id, int level,
				      const void *buf, int len)
{
	debug_entry_t *active;
	unsigned long flags;

	if (!debug_active || !id->areas)
		return NULL;
	if (debug_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	do {
		active = get_active_entry(id);
		memcpy(DEBUG_DATA(active), buf, min(len, id->buf_size));
		if (len < id->buf_size)
			memset((DEBUG_DATA(active)) + len, 0, id->buf_size - len);
		debug_finish_entry(id, active, level, len <= id->buf_size);
		len -= id->buf_size;
		buf += id->buf_size;
	} while (len > 0);

	spin_unlock_irqrestore(&id->lock, flags);
	return active;
}
EXPORT_SYMBOL(debug_exception_common);

/*
 * counts arguments in format string for sprintf view
 */
static inline int debug_count_numargs(char *string)
{
	int numargs = 0;

	while (*string) {
		if (*string++ == '%')
			numargs++;
	}
	return numargs;
}

/*
 * debug_sprintf_event:
 */
debug_entry_t *__debug_sprintf_event(debug_info_t *id, int level, char *string, ...)
{
	debug_sprintf_entry_t *curr_event;
	debug_entry_t *active;
	unsigned long flags;
	int numargs, idx;
	va_list ap;

	if (!debug_active || !id->areas)
		return NULL;
	numargs = debug_count_numargs(string);

	if (debug_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	active = get_active_entry(id);
	curr_event = (debug_sprintf_entry_t *) DEBUG_DATA(active);
	va_start(ap, string);
	curr_event->string = string;
	for (idx = 0; idx < min(numargs, (int)(id->buf_size / sizeof(long)) - 1); idx++)
		curr_event->args[idx] = va_arg(ap, long);
	va_end(ap);
	debug_finish_entry(id, active, level, 0);
	spin_unlock_irqrestore(&id->lock, flags);

	return active;
}
EXPORT_SYMBOL(__debug_sprintf_event);

/*
 * debug_sprintf_exception:
 */
debug_entry_t *__debug_sprintf_exception(debug_info_t *id, int level, char *string, ...)
{
	debug_sprintf_entry_t *curr_event;
	debug_entry_t *active;
	unsigned long flags;
	int numargs, idx;
	va_list ap;

	if (!debug_active || !id->areas)
		return NULL;

	numargs = debug_count_numargs(string);

	if (debug_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	active = get_active_entry(id);
	curr_event = (debug_sprintf_entry_t *)DEBUG_DATA(active);
	va_start(ap, string);
	curr_event->string = string;
	for (idx = 0; idx < min(numargs, (int)(id->buf_size / sizeof(long)) - 1); idx++)
		curr_event->args[idx] = va_arg(ap, long);
	va_end(ap);
	debug_finish_entry(id, active, level, 1);
	spin_unlock_irqrestore(&id->lock, flags);

	return active;
}
EXPORT_SYMBOL(__debug_sprintf_exception);

/**
 * debug_register_view() - registers new debug view and creates debugfs
 *			   dir entry
 *
 * @id:		handle for debug log
 * @view:	pointer to debug view struct
 *
 * Return:
 * -   0  : ok
 * -   < 0: Error
 */
int debug_register_view(debug_info_t *id, struct debug_view *view)
{
	unsigned long flags;
	struct dentry *pde;
	umode_t mode;
	int rc = 0;
	int i;

	if (!id)
		goto out;
	mode = (id->mode | S_IFREG) & ~S_IXUGO;
	if (!(view->prolog_proc || view->format_proc || view->header_proc))
		mode &= ~(S_IRUSR | S_IRGRP | S_IROTH);
	if (!view->input_proc)
		mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
	pde = debugfs_create_file(view->name, mode, id->debugfs_root_entry,
				  id, &debug_file_ops);
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (!id->views[i])
			break;
	}
	if (i == DEBUG_MAX_VIEWS) {
		rc = -1;
	} else {
		id->views[i] = view;
		id->debugfs_entries[i] = pde;
	}
	spin_unlock_irqrestore(&id->lock, flags);
	if (rc) {
		pr_err("Registering view %s/%s would exceed the maximum "
		       "number of views %i\n", id->name, view->name, i);
		debugfs_remove(pde);
	}
out:
	return rc;
}
EXPORT_SYMBOL(debug_register_view);

/**
 * debug_unregister_view() - unregisters debug view and removes debugfs
 *			     dir entry
 *
 * @id:		handle for debug log
 * @view:	pointer to debug view struct
 *
 * Return:
 * -   0  : ok
 * -   < 0: Error
 */
int debug_unregister_view(debug_info_t *id, struct debug_view *view)
{
	struct dentry *dentry = NULL;
	unsigned long flags;
	int i, rc = 0;

	if (!id)
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (id->views[i] == view)
			break;
	}
	if (i == DEBUG_MAX_VIEWS) {
		rc = -1;
	} else {
		dentry = id->debugfs_entries[i];
		id->views[i] = NULL;
		id->debugfs_entries[i] = NULL;
	}
	spin_unlock_irqrestore(&id->lock, flags);
	debugfs_remove(dentry);
out:
	return rc;
}
EXPORT_SYMBOL(debug_unregister_view);

static inline char *debug_get_user_string(const char __user *user_buf,
					  size_t user_len)
{
	char *buffer;

	buffer = kmalloc(user_len + 1, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(buffer, user_buf, user_len) != 0) {
		kfree(buffer);
		return ERR_PTR(-EFAULT);
	}
	/* got the string, now strip linefeed. */
	if (buffer[user_len - 1] == '\n')
		buffer[user_len - 1] = 0;
	else
		buffer[user_len] = 0;
	return buffer;
}

static inline int debug_get_uint(char *buf)
{
	int rc;

	buf = skip_spaces(buf);
	rc = simple_strtoul(buf, &buf, 10);
	if (*buf)
		rc = -EINVAL;
	return rc;
}

/*
 * functions for debug-views
 ***********************************
*/

/*
 * prints out actual debug level
 */

static int debug_prolog_pages_fn(debug_info_t *id, struct debug_view *view,
				 char *out_buf)
{
	return sprintf(out_buf, "%i\n", id->pages_per_area);
}

/*
 * reads new size (number of pages per debug area)
 */

static int debug_input_pages_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_len, loff_t *offset)
{
	int rc, new_pages;
	char *str;

	if (user_len > 0x10000)
		user_len = 0x10000;
	if (*offset != 0) {
		rc = -EPIPE;
		goto out;
	}
	str = debug_get_user_string(user_buf, user_len);
	if (IS_ERR(str)) {
		rc = PTR_ERR(str);
		goto out;
	}
	new_pages = debug_get_uint(str);
	if (new_pages < 0) {
		rc = -EINVAL;
		goto free_str;
	}
	rc = debug_set_size(id, id->nr_areas, new_pages);
	if (rc != 0) {
		rc = -EINVAL;
		goto free_str;
	}
	rc = user_len;
free_str:
	kfree(str);
out:
	*offset += user_len;
	return rc;		/* number of input characters */
}

/*
 * prints out actual debug level
 */
static int debug_prolog_level_fn(debug_info_t *id, struct debug_view *view,
				 char *out_buf)
{
	int rc = 0;

	if (id->level == DEBUG_OFF_LEVEL)
		rc = sprintf(out_buf, "-\n");
	else
		rc = sprintf(out_buf, "%i\n", id->level);
	return rc;
}

/*
 * reads new debug level
 */
static int debug_input_level_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_len, loff_t *offset)
{
	int rc, new_level;
	char *str;

	if (user_len > 0x10000)
		user_len = 0x10000;
	if (*offset != 0) {
		rc = -EPIPE;
		goto out;
	}
	str = debug_get_user_string(user_buf, user_len);
	if (IS_ERR(str)) {
		rc = PTR_ERR(str);
		goto out;
	}
	if (str[0] == '-') {
		debug_set_level(id, DEBUG_OFF_LEVEL);
		rc = user_len;
		goto free_str;
	} else {
		new_level = debug_get_uint(str);
	}
	if (new_level < 0) {
		pr_warn("%s is not a valid level for a debug feature\n", str);
		rc = -EINVAL;
	} else {
		debug_set_level(id, new_level);
		rc = user_len;
	}
free_str:
	kfree(str);
out:
	*offset += user_len;
	return rc;		/* number of input characters */
}

/*
 * flushes debug areas
 */
static void debug_flush(debug_info_t *id, int area)
{
	unsigned long flags;
	int i, j;

	if (!id || !id->areas)
		return;
	spin_lock_irqsave(&id->lock, flags);
	if (area == DEBUG_FLUSH_ALL) {
		id->active_area = 0;
		memset(id->active_entries, 0, id->nr_areas * sizeof(int));
		for (i = 0; i < id->nr_areas; i++) {
			id->active_pages[i] = 0;
			for (j = 0; j < id->pages_per_area; j++)
				memset(id->areas[i][j], 0, PAGE_SIZE);
		}
	} else if (area >= 0 && area < id->nr_areas) {
		id->active_entries[area] = 0;
		id->active_pages[area] = 0;
		for (i = 0; i < id->pages_per_area; i++)
			memset(id->areas[area][i], 0, PAGE_SIZE);
	}
	spin_unlock_irqrestore(&id->lock, flags);
}

/*
 * view function: flushes debug areas
 */
static int debug_input_flush_fn(debug_info_t *id, struct debug_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_len, loff_t *offset)
{
	char input_buf[1];
	int rc = user_len;

	if (user_len > 0x10000)
		user_len = 0x10000;
	if (*offset != 0) {
		rc = -EPIPE;
		goto out;
	}
	if (copy_from_user(input_buf, user_buf, 1)) {
		rc = -EFAULT;
		goto out;
	}
	if (input_buf[0] == '-') {
		debug_flush(id, DEBUG_FLUSH_ALL);
		goto out;
	}
	if (isdigit(input_buf[0])) {
		int area = ((int) input_buf[0] - (int) '0');

		debug_flush(id, area);
		goto out;
	}

	pr_info("Flushing debug data failed because %c is not a valid "
		 "area\n", input_buf[0]);

out:
	*offset += user_len;
	return rc;		/* number of input characters */
}

/*
 * prints debug data in hex/ascii format
 */
static int debug_hex_ascii_format_fn(debug_info_t *id, struct debug_view *view,
				     char *out_buf, const char *in_buf)
{
	int i, rc = 0;

	for (i = 0; i < id->buf_size; i++)
		rc += sprintf(out_buf + rc, "%02x ", ((unsigned char *) in_buf)[i]);
	rc += sprintf(out_buf + rc, "| ");
	for (i = 0; i < id->buf_size; i++) {
		unsigned char c = in_buf[i];

		if (isascii(c) && isprint(c))
			rc += sprintf(out_buf + rc, "%c", c);
		else
			rc += sprintf(out_buf + rc, ".");
	}
	rc += sprintf(out_buf + rc, "\n");
	return rc;
}

/*
 * prints header for debug entry
 */
int debug_dflt_header_fn(debug_info_t *id, struct debug_view *view,
			 int area, debug_entry_t *entry, char *out_buf)
{
	unsigned long sec, usec;
	unsigned long caller;
	unsigned int level;
	char *except_str;
	int rc = 0;

	level = entry->level;
	sec = entry->clock;
	usec = do_div(sec, USEC_PER_SEC);

	if (entry->exception)
		except_str = "*";
	else
		except_str = "-";
	caller = (unsigned long) entry->caller;
	rc += sprintf(out_buf, "%02i %011ld:%06lu %1u %1s %04u %px  ",
		      area, sec, usec, level, except_str,
		      entry->cpu, (void *)caller);
	return rc;
}
EXPORT_SYMBOL(debug_dflt_header_fn);

/*
 * prints debug data sprintf-formatted:
 * debug_sprinf_event/exception calls must be used together with this view
 */

#define DEBUG_SPRINTF_MAX_ARGS 10

static int debug_sprintf_format_fn(debug_info_t *id, struct debug_view *view,
				   char *out_buf, const char *inbuf)
{
	debug_sprintf_entry_t *curr_event = (debug_sprintf_entry_t *)inbuf;
	int num_longs, num_used_args = 0, i, rc = 0;
	int index[DEBUG_SPRINTF_MAX_ARGS];

	/* count of longs fit into one entry */
	num_longs = id->buf_size / sizeof(long);

	if (num_longs < 1)
		goto out; /* bufsize of entry too small */
	if (num_longs == 1) {
		/* no args, we use only the string */
		strcpy(out_buf, curr_event->string);
		rc = strlen(curr_event->string);
		goto out;
	}

	/* number of arguments used for sprintf (without the format string) */
	num_used_args = min(DEBUG_SPRINTF_MAX_ARGS, (num_longs - 1));

	memset(index, 0, DEBUG_SPRINTF_MAX_ARGS * sizeof(int));

	for (i = 0; i < num_used_args; i++)
		index[i] = i;

	rc = sprintf(out_buf, curr_event->string, curr_event->args[index[0]],
		     curr_event->args[index[1]], curr_event->args[index[2]],
		     curr_event->args[index[3]], curr_event->args[index[4]],
		     curr_event->args[index[5]], curr_event->args[index[6]],
		     curr_event->args[index[7]], curr_event->args[index[8]],
		     curr_event->args[index[9]]);
out:
	return rc;
}

/*
 * debug_init:
 * - is called exactly once to initialize the debug feature
 */
static int __init debug_init(void)
{
	s390dbf_sysctl_header = register_sysctl("s390dbf", s390dbf_table);
	mutex_lock(&debug_mutex);
	debug_debugfs_root_entry = debugfs_create_dir(DEBUG_DIR_ROOT, NULL);
	initialized = 1;
	mutex_unlock(&debug_mutex);
	return 0;
}
postcore_initcall(debug_init);
