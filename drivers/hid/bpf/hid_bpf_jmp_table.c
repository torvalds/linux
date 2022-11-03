// SPDX-License-Identifier: GPL-2.0-only

/*
 *  HID-BPF support for Linux
 *
 *  Copyright (c) 2022 Benjamin Tissoires
 */

#include <linux/bitops.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/circ_buf.h>
#include <linux/filter.h>
#include <linux/hid.h>
#include <linux/hid_bpf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "hid_bpf_dispatch.h"
#include "entrypoints/entrypoints.lskel.h"

#define HID_BPF_MAX_PROGS 1024 /* keep this in sync with preloaded bpf,
				* needs to be a power of 2 as we use it as
				* a circular buffer
				*/

#define NEXT(idx) (((idx) + 1) & (HID_BPF_MAX_PROGS - 1))
#define PREV(idx) (((idx) - 1) & (HID_BPF_MAX_PROGS - 1))

/*
 * represents one attached program stored in the hid jump table
 */
struct hid_bpf_prog_entry {
	struct bpf_prog *prog;
	struct hid_device *hdev;
	enum hid_bpf_prog_type type;
	u16 idx;
};

struct hid_bpf_jmp_table {
	struct bpf_map *map;
	struct bpf_map *prog_keys;
	struct hid_bpf_prog_entry entries[HID_BPF_MAX_PROGS]; /* compacted list, circular buffer */
	int tail, head;
	struct bpf_prog *progs[HID_BPF_MAX_PROGS]; /* idx -> progs mapping */
	unsigned long enabled[BITS_TO_LONGS(HID_BPF_MAX_PROGS)];
};

#define FOR_ENTRIES(__i, __start, __end) \
	for (__i = __start; CIRC_CNT(__end, __i, HID_BPF_MAX_PROGS); __i = NEXT(__i))

static struct hid_bpf_jmp_table jmp_table;

static DEFINE_MUTEX(hid_bpf_attach_lock);		/* held when attaching/detaching programs */

static void hid_bpf_release_progs(struct work_struct *work);

static DECLARE_WORK(release_work, hid_bpf_release_progs);

BTF_ID_LIST(hid_bpf_btf_ids)
BTF_ID(func, hid_bpf_device_event)			/* HID_BPF_PROG_TYPE_DEVICE_EVENT */

static int hid_bpf_max_programs(enum hid_bpf_prog_type type)
{
	switch (type) {
	case HID_BPF_PROG_TYPE_DEVICE_EVENT:
		return HID_BPF_MAX_PROGS_PER_DEV;
	default:
		return -EINVAL;
	}
}

static int hid_bpf_program_count(struct hid_device *hdev,
				 struct bpf_prog *prog,
				 enum hid_bpf_prog_type type)
{
	int i, n = 0;

	if (type >= HID_BPF_PROG_TYPE_MAX)
		return -EINVAL;

	FOR_ENTRIES(i, jmp_table.tail, jmp_table.head) {
		struct hid_bpf_prog_entry *entry = &jmp_table.entries[i];

		if (type != HID_BPF_PROG_TYPE_UNDEF && entry->type != type)
			continue;

		if (hdev && entry->hdev != hdev)
			continue;

		if (prog && entry->prog != prog)
			continue;

		n++;
	}

	return n;
}

__weak noinline int __hid_bpf_tail_call(struct hid_bpf_ctx *ctx)
{
	return 0;
}
ALLOW_ERROR_INJECTION(__hid_bpf_tail_call, ERRNO);

int hid_bpf_prog_run(struct hid_device *hdev, enum hid_bpf_prog_type type,
		     struct hid_bpf_ctx_kern *ctx_kern)
{
	struct hid_bpf_prog_list *prog_list;
	int i, idx, err = 0;

	rcu_read_lock();
	prog_list = rcu_dereference(hdev->bpf.progs[type]);

	if (!prog_list)
		goto out_unlock;

	for (i = 0; i < prog_list->prog_cnt; i++) {
		idx = prog_list->prog_idx[i];

		if (!test_bit(idx, jmp_table.enabled))
			continue;

		ctx_kern->ctx.index = idx;
		err = __hid_bpf_tail_call(&ctx_kern->ctx);
		if (err)
			break;
	}

 out_unlock:
	rcu_read_unlock();

	return err;
}

/*
 * assign the list of programs attached to a given hid device.
 */
