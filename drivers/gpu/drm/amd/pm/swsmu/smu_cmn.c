/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#define SWSMU_CODE_LAYER_L4

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_cmn.h"
#include "soc15_common.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define MP1_C2PMSG_90__CONTENT_MASK                                                                    0xFFFFFFFFL

const int link_speed[] = {25, 50, 80, 160, 320, 640};

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(type)	#type
static const char * const __smu_message_names[] = {
	SMU_MESSAGE_TYPES
};

#define smu_cmn_call_asic_func(intf, smu, args...)                             \
	((smu)->ppt_funcs ? ((smu)->ppt_funcs->intf ?                          \
				     (smu)->ppt_funcs->intf(smu, ##args) :     \
				     -ENOTSUPP) :                              \
			    -EINVAL)

#define SMU_MSG_V1_DEFAULT_RATELIMIT_INTERVAL (5 * HZ)
#define SMU_MSG_V1_DEFAULT_RATELIMIT_BURST 10

static const char *smu_get_message_name(struct smu_context *smu,
					enum smu_message_type type)
{
	if (type >= SMU_MSG_MAX_COUNT)
		return "unknown smu message";

	return __smu_message_names[type];
}

/* Redefine the SMU error codes here.
 *
 * Note that these definitions are redundant and should be removed
 * when the SMU has exported a unified header file containing these
 * macros, which header file we can just include and use the SMU's
 * macros. At the moment, these error codes are defined by the SMU
 * per-ASIC unfortunately, yet we're a one driver for all ASICs.
 */
#define SMU_RESP_NONE           0
#define SMU_RESP_OK             1
#define SMU_RESP_CMD_FAIL       0xFF
#define SMU_RESP_CMD_UNKNOWN    0xFE
#define SMU_RESP_CMD_BAD_PREREQ 0xFD
#define SMU_RESP_BUSY_OTHER     0xFC
#define SMU_RESP_DEBUG_END      0xFB

#define SMU_RESP_UNEXP (~0U)

static int smu_msg_v1_send_debug_msg(struct smu_msg_ctl *ctl, u32 msg, u32 param)
{
	struct amdgpu_device *adev = ctl->smu->adev;
	struct smu_msg_config *cfg = &ctl->config;

	if (!(ctl->flags & SMU_MSG_CTL_DEBUG_MAILBOX))
		return -EOPNOTSUPP;

	mutex_lock(&ctl->lock);

	WREG32(cfg->debug_param_reg, param);
	WREG32(cfg->debug_msg_reg, msg);
	WREG32(cfg->debug_resp_reg, 0);

	mutex_unlock(&ctl->lock);

	return 0;
}

static int __smu_cmn_send_debug_msg(struct smu_msg_ctl *ctl,
				    u32 msg,
				    u32 param)
{
	if (!ctl->ops || !ctl->ops->send_debug_msg)
		return -EOPNOTSUPP;

	return ctl->ops->send_debug_msg(ctl, msg, param);
}

/**
 * smu_cmn_wait_for_response -- wait for response from the SMU
 * @smu: pointer to an SMU context
 *
 * Wait for status from the SMU.
 *
 * Return 0 on success, -errno on error, indicating the execution
 * status and result of the message being waited for. See
 * smu_msg_v1_decode_response() for details of the -errno.
 */
int smu_cmn_wait_for_response(struct smu_context *smu)
{
	return smu_msg_wait_response(&smu->msg_ctl, 0);
}

/**
 * smu_cmn_send_smc_msg_with_param -- send a message with parameter
 * @smu: pointer to an SMU context
 * @msg: message to send
 * @param: parameter to send to the SMU
 * @read_arg: pointer to u32 to return a value from the SMU back
 *            to the caller
 *
 * Send the message @msg with parameter @param to the SMU, wait for
 * completion of the command, and return back a value from the SMU in
 * @read_arg pointer.
 *
 * Return 0 on success, -errno when a problem is encountered sending
 * message or receiving reply. If there is a PCI bus recovery or
 * the destination is a virtual GPU which does not allow this message
 * type, the message is simply dropped and success is also returned.
 * See smu_msg_v1_decode_response() for details of the -errno.
 *
 * If we weren't able to send the message to the SMU, we also print
 * the error to the standard log.
 *
 * Command completion status is printed only if the -errno is
 * -EREMOTEIO, indicating that the SMU returned back an
 * undefined/unknown/unspecified result. All other cases are
 * well-defined, not printed, but instead given back to the client to
 * decide what further to do.
 *
 * The return value, @read_arg is read back regardless, to give back
 * more information to the client, which on error would most likely be
 * @param, but we can't assume that. This also eliminates more
 * conditionals.
 */
int smu_cmn_send_smc_msg_with_param(struct smu_context *smu,
				    enum smu_message_type msg,
				    uint32_t param,
				    uint32_t *read_arg)
{
	struct smu_msg_ctl *ctl = &smu->msg_ctl;
	struct smu_msg_args args = {
		.msg = msg,
		.args[0] = param,
		.num_args = 1,
		.num_out_args = read_arg ? 1 : 0,
		.flags = 0,
		.timeout = 0,
	};
	int ret;

	ret = ctl->ops->send_msg(ctl, &args);

	if (read_arg)
		*read_arg = args.out_args[0];

	return ret;
}

int smu_cmn_send_smc_msg(struct smu_context *smu,
			 enum smu_message_type msg,
			 uint32_t *read_arg)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					       msg,
					       0,
					       read_arg);
}

