/* arch/arm/mach-rk29/vpu_mem.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * author: chenhengming chm@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include <mach/vpu_mem.h>

#define VPU_MEM_MIN_ALLOC               PAGE_SIZE
#define VPU_MEM_IS_PAGE_ALIGNED(addr)   (!((addr) & (~PAGE_MASK)))

#define VPU_MEM_DEBUG                   0
#define VPU_MEM_DEBUG_MSGS              0

#if VPU_MEM_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
		    ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif

/**
 * struct for process session which connect to vpu_mem
 *
 * @author ChenHengming (2011-4-11)
 */
typedef struct vpu_mem_session {
	/* a list of memory region used posted by current process */
	struct list_head list_used;
	struct list_head list_post;
	/* a linked list of data so we can access them for debugging */
	struct list_head list_session;
	/* a linked list of memory pool on current session */
	struct list_head list_pool;
	/* process id of teh mapping process */
	pid_t pid;
} vdm_session;

/**
 * session memory pool info
 */
typedef struct vpu_mem_pool_info {
	struct list_head session_link;      /* link to session use for search */
	struct list_head list_used;         /* a linked list for used memory in the pool */
	vdm_session *session;
	int count_current;
	int count_target;
	int count_used;
	int pfn;
} vdm_pool;

/**
 * session memory pool config input
 */
typedef struct vpu_mem_pool_config {
	int size;
	unsigned int count;
} vdm_pool_config;

/**
 * global region info
 */
typedef struct vpu_mem_region_info {
	struct list_head index_list;        /* link to index list use for search */
	int used;
	int post;
	int index;
	int pfn;
} vdm_region;

/**
 * struct for region information
 * this struct should be modified with bitmap lock
 */
typedef struct vpu_mem_link_info {
	struct list_head session_link;      /* link to vpu_mem_session list */
	struct list_head status_link;       /* link to vdm_info.status list use for search */
	struct list_head pool_link;         /* link to vpu_mem_session pool list for search */
	vdm_region *region;
	vdm_pool *pool;
	union {
		int post;
		int used;
		int count;
	} ref;
	int *ref_ptr;
	int index;
	int pfn;
} vdm_link;

/**
 * struct for global vpu memory info
 */
typedef struct vpu_mem_info {
	struct miscdevice dev;
	/* physical start address of the remaped vpu_mem space */
	unsigned long base;
	/* vitual start address of the remaped vpu_mem space */
	unsigned char __iomem *vbase;
	/* total size of the vpu_mem space */
	unsigned long size;
	/* number of entries in the vpu_mem space */
	unsigned long num_entries;
	/* indicates maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	unsigned buffered;
	/*
	 * vdm_session init only store the free region but use a vdm_session for convenience
	 */
	vdm_session status;
	struct list_head list_index;        /* sort by index */
	struct list_head list_free;         /* free region list */
	struct list_head list_session;      /* session list */
	struct rw_semaphore rw_sem;
} vdm_info;

static vdm_info vpu_mem;
static int vpu_mem_count;
static int vpu_mem_over = 0;

#define vdm_used                (vpu_mem.status.list_used)
#define vdm_post                (vpu_mem.status.list_post)
#define vdm_index               (vpu_mem.list_index)
#define vdm_free                (vpu_mem.list_free)
#define vdm_proc                (vpu_mem.list_session)
#define vdm_rwsem               (vpu_mem.rw_sem)
#define is_free_region(x)       ((0 == (x)->used) && (0 == (x)->post))

/**
 * vpu memory info dump:
 * first dump global info, then dump each session info
 *
 * @author ChenHengming (2011-4-20)
 */
static void dump_status(void)
{
	vdm_link    *link, *tmp_link;
	vdm_pool    *pool, *tmp_pool;
	vdm_region  *region, *tmp_region;
	vdm_session *session, *tmp_session;

	printk("vpu mem status dump :\n\n");

	// 按 index 打印全部 region
	printk("region:\n");
	list_for_each_entry_safe(region, tmp_region, &vdm_index, index_list) {
		printk("        idx %6d pfn %6d used %3d post %3d\n",
			region->index, region->pfn, region->used, region->post);
	}
	printk("free  :\n");
	list_for_each_entry_safe(link, tmp_link, &vdm_free, status_link) {
		printk("        idx %6d pfn %6d ref %3d\n",
			link->index, link->pfn, link->ref.used);
	}
	printk("used  :\n");
	list_for_each_entry_safe(link, tmp_link, &vdm_used, status_link) {
		printk("        idx %6d pfn %6d used %3d\n",
			link->index, link->pfn, link->ref.used);
	}
	printk("post  :\n");
	list_for_each_entry_safe(link, tmp_link, &vdm_post, status_link) {
		printk("        idx %6d pfn %6d post %3d\n",
			link->index, link->pfn, link->ref.post);
	}

	// 打印 vpu_mem_info 中的全部 session 的 region 占用情况
	list_for_each_entry_safe(session, tmp_session, &vdm_proc, list_session) {
		printk("pid: %d\n", session->pid);

		list_for_each_entry_safe(pool, tmp_pool, &session->list_pool, session_link) {
			printk("pool: pfn %6d target %3d current %2d\n",
				pool->pfn, pool->count_current, pool->count_target);
		}
		list_for_each_entry_safe(link, tmp_link, &session->list_used, session_link) {
			printk("used: idx %6d pfn %6d used %3d\n",
				link->index, link->pfn, link->ref.used);
		}
		list_for_each_entry_safe(link, tmp_link, &session->list_post, session_link) {
			printk("post: idx %6d pfn %6d post %3d\n",
				link->index, link->pfn, link->ref.post);
		}
	}
}

