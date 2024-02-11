// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - wave5 backend logic
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include <linux/iopoll.h>
#include <linux/bitfield.h>
#include "wave5-vpu.h"
#include "wave5.h"
#include "wave5-regdefine.h"

#define FIO_TIMEOUT			10000000
#define FIO_CTRL_READY			BIT(31)
#define FIO_CTRL_WRITE			BIT(16)
#define VPU_BUSY_CHECK_TIMEOUT		10000000
#define QUEUE_REPORT_MASK		0xffff

/* Encoder support fields */
#define FEATURE_HEVC10BIT_ENC		BIT(3)
#define FEATURE_AVC10BIT_ENC		BIT(11)
#define FEATURE_AVC_ENCODER		BIT(1)
#define FEATURE_HEVC_ENCODER		BIT(0)

/* Decoder support fields */
#define FEATURE_AVC_DECODER		BIT(3)
#define FEATURE_HEVC_DECODER		BIT(2)

#define FEATURE_BACKBONE		BIT(16)
#define FEATURE_VCORE_BACKBONE		BIT(22)
#define FEATURE_VCPU_BACKBONE		BIT(28)

#define REMAP_CTRL_MAX_SIZE_BITS	((W5_REMAP_MAX_SIZE >> 12) & 0x1ff)
#define REMAP_CTRL_REGISTER_VALUE(index)	(			\
	(BIT(31) | (index << 12) | BIT(11) | REMAP_CTRL_MAX_SIZE_BITS)	\
)

#define FASTIO_ADDRESS_MASK		GENMASK(15, 0)
#define SEQ_PARAM_PROFILE_MASK		GENMASK(30, 24)

static void _wave5_print_reg_err(struct vpu_device *vpu_dev, u32 reg_fail_reason,
				 const char *func);
#define PRINT_REG_ERR(dev, reason)	_wave5_print_reg_err((dev), (reason), __func__)

static inline const char *cmd_to_str(int cmd, bool is_dec)
{
	switch (cmd) {
	case W5_INIT_VPU:
		return "W5_INIT_VPU";
	case W5_WAKEUP_VPU:
		return "W5_WAKEUP_VPU";
	case W5_SLEEP_VPU:
		return "W5_SLEEP_VPU";
	case W5_CREATE_INSTANCE:
		return "W5_CREATE_INSTANCE";
	case W5_FLUSH_INSTANCE:
		return "W5_FLUSH_INSTANCE";
	case W5_DESTROY_INSTANCE:
		return "W5_DESTROY_INSTANCE";
	case W5_INIT_SEQ:
		return "W5_INIT_SEQ";
	case W5_SET_FB:
		return "W5_SET_FB";
	case W5_DEC_ENC_PIC:
		if (is_dec)
			return "W5_DEC_PIC";
		return "W5_ENC_PIC";
	case W5_ENC_SET_PARAM:
		return "W5_ENC_SET_PARAM";
	case W5_QUERY:
		return "W5_QUERY";
	case W5_UPDATE_BS:
		return "W5_UPDATE_BS";
	case W5_MAX_VPU_COMD:
		return "W5_MAX_VPU_COMD";
	default:
		return "UNKNOWN";
	}
}

static void _wave5_print_reg_err(struct vpu_device *vpu_dev, u32 reg_fail_reason,
				 const char *func)
{
	struct device *dev = vpu_dev->dev;
	u32 reg_val;

	switch (reg_fail_reason) {
	case WAVE5_SYSERR_QUEUEING_FAIL:
		reg_val = vpu_read_reg(vpu_dev, W5_RET_QUEUE_FAIL_REASON);
		dev_dbg(dev, "%s: queueing failure: 0x%x\n", func, reg_val);
		break;
	case WAVE5_SYSERR_RESULT_NOT_READY:
		dev_err(dev, "%s: result not ready: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_ACCESS_VIOLATION_HW:
		dev_err(dev, "%s: access violation: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_WATCHDOG_TIMEOUT:
		dev_err(dev, "%s: watchdog timeout: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_BUS_ERROR:
		dev_err(dev, "%s: bus error: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_DOUBLE_FAULT:
		dev_err(dev, "%s: double fault: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_VPU_STILL_RUNNING:
		dev_err(dev, "%s: still running: 0x%x\n", func, reg_fail_reason);
		break;
	case WAVE5_SYSERR_VLC_BUF_FULL:
		dev_err(dev, "%s: vlc buf full: 0x%x\n", func, reg_fail_reason);
		break;
	default:
		dev_err(dev, "%s: failure:: 0x%x\n", func, reg_fail_reason);
		break;
	}
}

static int wave5_wait_fio_readl(struct vpu_device *vpu_dev, u32 addr, u32 val)
{
	u32 ctrl;
	int ret;

	ctrl = addr & 0xffff;
	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_CTRL_ADDR, ctrl);
	ret = read_poll_timeout(wave5_vdi_read_register, ctrl, ctrl & FIO_CTRL_READY,
				0, FIO_TIMEOUT, false, vpu_dev, W5_VPU_FIO_CTRL_ADDR);
	if (ret)
		return ret;

	if (wave5_vdi_read_register(vpu_dev, W5_VPU_FIO_DATA) != val)
		return -ETIMEDOUT;

	return 0;
}

static void wave5_fio_writel(struct vpu_device *vpu_dev, unsigned int addr, unsigned int data)
{
	int ret;
	unsigned int ctrl;

	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_DATA, data);
	ctrl = FIELD_GET(FASTIO_ADDRESS_MASK, addr);
	ctrl |= FIO_CTRL_WRITE;
	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_CTRL_ADDR, ctrl);
	ret = read_poll_timeout(wave5_vdi_read_register, ctrl, ctrl & FIO_CTRL_READY, 0,
				FIO_TIMEOUT, false, vpu_dev, W5_VPU_FIO_CTRL_ADDR);
	if (ret)
		dev_dbg_ratelimited(vpu_dev->dev, "FIO write timeout: addr=0x%x data=%x\n",
				    ctrl, data);
}

static int wave5_wait_bus_busy(struct vpu_device *vpu_dev, unsigned int addr)
{
	u32 gdi_status_check_value = 0x3f;

	if (vpu_dev->product_code == WAVE521C_CODE ||
	    vpu_dev->product_code == WAVE521_CODE ||
	    vpu_dev->product_code == WAVE521E1_CODE)
		gdi_status_check_value = 0x00ff1f3f;

	return wave5_wait_fio_readl(vpu_dev, addr, gdi_status_check_value);
}

static int wave5_wait_vpu_busy(struct vpu_device *vpu_dev, unsigned int addr)
{
	u32 data;

	return read_poll_timeout(wave5_vdi_read_register, data, data == 0,
				 0, VPU_BUSY_CHECK_TIMEOUT, false, vpu_dev, addr);
}

static int wave5_wait_vcpu_bus_busy(struct vpu_device *vpu_dev, unsigned int addr)
{
	return wave5_wait_fio_readl(vpu_dev, addr, 0);
}

bool wave5_vpu_is_init(struct vpu_device *vpu_dev)
{
	return vpu_read_reg(vpu_dev, W5_VCPU_CUR_PC) != 0;
}

unsigned int wave5_vpu_get_product_id(struct vpu_device *vpu_dev)
{
	u32 val = vpu_read_reg(vpu_dev, W5_PRODUCT_NUMBER);

	switch (val) {
	case WAVE521C_CODE:
		return PRODUCT_ID_521;
	case WAVE521_CODE:
	case WAVE521C_DUAL_CODE:
	case WAVE521E1_CODE:
	case WAVE511_CODE:
	case WAVE517_CODE:
	case WAVE537_CODE:
		dev_err(vpu_dev->dev, "Unsupported product id (%x)\n", val);
		break;
	default:
		dev_err(vpu_dev->dev, "Invalid product id (%x)\n", val);
		break;
	}

	return PRODUCT_ID_NONE;
}

static void wave5_bit_issue_command(struct vpu_device *vpu_dev, struct vpu_instance *inst, u32 cmd)
{
	u32 instance_index;
	u32 codec_mode;

	if (inst) {
		instance_index = inst->id;
		codec_mode = inst->std;

		vpu_write_reg(vpu_dev, W5_CMD_INSTANCE_INFO, (codec_mode << 16) |
			      (instance_index & 0xffff));
		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	}

	vpu_write_reg(vpu_dev, W5_COMMAND, cmd);

	if (inst) {
		dev_dbg(vpu_dev->dev, "%s: cmd=0x%x (%s)\n", __func__, cmd,
			cmd_to_str(cmd, inst->type == VPU_INST_TYPE_DEC));
	} else {
		dev_dbg(vpu_dev->dev, "%s: cmd=0x%x\n", __func__, cmd);
	}

	vpu_write_reg(vpu_dev, W5_VPU_HOST_INT_REQ, 1);
}

static int wave5_vpu_firmware_command_queue_error_check(struct vpu_device *dev, u32 *fail_res)
{
	u32 reason = 0;

	/* Check if we were able to add a command into the VCPU QUEUE */
	if (!vpu_read_reg(dev, W5_RET_SUCCESS)) {
		reason = vpu_read_reg(dev, W5_RET_FAIL_REASON);
		PRINT_REG_ERR(dev, reason);

		/*
		 * The fail_res argument will be either NULL or 0.
		 * If the fail_res argument is NULL, then just return -EIO.
		 * Otherwise, assign the reason to fail_res, so that the
		 * calling function can use it.
		 */
		if (fail_res)
			*fail_res = reason;
		else
			return -EIO;

		if (reason == WAVE5_SYSERR_VPU_STILL_RUNNING)
			return -EBUSY;
	}
	return 0;
}

static int send_firmware_command(struct vpu_instance *inst, u32 cmd, bool check_success,
				 u32 *queue_status, u32 *fail_result)
{
	int ret;

	wave5_bit_issue_command(inst->dev, inst, cmd);
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "%s: command: '%s', timed out\n", __func__,
			 cmd_to_str(cmd, inst->type == VPU_INST_TYPE_DEC));
		return -ETIMEDOUT;
	}

	if (queue_status)
		*queue_status = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	/* In some cases we want to send multiple commands before checking
	 * whether they are queued properly
	 */
	if (!check_success)
		return 0;

	return wave5_vpu_firmware_command_queue_error_check(inst->dev, fail_result);
}

static int wave5_send_query(struct vpu_device *vpu_dev, struct vpu_instance *inst,
			    enum query_opt query_opt)
{
	int ret;

	vpu_write_reg(vpu_dev, W5_QUERY_OPTION, query_opt);
	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	wave5_bit_issue_command(vpu_dev, inst, W5_QUERY);

	ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(vpu_dev->dev, "command: 'W5_QUERY', timed out opt=0x%x\n", query_opt);
		return ret;
	}

	return wave5_vpu_firmware_command_queue_error_check(vpu_dev, NULL);
}

static int setup_wave5_properties(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	struct vpu_attr *p_attr = &vpu_dev->attr;
	u32 reg_val;
	u8 *str;
	int ret;
	u32 hw_config_def0, hw_config_def1, hw_config_feature;

	ret = wave5_send_query(vpu_dev, NULL, GET_VPU_INFO);
	if (ret)
		return ret;

	reg_val = vpu_read_reg(vpu_dev, W5_RET_PRODUCT_NAME);
	str = (u8 *)&reg_val;
	p_attr->product_name[0] = str[3];
	p_attr->product_name[1] = str[2];
	p_attr->product_name[2] = str[1];
	p_attr->product_name[3] = str[0];
	p_attr->product_name[4] = 0;

	p_attr->product_id = wave5_vpu_get_product_id(vpu_dev);
	p_attr->product_version = vpu_read_reg(vpu_dev, W5_RET_PRODUCT_VERSION);
	p_attr->fw_version = vpu_read_reg(vpu_dev, W5_RET_FW_VERSION);
	p_attr->customer_id = vpu_read_reg(vpu_dev, W5_RET_CUSTOMER_ID);
	hw_config_def0 = vpu_read_reg(vpu_dev, W5_RET_STD_DEF0);
	hw_config_def1 = vpu_read_reg(vpu_dev, W5_RET_STD_DEF1);
	hw_config_feature = vpu_read_reg(vpu_dev, W5_RET_CONF_FEATURE);

	p_attr->support_hevc10bit_enc = FIELD_GET(FEATURE_HEVC10BIT_ENC, hw_config_feature);
	p_attr->support_avc10bit_enc = FIELD_GET(FEATURE_AVC10BIT_ENC, hw_config_feature);

	p_attr->support_decoders = FIELD_GET(FEATURE_AVC_DECODER, hw_config_def1) << STD_AVC;
	p_attr->support_decoders |= FIELD_GET(FEATURE_HEVC_DECODER, hw_config_def1) << STD_HEVC;
	p_attr->support_encoders = FIELD_GET(FEATURE_AVC_ENCODER, hw_config_def1) << STD_AVC;
	p_attr->support_encoders |= FIELD_GET(FEATURE_HEVC_ENCODER, hw_config_def1) << STD_HEVC;

	p_attr->support_backbone = FIELD_GET(FEATURE_BACKBONE, hw_config_def0);
	p_attr->support_vcpu_backbone = FIELD_GET(FEATURE_VCPU_BACKBONE, hw_config_def0);
	p_attr->support_vcore_backbone = FIELD_GET(FEATURE_VCORE_BACKBONE, hw_config_def0);

	return 0;
}