int smu_cmn_send_debug_smc_msg(struct smu_context *smu,
			 uint32_t msg)
{
	return __smu_cmn_send_debug_msg(&smu->msg_ctl, msg, 0);
}

int smu_cmn_send_debug_smc_msg_with_param(struct smu_context *smu,
			 uint32_t msg, uint32_t param)
{
	return __smu_cmn_send_debug_msg(&smu->msg_ctl, msg, param);
}

static int smu_msg_v1_decode_response(u32 resp)
{
	int res;

	switch (resp) {
	case SMU_RESP_NONE:
		/* The SMU is busy--still executing your command.
		 */
		res = -ETIME;
		break;
	case SMU_RESP_OK:
		res = 0;
		break;
	case SMU_RESP_CMD_FAIL:
		/* Command completed successfully, but the command
		 * status was failure.
		 */
		res = -EIO;
		break;
	case SMU_RESP_CMD_UNKNOWN:
		/* Unknown command--ignored by the SMU.
		 */
		res = -EOPNOTSUPP;
		break;
	case SMU_RESP_CMD_BAD_PREREQ:
		/* Valid command--bad prerequisites.
		 */
		res = -EINVAL;
		break;
	case SMU_RESP_BUSY_OTHER:
		/* The SMU is busy with other commands. The client
		 * should retry in 10 us.
		 */
		res = -EBUSY;
		break;
	default:
		/* Unknown or debug response from the SMU.
		 */
		res = -EREMOTEIO;
		break;
	}

	return res;
}

