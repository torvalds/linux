// SPDX-License-Identifier: GPL-2.0
/*
 *   S/390 de facility
 *
 *    Copyright IBM Corp. 1999, 2012
 *
 *    Author(s): Michael Holzheu (holzheu@de.ibm.com),
 *		 Holger Smolinski (Holger.Smolinski@de.ibm.com)
 *
 *    reports to: <Linux390@de.ibm.com>
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
#include <linux/defs.h>

#include <asm/de.h>

#define DE_PROLOG_ENTRY -1

#define ALL_AREAS 0 /* copy all de areas */
#define NO_AREAS  1 /* copy no de areas */

/* typedefs */

typedef struct file_private_info {
	loff_t offset;			/* offset of last read in file */
	int    act_area;		/* number of last formated area */
	int    act_page;		/* act page in given area */
	int    act_entry;		/* last formated entry (offset */
					/* relative to beginning of last */
					/* formated page) */
	size_t act_entry_offset;	/* up to this offset we copied */
					/* in last read the last formated */
					/* entry to userland */
	char   temp_buf[2048];		/* buffer for output */
	de_info_t *de_info_org;	/* original de information */
	de_info_t *de_info_snap;	/* snapshot of de information */
	struct de_view *view;	/* used view of de info */
} file_private_info_t;

typedef struct {
	char *string;
	/*
	 * This assumes that all args are converted into longs
	 * on L/390 this is the case for all types of parameter
	 * except of floats, and long long (32 bit)
	 *
	 */
	long args[0];
} de_sprintf_entry_t;

/* internal function prototyes */

static int de_init(void);
static ssize_t de_output(struct file *file, char __user *user_buf,
			    size_t user_len, loff_t *offset);
static ssize_t de_input(struct file *file, const char __user *user_buf,
			   size_t user_len, loff_t *offset);
static int de_open(struct inode *inode, struct file *file);
static int de_close(struct inode *inode, struct file *file);
static de_info_t *de_info_create(const char *name, int pages_per_area,
				       int nr_areas, int buf_size, umode_t mode);
static void de_info_get(de_info_t *);
static void de_info_put(de_info_t *);
static int de_prolog_level_fn(de_info_t *id,
				 struct de_view *view, char *out_buf);
