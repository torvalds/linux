// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/mm.h>

#include "qcomtee.h"

/**
 * DOC: Memory and Mapping Objects
 *
 * QTEE uses memory objects for memory sharing with Linux.
 * A memory object can be a standard dma_buf or a contiguous memory range,
 * e.g., tee_shm. A memory object should support one operation: map. When
 * invoked by QTEE, a mapping object is generated. A mapping object supports
 * one operation: unmap.
 *
 *  (1) To map a memory object, QTEE invokes the primordial object with
 *      %QCOMTEE_OBJECT_OP_MAP_REGION operation; see
 *      qcomtee_primordial_obj_dispatch().
 *  (2) To unmap a memory object, QTEE releases the mapping object which
 *      calls qcomtee_mem_object_release().
 *
 * The map operation is implemented in the primordial object as a privileged
 * operation instead of qcomtee_mem_object_dispatch(). Otherwise, on
 * platforms without shm_bridge, a user can trick QTEE into writing to the
 * kernel memory by passing a user object as a memory object and returning a
 * random physical address as the result of the mapping request.
 */

struct qcomtee_mem_object {
	struct qcomtee_object object;
	struct tee_shm *shm;
	/* QTEE requires these felids to be page aligned. */
	phys_addr_t paddr; /* Physical address of range. */
	size_t size; /* Size of the range. */
};

#define to_qcomtee_mem_object(o) \
	container_of((o), struct qcomtee_mem_object, object)

static struct qcomtee_object_operations qcomtee_mem_object_ops;

/* Is it a memory object using tee_shm? */
int is_qcomtee_memobj_object(struct qcomtee_object *object)
{
	return object != NULL_QCOMTEE_OBJECT &&
	       typeof_qcomtee_object(object) == QCOMTEE_OBJECT_TYPE_CB &&
	       object->ops == &qcomtee_mem_object_ops;
}

static int qcomtee_mem_object_dispatch(struct qcomtee_object_invoke_ctx *oic,
				       struct qcomtee_object *object, u32 op,
				       struct qcomtee_arg *args)
{
	return -EINVAL;
}

static void qcomtee_mem_object_release(struct qcomtee_object *object)
{
	struct qcomtee_mem_object *mem_object = to_qcomtee_mem_object(object);

	/* Matching get is in qcomtee_memobj_param_to_object(). */
	tee_shm_put(mem_object->shm);
	kfree(mem_object);
}

static struct qcomtee_object_operations qcomtee_mem_object_ops = {
	.release = qcomtee_mem_object_release,
	.dispatch = qcomtee_mem_object_dispatch,
};

/**
 * qcomtee_memobj_param_to_object() - OBJREF parameter to &struct qcomtee_object.
 * @object: object returned.
 * @param: TEE parameter.
 * @ctx: context in which the conversion should happen.
 *
 * @param is an OBJREF with %QCOMTEE_OBJREF_FLAG_MEM flags.
 *
 * Return: On success return 0 or <0 on failure.
 */
int qcomtee_memobj_param_to_object(struct qcomtee_object **object,
				   struct tee_param *param,
				   struct tee_context *ctx)
{
	struct qcomtee_mem_object *mem_object __free(kfree) = NULL;
	struct tee_shm *shm;
	int err;

	mem_object = kzalloc(sizeof(*mem_object), GFP_KERNEL);
	if (!mem_object)
		return -ENOMEM;

	shm = tee_shm_get_from_id(ctx, param->u.objref.id);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	/* mem-object wrapping the memref. */
	err = qcomtee_object_user_init(&mem_object->object,
				       QCOMTEE_OBJECT_TYPE_CB,
				       &qcomtee_mem_object_ops, "tee-shm-%d",
				       shm->id);
	if (err) {
		tee_shm_put(shm);

		return err;
	}

	mem_object->paddr = shm->paddr;
	mem_object->size = shm->size;
	mem_object->shm = shm;

	*object = &no_free_ptr(mem_object)->object;

	return 0;
}

/* Reverse what qcomtee_memobj_param_to_object() does. */
int qcomtee_memobj_param_from_object(struct tee_param *param,
				     struct qcomtee_object *object,
				     struct tee_context *ctx)
{
	struct qcomtee_mem_object *mem_object;

	mem_object = to_qcomtee_mem_object(object);
	/* Sure if the memobj is in a same context it is originated from. */
	if (mem_object->shm->ctx != ctx)
		return -EINVAL;

	param->u.objref.id = mem_object->shm->id;
	param->u.objref.flags = QCOMTEE_OBJREF_FLAG_MEM;

	/* Passing shm->id to userspace; drop the reference. */
	qcomtee_object_put(object);

	return 0;
}

/**
 * qcomtee_mem_object_map() - Map a memory object.
 * @object: memory object.
 * @map_object: created mapping object.
 * @mem_paddr: physical address of the memory.
 * @mem_size: size of the memory.
 * @perms: QTEE access permissions.
 *
 * Return: On success return 0 or <0 on failure.
 */
int qcomtee_mem_object_map(struct qcomtee_object *object,
			   struct qcomtee_object **map_object, u64 *mem_paddr,
			   u64 *mem_size, u32 *perms)
{
	struct qcomtee_mem_object *mem_object = to_qcomtee_mem_object(object);

	/* Reuses the memory object as a mapping object by re-sharing it. */
	qcomtee_object_get(&mem_object->object);

	*map_object = &mem_object->object;
	*mem_paddr = mem_object->paddr;
	*mem_size = mem_object->size;
	*perms = QCOM_SCM_PERM_RW;

	return 0;
}