static u32 __smu_msg_v1_poll_stat(struct smu_msg_ctl *ctl, u32 timeout_us)
{
	struct amdgpu_device *adev = ctl->smu->adev;
	struct smu_msg_config *cfg = &ctl->config;
	u32 timeout = timeout_us ? timeout_us : ctl->default_timeout;
	u32 reg;

	for (; timeout > 0; timeout--) {
		reg = RREG32(cfg->resp_reg);
		if ((reg & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			break;
		udelay(1);
	}

	return reg;
}

static void __smu_msg_v1_send(struct smu_msg_ctl *ctl, u16 index,
			      struct smu_msg_args *args)
{
	struct amdgpu_device *adev = ctl->smu->adev;
	struct smu_msg_config *cfg = &ctl->config;
	int i;

	WREG32(cfg->resp_reg, 0);
	for (i = 0; i < args->num_args; i++)
		WREG32(cfg->arg_regs[i], args->args[i]);
	WREG32(cfg->msg_reg, index);
}

static void __smu_msg_v1_read_out_args(struct smu_msg_ctl *ctl,
				       struct smu_msg_args *args)
{
	struct amdgpu_device *adev = ctl->smu->adev;
	int i;

	for (i = 0; i < args->num_out_args; i++)
		args->out_args[i] = RREG32(ctl->config.arg_regs[i]);
}

static void __smu_msg_v1_print_err_limited(struct smu_msg_ctl *ctl,
					   struct smu_msg_args *args,
					   char *err_msg)
{
	static DEFINE_RATELIMIT_STATE(_rs,
				      SMU_MSG_V1_DEFAULT_RATELIMIT_INTERVAL,
				      SMU_MSG_V1_DEFAULT_RATELIMIT_BURST);
	struct smu_context *smu = ctl->smu;
	struct amdgpu_device *adev = smu->adev;

	if (__ratelimit(&_rs)) {
		u32 in[SMU_MSG_MAX_ARGS];
		int i;

		dev_err(adev->dev, "%s msg_reg: %x resp_reg: %x", err_msg,
			RREG32(ctl->config.msg_reg),
			RREG32(ctl->config.resp_reg));
		if (args->num_args > 0) {
			for (i = 0; i < args->num_args; i++)
				in[i] = RREG32(ctl->config.arg_regs[i]);
			print_hex_dump(KERN_ERR, "in params:", DUMP_PREFIX_NONE,
				       16, 4, in, args->num_args * sizeof(u32),
				       false);
		}
	}
}

static void __smu_msg_v1_print_error(struct smu_msg_ctl *ctl,
				     u32 resp,
				     struct smu_msg_args *args)
{
	struct smu_context *smu = ctl->smu;
	struct amdgpu_device *adev = smu->adev;
	int index = ctl->message_map[args->msg].map_to;

	switch (resp) {
	case SMU_RESP_NONE:
		__smu_msg_v1_print_err_limited(ctl, args, "SMU: No response");
		break;
	case SMU_RESP_OK:
		break;
	case SMU_RESP_CMD_FAIL:
		break;
	case SMU_RESP_CMD_UNKNOWN:
		__smu_msg_v1_print_err_limited(ctl, args,
					       "SMU: unknown command");
		break;
	case SMU_RESP_CMD_BAD_PREREQ:
		__smu_msg_v1_print_err_limited(
			ctl, args, "SMU: valid command, bad prerequisites");
		break;
	case SMU_RESP_BUSY_OTHER:
		if (args->msg != SMU_MSG_GetBadPageCount)
			__smu_msg_v1_print_err_limited(ctl, args,
						       "SMU: I'm very busy");
		break;
	case SMU_RESP_DEBUG_END:
		__smu_msg_v1_print_err_limited(ctl, args, "SMU: Debug Err");
		break;
	case SMU_RESP_UNEXP:
		if (amdgpu_device_bus_status_check(adev)) {
			dev_err(adev->dev,
				"SMU: bus error for message: %s(%d) response:0x%08X ",
				smu_get_message_name(smu, args->msg), index,
				resp);
			if (args->num_args > 0)
				print_hex_dump(KERN_ERR,
					       "in params:", DUMP_PREFIX_NONE,
					       16, 4, args->args,
					       args->num_args * sizeof(u32),
					       false);
		}
		break;
	default:
		__smu_msg_v1_print_err_limited(ctl, args,
					       "SMU: unknown response");
		break;
	}
}

static int __smu_msg_v1_ras_filter(struct smu_msg_ctl *ctl,
				   enum smu_message_type msg, u32 msg_flags,
				   bool *skip_pre_poll)
{
	struct smu_context *smu = ctl->smu;
	struct amdgpu_device *adev = smu->adev;
	bool fed_status;
	u32 reg;

	if (!(smu->smc_fw_caps & SMU_FW_CAP_RAS_PRI))
		return 0;

	fed_status = amdgpu_ras_get_fed_status(adev);

	/* Block non-RAS-priority messages during RAS error */
	if (fed_status && !(msg_flags & SMU_MSG_RAS_PRI)) {
		dev_dbg(adev->dev, "RAS error detected, skip sending %s",
			smu_get_message_name(smu, msg));
		return -EACCES;
	}

	/* Skip pre-poll for priority messages or during RAS error */
	if ((msg_flags & SMU_MSG_NO_PRECHECK) || fed_status) {
		reg = RREG32(ctl->config.resp_reg);
		dev_dbg(adev->dev,
			"Sending priority message %s response status: %x",
			smu_get_message_name(smu, msg), reg);
		if (reg == 0)
			*skip_pre_poll = true;
	}

	return 0;
}

/**
 * smu_msg_proto_v1_send_msg - Complete V1 protocol with all filtering
 * @ctl: Message control block
 * @args: Message arguments
 *
 * Return: 0 on success, negative errno on failure
 */
static int smu_msg_v1_send_msg(struct smu_msg_ctl *ctl,
			       struct smu_msg_args *args)
{
	struct smu_context *smu = ctl->smu;
	struct amdgpu_device *adev = smu->adev;
	const struct cmn2asic_msg_mapping *mapping;
	u32 reg, msg_flags;
	int ret, index;
	bool skip_pre_poll = false;
	bool lock_held = args->flags & SMU_MSG_FLAG_LOCK_HELD;

	/* Early exit if no HW access */
	if (adev->no_hw_access)
		return 0;

	/* Message index translation */
	if (args->msg >= SMU_MSG_MAX_COUNT || !ctl->message_map)
		return -EINVAL;

	if (args->num_args > ctl->config.num_arg_regs ||
	    args->num_out_args > ctl->config.num_arg_regs)
		return -EINVAL;

	mapping = &ctl->message_map[args->msg];
	if (!mapping->valid_mapping)
		return -EINVAL;

	msg_flags = mapping->flags;
	index = mapping->map_to;

	/* VF filter - skip messages not valid for VF */
	if (amdgpu_sriov_vf(adev) && !(msg_flags & SMU_MSG_VF_FLAG))
		return 0;

	if (!lock_held)
		mutex_lock(&ctl->lock);

	/* RAS priority filter */
	ret = __smu_msg_v1_ras_filter(ctl, args->msg, msg_flags,
				      &skip_pre_poll);
	if (ret)
		goto out;

	/* FW state checks */
	if (smu->smc_fw_state == SMU_FW_HANG) {
		dev_err(adev->dev,
			"SMU is in hanged state, failed to send smu message!\n");
		ret = -EREMOTEIO;
		goto out;
	} else if (smu->smc_fw_state == SMU_FW_INIT) {
		skip_pre_poll = true;
		smu->smc_fw_state = SMU_FW_RUNTIME;
	}

	/* Pre-poll: ensure previous message completed */
	if (!skip_pre_poll) {
		reg = __smu_msg_v1_poll_stat(ctl, args->timeout);
		ret = smu_msg_v1_decode_response(reg);
		if (reg == SMU_RESP_NONE || ret == -EREMOTEIO) {
			__smu_msg_v1_print_error(ctl, reg, args);
			goto out;
		}
	}

	/* Send message */
	__smu_msg_v1_send(ctl, (u16)index, args);

	/* Post-poll (skip if ASYNC) */
	if (args->flags & SMU_MSG_FLAG_ASYNC) {
		ret = 0;
		goto out;
	}

	reg = __smu_msg_v1_poll_stat(ctl, args->timeout);
	ret = smu_msg_v1_decode_response(reg);

	/* FW state update on fatal error */
	if (ret == -EREMOTEIO) {
		smu->smc_fw_state = SMU_FW_HANG;
		__smu_msg_v1_print_error(ctl, reg, args);
	} else if (ret != 0) {
		__smu_msg_v1_print_error(ctl, reg, args);
	}

	/* Read output args */
	if (ret == 0 && args->num_out_args > 0) {
		__smu_msg_v1_read_out_args(ctl, args);
		dev_dbg(adev->dev, "smu send message: %s(%d) resp : 0x%08x",
			smu_get_message_name(smu, args->msg), index, reg);
		if (args->num_args > 0)
			print_hex_dump_debug("in params:", DUMP_PREFIX_NONE, 16,
					     4, args->args,
					     args->num_args * sizeof(u32),
					     false);
		print_hex_dump_debug("out params:", DUMP_PREFIX_NONE, 16, 4,
				     args->out_args,
				     args->num_out_args * sizeof(u32), false);
	} else {
		dev_dbg(adev->dev, "smu send message: %s(%d), resp: 0x%08x\n",
			smu_get_message_name(smu, args->msg), index, reg);
		if (args->num_args > 0)
			print_hex_dump_debug("in params:", DUMP_PREFIX_NONE, 16,
					     4, args->args,
					     args->num_args * sizeof(u32),
					     false);
	}

out:
	/* Debug halt on error */
	if (unlikely(adev->pm.smu_debug_mask & SMU_DEBUG_HALT_ON_ERROR) &&
	    ret) {
		amdgpu_device_halt(adev);
		WARN_ON(1);
	}

	if (!lock_held)
		mutex_unlock(&ctl->lock);
	return ret;
}

static int smu_msg_v1_wait_response(struct smu_msg_ctl *ctl, u32 timeout_us)
{
	struct smu_context *smu = ctl->smu;
	struct amdgpu_device *adev = smu->adev;
	u32 reg;
	int ret;

	reg = __smu_msg_v1_poll_stat(ctl, timeout_us);
	ret = smu_msg_v1_decode_response(reg);

	if (ret == -EREMOTEIO)
		smu->smc_fw_state = SMU_FW_HANG;

	if (unlikely(adev->pm.smu_debug_mask & SMU_DEBUG_HALT_ON_ERROR) &&
	    ret && (ret != -ETIME)) {
		amdgpu_device_halt(adev);
		WARN_ON(1);
	}

	return ret;
}

const struct smu_msg_ops smu_msg_v1_ops = {
	.send_msg = smu_msg_v1_send_msg,
	.wait_response = smu_msg_v1_wait_response,
	.decode_response = smu_msg_v1_decode_response,
	.send_debug_msg = smu_msg_v1_send_debug_msg,
};

int smu_msg_wait_response(struct smu_msg_ctl *ctl, u32 timeout_us)
{
	return ctl->ops->wait_response(ctl, timeout_us);
}

/**
 * smu_msg_send_async_locked - Send message asynchronously, caller holds lock
 * @ctl: Message control block
 * @msg: Message type
 * @param: Message parameter
 *
 * Send an SMU message without waiting for response. Caller must hold ctl->lock
 * and call smu_msg_wait_response() later to get the result.
 *
 * Return: 0 on success, negative errno on failure
 */
int smu_msg_send_async_locked(struct smu_msg_ctl *ctl,
			      enum smu_message_type msg, u32 param)
{
	struct smu_msg_args args = {
		.msg = msg,
		.args[0] = param,
		.num_args = 1,
		.num_out_args = 0,
		.flags = SMU_MSG_FLAG_ASYNC | SMU_MSG_FLAG_LOCK_HELD,
		.timeout = 0,
	};

	return ctl->ops->send_msg(ctl, &args);
}

int smu_cmn_to_asic_specific_index(struct smu_context *smu,
				   enum smu_cmn2asic_mapping_type type,
				   uint32_t index)
{
	struct cmn2asic_msg_mapping msg_mapping;
	struct cmn2asic_mapping mapping;

	switch (type) {
	case CMN2ASIC_MAPPING_MSG:
		if (index >= SMU_MSG_MAX_COUNT ||
		    !smu->msg_ctl.message_map)
			return -EINVAL;

		msg_mapping = smu->msg_ctl.message_map[index];
		if (!msg_mapping.valid_mapping)
			return -EINVAL;

		if (amdgpu_sriov_vf(smu->adev) &&
		    !(msg_mapping.flags & SMU_MSG_VF_FLAG))
			return -EACCES;

		return msg_mapping.map_to;

	case CMN2ASIC_MAPPING_CLK:
		if (index >= SMU_CLK_COUNT ||
		    !smu->clock_map)
			return -EINVAL;

		mapping = smu->clock_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_FEATURE:
		if (index >= SMU_FEATURE_COUNT ||
		    !smu->feature_map)
			return -EINVAL;

		mapping = smu->feature_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_TABLE:
		if (index >= SMU_TABLE_COUNT ||
		    !smu->table_map)
			return -EINVAL;

		mapping = smu->table_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_PWR:
		if (index >= SMU_POWER_SOURCE_COUNT ||
		    !smu->pwr_src_map)
			return -EINVAL;

		mapping = smu->pwr_src_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_WORKLOAD:
		if (index >= PP_SMC_POWER_PROFILE_COUNT ||
		    !smu->workload_map)
			return -EINVAL;

		mapping = smu->workload_map[index];
		if (!mapping.valid_mapping)
			return -ENOTSUPP;

		return mapping.map_to;

	default:
		return -EINVAL;
	}
}

int smu_cmn_feature_is_supported(struct smu_context *smu,
				 enum smu_feature_mask mask)
{
	int feature_id;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return 0;

	return smu_feature_list_is_set(smu, SMU_FEATURE_LIST_SUPPORTED,
				       feature_id);
}

static int __smu_get_enabled_features(struct smu_context *smu,
			       uint64_t *enabled_features)
{
	return smu_cmn_call_asic_func(get_enabled_mask, smu, enabled_features);
}

int smu_cmn_feature_is_enabled(struct smu_context *smu,
			       enum smu_feature_mask mask)
{
	struct amdgpu_device *adev = smu->adev;
	uint64_t enabled_features;
	int feature_id;

	if (__smu_get_enabled_features(smu, &enabled_features)) {
		dev_err(adev->dev, "Failed to retrieve enabled ppfeatures!\n");
		return 0;
	}

	/*
	 * For Renoir and Cyan Skillfish, they are assumed to have all features
	 * enabled. Also considering they have no feature_map available, the
	 * check here can avoid unwanted feature_map check below.
	 */
	if (enabled_features == ULLONG_MAX)
		return 1;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return 0;

	return test_bit(feature_id, (unsigned long *)&enabled_features);
}

bool smu_cmn_clk_dpm_is_enabled(struct smu_context *smu,
				enum smu_clk_type clk_type)
{
	enum smu_feature_mask feature_id = 0;

	switch (clk_type) {
	case SMU_MCLK:
	case SMU_UCLK:
		feature_id = SMU_FEATURE_DPM_UCLK_BIT;
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		feature_id = SMU_FEATURE_DPM_GFXCLK_BIT;
		break;
	case SMU_SOCCLK:
		feature_id = SMU_FEATURE_DPM_SOCCLK_BIT;
		break;
	case SMU_VCLK:
	case SMU_VCLK1:
		feature_id = SMU_FEATURE_DPM_VCLK_BIT;
		break;
	case SMU_DCLK:
	case SMU_DCLK1:
		feature_id = SMU_FEATURE_DPM_DCLK_BIT;
		break;
	case SMU_FCLK:
		feature_id = SMU_FEATURE_DPM_FCLK_BIT;
		break;
	default:
		return true;
	}

	if (!smu_cmn_feature_is_enabled(smu, feature_id))
		return false;

	return true;
}

int smu_cmn_get_enabled_mask(struct smu_context *smu,
			     uint64_t *feature_mask)
{
	uint32_t *feature_mask_high;
	uint32_t *feature_mask_low;
	int ret = 0, index = 0;

	if (!feature_mask)
		return -EINVAL;

	feature_mask_low = &((uint32_t *)feature_mask)[0];
	feature_mask_high = &((uint32_t *)feature_mask)[1];

	index = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_MSG,
						SMU_MSG_GetEnabledSmuFeatures);
	if (index > 0) {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						      SMU_MSG_GetEnabledSmuFeatures,
						      0,
						      feature_mask_low);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
						      SMU_MSG_GetEnabledSmuFeatures,
						      1,
						      feature_mask_high);
	} else {
		ret = smu_cmn_send_smc_msg(smu,
					   SMU_MSG_GetEnabledSmuFeaturesHigh,
					   feature_mask_high);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg(smu,
					   SMU_MSG_GetEnabledSmuFeaturesLow,
					   feature_mask_low);
	}

	return ret;
}