/**
 * find used link in a session
 *
 * @author ChenHengming (2011-4-18)
 *
 * @param session
 * @param index
 *
 * @return vdm_link*
 */
static vdm_link *find_used_link(vdm_session *session, int index)
{
    vdm_link *pos, *n;

    list_for_each_entry_safe(pos, n, &session->list_used, session_link) {
        if (index == pos->index) {
            DLOG("found index %d ptr %p\n", index, pos);
            return pos;
        }
    }

    return NULL;
}

/**
 * find post link from vpu_mem's vdm_post list
 *
 * @author ChenHengming (2011-4-18)
 *
 * @param index
 *
 * @return vdm_link*
 */
static vdm_link *find_post_link(int index)
{
    vdm_link *pos, *n;

    list_for_each_entry_safe(pos, n, &vdm_post, status_link) {
        if (index == pos->index) {
            return pos;
        }
    }

    return NULL;
}

/**
 * find free link from vpu_mem's vdm_free list
 *
 * @author Administrator (2011-4-19)
 *
 * @param index
 *
 * @return vdm_link*
 */
static vdm_link *find_free_link(int index)
{
    vdm_link *pos, *n;

    list_for_each_entry_safe(pos, n, &vdm_free, status_link) {
        if (index == pos->index) {
            return pos;
        }
    }

    return NULL;
}

static vdm_pool *find_pool_by_pfn(vdm_session *session, unsigned int pfn)
{
    vdm_pool *pos, *n;

    list_for_each_entry_safe(pos, n, &session->list_pool, session_link) {
        if (pfn == pos->pfn) {
            return pos;
        }
    }

    return NULL;
}

static void link_ref_inc(vdm_link *link)
{
	link->ref.count++;
	if (link->ref_ptr) {
		*link->ref_ptr += 1;
	}
}

static void link_ref_dec(vdm_link *link)
{
	link->ref.count--;
	if (link->ref_ptr) {
		*link->ref_ptr -= 1;
	}
}

/**
 * insert a region into the index list for search
 *
 * @author ChenHengming (2011-4-18)
 *
 * @param region
 *
 * @return int
 */
static int _insert_region_index(vdm_region *region)
{
    int index = region->index;
    int last = -1;
    int next;
    vdm_region *tmp, *n;

    if (list_empty(&vdm_index)) {
        DLOG("index list is empty, insert first region\n");
        list_add_tail(&region->index_list, &vdm_index);
        return 0;
    }

    list_for_each_entry_safe(tmp, n, &vdm_index, index_list) {
        next = tmp->index;
        DLOG("insert index %d pfn %d last %d next %d ptr %p\n", index, region->pfn, last, next, tmp);
        if ((last < index) && (index < next))  {
            DLOG("Done\n");
            list_add_tail(&region->index_list, &tmp->index_list);
            return 0;
        }
        last = next;
    }

    printk(KERN_ERR "_insert_region_by_index %d fail!\n", index);
    dump_status();
    return -1;
}

/**
 * insert a link into vdm_free list, indexed by vdm_link->index
 *
 * @author ChenHengming (2011-4-20)
 *
 * @param link
 */
static void _insert_link_status_free(vdm_link *link)
{
    int index = link->index;
    int last = -1;
    int next;
    vdm_link *tmp, *n;

    if (list_empty(&vdm_free)) {
        DLOG("free list is empty, list_add_tail first region\n");
        list_add_tail(&link->status_link, &vdm_free);
        return ;
    }

    list_for_each_entry_safe(tmp, n, &vdm_free, status_link) {
        next = tmp->index;
        if ((last < index) && (index < next))  {
            DLOG("list_add_tail index %d pfn %d last %d next %d ptr %p\n", index, link->pfn, last, next, tmp);
            list_add_tail(&link->status_link, &tmp->status_link);
            return ;
        }
        last = next;
    }
    list_add_tail(&link->status_link, &tmp->status_link);
    DLOG("list_add index %d pfn %d last %d ptr %p\n", index, link->pfn, last, tmp);
    return ;
}

static void _insert_link_status_post(vdm_link *link)
{
    int index = link->index;
    int last = -1;
    int next;
    vdm_link *tmp, *n;

    if (list_empty(&vdm_post)) {
        DLOG("post list is empty, list_add_tail first region\n");
        list_add_tail(&link->status_link, &vdm_post);
        return ;
    }

    list_for_each_entry_safe(tmp, n, &vdm_post, status_link) {
        next = tmp->index;
        if ((last < index) && (index < next))  {
            DLOG("list_add_tail index %d pfn %d last %d next %d ptr %p\n", index, link->pfn, last, next, tmp);
            list_add_tail(&link->status_link, &tmp->status_link);
            return ;
        }
        last = next;
    }

    list_add_tail(&link->status_link, &tmp->status_link);
    DLOG("list_add index %d pfn %d last %d ptr %p\n", index, link->pfn, last, tmp);
    return ;
}

static void _insert_link_status_used(vdm_link *link)
{
    int index = link->index;
    int last = -1;
    int next;
    vdm_link *tmp, *n;

    if (list_empty(&vdm_used)) {
        DLOG("used list is empty, list_add_tail first region\n");
        list_add_tail(&link->status_link, &vdm_used);
        return ;
    }

    list_for_each_entry_safe(tmp, n, &vdm_used, status_link) {
        next = tmp->index;
        if ((last < index) && (index < next))  {
            DLOG("list_add_tail index %d pfn %d last %d next %d ptr %p\n", index, link->pfn, last, next, tmp);
            list_add_tail(&link->status_link, &tmp->status_link);
            return ;
        }
        last = next;
    }

    list_add_tail(&link->status_link, &tmp->status_link);
    DLOG("list_add index %d pfn %d last %d ptr %p\n", index, link->pfn, last, tmp);
    return ;
}

