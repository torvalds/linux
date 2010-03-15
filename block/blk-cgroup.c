/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */
#include <linux/ioprio.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/err.h>
#include "blk-cgroup.h"

static DEFINE_SPINLOCK(blkio_list_lock);
static LIST_HEAD(blkio_list);

struct blkio_cgroup blkio_root_cgroup = { .weight = 2*BLKIO_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct cgroup_subsys_state *blkiocg_create(struct cgroup_subsys *,
						  struct cgroup *);
static int blkiocg_can_attach(struct cgroup_subsys *, struct cgroup *,
			      struct task_struct *, bool);
static void blkiocg_attach(struct cgroup_subsys *, struct cgroup *,
			   struct cgroup *, struct task_struct *, bool);
static void blkiocg_destroy(struct cgroup_subsys *, struct cgroup *);
static int blkiocg_populate(struct cgroup_subsys *, struct cgroup *);

struct cgroup_subsys blkio_subsys = {
	.name = "blkio",
	.create = blkiocg_create,
	.can_attach = blkiocg_can_attach,
	.attach = blkiocg_attach,
	.destroy = blkiocg_destroy,
	.populate = blkiocg_populate,
#ifdef CONFIG_BLK_CGROUP
	/* note: blkio_subsys_id is otherwise defined in blk-cgroup.h */
	.subsys_id = blkio_subsys_id,
#endif
	.use_id = 1,
	.module = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(blkio_subsys);

struct blkio_cgroup *cgroup_to_blkio_cgroup(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, blkio_subsys_id),
			    struct blkio_cgroup, css);
}
EXPORT_SYMBOL_GPL(cgroup_to_blkio_cgroup);

void blkiocg_update_blkio_group_stats(struct blkio_group *blkg,
			unsigned long time, unsigned long sectors)
{
	blkg->time += time;
	blkg->sectors += sectors;
}
EXPORT_SYMBOL_GPL(blkiocg_update_blkio_group_stats);

void blkiocg_add_blkio_group(struct blkio_cgroup *blkcg,
			struct blkio_group *blkg, void *key, dev_t dev)
{
	unsigned long flags;

	spin_lock_irqsave(&blkcg->lock, flags);
	rcu_assign_pointer(blkg->key, key);
	blkg->blkcg_id = css_id(&blkcg->css);
	hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
	spin_unlock_irqrestore(&blkcg->lock, flags);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	/* Need to take css reference ? */
	cgroup_path(blkcg->css.cgroup, blkg->path, sizeof(blkg->path));
#endif
	blkg->dev = dev;
}
EXPORT_SYMBOL_GPL(blkiocg_add_blkio_group);

static void __blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	hlist_del_init_rcu(&blkg->blkcg_node);
	blkg->blkcg_id = 0;
}

/*
 * returns 0 if blkio_group was still on cgroup list. Otherwise returns 1
 * indicating that blk_group was unhashed by the time we got to it.
 */
int blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	struct blkio_cgroup *blkcg;
	unsigned long flags;
	struct cgroup_subsys_state *css;
	int ret = 1;

	rcu_read_lock();
	css = css_lookup(&blkio_subsys, blkg->blkcg_id);
	if (!css)
		goto out;

	blkcg = container_of(css, struct blkio_cgroup, css);
	spin_lock_irqsave(&blkcg->lock, flags);
	if (!hlist_unhashed(&blkg->blkcg_node)) {
		__blkiocg_del_blkio_group(blkg);
		ret = 0;
	}
	spin_unlock_irqrestore(&blkcg->lock, flags);
out:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(blkiocg_del_blkio_group);

/* called under rcu_read_lock(). */
struct blkio_group *blkiocg_lookup_group(struct blkio_cgroup *blkcg, void *key)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	void *__key;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {
		__key = blkg->key;
		if (__key == key)
			return blkg;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(blkiocg_lookup_group);

#define SHOW_FUNCTION(__VAR)						\
static u64 blkiocg_##__VAR##_read(struct cgroup *cgroup,		\
				       struct cftype *cftype)		\
{									\
	struct blkio_cgroup *blkcg;					\
									\
	blkcg = cgroup_to_blkio_cgroup(cgroup);				\
	return (u64)blkcg->__VAR;					\
}

SHOW_FUNCTION(weight);
#undef SHOW_FUNCTION

static int
blkiocg_weight_write(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg;
	struct blkio_group *blkg;
	struct hlist_node *n;
	struct blkio_policy_type *blkiop;

	if (val < BLKIO_WEIGHT_MIN || val > BLKIO_WEIGHT_MAX)
		return -EINVAL;

	blkcg = cgroup_to_blkio_cgroup(cgroup);
	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);
	blkcg->weight = (unsigned int)val;
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		list_for_each_entry(blkiop, &blkio_list, list)
			blkiop->ops.blkio_update_group_weight_fn(blkg,
					blkcg->weight);
	}
	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
	return 0;
}