static void __hid_bpf_set_hdev_progs(struct hid_device *hdev, struct hid_bpf_prog_list *new_list,
				     enum hid_bpf_prog_type type)
{
	struct hid_bpf_prog_list *old_list;

	spin_lock(&hdev->bpf.progs_lock);
	old_list = rcu_dereference_protected(hdev->bpf.progs[type],
					     lockdep_is_held(&hdev->bpf.progs_lock));
	rcu_assign_pointer(hdev->bpf.progs[type], new_list);
	spin_unlock(&hdev->bpf.progs_lock);
	synchronize_rcu();

	kfree(old_list);
}

/*
 * allocate and populate the list of programs attached to a given hid device.
 *
 * Must be called under lock.
 */
static int hid_bpf_populate_hdev(struct hid_device *hdev, enum hid_bpf_prog_type type)
{
	struct hid_bpf_prog_list *new_list;
	int i;

	if (type >= HID_BPF_PROG_TYPE_MAX || !hdev)
		return -EINVAL;

	if (hdev->bpf.destroyed)
		return 0;

	new_list = kzalloc(sizeof(*new_list), GFP_KERNEL);
	if (!new_list)
		return -ENOMEM;

	FOR_ENTRIES(i, jmp_table.tail, jmp_table.head) {
		struct hid_bpf_prog_entry *entry = &jmp_table.entries[i];

		if (entry->type == type && entry->hdev == hdev &&
		    test_bit(entry->idx, jmp_table.enabled))
			new_list->prog_idx[new_list->prog_cnt++] = entry->idx;
	}

	__hid_bpf_set_hdev_progs(hdev, new_list, type);

	return 0;
}

static void __hid_bpf_do_release_prog(int map_fd, unsigned int idx)
{
	skel_map_delete_elem(map_fd, &idx);
	jmp_table.progs[idx] = NULL;
}

static void hid_bpf_release_progs(struct work_struct *work)
{
	int i, j, n, map_fd = -1;

	if (!jmp_table.map)
		return;

	/* retrieve a fd of our prog_array map in BPF */
	map_fd = skel_map_get_fd_by_id(jmp_table.map->id);
	if (map_fd < 0)
		return;

	mutex_lock(&hid_bpf_attach_lock); /* protects against attaching new programs */

	/* detach unused progs from HID devices */
	FOR_ENTRIES(i, jmp_table.tail, jmp_table.head) {
		struct hid_bpf_prog_entry *entry = &jmp_table.entries[i];
		enum hid_bpf_prog_type type;
		struct hid_device *hdev;

		if (test_bit(entry->idx, jmp_table.enabled))
			continue;

		/* we have an attached prog */
		if (entry->hdev) {
			hdev = entry->hdev;
			type = entry->type;

			hid_bpf_populate_hdev(hdev, type);

			/* mark all other disabled progs from hdev of the given type as detached */
			FOR_ENTRIES(j, i, jmp_table.head) {
				struct hid_bpf_prog_entry *next;

				next = &jmp_table.entries[j];

				if (test_bit(next->idx, jmp_table.enabled))
					continue;

				if (next->hdev == hdev && next->type == type)
					next->hdev = NULL;
			}
		}
	}

	/* remove all unused progs from the jump table */
	FOR_ENTRIES(i, jmp_table.tail, jmp_table.head) {
		struct hid_bpf_prog_entry *entry = &jmp_table.entries[i];

		if (test_bit(entry->idx, jmp_table.enabled))
			continue;

		if (entry->prog)
			__hid_bpf_do_release_prog(map_fd, entry->idx);
	}

	/* compact the entry list */
	n = jmp_table.tail;
	FOR_ENTRIES(i, jmp_table.tail, jmp_table.head) {
		struct hid_bpf_prog_entry *entry = &jmp_table.entries[i];

		if (!test_bit(entry->idx, jmp_table.enabled))
			continue;

		jmp_table.entries[n] = jmp_table.entries[i];
		n = NEXT(n);
	}

	jmp_table.head = n;

	mutex_unlock(&hid_bpf_attach_lock);

	if (map_fd >= 0)
		close_fd(map_fd);
}

static void hid_bpf_release_prog_at(int idx)
{
	int map_fd = -1;

	/* retrieve a fd of our prog_array map in BPF */
	map_fd = skel_map_get_fd_by_id(jmp_table.map->id);
	if (map_fd < 0)
		return;

	__hid_bpf_do_release_prog(map_fd, idx);

	close(map_fd);
}