uint64_t smu_cmn_get_indep_throttler_status(
					const unsigned long dep_status,
					const uint8_t *throttler_map)
{
	uint64_t indep_status = 0;
	uint8_t dep_bit = 0;

	for_each_set_bit(dep_bit, &dep_status, 32)
		indep_status |= 1ULL << throttler_map[dep_bit];

	return indep_status;
}

int smu_cmn_feature_update_enable_state(struct smu_context *smu,
					uint64_t feature_mask,
					bool enabled)
{
	int ret = 0;

	if (enabled) {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_EnableSmuFeaturesLow,
						  lower_32_bits(feature_mask),
						  NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_EnableSmuFeaturesHigh,
						  upper_32_bits(feature_mask),
						  NULL);
	} else {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_DisableSmuFeaturesLow,
						  lower_32_bits(feature_mask),
						  NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_DisableSmuFeaturesHigh,
						  upper_32_bits(feature_mask),
						  NULL);
	}

	return ret;
}

int smu_cmn_feature_set_enabled(struct smu_context *smu,
				enum smu_feature_mask mask,
				bool enable)
{
	int feature_id;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return -EINVAL;

	return smu_cmn_feature_update_enable_state(smu,
					       1ULL << feature_id,
					       enable);
}

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(fea)	#fea
static const char *__smu_feature_names[] = {
	SMU_FEATURE_MASKS
};