int wave5_vpu_get_version(struct vpu_device *vpu_dev, u32 *revision)
{
	u32 reg_val;
	int ret;

	ret = wave5_send_query(vpu_dev, NULL, GET_VPU_INFO);
	if (ret)
		return ret;

	reg_val = vpu_read_reg(vpu_dev, W5_RET_FW_VERSION);
	if (revision) {
		*revision = reg_val;
		return 0;
	}

	return -EINVAL;
}

static void remap_page(struct vpu_device *vpu_dev, dma_addr_t code_base, u32 index)
{
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_CTRL, REMAP_CTRL_REGISTER_VALUE(index));
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_VADDR, index * W5_REMAP_MAX_SIZE);
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_PADDR, code_base + index * W5_REMAP_MAX_SIZE);
}

int wave5_vpu_init(struct device *dev, u8 *fw, size_t size)
{
	struct vpu_buf *common_vb;
	dma_addr_t code_base, temp_base;
	u32 code_size, temp_size;
	u32 i, reg_val, reason_code;
	int ret;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	common_vb = &vpu_dev->common_mem;

	code_base = common_vb->daddr;
	/* ALIGN TO 4KB */
	code_size = (WAVE5_MAX_CODE_BUF_SIZE & ~0xfff);
	if (code_size < size * 2)
		return -EINVAL;

	temp_base = common_vb->daddr + WAVE5_TEMPBUF_OFFSET;
	temp_size = WAVE5_TEMPBUF_SIZE;

	ret = wave5_vdi_write_memory(vpu_dev, common_vb, 0, fw, size);
	if (ret < 0) {
		dev_err(vpu_dev->dev, "VPU init, Writing firmware to common buffer, fail: %d\n",
			ret);
		return ret;
	}

	vpu_write_reg(vpu_dev, W5_PO_CONF, 0);

	/* clear registers */

	for (i = W5_CMD_REG_BASE; i < W5_CMD_REG_END; i += 4)
		vpu_write_reg(vpu_dev, i, 0x00);

	remap_page(vpu_dev, code_base, W5_REMAP_INDEX0);
	remap_page(vpu_dev, code_base, W5_REMAP_INDEX1);

	vpu_write_reg(vpu_dev, W5_ADDR_CODE_BASE, code_base);
	vpu_write_reg(vpu_dev, W5_CODE_SIZE, code_size);
	vpu_write_reg(vpu_dev, W5_CODE_PARAM, (WAVE5_UPPER_PROC_AXI_ID << 4) | 0);
	vpu_write_reg(vpu_dev, W5_ADDR_TEMP_BASE, temp_base);
	vpu_write_reg(vpu_dev, W5_TEMP_SIZE, temp_size);

	/* These register must be reset explicitly */
	vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);
	wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, 0);
	wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, 0);
	vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, 0);

	/* Encoder interrupt */
	reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
	reg_val |= BIT(INT_WAVE5_ENC_PIC);
	reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
	/* Decoder interrupt */
	reg_val |= BIT(INT_WAVE5_INIT_SEQ);
	reg_val |= BIT(INT_WAVE5_DEC_PIC);
	reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
	vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

	reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
	if (FIELD_GET(FEATURE_BACKBONE, reg_val)) {
		reg_val = ((WAVE5_PROC_AXI_ID << 28) |
			   (WAVE5_PRP_AXI_ID << 24) |
			   (WAVE5_FBD_Y_AXI_ID << 20) |
			   (WAVE5_FBC_Y_AXI_ID << 16) |
			   (WAVE5_FBD_C_AXI_ID << 12) |
			   (WAVE5_FBC_C_AXI_ID << 8) |
			   (WAVE5_PRI_AXI_ID << 4) |
			   WAVE5_SEC_AXI_ID);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_PROG_AXI_ID, reg_val);
	}

	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	vpu_write_reg(vpu_dev, W5_COMMAND, W5_INIT_VPU);
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_CORE_START, 1);
	ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_err(vpu_dev->dev, "VPU init(W5_VPU_REMAP_CORE_START) timeout\n");
		return ret;
	}

	ret = wave5_vpu_firmware_command_queue_error_check(vpu_dev, &reason_code);
	if (ret)
		return ret;

	return setup_wave5_properties(dev);
}

int wave5_vpu_build_up_dec_param(struct vpu_instance *inst,
				 struct dec_open_param *param)
{
	int ret;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct vpu_device *vpu_dev = inst->dev;

	p_dec_info->cycle_per_tick = 256;
	if (vpu_dev->sram_buf.size) {
		p_dec_info->sec_axi_info.use_bit_enable = 1;
		p_dec_info->sec_axi_info.use_ip_enable = 1;
		p_dec_info->sec_axi_info.use_lf_row_enable = 1;
	}
	switch (inst->std) {
	case W_HEVC_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_HEVC;
		break;
	case W_AVC_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_AVC;
		break;
	default:
		return -EINVAL;
	}

	p_dec_info->vb_work.size = WAVE521DEC_WORKBUF_SIZE;
	ret = wave5_vdi_allocate_dma_memory(inst->dev, &p_dec_info->vb_work);
	if (ret)
		return ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_VCORE_INFO, 1);

	wave5_vdi_clear_memory(inst->dev, &p_dec_info->vb_work);

	vpu_write_reg(inst->dev, W5_ADDR_WORK_BASE, p_dec_info->vb_work.daddr);
	vpu_write_reg(inst->dev, W5_WORK_SIZE, p_dec_info->vb_work.size);

	vpu_write_reg(inst->dev, W5_CMD_ADDR_SEC_AXI, vpu_dev->sram_buf.daddr);
	vpu_write_reg(inst->dev, W5_CMD_SEC_AXI_SIZE, vpu_dev->sram_buf.size);

	vpu_write_reg(inst->dev, W5_CMD_DEC_BS_START_ADDR, p_dec_info->stream_buf_start_addr);
	vpu_write_reg(inst->dev, W5_CMD_DEC_BS_SIZE, p_dec_info->stream_buf_size);

	/* NOTE: SDMA reads MSB first */
	vpu_write_reg(inst->dev, W5_CMD_BS_PARAM, BITSTREAM_ENDIANNESS_BIG_ENDIAN);
	/* This register must be reset explicitly */
	vpu_write_reg(inst->dev, W5_CMD_EXT_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_NUM_CQ_DEPTH_M1, (COMMAND_QUEUE_DEPTH - 1));

	ret = send_firmware_command(inst, W5_CREATE_INSTANCE, true, NULL, NULL);
	if (ret) {
		wave5_vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_work);
		return ret;
	}

	p_dec_info->product_code = vpu_read_reg(inst->dev, W5_PRODUCT_NUMBER);

	return 0;
}

int wave5_vpu_hw_flush_instance(struct vpu_instance *inst)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	u32 instance_queue_count, report_queue_count;
	u32 reg_val = 0;
	u32 fail_res = 0;
	int ret;

	ret = send_firmware_command(inst, W5_FLUSH_INSTANCE, true, &reg_val, &fail_res);
	if (ret)
		return ret;

	instance_queue_count = (reg_val >> 16) & 0xff;
	report_queue_count = (reg_val & QUEUE_REPORT_MASK);
	if (instance_queue_count != 0 || report_queue_count != 0) {
		dev_warn(inst->dev->dev,
			 "FLUSH_INSTANCE cmd didn't reset the amount of queued commands & reports");
	}

	/* reset our local copy of the counts */
	p_dec_info->instance_queue_count = 0;
	p_dec_info->report_queue_count = 0;

	return 0;
}

static u32 get_bitstream_options(struct dec_info *info)
{
	u32 bs_option = BSOPTION_ENABLE_EXPLICIT_END;

	if (info->stream_endflag)
		bs_option |= BSOPTION_HIGHLIGHT_STREAM_END;
	return bs_option;
}

int wave5_vpu_dec_init_seq(struct vpu_instance *inst)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	u32 cmd_option = INIT_SEQ_NORMAL;
	u32 reg_val, fail_res;
	int ret;

	if (!inst->codec_info)
		return -EINVAL;

	vpu_write_reg(inst->dev, W5_BS_RD_PTR, p_dec_info->stream_rd_ptr);
	vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);

	vpu_write_reg(inst->dev, W5_BS_OPTION, get_bitstream_options(p_dec_info));

	vpu_write_reg(inst->dev, W5_COMMAND_OPTION, cmd_option);
	vpu_write_reg(inst->dev, W5_CMD_DEC_USER_MASK, p_dec_info->user_data_enable);

	ret = send_firmware_command(inst, W5_INIT_SEQ, true, &reg_val, &fail_res);
	if (ret)
		return ret;

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	dev_dbg(inst->dev->dev, "%s: init seq sent (queue %u : %u)\n", __func__,
		p_dec_info->instance_queue_count, p_dec_info->report_queue_count);

	return 0;
}

static void wave5_get_dec_seq_result(struct vpu_instance *inst, struct dec_initial_info *info)
{
	u32 reg_val;
	u32 profile_compatibility_flag;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;

	p_dec_info->stream_rd_ptr = wave5_dec_get_rd_ptr(inst);
	info->rd_ptr = p_dec_info->stream_rd_ptr;

	p_dec_info->frame_display_flag = vpu_read_reg(inst->dev, W5_RET_DEC_DISP_IDC);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_SIZE);
	info->pic_width = ((reg_val >> 16) & 0xffff);
	info->pic_height = (reg_val & 0xffff);
	info->min_frame_buffer_count = vpu_read_reg(inst->dev, W5_RET_DEC_NUM_REQUIRED_FB);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_CROP_LEFT_RIGHT);
	info->pic_crop_rect.left = (reg_val >> 16) & 0xffff;
	info->pic_crop_rect.right = reg_val & 0xffff;
	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_CROP_TOP_BOTTOM);
	info->pic_crop_rect.top = (reg_val >> 16) & 0xffff;
	info->pic_crop_rect.bottom = reg_val & 0xffff;

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_COLOR_SAMPLE_INFO);
	info->luma_bitdepth = reg_val & 0xf;
	info->chroma_bitdepth = (reg_val >> 4) & 0xf;

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_SEQ_PARAM);
	profile_compatibility_flag = (reg_val >> 12) & 0xff;
	info->profile = (reg_val >> 24) & 0x1f;

	if (inst->std == W_HEVC_DEC) {
		/* guessing profile */
		if (!info->profile) {
			if ((profile_compatibility_flag & 0x06) == 0x06)
				info->profile = HEVC_PROFILE_MAIN; /* main profile */
			else if (profile_compatibility_flag & 0x04)
				info->profile = HEVC_PROFILE_MAIN10; /* main10 profile */
			else if (profile_compatibility_flag & 0x08)
				/* main still picture profile */
				info->profile = HEVC_PROFILE_STILLPICTURE;
			else
				info->profile = HEVC_PROFILE_MAIN; /* for old version HM */
		}
	} else if (inst->std == W_AVC_DEC) {
		info->profile = FIELD_GET(SEQ_PARAM_PROFILE_MASK, reg_val);
	}

	info->vlc_buf_size = vpu_read_reg(inst->dev, W5_RET_VLC_BUF_SIZE);
	info->param_buf_size = vpu_read_reg(inst->dev, W5_RET_PARAM_BUF_SIZE);
	p_dec_info->vlc_buf_size = info->vlc_buf_size;
	p_dec_info->param_buf_size = info->param_buf_size;
}

int wave5_vpu_dec_get_seq_info(struct vpu_instance *inst, struct dec_initial_info *info)
{
	int ret;
	u32 reg_val;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;

	vpu_write_reg(inst->dev, W5_CMD_DEC_ADDR_REPORT_BASE, p_dec_info->user_data_buf_addr);
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_SIZE, p_dec_info->user_data_buf_size);
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_PARAM, REPORT_PARAM_ENDIANNESS_BIG_ENDIAN);

	/* send QUERY cmd */
	ret = wave5_send_query(inst->dev, inst, GET_RESULT);
	if (ret)
		return ret;

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	dev_dbg(inst->dev->dev, "%s: init seq complete (queue %u : %u)\n", __func__,
		p_dec_info->instance_queue_count, p_dec_info->report_queue_count);

	/* this is not a fatal error, set ret to -EIO but don't return immediately */
	if (vpu_read_reg(inst->dev, W5_RET_DEC_DECODING_SUCCESS) != 1) {
		info->seq_init_err_reason = vpu_read_reg(inst->dev, W5_RET_DEC_ERR_INFO);
		ret = -EIO;
	}

	wave5_get_dec_seq_result(inst, info);

	return ret;
}