/*
 * Insert the given BPF program represented by its fd in the jmp table.
 * Returns the index in the jump table or a negative error.
 */
static int hid_bpf_insert_prog(int prog_fd, struct bpf_prog *prog)
{
	int i, cnt, index = -1, map_fd = -1, progs_map_fd = -1, err = -EINVAL;

	/* retrieve a fd of our prog_array map in BPF */
	map_fd = skel_map_get_fd_by_id(jmp_table.map->id);
	/* take an fd for the table of progs we monitor with SEC("fexit/bpf_prog_release") */
	progs_map_fd = skel_map_get_fd_by_id(jmp_table.prog_keys->id);

	if (map_fd < 0 || progs_map_fd < 0) {
		err = -EINVAL;
		goto out;
	}

	cnt = 0;
	/* find the first available index in the jmp_table
	 * and count how many time this program has been inserted
	 */
	for (i = 0; i < HID_BPF_MAX_PROGS; i++) {
		if (!jmp_table.progs[i] && index < 0) {
			/* mark the index as used */
			jmp_table.progs[i] = prog;
			index = i;
			__set_bit(i, jmp_table.enabled);
			cnt++;
		} else {
			if (jmp_table.progs[i] == prog)
				cnt++;
		}
	}
	if (index < 0) {
		err = -ENOMEM;
		goto out;
	}

	/* insert the program in the jump table */
	err = skel_map_update_elem(map_fd, &index, &prog_fd, 0);
	if (err)
		goto out;

	/* insert the program in the prog list table */
	err = skel_map_update_elem(progs_map_fd, &prog, &cnt, 0);
	if (err)
		goto out;

	/* return the index */
	err = index;

 out:
	if (err < 0)
		__hid_bpf_do_release_prog(map_fd, index);
	if (map_fd >= 0)
		close_fd(map_fd);
	if (progs_map_fd >= 0)
		close_fd(progs_map_fd);
	return err;
}