#define SHOW_FUNCTION_PER_GROUP(__VAR)					\
static int blkiocg_##__VAR##_read(struct cgroup *cgroup,		\
			struct cftype *cftype, struct seq_file *m)	\
{									\
	struct blkio_cgroup *blkcg;					\
	struct blkio_group *blkg;					\
	struct hlist_node *n;						\
									\
	if (!cgroup_lock_live_group(cgroup))				\
		return -ENODEV;						\
									\
	blkcg = cgroup_to_blkio_cgroup(cgroup);				\
	rcu_read_lock();						\
	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {\
		if (blkg->dev)						\
			seq_printf(m, "%u:%u %lu\n", MAJOR(blkg->dev),	\
				 MINOR(blkg->dev), blkg->__VAR);	\
	}								\
	rcu_read_unlock();						\
	cgroup_unlock();						\
	return 0;							\
}

SHOW_FUNCTION_PER_GROUP(time);
SHOW_FUNCTION_PER_GROUP(sectors);
#ifdef CONFIG_DEBUG_BLK_CGROUP
SHOW_FUNCTION_PER_GROUP(dequeue);
#endif
#undef SHOW_FUNCTION_PER_GROUP

#ifdef CONFIG_DEBUG_BLK_CGROUP
void blkiocg_update_blkio_group_dequeue_stats(struct blkio_group *blkg,
			unsigned long dequeue)
{
	blkg->dequeue += dequeue;
}
EXPORT_SYMBOL_GPL(blkiocg_update_blkio_group_dequeue_stats);
#endif

struct cftype blkio_files[] = {
	{
		.name = "weight",
		.read_u64 = blkiocg_weight_read,
		.write_u64 = blkiocg_weight_write,
	},
	{
		.name = "time",
		.read_seq_string = blkiocg_time_read,
	},
	{
		.name = "sectors",
		.read_seq_string = blkiocg_sectors_read,
	},
#ifdef CONFIG_DEBUG_BLK_CGROUP
       {
		.name = "dequeue",
		.read_seq_string = blkiocg_dequeue_read,
       },
#endif
};

static int blkiocg_populate(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, subsys, blkio_files,
				ARRAY_SIZE(blkio_files));
}

static void blkiocg_destroy(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	unsigned long flags;
	struct blkio_group *blkg;
	void *key;
	struct blkio_policy_type *blkiop;

	rcu_read_lock();
remove_entry:
	spin_lock_irqsave(&blkcg->lock, flags);

	if (hlist_empty(&blkcg->blkg_list)) {
		spin_unlock_irqrestore(&blkcg->lock, flags);
		goto done;
	}

	blkg = hlist_entry(blkcg->blkg_list.first, struct blkio_group,
				blkcg_node);
	key = rcu_dereference(blkg->key);
	__blkiocg_del_blkio_group(blkg);

	spin_unlock_irqrestore(&blkcg->lock, flags);

	/*
	 * This blkio_group is being unlinked as associated cgroup is going
	 * away. Let all the IO controlling policies know about this event.
	 *
	 * Currently this is static call to one io controlling policy. Once
	 * we have more policies in place, we need some dynamic registration
	 * of callback function.
	 */
	spin_lock(&blkio_list_lock);
	list_for_each_entry(blkiop, &blkio_list, list)
		blkiop->ops.blkio_unlink_group_fn(key, blkg);
	spin_unlock(&blkio_list_lock);
	goto remove_entry;
done:
	free_css_id(&blkio_subsys, &blkcg->css);
	rcu_read_unlock();
	if (blkcg != &blkio_root_cgroup)
		kfree(blkcg);
}

static struct cgroup_subsys_state *
blkiocg_create(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg, *parent_blkcg;

	if (!cgroup->parent) {
		blkcg = &blkio_root_cgroup;
		goto done;
	}

	/* Currently we do not support hierarchy deeper than two level (0,1) */
	parent_blkcg = cgroup_to_blkio_cgroup(cgroup->parent);
	if (css_depth(&parent_blkcg->css) > 0)
		return ERR_PTR(-EINVAL);

	blkcg = kzalloc(sizeof(*blkcg), GFP_KERNEL);
	if (!blkcg)
		return ERR_PTR(-ENOMEM);

	blkcg->weight = BLKIO_WEIGHT_DEFAULT;
done:
	spin_lock_init(&blkcg->lock);
	INIT_HLIST_HEAD(&blkcg->blkg_list);

	return &blkcg->css;
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic data structures.  For now we allow a task to change
 * its cgroup only if it's the only owner of its ioc.
 */
static int blkiocg_can_attach(struct cgroup_subsys *subsys,
				struct cgroup *cgroup, struct task_struct *tsk,
				bool threadgroup)
{
	struct io_context *ioc;
	int ret = 0;

	/* task_lock() is needed to avoid races with exit_io_context() */
	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc && atomic_read(&ioc->nr_tasks) > 1)
		ret = -EINVAL;
	task_unlock(tsk);

	return ret;
}

static void blkiocg_attach(struct cgroup_subsys *subsys, struct cgroup *cgroup,
				struct cgroup *prev, struct task_struct *tsk,
				bool threadgroup)
{
	struct io_context *ioc;

	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc)
		ioc->cgroup_changed = 1;
	task_unlock(tsk);
}

void blkio_policy_register(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_add_tail(&blkiop->list, &blkio_list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_del_init(&blkiop->list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);

static int __init init_cgroup_blkio(void)
{
	return cgroup_load_subsys(&blkio_subsys);
}

static void __exit exit_cgroup_blkio(void)
{
	cgroup_unload_subsys(&blkio_subsys);
}

module_init(init_cgroup_blkio);
module_exit(exit_cgroup_blkio);
MODULE_LICENSE("GPL");