static void _insert_link_session_used(vdm_link *link, vdm_session *session)
{
    int index = link->index;
    int last = -1;
    int next;
    vdm_link *tmp, *n;

    if (list_empty(&session->list_used)) {
        DLOG("session used list is empty, list_add_tail first region\n");
        list_add_tail(&link->session_link, &session->list_used);
        return ;
    }

    list_for_each_entry_safe(tmp, n, &session->list_used, session_link) {
        next = tmp->index;
        if ((last < index) && (index < next))  {
            list_add_tail(&link->session_link, &tmp->session_link);
            DLOG("list_add_tail index %d pfn %d last %d next %d ptr %p\n", index, link->pfn, last, next, tmp);
            return ;
        }
        last = next;
    }

    list_add_tail(&link->session_link, &tmp->session_link);
    DLOG("list_add index %d pfn %d last %d ptr %p\n", index, link->pfn, last, tmp);
    return ;
}

static void _insert_link_session_post(vdm_link *link, vdm_session *session)
{
    int index = link->index;
    int last = -1;
    int next;
    vdm_link *tmp, *n;

    if (list_empty(&session->list_post)) {
        DLOG("session post list is empty, list_add_tail first region\n");
        list_add_tail(&link->session_link, &session->list_post);
        return ;
    }

    list_for_each_entry_safe(tmp, n, &session->list_post, session_link) {
        next = tmp->index;
        if ((last < index) && (index < next))  {
            list_add_tail(&link->session_link, &tmp->session_link);
            DLOG("list_add_tail index %d pfn %d last %d next %d ptr %p\n", index, link->pfn, last, next, tmp);
            return ;
        }
        last = next;
    }

    list_add_tail(&link->session_link, &tmp->session_link);
    DLOG("list_add index %d pfn %d last %d ptr %p\n", index, link->pfn, last, tmp);
    return ;
}

static void _remove_free_region(vdm_region *region)
{
    list_del_init(&region->index_list);
    kfree(region);
}

static void _remove_free_link(vdm_link *link)
{
    list_del_init(&link->session_link);
    list_del_init(&link->status_link);
    kfree(link);
}

static void _merge_two_region(vdm_region *dst, vdm_region *src)
{
    vdm_link *dst_link = find_free_link(dst->index);
    vdm_link *src_link = find_free_link(src->index);
    dst->pfn        += src->pfn;
    dst_link->pfn   += src_link->pfn;
    _remove_free_link(src_link);
    _remove_free_region(src);
}

static void merge_free_region_and_link(vdm_region *region)
{
    if (region->used || region->post) {
        printk(KERN_ALERT "try to merge unfree region!\n");
        return ;
    } else {
        vdm_region *neighbor;
        struct list_head *tmp = region->index_list.next;
        if (tmp != &vdm_index) {
            neighbor = (vdm_region *)list_entry(tmp, vdm_region, index_list);
            if (is_free_region(neighbor)) {
                DLOG("merge next\n");
                _merge_two_region(region, neighbor);
            }
        }
        tmp = region->index_list.prev;
        if (tmp != &vdm_index) {
            neighbor = (vdm_region *)list_entry(tmp, vdm_region, index_list);
            if (is_free_region(neighbor)) {
                DLOG("merge prev\n");
                _merge_two_region(neighbor, region);
            }
        }
    }
}

static void put_free_link(vdm_link *link)
{
	if (link->pool) {
		vdm_pool *pool = link->pool;
		link->pool = NULL;
		list_del_init(&link->pool_link);
		pool->count_current--;
		pool->count_used--;
	}
	list_del_init(&link->session_link);
	list_del_init(&link->status_link);
	_insert_link_status_free(link);
}

static void put_used_link(vdm_link *link, vdm_session *session)
{
	list_del_init(&link->session_link);
	list_del_init(&link->status_link);
	_insert_link_status_used(link);
	_insert_link_session_used(link, session);
	if (NULL == link->pool) {
		vdm_pool *pool = find_pool_by_pfn(session, link->pfn);
		if (pool) {
			link->pool = pool;
			list_add_tail(&link->pool_link, &pool->list_used);
			pool->count_used++;
			pool->count_current++;
		}
	}
}

static void put_post_link(vdm_link *link, vdm_session *session)
{
	list_del_init(&link->session_link);
	list_del_init(&link->status_link);
	_insert_link_status_post(link);
	_insert_link_session_post(link, session);
	if (NULL == link->pool) {
		vdm_pool *pool = find_pool_by_pfn(session, link->pfn);
		if (pool) {
			link->pool = pool;
			list_add_tail(&link->pool_link, &pool->list_used);
			pool->count_used++;
			pool->count_current++;
		}
	}
}

/**
 * Create a link and a region by index and pfn at a same time,
 * and connect the link with the region
 *
 * @author ChenHengming (2011-4-20)
 *
 * @param index
 * @param pfn
 *
 * @return vdm_link*
 */
