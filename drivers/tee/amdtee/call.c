// SPDX-License-Identifier: MIT
/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 */

#include <linux/device.h>
#include <linux/tee.h>
#include <linux/tee_drv.h>
#include <linux/psp-tee.h>
#include <linux/slab.h>
#include <linux/psp-sev.h>
#include "amdtee_if.h"
#include "amdtee_private.h"

static int tee_params_to_amd_params(struct tee_param *tee, u32 count,
				    struct tee_operation *amd)
{
	int i, ret = 0;
	u32 type;

	if (!count)
		return 0;

	if (!tee || !amd || count > TEE_MAX_PARAMS)
		return -EINVAL;

	amd->param_types = 0;
	for (i = 0; i < count; i++) {
		/* AMD TEE does not support meta parameter */
		if (tee[i].attr > TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT)
			return -EINVAL;

		amd->param_types |= ((tee[i].attr & 0xF) << i * 4);
	}

	for (i = 0; i < count; i++) {
		type = TEE_PARAM_TYPE_GET(amd->param_types, i);
		pr_debug("%s: type[%d] = 0x%x\n", __func__, i, type);

		if (type == TEE_OP_PARAM_TYPE_INVALID)
			return -EINVAL;

		if (type == TEE_OP_PARAM_TYPE_NONE)
			continue;

		/* It is assumed that all values are within 2^32-1 */
		if (type > TEE_OP_PARAM_TYPE_VALUE_INOUT) {
			u32 buf_id = get_buffer_id(tee[i].u.memref.shm);

			amd->params[i].mref.buf_id = buf_id;
			amd->params[i].mref.offset = tee[i].u.memref.shm_offs;
			amd->params[i].mref.size = tee[i].u.memref.size;
			pr_debug("%s: bufid[%d] = 0x%x, offset[%d] = 0x%x, size[%d] = 0x%x\n",
				 __func__,
				 i, amd->params[i].mref.buf_id,
				 i, amd->params[i].mref.offset,
				 i, amd->params[i].mref.size);
		} else {
			if (tee[i].u.value.c)
				pr_warn("%s: Discarding value c", __func__);

			amd->params[i].val.a = tee[i].u.value.a;
			amd->params[i].val.b = tee[i].u.value.b;
			pr_debug("%s: a[%d] = 0x%x, b[%d] = 0x%x\n", __func__,
				 i, amd->params[i].val.a,
				 i, amd->params[i].val.b);
		}
	}
	return ret;
}

static int amd_params_to_tee_params(struct tee_param *tee, u32 count,
				    struct tee_operation *amd)
{
	int i, ret = 0;
	u32 type;

	if (!count)
		return 0;

	if (!tee || !amd || count > TEE_MAX_PARAMS)
		return -EINVAL;

	/* Assumes amd->param_types is valid */
	for (i = 0; i < count; i++) {
		type = TEE_PARAM_TYPE_GET(amd->param_types, i);
		pr_debug("%s: type[%d] = 0x%x\n", __func__, i, type);

		if (type == TEE_OP_PARAM_TYPE_INVALID ||
		    type > TEE_OP_PARAM_TYPE_MEMREF_INOUT)
			return -EINVAL;

		if (type == TEE_OP_PARAM_TYPE_NONE ||
		    type == TEE_OP_PARAM_TYPE_VALUE_INPUT ||
		    type == TEE_OP_PARAM_TYPE_MEMREF_INPUT)
			continue;

		/*
		 * It is assumed that buf_id remains unchanged for
		 * both open_session and invoke_cmd call
		 */
		if (type > TEE_OP_PARAM_TYPE_MEMREF_INPUT) {
			tee[i].u.memref.shm_offs = amd->params[i].mref.offset;
			tee[i].u.memref.size = amd->params[i].mref.size;
			pr_debug("%s: bufid[%d] = 0x%x, offset[%d] = 0x%x, size[%d] = 0x%x\n",
				 __func__,
				 i, amd->params[i].mref.buf_id,
				 i, amd->params[i].mref.offset,
				 i, amd->params[i].mref.size);
		} else {
			/* field 'c' not supported by AMD TEE */
			tee[i].u.value.a = amd->params[i].val.a;
			tee[i].u.value.b = amd->params[i].val.b;
			tee[i].u.value.c = 0;
			pr_debug("%s: a[%d] = 0x%x, b[%d] = 0x%x\n",
				 __func__,
				 i, amd->params[i].val.a,
				 i, amd->params[i].val.b);
		}
	}
	return ret;
}

static DEFINE_MUTEX(ta_refcount_mutex);
static LIST_HEAD(ta_list);

static u32 get_ta_refcount(u32 ta_handle)
{
	struct amdtee_ta_data *ta_data;
	u32 count = 0;

	/* Caller must hold a mutex */
	list_for_each_entry(ta_data, &ta_list, list_node)
		if (ta_data->ta_handle == ta_handle)
			return ++ta_data->refcount;

	ta_data = kzalloc(sizeof(*ta_data), GFP_KERNEL);
	if (ta_data) {
		ta_data->ta_handle = ta_handle;
		ta_data->refcount = 1;
		count = ta_data->refcount;
		list_add(&ta_data->list_node, &ta_list);
	}

	return count;
}

