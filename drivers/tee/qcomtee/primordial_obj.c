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
 *  - %QCOMTEE_OBJECT_OP_YIELD to yield by the thread running in QTEE.
 *  - %QCOMTEE_OBJECT_OP_SLEEP to wait for a period of time.
 */

#define QCOMTEE_OBJECT_OP_YIELD 1
#define QCOMTEE_OBJECT_OP_SLEEP 2

static int
qcomtee_primordial_obj_dispatch(struct qcomtee_object_invoke_ctx *oic,
				struct qcomtee_object *primordial_object_unused,
				u32 op, struct qcomtee_arg *args)
{
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
	default:
		err = -EINVAL;
	}

	return err;
}

static struct qcomtee_object_operations qcomtee_primordial_obj_ops = {
	.dispatch = qcomtee_primordial_obj_dispatch,
};

struct qcomtee_object qcomtee_primordial_object = {
	.name = "primordial",
	.object_type = QCOMTEE_OBJECT_TYPE_CB,
	.ops = &qcomtee_primordial_obj_ops
};