int wave5_vpu_dec_register_framebuffer(struct vpu_instance *inst, struct frame_buffer *fb_arr,
				       enum tiled_map_type map_type, unsigned int count)
{
	int ret;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct dec_initial_info *init_info = &p_dec_info->initial_info;
	size_t remain, idx, j, i, cnt_8_chunk, size;
	u32 start_no, end_no;
	u32 reg_val, cbcr_interleave, nv21, pic_size;
	u32 addr_y, addr_cb, addr_cr;
	u32 mv_col_size, frame_width, frame_height, fbc_y_tbl_size, fbc_c_tbl_size;
	struct vpu_buf vb_buf;
	bool justified = WTL_RIGHT_JUSTIFIED;
	u32 format_no = WTL_PIXEL_8BIT;
	u32 color_format = 0;
	u32 pixel_order = 1;
	u32 bwb_flag = (map_type == LINEAR_FRAME_MAP) ? 1 : 0;

	cbcr_interleave = inst->cbcr_interleave;
	nv21 = inst->nv21;
	mv_col_size = 0;
	fbc_y_tbl_size = 0;
	fbc_c_tbl_size = 0;

	if (map_type >= COMPRESSED_FRAME_MAP) {
		cbcr_interleave = 0;
		nv21 = 0;

		switch (inst->std) {
		case W_HEVC_DEC:
			mv_col_size = WAVE5_DEC_HEVC_BUF_SIZE(init_info->pic_width,
							      init_info->pic_height);
			break;
		case W_AVC_DEC:
			mv_col_size = WAVE5_DEC_AVC_BUF_SIZE(init_info->pic_width,
							     init_info->pic_height);
			break;
		default:
			return -EINVAL;
		}

		if (inst->std == W_HEVC_DEC || inst->std == W_AVC_DEC) {
			size = ALIGN(ALIGN(mv_col_size, 16), BUFFER_MARGIN) + BUFFER_MARGIN;
			ret = wave5_vdi_allocate_array(inst->dev, p_dec_info->vb_mv, count, size);
			if (ret)
				goto free_mv_buffers;
		}

		frame_width = init_info->pic_width;
		frame_height = init_info->pic_height;
		fbc_y_tbl_size = ALIGN(WAVE5_FBC_LUMA_TABLE_SIZE(frame_width, frame_height), 16);
		fbc_c_tbl_size = ALIGN(WAVE5_FBC_CHROMA_TABLE_SIZE(frame_width, frame_height), 16);

		size = ALIGN(fbc_y_tbl_size, BUFFER_MARGIN) + BUFFER_MARGIN;
		ret = wave5_vdi_allocate_array(inst->dev, p_dec_info->vb_fbc_y_tbl, count, size);
		if (ret)
			goto free_fbc_y_tbl_buffers;

		size = ALIGN(fbc_c_tbl_size, BUFFER_MARGIN) + BUFFER_MARGIN;
		ret = wave5_vdi_allocate_array(inst->dev, p_dec_info->vb_fbc_c_tbl, count, size);
		if (ret)
			goto free_fbc_c_tbl_buffers;

		pic_size = (init_info->pic_width << 16) | (init_info->pic_height);

		vb_buf.size = (p_dec_info->vlc_buf_size * VLC_BUF_NUM) +
				(p_dec_info->param_buf_size * COMMAND_QUEUE_DEPTH);
		vb_buf.daddr = 0;

		if (vb_buf.size != p_dec_info->vb_task.size) {
			wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_task);
			ret = wave5_vdi_allocate_dma_memory(inst->dev, &vb_buf);
			if (ret)
				goto free_fbc_c_tbl_buffers;

			p_dec_info->vb_task = vb_buf;
		}

		vpu_write_reg(inst->dev, W5_CMD_SET_FB_ADDR_TASK_BUF,
			      p_dec_info->vb_task.daddr);
		vpu_write_reg(inst->dev, W5_CMD_SET_FB_TASK_BUF_SIZE, vb_buf.size);
	} else {
		pic_size = (init_info->pic_width << 16) | (init_info->pic_height);

		if (inst->output_format == FORMAT_422)
			color_format = 1;
	}
	vpu_write_reg(inst->dev, W5_PIC_SIZE, pic_size);

	reg_val = (bwb_flag << 28) |
		  (pixel_order << 23) |
		  (justified << 22) |
		  (format_no << 20) |
		  (color_format << 19) |
		  (nv21 << 17) |
		  (cbcr_interleave << 16) |
		  (fb_arr[0].stride);
	vpu_write_reg(inst->dev, W5_COMMON_PIC_INFO, reg_val);

	remain = count;
	cnt_8_chunk = DIV_ROUND_UP(count, 8);
	idx = 0;
	for (j = 0; j < cnt_8_chunk; j++) {
		reg_val = (j == cnt_8_chunk - 1) << 4 | ((j == 0) << 3);
		vpu_write_reg(inst->dev, W5_SFB_OPTION, reg_val);
		start_no = j * 8;
		end_no = start_no + ((remain >= 8) ? 8 : remain) - 1;

		vpu_write_reg(inst->dev, W5_SET_FB_NUM, (start_no << 8) | end_no);

		for (i = 0; i < 8 && i < remain; i++) {
			addr_y = fb_arr[i + start_no].buf_y;
			addr_cb = fb_arr[i + start_no].buf_cb;
			addr_cr = fb_arr[i + start_no].buf_cr;
			vpu_write_reg(inst->dev, W5_ADDR_LUMA_BASE0 + (i << 4), addr_y);
			vpu_write_reg(inst->dev, W5_ADDR_CB_BASE0 + (i << 4), addr_cb);
			if (map_type >= COMPRESSED_FRAME_MAP) {
				/* luma FBC offset table */
				vpu_write_reg(inst->dev, W5_ADDR_FBC_Y_OFFSET0 + (i << 4),
					      p_dec_info->vb_fbc_y_tbl[idx].daddr);
				/* chroma FBC offset table */
				vpu_write_reg(inst->dev, W5_ADDR_FBC_C_OFFSET0 + (i << 4),
					      p_dec_info->vb_fbc_c_tbl[idx].daddr);
				vpu_write_reg(inst->dev, W5_ADDR_MV_COL0 + (i << 2),
					      p_dec_info->vb_mv[idx].daddr);
			} else {
				vpu_write_reg(inst->dev, W5_ADDR_CR_BASE0 + (i << 4), addr_cr);
				vpu_write_reg(inst->dev, W5_ADDR_FBC_C_OFFSET0 + (i << 4), 0);
				vpu_write_reg(inst->dev, W5_ADDR_MV_COL0 + (i << 2), 0);
			}
			idx++;
		}
		remain -= i;

		ret = send_firmware_command(inst, W5_SET_FB, false, NULL, NULL);
		if (ret)
			goto free_buffers;
	}

	reg_val = vpu_read_reg(inst->dev, W5_RET_SUCCESS);
	if (!reg_val) {
		ret = -EIO;
		goto free_buffers;
	}

	return 0;

free_buffers:
	wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_task);
free_fbc_c_tbl_buffers:
	for (i = 0; i < count; i++)
		wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_fbc_c_tbl[i]);
free_fbc_y_tbl_buffers:
	for (i = 0; i < count; i++)
		wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_fbc_y_tbl[i]);
free_mv_buffers:
	for (i = 0; i < count; i++)
		wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_mv[i]);
	return ret;
}

int wave5_vpu_decode(struct vpu_instance *inst, u32 *fail_res)
{
	u32 reg_val;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;

	vpu_write_reg(inst->dev, W5_BS_RD_PTR, p_dec_info->stream_rd_ptr);
	vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);

	vpu_write_reg(inst->dev, W5_BS_OPTION, get_bitstream_options(p_dec_info));

	/* secondary AXI */
	reg_val = p_dec_info->sec_axi_info.use_bit_enable |
		(p_dec_info->sec_axi_info.use_ip_enable << 9) |
		(p_dec_info->sec_axi_info.use_lf_row_enable << 15);
	vpu_write_reg(inst->dev, W5_USE_SEC_AXI, reg_val);

	/* set attributes of user buffer */
	vpu_write_reg(inst->dev, W5_CMD_DEC_USER_MASK, p_dec_info->user_data_enable);

	vpu_write_reg(inst->dev, W5_COMMAND_OPTION, DEC_PIC_NORMAL);
	vpu_write_reg(inst->dev, W5_CMD_DEC_TEMPORAL_ID_PLUS1,
		      (p_dec_info->target_spatial_id << 9) |
		      (p_dec_info->temp_id_select_mode << 8) | p_dec_info->target_temp_id);
	vpu_write_reg(inst->dev, W5_CMD_SEQ_CHANGE_ENABLE_FLAG, p_dec_info->seq_change_mask);
	/* When reordering is disabled we force the latency of the framebuffers */
	vpu_write_reg(inst->dev, W5_CMD_DEC_FORCE_FB_LATENCY_PLUS1, !p_dec_info->reorder_enable);

	ret = send_firmware_command(inst, W5_DEC_ENC_PIC, true, &reg_val, fail_res);
	if (ret == -ETIMEDOUT)
		return ret;

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	dev_dbg(inst->dev->dev, "%s: dec pic sent (queue %u : %u)\n", __func__,
		p_dec_info->instance_queue_count, p_dec_info->report_queue_count);

	if (ret)
		return ret;

	return 0;
}

int wave5_vpu_dec_get_result(struct vpu_instance *inst, struct dec_output_info *result)
{
	int ret;
	u32 index, nal_unit_type, reg_val, sub_layer_info;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct vpu_device *vpu_dev = inst->dev;

	vpu_write_reg(inst->dev, W5_CMD_DEC_ADDR_REPORT_BASE, p_dec_info->user_data_buf_addr);
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_SIZE, p_dec_info->user_data_buf_size);
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_PARAM, REPORT_PARAM_ENDIANNESS_BIG_ENDIAN);

	/* send QUERY cmd */
	ret = wave5_send_query(vpu_dev, inst, GET_RESULT);
	if (ret)
		return ret;

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	dev_dbg(inst->dev->dev, "%s: dec pic complete (queue %u : %u)\n", __func__,
		p_dec_info->instance_queue_count, p_dec_info->report_queue_count);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_TYPE);

	nal_unit_type = (reg_val >> 4) & 0x3f;

	if (inst->std == W_HEVC_DEC) {
		if (reg_val & 0x04)
			result->pic_type = PIC_TYPE_B;
		else if (reg_val & 0x02)
			result->pic_type = PIC_TYPE_P;
		else if (reg_val & 0x01)
			result->pic_type = PIC_TYPE_I;
		else
			result->pic_type = PIC_TYPE_MAX;
		if ((nal_unit_type == 19 || nal_unit_type == 20) && result->pic_type == PIC_TYPE_I)
			/* IDR_W_RADL, IDR_N_LP */
			result->pic_type = PIC_TYPE_IDR;
	} else if (inst->std == W_AVC_DEC) {
		if (reg_val & 0x04)
			result->pic_type = PIC_TYPE_B;
		else if (reg_val & 0x02)
			result->pic_type = PIC_TYPE_P;
		else if (reg_val & 0x01)
			result->pic_type = PIC_TYPE_I;
		else
			result->pic_type = PIC_TYPE_MAX;
		if (nal_unit_type == 5 && result->pic_type == PIC_TYPE_I)
			result->pic_type = PIC_TYPE_IDR;
	}
	index = vpu_read_reg(inst->dev, W5_RET_DEC_DISPLAY_INDEX);
	result->index_frame_display = index;
	index = vpu_read_reg(inst->dev, W5_RET_DEC_DECODED_INDEX);
	result->index_frame_decoded = index;
	result->index_frame_decoded_for_tiled = index;

	sub_layer_info = vpu_read_reg(inst->dev, W5_RET_DEC_SUB_LAYER_INFO);
	result->temporal_id = sub_layer_info & 0x7;

	if (inst->std == W_HEVC_DEC || inst->std == W_AVC_DEC) {
		result->decoded_poc = -1;
		if (result->index_frame_decoded >= 0 ||
		    result->index_frame_decoded == DECODED_IDX_FLAG_SKIP)
			result->decoded_poc = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_POC);
	}

	result->sequence_changed = vpu_read_reg(inst->dev, W5_RET_DEC_NOTIFICATION);
	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_SIZE);
	result->dec_pic_width = reg_val >> 16;
	result->dec_pic_height = reg_val & 0xffff;

	if (result->sequence_changed) {
		memcpy((void *)&p_dec_info->new_seq_info, (void *)&p_dec_info->initial_info,
		       sizeof(struct dec_initial_info));
		wave5_get_dec_seq_result(inst, &p_dec_info->new_seq_info);
	}

	result->dec_host_cmd_tick = vpu_read_reg(inst->dev, W5_RET_DEC_HOST_CMD_TICK);
	result->dec_decode_end_tick = vpu_read_reg(inst->dev, W5_RET_DEC_DECODING_ENC_TICK);

	if (!p_dec_info->first_cycle_check) {
		result->frame_cycle =
			(result->dec_decode_end_tick - result->dec_host_cmd_tick) *
			p_dec_info->cycle_per_tick;
		vpu_dev->last_performance_cycles = result->dec_decode_end_tick;
		p_dec_info->first_cycle_check = true;
	} else if (result->index_frame_decoded_for_tiled != -1) {
		result->frame_cycle =
			(result->dec_decode_end_tick - vpu_dev->last_performance_cycles) *
			p_dec_info->cycle_per_tick;
		vpu_dev->last_performance_cycles = result->dec_decode_end_tick;
		if (vpu_dev->last_performance_cycles < result->dec_host_cmd_tick)
			result->frame_cycle =
				(result->dec_decode_end_tick - result->dec_host_cmd_tick) *
				p_dec_info->cycle_per_tick;
	}

	/* no remaining command. reset frame cycle. */
	if (p_dec_info->instance_queue_count == 0 && p_dec_info->report_queue_count == 0)
		p_dec_info->first_cycle_check = false;

	return 0;
}