int hid_bpf_get_prog_attach_type(int prog_fd)
{
	struct bpf_prog *prog = NULL;
	int i;
	int prog_type = HID_BPF_PROG_TYPE_UNDEF;

	prog = bpf_prog_get(prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	for (i = 0; i < HID_BPF_PROG_TYPE_MAX; i++) {
		if (hid_bpf_btf_ids[i] == prog->aux->attach_btf_id) {
			prog_type = i;
			break;
		}
	}

	bpf_prog_put(prog);

	return prog_type;
}

/* called from syscall */
noinline int
__hid_bpf_attach_prog(struct hid_device *hdev, enum hid_bpf_prog_type prog_type,
		      int prog_fd, __u32 flags)
{
	struct bpf_prog *prog = NULL;
	struct hid_bpf_prog_entry *prog_entry;
	int cnt, err = -EINVAL, prog_idx = -1;

	/* take a ref on the prog itself */
	prog = bpf_prog_get(prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	mutex_lock(&hid_bpf_attach_lock);

	/* do not attach too many programs to a given HID device */
	cnt = hid_bpf_program_count(hdev, NULL, prog_type);
	if (cnt < 0) {
		err = cnt;
		goto out_unlock;
	}

	if (cnt >= hid_bpf_max_programs(prog_type)) {
		err = -E2BIG;
		goto out_unlock;
	}

	prog_idx = hid_bpf_insert_prog(prog_fd, prog);
	/* if the jmp table is full, abort */
	if (prog_idx < 0) {
		err = prog_idx;
		goto out_unlock;
	}

	if (flags & HID_BPF_FLAG_INSERT_HEAD) {
		/* take the previous prog_entry slot */
		jmp_table.tail = PREV(jmp_table.tail);
		prog_entry = &jmp_table.entries[jmp_table.tail];
	} else {
		/* take the next prog_entry slot */
		prog_entry = &jmp_table.entries[jmp_table.head];
		jmp_table.head = NEXT(jmp_table.head);
	}

	/* we steal the ref here */
	prog_entry->prog = prog;
	prog_entry->idx = prog_idx;
	prog_entry->hdev = hdev;
	prog_entry->type = prog_type;

	/* finally store the index in the device list */
	err = hid_bpf_populate_hdev(hdev, prog_type);
	if (err)
		hid_bpf_release_prog_at(prog_idx);

 out_unlock:
	mutex_unlock(&hid_bpf_attach_lock);

	/* we only use prog as a key in the various tables, so we don't need to actually
	 * increment the ref count.
	 */
	bpf_prog_put(prog);

	return err;
}

void __hid_bpf_destroy_device(struct hid_device *hdev)
{
	int type, i;
	struct hid_bpf_prog_list *prog_list;

	rcu_read_lock();

	for (type = 0; type < HID_BPF_PROG_TYPE_MAX; type++) {
		prog_list = rcu_dereference(hdev->bpf.progs[type]);

		if (!prog_list)
			continue;

		for (i = 0; i < prog_list->prog_cnt; i++)
			__clear_bit(prog_list->prog_idx[i], jmp_table.enabled);
	}

	rcu_read_unlock();

	for (type = 0; type < HID_BPF_PROG_TYPE_MAX; type++)
		__hid_bpf_set_hdev_progs(hdev, NULL, type);

	/* schedule release of all detached progs */
	schedule_work(&release_work);
}

noinline bool
call_hid_bpf_prog_release(u64 prog_key, int table_cnt)
{
	/* compare with how many refs are left in the bpf program */
	struct bpf_prog *prog = (struct bpf_prog *)prog_key;
	int idx;

	if (!prog)
		return false;

	if (atomic64_read(&prog->aux->refcnt) != table_cnt)
		return false;

	/* we don't need locking here because the entries in the progs table
	 * are stable:
	 * if there are other users (and the progs entries might change), we
	 * would return in the statement above.
	 */
	for (idx = 0; idx < HID_BPF_MAX_PROGS; idx++) {
		if (jmp_table.progs[idx] == prog) {
			__clear_bit(idx, jmp_table.enabled);
			break;
		}
	}
	if (idx >= HID_BPF_MAX_PROGS) {
		/* should never happen if we get our refcount right */
		idx = -1;
	}

	/* schedule release of all detached progs */
	schedule_work(&release_work);
	return idx >= 0;
}

#define HID_BPF_PROGS_COUNT 3

static struct bpf_link *links[HID_BPF_PROGS_COUNT];
static struct entrypoints_bpf *skel;

void hid_bpf_free_links_and_skel(void)
{
	int i;

	/* the following is enough to release all programs attached to hid */
	if (jmp_table.prog_keys)
		bpf_map_put_with_uref(jmp_table.prog_keys);

	if (jmp_table.map)
		bpf_map_put_with_uref(jmp_table.map);

	for (i = 0; i < ARRAY_SIZE(links); i++) {
		if (!IS_ERR_OR_NULL(links[i]))
			bpf_link_put(links[i]);
	}
	entrypoints_bpf__destroy(skel);
}

#define ATTACH_AND_STORE_LINK(__name) do {					\
	err = entrypoints_bpf__##__name##__attach(skel);			\
	if (err)								\
		goto out;							\
										\
	links[idx] = bpf_link_get_from_fd(skel->links.__name##_fd);		\
	if (IS_ERR(links[idx])) {						\
		err = PTR_ERR(links[idx]);					\
		goto out;							\
	}									\
										\
	/* Avoid taking over stdin/stdout/stderr of init process. Zeroing out	\
	 * makes skel_closenz() a no-op later in iterators_bpf__destroy().	\
	 */									\
	close_fd(skel->links.__name##_fd);					\
	skel->links.__name##_fd = 0;						\
	idx++;									\
} while (0)

int hid_bpf_preload_skel(void)
{
	int err, idx = 0;

	skel = entrypoints_bpf__open();
	if (!skel)
		return -ENOMEM;

	err = entrypoints_bpf__load(skel);
	if (err)
		goto out;

	jmp_table.map = bpf_map_get_with_uref(skel->maps.hid_jmp_table.map_fd);
	if (IS_ERR(jmp_table.map)) {
		err = PTR_ERR(jmp_table.map);
		goto out;
	}

	jmp_table.prog_keys = bpf_map_get_with_uref(skel->maps.progs_map.map_fd);
	if (IS_ERR(jmp_table.prog_keys)) {
		err = PTR_ERR(jmp_table.prog_keys);
		goto out;
	}

	ATTACH_AND_STORE_LINK(hid_tail_call);
	ATTACH_AND_STORE_LINK(hid_prog_release);
	ATTACH_AND_STORE_LINK(hid_free_inode);

	return 0;
out:
	hid_bpf_free_links_and_skel();
	return err;
}
