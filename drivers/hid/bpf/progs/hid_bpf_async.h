/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2024 Benjamin Tissoires
 */

#ifndef __HID_BPF_ASYNC_H__
#define __HID_BPF_ASYNC_H__

#ifndef HID_BPF_ASYNC_MAX_CTX
#error "HID_BPF_ASYNC_MAX_CTX should be set to the maximum number of concurrent async functions"
#endif /* HID_BPF_ASYNC_MAX_CTX */

#define CLOCK_MONOTONIC		1

typedef int (*hid_bpf_async_callback_t)(void *map, int *key, void *value);

enum hid_bpf_async_state {
	HID_BPF_ASYNC_STATE_UNSET = 0,
	HID_BPF_ASYNC_STATE_INITIALIZING,
	HID_BPF_ASYNC_STATE_INITIALIZED,
	HID_BPF_ASYNC_STATE_STARTING,
	HID_BPF_ASYNC_STATE_RUNNING,
};

struct hid_bpf_async_map_elem {
	struct bpf_spin_lock lock;
	enum hid_bpf_async_state state;
	struct bpf_timer t;
	struct bpf_wq wq;
	u32 hid;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, HID_BPF_ASYNC_MAX_CTX);
	__type(key, u32);
	__type(value, struct hid_bpf_async_map_elem);
} hid_bpf_async_ctx_map SEC(".maps");

/**
 * HID_BPF_ASYNC_CB: macro to define an async callback used in a bpf_wq
 *
 * The caller is responsible for allocating a key in the async map
 * with hid_bpf_async_get_ctx().
 */
#define HID_BPF_ASYNC_CB(cb)					\
cb(void *map, int *key, void *value);				\
static __always_inline int					\
____##cb(struct hid_bpf_ctx *ctx);				\
typeof(cb(0, 0, 0)) cb(void *map, int *key, void *value)	\
{								\
	struct hid_bpf_async_map_elem *e;				\
	struct hid_bpf_ctx *ctx;				\
								\
	e = (struct hid_bpf_async_map_elem *)value;			\
	ctx = hid_bpf_allocate_context(e->hid);			\
	if (!ctx)						\
		return 0; /* EPERM check */			\
								\
	e->state = HID_BPF_ASYNC_STATE_RUNNING;			\
								\
	____##cb(ctx);						\
								\
	e->state = HID_BPF_ASYNC_STATE_INITIALIZED;		\
	hid_bpf_release_context(ctx);				\
	return 0;						\
}								\
static __always_inline int					\
____##cb

/**
 * ASYNC: macro to automatically handle async callbacks contexts
 *
 * Needs to be used in conjunction with HID_BPF_ASYNC_INIT and HID_BPF_ASYNC_DELAYED_CALL
 */
#define HID_BPF_ASYNC_FUN(fun)						\
fun(struct hid_bpf_ctx *ctx);					\
int ____key__##fun;						\
static int ____async_init_##fun(void)				\
{								\
	____key__##fun = hid_bpf_async_get_ctx();			\
	if (____key__##fun < 0)					\
		return ____key__##fun;				\
	return 0;						\
}								\
static int HID_BPF_ASYNC_CB(____##fun##_cb)(struct hid_bpf_ctx *hctx)	\
{								\
	return fun(hctx);					\
}								\
typeof(fun(0)) fun

#define HID_BPF_ASYNC_INIT(fun)	____async_init_##fun()
#define HID_BPF_ASYNC_DELAYED_CALL(fun, ctx, delay)		\
	hid_bpf_async_delayed_call(ctx, delay, ____key__##fun, ____##fun##_cb)

/*
 * internal cb for starting the delayed work callback in a workqueue.
 */
static int __start_wq_timer_cb(void *map, int *key, void *value)
{
	struct hid_bpf_async_map_elem *e = (struct hid_bpf_async_map_elem *)value;

	bpf_wq_start(&e->wq, 0);

	return 0;
}

static int hid_bpf_async_find_empty_key(void)
{
	int i;

	bpf_for(i, 0, HID_BPF_ASYNC_MAX_CTX) {
		struct hid_bpf_async_map_elem *elem;
		int key = i;

		elem = bpf_map_lookup_elem(&hid_bpf_async_ctx_map, &key);
		if (!elem)
			return -ENOMEM; /* should never happen */

		bpf_spin_lock(&elem->lock);

		if (elem->state == HID_BPF_ASYNC_STATE_UNSET) {
			elem->state = HID_BPF_ASYNC_STATE_INITIALIZING;
			bpf_spin_unlock(&elem->lock);
			return i;
		}

		bpf_spin_unlock(&elem->lock);
	}

	return -EINVAL;
}

static int hid_bpf_async_get_ctx(void)
{
	int key = hid_bpf_async_find_empty_key();
	struct hid_bpf_async_map_elem *elem;
	int err;

	if (key < 0)
		return key;

	elem = bpf_map_lookup_elem(&hid_bpf_async_ctx_map, &key);
	if (!elem)
		return -EINVAL;

	err = bpf_timer_init(&elem->t, &hid_bpf_async_ctx_map, CLOCK_MONOTONIC);
	if (err)
		return err;

	err = bpf_timer_set_callback(&elem->t, __start_wq_timer_cb);
	if (err)
		return err;

	err = bpf_wq_init(&elem->wq, &hid_bpf_async_ctx_map, 0);
	if (err)
		return err;

	elem->state = HID_BPF_ASYNC_STATE_INITIALIZED;

	return key;
}

static inline u64 ms_to_ns(u64 milliseconds)
{
	return (u64)milliseconds * 1000UL * 1000UL;
}

static int hid_bpf_async_delayed_call(struct hid_bpf_ctx *hctx, u64 milliseconds, int key,
			      hid_bpf_async_callback_t wq_cb)
{
	struct hid_bpf_async_map_elem *elem;
	int err;

	elem = bpf_map_lookup_elem(&hid_bpf_async_ctx_map, &key);
	if (!elem)
		return -EINVAL;

	bpf_spin_lock(&elem->lock);
	/* The wq must be:
	 * - HID_BPF_ASYNC_STATE_INITIALIZED -> it's been initialized and ready to be called
	 * - HID_BPF_ASYNC_STATE_RUNNING -> possible re-entry from the wq itself
	 */
	if (elem->state != HID_BPF_ASYNC_STATE_INITIALIZED &&
	    elem->state != HID_BPF_ASYNC_STATE_RUNNING) {
		bpf_spin_unlock(&elem->lock);
		return -EINVAL;
	}
	elem->state = HID_BPF_ASYNC_STATE_STARTING;
	bpf_spin_unlock(&elem->lock);

	elem->hid = hctx->hid->id;

	err = bpf_wq_set_callback(&elem->wq, wq_cb, 0);
	if (err)
		return err;

	if (milliseconds) {
		/* needed for every call because a cancel might unset this */
		err = bpf_timer_set_callback(&elem->t, __start_wq_timer_cb);
		if (err)
			return err;

		err = bpf_timer_start(&elem->t, ms_to_ns(milliseconds), 0);
		if (err)
			return err;

		return 0;
	}

	return bpf_wq_start(&elem->wq, 0);
}

static inline int hid_bpf_async_call(struct hid_bpf_ctx *ctx, int key,
				     hid_bpf_async_callback_t wq_cb)
{
	return hid_bpf_async_delayed_call(ctx, 0, key, wq_cb);
}

#endif /* __HID_BPF_ASYNC_H__ */