int wave5_vpu_re_init(struct device *dev, u8 *fw, size_t size)
{
	struct vpu_buf *common_vb;
	dma_addr_t code_base, temp_base;
	dma_addr_t old_code_base, temp_size;
	u32 code_size, reason_code;
	u32 reg_val;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	common_vb = &vpu_dev->common_mem;

	code_base = common_vb->daddr;
	/* ALIGN TO 4KB */
	code_size = (WAVE5_MAX_CODE_BUF_SIZE & ~0xfff);
	if (code_size < size * 2)
		return -EINVAL;
	temp_base = common_vb->daddr + WAVE5_TEMPBUF_OFFSET;
	temp_size = WAVE5_TEMPBUF_SIZE;

	old_code_base = vpu_read_reg(vpu_dev, W5_VPU_REMAP_PADDR);

	if (old_code_base != code_base + W5_REMAP_INDEX1 * W5_REMAP_MAX_SIZE) {
		int ret;

		ret = wave5_vdi_write_memory(vpu_dev, common_vb, 0, fw, size);
		if (ret < 0) {
			dev_err(vpu_dev->dev,
				"VPU init, Writing firmware to common buffer, fail: %d\n", ret);
			return ret;
		}

		vpu_write_reg(vpu_dev, W5_PO_CONF, 0);

		ret = wave5_vpu_reset(dev, SW_RESET_ON_BOOT);
		if (ret < 0) {
			dev_err(vpu_dev->dev, "VPU init, Resetting the VPU, fail: %d\n", ret);
			return ret;
		}

		remap_page(vpu_dev, code_base, W5_REMAP_INDEX0);
		remap_page(vpu_dev, code_base, W5_REMAP_INDEX1);

		vpu_write_reg(vpu_dev, W5_ADDR_CODE_BASE, code_base);
		vpu_write_reg(vpu_dev, W5_CODE_SIZE, code_size);
		vpu_write_reg(vpu_dev, W5_CODE_PARAM, (WAVE5_UPPER_PROC_AXI_ID << 4) | 0);
		vpu_write_reg(vpu_dev, W5_ADDR_TEMP_BASE, temp_base);
		vpu_write_reg(vpu_dev, W5_TEMP_SIZE, temp_size);

		/* These register must be reset explicitly */
		vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, 0);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, 0);
		vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, 0);

		/* Encoder interrupt */
		reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
		reg_val |= BIT(INT_WAVE5_ENC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
		/* Decoder interrupt */
		reg_val |= BIT(INT_WAVE5_INIT_SEQ);
		reg_val |= BIT(INT_WAVE5_DEC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
		vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

		reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
		if (FIELD_GET(FEATURE_BACKBONE, reg_val)) {
			reg_val = ((WAVE5_PROC_AXI_ID << 28) |
					(WAVE5_PRP_AXI_ID << 24) |
					(WAVE5_FBD_Y_AXI_ID << 20) |
					(WAVE5_FBC_Y_AXI_ID << 16) |
					(WAVE5_FBD_C_AXI_ID << 12) |
					(WAVE5_FBC_C_AXI_ID << 8) |
					(WAVE5_PRI_AXI_ID << 4) |
					WAVE5_SEC_AXI_ID);
			wave5_fio_writel(vpu_dev, W5_BACKBONE_PROG_AXI_ID, reg_val);
		}

		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
		vpu_write_reg(vpu_dev, W5_COMMAND, W5_INIT_VPU);
		vpu_write_reg(vpu_dev, W5_VPU_REMAP_CORE_START, 1);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret) {
			dev_err(vpu_dev->dev, "VPU reinit(W5_VPU_REMAP_CORE_START) timeout\n");
			return ret;
		}

		ret = wave5_vpu_firmware_command_queue_error_check(vpu_dev, &reason_code);
		if (ret)
			return ret;
	}

	return setup_wave5_properties(dev);
}

static int wave5_vpu_sleep_wake(struct device *dev, bool i_sleep_wake, const uint16_t *code,
				size_t size)
{
	u32 reg_val;
	struct vpu_buf *common_vb;
	dma_addr_t code_base;
	u32 code_size, reason_code;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int ret;

	if (i_sleep_wake) {
		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret)
			return ret;

		/*
		 * Declare who has ownership for the host interface access
		 * 1 = VPU
		 * 0 = Host processor
		 */
		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
		vpu_write_reg(vpu_dev, W5_COMMAND, W5_SLEEP_VPU);
		/* Send an interrupt named HOST to the VPU */
		vpu_write_reg(vpu_dev, W5_VPU_HOST_INT_REQ, 1);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret)
			return ret;

		ret = wave5_vpu_firmware_command_queue_error_check(vpu_dev, &reason_code);
		if (ret)
			return ret;
	} else { /* restore */
		common_vb = &vpu_dev->common_mem;

		code_base = common_vb->daddr;
		/* ALIGN TO 4KB */
		code_size = (WAVE5_MAX_CODE_BUF_SIZE & ~0xfff);
		if (code_size < size * 2) {
			dev_err(dev, "size too small\n");
			return -EINVAL;
		}

		/* Power on without DEBUG mode */
		vpu_write_reg(vpu_dev, W5_PO_CONF, 0);

		remap_page(vpu_dev, code_base, W5_REMAP_INDEX0);
		remap_page(vpu_dev, code_base, W5_REMAP_INDEX1);

		vpu_write_reg(vpu_dev, W5_ADDR_CODE_BASE, code_base);
		vpu_write_reg(vpu_dev, W5_CODE_SIZE, code_size);
		vpu_write_reg(vpu_dev, W5_CODE_PARAM, (WAVE5_UPPER_PROC_AXI_ID << 4) | 0);

		/* These register must be reset explicitly */
		vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, 0);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, 0);
		vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, 0);

		/* Encoder interrupt */
		reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
		reg_val |= BIT(INT_WAVE5_ENC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
		/* Decoder interrupt */
		reg_val |= BIT(INT_WAVE5_INIT_SEQ);
		reg_val |= BIT(INT_WAVE5_DEC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
		vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

		reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
		if (FIELD_GET(FEATURE_BACKBONE, reg_val)) {
			reg_val = ((WAVE5_PROC_AXI_ID << 28) |
					(WAVE5_PRP_AXI_ID << 24) |
					(WAVE5_FBD_Y_AXI_ID << 20) |
					(WAVE5_FBC_Y_AXI_ID << 16) |
					(WAVE5_FBD_C_AXI_ID << 12) |
					(WAVE5_FBC_C_AXI_ID << 8) |
					(WAVE5_PRI_AXI_ID << 4) |
					WAVE5_SEC_AXI_ID);
			wave5_fio_writel(vpu_dev, W5_BACKBONE_PROG_AXI_ID, reg_val);
		}

		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
		vpu_write_reg(vpu_dev, W5_COMMAND, W5_WAKEUP_VPU);
		/* Start VPU after settings */
		vpu_write_reg(vpu_dev, W5_VPU_REMAP_CORE_START, 1);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret) {
			dev_err(vpu_dev->dev, "VPU wakeup(W5_VPU_REMAP_CORE_START) timeout\n");
			return ret;
		}

		return wave5_vpu_firmware_command_queue_error_check(vpu_dev, &reason_code);
	}

	return 0;
}

int wave5_vpu_reset(struct device *dev, enum sw_reset_mode reset_mode)
{
	u32 val = 0;
	int ret = 0;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	struct vpu_attr *p_attr = &vpu_dev->attr;
	/* VPU doesn't send response. force to set BUSY flag to 0. */
	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 0);

	if (reset_mode == SW_RESET_SAFETY) {
		ret = wave5_vpu_sleep_wake(dev, true, NULL, 0);
		if (ret)
			return ret;
	}

	val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
	if ((val >> 16) & 0x1)
		p_attr->support_backbone = true;
	if ((val >> 22) & 0x1)
		p_attr->support_vcore_backbone = true;
	if ((val >> 28) & 0x1)
		p_attr->support_vcpu_backbone = true;

	/* waiting for completion of bus transaction */
	if (p_attr->support_backbone) {
		dev_dbg(dev, "%s: backbone supported\n", __func__);

		if (p_attr->support_vcore_backbone) {
			if (p_attr->support_vcpu_backbone) {
				/* step1 : disable request */
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCPU, 0xFF);

				/* step2 : waiting for completion of bus transaction */
				ret = wave5_wait_vcpu_bus_busy(vpu_dev,
							       W5_BACKBONE_BUS_STATUS_VCPU);
				if (ret) {
					wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCPU, 0x00);
					return ret;
				}
			}
			/* step1 : disable request */
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x7);

			/* step2 : waiting for completion of bus transaction */
			if (wave5_wait_bus_busy(vpu_dev, W5_BACKBONE_BUS_STATUS_VCORE0)) {
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x00);
				return -EBUSY;
			}
		} else {
			/* step1 : disable request */
			wave5_fio_writel(vpu_dev, W5_COMBINED_BACKBONE_BUS_CTRL, 0x7);

			/* step2 : waiting for completion of bus transaction */
			if (wave5_wait_bus_busy(vpu_dev, W5_COMBINED_BACKBONE_BUS_STATUS)) {
				wave5_fio_writel(vpu_dev, W5_COMBINED_BACKBONE_BUS_CTRL, 0x00);
				return -EBUSY;
			}
		}
	} else {
		dev_dbg(dev, "%s: backbone NOT supported\n", __func__);
		/* step1 : disable request */
		wave5_fio_writel(vpu_dev, W5_GDI_BUS_CTRL, 0x100);

		/* step2 : waiting for completion of bus transaction */
		ret = wave5_wait_bus_busy(vpu_dev, W5_GDI_BUS_STATUS);
		if (ret) {
			wave5_fio_writel(vpu_dev, W5_GDI_BUS_CTRL, 0x00);
			return ret;
		}
	}

	switch (reset_mode) {
	case SW_RESET_ON_BOOT:
	case SW_RESET_FORCE:
	case SW_RESET_SAFETY:
		val = W5_RST_BLOCK_ALL;
		break;
	default:
		return -EINVAL;
	}

	if (val) {
		vpu_write_reg(vpu_dev, W5_VPU_RESET_REQ, val);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_RESET_STATUS);
		if (ret) {
			vpu_write_reg(vpu_dev, W5_VPU_RESET_REQ, 0);
			return ret;
		}
		vpu_write_reg(vpu_dev, W5_VPU_RESET_REQ, 0);
	}
	/* step3 : must clear GDI_BUS_CTRL after done SW_RESET */
	if (p_attr->support_backbone) {
		if (p_attr->support_vcore_backbone) {
			if (p_attr->support_vcpu_backbone)
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCPU, 0x00);
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x00);
		} else {
			wave5_fio_writel(vpu_dev, W5_COMBINED_BACKBONE_BUS_CTRL, 0x00);
		}
	} else {
		wave5_fio_writel(vpu_dev, W5_GDI_BUS_CTRL, 0x00);
	}
	if (reset_mode == SW_RESET_SAFETY || reset_mode == SW_RESET_FORCE)
		ret = wave5_vpu_sleep_wake(dev, false, NULL, 0);

	return ret;
}

int wave5_vpu_dec_finish_seq(struct vpu_instance *inst, u32 *fail_res)
{
	return send_firmware_command(inst, W5_DESTROY_INSTANCE, true, NULL, fail_res);
}

int wave5_vpu_dec_set_bitstream_flag(struct vpu_instance *inst, bool eos)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;

	p_dec_info->stream_endflag = eos ? 1 : 0;
	vpu_write_reg(inst->dev, W5_BS_OPTION, get_bitstream_options(p_dec_info));
	vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);

	return send_firmware_command(inst, W5_UPDATE_BS, true, NULL, NULL);
}