static vdm_link *new_link_by_index(int index, int pfn)
{
    vdm_region *region = (vdm_region *)kmalloc(sizeof(vdm_region), GFP_KERNEL);
    vdm_link   *link   = (vdm_link   *)kmalloc(sizeof(vdm_link  ), GFP_KERNEL);

    if ((NULL == region) || (NULL == link)) {
        printk(KERN_ALERT "can not kmalloc vdm_region and vdm_link in %s", __FUNCTION__);
        if (region) {
            kfree(region);
        }
        if (link) {
            kfree(link);
        }
        return NULL;
    }

    region->post    = 0;
    region->used    = 0;
    region->index   = index;
    region->pfn     = pfn;

    INIT_LIST_HEAD(&region->index_list);

    link->ref.count = 0;
    link->ref_ptr   = NULL;
    link->region    = region;
    link->index     = region->index;
    link->pfn       = region->pfn;
    INIT_LIST_HEAD(&link->session_link);
    INIT_LIST_HEAD(&link->status_link);
    INIT_LIST_HEAD(&link->pool_link);
    link->pool      = NULL;

    return link;
}

/**
 * Create a link from a already exist region and connect to the
 * region
 *
 * @author ChenHengming (2011-4-20)
 *
 * @param region
 *
 * @return vdm_link*
 */
static vdm_link *new_link_by_region(vdm_region *region)
{
    vdm_link *link = (vdm_link *)kmalloc(sizeof(vdm_link), GFP_KERNEL);
    if (NULL == link) {
        printk(KERN_ALERT "can not kmalloc vdm_region and vdm_link in %s", __FUNCTION__);
        return NULL;
    }

    link->ref.count = 0;
    link->ref_ptr   = NULL;
    link->region    = region;
    link->index     = region->index;
    link->pfn       = region->pfn;
    INIT_LIST_HEAD(&link->session_link);
    INIT_LIST_HEAD(&link->status_link);
    INIT_LIST_HEAD(&link->pool_link);
    link->pool      = NULL;

    return link;
}

/**
 * Delete a link completely
 *
 * @author ChenHengming (2011-4-20)
 *
 * @param link
 */
static void link_del(vdm_link *link)
{
	if (link->pool) {
		vdm_pool *pool = link->pool;
		link->pool = NULL;
		list_del_init(&link->pool_link);
		pool->count_current--;
		pool->count_used--;
	}
	list_del_init(&link->session_link);
	list_del_init(&link->status_link);
	if (is_free_region(link->region) && NULL == find_free_link(link->index)) {
		put_free_link(link);
		merge_free_region_and_link(link->region);
	} else {
		kfree(link);
	}
}

/**
 * Called by malloc, check whether a free link can by used for a
 * len of pfn, if can then put a used link to status link
 *
 * @author ChenHengming (2011-4-20)
 *
 * @param link
 * @param session
 * @param pfn
 *
 * @return vdm_link*
 */
static vdm_link *get_used_link_from_free_link(vdm_link *link, vdm_session *session, int pfn)
{
    if (pfn > link->pfn) {
        return NULL;
    }
    if (pfn == link->pfn) {
        DLOG("pfn == link->pfn %d\n", pfn);
        link->ref.used      = 1;
        link->region->used  = 1;
	link->ref_ptr       = &link->region->used;
        put_used_link(link, session);
        return link;
    } else {
        vdm_link *used = new_link_by_index(link->index, pfn);
        if (NULL == used)
            return NULL;

        link->index         += pfn;
        link->pfn           -= pfn;
        link->region->index += pfn;
        link->region->pfn   -= pfn;
        used->ref.used      = 1;
        used->region->used  = 1;
	used->ref_ptr       = &used->region->used;

        DLOG("used: index %d pfn %d ptr %p\n", used->index, used->pfn, used->region);
        if (_insert_region_index(used->region)) {
            printk(KERN_ALERT "fail to insert allocated region index %d pfn %d\n", used->index, used->pfn);
            link_del(used);
            link->index         -= pfn;
            link->pfn           += pfn;
            link->region->index -= pfn;
            link->region->pfn   += pfn;
            _remove_free_region(used->region);
            _remove_free_link(used);
            return NULL;
        }
        put_used_link(used, session);
        return used;
    }
}

int is_vpu_mem_file(struct file *file)
{
	if (unlikely(!file || !file->f_dentry || !file->f_dentry->d_inode))
		return 0;
	if (unlikely(file->f_dentry->d_inode->i_rdev !=
	     MKDEV(MISC_MAJOR, vpu_mem.dev.minor)))
		return 0;
	return 1;
}

