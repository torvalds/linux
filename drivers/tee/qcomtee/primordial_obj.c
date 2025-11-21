// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include "qcomtee.h"

/**
 * DOC: Primordial Object
 *
 * After boot, the kernel provides a static object of type
 * %QCOMTEE_OBJECT_TYPE_CB called the primordial object. This object is used
 * for native kernel services or privileged operations.
 *
 * We support:
 *  - %QCOMTEE_OBJECT_OP_MAP_REGION to map a memory object and return mapping
 *    object and mapping information (see qcomtee_mem_object_map()).
 *  - %QCOMTEE_OBJECT_OP_YIELD to yield by the thread running in QTEE.
 *  - %QCOMTEE_OBJECT_OP_SLEEP to wait for a period of time.
 */

#define QCOMTEE_OBJECT_OP_MAP_REGION 0
#define QCOMTEE_OBJECT_OP_YIELD 1
#define QCOMTEE_OBJECT_OP_SLEEP 2

/* Mapping information format as expected by QTEE. */
struct qcomtee_mapping_info {
	u64 paddr;
	u64 len;
	u32 perms;
} __packed;

static int
qcomtee_primordial_obj_dispatch(struct qcomtee_object_invoke_ctx *oic,
				struct qcomtee_object *primordial_object_unused,
				u32 op, struct qcomtee_arg *args)
{
	struct qcomtee_mapping_info *map_info;
	struct qcomtee_object *mem_object;
	struct qcomtee_object *map_object;
	int err = 0;

	switch (op) {
	case QCOMTEE_OBJECT_OP_YIELD:
		cond_resched();
		/* No output object. */
		oic->data = NULL;

		break;
	case QCOMTEE_OBJECT_OP_SLEEP:
		/* Check message format matched QCOMTEE_OBJECT_OP_SLEEP op. */
		if (qcomtee_args_len(args) != 1 ||
		    args[0].type != QCOMTEE_ARG_TYPE_IB ||
		    args[0].b.size < sizeof(u32))
			return -EINVAL;

		msleep(*(u32 *)(args[0].b.addr));
		/* No output object. */
		oic->data = NULL;

		break;
	case QCOMTEE_OBJECT_OP_MAP_REGION:
		if (qcomtee_args_len(args) != 3 ||
		    args[0].type != QCOMTEE_ARG_TYPE_OB ||
		    args[1].type != QCOMTEE_ARG_TYPE_IO ||
		    args[2].type != QCOMTEE_ARG_TYPE_OO ||
		    args[0].b.size < sizeof(struct qcomtee_mapping_info))
			return -EINVAL;

		map_info = args[0].b.addr;
		mem_object = args[1].o;

		qcomtee_mem_object_map(mem_object, &map_object,
				       &map_info->paddr, &map_info->len,
				       &map_info->perms);

		args[2].o = map_object;
		/* One output object; pass it for cleanup to notify. */
		oic->data = map_object;

		qcomtee_object_put(mem_object);

		break;
	default:
		err = -EINVAL;
	}

	return err;
}

/* Called after submitting the callback response. */
static void qcomtee_primordial_obj_notify(struct qcomtee_object_invoke_ctx *oic,
					  struct qcomtee_object *unused,
					  int err)
{
	struct qcomtee_object *object = oic->data;

	/* If err, QTEE did not obtain mapping object. Drop it. */
	if (object && err)
		qcomtee_object_put(object);
}

static struct qcomtee_object_operations qcomtee_primordial_obj_ops = {
	.dispatch = qcomtee_primordial_obj_dispatch,
	.notify = qcomtee_primordial_obj_notify,
};

struct qcomtee_object qcomtee_primordial_object = {
	.name = "primordial",
	.object_type = QCOMTEE_OBJECT_TYPE_CB,
	.ops = &qcomtee_primordial_obj_ops
};