static const char *smu_get_feature_name(struct smu_context *smu,
					enum smu_feature_mask feature)
{
	if (feature >= SMU_FEATURE_COUNT)
		return "unknown smu feature";
	return __smu_feature_names[feature];
}

size_t smu_cmn_get_pp_feature_mask(struct smu_context *smu,
				   char *buf)
{
	int8_t sort_feature[MAX(SMU_FEATURE_COUNT, SMU_FEATURE_MAX)];
	uint64_t feature_mask;
	int i, feature_index;
	uint32_t count = 0;
	size_t size = 0;

	if (__smu_get_enabled_features(smu, &feature_mask))
		return 0;

	size =  sysfs_emit_at(buf, size, "features high: 0x%08x low: 0x%08x\n",
			upper_32_bits(feature_mask), lower_32_bits(feature_mask));

	memset(sort_feature, -1, sizeof(sort_feature));

	for (i = 0; i < SMU_FEATURE_COUNT; i++) {
		feature_index = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_FEATURE,
							       i);
		if (feature_index < 0)
			continue;

		sort_feature[feature_index] = i;
	}

	size += sysfs_emit_at(buf, size, "%-2s. %-20s  %-3s : %-s\n",
			"No", "Feature", "Bit", "State");

	for (feature_index = 0; feature_index < SMU_FEATURE_MAX; feature_index++) {
		if (sort_feature[feature_index] < 0)
			continue;

		size += sysfs_emit_at(buf, size, "%02d. %-20s (%2d) : %s\n",
				count++,
				smu_get_feature_name(smu, sort_feature[feature_index]),
				feature_index,
				!!test_bit(feature_index, (unsigned long *)&feature_mask) ?
				"enabled" : "disabled");
	}

	return size;
}