static int de_input_level_fn(de_info_t *id, struct de_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int de_prolog_pages_fn(de_info_t *id,
				 struct de_view *view, char *out_buf);
static int de_input_pages_fn(de_info_t *id, struct de_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int de_input_flush_fn(de_info_t *id, struct de_view *view,
				struct file *file, const char __user *user_buf,
				size_t user_buf_size, loff_t *offset);
static int de_hex_ascii_format_fn(de_info_t *id, struct de_view *view,
				     char *out_buf, const char *in_buf);
static int de_raw_format_fn(de_info_t *id,
			       struct de_view *view, char *out_buf,
			       const char *in_buf);
static int de_raw_header_fn(de_info_t *id, struct de_view *view,
			       int area, de_entry_t *entry, char *out_buf);

static int de_sprintf_format_fn(de_info_t *id, struct de_view *view,
				   char *out_buf, de_sprintf_entry_t *curr_event);

/* globals */

struct de_view de_raw_view = {
	"raw",
	NULL,
	&de_raw_header_fn,
	&de_raw_format_fn,
	NULL,
	NULL
};
EXPORT_SYMBOL(de_raw_view);

struct de_view de_hex_ascii_view = {
	"hex_ascii",
	NULL,
	&de_dflt_header_fn,
	&de_hex_ascii_format_fn,
	NULL,
	NULL
};
EXPORT_SYMBOL(de_hex_ascii_view);

static struct de_view de_level_view = {
	"level",
	&de_prolog_level_fn,
	NULL,
	NULL,
	&de_input_level_fn,
	NULL
};

static struct de_view de_pages_view = {
	"pages",
	&de_prolog_pages_fn,
	NULL,
	NULL,
	&de_input_pages_fn,
	NULL
};

static struct de_view de_flush_view = {
	"flush",
	NULL,
	NULL,
	NULL,
	&de_input_flush_fn,
	NULL
};

struct de_view de_sprintf_view = {
	"sprintf",
	NULL,
	&de_dflt_header_fn,
	(de_format_proc_t *)&de_sprintf_format_fn,
	NULL,
	NULL
};
EXPORT_SYMBOL(de_sprintf_view);

/* used by dump analysis tools to determine version of de feature */
static unsigned int __used de_feature_version = __DE_FEATURE_VERSION;

/* static globals */

static de_info_t *de_area_first;
static de_info_t *de_area_last;
static DEFINE_MUTEX(de_mutex);

static int initialized;
static int de_critical;

static const struct file_operations de_file_ops = {
	.owner	 = THIS_MODULE,
	.read	 = de_output,
	.write	 = de_input,
	.open	 = de_open,
	.release = de_close,
	.llseek  = no_llseek,
};

static struct dentry *de_defs_root_entry;

/* functions */

/*
 * de_areas_alloc
 * - De areas are implemented as a threedimensonal array:
 *   areas[areanumber][pagenumber][pageoffset]
 */

static de_entry_t ***de_areas_alloc(int pages_per_area, int nr_areas)
{
	de_entry_t ***areas;
	int i, j;

	areas = kmalloc_array(nr_areas, sizeof(de_entry_t **), GFP_KERNEL);
	if (!areas)
		goto fail_malloc_areas;
	for (i = 0; i < nr_areas; i++) {
		areas[i] = kmalloc_array(pages_per_area,
					 sizeof(de_entry_t *),
					 GFP_KERNEL);
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
 * de_info_alloc
 * - alloc new de-info
 */
static de_info_t *de_info_alloc(const char *name, int pages_per_area,
				      int nr_areas, int buf_size, int level,
				      int mode)
{
	de_info_t *rc;

	/* alloc everything */
	rc = kmalloc(sizeof(de_info_t), GFP_KERNEL);
	if (!rc)
		goto fail_malloc_rc;
	rc->active_entries = kcalloc(nr_areas, sizeof(int), GFP_KERNEL);
	if (!rc->active_entries)
		goto fail_malloc_active_entries;
	rc->active_pages = kcalloc(nr_areas, sizeof(int), GFP_KERNEL);
	if (!rc->active_pages)
		goto fail_malloc_active_pages;
	if ((mode == ALL_AREAS) && (pages_per_area != 0)) {
		rc->areas = de_areas_alloc(pages_per_area, nr_areas);
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
	rc->entry_size	   = sizeof(de_entry_t) + buf_size;
	strlcpy(rc->name, name, sizeof(rc->name));
	memset(rc->views, 0, DE_MAX_VIEWS * sizeof(struct de_view *));
	memset(rc->defs_entries, 0, DE_MAX_VIEWS * sizeof(struct dentry *));
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
 * de_areas_free
 * - free all de areas
 */
static void de_areas_free(de_info_t *db_info)
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
 * de_info_free
 * - free memory de-info
 */
static void de_info_free(de_info_t *db_info)
{
	de_areas_free(db_info);
	kfree(db_info->active_entries);
	kfree(db_info->active_pages);
	kfree(db_info);
}

/*
 * de_info_create
 * - create new de-info
 */

static de_info_t *de_info_create(const char *name, int pages_per_area,
				       int nr_areas, int buf_size, umode_t mode)
{
	de_info_t *rc;

	rc = de_info_alloc(name, pages_per_area, nr_areas, buf_size,
			      DE_DEFAULT_LEVEL, ALL_AREAS);
	if (!rc)
		goto out;

	rc->mode = mode & ~S_IFMT;

	/* create root directory */
	rc->defs_root_entry = defs_create_dir(rc->name,
						    de_defs_root_entry);

	/* append new element to linked list */
	if (!de_area_first) {
		/* first element in list */
		de_area_first = rc;
		rc->prev = NULL;
	} else {
		/* append element to end of list */
		de_area_last->next = rc;
		rc->prev = de_area_last;
	}
	de_area_last = rc;
	rc->next = NULL;

	refcount_set(&rc->ref_count, 1);
out:
	return rc;
}

/*
 * de_info_copy
 * - copy de-info
 */
static de_info_t *de_info_copy(de_info_t *in, int mode)
{
	unsigned long flags;
	de_info_t *rc;
	int i, j;

	/* get a consistent copy of the de areas */
	do {
		rc = de_info_alloc(in->name, in->pages_per_area,
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
		de_info_free(rc);
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
 * de_info_get
 * - increments reference count for de-info
 */
static void de_info_get(de_info_t *db_info)
{
	if (db_info)
		refcount_inc(&db_info->ref_count);
}

/*
 * de_info_put:
 * - decreases reference count for de-info and frees it if necessary
 */
static void de_info_put(de_info_t *db_info)
{
	int i;

	if (!db_info)
		return;
	if (refcount_dec_and_test(&db_info->ref_count)) {
		for (i = 0; i < DE_MAX_VIEWS; i++) {
			if (!db_info->views[i])
				continue;
			defs_remove(db_info->defs_entries[i]);
		}
		defs_remove(db_info->defs_root_entry);
		if (db_info == de_area_first)
			de_area_first = db_info->next;
		if (db_info == de_area_last)
			de_area_last = db_info->prev;
		if (db_info->prev)
			db_info->prev->next = db_info->next;
		if (db_info->next)
			db_info->next->prev = db_info->prev;
		de_info_free(db_info);
	}
}

/*
 * de_format_entry:
 * - format one de entry and return size of formated data
 */
static int de_format_entry(file_private_info_t *p_info)
{
	de_info_t *id_snap	= p_info->de_info_snap;
	struct de_view *view = p_info->view;
	de_entry_t *act_entry;
	size_t len = 0;

	if (p_info->act_entry == DE_PROLOG_ENTRY) {
		/* print prolog */
		if (view->prolog_proc)
			len += view->prolog_proc(id_snap, view, p_info->temp_buf);
		goto out;
	}
	if (!id_snap->areas) /* this is true, if we have a prolog only view */
		goto out;    /* or if 'pages_per_area' is 0 */
	act_entry = (de_entry_t *) ((char *)id_snap->areas[p_info->act_area]
				       [p_info->act_page] + p_info->act_entry);

	if (act_entry->id.stck == 0LL)
		goto out; /* empty entry */
	if (view->header_proc)
		len += view->header_proc(id_snap, view, p_info->act_area,
					 act_entry, p_info->temp_buf + len);
	if (view->format_proc)
		len += view->format_proc(id_snap, view, p_info->temp_buf + len,
					 DE_DATA(act_entry));
out:
	return len;
}

/*
 * de_next_entry:
 * - goto next entry in p_info
 */
static inline int de_next_entry(file_private_info_t *p_info)
{
	de_info_t *id;

	id = p_info->de_info_snap;
	if (p_info->act_entry == DE_PROLOG_ENTRY) {
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
 * de_output:
 * - called for user read()
 * - copies formated de entries to the user buffer
 */
static ssize_t de_output(struct file *file,		/* file descriptor */
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
	if (p_info->act_area >= p_info->de_info_snap->nr_areas)
		return 0;
	entry_offset = p_info->act_entry_offset;
	while (count < len) {
		int formatted_line_residue;
		int formatted_line_size;
		int user_buf_residue;
		size_t copy_size;

		formatted_line_size = de_format_entry(p_info);
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
			if (de_next_entry(p_info))
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
 * de_input:
 * - called for user write()
 * - calls input function of view
 */
static ssize_t de_input(struct file *file, const char __user *user_buf,
			   size_t length, loff_t *offset)
{
	file_private_info_t *p_info;
	int rc = 0;

	mutex_lock(&de_mutex);
	p_info = ((file_private_info_t *) file->private_data);
	if (p_info->view->input_proc) {
		rc = p_info->view->input_proc(p_info->de_info_org,
					      p_info->view, file, user_buf,
					      length, offset);
	} else {
		rc = -EPERM;
	}
	mutex_unlock(&de_mutex);
	return rc; /* number of input characters */
}

/*
 * de_open:
 * - called for user open()
 * - copies formated output to private_data area of the file
 *   handle
 */
static int de_open(struct inode *inode, struct file *file)
{
	de_info_t *de_info, *de_info_snapshot;
	file_private_info_t *p_info;
	int i, rc = 0;

	mutex_lock(&de_mutex);
	de_info = file_inode(file)->i_private;
	/* find de view */
	for (i = 0; i < DE_MAX_VIEWS; i++) {
		if (!de_info->views[i])
			continue;
		else if (de_info->defs_entries[i] == file->f_path.dentry)
			goto found; /* found view ! */
	}
	/* no entry found */
	rc = -EINVAL;
	goto out;

found:

	/* Make snapshot of current de areas to get it consistent.	  */
	/* To copy all the areas is only needed, if we have a view which  */
	/* formats the de areas. */

	if (!de_info->views[i]->format_proc && !de_info->views[i]->header_proc)
		de_info_snapshot = de_info_copy(de_info, NO_AREAS);
	else
		de_info_snapshot = de_info_copy(de_info, ALL_AREAS);

	if (!de_info_snapshot) {
		rc = -ENOMEM;
		goto out;
	}
	p_info = kmalloc(sizeof(file_private_info_t), GFP_KERNEL);
	if (!p_info) {
		de_info_free(de_info_snapshot);
		rc = -ENOMEM;
		goto out;
	}
	p_info->offset = 0;
	p_info->de_info_snap = de_info_snapshot;
	p_info->de_info_org	= de_info;
	p_info->view = de_info->views[i];
	p_info->act_area = 0;
	p_info->act_page = 0;
	p_info->act_entry = DE_PROLOG_ENTRY;
	p_info->act_entry_offset = 0;
	file->private_data = p_info;
	de_info_get(de_info);
	nonseekable_open(inode, file);
out:
	mutex_unlock(&de_mutex);
	return rc;
}

/*
 * de_close:
 * - called for user close()
 * - deletes  private_data area of the file handle
 */
static int de_close(struct inode *inode, struct file *file)
{
	file_private_info_t *p_info;

	p_info = (file_private_info_t *) file->private_data;
	if (p_info->de_info_snap)
		de_info_free(p_info->de_info_snap);
	de_info_put(p_info->de_info_org);
	kfree(file->private_data);
	return 0; /* success */
}

/*
 * de_register_mode:
 * - Creates and initializes de area for the caller
 *   The mode parameter allows to specify access rights for the s390dbf files
 * - Returns handle for de area
 */
de_info_t *de_register_mode(const char *name, int pages_per_area,
				  int nr_areas, int buf_size, umode_t mode,
				  uid_t uid, gid_t gid)
{
	de_info_t *rc = NULL;

	/* Since defs currently does not support uid/gid other than root, */
	/* we do not allow gid/uid != 0 until we get support for that. */
	if ((uid != 0) || (gid != 0))
		pr_warn("Root becomes the owner of all s390dbf files in sysfs\n");
	_ON(!initialized);
	mutex_lock(&de_mutex);

	/* create new de_info */
	rc = de_info_create(name, pages_per_area, nr_areas, buf_size, mode);
	if (!rc)
		goto out;
	de_register_view(rc, &de_level_view);
	de_register_view(rc, &de_flush_view);
	de_register_view(rc, &de_pages_view);
out:
	if (!rc)
		pr_err("Registering de feature %s failed\n", name);
	mutex_unlock(&de_mutex);
	return rc;
}
EXPORT_SYMBOL(de_register_mode);

/*
 * de_register:
 * - creates and initializes de area for the caller
 * - returns handle for de area
 */
de_info_t *de_register(const char *name, int pages_per_area,
			     int nr_areas, int buf_size)
{
	return de_register_mode(name, pages_per_area, nr_areas, buf_size,
				   S_IRUSR | S_IWUSR, 0, 0);
}
EXPORT_SYMBOL(de_register);

/*
 * de_unregister:
 * - give back de area
 */
void de_unregister(de_info_t *id)
{
	if (!id)
		return;
	mutex_lock(&de_mutex);
	de_info_put(id);
	mutex_unlock(&de_mutex);
}
EXPORT_SYMBOL(de_unregister);

/*
 * de_set_size:
 * - set area size (number of pages) and number of areas
 */
static int de_set_size(de_info_t *id, int nr_areas, int pages_per_area)
{
	de_entry_t ***new_areas;
	unsigned long flags;
	int rc = 0;

	if (!id || (nr_areas <= 0) || (pages_per_area < 0))
		return -EINVAL;
	if (pages_per_area > 0) {
		new_areas = de_areas_alloc(pages_per_area, nr_areas);
		if (!new_areas) {
			pr_info("Allocating memory for %i pages failed\n",
				pages_per_area);
			rc = -ENOMEM;
			goto out;
		}
	} else {
		new_areas = NULL;
	}
	spin_lock_irqsave(&id->lock, flags);
	de_areas_free(id);
	id->areas = new_areas;
	id->nr_areas = nr_areas;
	id->pages_per_area = pages_per_area;
	id->active_area = 0;
	memset(id->active_entries, 0, sizeof(int)*id->nr_areas);
	memset(id->active_pages, 0, sizeof(int)*id->nr_areas);
	spin_unlock_irqrestore(&id->lock, flags);
	pr_info("%s: set new size (%i pages)\n", id->name, pages_per_area);
out:
	return rc;
}

/*
 * de_set_level:
 * - set actual de level
 */
void de_set_level(de_info_t *id, int new_level)
{
	unsigned long flags;

	if (!id)
		return;
	spin_lock_irqsave(&id->lock, flags);
	if (new_level == DE_OFF_LEVEL) {
		id->level = DE_OFF_LEVEL;
		pr_info("%s: switched off\n", id->name);
	} else if ((new_level > DE_MAX_LEVEL) || (new_level < 0)) {
		pr_info("%s: level %i is out of range (%i - %i)\n",
			id->name, new_level, 0, DE_MAX_LEVEL);
	} else {
		id->level = new_level;
	}
	spin_unlock_irqrestore(&id->lock, flags);
}
EXPORT_SYMBOL(de_set_level);

/*
 * proceed_active_entry:
 * - set active entry to next in the ring buffer
 */
static inline void proceed_active_entry(de_info_t *id)
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
static inline void proceed_active_area(de_info_t *id)
{
	id->active_area++;
	id->active_area = id->active_area % id->nr_areas;
}

/*
 * get_active_entry:
 */
static inline de_entry_t *get_active_entry(de_info_t *id)
{
	return (de_entry_t *) (((char *) id->areas[id->active_area]
				   [id->active_pages[id->active_area]]) +
				  id->active_entries[id->active_area]);
}

/*
 * de_finish_entry:
 * - set timestamp, caller address, cpu number etc.
 */

static inline void de_finish_entry(de_info_t *id, de_entry_t *active,
				      int level, int exception)
{
	active->id.stck = get_tod_clock_fast() -
		*(unsigned long long *) &tod_clock_base[1];
	active->id.fields.cpuid = smp_processor_id();
	active->caller = __builtin_return_address(0);
	active->id.fields.exception = exception;
	active->id.fields.level = level;
	proceed_active_entry(id);
	if (exception)
		proceed_active_area(id);
}

static int de_stoppable = 1;
static int de_active = 1;

#define CTL_S390DBF_STOPPABLE 5678
#define CTL_S390DBF_ACTIVE 5679

/*
 * proc handler for the running de_active sysctl
 * always allow read, allow write only if de_stoppable is set or
 * if de_active is already off
 */
static int s390dbf_procactive(struct ctl_table *table, int write,
			      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	if (!write || de_stoppable || !de_active)
		return proc_dointvec(table, write, buffer, lenp, ppos);
	else
		return 0;
}

static struct ctl_table s390dbf_table[] = {
	{
		.procname	= "de_stoppable",
		.data		= &de_stoppable,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "de_active",
		.data		= &de_active,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= s390dbf_procactive,
	},
	{ }
};

static struct ctl_table s390dbf_dir_table[] = {
	{
		.procname	= "s390dbf",
		.maxlen		= 0,
		.mode		= S_IRUGO | S_IXUGO,
		.child		= s390dbf_table,
	},
	{ }
};

static struct ctl_table_header *s390dbf_sysctl_header;

void de_stop_all(void)
{
	if (de_stoppable)
		de_active = 0;
}
EXPORT_SYMBOL(de_stop_all);

void de_set_critical(void)
{
	de_critical = 1;
}

/*
 * de_event_common:
 * - write de entry with given size
 */
de_entry_t *de_event_common(de_info_t *id, int level, const void *buf,
				  int len)
{
	de_entry_t *active;
	unsigned long flags;

	if (!de_active || !id->areas)
		return NULL;
	if (de_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	do {
		active = get_active_entry(id);
		memcpy(DE_DATA(active), buf, min(len, id->buf_size));
		if (len < id->buf_size)
			memset((DE_DATA(active)) + len, 0, id->buf_size - len);
		de_finish_entry(id, active, level, 0);
		len -= id->buf_size;
		buf += id->buf_size;
	} while (len > 0);

	spin_unlock_irqrestore(&id->lock, flags);
	return active;
}
EXPORT_SYMBOL(de_event_common);

/*
 * de_exception_common:
 * - write de entry with given size and switch to next de area
 */
de_entry_t *de_exception_common(de_info_t *id, int level,
				      const void *buf, int len)
{
	de_entry_t *active;
	unsigned long flags;

	if (!de_active || !id->areas)
		return NULL;
	if (de_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	do {
		active = get_active_entry(id);
		memcpy(DE_DATA(active), buf, min(len, id->buf_size));
		if (len < id->buf_size)
			memset((DE_DATA(active)) + len, 0, id->buf_size - len);
		de_finish_entry(id, active, level, len <= id->buf_size);
		len -= id->buf_size;
		buf += id->buf_size;
	} while (len > 0);

	spin_unlock_irqrestore(&id->lock, flags);
	return active;
}
EXPORT_SYMBOL(de_exception_common);

/*
 * counts arguments in format string for sprintf view
 */
static inline int de_count_numargs(char *string)
{
	int numargs = 0;

	while (*string) {
		if (*string++ == '%')
			numargs++;
	}
	return numargs;
}

/*
 * de_sprintf_event:
 */
de_entry_t *__de_sprintf_event(de_info_t *id, int level, char *string, ...)
{
	de_sprintf_entry_t *curr_event;
	de_entry_t *active;
	unsigned long flags;
	int numargs, idx;
	va_list ap;

	if (!de_active || !id->areas)
		return NULL;
	numargs = de_count_numargs(string);

	if (de_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	active = get_active_entry(id);
	curr_event = (de_sprintf_entry_t *) DE_DATA(active);
	va_start(ap, string);
	curr_event->string = string;
	for (idx = 0; idx < min(numargs, (int)(id->buf_size / sizeof(long)) - 1); idx++)
		curr_event->args[idx] = va_arg(ap, long);
	va_end(ap);
	de_finish_entry(id, active, level, 0);
	spin_unlock_irqrestore(&id->lock, flags);

	return active;
}
EXPORT_SYMBOL(__de_sprintf_event);

/*
 * de_sprintf_exception:
 */
de_entry_t *__de_sprintf_exception(de_info_t *id, int level, char *string, ...)
{
	de_sprintf_entry_t *curr_event;
	de_entry_t *active;
	unsigned long flags;
	int numargs, idx;
	va_list ap;

	if (!de_active || !id->areas)
		return NULL;

	numargs = de_count_numargs(string);

	if (de_critical) {
		if (!spin_trylock_irqsave(&id->lock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&id->lock, flags);
	}
	active = get_active_entry(id);
	curr_event = (de_sprintf_entry_t *)DE_DATA(active);
	va_start(ap, string);
	curr_event->string = string;
	for (idx = 0; idx < min(numargs, (int)(id->buf_size / sizeof(long)) - 1); idx++)
		curr_event->args[idx] = va_arg(ap, long);
	va_end(ap);
	de_finish_entry(id, active, level, 1);
	spin_unlock_irqrestore(&id->lock, flags);

	return active;
}
EXPORT_SYMBOL(__de_sprintf_exception);

/*
 * de_register_view:
 */
int de_register_view(de_info_t *id, struct de_view *view)
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
	pde = defs_create_file(view->name, mode, id->defs_root_entry,
				  id, &de_file_ops);
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DE_MAX_VIEWS; i++) {
		if (!id->views[i])
			break;
	}
	if (i == DE_MAX_VIEWS) {
		pr_err("Registering view %s/%s would exceed the maximum "
		       "number of views %i\n", id->name, view->name, i);
		rc = -1;
	} else {
		id->views[i] = view;
		id->defs_entries[i] = pde;
	}
	spin_unlock_irqrestore(&id->lock, flags);
	if (rc)
		defs_remove(pde);
out:
	return rc;
}
EXPORT_SYMBOL(de_register_view);

/*
 * de_unregister_view:
 */
int de_unregister_view(de_info_t *id, struct de_view *view)
{
	struct dentry *dentry = NULL;
	unsigned long flags;
	int i, rc = 0;

	if (!id)
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DE_MAX_VIEWS; i++) {
		if (id->views[i] == view)
			break;
	}
	if (i == DE_MAX_VIEWS) {
		rc = -1;
	} else {
		dentry = id->defs_entries[i];
		id->views[i] = NULL;
		id->defs_entries[i] = NULL;
	}
	spin_unlock_irqrestore(&id->lock, flags);
	defs_remove(dentry);
out:
	return rc;
}
EXPORT_SYMBOL(de_unregister_view);

static inline char *de_get_user_string(const char __user *user_buf,
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

static inline int de_get_uint(char *buf)
{
	int rc;

	buf = skip_spaces(buf);
	rc = simple_strtoul(buf, &buf, 10);
	if (*buf)
		rc = -EINVAL;
	return rc;
}

/*
 * functions for de-views
 ***********************************
*/

/*
 * prints out actual de level
 */

static int de_prolog_pages_fn(de_info_t *id, struct de_view *view,
				 char *out_buf)
{
	return sprintf(out_buf, "%i\n", id->pages_per_area);
}

/*
 * reads new size (number of pages per de area)
 */

static int de_input_pages_fn(de_info_t *id, struct de_view *view,
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
	str = de_get_user_string(user_buf, user_len);
	if (IS_ERR(str)) {
		rc = PTR_ERR(str);
		goto out;
	}
	new_pages = de_get_uint(str);
	if (new_pages < 0) {
		rc = -EINVAL;
		goto free_str;
	}
	rc = de_set_size(id, id->nr_areas, new_pages);
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
 * prints out actual de level
 */
static int de_prolog_level_fn(de_info_t *id, struct de_view *view,
				 char *out_buf)
{
	int rc = 0;

	if (id->level == DE_OFF_LEVEL)
		rc = sprintf(out_buf, "-\n");
	else
		rc = sprintf(out_buf, "%i\n", id->level);
	return rc;
}

/*
 * reads new de level
 */
static int de_input_level_fn(de_info_t *id, struct de_view *view,
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
	str = de_get_user_string(user_buf, user_len);
	if (IS_ERR(str)) {
		rc = PTR_ERR(str);
		goto out;
	}
	if (str[0] == '-') {
		de_set_level(id, DE_OFF_LEVEL);
		rc = user_len;
		goto free_str;
	} else {
		new_level = de_get_uint(str);
	}
	if (new_level < 0) {
		pr_warn("%s is not a valid level for a de feature\n", str);
		rc = -EINVAL;
	} else {
		de_set_level(id, new_level);
		rc = user_len;
	}
free_str:
	kfree(str);
out:
	*offset += user_len;
	return rc;		/* number of input characters */
}

/*
 * flushes de areas
 */
static void de_flush(de_info_t *id, int area)
{
	unsigned long flags;
	int i, j;

	if (!id || !id->areas)
		return;
	spin_lock_irqsave(&id->lock, flags);
	if (area == DE_FLUSH_ALL) {
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
 * view function: flushes de areas
 */
static int de_input_flush_fn(de_info_t *id, struct de_view *view,
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
		de_flush(id, DE_FLUSH_ALL);
		goto out;
	}
	if (isdigit(input_buf[0])) {
		int area = ((int) input_buf[0] - (int) '0');

		de_flush(id, area);
		goto out;
	}

	pr_info("Flushing de data failed because %c is not a valid "
		 "area\n", input_buf[0]);

out:
	*offset += user_len;
	return rc;		/* number of input characters */
}

/*
 * prints de header in raw format
 */
static int de_raw_header_fn(de_info_t *id, struct de_view *view,
			       int area, de_entry_t *entry, char *out_buf)
{
	int rc;

	rc = sizeof(de_entry_t);
	memcpy(out_buf, entry, sizeof(de_entry_t));
	return rc;
}

/*
 * prints de data in raw format
 */
static int de_raw_format_fn(de_info_t *id, struct de_view *view,
			       char *out_buf, const char *in_buf)
{
	int rc;

	rc = id->buf_size;
	memcpy(out_buf, in_buf, id->buf_size);
	return rc;
}

/*
 * prints de data in hex/ascii format
 */
static int de_hex_ascii_format_fn(de_info_t *id, struct de_view *view,
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
 * prints header for de entry
 */
int de_dflt_header_fn(de_info_t *id, struct de_view *view,
			 int area, de_entry_t *entry, char *out_buf)
{
	unsigned long base, sec, usec;
	unsigned long caller;
	unsigned int level;
	char *except_str;
	int rc = 0;

	level = entry->id.fields.level;
	base = (*(unsigned long *) &tod_clock_base[0]) >> 4;
	sec = (entry->id.stck >> 12) + base - (TOD_UNIX_EPOCH >> 12);
	usec = do_div(sec, USEC_PER_SEC);

	if (entry->id.fields.exception)
		except_str = "*";
	else
		except_str = "-";
	caller = (unsigned long) entry->caller;
	rc += sprintf(out_buf, "%02i %011ld:%06lu %1u %1s %02i %pK  ",
		      area, sec, usec, level, except_str,
		      entry->id.fields.cpuid, (void *)caller);
	return rc;
}
EXPORT_SYMBOL(de_dflt_header_fn);

/*
 * prints de data sprintf-formated:
 * de_sprinf_event/exception calls must be used together with this view
 */

#define DE_SPRINTF_MAX_ARGS 10

static int de_sprintf_format_fn(de_info_t *id, struct de_view *view,
				   char *out_buf, de_sprintf_entry_t *curr_event)
{
	int num_longs, num_used_args = 0, i, rc = 0;
	int index[DE_SPRINTF_MAX_ARGS];

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
	num_used_args = min(DE_SPRINTF_MAX_ARGS, (num_longs - 1));

	memset(index, 0, DE_SPRINTF_MAX_ARGS * sizeof(int));

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
 * de_init:
 * - is called exactly once to initialize the de feature
 */
static int __init de_init(void)
{
	s390dbf_sysctl_header = register_sysctl_table(s390dbf_dir_table);
	mutex_lock(&de_mutex);
	de_defs_root_entry = defs_create_dir(DE_DIR_ROOT, NULL);
	initialized = 1;
	mutex_unlock(&de_mutex);
	return 0;
}
postcore_initcall(de_init);