int wave5_dec_clr_disp_flag(struct vpu_instance *inst, unsigned int index)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_CLR_DISP_IDC, BIT(index));
	vpu_write_reg(inst->dev, W5_CMD_DEC_SET_DISP_IDC, 0);

	ret = wave5_send_query(inst->dev, inst, UPDATE_DISP_FLAG);
	if (ret)
		return ret;

	p_dec_info->frame_display_flag = vpu_read_reg(inst->dev, W5_RET_DEC_DISP_IDC);

	return 0;
}

int wave5_dec_set_disp_flag(struct vpu_instance *inst, unsigned int index)
{
	int ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_CLR_DISP_IDC, 0);
	vpu_write_reg(inst->dev, W5_CMD_DEC_SET_DISP_IDC, BIT(index));

	ret = wave5_send_query(inst->dev, inst, UPDATE_DISP_FLAG);
	if (ret)
		return ret;

	return 0;
}

int wave5_vpu_clear_interrupt(struct vpu_instance *inst, u32 flags)
{
	u32 interrupt_reason;

	interrupt_reason = vpu_read_reg(inst->dev, W5_VPU_VINT_REASON_USR);
	interrupt_reason &= ~flags;
	vpu_write_reg(inst->dev, W5_VPU_VINT_REASON_USR, interrupt_reason);

	return 0;
}

dma_addr_t wave5_dec_get_rd_ptr(struct vpu_instance *inst)
{
	int ret;

	ret = wave5_send_query(inst->dev, inst, GET_BS_RD_PTR);
	if (ret)
		return inst->codec_info->dec_info.stream_rd_ptr;

	return vpu_read_reg(inst->dev, W5_RET_QUERY_DEC_BS_RD_PTR);
}

int wave5_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr)
{
	int ret;

	vpu_write_reg(inst->dev, W5_RET_QUERY_DEC_SET_BS_RD_PTR, addr);

	ret = wave5_send_query(inst->dev, inst, SET_BS_RD_PTR);

	return ret;
}

/************************************************************************/
/* ENCODER functions */
/************************************************************************/

int wave5_vpu_build_up_enc_param(struct device *dev, struct vpu_instance *inst,
				 struct enc_open_param *open_param)
{
	int ret;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	u32 reg_val;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	dma_addr_t buffer_addr;
	size_t buffer_size;

	p_enc_info->cycle_per_tick = 256;
	if (vpu_dev->sram_buf.size) {
		p_enc_info->sec_axi_info.use_enc_rdo_enable = 1;
		p_enc_info->sec_axi_info.use_enc_lf_enable = 1;
	}

	p_enc_info->vb_work.size = WAVE521ENC_WORKBUF_SIZE;
	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &p_enc_info->vb_work);
	if (ret) {
		memset(&p_enc_info->vb_work, 0, sizeof(p_enc_info->vb_work));
		return ret;
	}

	wave5_vdi_clear_memory(vpu_dev, &p_enc_info->vb_work);

	vpu_write_reg(inst->dev, W5_ADDR_WORK_BASE, p_enc_info->vb_work.daddr);
	vpu_write_reg(inst->dev, W5_WORK_SIZE, p_enc_info->vb_work.size);

	vpu_write_reg(inst->dev, W5_CMD_ADDR_SEC_AXI, vpu_dev->sram_buf.daddr);
	vpu_write_reg(inst->dev, W5_CMD_SEC_AXI_SIZE, vpu_dev->sram_buf.size);

	reg_val = (open_param->line_buf_int_en << 6) | BITSTREAM_ENDIANNESS_BIG_ENDIAN;
	vpu_write_reg(inst->dev, W5_CMD_BS_PARAM, reg_val);
	vpu_write_reg(inst->dev, W5_CMD_EXT_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_NUM_CQ_DEPTH_M1, (COMMAND_QUEUE_DEPTH - 1));

	/* This register must be reset explicitly */
	vpu_write_reg(inst->dev, W5_CMD_ENC_SRC_OPTIONS, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_VCORE_INFO, 1);

	ret = send_firmware_command(inst, W5_CREATE_INSTANCE, true, NULL, NULL);
	if (ret)
		goto free_vb_work;

	buffer_addr = open_param->bitstream_buffer;
	buffer_size = open_param->bitstream_buffer_size;
	p_enc_info->stream_rd_ptr = buffer_addr;
	p_enc_info->stream_wr_ptr = buffer_addr;
	p_enc_info->line_buf_int_en = open_param->line_buf_int_en;
	p_enc_info->stream_buf_start_addr = buffer_addr;
	p_enc_info->stream_buf_size = buffer_size;
	p_enc_info->stream_buf_end_addr = buffer_addr + buffer_size;
	p_enc_info->stride = 0;
	p_enc_info->initial_info_obtained = false;
	p_enc_info->product_code = vpu_read_reg(inst->dev, W5_PRODUCT_NUMBER);

	return 0;
free_vb_work:
	if (wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_work))
		memset(&p_enc_info->vb_work, 0, sizeof(p_enc_info->vb_work));
	return ret;
}

static void wave5_set_enc_crop_info(u32 codec, struct enc_wave_param *param, int rot_mode,
				    int src_width, int src_height)
{
	int aligned_width = (codec == W_HEVC_ENC) ? ALIGN(src_width, 32) : ALIGN(src_width, 16);
	int aligned_height = (codec == W_HEVC_ENC) ? ALIGN(src_height, 32) : ALIGN(src_height, 16);
	int pad_right, pad_bot;
	int crop_right, crop_left, crop_top, crop_bot;
	int prp_mode = rot_mode >> 1; /* remove prp_enable bit */

	if (codec == W_HEVC_ENC &&
	    (!rot_mode || prp_mode == 14)) /* prp_mode 14 : hor_mir && ver_mir && rot_180 */
		return;

	pad_right = aligned_width - src_width;
	pad_bot = aligned_height - src_height;

	if (param->conf_win_right > 0)
		crop_right = param->conf_win_right + pad_right;
	else
		crop_right = pad_right;

	if (param->conf_win_bot > 0)
		crop_bot = param->conf_win_bot + pad_bot;
	else
		crop_bot = pad_bot;

	crop_top = param->conf_win_top;
	crop_left = param->conf_win_left;

	param->conf_win_top = crop_top;
	param->conf_win_left = crop_left;
	param->conf_win_bot = crop_bot;
	param->conf_win_right = crop_right;

	switch (prp_mode) {
	case 0:
		return;
	case 1:
	case 15:
		param->conf_win_top = crop_right;
		param->conf_win_left = crop_top;
		param->conf_win_bot = crop_left;
		param->conf_win_right = crop_bot;
		break;
	case 2:
	case 12:
		param->conf_win_top = crop_bot;
		param->conf_win_left = crop_right;
		param->conf_win_bot = crop_top;
		param->conf_win_right = crop_left;
		break;
	case 3:
	case 13:
		param->conf_win_top = crop_left;
		param->conf_win_left = crop_bot;
		param->conf_win_bot = crop_right;
		param->conf_win_right = crop_top;
		break;
	case 4:
	case 10:
		param->conf_win_top = crop_bot;
		param->conf_win_bot = crop_top;
		break;
	case 8:
	case 6:
		param->conf_win_left = crop_right;
		param->conf_win_right = crop_left;
		break;
	case 5:
	case 11:
		param->conf_win_top = crop_left;
		param->conf_win_left = crop_top;
		param->conf_win_bot = crop_right;
		param->conf_win_right = crop_bot;
		break;
	case 7:
	case 9:
		param->conf_win_top = crop_right;
		param->conf_win_left = crop_bot;
		param->conf_win_bot = crop_left;
		param->conf_win_right = crop_top;
		break;
	default:
		WARN(1, "Invalid prp_mode: %d, must be in range of 1 - 15\n", prp_mode);
	}
}

int wave5_vpu_enc_init_seq(struct vpu_instance *inst)
{
	u32 reg_val = 0, rot_mir_mode, fixed_cu_size_mode = 0x7;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct enc_open_param *p_open_param = &p_enc_info->open_param;
	struct enc_wave_param *p_param = &p_open_param->wave_param;

	/*
	 * OPT_COMMON:
	 *	the last SET_PARAM command should be called with OPT_COMMON
	 */
	rot_mir_mode = 0;
	if (p_enc_info->rotation_enable) {
		switch (p_enc_info->rotation_angle) {
		case 0:
			rot_mir_mode |= NONE_ROTATE;
			break;
		case 90:
			rot_mir_mode |= ROT_CLOCKWISE_90;
			break;
		case 180:
			rot_mir_mode |= ROT_CLOCKWISE_180;
			break;
		case 270:
			rot_mir_mode |= ROT_CLOCKWISE_270;
			break;
		}
	}

	if (p_enc_info->mirror_enable) {
		switch (p_enc_info->mirror_direction) {
		case MIRDIR_NONE:
			rot_mir_mode |= NONE_ROTATE;
			break;
		case MIRDIR_VER:
			rot_mir_mode |= MIR_VER_FLIP;
			break;
		case MIRDIR_HOR:
			rot_mir_mode |= MIR_HOR_FLIP;
			break;
		case MIRDIR_HOR_VER:
			rot_mir_mode |= MIR_HOR_VER_FLIP;
			break;
		}
	}

	wave5_set_enc_crop_info(inst->std, p_param, rot_mir_mode, p_open_param->pic_width,
				p_open_param->pic_height);

	/* SET_PARAM + COMMON */
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_SET_PARAM_OPTION, OPT_COMMON);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_SRC_SIZE, p_open_param->pic_height << 16
			| p_open_param->pic_width);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MAP_ENDIAN, VDI_LITTLE_ENDIAN);

	reg_val = p_param->profile |
		(p_param->level << 3) |
		(p_param->internal_bit_depth << 14);
	if (inst->std == W_HEVC_ENC)
		reg_val |= (p_param->tier << 12) |
			(p_param->tmvp_enable << 23) |
			(p_param->sao_enable << 24) |
			(p_param->skip_intra_trans << 25) |
			(p_param->strong_intra_smooth_enable << 27) |
			(p_param->en_still_picture << 30);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_SPS_PARAM, reg_val);

	reg_val = (p_param->lossless_enable) |
		(p_param->const_intra_pred_flag << 1) |
		(p_param->lf_cross_slice_boundary_enable << 2) |
		(p_param->wpp_enable << 4) |
		(p_param->disable_deblk << 5) |
		((p_param->beta_offset_div2 & 0xF) << 6) |
		((p_param->tc_offset_div2 & 0xF) << 10) |
		((p_param->chroma_cb_qp_offset & 0x1F) << 14) |
		((p_param->chroma_cr_qp_offset & 0x1F) << 19) |
		(p_param->transform8x8_enable << 29) |
		(p_param->entropy_coding_mode << 30);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_PPS_PARAM, reg_val);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_GOP_PARAM, p_param->gop_preset_idx);

	if (inst->std == W_AVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_PARAM, p_param->intra_qp |
				((p_param->intra_period & 0x7ff) << 6) |
				((p_param->avc_idr_period & 0x7ff) << 17));
	else if (inst->std == W_HEVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_PARAM,
			      p_param->decoding_refresh_type | (p_param->intra_qp << 3) |
				(p_param->intra_period << 16));

	reg_val = (p_param->rdo_skip << 2) |
		(p_param->lambda_scaling_enable << 3) |
		(fixed_cu_size_mode << 5) |
		(p_param->intra_nx_n_enable << 8) |
		(p_param->max_num_merge << 18);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RDO_PARAM, reg_val);

	if (inst->std == W_AVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_REFRESH,
			      p_param->intra_mb_refresh_arg << 16 | p_param->intra_mb_refresh_mode);
	else if (inst->std == W_HEVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_REFRESH,
			      p_param->intra_refresh_arg << 16 | p_param->intra_refresh_mode);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_FRAME_RATE, p_open_param->frame_rate_info);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_TARGET_RATE, p_open_param->bit_rate);

	reg_val = p_open_param->rc_enable |
		(p_param->hvs_qp_enable << 2) |
		(p_param->hvs_qp_scale << 4) |
		((p_param->initial_rc_qp & 0x3F) << 14) |
		(p_open_param->vbv_buffer_size << 20);
	if (inst->std == W_AVC_ENC)
		reg_val |= (p_param->mb_level_rc_enable << 1);
	else if (inst->std == W_HEVC_ENC)
		reg_val |= (p_param->cu_level_rc_enable << 1);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_PARAM, reg_val);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_WEIGHT_PARAM,
		      p_param->rc_weight_buf << 8 | p_param->rc_weight_param);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_MIN_MAX_QP, p_param->min_qp_i |
		      (p_param->max_qp_i << 6) | (p_param->hvs_max_delta_qp << 12));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_INTER_MIN_MAX_QP, p_param->min_qp_p |
		      (p_param->max_qp_p << 6) | (p_param->min_qp_b << 12) |
		      (p_param->max_qp_b << 18));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_BIT_RATIO_LAYER_0_3, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_BIT_RATIO_LAYER_4_7, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_ROT_PARAM, rot_mir_mode);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_BG_PARAM, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_LAMBDA_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CONF_WIN_TOP_BOT,
		      p_param->conf_win_bot << 16 | p_param->conf_win_top);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CONF_WIN_LEFT_RIGHT,
		      p_param->conf_win_right << 16 | p_param->conf_win_left);

	if (inst->std == W_AVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INDEPENDENT_SLICE,
			      p_param->avc_slice_arg << 16 | p_param->avc_slice_mode);
	else if (inst->std == W_HEVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INDEPENDENT_SLICE,
			      p_param->independ_slice_mode_arg << 16 |
			      p_param->independ_slice_mode);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_USER_SCALING_LIST_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NUM_UNITS_IN_TICK, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_TIME_SCALE, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NUM_TICKS_POC_DIFF_ONE, 0);

	if (inst->std == W_HEVC_ENC) {
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU04, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU08, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU16, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU32, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU08, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU16, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU32, 0);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_DEPENDENT_SLICE,
			      p_param->depend_slice_mode_arg << 16 | p_param->depend_slice_mode);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NR_PARAM, 0);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NR_WEIGHT,
			      p_param->nr_intra_weight_y |
			      (p_param->nr_intra_weight_cb << 5) |
			      (p_param->nr_intra_weight_cr << 10) |
			      (p_param->nr_inter_weight_y << 15) |
			      (p_param->nr_inter_weight_cb << 20) |
			      (p_param->nr_inter_weight_cr << 25));
	}
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_VUI_HRD_PARAM, 0);

	return send_firmware_command(inst, W5_ENC_SET_PARAM, true, NULL, NULL);
}