int smu_cmn_set_pp_feature_mask(struct smu_context *smu,
				uint64_t new_mask)
{
	int ret = 0;
	uint64_t feature_mask;
	uint64_t feature_2_enabled = 0;
	uint64_t feature_2_disabled = 0;

	ret = __smu_get_enabled_features(smu, &feature_mask);
	if (ret)
		return ret;

	feature_2_enabled  = ~feature_mask & new_mask;
	feature_2_disabled = feature_mask & ~new_mask;

	if (feature_2_enabled) {
		ret = smu_cmn_feature_update_enable_state(smu,
							  feature_2_enabled,
							  true);
		if (ret)
			return ret;
	}
	if (feature_2_disabled) {
		ret = smu_cmn_feature_update_enable_state(smu,
							  feature_2_disabled,
							  false);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * smu_cmn_disable_all_features_with_exception - disable all dpm features
 *                                               except this specified by
 *                                               @mask
 *
 * @smu:               smu_context pointer
 * @mask:              the dpm feature which should not be disabled
 *                     SMU_FEATURE_COUNT: no exception, all dpm features
 *                     to disable
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int smu_cmn_disable_all_features_with_exception(struct smu_context *smu,
						enum smu_feature_mask mask)
{
	uint64_t features_to_disable = U64_MAX;
	int skipped_feature_id;

	if (mask != SMU_FEATURE_COUNT) {
		skipped_feature_id = smu_cmn_to_asic_specific_index(smu,
								    CMN2ASIC_MAPPING_FEATURE,
								    mask);
		if (skipped_feature_id < 0)
			return -EINVAL;

		features_to_disable &= ~(1ULL << skipped_feature_id);
	}

	return smu_cmn_feature_update_enable_state(smu,
						   features_to_disable,
						   0);
}

int smu_cmn_get_smc_version(struct smu_context *smu,
			    uint32_t *if_version,
			    uint32_t *smu_version)
{
	int ret = 0;

	if (!if_version && !smu_version)
		return -EINVAL;

	if (smu->smc_fw_if_version && smu->smc_fw_version)
	{
		if (if_version)
			*if_version = smu->smc_fw_if_version;

		if (smu_version)
			*smu_version = smu->smc_fw_version;

		return 0;
	}

	if (if_version) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetDriverIfVersion, if_version);
		if (ret)
			return ret;

		smu->smc_fw_if_version = *if_version;
	}

	if (smu_version) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetSmuVersion, smu_version);
		if (ret)
			return ret;

		smu->smc_fw_version = *smu_version;
	}

	return ret;
}

int smu_cmn_update_table(struct smu_context *smu,
			 enum smu_table_id table_index,
			 int argument,
			 void *table_data,
			 bool drv2smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct amdgpu_device *adev = smu->adev;
	struct smu_table *table = &smu_table->driver_table;
	int table_id = smu_cmn_to_asic_specific_index(smu,
						      CMN2ASIC_MAPPING_TABLE,
						      table_index);
	uint32_t table_size;
	int ret = 0;
	if (!table_data || table_index >= SMU_TABLE_COUNT || table_id < 0)
		return -EINVAL;

	table_size = smu_table->tables[table_index].size;

	if (drv2smu) {
		memcpy(table->cpu_addr, table_data, table_size);
		/*
		 * Flush hdp cache: to guard the content seen by
		 * GPU is consitent with CPU.
		 */
		amdgpu_hdp_flush(adev, NULL);
	}

	ret = smu_cmn_send_smc_msg_with_param(smu, drv2smu ?
					  SMU_MSG_TransferTableDram2Smu :
					  SMU_MSG_TransferTableSmu2Dram,
					  table_id | ((argument & 0xFFFF) << 16),
					  NULL);
	if (ret)
		return ret;

	if (!drv2smu) {
		amdgpu_hdp_invalidate(adev, NULL);
		memcpy(table_data, table->cpu_addr, table_size);
	}

	return 0;
}