static long vpu_mem_allocate(struct file *file, unsigned int len)
{
	vdm_link *free, *n;
	unsigned int pfn = (len + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
	vdm_session *session = (vdm_session *)file->private_data;

	if (!is_vpu_mem_file(file)) {
		printk(KERN_INFO "allocate vpu_mem session from invalid file\n");
		return -ENODEV;
	}

	list_for_each_entry_safe(free, n, &vdm_free, status_link) {
		/* find match free buffer use it first */
		vdm_link *used = get_used_link_from_free_link(free, session, pfn);
		DLOG("search free buffer at index %d pfn %d for len %d\n", free->index, free->pfn, pfn);
		if (NULL == used) {
			continue;
		} else {
			DLOG("found buffer at index %d pfn %d for ptr %p\n", used->index, used->pfn, used);
			return used->index;
		}
	}

	if (!vpu_mem_over) {
		printk(KERN_INFO "vpu_mem: no space left to allocate!\n");
		dump_status();
		vpu_mem_over = 1;
	}
	return -1;
}

static int vpu_mem_free(struct file *file, int index)
{
	vdm_session *session = (vdm_session *)file->private_data;

	if (!is_vpu_mem_file(file)) {
		printk(KERN_INFO "free vpu_mem session from invalid file.\n");
		return -ENODEV;
    }

	DLOG("searching for index %d\n", index);
    {
        vdm_link *link = find_used_link(session, index);
        if (NULL == link) {
            DLOG("no link of index %d searched\n", index);
            return -1;
        }
	link_ref_dec(link);
        if (0 == link->ref.used) {
		link_del(link);
        }
	}
	return 0;
}

static int vpu_mem_duplicate(struct file *file, int index)
{
	vdm_session *session = (vdm_session *)file->private_data;
	/* caller should hold the write lock on vpu_mem_sem! */
	if (!is_vpu_mem_file(file)) {
		printk(KERN_INFO "duplicate vpu_mem session from invalid file.\n");
		return -ENODEV;
	}

	DLOG("duplicate index %d\n", index);
	{
		vdm_link *post = find_post_link(index);
		if (NULL == post) {
			vdm_link *used = find_used_link(session, index);
			if (NULL == used) {
				printk(KERN_ERR "try to duplicate unknown index %d\n", index);
				dump_status();
				return -1;
			}
			post = new_link_by_region(used->region);
			post->ref_ptr  = &post->region->post;
			link_ref_inc(post);
			put_post_link(post, session);
		} else {
			DLOG("duplicate posted index %d\n", index);
			link_ref_inc(post);
		}
	}

	return 0;
}

static int vpu_mem_link(struct file *file, int index)
{
	vdm_session *session = (vdm_session *)file->private_data;

	if (!is_vpu_mem_file(file)) {
		printk(KERN_INFO "link vpu_mem session from invalid file.\n");
		return -ENODEV;
	}

	DLOG("link index %d\n", index);
	{
		vdm_link *post = find_post_link(index);
		if (NULL == post) {
			printk(KERN_ERR "try to link unknown index %d\n", index);
			dump_status();
			return -1;
		} else {
			vdm_link *used = find_used_link(session, index);
			link_ref_dec(post);

			if (used) {
				if (0 == post->ref.post) {
					link_del(post);
					post = NULL;
				}
			} else {
				if (post->ref.post) {
					used = new_link_by_region(post->region);
				} else {
					used = post;
					post = NULL;
				}
				used->ref_ptr = &used->region->used;
				put_used_link(used, session);
			}
			link_ref_inc(used);
		}
	}

	return 0;
}

static int vpu_mem_pool_add(vdm_session *session, unsigned int pfn, unsigned int count)
{
	vdm_link *link, *n;
	vdm_pool *pool = kmalloc(sizeof(vdm_pool), GFP_KERNEL);
	DLOG("vpu_mem_pool_add %p pfn %d count %d\n", pool, pfn, count);
	if (NULL == pool) {
		printk(KERN_ALERT "vpu_mem: unable to allocate memory for vpu_mem pool.");
		return -1;
	}
	INIT_LIST_HEAD(&pool->session_link);
	INIT_LIST_HEAD(&pool->list_used);
	pool->session = session;
	pool->pfn = pfn;
	pool->count_target = count;
	pool->count_current = 0;
	pool->count_used = 0;

	list_for_each_entry_safe(link, n, &session->list_used, session_link) {
		if (pfn == link->pfn && NULL == link->pool) {
			link->pool = pool;
			list_add_tail(&link->pool_link, &pool->list_used);
			pool->count_used++;
			pool->count_current++;
		}
	}

	list_add_tail(&pool->session_link, &session->list_pool);

	return 0;
}

static void vpu_mem_pool_del(vdm_pool *pool)
{
	vdm_link *link, *n;
	DLOG("vpu_mem_pool_del %p\n", pool);
	list_for_each_entry_safe(link, n, &pool->list_used, pool_link) {
		link->pool = NULL;
		list_del_init(&link->pool_link);
		pool->count_current--;
		pool->count_used--;
	}
	return ;
}

static int vpu_mem_pool_set(struct file *file, unsigned int pfn, unsigned int count)
{
	int ret = 0;
	vdm_session *session = (vdm_session *)file->private_data;
	vdm_pool *pool = find_pool_by_pfn(session, pfn);
	if (NULL == pool) {
		// no pool build pool first
		ret = vpu_mem_pool_add(session, pfn, count);
	} else {
		pool->count_target += count;
	}
	return ret;
}

static int vpu_mem_pool_unset(struct file *file, unsigned int pfn, unsigned int count)
{
	int ret = 0;
	vdm_session *session = (vdm_session *)file->private_data;
	vdm_pool *pool = find_pool_by_pfn(session, pfn);
	if (pool) {
		pool->count_target -= count;
		if (pool->count_target <= 0) {
			vpu_mem_pool_del(pool);
			pool->count_target = 0;
		}
	}
	return ret;
}

static int vpu_mem_pool_check(struct file *file, unsigned int pfn)
{
	int ret = 0;
	vdm_session *session = (vdm_session *)file->private_data;
	vdm_pool *pool = find_pool_by_pfn(session, pfn);
	if (pool) {
		if (pool->count_current > pool->count_target) {
			ret = 1;
		}
		DLOG("vpu_mem_pool_check pfn %u current %d target %d ret %d\n", pfn, pool->count_current, pool->count_target, ret);
	}
	return ret;
}

void vpu_mem_cache_opt(struct file *file, long index, unsigned int cmd)
{
	vdm_session *session = (vdm_session *)file->private_data;
	void *start, *end;

	if (!is_vpu_mem_file(file)) {
		return;
	}

	if (!vpu_mem.cached || file->f_flags & O_SYNC)
		return;

	down_read(&vdm_rwsem);
	do {
		vdm_link *link = find_used_link(session, index);
		if (NULL == link) {
			pr_err("vpu_mem_cache_opt on non-exsist index %ld\n", index);
			break;
		}
		start = vpu_mem.vbase + index * VPU_MEM_MIN_ALLOC;
		end   = start + link->pfn * VPU_MEM_MIN_ALLOC;;
		switch (cmd) {
		case VPU_MEM_CACHE_FLUSH : {
			dmac_flush_range(start, end);
			break;
		}
		case VPU_MEM_CACHE_CLEAN : {
			dmac_clean_range(start, end);
			break;
		}
		case VPU_MEM_CACHE_INVALID : {
			dmac_inv_range(start, end);
			break;
		}
		default :
		break;
		}
	} while (0);
	up_read(&vdm_rwsem);
}

static pgprot_t vpu_mem_phys_mem_access_prot(struct file *file, pgprot_t vma_prot)
{
#ifdef pgprot_noncached
	if (vpu_mem.cached == 0 || file->f_flags & O_SYNC)
		return pgprot_noncached(vma_prot);
#endif
#ifdef pgprot_ext_buffered
	else if (vpu_mem.buffered)
		return pgprot_ext_buffered(vma_prot);
#endif
	return vma_prot;
}

static int vpu_mem_map_pfn_range(struct vm_area_struct *vma, unsigned long len)
{
	DLOG("map len %lx\n", len);
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(vma->vm_start));
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(vma->vm_end));
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(len));
	if (io_remap_pfn_range(vma, vma->vm_start,
		vpu_mem.base >> PAGE_SHIFT,
		len, vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static int vpu_mem_open(struct inode *inode, struct file *file)
{
    vdm_session *session;
    int ret = 0;

    DLOG("current %u file %p(%d)\n", current->pid, file, (int)file_count(file));
    /* setup file->private_data to indicate its unmapped */
    /*  you can only open a vpu_mem device one time */
    if (file->private_data != NULL && file->private_data != &vpu_mem.dev)
            return -1;
    session = kmalloc(sizeof(vdm_session), GFP_KERNEL);
    if (!session) {
        printk(KERN_ALERT "vpu_mem: unable to allocate memory for vpu_mem metadata.");
        return -1;
    }
    session->pid = current->pid;
    INIT_LIST_HEAD(&session->list_post);
    INIT_LIST_HEAD(&session->list_used);
    INIT_LIST_HEAD(&session->list_pool);

    file->private_data = session;

    down_write(&vdm_rwsem);
    list_add_tail(&session->list_session, &vdm_proc);
    up_write(&vdm_rwsem);
    return ret;
}

static int vpu_mem_mmap(struct file *file, struct vm_area_struct *vma)
{
	vdm_session *session;
	unsigned long vma_size =  vma->vm_end - vma->vm_start;
	int ret = 0;

	if (vma->vm_pgoff || !VPU_MEM_IS_PAGE_ALIGNED(vma_size)) {
		printk(KERN_ALERT "vpu_mem: mmaps must be at offset zero, aligned"
				" and a multiple of pages_size.\n");
		return -EINVAL;
	}

	session = (vdm_session *)file->private_data;

	/* assert: vma_size must be the total size of the vpu_mem */
	if (vpu_mem.size != vma_size) {
		printk(KERN_WARNING "vpu_mem: mmap size [%lu] does not match"
		       "size of backing region [%lu].\n", vma_size, vpu_mem.size);
		ret = -EINVAL;
		goto error;
	}

	vma->vm_pgoff = vpu_mem.base >> PAGE_SHIFT;
	vma->vm_page_prot = vpu_mem_phys_mem_access_prot(file, vma->vm_page_prot);

	if (vpu_mem_map_pfn_range(vma, vma_size)) {
		printk(KERN_INFO "vpu_mem: mmap failed in kernel!\n");
		ret = -EAGAIN;
		goto error;
	}

	session->pid = current->pid;

error:
	return ret;
}

static int vpu_mem_release(struct inode *inode, struct file *file)
{
	vdm_session *session = (vdm_session *)file->private_data;

    down_write(&vdm_rwsem);
    {
        vdm_link *link, *tmp_link;
        //unsigned long flags = current->flags;
        //printk("current->flags: %lx\n", flags);
        list_del(&session->list_session);
        file->private_data = NULL;

        list_for_each_entry_safe(link, tmp_link, &session->list_post, session_link) {
            do {
	    	link_ref_dec(link);
            } while (link->ref.post);
	    link_del(link);
        }
        list_for_each_entry_safe(link, tmp_link, &session->list_used, session_link) {
            do {
	    	link_ref_dec(link);
            } while (link->ref.used);
	    link_del(link);
        }
    }
    up_write(&vdm_rwsem);
    kfree(session);

    return 0;
}

static long vpu_mem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long index, ret = 0;

	switch (cmd) {
	case VPU_MEM_GET_PHYS: {
		DLOG("get_phys\n");
		printk(KERN_INFO "vpu_mem: request for physical address of vpu_mem region "
				"from process %d.\n", current->pid);
		if (copy_to_user((void __user *)arg, &vpu_mem.base, sizeof(vpu_mem.base)))
		return -EFAULT;
	} break;
	case VPU_MEM_GET_TOTAL_SIZE: {
		DLOG("get total size\n");
		if (copy_to_user((void __user *)arg, &vpu_mem.size, sizeof(vpu_mem.size)))
			return -EFAULT;
	} break;
	case VPU_MEM_ALLOCATE: {
		unsigned int size;
		DLOG("allocate\n");
		if (copy_from_user(&size, (void __user *)arg, sizeof(size)))
		return -EFAULT;
		down_write(&vdm_rwsem);
		ret = vpu_mem_allocate(file, size);
		up_write(&vdm_rwsem);
		DLOG("allocate at index %ld\n", ret);
	} break;
	case VPU_MEM_FREE: {
		DLOG("mem free\n");
		if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
			return -EFAULT;
		if (index >= vpu_mem.size)
			return -EACCES;
		down_write(&vdm_rwsem);
		ret = vpu_mem_free(file, index);
		up_write(&vdm_rwsem);
	} break;

	case VPU_MEM_CACHE_FLUSH:
	case VPU_MEM_CACHE_CLEAN:
	case VPU_MEM_CACHE_INVALID: {
		DLOG("flush\n");
		if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
			return -EFAULT;
		if (index < 0)
			return -EINVAL;
		vpu_mem_cache_opt(file, index, cmd);
	} break;
	case VPU_MEM_DUPLICATE: {
		DLOG("duplicate\n");
		if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
			return -EFAULT;
		down_write(&vdm_rwsem);
		ret = vpu_mem_duplicate(file, index);
		up_write(&vdm_rwsem);
	} break;

	case VPU_MEM_LINK: {
		DLOG("link\n");
		if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
			return -EFAULT;
		down_write(&vdm_rwsem);
		ret = vpu_mem_link(file, index);
		up_write(&vdm_rwsem);
	} break;

	case VPU_MEM_POOL_SET: {
		struct vpu_mem_pool_config config;
		DLOG("pool set\n");
		if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
			return -EFAULT;
		config.size = (config.size + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
		down_write(&vdm_rwsem);
		ret = vpu_mem_pool_set(file, config.size, config.count);
		up_write(&vdm_rwsem);
	} break;

	case VPU_MEM_POOL_UNSET: {
		struct vpu_mem_pool_config config;
		DLOG("pool unset\n");
		if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
			return -EFAULT;
		config.size = (config.size + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
		down_write(&vdm_rwsem);
		ret = vpu_mem_pool_unset(file, config.size, config.count);
		up_write(&vdm_rwsem);
	} break;

	case VPU_MEM_POOL_CHECK: {
		int pfn;
		if (copy_from_user(&pfn, (void __user *)arg, sizeof(int)))
			return -EFAULT;
		pfn = (pfn + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
		DLOG("pool check\n");
		down_write(&vdm_rwsem);
		ret = vpu_mem_pool_check(file, pfn);
		up_write(&vdm_rwsem);
	} break;

	default:
		return -EINVAL;
	}
	return ret;
}

struct file_operations vpu_mem_fops = {
	.open = vpu_mem_open,
	.mmap = vpu_mem_mmap,
	.unlocked_ioctl = vpu_mem_ioctl,
	.release = vpu_mem_release,
};

#if VPU_MEM_DEBUG
static ssize_t debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debug_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	vdm_region *region, *tmp_region;
	const int debug_bufmax = 4096;
	static char buffer[4096];
	int n = 0;

	DLOG("debug open\n");
	n = scnprintf(buffer, debug_bufmax,
		      "pid #: mapped regions (offset, len, used, post) ...\n");
	down_read(&vdm_rwsem);
    list_for_each_entry_safe(region, tmp_region, &vdm_index, index_list) {
        n += scnprintf(buffer + n, debug_bufmax - n,
                "(%d,%d,%d,%d) ",
                region->index, region->pfn, region->used, region->post);
	}
	up_read(&vdm_rwsem);
	n++;
	buffer[n] = 0;
	return simple_read_from_buffer(buf, count, ppos, buffer, n);
}

static struct file_operations debug_fops = {
	.read = debug_read,
	.open = debug_open,
};
#endif

int vpu_mem_setup(struct vpu_mem_platform_data *pdata)
{
    vdm_link *tmp = NULL;
	int err = 0;

    if (vpu_mem_count) {
		printk(KERN_ALERT "Only one vpu_mem driver can be register!\n");
        goto err_cant_register_device;
    }

    memset(&vpu_mem, 0, sizeof(struct vpu_mem_info));

    vpu_mem.cached = pdata->cached;
    vpu_mem.buffered = pdata->buffered;
    vpu_mem.base = pdata->start;
    vpu_mem.size = pdata->size;
    init_rwsem(&vdm_rwsem);
    INIT_LIST_HEAD(&vdm_proc);
    INIT_LIST_HEAD(&vdm_used);
    INIT_LIST_HEAD(&vdm_post);
    INIT_LIST_HEAD(&vdm_free);
    INIT_LIST_HEAD(&vdm_index);
    vpu_mem.dev.name = pdata->name;
    vpu_mem.dev.minor = MISC_DYNAMIC_MINOR;
    vpu_mem.dev.fops = &vpu_mem_fops;

    err = misc_register(&vpu_mem.dev);
    if (err) {
        printk(KERN_ALERT "Unable to register vpu_mem driver!\n");
        goto err_cant_register_device;
    }

    vpu_mem.num_entries = vpu_mem.size / VPU_MEM_MIN_ALLOC;

    tmp = new_link_by_index(0, vpu_mem.num_entries);
    if (NULL == tmp) {
		printk(KERN_ALERT "init free region failed\n");
        goto err_no_mem_for_metadata;
    }
    put_free_link(tmp);
    _insert_region_index(tmp->region);

    if (vpu_mem.cached)
        vpu_mem.vbase = ioremap_cached(vpu_mem.base, vpu_mem.size);
    #ifdef ioremap_ext_buffered
    else if (vpu_mem.buffered)
        vpu_mem.vbase = ioremap_ext_buffered(vpu_mem.base, vpu_mem.size);
    #endif
    else
        vpu_mem.vbase = ioremap(vpu_mem.base, vpu_mem.size);

    if (vpu_mem.vbase == 0)
        goto error_cant_remap;

    #if VPU_MEM_DEBUG
    debugfs_create_file(pdata->name, S_IFREG | S_IRUGO, NULL, (void *)vpu_mem.dev.minor,
                        &debug_fops);
    #endif
    printk("%s: %d initialized\n", pdata->name, vpu_mem.dev.minor);
    vpu_mem_count++;
	return 0;
error_cant_remap:
    if (tmp) {
        kfree(tmp);
    }
err_no_mem_for_metadata:
	misc_deregister(&vpu_mem.dev);
err_cant_register_device:
	return -1;
}

static int vpu_mem_probe(struct platform_device *pdev)
{
	struct vpu_mem_platform_data *pdata;

	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to probe vpu_mem!\n");
		return -1;
	}
	pdata = pdev->dev.platform_data;
	return vpu_mem_setup(pdata);
}

static int vpu_mem_remove(struct platform_device *pdev)
{
	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to remove vpu_mem!\n");
		return -1;
	}
    if (vpu_mem_count) {
	    misc_deregister(&vpu_mem.dev);
        vpu_mem_count--;
    } else {
		printk(KERN_ALERT "no vpu_mem to remove!\n");
    }
	return 0;
}

static struct platform_driver vpu_mem_driver = {
	.probe  = vpu_mem_probe,
	.remove = vpu_mem_remove,
	.driver = { .name = "vpu_mem" }
};


static int __init vpu_mem_init(void)
{
	return platform_driver_register(&vpu_mem_driver);
}

static void __exit vpu_mem_exit(void)
{
	platform_driver_unregister(&vpu_mem_driver);
}

module_init(vpu_mem_init);
module_exit(vpu_mem_exit);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_vpu_mem_show(struct seq_file *s, void *v)
{
	if (vpu_mem_count) {
		seq_printf(s, "vpu mem opened\n");
	} else {
		seq_printf(s, "vpu mem closed\n");
        return 0;
	}

    down_read(&vdm_rwsem);
    {
        vdm_link    *link, *tmp_link;
        vdm_pool    *pool, *tmp_pool;
        vdm_region  *region, *tmp_region;
        vdm_session *session, *tmp_session;
        // 按 index 打印全部 region
        seq_printf(s, "index:\n");
        list_for_each_entry_safe(region, tmp_region, &vdm_index, index_list) {
            seq_printf(s, "       idx %6d pfn %6d used %3d post %3d\n",
                region->index, region->pfn, region->used, region->post);
        }
        if (list_empty(&vdm_free)) {
            seq_printf(s, "free : empty\n");
        } else {
            seq_printf(s, "free :\n");
            list_for_each_entry_safe(link, tmp_link, &vdm_free, status_link) {
                seq_printf(s, "       idx %6d pfn %6d used %3d post %3d\n",
                    link->index, link->pfn, link->ref.used, link->ref.post);
            }
        }
        if (list_empty(&vdm_used)) {
            seq_printf(s, "used : empty\n");
        } else {
            seq_printf(s, "used :\n");
            list_for_each_entry_safe(link, tmp_link, &vdm_used, status_link) {
                seq_printf(s, "       idx %6d pfn %6d used %3d post %3d\n",
                    link->index, link->pfn, link->ref.used, link->ref.post);
            }
        }
        if (list_empty(&vdm_post)) {
            seq_printf(s, "post : empty\n");
        } else {
            seq_printf(s, "post :\n");
            list_for_each_entry_safe(link, tmp_link, &vdm_post, status_link) {
                seq_printf(s, "       idx %6d pfn %6d used %3d post %3d\n",
                    link->index, link->pfn, link->ref.used, link->ref.post);
            }
        }

        // 打印 vpu_mem_info 中的全部 session 的 region 占用情况
        list_for_each_entry_safe(session, tmp_session, &vdm_proc, list_session) {
            seq_printf(s, "\npid: %d\n", session->pid);
            if (list_empty(&session->list_pool)) {
                seq_printf(s, "pool : empty\n");
            } else {
                seq_printf(s, "pool :\n");
                list_for_each_entry_safe(pool, tmp_pool, &session->list_pool, session_link) {
                    seq_printf(s, "       pfn %6d target %4d current %2d\n",
                        pool->pfn, pool->count_target, pool->count_current);
                }
            }
            if (list_empty(&session->list_used)) {
                seq_printf(s, "used : empty\n");
            } else {
                seq_printf(s, "used :\n");
                list_for_each_entry_safe(link, tmp_link, &session->list_used, session_link) {
                    seq_printf(s, "       idx %6d pfn %6d used %3d\n",
                        link->index, link->pfn, link->ref.used);
                }
            }
            if (list_empty(&session->list_post)) {
                seq_printf(s, "post : empty\n");
            } else {
                seq_printf(s, "post :\n");
                list_for_each_entry_safe(link, tmp_link, &session->list_post, session_link) {
                    seq_printf(s, "       idx %6d pfn %6d post %3d\n",
                        link->index, link->pfn, link->ref.post);
                }
            }
        }
    }

    up_read(&vdm_rwsem);
    return 0;
}

static int proc_vpu_mem_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vpu_mem_show, NULL);
}

static const struct file_operations proc_vpu_mem_fops = {
	.open		= proc_vpu_mem_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init vpu_mem_proc_init(void)
{
	proc_create("vpu_mem", 0, NULL, &proc_vpu_mem_fops);
	return 0;

}
late_initcall(vpu_mem_proc_init);
#endif /* CONFIG_PROC_FS */