int wave5_vpu_enc_get_seq_info(struct vpu_instance *inst, struct enc_initial_info *info)
{
	int ret;
	u32 reg_val;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;

	/* send QUERY cmd */
	ret = wave5_send_query(inst->dev, inst, GET_RESULT);
	if (ret)
		return ret;

	dev_dbg(inst->dev->dev, "%s: init seq\n", __func__);

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_enc_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_enc_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	if (vpu_read_reg(inst->dev, W5_RET_ENC_ENCODING_SUCCESS) != 1) {
		info->seq_init_err_reason = vpu_read_reg(inst->dev, W5_RET_ENC_ERR_INFO);
		ret = -EIO;
	} else {
		info->warn_info = vpu_read_reg(inst->dev, W5_RET_ENC_WARN_INFO);
	}

	info->min_frame_buffer_count = vpu_read_reg(inst->dev, W5_RET_ENC_NUM_REQUIRED_FB);
	info->min_src_frame_count = vpu_read_reg(inst->dev, W5_RET_ENC_MIN_SRC_BUF_NUM);
	info->vlc_buf_size = vpu_read_reg(inst->dev, W5_RET_VLC_BUF_SIZE);
	info->param_buf_size = vpu_read_reg(inst->dev, W5_RET_PARAM_BUF_SIZE);
	p_enc_info->vlc_buf_size = info->vlc_buf_size;
	p_enc_info->param_buf_size = info->param_buf_size;

	return ret;
}

static u32 calculate_luma_stride(u32 width, u32 bit_depth)
{
	return ALIGN(ALIGN(width, 16) * ((bit_depth > 8) ? 5 : 4), 32);
}

static u32 calculate_chroma_stride(u32 width, u32 bit_depth)
{
	return ALIGN(ALIGN(width / 2, 16) * ((bit_depth > 8) ? 5 : 4), 32);
}

int wave5_vpu_enc_register_framebuffer(struct device *dev, struct vpu_instance *inst,
				       struct frame_buffer *fb_arr, enum tiled_map_type map_type,
				       unsigned int count)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int ret = 0;
	u32 stride;
	u32 start_no, end_no;
	size_t remain, idx, j, i, cnt_8_chunk;
	u32 reg_val = 0, pic_size = 0, mv_col_size, fbc_y_tbl_size, fbc_c_tbl_size;
	u32 sub_sampled_size = 0;
	u32 luma_stride, chroma_stride;
	u32 buf_height = 0, buf_width = 0;
	u32 bit_depth;
	bool avc_encoding = (inst->std == W_AVC_ENC);
	struct vpu_buf vb_mv = {0};
	struct vpu_buf vb_fbc_y_tbl = {0};
	struct vpu_buf vb_fbc_c_tbl = {0};
	struct vpu_buf vb_sub_sam_buf = {0};
	struct vpu_buf vb_task = {0};
	struct enc_open_param *p_open_param;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;

	p_open_param = &p_enc_info->open_param;
	mv_col_size = 0;
	fbc_y_tbl_size = 0;
	fbc_c_tbl_size = 0;
	stride = p_enc_info->stride;
	bit_depth = p_open_param->wave_param.internal_bit_depth;

	if (avc_encoding) {
		buf_width = ALIGN(p_open_param->pic_width, 16);
		buf_height = ALIGN(p_open_param->pic_height, 16);

		if ((p_enc_info->rotation_angle || p_enc_info->mirror_direction) &&
		    !(p_enc_info->rotation_angle == 180 &&
					p_enc_info->mirror_direction == MIRDIR_HOR_VER)) {
			buf_width = ALIGN(p_open_param->pic_width, 16);
			buf_height = ALIGN(p_open_param->pic_height, 16);
		}

		if (p_enc_info->rotation_angle == 90 || p_enc_info->rotation_angle == 270) {
			buf_width = ALIGN(p_open_param->pic_height, 16);
			buf_height = ALIGN(p_open_param->pic_width, 16);
		}
	} else {
		buf_width = ALIGN(p_open_param->pic_width, 8);
		buf_height = ALIGN(p_open_param->pic_height, 8);

		if ((p_enc_info->rotation_angle || p_enc_info->mirror_direction) &&
		    !(p_enc_info->rotation_angle == 180 &&
					p_enc_info->mirror_direction == MIRDIR_HOR_VER)) {
			buf_width = ALIGN(p_open_param->pic_width, 32);
			buf_height = ALIGN(p_open_param->pic_height, 32);
		}

		if (p_enc_info->rotation_angle == 90 || p_enc_info->rotation_angle == 270) {
			buf_width = ALIGN(p_open_param->pic_height, 32);
			buf_height = ALIGN(p_open_param->pic_width, 32);
		}
	}

	pic_size = (buf_width << 16) | buf_height;

	if (avc_encoding) {
		mv_col_size = WAVE5_ENC_AVC_BUF_SIZE(buf_width, buf_height);
		vb_mv.daddr = 0;
		vb_mv.size = ALIGN(mv_col_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	} else {
		mv_col_size = WAVE5_ENC_HEVC_BUF_SIZE(buf_width, buf_height);
		mv_col_size = ALIGN(mv_col_size, 16);
		vb_mv.daddr = 0;
		vb_mv.size = ALIGN(mv_col_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	}

	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_mv);
	if (ret)
		return ret;

	p_enc_info->vb_mv = vb_mv;

	fbc_y_tbl_size = ALIGN(WAVE5_FBC_LUMA_TABLE_SIZE(buf_width, buf_height), 16);
	fbc_c_tbl_size = ALIGN(WAVE5_FBC_CHROMA_TABLE_SIZE(buf_width, buf_height), 16);

	vb_fbc_y_tbl.daddr = 0;
	vb_fbc_y_tbl.size = ALIGN(fbc_y_tbl_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_fbc_y_tbl);
	if (ret)
		goto free_vb_fbc_y_tbl;

	p_enc_info->vb_fbc_y_tbl = vb_fbc_y_tbl;

	vb_fbc_c_tbl.daddr = 0;
	vb_fbc_c_tbl.size = ALIGN(fbc_c_tbl_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_fbc_c_tbl);
	if (ret)
		goto free_vb_fbc_c_tbl;

	p_enc_info->vb_fbc_c_tbl = vb_fbc_c_tbl;

	if (avc_encoding)
		sub_sampled_size = WAVE5_SUBSAMPLED_ONE_SIZE_AVC(buf_width, buf_height);
	else
		sub_sampled_size = WAVE5_SUBSAMPLED_ONE_SIZE(buf_width, buf_height);
	vb_sub_sam_buf.size = ALIGN(sub_sampled_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	vb_sub_sam_buf.daddr = 0;
	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_sub_sam_buf);
	if (ret)
		goto free_vb_sam_buf;

	p_enc_info->vb_sub_sam_buf = vb_sub_sam_buf;

	vb_task.size = (p_enc_info->vlc_buf_size * VLC_BUF_NUM) +
			(p_enc_info->param_buf_size * COMMAND_QUEUE_DEPTH);
	vb_task.daddr = 0;
	if (p_enc_info->vb_task.size == 0) {
		ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_task);
		if (ret)
			goto free_vb_task;

		p_enc_info->vb_task = vb_task;

		vpu_write_reg(inst->dev, W5_CMD_SET_FB_ADDR_TASK_BUF,
			      p_enc_info->vb_task.daddr);
		vpu_write_reg(inst->dev, W5_CMD_SET_FB_TASK_BUF_SIZE, vb_task.size);
	}

	/* set sub-sampled buffer base addr */
	vpu_write_reg(inst->dev, W5_ADDR_SUB_SAMPLED_FB_BASE, vb_sub_sam_buf.daddr);
	/* set sub-sampled buffer size for one frame */
	vpu_write_reg(inst->dev, W5_SUB_SAMPLED_ONE_FB_SIZE, sub_sampled_size);

	vpu_write_reg(inst->dev, W5_PIC_SIZE, pic_size);

	/* set stride of luma/chroma for compressed buffer */
	if ((p_enc_info->rotation_angle || p_enc_info->mirror_direction) &&
	    !(p_enc_info->rotation_angle == 180 &&
	    p_enc_info->mirror_direction == MIRDIR_HOR_VER)) {
		luma_stride = calculate_luma_stride(buf_width, bit_depth);
		chroma_stride = calculate_chroma_stride(buf_width / 2, bit_depth);
	} else {
		luma_stride = calculate_luma_stride(p_open_param->pic_width, bit_depth);
		chroma_stride = calculate_chroma_stride(p_open_param->pic_width / 2, bit_depth);
	}

	vpu_write_reg(inst->dev, W5_FBC_STRIDE, luma_stride << 16 | chroma_stride);
	vpu_write_reg(inst->dev, W5_COMMON_PIC_INFO, stride);

	remain = count;
	cnt_8_chunk = DIV_ROUND_UP(count, 8);
	idx = 0;
	for (j = 0; j < cnt_8_chunk; j++) {
		reg_val = (j == cnt_8_chunk - 1) << 4 | ((j == 0) << 3);
		vpu_write_reg(inst->dev, W5_SFB_OPTION, reg_val);
		start_no = j * 8;
		end_no = start_no + ((remain >= 8) ? 8 : remain) - 1;

		vpu_write_reg(inst->dev, W5_SET_FB_NUM, (start_no << 8) | end_no);

		for (i = 0; i < 8 && i < remain; i++) {
			vpu_write_reg(inst->dev, W5_ADDR_LUMA_BASE0 + (i << 4), fb_arr[i +
					start_no].buf_y);
			vpu_write_reg(inst->dev, W5_ADDR_CB_BASE0 + (i << 4),
				      fb_arr[i + start_no].buf_cb);
			/* luma FBC offset table */
			vpu_write_reg(inst->dev, W5_ADDR_FBC_Y_OFFSET0 + (i << 4),
				      vb_fbc_y_tbl.daddr + idx * fbc_y_tbl_size);
			/* chroma FBC offset table */
			vpu_write_reg(inst->dev, W5_ADDR_FBC_C_OFFSET0 + (i << 4),
				      vb_fbc_c_tbl.daddr + idx * fbc_c_tbl_size);

			vpu_write_reg(inst->dev, W5_ADDR_MV_COL0 + (i << 2),
				      vb_mv.daddr + idx * mv_col_size);
			idx++;
		}
		remain -= i;

		ret = send_firmware_command(inst, W5_SET_FB, false, NULL, NULL);
		if (ret)
			goto free_vb_mem;
	}

	ret = wave5_vpu_firmware_command_queue_error_check(vpu_dev, NULL);
	if (ret)
		goto free_vb_mem;

	return ret;

free_vb_mem:
	wave5_vdi_free_dma_memory(vpu_dev, &vb_task);
free_vb_task:
	wave5_vdi_free_dma_memory(vpu_dev, &vb_sub_sam_buf);
free_vb_sam_buf:
	wave5_vdi_free_dma_memory(vpu_dev, &vb_fbc_c_tbl);
free_vb_fbc_c_tbl:
	wave5_vdi_free_dma_memory(vpu_dev, &vb_fbc_y_tbl);
free_vb_fbc_y_tbl:
	wave5_vdi_free_dma_memory(vpu_dev, &vb_mv);
	return ret;
}