static u32 put_ta_refcount(u32 ta_handle)
{
	struct amdtee_ta_data *ta_data;
	u32 count = 0;

	/* Caller must hold a mutex */
	list_for_each_entry(ta_data, &ta_list, list_node)
		if (ta_data->ta_handle == ta_handle) {
			count = --ta_data->refcount;
			if (count == 0) {
				list_del(&ta_data->list_node);
				kfree(ta_data);
				break;
			}
		}

	return count;
}

int handle_unload_ta(u32 ta_handle)
{
	struct tee_cmd_unload_ta cmd = {0};
	u32 status, count;
	int ret;

	if (!ta_handle)
		return -EINVAL;

	mutex_lock(&ta_refcount_mutex);

	count = put_ta_refcount(ta_handle);

	if (count) {
		pr_debug("unload ta: not unloading %u count %u\n",
			 ta_handle, count);
		ret = -EBUSY;
		goto unlock;
	}

	cmd.ta_handle = ta_handle;

	ret = psp_tee_process_cmd(TEE_CMD_ID_UNLOAD_TA, (void *)&cmd,
				  sizeof(cmd), &status);
	if (!ret && status != 0) {
		pr_err("unload ta: status = 0x%x\n", status);
		ret = -EBUSY;
	} else {
		pr_debug("unloaded ta handle %u\n", ta_handle);
	}

unlock:
	mutex_unlock(&ta_refcount_mutex);
	return ret;
}

int handle_close_session(u32 ta_handle, u32 info)
{
	struct tee_cmd_close_session cmd = {0};
	u32 status;
	int ret;

	if (ta_handle == 0)
		return -EINVAL;

	cmd.ta_handle = ta_handle;
	cmd.session_info = info;

	ret = psp_tee_process_cmd(TEE_CMD_ID_CLOSE_SESSION, (void *)&cmd,
				  sizeof(cmd), &status);
	if (!ret && status != 0) {
		pr_err("close session: status = 0x%x\n", status);
		ret = -EBUSY;
	}

	return ret;
}

void handle_unmap_shmem(u32 buf_id)
{
	struct tee_cmd_unmap_shared_mem cmd = {0};
	u32 status;
	int ret;

	cmd.buf_id = buf_id;

	ret = psp_tee_process_cmd(TEE_CMD_ID_UNMAP_SHARED_MEM, (void *)&cmd,
				  sizeof(cmd), &status);
	if (!ret)
		pr_debug("unmap shared memory: buf_id %u status = 0x%x\n",
			 buf_id, status);
}

int handle_invoke_cmd(struct tee_ioctl_invoke_arg *arg, u32 sinfo,
		      struct tee_param *p)
{
	struct tee_cmd_invoke_cmd cmd = {0};
	int ret;

	if (!arg || (!p && arg->num_params))
		return -EINVAL;

	arg->ret_origin = TEEC_ORIGIN_COMMS;

	if (arg->session == 0) {
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return -EINVAL;
	}

	ret = tee_params_to_amd_params(p, arg->num_params, &cmd.op);
	if (ret) {
		pr_err("invalid Params. Abort invoke command\n");
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return ret;
	}

	cmd.ta_handle = get_ta_handle(arg->session);
	cmd.cmd_id = arg->func;
	cmd.session_info = sinfo;

	ret = psp_tee_process_cmd(TEE_CMD_ID_INVOKE_CMD, (void *)&cmd,
				  sizeof(cmd), &arg->ret);
	if (ret) {
		arg->ret = TEEC_ERROR_COMMUNICATION;
	} else {
		ret = amd_params_to_tee_params(p, arg->num_params, &cmd.op);
		if (unlikely(ret)) {
			pr_err("invoke command: failed to copy output\n");
			arg->ret = TEEC_ERROR_GENERIC;
			return ret;
		}
		arg->ret_origin = cmd.return_origin;
		pr_debug("invoke command: RO = 0x%x ret = 0x%x\n",
			 arg->ret_origin, arg->ret);
	}

	return ret;
}