int smu_cmn_write_watermarks_table(struct smu_context *smu)
{
	void *watermarks_table = smu->smu_table.watermarks_table;

	if (!watermarks_table)
		return -EINVAL;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_WATERMARKS,
				    0,
				    watermarks_table,
				    true);
}

int smu_cmn_write_pptable(struct smu_context *smu)
{
	void *pptable = smu->smu_table.driver_pptable;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_PPTABLE,
				    0,
				    pptable,
				    true);
}

int smu_cmn_get_metrics_table(struct smu_context *smu,
			      void *metrics_table,
			      bool bypass_cache)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t table_size =
		smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	int ret = 0;

	if (bypass_cache ||
	    !smu_table->metrics_time ||
	    time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(1))) {
		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_SMU_METRICS,
				       0,
				       smu_table->metrics_table,
				       false);
		if (ret) {
			dev_info(smu->adev->dev, "Failed to export SMU metrics table!\n");
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	if (metrics_table)
		memcpy(metrics_table, smu_table->metrics_table, table_size);

	return 0;
}

int smu_cmn_get_combo_pptable(struct smu_context *smu)
{
	void *pptable = smu->smu_table.combo_pptable;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_COMBO_PPTABLE,
				    0,
				    pptable,
				    false);
}

int smu_cmn_set_mp1_state(struct smu_context *smu,
			  enum pp_mp1_state mp1_state)
{
	enum smu_message_type msg;
	int ret;

	switch (mp1_state) {
	case PP_MP1_STATE_SHUTDOWN:
		msg = SMU_MSG_PrepareMp1ForShutdown;
		break;
	case PP_MP1_STATE_UNLOAD:
		msg = SMU_MSG_PrepareMp1ForUnload;
		break;
	case PP_MP1_STATE_RESET:
		msg = SMU_MSG_PrepareMp1ForReset;
		break;
	case PP_MP1_STATE_NONE:
	default:
		return 0;
	}

	ret = smu_cmn_send_smc_msg(smu, msg, NULL);
	if (ret)
		dev_err(smu->adev->dev, "[PrepareMp1] Failed!\n");

	return ret;
}

bool smu_cmn_is_audio_func_enabled(struct amdgpu_device *adev)
{
	struct pci_dev *p = NULL;
	bool snd_driver_loaded;

	/*
	 * If the ASIC comes with no audio function, we always assume
	 * it is "enabled".
	 */
	p = pci_get_domain_bus_and_slot(pci_domain_nr(adev->pdev->bus),
			adev->pdev->bus->number, 1);
	if (!p)
		return true;

	snd_driver_loaded = pci_is_enabled(p) ? true : false;

	pci_dev_put(p);

	return snd_driver_loaded;
}

static char *smu_soc_policy_get_desc(struct smu_dpm_policy *policy, int level)
{
	if (level < 0 || !(policy->level_mask & BIT(level)))
		return "Invalid";

	switch (level) {
	case SOC_PSTATE_DEFAULT:
		return "soc_pstate_default";
	case SOC_PSTATE_0:
		return "soc_pstate_0";
	case SOC_PSTATE_1:
		return "soc_pstate_1";
	case SOC_PSTATE_2:
		return "soc_pstate_2";
	}

	return "Invalid";
}

static struct smu_dpm_policy_desc pstate_policy_desc = {
	.name = STR_SOC_PSTATE_POLICY,
	.get_desc = smu_soc_policy_get_desc,
};

void smu_cmn_generic_soc_policy_desc(struct smu_dpm_policy *policy)
{
	policy->desc = &pstate_policy_desc;
}

static char *smu_xgmi_plpd_policy_get_desc(struct smu_dpm_policy *policy,
					   int level)
{
	if (level < 0 || !(policy->level_mask & BIT(level)))
		return "Invalid";

	switch (level) {
	case XGMI_PLPD_DISALLOW:
		return "plpd_disallow";
	case XGMI_PLPD_DEFAULT:
		return "plpd_default";
	case XGMI_PLPD_OPTIMIZED:
		return "plpd_optimized";
	}

	return "Invalid";
}

static struct smu_dpm_policy_desc xgmi_plpd_policy_desc = {
	.name = STR_XGMI_PLPD_POLICY,
	.get_desc = smu_xgmi_plpd_policy_get_desc,
};

void smu_cmn_generic_plpd_policy_desc(struct smu_dpm_policy *policy)
{
	policy->desc = &xgmi_plpd_policy_desc;
}

void smu_cmn_get_backend_workload_mask(struct smu_context *smu,
				       u32 workload_mask,
				       u32 *backend_workload_mask)
{
	int workload_type;
	u32 profile_mode;

	*backend_workload_mask = 0;

	for (profile_mode = 0; profile_mode < PP_SMC_POWER_PROFILE_COUNT; profile_mode++) {
		if (!(workload_mask & (1 << profile_mode)))
			continue;

		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_WORKLOAD,
							       profile_mode);

		if (workload_type < 0)
			continue;

		*backend_workload_mask |= 1 << workload_type;
	}
}