int wave5_vpu_encode(struct vpu_instance *inst, struct enc_param *option, u32 *fail_res)
{
	u32 src_frame_format;
	u32 reg_val = 0;
	u32 src_stride_c = 0;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct frame_buffer *p_src_frame = option->source_frame;
	struct enc_open_param *p_open_param = &p_enc_info->open_param;
	bool justified = WTL_RIGHT_JUSTIFIED;
	u32 format_no = WTL_PIXEL_8BIT;
	int ret;

	vpu_write_reg(inst->dev, W5_CMD_ENC_BS_START_ADDR, option->pic_stream_buffer_addr);
	vpu_write_reg(inst->dev, W5_CMD_ENC_BS_SIZE, option->pic_stream_buffer_size);
	p_enc_info->stream_buf_start_addr = option->pic_stream_buffer_addr;
	p_enc_info->stream_buf_size = option->pic_stream_buffer_size;
	p_enc_info->stream_buf_end_addr =
		option->pic_stream_buffer_addr + option->pic_stream_buffer_size;

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_AXI_SEL, DEFAULT_SRC_AXI);
	/* secondary AXI */
	reg_val = (p_enc_info->sec_axi_info.use_enc_rdo_enable << 11) |
		(p_enc_info->sec_axi_info.use_enc_lf_enable << 15);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_USE_SEC_AXI, reg_val);

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_REPORT_PARAM, 0);

	/*
	 * CODEOPT_ENC_VCL is used to implicitly encode header/headers to generate bitstream.
	 * (use ENC_PUT_VIDEO_HEADER for give_command to encode only a header)
	 */
	if (option->code_option.implicit_header_encode)
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CODE_OPTION,
			      CODEOPT_ENC_HEADER_IMPLICIT | CODEOPT_ENC_VCL |
			      (option->code_option.encode_aud << 5) |
			      (option->code_option.encode_eos << 6) |
			      (option->code_option.encode_eob << 7));
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CODE_OPTION,
			      option->code_option.implicit_header_encode |
			      (option->code_option.encode_vcl << 1) |
			      (option->code_option.encode_vps << 2) |
			      (option->code_option.encode_sps << 3) |
			      (option->code_option.encode_pps << 4) |
			      (option->code_option.encode_aud << 5) |
			      (option->code_option.encode_eos << 6) |
			      (option->code_option.encode_eob << 7));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PIC_PARAM, 0);

	if (option->src_end_flag)
		/* no more source images. */
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_PIC_IDX, 0xFFFFFFFF);
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_PIC_IDX, option->src_idx);

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_Y, p_src_frame->buf_y);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_U, p_src_frame->buf_cb);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_V, p_src_frame->buf_cr);

	switch (p_open_param->src_format) {
	case FORMAT_420:
	case FORMAT_422:
	case FORMAT_YUYV:
	case FORMAT_YVYU:
	case FORMAT_UYVY:
	case FORMAT_VYUY:
		justified = WTL_LEFT_JUSTIFIED;
		format_no = WTL_PIXEL_8BIT;
		src_stride_c = inst->cbcr_interleave ? p_src_frame->stride :
			(p_src_frame->stride / 2);
		src_stride_c = (p_open_param->src_format == FORMAT_422) ? src_stride_c * 2 :
			src_stride_c;
		break;
	case FORMAT_420_P10_16BIT_MSB:
	case FORMAT_422_P10_16BIT_MSB:
	case FORMAT_YUYV_P10_16BIT_MSB:
	case FORMAT_YVYU_P10_16BIT_MSB:
	case FORMAT_UYVY_P10_16BIT_MSB:
	case FORMAT_VYUY_P10_16BIT_MSB:
		justified = WTL_RIGHT_JUSTIFIED;
		format_no = WTL_PIXEL_16BIT;
		src_stride_c = inst->cbcr_interleave ? p_src_frame->stride :
			(p_src_frame->stride / 2);
		src_stride_c = (p_open_param->src_format ==
				FORMAT_422_P10_16BIT_MSB) ? src_stride_c * 2 : src_stride_c;
		break;
	case FORMAT_420_P10_16BIT_LSB:
	case FORMAT_422_P10_16BIT_LSB:
	case FORMAT_YUYV_P10_16BIT_LSB:
	case FORMAT_YVYU_P10_16BIT_LSB:
	case FORMAT_UYVY_P10_16BIT_LSB:
	case FORMAT_VYUY_P10_16BIT_LSB:
		justified = WTL_LEFT_JUSTIFIED;
		format_no = WTL_PIXEL_16BIT;
		src_stride_c = inst->cbcr_interleave ? p_src_frame->stride :
			(p_src_frame->stride / 2);
		src_stride_c = (p_open_param->src_format ==
				FORMAT_422_P10_16BIT_LSB) ? src_stride_c * 2 : src_stride_c;
		break;
	case FORMAT_420_P10_32BIT_MSB:
	case FORMAT_422_P10_32BIT_MSB:
	case FORMAT_YUYV_P10_32BIT_MSB:
	case FORMAT_YVYU_P10_32BIT_MSB:
	case FORMAT_UYVY_P10_32BIT_MSB:
	case FORMAT_VYUY_P10_32BIT_MSB:
		justified = WTL_RIGHT_JUSTIFIED;
		format_no = WTL_PIXEL_32BIT;
		src_stride_c = inst->cbcr_interleave ? p_src_frame->stride :
			ALIGN(p_src_frame->stride / 2, 16) * BIT(inst->cbcr_interleave);
		src_stride_c = (p_open_param->src_format ==
				FORMAT_422_P10_32BIT_MSB) ? src_stride_c * 2 : src_stride_c;
		break;
	case FORMAT_420_P10_32BIT_LSB:
	case FORMAT_422_P10_32BIT_LSB:
	case FORMAT_YUYV_P10_32BIT_LSB:
	case FORMAT_YVYU_P10_32BIT_LSB:
	case FORMAT_UYVY_P10_32BIT_LSB:
	case FORMAT_VYUY_P10_32BIT_LSB:
		justified = WTL_LEFT_JUSTIFIED;
		format_no = WTL_PIXEL_32BIT;
		src_stride_c = inst->cbcr_interleave ? p_src_frame->stride :
			ALIGN(p_src_frame->stride / 2, 16) * BIT(inst->cbcr_interleave);
		src_stride_c = (p_open_param->src_format ==
				FORMAT_422_P10_32BIT_LSB) ? src_stride_c * 2 : src_stride_c;
		break;
	default:
		return -EINVAL;
	}

	src_frame_format = (inst->cbcr_interleave << 1) | (inst->nv21);
	switch (p_open_param->packed_format) {
	case PACKED_YUYV:
		src_frame_format = 4;
		break;
	case PACKED_YVYU:
		src_frame_format = 5;
		break;
	case PACKED_UYVY:
		src_frame_format = 6;
		break;
	case PACKED_VYUY:
		src_frame_format = 7;
		break;
	default:
		break;
	}

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_STRIDE,
		      (p_src_frame->stride << 16) | src_stride_c);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_FORMAT, src_frame_format |
		      (format_no << 3) | (justified << 5) | (PIC_SRC_ENDIANNESS_BIG_ENDIAN << 6));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CUSTOM_MAP_OPTION_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CUSTOM_MAP_OPTION_PARAM, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_LONGTERM_PIC, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_SIGMA_Y, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_SIGMA_C, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_MEAN_Y, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_MEAN_C, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PREFIX_SEI_INFO, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PREFIX_SEI_NAL_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SUFFIX_SEI_INFO, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SUFFIX_SEI_NAL_ADDR, 0);

	ret = send_firmware_command(inst, W5_DEC_ENC_PIC, true, &reg_val, fail_res);
	if (ret == -ETIMEDOUT)
		return ret;

	p_enc_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_enc_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	if (ret)
		return ret;

	return 0;
}

int wave5_vpu_enc_get_result(struct vpu_instance *inst, struct enc_output_info *result)
{
	int ret;
	u32 encoding_success;
	u32 reg_val;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct vpu_device *vpu_dev = inst->dev;

	ret = wave5_send_query(inst->dev, inst, GET_RESULT);
	if (ret)
		return ret;

	dev_dbg(inst->dev->dev, "%s: enc pic complete\n", __func__);

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_enc_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_enc_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	encoding_success = vpu_read_reg(inst->dev, W5_RET_ENC_ENCODING_SUCCESS);
	if (!encoding_success) {
		result->error_reason = vpu_read_reg(inst->dev, W5_RET_ENC_ERR_INFO);
		return -EIO;
	}

	result->warn_info = vpu_read_reg(inst->dev, W5_RET_ENC_WARN_INFO);

	reg_val = vpu_read_reg(inst->dev, W5_RET_ENC_PIC_TYPE);
	result->pic_type = reg_val & 0xFFFF;

	result->enc_vcl_nut = vpu_read_reg(inst->dev, W5_RET_ENC_VCL_NUT);
	/*
	 * To get the reconstructed frame use the following index on
	 * inst->frame_buf
	 */
	result->recon_frame_index = vpu_read_reg(inst->dev, W5_RET_ENC_PIC_IDX);
	result->enc_pic_byte = vpu_read_reg(inst->dev, W5_RET_ENC_PIC_BYTE);
	result->enc_src_idx = vpu_read_reg(inst->dev, W5_RET_ENC_USED_SRC_IDX);
	p_enc_info->stream_wr_ptr = vpu_read_reg(inst->dev, W5_RET_ENC_WR_PTR);
	p_enc_info->stream_rd_ptr = vpu_read_reg(inst->dev, W5_RET_ENC_RD_PTR);

	result->bitstream_buffer = vpu_read_reg(inst->dev, W5_RET_ENC_RD_PTR);
	result->rd_ptr = p_enc_info->stream_rd_ptr;
	result->wr_ptr = p_enc_info->stream_wr_ptr;

	/*result for header only(no vcl) encoding */
	if (result->recon_frame_index == RECON_IDX_FLAG_HEADER_ONLY)
		result->bitstream_size = result->enc_pic_byte;
	else if (result->recon_frame_index < 0)
		result->bitstream_size = 0;
	else
		result->bitstream_size = result->enc_pic_byte;

	result->enc_host_cmd_tick = vpu_read_reg(inst->dev, W5_RET_ENC_HOST_CMD_TICK);
	result->enc_encode_end_tick = vpu_read_reg(inst->dev, W5_RET_ENC_ENCODING_END_TICK);

	if (!p_enc_info->first_cycle_check) {
		result->frame_cycle = (result->enc_encode_end_tick - result->enc_host_cmd_tick) *
			p_enc_info->cycle_per_tick;
		p_enc_info->first_cycle_check = true;
	} else {
		result->frame_cycle =
			(result->enc_encode_end_tick - vpu_dev->last_performance_cycles) *
			p_enc_info->cycle_per_tick;
		if (vpu_dev->last_performance_cycles < result->enc_host_cmd_tick)
			result->frame_cycle = (result->enc_encode_end_tick -
					result->enc_host_cmd_tick) * p_enc_info->cycle_per_tick;
	}
	vpu_dev->last_performance_cycles = result->enc_encode_end_tick;

	return 0;
}

int wave5_vpu_enc_finish_seq(struct vpu_instance *inst, u32 *fail_res)
{
	return send_firmware_command(inst, W5_DESTROY_INSTANCE, true, NULL, fail_res);
}

static bool wave5_vpu_enc_check_common_param_valid(struct vpu_instance *inst,
						   struct enc_open_param *open_param)
{
	bool low_delay = true;
	struct enc_wave_param *param = &open_param->wave_param;
	struct vpu_device *vpu_dev = inst->dev;
	struct device *dev = vpu_dev->dev;
	u32 num_ctu_row = (open_param->pic_height + 64 - 1) / 64;
	u32 num_ctu_col = (open_param->pic_width + 64 - 1) / 64;
	u32 ctu_sz = num_ctu_col * num_ctu_row;

	if (inst->std == W_HEVC_ENC && low_delay &&
	    param->decoding_refresh_type == DEC_REFRESH_TYPE_CRA) {
		dev_warn(dev,
			 "dec_refresh_type(CRA) shouldn't be used together with low delay GOP\n");
		dev_warn(dev, "Suggested configuration parameter: decoding refresh type (IDR)\n");
		param->decoding_refresh_type = 2;
	}

	if (param->wpp_enable && param->independ_slice_mode) {
		unsigned int num_ctb_in_width = ALIGN(open_param->pic_width, 64) >> 6;

		if (param->independ_slice_mode_arg % num_ctb_in_width) {
			dev_err(dev, "independ_slice_mode_arg %u must be a multiple of %u\n",
				param->independ_slice_mode_arg, num_ctb_in_width);
			return false;
		}
	}

	/* multi-slice & wpp */
	if (param->wpp_enable && param->depend_slice_mode) {
		dev_err(dev, "wpp_enable && depend_slice_mode cannot be used simultaneously\n");
		return false;
	}

	if (!param->independ_slice_mode && param->depend_slice_mode) {
		dev_err(dev, "depend_slice_mode requires independ_slice_mode\n");
		return false;
	} else if (param->independ_slice_mode &&
		   param->depend_slice_mode == DEPEND_SLICE_MODE_RECOMMENDED &&
		   param->independ_slice_mode_arg < param->depend_slice_mode_arg) {
		dev_err(dev, "independ_slice_mode_arg: %u must be smaller than %u\n",
			param->independ_slice_mode_arg, param->depend_slice_mode_arg);
		return false;
	}