int handle_map_shmem(u32 count, struct shmem_desc *start, u32 *buf_id)
{
	struct tee_cmd_map_shared_mem *cmd;
	phys_addr_t paddr;
	int ret, i;
	u32 status;

	if (!count || !start || !buf_id)
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	/* Size must be page aligned */
	for (i = 0; i < count ; i++) {
		if (!start[i].kaddr || (start[i].size & (PAGE_SIZE - 1))) {
			ret = -EINVAL;
			goto free_cmd;
		}

		if ((u64)start[i].kaddr & (PAGE_SIZE - 1)) {
			pr_err("map shared memory: page unaligned. addr 0x%llx",
			       (u64)start[i].kaddr);
			ret = -EINVAL;
			goto free_cmd;
		}
	}

	cmd->sg_list.count = count;

	/* Create buffer list */
	for (i = 0; i < count ; i++) {
		paddr = __psp_pa(start[i].kaddr);
		cmd->sg_list.buf[i].hi_addr = upper_32_bits(paddr);
		cmd->sg_list.buf[i].low_addr = lower_32_bits(paddr);
		cmd->sg_list.buf[i].size = start[i].size;
		cmd->sg_list.size += cmd->sg_list.buf[i].size;

		pr_debug("buf[%d]:hi addr = 0x%x\n", i,
			 cmd->sg_list.buf[i].hi_addr);
		pr_debug("buf[%d]:low addr = 0x%x\n", i,
			 cmd->sg_list.buf[i].low_addr);
		pr_debug("buf[%d]:size = 0x%x\n", i, cmd->sg_list.buf[i].size);
		pr_debug("list size = 0x%x\n", cmd->sg_list.size);
	}

	*buf_id = 0;

	ret = psp_tee_process_cmd(TEE_CMD_ID_MAP_SHARED_MEM, (void *)cmd,
				  sizeof(*cmd), &status);
	if (!ret && !status) {
		*buf_id = cmd->buf_id;
		pr_debug("mapped buffer ID = 0x%x\n", *buf_id);
	} else {
		pr_err("map shared memory: status = 0x%x\n", status);
		ret = -ENOMEM;
	}

free_cmd:
	kfree(cmd);

	return ret;
}

int handle_open_session(struct tee_ioctl_open_session_arg *arg, u32 *info,
			struct tee_param *p)
{
	struct tee_cmd_open_session cmd = {0};
	int ret;

	if (!arg || !info || (!p && arg->num_params))
		return -EINVAL;

	arg->ret_origin = TEEC_ORIGIN_COMMS;

	if (arg->session == 0) {
		arg->ret = TEEC_ERROR_GENERIC;
		return -EINVAL;
	}

	ret = tee_params_to_amd_params(p, arg->num_params, &cmd.op);
	if (ret) {
		pr_err("invalid Params. Abort open session\n");
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return ret;
	}

	cmd.ta_handle = get_ta_handle(arg->session);
	*info = 0;

	ret = psp_tee_process_cmd(TEE_CMD_ID_OPEN_SESSION, (void *)&cmd,
				  sizeof(cmd), &arg->ret);
	if (ret) {
		arg->ret = TEEC_ERROR_COMMUNICATION;
	} else {
		ret = amd_params_to_tee_params(p, arg->num_params, &cmd.op);
		if (unlikely(ret)) {
			pr_err("open session: failed to copy output\n");
			arg->ret = TEEC_ERROR_GENERIC;
			return ret;
		}
		arg->ret_origin = cmd.return_origin;
		*info = cmd.session_info;
		pr_debug("open session: session info = 0x%x\n", *info);
	}

	pr_debug("open session: ret = 0x%x RO = 0x%x\n", arg->ret,
		 arg->ret_origin);

	return ret;
}

int handle_load_ta(void *data, u32 size, struct tee_ioctl_open_session_arg *arg)
{
	struct tee_cmd_unload_ta unload_cmd = {};
	struct tee_cmd_load_ta load_cmd = {};
	phys_addr_t blob;
	int ret;

	if (size == 0 || !data || !arg)
		return -EINVAL;

	blob = __psp_pa(data);
	if (blob & (PAGE_SIZE - 1)) {
		pr_err("load TA: page unaligned. blob 0x%llx", blob);
		return -EINVAL;
	}

	load_cmd.hi_addr = upper_32_bits(blob);
	load_cmd.low_addr = lower_32_bits(blob);
	load_cmd.size = size;

	mutex_lock(&ta_refcount_mutex);

	ret = psp_tee_process_cmd(TEE_CMD_ID_LOAD_TA, (void *)&load_cmd,
				  sizeof(load_cmd), &arg->ret);
	if (ret) {
		arg->ret_origin = TEEC_ORIGIN_COMMS;
		arg->ret = TEEC_ERROR_COMMUNICATION;
	} else {
		arg->ret_origin = load_cmd.return_origin;

		if (arg->ret == TEEC_SUCCESS) {
			ret = get_ta_refcount(load_cmd.ta_handle);
			if (!ret) {
				arg->ret_origin = TEEC_ORIGIN_COMMS;
				arg->ret = TEEC_ERROR_OUT_OF_MEMORY;

				/* Unload the TA on error */
				unload_cmd.ta_handle = load_cmd.ta_handle;
				psp_tee_process_cmd(TEE_CMD_ID_UNLOAD_TA,
						    (void *)&unload_cmd,
						    sizeof(unload_cmd), &ret);
			} else {
				set_session_id(load_cmd.ta_handle, 0, &arg->session);
			}
		}
	}
	mutex_unlock(&ta_refcount_mutex);

	pr_debug("load TA: TA handle = 0x%x, RO = 0x%x, ret = 0x%x\n",
		 load_cmd.ta_handle, arg->ret_origin, arg->ret);

	return 0;
}