static inline bool smu_cmn_freqs_match(uint32_t freq1, uint32_t freq2)
{
	/* Frequencies within 25 MHz are considered equal */
	return (abs((int)freq1 - (int)freq2) <= 25);
}

int smu_cmn_print_dpm_clk_levels(struct smu_context *smu,
				 struct smu_dpm_table *dpm_table,
				 uint32_t cur_clk, char *buf, int *offset)
{
	uint32_t min_clk, max_clk, level_index, count;
	uint32_t freq_values[3];
	int size, lvl, i;
	bool is_fine_grained;
	bool is_deep_sleep;
	bool freq_match;

	if (!dpm_table || !buf)
		return -EINVAL;

	level_index = 0;
	size = *offset;
	count = dpm_table->count;
	is_fine_grained = dpm_table->flags & SMU_DPM_TABLE_FINE_GRAINED;
	min_clk = SMU_DPM_TABLE_MIN(dpm_table);
	max_clk = SMU_DPM_TABLE_MAX(dpm_table);

	/* Deep sleep - current clock < min_clock/2, TBD: cur_clk = 0 as GFXOFF */
	is_deep_sleep = cur_clk < min_clk / 2;
	if (is_deep_sleep) {
		size += sysfs_emit_at(buf, size, "S: %uMhz *\n", cur_clk);
		level_index = 1;
	}

	if (!is_fine_grained) {
		for (i = 0; i < count; i++) {
			freq_match = !is_deep_sleep &&
				     smu_cmn_freqs_match(
					     cur_clk,
					     dpm_table->dpm_levels[i].value);
			size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n",
					      level_index + i,
					      dpm_table->dpm_levels[i].value,
					      freq_match ? "*" : "");
		}
	} else {
		count = 2;
		freq_values[0] = min_clk;
		freq_values[1] = max_clk;

		if (!is_deep_sleep) {
			if (smu_cmn_freqs_match(cur_clk, min_clk)) {
				lvl = 0;
			} else if (smu_cmn_freqs_match(cur_clk, max_clk)) {
				lvl = 1;
			} else {
				/* NOTE: use index '1' to show current clock value */
				lvl = 1;
				count = 3;
				freq_values[1] = cur_clk;
				freq_values[2] = max_clk;
			}
		}

		for (i = 0; i < count; i++) {
			size += sysfs_emit_at(
				buf, size, "%d: %uMhz %s\n", level_index + i,
				freq_values[i],
				(!is_deep_sleep && i == lvl) ? "*" : "");
		}
	}

	*offset = size;

	return 0;
}

int smu_cmn_print_pcie_levels(struct smu_context *smu,
			      struct smu_pcie_table *pcie_table,
			      uint32_t cur_gen, uint32_t cur_lane, char *buf,
			      int *offset)
{
	int size, i;

	if (!pcie_table || !buf)
		return -EINVAL;

	size = *offset;

	for (i = 0; i < pcie_table->lclk_levels; i++) {
		size += sysfs_emit_at(
			buf, size, "%d: %s %s %dMhz %s\n", i,
			(pcie_table->pcie_gen[i] == 0) ? "2.5GT/s," :
			(pcie_table->pcie_gen[i] == 1) ? "5.0GT/s," :
			(pcie_table->pcie_gen[i] == 2) ? "8.0GT/s," :
			(pcie_table->pcie_gen[i] == 3) ? "16.0GT/s," :
			(pcie_table->pcie_gen[i] == 4) ? "32.0GT/s," :
			(pcie_table->pcie_gen[i] == 5) ? "64.0GT/s," :
							 "",
			(pcie_table->pcie_lane[i] == 1) ? "x1" :
			(pcie_table->pcie_lane[i] == 2) ? "x2" :
			(pcie_table->pcie_lane[i] == 3) ? "x4" :
			(pcie_table->pcie_lane[i] == 4) ? "x8" :
			(pcie_table->pcie_lane[i] == 5) ? "x12" :
			(pcie_table->pcie_lane[i] == 6) ? "x16" :
			(pcie_table->pcie_lane[i] == 7) ? "x32" :
							  "",
			pcie_table->lclk_freq[i],
			(cur_gen == pcie_table->pcie_gen[i]) &&
					(cur_lane == pcie_table->pcie_lane[i]) ?
				"*" :
				"");
	}

	*offset = size;

	return 0;
}

int smu_cmn_dpm_pcie_gen_idx(int gen)
{
	int ret;

	switch (gen) {
	case 1 ... 5:
		ret = gen - 1;
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

int smu_cmn_dpm_pcie_width_idx(int width)
{
	int ret;

	switch (width) {
	case 1:
		ret = 1;
		break;
	case 2:
		ret = 2;
		break;
	case 4:
		ret = 3;
		break;
	case 8:
		ret = 4;
		break;
	case 12:
		ret = 5;
		break;
	case 16:
		ret = 6;
		break;
	case 32:
		ret = 7;
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}