	if (param->independ_slice_mode && param->independ_slice_mode_arg > 65535) {
		dev_err(dev, "independ_slice_mode_arg: %u must be smaller than 65535\n",
			param->independ_slice_mode_arg);
		return false;
	}

	if (param->depend_slice_mode && param->depend_slice_mode_arg > 65535) {
		dev_err(dev, "depend_slice_mode_arg: %u must be smaller than 65535\n",
			param->depend_slice_mode_arg);
		return false;
	}

	if (param->conf_win_top % 2) {
		dev_err(dev, "conf_win_top: %u, must be a multiple of 2\n", param->conf_win_top);
		return false;
	}

	if (param->conf_win_bot % 2) {
		dev_err(dev, "conf_win_bot: %u, must be a multiple of 2\n", param->conf_win_bot);
		return false;
	}

	if (param->conf_win_left % 2) {
		dev_err(dev, "conf_win_left: %u, must be a multiple of 2\n", param->conf_win_left);
		return false;
	}

	if (param->conf_win_right % 2) {
		dev_err(dev, "conf_win_right: %u, Must be a multiple of 2\n",
			param->conf_win_right);
		return false;
	}

	if (param->lossless_enable && open_param->rc_enable) {
		dev_err(dev, "option rate_control cannot be used with lossless_coding\n");
		return false;
	}

	if (param->lossless_enable && !param->skip_intra_trans) {
		dev_err(dev, "option intra_trans_skip must be enabled with lossless_coding\n");
		return false;
	}

	/* intra refresh */
	if (param->intra_refresh_mode && param->intra_refresh_arg == 0) {
		dev_err(dev, "Invalid refresh argument, mode: %u, refresh: %u must be > 0\n",
			param->intra_refresh_mode, param->intra_refresh_arg);
		return false;
	}
	switch (param->intra_refresh_mode) {
	case REFRESH_MODE_CTU_ROWS:
		if (param->intra_mb_refresh_arg > num_ctu_row)
			goto invalid_refresh_argument;
		break;
	case REFRESH_MODE_CTU_COLUMNS:
		if (param->intra_refresh_arg > num_ctu_col)
			goto invalid_refresh_argument;
		break;
	case REFRESH_MODE_CTU_STEP_SIZE:
		if (param->intra_refresh_arg > ctu_sz)
			goto invalid_refresh_argument;
		break;
	case REFRESH_MODE_CTUS:
		if (param->intra_refresh_arg > ctu_sz)
			goto invalid_refresh_argument;
		if (param->lossless_enable) {
			dev_err(dev, "mode: %u cannot be used lossless_enable",
				param->intra_refresh_mode);
			return false;
		}
	};
	return true;

invalid_refresh_argument:
	dev_err(dev, "Invalid refresh argument, mode: %u, refresh: %u > W(%u)xH(%u)\n",
		param->intra_refresh_mode, param->intra_refresh_arg,
		num_ctu_row, num_ctu_col);
	return false;
}

static bool wave5_vpu_enc_check_param_valid(struct vpu_device *vpu_dev,
					    struct enc_open_param *open_param)
{
	struct enc_wave_param *param = &open_param->wave_param;

	if (open_param->rc_enable) {
		if (param->min_qp_i > param->max_qp_i || param->min_qp_p > param->max_qp_p ||
		    param->min_qp_b > param->max_qp_b) {
			dev_err(vpu_dev->dev, "Configuration failed because min_qp is greater than max_qp\n");
			dev_err(vpu_dev->dev, "Suggested configuration parameters: min_qp = max_qp\n");
			return false;
		}

		if (open_param->bit_rate <= (int)open_param->frame_rate_info) {
			dev_err(vpu_dev->dev,
				"enc_bit_rate: %u must be greater than the frame_rate: %u\n",
				open_param->bit_rate, (int)open_param->frame_rate_info);
			return false;
		}
	}

	return true;
}

int wave5_vpu_enc_check_open_param(struct vpu_instance *inst, struct enc_open_param *open_param)
{
	u32 pic_width;
	u32 pic_height;
	s32 product_id = inst->dev->product;
	struct vpu_attr *p_attr = &inst->dev->attr;
	struct enc_wave_param *param;

	if (!open_param)
		return -EINVAL;

	param = &open_param->wave_param;
	pic_width = open_param->pic_width;
	pic_height = open_param->pic_height;

	if (inst->id >= MAX_NUM_INSTANCE) {
		dev_err(inst->dev->dev, "Too many simultaneous instances: %d (max: %u)\n",
			inst->id, MAX_NUM_INSTANCE);
		return -EOPNOTSUPP;
	}

	if (inst->std != W_HEVC_ENC &&
	    !(inst->std == W_AVC_ENC && product_id == PRODUCT_ID_521)) {
		dev_err(inst->dev->dev, "Unsupported encoder-codec & product combination\n");
		return -EOPNOTSUPP;
	}

	if (param->internal_bit_depth == 10) {
		if (inst->std == W_HEVC_ENC && !p_attr->support_hevc10bit_enc) {
			dev_err(inst->dev->dev,
				"Flag support_hevc10bit_enc must be set to encode 10bit HEVC\n");
			return -EOPNOTSUPP;
		} else if (inst->std == W_AVC_ENC && !p_attr->support_avc10bit_enc) {
			dev_err(inst->dev->dev,
				"Flag support_avc10bit_enc must be set to encode 10bit AVC\n");
			return -EOPNOTSUPP;
		}
	}

	if (!open_param->frame_rate_info) {
		dev_err(inst->dev->dev, "No frame rate information.\n");
		return -EINVAL;
	}

	if (open_param->bit_rate > MAX_BIT_RATE) {
		dev_err(inst->dev->dev, "Invalid encoding bit-rate: %u (valid: 0-%u)\n",
			open_param->bit_rate, MAX_BIT_RATE);
		return -EINVAL;
	}

	if (pic_width < W5_MIN_ENC_PIC_WIDTH || pic_width > W5_MAX_ENC_PIC_WIDTH ||
	    pic_height < W5_MIN_ENC_PIC_HEIGHT || pic_height > W5_MAX_ENC_PIC_HEIGHT) {
		dev_err(inst->dev->dev, "Invalid encoding dimension: %ux%u\n",
			pic_width, pic_height);
		return -EINVAL;
	}

	if (param->profile) {
		if (inst->std == W_HEVC_ENC) {
			if ((param->profile != HEVC_PROFILE_MAIN ||
			     (param->profile == HEVC_PROFILE_MAIN &&
			      param->internal_bit_depth > 8)) &&
			    (param->profile != HEVC_PROFILE_MAIN10 ||
			     (param->profile == HEVC_PROFILE_MAIN10 &&
			      param->internal_bit_depth < 10)) &&
			    param->profile != HEVC_PROFILE_STILLPICTURE) {
				dev_err(inst->dev->dev,
					"Invalid HEVC encoding profile: %u (bit-depth: %u)\n",
					param->profile, param->internal_bit_depth);
				return -EINVAL;
			}
		} else if (inst->std == W_AVC_ENC) {
			if ((param->internal_bit_depth > 8 &&
			     param->profile != H264_PROFILE_HIGH10)) {
				dev_err(inst->dev->dev,
					"Invalid AVC encoding profile: %u (bit-depth: %u)\n",
					param->profile, param->internal_bit_depth);
				return -EINVAL;
			}
		}
	}

	if (param->decoding_refresh_type > DEC_REFRESH_TYPE_IDR) {
		dev_err(inst->dev->dev, "Invalid decoding refresh type: %u (valid: 0-2)\n",
			param->decoding_refresh_type);
		return -EINVAL;
	}

	if (param->intra_refresh_mode > REFRESH_MODE_CTUS) {
		dev_err(inst->dev->dev, "Invalid intra refresh mode: %d (valid: 0-4)\n",
			param->intra_refresh_mode);
		return -EINVAL;
	}

	if (inst->std == W_HEVC_ENC && param->independ_slice_mode &&
	    param->depend_slice_mode > DEPEND_SLICE_MODE_BOOST) {
		dev_err(inst->dev->dev,
			"Can't combine slice modes: independent and fast dependent for HEVC\n");
		return -EINVAL;
	}

	if (!param->disable_deblk) {
		if (param->beta_offset_div2 < -6 || param->beta_offset_div2 > 6) {
			dev_err(inst->dev->dev, "Invalid beta offset: %d (valid: -6-6)\n",
				param->beta_offset_div2);
			return -EINVAL;
		}

		if (param->tc_offset_div2 < -6 || param->tc_offset_div2 > 6) {
			dev_err(inst->dev->dev, "Invalid tc offset: %d (valid: -6-6)\n",
				param->tc_offset_div2);
			return -EINVAL;
		}
	}

	if (param->intra_qp > MAX_INTRA_QP) {
		dev_err(inst->dev->dev,
			"Invalid intra quantization parameter: %u (valid: 0-%u)\n",
			param->intra_qp, MAX_INTRA_QP);
		return -EINVAL;
	}

	if (open_param->rc_enable) {
		if (param->min_qp_i > MAX_INTRA_QP || param->max_qp_i > MAX_INTRA_QP ||
		    param->min_qp_p > MAX_INTRA_QP || param->max_qp_p > MAX_INTRA_QP ||
		    param->min_qp_b > MAX_INTRA_QP || param->max_qp_b > MAX_INTRA_QP) {
			dev_err(inst->dev->dev,
				"Invalid quantization parameter min/max values: "
				"I: %u-%u, P: %u-%u, B: %u-%u (valid for each: 0-%u)\n",
				param->min_qp_i, param->max_qp_i, param->min_qp_p, param->max_qp_p,
				param->min_qp_b, param->max_qp_b, MAX_INTRA_QP);
			return -EINVAL;
		}

		if (param->hvs_qp_enable && param->hvs_max_delta_qp > MAX_HVS_MAX_DELTA_QP) {
			dev_err(inst->dev->dev,
				"Invalid HVS max delta quantization parameter: %u (valid: 0-%u)\n",
				param->hvs_max_delta_qp, MAX_HVS_MAX_DELTA_QP);
			return -EINVAL;
		}

		if (open_param->vbv_buffer_size < MIN_VBV_BUFFER_SIZE ||
		    open_param->vbv_buffer_size > MAX_VBV_BUFFER_SIZE) {
			dev_err(inst->dev->dev, "VBV buffer size: %u (valid: %u-%u)\n",
				open_param->vbv_buffer_size, MIN_VBV_BUFFER_SIZE,
				MAX_VBV_BUFFER_SIZE);
			return -EINVAL;
		}
	}

	if (!wave5_vpu_enc_check_common_param_valid(inst, open_param))
		return -EINVAL;

	if (!wave5_vpu_enc_check_param_valid(inst->dev, open_param))
		return -EINVAL;

	if (param->chroma_cb_qp_offset < -12 || param->chroma_cb_qp_offset > 12) {
		dev_err(inst->dev->dev,
			"Invalid chroma Cb quantization parameter offset: %d (valid: -12-12)\n",
			param->chroma_cb_qp_offset);
		return -EINVAL;
	}

	if (param->chroma_cr_qp_offset < -12 || param->chroma_cr_qp_offset > 12) {
		dev_err(inst->dev->dev,
			"Invalid chroma Cr quantization parameter offset: %d (valid: -12-12)\n",
			param->chroma_cr_qp_offset);
		return -EINVAL;
	}

	if (param->intra_refresh_mode == REFRESH_MODE_CTU_STEP_SIZE && !param->intra_refresh_arg) {
		dev_err(inst->dev->dev,
			"Intra refresh mode CTU step-size requires an argument\n");
		return -EINVAL;
	}

	if (inst->std == W_HEVC_ENC) {
		if (param->nr_intra_weight_y > MAX_INTRA_WEIGHT ||
		    param->nr_intra_weight_cb > MAX_INTRA_WEIGHT ||
		    param->nr_intra_weight_cr > MAX_INTRA_WEIGHT) {
			dev_err(inst->dev->dev,
				"Invalid intra weight Y(%u) Cb(%u) Cr(%u) (valid: %u)\n",
				param->nr_intra_weight_y, param->nr_intra_weight_cb,
				param->nr_intra_weight_cr, MAX_INTRA_WEIGHT);
			return -EINVAL;
		}

		if (param->nr_inter_weight_y > MAX_INTER_WEIGHT ||
		    param->nr_inter_weight_cb > MAX_INTER_WEIGHT ||
		    param->nr_inter_weight_cr > MAX_INTER_WEIGHT) {
			dev_err(inst->dev->dev,
				"Invalid inter weight Y(%u) Cb(%u) Cr(%u) (valid: %u)\n",
				param->nr_inter_weight_y, param->nr_inter_weight_cb,
				param->nr_inter_weight_cr, MAX_INTER_WEIGHT);
			return -EINVAL;
		}
	}

	return 0;
}
