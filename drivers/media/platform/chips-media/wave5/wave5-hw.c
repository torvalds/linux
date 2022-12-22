// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - wave5 backend logic
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */

#include <linux/iopoll.h>
#include "wave5-vpu.h"
#include "wave5.h"
#include "wave5-regdefine.h"

#define FIO_TIMEOUT			10000000
#define FIO_CTRL_READY			BIT(31)
#define FIO_CTRL_WRITE			BIT(16)
#define VPU_BUSY_CHECK_TIMEOUT		10000000
#define QUEUE_REPORT_MASK		0xffff

static void wave5_print_reg_err(struct vpu_device *vpu_dev, u32 reg_fail_reason)
{
	char *caller = __builtin_return_address(0);
	struct device *dev = vpu_dev->dev;
	u32 reg_val;

	switch (reg_fail_reason) {
	case WAVE5_SYSERR_QUEUEING_FAIL:
		reg_val = vpu_read_reg(vpu_dev, W5_RET_QUEUE_FAIL_REASON);
		dev_dbg(dev, "%s: queueing failure: 0x%x\n", caller, reg_val);
		break;
	case WAVE5_SYSERR_RESULT_NOT_READY:
		dev_err(dev, "%s: result not ready: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_ACCESS_VIOLATION_HW:
		dev_err(dev, "%s: access violation: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_WATCHDOG_TIMEOUT:
		dev_err(dev, "%s: watchdog timeout: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_BUS_ERROR:
		dev_err(dev, "%s: bus error: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_DOUBLE_FAULT:
		dev_err(dev, "%s: double fault: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_VPU_STILL_RUNNING:
		dev_err(dev, "%s: still running: 0x%x\n", caller, reg_fail_reason);
		break;
	case WAVE5_SYSERR_VLC_BUF_FULL:
		dev_err(dev, "%s: vlc buf full: 0x%x\n", caller, reg_fail_reason);
		break;
	default:
		dev_err(dev, "%s: failure:: 0x%x\n", caller, reg_fail_reason);
		break;
	}
}

static int wave5_wait_fio_readl(struct vpu_device *vpu_dev, u32 addr, u32 val)
{
	u32 ctrl;
	int ret;

	ctrl = addr & 0xffff;
	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_CTRL_ADDR, ctrl);
	ret = read_poll_timeout(wave5_vdi_readl, ctrl, ctrl & FIO_CTRL_READY,
				0, FIO_TIMEOUT, false, vpu_dev, W5_VPU_FIO_CTRL_ADDR);
	if (ret)
		return ret;
	if (wave5_vdi_readl(vpu_dev, W5_VPU_FIO_DATA) != val)
		return -ETIMEDOUT;
	return 0;
}

static void wave5_fio_writel(struct vpu_device *vpu_dev, unsigned int addr, unsigned int data)
{
	int ret;
	unsigned int ctrl;

	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_DATA, data);
	ctrl = FIO_CTRL_WRITE | (addr & 0xffff);
	wave5_vdi_write_register(vpu_dev, W5_VPU_FIO_CTRL_ADDR, ctrl);
	ret = read_poll_timeout(wave5_vdi_readl, ctrl, ctrl & FIO_CTRL_READY, 0, FIO_TIMEOUT,
				false, vpu_dev, W5_VPU_FIO_CTRL_ADDR);
	if (ret) {
		dev_dbg_ratelimited(vpu_dev->dev, "FIO write timeout: addr=0x%x data=%x\n",
				    ctrl, data);
	}
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

	return read_poll_timeout(wave5_vdi_readl, data, data == 0,
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
	unsigned int product_id = PRODUCT_ID_NONE;
	u32 val;

	val = vpu_read_reg(vpu_dev, W5_PRODUCT_NUMBER);

	switch (val) {
	case WAVE521_CODE:
	case WAVE521C_CODE:
	case WAVE521C_DUAL_CODE:
	case WAVE521E1_CODE:
		product_id = PRODUCT_ID_521;
		break;
	case WAVE511_CODE:
		product_id = PRODUCT_ID_511;
		break;
	case WAVE517_CODE:
	case WAVE537_CODE:
		product_id = PRODUCT_ID_517;
		break;
	default:
		dev_err(vpu_dev->dev, "Invalid product id (%x)\n", val);
		break;
	}
	return product_id;
}

void wave5_bit_issue_command(struct vpu_instance *inst, u32 cmd)
{
	u32 instance_index = inst->id;
	u32 codec_mode = inst->std;

	vpu_write_reg(inst->dev, W5_CMD_INSTANCE_INFO, (codec_mode << 16) |
			(instance_index & 0xffff));
	vpu_write_reg(inst->dev, W5_VPU_BUSY_STATUS, 1);
	vpu_write_reg(inst->dev, W5_COMMAND, cmd);

	dev_dbg(inst->dev->dev, "%s: cmd=0x%x\n", __func__, cmd);

	vpu_write_reg(inst->dev, W5_VPU_HOST_INT_REQ, 1);
}

static int wave5_send_query(struct vpu_instance *inst, enum QUERY_OPT query_opt)
{
	int ret;

	vpu_write_reg(inst->dev, W5_QUERY_OPTION, query_opt);
	vpu_write_reg(inst->dev, W5_VPU_BUSY_STATUS, 1);
	wave5_bit_issue_command(inst, W5_QUERY);

	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_QUERY', timed out opt=0x%x\n", query_opt);
		return ret;
	}

	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS))
		return -EIO;

	return 0;
}

static int setup_wave5_properties(struct device *dev)
{
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	struct vpu_attr *p_attr = &vpu_dev->attr;
	u32 reg_val;
	u8 *str;
	int ret;
	u32 hw_config_def0, hw_config_def1, hw_config_feature, hw_config_rev;

	vpu_write_reg(vpu_dev, W5_QUERY_OPTION, GET_VPU_INFO);
	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	vpu_write_reg(vpu_dev, W5_COMMAND, W5_QUERY);
	vpu_write_reg(vpu_dev, W5_VPU_HOST_INT_REQ, 1);
	ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
	if (ret)
		return ret;

	if (!vpu_read_reg(vpu_dev, W5_RET_SUCCESS))
		return -EIO;

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
	hw_config_rev = vpu_read_reg(vpu_dev, W5_RET_CONF_REVISION);

	p_attr->support_hevc10bit_enc = (hw_config_feature >> 3) & 1;
	if (hw_config_rev > 167455) //20190321
		p_attr->support_avc10bit_enc = (hw_config_feature >> 11) & 1;
	else
		p_attr->support_avc10bit_enc = p_attr->support_hevc10bit_enc;

	p_attr->support_decoders = 0;
	p_attr->support_encoders = 0;
	if (p_attr->product_id == PRODUCT_ID_521) {
		p_attr->support_dual_core = ((hw_config_def1 >> 26) & 0x01);
		if (p_attr->support_dual_core || hw_config_rev < 206116) {
			p_attr->support_decoders = BIT(STD_AVC);
			p_attr->support_decoders |= BIT(STD_HEVC);
			p_attr->support_encoders = BIT(STD_AVC);
			p_attr->support_encoders |= BIT(STD_HEVC);
		} else {
			p_attr->support_decoders |= (((hw_config_def1 >> 3) & 0x01) << STD_AVC);
			p_attr->support_decoders |= (((hw_config_def1 >> 2) & 0x01) << STD_HEVC);
			p_attr->support_encoders = (((hw_config_def1 >> 1) & 0x01) << STD_AVC);
			p_attr->support_encoders |= ((hw_config_def1 & 0x01) << STD_HEVC);
		}
	} else if (p_attr->product_id == PRODUCT_ID_511) {
		p_attr->support_decoders = BIT(STD_HEVC);
		p_attr->support_decoders |= BIT(STD_AVC);
	} else if (p_attr->product_id == PRODUCT_ID_517) {
		p_attr->support_decoders = (((hw_config_def1 >> 4) & 0x01) << STD_AV1);
		p_attr->support_decoders |= (((hw_config_def1 >> 3) & 0x01) << STD_AVS2);
		p_attr->support_decoders |= (((hw_config_def1 >> 2) & 0x01) << STD_AVC);
		p_attr->support_decoders |= (((hw_config_def1 >> 1) & 0x01) << STD_VP9);
		p_attr->support_decoders |= ((hw_config_def1 & 0x01) << STD_HEVC);
	}

	p_attr->support_backbone = (hw_config_def0 >> 16) & 0x01;
	p_attr->support_vcpu_backbone = (hw_config_def0 >> 28) & 0x01;
	p_attr->support_vcore_backbone = (hw_config_def0 >> 22) & 0x01;
	p_attr->support_dual_core = (hw_config_def1 >> 26) & 0x01;
	p_attr->support_endian_mask = BIT(VDI_LITTLE_ENDIAN) |
				      BIT(VDI_BIG_ENDIAN) |
				      BIT(VDI_32BIT_LITTLE_ENDIAN) |
				      BIT(VDI_32BIT_BIG_ENDIAN) |
				      (0xffffUL << 16);
	p_attr->support_bitstream_mode = BIT(BS_MODE_INTERRUPT) |
		BIT(BS_MODE_PIC_END);

	return 0;
}

int wave5_vpu_get_version(struct vpu_device *vpu_dev, u32 *revision)
{
	u32 reg_val;
	int ret;

	vpu_write_reg(vpu_dev, W5_QUERY_OPTION, GET_VPU_INFO);
	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	vpu_write_reg(vpu_dev, W5_COMMAND, W5_QUERY);
	vpu_write_reg(vpu_dev, W5_VPU_HOST_INT_REQ, 1);
	ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_err(vpu_dev->dev, "%s: timeout\n", __func__);
		return ret;
	}

	if (!vpu_read_reg(vpu_dev, W5_RET_SUCCESS)) {
		dev_err(vpu_dev->dev, "%s: failed\n", __func__);
		return -EIO;
	}

	reg_val = vpu_read_reg(vpu_dev, W5_RET_FW_VERSION);
	if (revision)
		*revision = reg_val;

	return 0;
}

static void remap_page(struct vpu_device *vpu_dev, dma_addr_t code_base, u32 index)
{
	u32 remap_size = (W5_REMAP_MAX_SIZE >> 12) & 0x1ff;
	u32 reg_val = 0x80000000 | (WAVE5_UPPER_PROC_AXI_ID << 20) | (index << 12) | BIT(11)
		| remap_size;

	vpu_write_reg(vpu_dev, W5_VPU_REMAP_CTRL, reg_val);
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_VADDR, index * W5_REMAP_MAX_SIZE);
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_PADDR, code_base + index * W5_REMAP_MAX_SIZE);
}

int wave5_vpu_init(struct device *dev, u8 *fw, size_t size)
{
	struct vpu_buf *common_vb;
	struct dma_vpu_buf *sram_vb;
	dma_addr_t code_base, temp_base;
	u32 code_size, temp_size;
	u32 i, reg_val;
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

	ret = wave5_vdi_write_memory(vpu_dev, common_vb, 0, fw, size, VDI_128BIT_LITTLE_ENDIAN);
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

	vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);

	reg_val = (WAVE5_PROC_AXI_EXT_ADDR & 0xFFFF);
	wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, reg_val);
	reg_val = ((WAVE5_PROC_AXI_AXPROT & 0x7) << 4) |
		(WAVE5_PROC_AXI_AXCACHE & 0xF);
	wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, reg_val);
	reg_val = ((WAVE5_SEC_AXI_AXPROT & 0x7) << 20) |
		((WAVE5_SEC_AXI_AXCACHE & 0xF) << 16) |
		(WAVE5_SEC_AXI_EXT_ADDR & 0xFFFF);
	vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, reg_val);

	/* interrupt */
	// encoder
	reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
	reg_val |= BIT(INT_WAVE5_ENC_PIC);
	reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
	// decoder
	reg_val |= BIT(INT_WAVE5_INIT_SEQ);
	reg_val |= BIT(INT_WAVE5_DEC_PIC);
	reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
	vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

	reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
	if ((reg_val >> 16) & 1) {
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

	sram_vb = &vpu_dev->sram_buf;

	vpu_write_reg(vpu_dev, W5_ADDR_SEC_AXI, sram_vb->daddr);
	vpu_write_reg(vpu_dev, W5_SEC_AXI_SIZE, sram_vb->size);
	vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
	vpu_write_reg(vpu_dev, W5_COMMAND, W5_INIT_VPU);
	vpu_write_reg(vpu_dev, W5_VPU_REMAP_CORE_START, 1);
	ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_err(vpu_dev->dev, "VPU init(W5_VPU_REMAP_CORE_START) timeout\n");
		return ret;
	}

	reg_val = vpu_read_reg(vpu_dev, W5_RET_SUCCESS);
	if (!reg_val) {
		u32 reason_code = vpu_read_reg(vpu_dev, W5_RET_FAIL_REASON);

		wave5_print_reg_err(vpu_dev, reason_code);
		return -EIO;
	}

	return setup_wave5_properties(dev);
}

int wave5_vpu_build_up_dec_param(struct vpu_instance *inst,
				 struct dec_open_param *param)
{
	int ret;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	u32 bs_endian;
	struct dma_vpu_buf *sram_vb;
	struct vpu_device *vpu_dev = inst->dev;

	p_dec_info->cycle_per_tick = 256;
	switch (inst->std) {
	case W_HEVC_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_HEVC;
		break;
	case W_VP9_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_VP9;
		break;
	case W_AVS2_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_AVS2;
		break;
	case W_AVC_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_AVC;
		break;
	case W_AV1_DEC:
		p_dec_info->seq_change_mask = SEQ_CHANGE_ENABLE_ALL_AV1;
		break;
	default:
		return -EINVAL;
	}

	if (vpu_dev->product == PRODUCT_ID_517)
		p_dec_info->vb_work.size = WAVE517_WORKBUF_SIZE;
	else if (vpu_dev->product == PRODUCT_ID_521)
		p_dec_info->vb_work.size = WAVE521DEC_WORKBUF_SIZE;
	else if (vpu_dev->product == PRODUCT_ID_511)
		p_dec_info->vb_work.size = WAVE521DEC_WORKBUF_SIZE;

	ret = wave5_vdi_allocate_dma_memory(inst->dev, &p_dec_info->vb_work);
	if (ret)
		return ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_VCORE_INFO, 1);

	sram_vb = &vpu_dev->sram_buf;
	p_dec_info->sec_axi_info.buf_base = sram_vb->daddr;
	p_dec_info->sec_axi_info.buf_size = sram_vb->size;

	wave5_vdi_clear_memory(inst->dev, &p_dec_info->vb_work);

	vpu_write_reg(inst->dev, W5_ADDR_WORK_BASE, p_dec_info->vb_work.daddr);
	vpu_write_reg(inst->dev, W5_WORK_SIZE, p_dec_info->vb_work.size);

	vpu_write_reg(inst->dev, W5_CMD_DEC_BS_START_ADDR, p_dec_info->stream_buf_start_addr);
	vpu_write_reg(inst->dev, W5_CMD_DEC_BS_SIZE, p_dec_info->stream_buf_size);

	/* NOTE: when endian mode is 0, SDMA reads MSB first */
	bs_endian = wave5_vdi_convert_endian(inst->dev, param->stream_endian);
	bs_endian = (~bs_endian & VDI_128BIT_ENDIAN_MASK);
	vpu_write_reg(inst->dev, W5_CMD_BS_PARAM, bs_endian);
	vpu_write_reg(inst->dev, W5_CMD_EXT_ADDR, (param->pri_axprot << 20) |
			(param->pri_axcache << 16) | param->pri_ext_addr);
	vpu_write_reg(inst->dev, W5_CMD_NUM_CQ_DEPTH_M1, (COMMAND_QUEUE_DEPTH - 1));
	vpu_write_reg(inst->dev, W5_CMD_ERR_CONCEAL, (param->error_conceal_unit << 2) |
			(param->error_conceal_mode));

	wave5_bit_issue_command(inst, W5_CREATE_INSTANCE);
	// check QUEUE_DONE
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_CREATE_INSTANCE' timed out\n");
		goto free_vb_work;
	}

	// Check if we were able to add the parameters into the VCPU QUEUE
	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		ret = -EIO;
		goto free_vb_work;
	}

	p_dec_info->product_code = vpu_read_reg(inst->dev, W5_PRODUCT_NUMBER);

	return 0;
free_vb_work:
	wave5_vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_work);
	return ret;
}

int wave5_vpu_dec_init_seq(struct vpu_instance *inst)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	u32 cmd_option = INIT_SEQ_NORMAL;
	u32 reg_val, bs_option;
	int ret;

	if (!inst->codec_info)
		return -EINVAL;

	if (p_dec_info->thumbnail_mode)
		cmd_option = INIT_SEQ_W_THUMBNAIL;

	/* set attributes of bitstream buffer controller */
	switch (p_dec_info->open_param.bitstream_mode) {
	case BS_MODE_INTERRUPT:
		bs_option = BSOPTION_ENABLE_EXPLICIT_END;
		break;
	case BS_MODE_PIC_END:
		bs_option = BSOPTION_ENABLE_EXPLICIT_END;
		break;
	default:
		return -EINVAL;
	}

	vpu_write_reg(inst->dev, W5_BS_RD_PTR, p_dec_info->stream_rd_ptr);
	vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);

	if (p_dec_info->stream_endflag)
		bs_option = 3;
	if (inst->std == W_AV1_DEC)
		bs_option |= ((p_dec_info->open_param.av1_format) << 2);
	vpu_write_reg(inst->dev, W5_BS_OPTION, BIT(31) | bs_option);

	vpu_write_reg(inst->dev, W5_COMMAND_OPTION, cmd_option);
	vpu_write_reg(inst->dev, W5_CMD_DEC_USER_MASK, p_dec_info->user_data_enable);

	wave5_bit_issue_command(inst, W5_INIT_SEQ);

	// check QUEUE_DONE
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_INIT_SEQ', timed out\n");
		return ret;
	}

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	// Check if we were able to add a command into VCPU QUEUE
	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, reg_val);
		return -EIO;
	}

	return 0;
}

static void wave5_get_dec_seq_result(struct vpu_instance *inst, struct dec_initial_info *info)
{
	u32 reg_val, sub_layer_info;
	u32 profile_compatibility_flag;
	u32 output_bit_depth_minus8;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;

	p_dec_info->stream_rd_ptr = wave5_vpu_dec_get_rd_ptr(inst);
	info->rd_ptr = p_dec_info->stream_rd_ptr;

	p_dec_info->frame_display_flag = vpu_read_reg(inst->dev, W5_RET_DEC_DISP_IDC);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_SIZE);
	info->pic_width = ((reg_val >> 16) & 0xffff);
	info->pic_height = (reg_val & 0xffff);
	dev_dbg(inst->dev->dev,  "%s info->pic_width %d info->pic_height %d\n",__func__, info->pic_width, info->pic_height);
	info->min_frame_buffer_count = vpu_read_reg(inst->dev, W5_RET_DEC_NUM_REQUIRED_FB);
	info->frame_buf_delay = vpu_read_reg(inst->dev, W5_RET_DEC_NUM_REORDER_DELAY);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_CROP_LEFT_RIGHT);
	info->pic_crop_rect.left = (reg_val >> 16) & 0xffff;
	info->pic_crop_rect.right = reg_val & 0xffff;
	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_CROP_TOP_BOTTOM);
	info->pic_crop_rect.top = (reg_val >> 16) & 0xffff;
	info->pic_crop_rect.bottom = reg_val & 0xffff;
	dev_dbg(inst->dev->dev, "%s pic_crop_rect.left %d pic_crop_rect.right %d pic_crop_rect.top %d pic_crop_rect.bottom %d\n",__func__,
				info->pic_crop_rect.left, info->pic_crop_rect.right, info->pic_crop_rect.top, info->pic_crop_rect.bottom);

	info->f_rate_numerator = vpu_read_reg(inst->dev, W5_RET_DEC_FRAME_RATE_NR);
	info->f_rate_denominator = vpu_read_reg(inst->dev, W5_RET_DEC_FRAME_RATE_DR);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_COLOR_SAMPLE_INFO);
	info->luma_bitdepth = reg_val & 0xf;
	info->chroma_bitdepth = (reg_val >> 4) & 0xf;
	info->chroma_format_idc = (reg_val >> 8) & 0xf;
	info->aspect_rate_info = (reg_val >> 16) & 0xff;
	info->is_ext_sar = ((info->aspect_rate_info == 255) ? true : false);
	/* [0:15] - vertical size, [16:31] - horizontal size */
	if (info->is_ext_sar)
		info->aspect_rate_info = vpu_read_reg(inst->dev, W5_RET_DEC_ASPECT_RATIO);
	info->bit_rate = vpu_read_reg(inst->dev, W5_RET_DEC_BIT_RATE);

	sub_layer_info = vpu_read_reg(inst->dev, W5_RET_DEC_SUB_LAYER_INFO);
	info->max_temporal_layers = (sub_layer_info >> 8) & 0x7;

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_SEQ_PARAM);
	info->level = reg_val & 0xff;
	profile_compatibility_flag = (reg_val >> 12) & 0xff;
	info->profile = (reg_val >> 24) & 0x1f;
	info->tier = (reg_val >> 29) & 0x01;
	output_bit_depth_minus8 = (reg_val >> 30) & 0x03;

	if (inst->std == W_HEVC_DEC) {
		/* guessing profile */
		if (!info->profile) {
			if ((profile_compatibility_flag & 0x06) == 0x06)
				info->profile = HEVC_PROFILE_MAIN; /* main profile */
			else if ((profile_compatibility_flag & 0x04) == 0x04)
				info->profile = HEVC_PROFILE_MAIN10; /* main10 profile */
			else if ((profile_compatibility_flag & 0x08) == 0x08)
				/* main still picture profile */
				info->profile = HEVC_PROFILE_STILLPICTURE;
			else
				info->profile = HEVC_PROFILE_MAIN; /* for old version HM */
		}

	} else if (inst->std == W_AVS2_DEC) {
		if (info->luma_bitdepth == 10 && output_bit_depth_minus8 == 2)
			info->output_bit_depth = 10;
		else
			info->output_bit_depth = 8;

	} else if (inst->std == W_AVC_DEC) {
		info->profile = (reg_val >> 24) & 0x7f;
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
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_PARAM,
		      VPU_USER_DATA_ENDIAN & VDI_128BIT_ENDIAN_MASK);

	// send QUERY cmd
	ret = wave5_send_query(inst, GET_RESULT);
	if (ret) {
		if (ret == -EIO) {
			reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
			wave5_print_reg_err(inst->dev, reg_val);
		}
		return ret;
	}

	dev_dbg(inst->dev->dev, "%s: init seq complete\n", __func__);

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	/* this is not a fatal error, set ret to -EIO but don't return immediately */
	if (vpu_read_reg(inst->dev, W5_RET_DEC_DECODING_SUCCESS) != 1) {
		info->seq_init_err_reason = vpu_read_reg(inst->dev, W5_RET_DEC_ERR_INFO);
		ret = -EIO;
	} else {
		info->warn_info = vpu_read_reg(inst->dev, W5_RET_DEC_WARN_INFO);
	}

	// get sequence info
	info->user_data_size = 0;
	info->user_data_buf_full = false;
	info->user_data_header = vpu_read_reg(inst->dev, W5_RET_DEC_USERDATA_IDC);
	if (info->user_data_header) {
		if (info->user_data_header & BIT(USERDATA_FLAG_BUFF_FULL))
			info->user_data_buf_full = true;
		info->user_data_size = p_dec_info->user_data_buf_size;
	}

	wave5_get_dec_seq_result(inst, info);

	return ret;
}

static u32 calculate_table_size(u32 bit_depth, u32 frame_width, u32 frame_height, u32 ot_bg_width)
{
	u32 bgs_width = ((bit_depth > 8) ? 256 : 512);
	u32 comp_frame_width = ALIGN(ALIGN(frame_width, 16) + 16, 16);
	u32 ot_frame_width = ALIGN(comp_frame_width, ot_bg_width);

	// sizeof_offset_table()
	u32 ot_bg_height = 32;
	u32 bgs_height = BIT(14) / bgs_width / ((bit_depth > 8) ? 2 : 1);
	u32 comp_frame_height = ALIGN(ALIGN(frame_height, 4) + 4, bgs_height);
	u32 ot_frame_height = ALIGN(comp_frame_height, ot_bg_height);

	return (ot_frame_width / 16) * (ot_frame_height / 4) * 2;
}

int wave5_vpu_dec_register_framebuffer(struct vpu_instance *inst, struct frame_buffer *fb_arr,
				       enum tiled_map_type map_type, unsigned int count)
{
	int ret;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct dec_initial_info *init_info = &p_dec_info->initial_info;
	size_t remain, idx, j, i, cnt_8_chunk;
	u32 start_no, end_no;
	u32 reg_val, cbcr_interleave, nv21, pic_size;
	u32 endian, yuv_format;
	u32 addr_y, addr_cb, addr_cr;
	u32 table_width = init_info->pic_width;
	u32 table_height = init_info->pic_height;
	u32 mv_col_size, frame_width, frame_height, fbc_y_tbl_size, fbc_c_tbl_size;
	struct vpu_buf vb_buf;
	u32 color_format = 0;
	u32 pixel_order = 1;
	u32 scale_en = 0;
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
		case W_VP9_DEC:
			mv_col_size = WAVE5_DEC_VP9_BUF_SIZE(init_info->pic_width,
							     init_info->pic_height);
			table_width = ALIGN(table_width, 64);
			table_height = ALIGN(table_height, 64);
			break;
		case W_AVS2_DEC:
			mv_col_size = WAVE5_DEC_AVS2_BUF_SIZE(init_info->pic_width,
							      init_info->pic_height);
			break;
		case W_AVC_DEC:
			mv_col_size = WAVE5_DEC_AVC_BUF_SIZE(init_info->pic_width,
							     init_info->pic_height);
			break;
		case W_AV1_DEC:
			mv_col_size = WAVE5_DEC_AV1_BUF_SZ_1(init_info->pic_width,
							     init_info->pic_height) +
				WAVE5_DEC_AV1_BUF_SZ_2(init_info->pic_width, init_info->pic_width,
						       init_info->pic_height);
			table_width = ALIGN(table_width, 16);
			table_height = ALIGN(table_height, 8);
			break;
		default:
			return -EINVAL;
		}

		mv_col_size = ALIGN(mv_col_size, 16);
		vb_buf.daddr = 0;
		if (inst->std == W_HEVC_DEC || inst->std == W_AVS2_DEC || inst->std ==
				W_VP9_DEC || inst->std == W_AVC_DEC || inst->std ==
				W_AV1_DEC) {
			vb_buf.size = ALIGN(mv_col_size, BUFFER_MARGIN) + BUFFER_MARGIN;

			for (i = 0 ; i < count ; i++) {
				if (p_dec_info->vb_mv[i].size == 0) {
					ret = wave5_vdi_allocate_dma_memory(inst->dev, &vb_buf);
					if (ret)
						goto free_mv_buffers;
					p_dec_info->vb_mv[i] = vb_buf;
				}
			}
		}

		frame_width = ALIGN(init_info->pic_width, 16);
		frame_height = ALIGN(init_info->pic_height, 16);
		if (p_dec_info->product_code == WAVE521C_DUAL_CODE) {
			// Use a offset table BG width of 1024 for all decoders
			fbc_y_tbl_size = calculate_table_size(init_info->luma_bitdepth,
							      frame_width, frame_height, 1024);
		} else {
			fbc_y_tbl_size = ALIGN(WAVE5_FBC_LUMA_TABLE_SIZE(table_width,
									 table_height), 16);
		}

		vb_buf.daddr = 0;
		vb_buf.size = ALIGN(fbc_y_tbl_size, BUFFER_MARGIN) + BUFFER_MARGIN;
		for (i = 0 ; i < count ; i++) {
			if (p_dec_info->vb_fbc_y_tbl[i].size == 0) {
				ret = wave5_vdi_allocate_dma_memory(inst->dev, &vb_buf);
				if (ret)
					goto free_fbc_y_tbl_buffers;
				p_dec_info->vb_fbc_y_tbl[i] = vb_buf;
			}
		}

		if (p_dec_info->product_code == WAVE521C_DUAL_CODE) {
			// Use a offset table BG width of 1024 for all decoders
			fbc_c_tbl_size = calculate_table_size(init_info->chroma_bitdepth,
							      frame_width / 2, frame_height, 1024);
		} else {
			fbc_c_tbl_size = ALIGN(WAVE5_FBC_CHROMA_TABLE_SIZE(table_width,
									   table_height), 16);
		}

		vb_buf.daddr = 0;
		vb_buf.size = ALIGN(fbc_c_tbl_size, BUFFER_MARGIN) + BUFFER_MARGIN;
		for (i = 0 ; i < count ; i++) {
			if (p_dec_info->vb_fbc_c_tbl[i].size == 0) {
				ret = wave5_vdi_allocate_dma_memory(inst->dev, &vb_buf);
				if (ret)
					goto free_fbc_c_tbl_buffers;
				p_dec_info->vb_fbc_c_tbl[i] = vb_buf;
			}
		}
		pic_size = (inst->display_fmt.width << 16) | (inst->display_fmt.height);
		if (init_info->pic_width != inst->display_fmt.width ||
				init_info->pic_height != inst->display_fmt.height)
			scale_en = 1;

		// allocate task_buffer
		vb_buf.size = (p_dec_info->vlc_buf_size * VLC_BUF_NUM) +
				(p_dec_info->param_buf_size * COMMAND_QUEUE_DEPTH);
		vb_buf.daddr = 0;
		ret = wave5_vdi_allocate_dma_memory(inst->dev, &vb_buf);
		if (ret)
			goto free_fbc_c_tbl_buffers;

		p_dec_info->vb_task = vb_buf;

		vpu_write_reg(inst->dev, W5_CMD_SET_FB_ADDR_TASK_BUF,
			      p_dec_info->vb_task.daddr);
		vpu_write_reg(inst->dev, W5_CMD_SET_FB_TASK_BUF_SIZE, vb_buf.size);
	} else {
		pic_size = (inst->display_fmt.width << 16) | (inst->display_fmt.height);
		if (init_info->pic_width != inst->display_fmt.width ||
				init_info->pic_height != inst->display_fmt.height)
			scale_en = 1;
	}
	dev_dbg(inst->dev->dev, "set pic_size 0x%x\n", pic_size);
	endian = wave5_vdi_convert_endian(inst->dev, fb_arr[0].endian);
	vpu_write_reg(inst->dev, W5_PIC_SIZE, pic_size);

	yuv_format = 0;
	color_format = 0;

	reg_val =
		(scale_en << 29) |
		(bwb_flag << 28) |
		(pixel_order << 23) | /* PIXEL ORDER in 128bit. first pixel in low address */
		(yuv_format << 20) |
		(color_format << 19) |
		(nv21 << 17) |
		(cbcr_interleave << 16) |
		inst->display_fmt.width;
	dev_dbg(inst->dev->dev, "set W5_COMMON_PIC_INFO 0x%x\n",reg_val);
	vpu_write_reg(inst->dev, W5_COMMON_PIC_INFO, reg_val);

	remain = count;
	cnt_8_chunk = ALIGN(count, 8) / 8;
	idx = 0;
	for (j = 0; j < cnt_8_chunk; j++) {
		reg_val = (endian << 16) | (j == cnt_8_chunk - 1) << 4 | ((j == 0) << 3);
		reg_val |= (p_dec_info->open_param.enable_non_ref_fbc_write << 26);
		vpu_write_reg(inst->dev, W5_SFB_OPTION, reg_val);
		start_no = j * 8;
		end_no = start_no + ((remain >= 8) ? 8 : remain) - 1;

		vpu_write_reg(inst->dev, W5_SET_FB_NUM, (start_no << 8) | end_no);

		for (i = 0; i < 8 && i < remain; i++) {
			if (map_type == LINEAR_FRAME_MAP && p_dec_info->open_param.cbcr_order ==
					CBCR_ORDER_REVERSED) {
				addr_y = fb_arr[i + start_no].buf_y;
				addr_cb = fb_arr[i + start_no].buf_cr;
				addr_cr = fb_arr[i + start_no].buf_cb;
			} else {
				addr_y = fb_arr[i + start_no].buf_y;
				addr_cb = fb_arr[i + start_no].buf_cb;
				addr_cr = fb_arr[i + start_no].buf_cr;
			}
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

		wave5_bit_issue_command(inst, W5_SET_FB);
		ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
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

int wave5_vpu_decode(struct vpu_instance *inst, struct dec_param *option, u32 *fail_res)
{
	u32 mode_option = DEC_PIC_NORMAL, bs_option, reg_val;
	u32 force_latency = 0;
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct dec_open_param *p_open_param = &p_dec_info->open_param;
	int ret;

	if (p_dec_info->thumbnail_mode) {
		mode_option = DEC_PIC_W_THUMBNAIL;
	} else if (option->skipframe_mode) {
		switch (option->skipframe_mode) {
		case WAVE_SKIPMODE_NON_IRAP:
			mode_option = SKIP_NON_IRAP;
			force_latency = 1;
			break;
		case WAVE_SKIPMODE_NON_REF:
			mode_option = SKIP_NON_REF_PIC;
			break;
		default:
			// skip mode off
			break;
		}
	}

	// set disable reorder
	if (!p_dec_info->reorder_enable)
		force_latency = 1;

	/* set attributes of bitstream buffer controller */
	bs_option = 0;
	switch (p_open_param->bitstream_mode) {
	case BS_MODE_INTERRUPT:
		bs_option = BSOPTION_ENABLE_EXPLICIT_END;
		break;
	case BS_MODE_PIC_END:
		bs_option = BSOPTION_ENABLE_EXPLICIT_END;
		break;
	default:
		return -EINVAL;
	}

	vpu_write_reg(inst->dev, W5_BS_RD_PTR, p_dec_info->stream_rd_ptr);
	vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);
	bs_option = (p_dec_info->stream_endflag << 1) | BS_EXPLICIT_END_MODE_ON;
	if (p_open_param->bitstream_mode == BS_MODE_PIC_END)
		bs_option |= BIT(31);
	if (inst->std == W_AV1_DEC)
		bs_option |= ((p_open_param->av1_format) << 2);
	vpu_write_reg(inst->dev, W5_BS_OPTION, bs_option);

	/* secondary AXI */
	reg_val = p_dec_info->sec_axi_info.wave.use_bit_enable |
		(p_dec_info->sec_axi_info.wave.use_ip_enable << 9) |
		(p_dec_info->sec_axi_info.wave.use_lf_row_enable << 15);
	vpu_write_reg(inst->dev, W5_USE_SEC_AXI, reg_val);

	/* set attributes of user buffer */
	vpu_write_reg(inst->dev, W5_CMD_DEC_USER_MASK, p_dec_info->user_data_enable);

	vpu_write_reg(inst->dev, W5_COMMAND_OPTION,
		      ((option->disable_film_grain << 6) | (option->cra_as_bla_flag << 5) |
		      mode_option));
	vpu_write_reg(inst->dev, W5_CMD_DEC_TEMPORAL_ID_PLUS1,
		      (p_dec_info->target_spatial_id << 9) |
		      (p_dec_info->temp_id_select_mode << 8) | p_dec_info->target_temp_id);
	vpu_write_reg(inst->dev, W5_CMD_SEQ_CHANGE_ENABLE_FLAG, p_dec_info->seq_change_mask);
	vpu_write_reg(inst->dev, W5_CMD_DEC_FORCE_FB_LATENCY_PLUS1, force_latency);

	wave5_bit_issue_command(inst, W5_DEC_PIC);
	// check QUEUE_DONE
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_DEC_PIC', timed out\n");
		return -ETIMEDOUT;
	}

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);
	// Check if we were able to add a command into the VCPU QUEUE
	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		*fail_res = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, *fail_res);
		return -EIO;
	}

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
	vpu_write_reg(inst->dev, W5_CMD_DEC_REPORT_PARAM,
		      VPU_USER_DATA_ENDIAN & VDI_128BIT_ENDIAN_MASK);

	// send QUERY cmd
	ret = wave5_send_query(inst, GET_RESULT);
	if (ret) {
		if (ret == -EIO) {
			reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
			wave5_print_reg_err(inst->dev, reg_val);
		}

		return ret;
	}

	dev_dbg(inst->dev->dev, "%s: dec pic complete\n", __func__);

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_dec_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_dec_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	reg_val = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_TYPE);

	nal_unit_type = (reg_val >> 4) & 0x3f;

	if (inst->std == W_VP9_DEC) {
		if (reg_val & 0x01)
			result->pic_type = PIC_TYPE_I;
		else if (reg_val & 0x02)
			result->pic_type = PIC_TYPE_P;
		else if (reg_val & 0x04)
			result->pic_type = PIC_TYPE_REPEAT;
		else
			result->pic_type = PIC_TYPE_MAX;
	} else if (inst->std == W_HEVC_DEC) {
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
	} else if (inst->std == W_AV1_DEC) {
		switch (reg_val & 0x07) {
		case 0:
			result->pic_type = PIC_TYPE_KEY;
			break;
		case 1:
			result->pic_type = PIC_TYPE_INTER;
			break;
		case 2:
			result->pic_type = PIC_TYPE_AV1_INTRA;
			break;
		case 3:
			result->pic_type = PIC_TYPE_AV1_SWITCH;
			break;
		default:
			result->pic_type = PIC_TYPE_MAX;
			break;
		}
	} else { // AVS2
		switch (reg_val & 0x07) {
		case 0:
			result->pic_type = PIC_TYPE_I;
			break;
		case 1:
			result->pic_type = PIC_TYPE_P;
			break;
		case 2:
			result->pic_type = PIC_TYPE_B;
			break;
		case 3:
			result->pic_type = PIC_TYPE_AVS2_F;
			break;
		case 4:
			result->pic_type = PIC_TYPE_AVS2_S;
			break;
		case 5:
			result->pic_type = PIC_TYPE_AVS2_G;
			break;
		case 6:
			result->pic_type = PIC_TYPE_AVS2_GB;
			break;
		default:
			result->pic_type = PIC_TYPE_MAX;
			break;
		}
	}
	index = vpu_read_reg(inst->dev, W5_RET_DEC_DISPLAY_INDEX);
	result->index_frame_display = index;
	index = vpu_read_reg(inst->dev, W5_RET_DEC_DECODED_INDEX);
	result->index_frame_decoded = index;
	result->index_frame_decoded_for_tiled = index;

	sub_layer_info = vpu_read_reg(inst->dev, W5_RET_DEC_SUB_LAYER_INFO);
	result->temporal_id = sub_layer_info & 0x7;

	if (inst->std == W_HEVC_DEC) {
		result->decoded_poc = -1;
		if (result->index_frame_decoded >= 0 ||
		    result->index_frame_decoded == DECODED_IDX_FLAG_SKIP)
			result->decoded_poc = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_POC);
	} else if (inst->std == W_AVS2_DEC) {
		result->avs2_info.decoded_poi = -1;
		result->avs2_info.display_poi = -1;
		if (result->index_frame_decoded >= 0)
			result->avs2_info.decoded_poi =
				vpu_read_reg(inst->dev, W5_RET_DEC_PIC_POC);
	} else if (inst->std == W_AVC_DEC) {
		result->decoded_poc = -1;
		if (result->index_frame_decoded >= 0 ||
		    result->index_frame_decoded == DECODED_IDX_FLAG_SKIP)
			result->decoded_poc = vpu_read_reg(inst->dev, W5_RET_DEC_PIC_POC);
	} else if (inst->std == W_AV1_DEC) {
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

	// no remaining command. reset frame cycle.
	if (p_dec_info->instance_queue_count == 0 && p_dec_info->report_queue_count == 0)
		p_dec_info->first_cycle_check = false;

	return 0;
}

int wave5_vpu_re_init(struct device *dev, u8 *fw, size_t size)
{
	struct vpu_buf *common_vb;
	dma_addr_t code_base, temp_base;
	dma_addr_t old_code_base, temp_size;
	u32 code_size;
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
		struct dma_vpu_buf *sram_vb;

		ret = wave5_vdi_write_memory(vpu_dev, common_vb, 0, fw, size,
					     VDI_128BIT_LITTLE_ENDIAN);
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

		vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);

		reg_val = (WAVE5_PROC_AXI_EXT_ADDR & 0xFFFF);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, reg_val);
		reg_val = ((WAVE5_PROC_AXI_AXPROT & 0x7) << 4) |
			(WAVE5_PROC_AXI_AXCACHE & 0xF);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, reg_val);
		reg_val = ((WAVE5_SEC_AXI_AXPROT & 0x7) << 20) |
			((WAVE5_SEC_AXI_AXCACHE & 0xF) << 16) |
			(WAVE5_SEC_AXI_EXT_ADDR & 0xFFFF);
		vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, reg_val);

		/* interrupt */
		// encoder
		reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
		reg_val |= BIT(INT_WAVE5_ENC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
		// decoder
		reg_val |= BIT(INT_WAVE5_INIT_SEQ);
		reg_val |= BIT(INT_WAVE5_DEC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
		vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

		reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
		if ((reg_val >> 16) & 1) {
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

		sram_vb = &vpu_dev->sram_buf;

		vpu_write_reg(vpu_dev, W5_ADDR_SEC_AXI, sram_vb->daddr);
		vpu_write_reg(vpu_dev, W5_SEC_AXI_SIZE, sram_vb->size);
		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
		vpu_write_reg(vpu_dev, W5_COMMAND, W5_INIT_VPU);
		vpu_write_reg(vpu_dev, W5_VPU_REMAP_CORE_START, 1);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret) {
			dev_err(vpu_dev->dev, "VPU reinit(W5_VPU_REMAP_CORE_START) timeout\n");
			return ret;
		}

		reg_val = vpu_read_reg(vpu_dev, W5_RET_SUCCESS);
		if (!reg_val) {
			u32 reason_code = vpu_read_reg(vpu_dev, W5_RET_FAIL_REASON);

			wave5_print_reg_err(vpu_dev, reason_code);
			return -EIO;
		}
	}

	return setup_wave5_properties(dev);
}

static int wave5_vpu_sleep_wake(struct device *dev, bool i_sleep_wake, const uint16_t *code,
				size_t size)
{
	u32 reg_val;
	struct vpu_buf *common_vb;
	dma_addr_t code_base;
	u32 code_size;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	int ret;

	if (i_sleep_wake) {
		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret)
			return ret;

		/*
		 * Declare who has ownership for the host interface access
		 * 1 = VPU
		 * 0 = Host processer
		 */
		vpu_write_reg(vpu_dev, W5_VPU_BUSY_STATUS, 1);
		vpu_write_reg(vpu_dev, W5_COMMAND, W5_SLEEP_VPU);
		/* Send an interrupt named HOST to the VPU */
		vpu_write_reg(vpu_dev, W5_VPU_HOST_INT_REQ, 1);

		ret = wave5_wait_vpu_busy(vpu_dev, W5_VPU_BUSY_STATUS);
		if (ret)
			return ret;

		if (!vpu_read_reg(vpu_dev, W5_RET_SUCCESS)) {
			u32 reason = vpu_read_reg(vpu_dev, W5_RET_FAIL_REASON);

			wave5_print_reg_err(vpu_dev, reason);
			return -EIO;
		}
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

		vpu_write_reg(vpu_dev, W5_HW_OPTION, 0);

		reg_val = (WAVE5_PROC_AXI_EXT_ADDR & 0xFFFF);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_PROC_EXT_ADDR, reg_val);
		reg_val = ((WAVE5_PROC_AXI_AXPROT & 0x7) << 4) |
			(WAVE5_PROC_AXI_AXCACHE & 0xF);
		wave5_fio_writel(vpu_dev, W5_BACKBONE_AXI_PARAM, reg_val);
		reg_val = ((WAVE5_SEC_AXI_AXPROT & 0x7) << 20) |
			((WAVE5_SEC_AXI_AXCACHE & 0xF) << 16) |
			(WAVE5_SEC_AXI_EXT_ADDR & 0xFFFF);
		vpu_write_reg(vpu_dev, W5_SEC_AXI_PARAM, reg_val);

		/* interrupt */
		// encoder
		reg_val = BIT(INT_WAVE5_ENC_SET_PARAM);
		reg_val |= BIT(INT_WAVE5_ENC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_FULL);
		// decoder
		reg_val |= BIT(INT_WAVE5_INIT_SEQ);
		reg_val |= BIT(INT_WAVE5_DEC_PIC);
		reg_val |= BIT(INT_WAVE5_BSBUF_EMPTY);
		vpu_write_reg(vpu_dev, W5_VPU_VINT_ENABLE, reg_val);

		reg_val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG0);
		if ((reg_val >> 16) & 1) {
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

		reg_val = vpu_read_reg(vpu_dev, W5_RET_SUCCESS);
		if (!reg_val) {
			u32 reason_code = vpu_read_reg(vpu_dev, W5_RET_FAIL_REASON);

			wave5_print_reg_err(vpu_dev, reason_code);
			return -EIO;
		}
	}

	return 0;
}

int wave5_vpu_reset(struct device *dev, enum sw_reset_mode reset_mode)
{
	u32 val = 0;
	int ret = 0;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	struct vpu_attr *p_attr = &vpu_dev->attr;
	// VPU doesn't send response. force to set BUSY flag to 0.
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

	val = vpu_read_reg(vpu_dev, W5_VPU_RET_VPU_CONFIG1);
	if ((val >> 26) & 0x1)
		p_attr->support_dual_core = true;

	// waiting for completion of bus transaction
	if (p_attr->support_backbone) {
		dev_dbg(dev, "%s: backbone supported\n", __func__);

		if (p_attr->support_dual_core) {
			// check CORE0
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x7);

			ret = wave5_wait_bus_busy(vpu_dev, W5_BACKBONE_BUS_STATUS_VCORE0);
			if (ret) {
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x00);
				return ret;
			}

			// check CORE1
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE1, 0x7);

			ret = wave5_wait_bus_busy(vpu_dev, W5_BACKBONE_BUS_STATUS_VCORE1);
			if (ret) {
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE1, 0x00);
				return ret;
			}

		} else if (p_attr->support_vcore_backbone) {
			if (p_attr->support_vcpu_backbone) {
				// step1 : disable request
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCPU, 0xFF);

				// step2 : waiting for completion of bus transaction
				ret = wave5_wait_vcpu_bus_busy(vpu_dev,
							       W5_BACKBONE_BUS_STATUS_VCPU);
				if (ret) {
					wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCPU, 0x00);
					return ret;
				}
			}
			// step1 : disable request
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x7);

			// step2 : waiting for completion of bus transaction
			if (wave5_wait_bus_busy(vpu_dev, W5_BACKBONE_BUS_STATUS_VCORE0)) {
				wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x00);
				return -EBUSY;
			}
		} else {
			// step1 : disable request
			wave5_fio_writel(vpu_dev, W5_COMBINED_BACKBONE_BUS_CTRL, 0x7);

			// step2 : waiting for completion of bus transaction
			if (wave5_wait_bus_busy(vpu_dev, W5_COMBINED_BACKBONE_BUS_STATUS)) {
				wave5_fio_writel(vpu_dev, W5_COMBINED_BACKBONE_BUS_CTRL, 0x00);
				return -EBUSY;
			}
		}
	} else {
		dev_dbg(dev, "%s: backbone NOT supported\n", __func__);
		// step1 : disable request
		wave5_fio_writel(vpu_dev, W5_GDI_BUS_CTRL, 0x100);

		// step2 : waiting for completion of bus transaction
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
	// step3 : must clear GDI_BUS_CTRL after done SW_RESET
	if (p_attr->support_backbone) {
		if (p_attr->support_dual_core) {
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE0, 0x00);
			wave5_fio_writel(vpu_dev, W5_BACKBONE_BUS_CTRL_VCORE1, 0x00);
		} else if (p_attr->support_vcore_backbone) {
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
	int ret;

	wave5_bit_issue_command(inst, W5_DESTROY_INSTANCE);
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret)
		return -ETIMEDOUT;

	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		*fail_res = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, *fail_res);
		return -EIO;
	}

	return 0;
}

int wave5_vpu_dec_set_bitstream_flag(struct vpu_instance *inst, bool eos)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	enum bit_stream_mode bs_mode = (enum bit_stream_mode)p_dec_info->open_param.bitstream_mode;

	p_dec_info->stream_endflag = eos ? 1 : 0;

	if (bs_mode == BS_MODE_INTERRUPT) {
		int ret;

		vpu_write_reg(inst->dev, W5_BS_OPTION, (p_dec_info->stream_endflag << 1) |
			      p_dec_info->stream_endflag);
		vpu_write_reg(inst->dev, W5_BS_WR_PTR, p_dec_info->stream_wr_ptr);

		wave5_bit_issue_command(inst, W5_UPDATE_BS);
		ret = wave5_wait_vpu_busy(inst->dev,
					  W5_VPU_BUSY_STATUS);
		if (ret)
			return ret;

		if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS))
			return -EIO;
	}

	return 0;
}

int wave5_dec_clr_disp_flag(struct vpu_instance *inst, unsigned int index)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_CLR_DISP_IDC, BIT(index));
	vpu_write_reg(inst->dev, W5_CMD_DEC_SET_DISP_IDC, 0);
	ret = wave5_send_query(inst, UPDATE_DISP_FLAG);

	if (ret) {
		if (ret == -EIO) {
			u32 reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);

			wave5_print_reg_err(inst->dev, reg_val);
		}
		return ret;
	}

	p_dec_info->frame_display_flag = vpu_read_reg(inst->dev, W5_RET_DEC_DISP_IDC);

	return 0;
}

int wave5_dec_set_disp_flag(struct vpu_instance *inst, unsigned int index)
{
	int ret;

	vpu_write_reg(inst->dev, W5_CMD_DEC_CLR_DISP_IDC, 0);
	vpu_write_reg(inst->dev, W5_CMD_DEC_SET_DISP_IDC, BIT(index));
	ret = wave5_send_query(inst, UPDATE_DISP_FLAG);

	if (ret) {
		if (ret == -EIO) {
			u32 reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);

			wave5_print_reg_err(inst->dev, reg_val);
		}
		return ret;
	}

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

dma_addr_t wave5_vpu_dec_get_rd_ptr(struct vpu_instance *inst)
{
	int ret;

	ret = wave5_send_query(inst, GET_BS_RD_PTR);

	if (ret)
		return inst->codec_info->dec_info.stream_rd_ptr;

	return vpu_read_reg(inst->dev, W5_RET_QUERY_DEC_BS_RD_PTR);
}

int wave5_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr)
{
	int ret;

	vpu_write_reg(inst->dev, W5_RET_QUERY_DEC_SET_BS_RD_PTR, addr);

	ret = wave5_send_query(inst, SET_BS_RD_PTR);

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
	struct dma_vpu_buf *sram_vb;
	u32 bs_endian;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);
	dma_addr_t buffer_addr;
	size_t buffer_size;

	p_enc_info->cycle_per_tick = 256;
	sram_vb = &vpu_dev->sram_buf;
	p_enc_info->sec_axi_info.buf_base = sram_vb->daddr;
	p_enc_info->sec_axi_info.buf_size = sram_vb->size;

	if (vpu_dev->product == PRODUCT_ID_521)
		p_enc_info->vb_work.size = WAVE521ENC_WORKBUF_SIZE;

	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &p_enc_info->vb_work);
	if (ret) {
		memset(&p_enc_info->vb_work, 0, sizeof(p_enc_info->vb_work));
		return ret;
	}

	wave5_vdi_clear_memory(vpu_dev, &p_enc_info->vb_work);

	vpu_write_reg(inst->dev, W5_ADDR_WORK_BASE, p_enc_info->vb_work.daddr);
	vpu_write_reg(inst->dev, W5_WORK_SIZE, p_enc_info->vb_work.size);

	reg_val = wave5_vdi_convert_endian(vpu_dev, open_param->stream_endian);
	bs_endian = (~reg_val & VDI_128BIT_ENDIAN_MASK);

	reg_val = (open_param->line_buf_int_en << 6) | bs_endian;
	vpu_write_reg(inst->dev, W5_CMD_BS_PARAM, reg_val);
	vpu_write_reg(inst->dev, W5_CMD_EXT_ADDR, (open_param->pri_axprot << 20) |
			(open_param->pri_axcache << 16) | open_param->pri_ext_addr);
	vpu_write_reg(inst->dev, W5_CMD_NUM_CQ_DEPTH_M1, (COMMAND_QUEUE_DEPTH - 1));

	reg_val = 0;
	if (vpu_dev->product == PRODUCT_ID_521)
		reg_val |= (open_param->sub_frame_sync_enable |
			    open_param->sub_frame_sync_mode << 1);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SRC_OPTIONS, reg_val);

	vpu_write_reg(inst->dev, W5_CMD_ENC_VCORE_INFO, 1);

	wave5_bit_issue_command(inst, W5_CREATE_INSTANCE);
	// check QUEUE_DONE
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_CREATE_INSTANCE' timed out\n");
		goto free_vb_work;
	}

	// Check if we were able to add the parameters into the VCPU QUEUE
	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, reg_val);
		ret = -EIO;
		goto free_vb_work;
	}

	buffer_addr = open_param->bitstream_buffer;
	buffer_size = open_param->bitstream_buffer_size;
	p_enc_info->sub_frame_sync_config.sub_frame_sync_mode = open_param->sub_frame_sync_mode;
	p_enc_info->sub_frame_sync_config.sub_frame_sync_on = open_param->sub_frame_sync_enable;
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
	wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_work);
	return ret;
}

static void wave5_set_enc_crop_info(u32 codec, struct enc_wave_param *param, int rot_mode,
				    int src_width, int src_height)
{
	int aligned_width = (codec == W_HEVC_ENC) ? ALIGN(src_width, 32) : ALIGN(src_width, 16);
	int aligned_height = (codec == W_HEVC_ENC) ? ALIGN(src_height, 32) : ALIGN(src_height, 16);
	int pad_right, pad_bot;
	int crop_right, crop_left, crop_top, crop_bot;
	int prp_mode = rot_mode >> 1; // remove prp_enable bit

	if (codec == W_HEVC_ENC &&
	    (!rot_mode || prp_mode == 14)) // prp_mode 14 : hor_mir && ver_mir && rot_180
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

	if (prp_mode == 1 || prp_mode == 15) {
		param->conf_win_top = crop_right;
		param->conf_win_left = crop_top;
		param->conf_win_bot = crop_left;
		param->conf_win_right = crop_bot;
	} else if (prp_mode == 2 || prp_mode == 12) {
		param->conf_win_top = crop_bot;
		param->conf_win_left = crop_right;
		param->conf_win_bot = crop_top;
		param->conf_win_right = crop_left;
	} else if (prp_mode == 3 || prp_mode == 13) {
		param->conf_win_top = crop_left;
		param->conf_win_left = crop_bot;
		param->conf_win_bot = crop_right;
		param->conf_win_right = crop_top;
	} else if (prp_mode == 4 || prp_mode == 10) {
		param->conf_win_top = crop_bot;
		param->conf_win_bot = crop_top;
	} else if (prp_mode == 8 || prp_mode == 6) {
		param->conf_win_left = crop_right;
		param->conf_win_right = crop_left;
	} else if (prp_mode == 5 || prp_mode == 11) {
		param->conf_win_top = crop_left;
		param->conf_win_left = crop_top;
		param->conf_win_bot = crop_right;
		param->conf_win_right = crop_bot;
	} else if (prp_mode == 7 || prp_mode == 9) {
		param->conf_win_top = crop_right;
		param->conf_win_left = crop_bot;
		param->conf_win_bot = crop_left;
		param->conf_win_right = crop_top;
	}
}

int wave5_vpu_enc_init_seq(struct vpu_instance *inst)
{
	u32 reg_val = 0, rot_mir_mode, fixed_cu_size_mode = 0x7;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct enc_open_param *p_open_param = &p_enc_info->open_param;
	struct enc_wave_param *p_param = &p_open_param->wave_param;
	int ret;

	if (inst->dev->product != PRODUCT_ID_521)
		return -EINVAL;

	/*==============================================*/
	/* OPT_CUSTOM_GOP */
	/*==============================================*/
	/*
	 * SET_PARAM + CUSTOM_GOP
	 * only when gop_preset_idx == custom_gop, custom_gop related registers should be set
	 */
	if (p_param->gop_preset_idx == PRESET_IDX_CUSTOM_GOP) {
		int i = 0, j = 0;

		vpu_write_reg(inst->dev, W5_CMD_ENC_CUSTOM_GOP_PARAM,
			      p_param->gop_param.custom_gop_size);
		for (i = 0; i < p_param->gop_param.custom_gop_size; i++) {
			vpu_write_reg(inst->dev, W5_CMD_ENC_CUSTOM_GOP_PIC_PARAM_0 + (i * 4),
				      p_param->gop_param.pic_param[i].pic_type |
				      (p_param->gop_param.pic_param[i].poc_offset << 2) |
				      (p_param->gop_param.pic_param[i].pic_qp << 6) |
				      (p_param->gop_param.pic_param[i].use_multi_ref_p << 13) |
				      ((p_param->gop_param.pic_param[i].ref_poc_l0 & 0x1F) << 14) |
				      ((p_param->gop_param.pic_param[i].ref_poc_l1 & 0x1F) << 19) |
				      (p_param->gop_param.pic_param[i].temporal_id << 24));
		}

		for (j = i; j < MAX_GOP_NUM; j++)
			vpu_write_reg(inst->dev,
				      W5_CMD_ENC_CUSTOM_GOP_PIC_PARAM_0 + (j * 4), 0);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_SET_PARAM_OPTION, OPT_CUSTOM_GOP);
		wave5_bit_issue_command(inst, W5_ENC_SET_PARAM);

		ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
		if (ret) {
			dev_warn(inst->dev->dev, "command: 'W5_ENC_SET_PARAM', timed out op=0x%x\n",
				 OPT_CUSTOM_GOP);
			return ret;
		}
	}

	/*======================================================================*/
	/* OPT_COMMON:								*/
	/*	the last SET_PARAM command should be called with OPT_COMMON	*/
	/*======================================================================*/
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

	if (inst->std == W_AVC_ENC) {
		reg_val = p_param->profile | (p_param->level << 3) |
			(p_param->internal_bit_depth << 14) | (p_param->use_long_term << 21);
		if (p_param->scaling_list_enable == 2) {
			reg_val |= BIT(22) | BIT(23); // [23]=USE_DEFAULT_SCALING_LIST
		} else { // 0 or 1
			reg_val |= (p_param->scaling_list_enable << 22);
		}
	} else { // HEVC enc
		reg_val = p_param->profile |
			(p_param->level << 3) |
			(p_param->tier << 12) |
			(p_param->internal_bit_depth << 14) |
			(p_param->use_long_term << 21) |
			(p_param->tmvp_enable << 23) |
			(p_param->sao_enable << 24) |
			(p_param->skip_intra_trans << 25) |
			(p_param->strong_intra_smooth_enable << 27) |
			(p_param->en_still_picture << 30);
		if (p_param->scaling_list_enable == 2)
			reg_val |= BIT(22) | BIT(31); // [31]=USE_DEFAULT_SCALING_LIST
		else
			reg_val |= (p_param->scaling_list_enable << 22);
	}

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_SPS_PARAM, reg_val);

	reg_val = (p_param->lossless_enable) |
		(p_param->const_intra_pred_flag << 1) |
		(p_param->lf_cross_slice_boundary_enable << 2) |
		(p_param->weight_pred_enable << 3) |
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
				((p_param->avc_idr_period & 0x7ff) << 17) |
				((p_param->forced_idr_header_enable & 3) << 28));
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_PARAM,
			      p_param->decoding_refresh_type | (p_param->intra_qp << 3) |
				(p_param->forced_idr_header_enable << 9) |
				(p_param->intra_period << 16));

	reg_val = (p_param->use_recommend_enc_param) |
		(p_param->rdo_skip << 2) |
		(p_param->lambda_scaling_enable << 3) |
		(p_param->coef_clear_disable << 4) |
		(fixed_cu_size_mode << 5) |
		(p_param->intra_nx_n_enable << 8) |
		(p_param->max_num_merge << 18) |
		(p_param->custom_md_enable << 20) |
		(p_param->custom_lambda_enable << 21) |
		(p_param->monochrome_enable << 22);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RDO_PARAM, reg_val);

	if (inst->std == W_AVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_REFRESH,
			      p_param->intra_mb_refresh_arg << 16 | p_param->intra_mb_refresh_mode);
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INTRA_REFRESH,
			      p_param->intra_refresh_arg << 16 | p_param->intra_refresh_mode);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_FRAME_RATE, p_open_param->frame_rate_info);
	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_TARGET_RATE, p_open_param->bit_rate);

	if (inst->std == W_AVC_ENC)
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_PARAM,
			      p_open_param->rc_enable | (p_param->mb_level_rc_enable << 1) |
			      (p_param->hvs_qp_enable << 2) | (p_param->hvs_qp_scale << 4) |
			      (p_param->bit_alloc_mode << 8) | (p_param->roi_enable << 13) |
			      ((p_param->initial_rc_qp & 0x3F) << 14) |
			      (p_open_param->vbv_buffer_size << 20));
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_PARAM,
			      p_open_param->rc_enable | (p_param->cu_level_rc_enable << 1) |
			      (p_param->hvs_qp_enable << 2) | (p_param->hvs_qp_scale << 4) |
			      (p_param->bit_alloc_mode << 8) | (p_param->roi_enable << 13) |
			      ((p_param->initial_rc_qp & 0x3F) << 14) |
			      (p_open_param->vbv_buffer_size << 20));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_WEIGHT_PARAM,
		      p_param->rc_weight_buf << 8 | p_param->rc_weight_param);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_MIN_MAX_QP, p_param->min_qp_i |
		      (p_param->max_qp_i << 6) | (p_param->hvs_max_delta_qp << 12));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_INTER_MIN_MAX_QP, p_param->min_qp_p |
		      (p_param->max_qp_p << 6) | (p_param->min_qp_b << 12) |
		      (p_param->max_qp_b << 18));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_BIT_RATIO_LAYER_0_3,
		      (u32)p_param->fixed_bit_ratio[0] |
		      ((u32)p_param->fixed_bit_ratio[1] << 8) |
		      ((u32)p_param->fixed_bit_ratio[2] << 16) |
		      ((u32)p_param->fixed_bit_ratio[3] << 24));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_RC_BIT_RATIO_LAYER_4_7,
		      (u32)p_param->fixed_bit_ratio[4] |
		      ((u32)p_param->fixed_bit_ratio[5] << 8) |
		      ((u32)p_param->fixed_bit_ratio[6] << 16) |
		      ((u32)p_param->fixed_bit_ratio[7] << 24));

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_ROT_PARAM, rot_mir_mode);

	vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_BG_PARAM, (p_param->bg_detect_enable) |
		      (p_param->bg_thr_diff << 1) | (p_param->bg_thr_mean_diff << 10) |
		      (p_param->bg_lambda_qp << 18) | ((p_param->bg_delta_qp & 0x1F) << 24) |
		      ((inst->std == W_AVC_ENC) ? p_param->s2fme_disable << 29 : 0));

	if (inst->std == W_HEVC_ENC || inst->std == W_AVC_ENC) {
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_LAMBDA_ADDR,
			      p_param->custom_lambda_addr);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CONF_WIN_TOP_BOT,
			      p_param->conf_win_bot << 16 | p_param->conf_win_top);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CONF_WIN_LEFT_RIGHT,
			      p_param->conf_win_right << 16 | p_param->conf_win_left);

		if (inst->std == W_AVC_ENC)
			vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INDEPENDENT_SLICE,
				      p_param->avc_slice_arg << 16 | p_param->avc_slice_mode);
		else
			vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_INDEPENDENT_SLICE,
				      p_param->independ_slice_mode_arg << 16 |
				      p_param->independ_slice_mode);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_USER_SCALING_LIST_ADDR,
			      p_param->user_scaling_list_addr);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NUM_UNITS_IN_TICK,
			      p_param->num_units_in_tick);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_TIME_SCALE, p_param->time_scale);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NUM_TICKS_POC_DIFF_ONE,
			      p_param->num_ticks_poc_diff_one);
	}

	if (inst->std == W_HEVC_ENC) {
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU04,
			      (p_param->pu04_delta_rate & 0xFF) |
			      ((p_param->pu04_intra_planar_delta_rate & 0xFF) << 8) |
			      ((p_param->pu04_intra_dc_delta_rate & 0xFF) << 16) |
			      ((p_param->pu04_intra_angle_delta_rate & 0xFF) << 24));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU08,
			      (p_param->pu08_delta_rate & 0xFF) |
			      ((p_param->pu08_intra_planar_delta_rate & 0xFF) << 8) |
			      ((p_param->pu08_intra_dc_delta_rate & 0xFF) << 16) |
			      ((p_param->pu08_intra_angle_delta_rate & 0xFF) << 24));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU16,
			      (p_param->pu16_delta_rate & 0xFF) |
			      ((p_param->pu16_intra_planar_delta_rate & 0xFF) << 8) |
			      ((p_param->pu16_intra_dc_delta_rate & 0xFF) << 16) |
			      ((p_param->pu16_intra_angle_delta_rate & 0xFF) << 24));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_PU32,
			      (p_param->pu32_delta_rate & 0xFF) |
			      ((p_param->pu32_intra_planar_delta_rate & 0xFF) << 8) |
			      ((p_param->pu32_intra_dc_delta_rate & 0xFF) << 16) |
			      ((p_param->pu32_intra_angle_delta_rate & 0xFF) << 24));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU08,
			      (p_param->cu08_intra_delta_rate & 0xFF) |
			      ((p_param->cu08_inter_delta_rate & 0xFF) << 8) |
			      ((p_param->cu08_merge_delta_rate & 0xFF) << 16));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU16,
			      (p_param->cu16_intra_delta_rate & 0xFF) |
			      ((p_param->cu16_inter_delta_rate & 0xFF) << 8) |
			      ((p_param->cu16_merge_delta_rate & 0xFF) << 16));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_CUSTOM_MD_CU32,
			      (p_param->cu32_intra_delta_rate & 0xFF) |
			      ((p_param->cu32_inter_delta_rate & 0xFF) << 8) |
			      ((p_param->cu32_merge_delta_rate & 0xFF) << 16));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_DEPENDENT_SLICE,
			      p_param->depend_slice_mode_arg << 16 | p_param->depend_slice_mode);

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NR_PARAM, p_param->nr_y_enable |
			      (p_param->nr_cb_enable << 1) | (p_param->nr_cr_enable << 2) |
			      (p_param->nr_noise_est_enable << 3) |
			      (p_param->nr_noise_sigma_y << 4) |
			      (p_param->nr_noise_sigma_cb << 12) |
			      (p_param->nr_noise_sigma_cr << 20));

		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_NR_WEIGHT,
			      p_param->nr_intra_weight_y |
			      (p_param->nr_intra_weight_cb << 5) |
			      (p_param->nr_intra_weight_cr << 10) |
			      (p_param->nr_inter_weight_y << 15) |
			      (p_param->nr_inter_weight_cb << 20) |
			      (p_param->nr_inter_weight_cr << 25));
	}
	if (p_enc_info->open_param.encode_vui_rbsp || p_enc_info->open_param.enc_hrd_rbsp_in_vps) {
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_VUI_HRD_PARAM,
			      (p_enc_info->open_param.hrd_rbsp_data_size << 18) |
			      (p_enc_info->open_param.vui_rbsp_data_size << 4) |
			      (p_enc_info->open_param.enc_hrd_rbsp_in_vps << 2) |
			      (p_enc_info->open_param.encode_vui_rbsp));
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_VUI_RBSP_ADDR,
			      p_enc_info->open_param.vui_rbsp_data_addr);
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_HRD_RBSP_ADDR,
			      p_enc_info->open_param.hrd_rbsp_data_addr);
	} else {
		vpu_write_reg(inst->dev, W5_CMD_ENC_SEQ_VUI_HRD_PARAM, 0);
	}

	wave5_bit_issue_command(inst, W5_ENC_SET_PARAM);

	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_ENC_SET_PARAM', timed out\n");
		return ret;
	}

	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, reg_val);
		return -EIO;
	}

	return 0;
}

int wave5_vpu_enc_get_seq_info(struct vpu_instance *inst, struct enc_initial_info *info)
{
	int ret;
	u32 reg_val;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;

	if (inst->dev->product != PRODUCT_ID_521)
		return -EINVAL;

	// send QUERY cmd
	ret = wave5_send_query(inst, GET_RESULT);
	if (ret) {
		if (ret == -EIO) {
			reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
			wave5_print_reg_err(inst->dev, reg_val);
		}
		return ret;
	}

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
	info->max_latency_pictures = vpu_read_reg(inst->dev, W5_RET_ENC_PIC_MAX_LATENCY_PICS);
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
	u32 endian, luma_stride, chroma_stride, frame_width, frame_height;
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

	frame_width = ALIGN(buf_width, 16);
	frame_height = ALIGN(buf_height, 16);
	if (p_enc_info->product_code == WAVE521C_DUAL_CODE) {
		// Use 1024 for H264(AVC) and 512 for H265(HEVC)
		fbc_y_tbl_size = calculate_table_size(bit_depth, frame_width, frame_height,
						      (avc_encoding ? 1024 : 512));
	} else {
		fbc_y_tbl_size = WAVE5_FBC_LUMA_TABLE_SIZE(buf_width, buf_height);
		fbc_y_tbl_size = ALIGN(fbc_y_tbl_size, 16);
	}

	vb_fbc_y_tbl.daddr = 0;
	vb_fbc_y_tbl.size = ALIGN(fbc_y_tbl_size * count, BUFFER_MARGIN) + BUFFER_MARGIN;
	ret = wave5_vdi_allocate_dma_memory(vpu_dev, &vb_fbc_y_tbl);
	if (ret)
		goto free_vb_fbc_y_tbl;

	p_enc_info->vb_fbc_y_tbl = vb_fbc_y_tbl;

	if (p_enc_info->product_code == WAVE521C_DUAL_CODE) {
		// Use 1024 for H264(AVC) and 512 for HEVC
		fbc_c_tbl_size = calculate_table_size(bit_depth, frame_width, frame_height,
						      (avc_encoding ? 1024 : 512));
	} else {
		fbc_c_tbl_size = WAVE5_FBC_CHROMA_TABLE_SIZE(buf_width, buf_height);
		fbc_c_tbl_size = ALIGN(fbc_c_tbl_size, 16);
	}

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

	// set sub-sampled buffer base addr
	vpu_write_reg(inst->dev, W5_ADDR_SUB_SAMPLED_FB_BASE, vb_sub_sam_buf.daddr);
	// set sub-sampled buffer size for one frame
	vpu_write_reg(inst->dev, W5_SUB_SAMPLED_ONE_FB_SIZE, sub_sampled_size);

	endian = wave5_vdi_convert_endian(vpu_dev, fb_arr[0].endian);

	vpu_write_reg(inst->dev, W5_PIC_SIZE, pic_size);

	// set stride of luma/chroma for compressed buffer
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
	cnt_8_chunk = ALIGN(count, 8) / 8;
	idx = 0;
	for (j = 0; j < cnt_8_chunk; j++) {
		reg_val = (endian << 16) | (j == cnt_8_chunk - 1) << 4 | ((j == 0) << 3);
		reg_val |= (p_open_param->enable_non_ref_fbc_write << 26);
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

		wave5_bit_issue_command(inst, W5_SET_FB);
		ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
		if (ret)
			goto free_vb_mem;
	}

	reg_val = vpu_read_reg(inst->dev, W5_RET_SUCCESS);
	if (!reg_val) {
		ret = -EIO;
		goto free_vb_mem;
	}

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
	u32 reg_val = 0, bs_endian;
	u32 src_stride_c = 0;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct frame_buffer *p_src_frame = option->source_frame;
	struct enc_open_param *p_open_param = &p_enc_info->open_param;
	bool justified = WTL_RIGHT_JUSTIFIED;
	u32 format_no = WTL_PIXEL_8BIT;
	int ret;

	if (inst->dev->product != PRODUCT_ID_521)
		return -EINVAL;

	vpu_write_reg(inst->dev, W5_CMD_ENC_BS_START_ADDR, option->pic_stream_buffer_addr);
	vpu_write_reg(inst->dev, W5_CMD_ENC_BS_SIZE, option->pic_stream_buffer_size);
	p_enc_info->stream_buf_start_addr = option->pic_stream_buffer_addr;
	p_enc_info->stream_buf_size = option->pic_stream_buffer_size;
	p_enc_info->stream_buf_end_addr =
		option->pic_stream_buffer_addr + option->pic_stream_buffer_size;

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_AXI_SEL, DEFAULT_SRC_AXI);
	/* secondary AXI */
	reg_val = (p_enc_info->sec_axi_info.wave.use_enc_rdo_enable << 11) |
		(p_enc_info->sec_axi_info.wave.use_enc_lf_enable << 15);
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

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PIC_PARAM, option->skip_picture |
		      (option->force_pic_qp_enable << 1) | (option->force_pic_qp_i << 2) |
		      (option->force_pic_qp_p << 8) | (option->force_pic_qp_b << 14) |
		      (option->force_pic_type_enable << 20) | (option->force_pic_type << 21) |
		      (option->force_all_ctu_coef_drop_enable << 24));

	if (option->src_end_flag)
		// no more source images.
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_PIC_IDX, 0xFFFFFFFF);
	else
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_PIC_IDX, option->src_idx);

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_Y, p_src_frame->buf_y);
	if (p_open_param->cbcr_order == CBCR_ORDER_NORMAL) {
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_U, p_src_frame->buf_cb);
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_V, p_src_frame->buf_cr);
	} else {
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_U, p_src_frame->buf_cr);
		vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_ADDR_V, p_src_frame->buf_cb);
	}

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

	reg_val = wave5_vdi_convert_endian(inst->dev, p_open_param->source_endian);
	bs_endian = (~reg_val & VDI_128BIT_ENDIAN_MASK);

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_STRIDE,
		      (p_src_frame->stride << 16) | src_stride_c);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SRC_FORMAT, src_frame_format |
		      (format_no << 3) | (justified << 5) | (bs_endian << 6));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CUSTOM_MAP_OPTION_ADDR,
		      option->custom_map_opt.addr_custom_map);

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_CUSTOM_MAP_OPTION_PARAM,
		      option->custom_map_opt.custom_roi_map_enable |
		      (option->custom_map_opt.roi_avg_qp << 1) |
		      (option->custom_map_opt.custom_lambda_map_enable << 8) |
		      (option->custom_map_opt.custom_mode_map_enable << 9) |
		      (option->custom_map_opt.custom_coef_drop_enable << 10));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_LONGTERM_PIC,
		      option->use_cur_src_as_longterm_pic | (option->use_longterm_ref << 1));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_SIGMA_Y, option->wp_pix_sigma_y);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_SIGMA_C,
		      (option->wp_pix_sigma_cr << 16) | option->wp_pix_sigma_cb);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_MEAN_Y, option->wp_pix_mean_y);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_WP_PIXEL_MEAN_C,
		      (option->wp_pix_mean_cr << 16) | (option->wp_pix_mean_cb));

	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PREFIX_SEI_INFO, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_PREFIX_SEI_NAL_ADDR, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SUFFIX_SEI_INFO, 0);
	vpu_write_reg(inst->dev, W5_CMD_ENC_PIC_SUFFIX_SEI_NAL_ADDR, 0);

	wave5_bit_issue_command(inst, W5_ENC_PIC);

	// check QUEUE_DONE
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret) {
		dev_warn(inst->dev->dev, "command: 'W5_ENC_PIC', timed out\n");
		return -ETIMEDOUT;
	}

	reg_val = vpu_read_reg(inst->dev, W5_RET_QUEUE_STATUS);

	p_enc_info->instance_queue_count = (reg_val >> 16) & 0xff;
	p_enc_info->report_queue_count = (reg_val & QUEUE_REPORT_MASK);

	// Check if we were able to add a command into the VCPU QUEUE
	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		*fail_res = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, *fail_res);
		return -EIO;
	}

	return 0;
}

int wave5_vpu_enc_get_result(struct vpu_instance *inst, struct enc_output_info *result)
{
	int ret;
	u32 encoding_success;
	u32 reg_val;
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	struct vpu_device *vpu_dev = inst->dev;

	if (vpu_dev->product != PRODUCT_ID_521)
		return -EINVAL;

	ret = wave5_send_query(inst, GET_RESULT);
	if (ret) {
		if (ret == -EIO) {
			reg_val = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
			wave5_print_reg_err(inst->dev, reg_val);
		}
		return ret;
	}
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

	//result for header only(no vcl) encoding
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
	int ret;

	if (inst->dev->product != PRODUCT_ID_521)
		return -EINVAL;

	wave5_bit_issue_command(inst, W5_DESTROY_INSTANCE);
	ret = wave5_wait_vpu_busy(inst->dev, W5_VPU_BUSY_STATUS);
	if (ret)
		return -ETIMEDOUT;

	if (!vpu_read_reg(inst->dev, W5_RET_SUCCESS)) {
		*fail_res = vpu_read_reg(inst->dev, W5_RET_FAIL_REASON);
		wave5_print_reg_err(inst->dev, *fail_res);
		return -EIO;
	}
	return 0;
}

static int wave5_vpu_enc_check_common_param_valid(struct vpu_instance *inst,
						  struct enc_open_param *open_param)
{
	int i = 0;
	bool low_delay = true;
	struct enc_wave_param *param = &open_param->wave_param;
	struct vpu_device *vpu_dev = inst->dev;
	struct device *dev = vpu_dev->dev;
	u32 num_ctu_row = (open_param->pic_height + 64 - 1) / 64;
	u32 num_ctu_col = (open_param->pic_width + 64 - 1) / 64;
	u32 ctu_sz = num_ctu_col * num_ctu_row;

	// check low-delay gop structure
	if (param->gop_preset_idx == PRESET_IDX_CUSTOM_GOP) { /* common gop */
		if (param->gop_param.custom_gop_size > 1) {
			s32 min_val = param->gop_param.pic_param[0].poc_offset;

			for (i = 1; i < param->gop_param.custom_gop_size; i++) {
				if (min_val > param->gop_param.pic_param[i].poc_offset) {
					low_delay = false;
					break;
				}
				min_val = param->gop_param.pic_param[i].poc_offset;
			}
		}
	}

	if (inst->std == W_HEVC_ENC && low_delay &&
	    param->decoding_refresh_type == DEC_REFRESH_TYPE_CRA) {
		dev_warn(dev,
			 "dec_refresh_type(CRA) shouldn't be used together with low delay GOP\n");
		dev_warn(dev, "Suggested configuration parameter: decoding refresh type (IDR)\n");
		param->decoding_refresh_type = 2;
	}

	if (param->gop_preset_idx == PRESET_IDX_CUSTOM_GOP) {
		for (i = 0; i < param->gop_param.custom_gop_size; i++) {
			if (param->gop_param.pic_param[i].temporal_id >= MAX_NUM_TEMPORAL_LAYER) {
				dev_err(dev, "temporal_id: %d exceeds MAX_NUM_TEMPORAL_LAYER (%u)\n",
					param->gop_param.pic_param[i].temporal_id,
					MAX_NUM_TEMPORAL_LAYER);
				return -EINVAL;
			}

			if (param->gop_param.pic_param[i].temporal_id < 0) {
				dev_err(dev, "temporal_id: %d must be greater or equal to 0\n",
					param->gop_param.pic_param[i].temporal_id);
				return -EINVAL;
			}
		}
	}

	if (param->wpp_enable && param->independ_slice_mode) {
		unsigned int num_ctb_in_width = ALIGN(open_param->pic_width, 64) >> 6;

		if (param->independ_slice_mode_arg % num_ctb_in_width) {
			dev_err(dev, "independ_slice_mode_arg %u must be a multiple of %u\n",
				param->independ_slice_mode_arg, num_ctb_in_width);
			return -EINVAL;
		}
	}

	// multi-slice & wpp
	if (param->wpp_enable && param->depend_slice_mode) {
		dev_err(dev, "wpp_enable && depend_slice_mode cannot be used simultaneously\n");
		return -EINVAL;
	}

	if (!param->independ_slice_mode && param->depend_slice_mode) {
		dev_err(dev, "depend_slice_mode requires independ_slice_mode\n");
		return -EINVAL;
	} else if (param->independ_slice_mode &&
		   param->depend_slice_mode == DEPEND_SLICE_MODE_RECOMMENDED &&
		   param->independ_slice_mode_arg < param->depend_slice_mode_arg) {
		dev_err(dev, "independ_slice_mode_arg: %u must be smaller than %u\n",
			param->independ_slice_mode_arg, param->depend_slice_mode_arg);
		return -EINVAL;
	}

	if (param->independ_slice_mode && param->independ_slice_mode_arg > 65535) {
		dev_err(dev, "independ_slice_mode_arg: %u must be smaller than 65535\n",
			param->independ_slice_mode_arg);
		return -EINVAL;
	}

	if (param->depend_slice_mode && param->depend_slice_mode_arg > 65535) {
		dev_err(dev, "depend_slice_mode_arg: %u must be smaller than 65535\n",
			param->depend_slice_mode_arg);
		return -EINVAL;
	}

	if (param->conf_win_top % 2) {
		dev_err(dev, "conf_win_top: %u, must be a multiple of 2\n", param->conf_win_top);
		return -EINVAL;
	}

	if (param->conf_win_bot % 2) {
		dev_err(dev, "conf_win_bot: %u, must be a multiple of 2\n", param->conf_win_bot);
		return -EINVAL;
	}

	if (param->conf_win_left % 2) {
		dev_err(dev, "conf_win_left: %u, must be a multiple of 2\n", param->conf_win_left);
		return -EINVAL;
	}

	if (param->conf_win_right % 2) {
		dev_err(dev, "conf_win_right: %u, Must be a multiple of 2\n",
			param->conf_win_right);
		return -EINVAL;
	}

	if (param->lossless_enable && (param->nr_y_enable || param->nr_cb_enable ||
				       param->nr_cr_enable)) {
		/* Noise reduction => en_nr_y, en_nr_cb, en_nr_cr */
		dev_err(dev, "option noise_reduction cannot be used with lossless_coding\n");
		return -EINVAL;
	}

	if (param->lossless_enable && param->bg_detect_enable) {
		dev_err(dev, "option bg_detect cannot be used with lossless_coding\n");
		return -EINVAL;
	}

	if (param->lossless_enable && open_param->rc_enable) {
		dev_err(dev, "option rate_control cannot be used with lossless_coding\n");
		return -EINVAL;
	}

	if (param->lossless_enable && param->roi_enable) {
		dev_err(dev, "option roi cannot be used with lossless_coding\n");
		return -EINVAL;
	}

	if (param->lossless_enable && !param->skip_intra_trans) {
		dev_err(dev, "option intra_trans_skip must be enabled with lossless_coding\n");
		return -EINVAL;
	}

	// intra refresh
	if (param->intra_refresh_mode && param->intra_refresh_arg == 0) {
		dev_err(dev, "Invalid refresh argument, mode: %u, refresh: %u must be > 0\n",
			param->intra_refresh_mode, param->intra_refresh_arg);
		return -EINVAL;
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
			return -EINVAL;
		}
		if (param->roi_enable) {
			dev_err(dev, "mode: %u cannot be used and roi_enable",
				param->intra_refresh_mode);
			return -EINVAL;
		}
	};
	return 0;

invalid_refresh_argument:
	dev_err(dev, "Invalid refresh argument, mode: %u, refresh: %u > W(%u)xH(%u)\n",
		param->intra_refresh_mode, param->intra_refresh_arg,
		num_ctu_row, num_ctu_col);
	return -EINVAL;
}

static int wave5_vpu_enc_check_param_valid(struct vpu_device *vpu_dev,
					   struct enc_open_param *open_param)
{
	struct enc_wave_param *param = &open_param->wave_param;

	if (open_param->rc_enable) {
		if (param->min_qp_i > param->max_qp_i || param->min_qp_p > param->max_qp_p ||
		    param->min_qp_b > param->max_qp_b) {
			dev_err(vpu_dev->dev, "Configuration failed because min_qp is greater than max_qp\n");
			dev_err(vpu_dev->dev, "Suggested configuration parameters: min_qp = max_qp\n");
			return -EINVAL;
		}

		if (open_param->bit_rate <= (int)open_param->frame_rate_info) {
			dev_err(vpu_dev->dev,
				"enc_bit_rate: %u must be greater than the frame_rate: %u\n",
				open_param->bit_rate, (int)open_param->frame_rate_info);
			return -EINVAL;
		}
	}

	return 0;
}

static int wave5_vpu_enc_check_custom_gop(struct vpu_device *vpu_dev,
					  struct enc_open_param *open_param)
{
	struct custom_gop_param *gop_param;
	struct custom_gop_pic_param *gop_pic_param;
	struct custom_gop_pic_param new_gop[MAX_GOP_NUM * 2 + 1];

	unsigned int i, ei, gi;
	u32 gop_size;
	s32 curr_poc, ref_poc;
	s32 enc_tid[MAX_GOP_NUM * 2 + 1];

	gop_param = &open_param->wave_param.gop_param;
	gop_size = gop_param->custom_gop_size;

	new_gop[0].poc_offset = 0;
	new_gop[0].temporal_id = 0;
	new_gop[0].pic_type = PIC_TYPE_I;
	new_gop[0].use_multi_ref_p = 0;
	enc_tid[0] = 0;

	for (i = 0; i < gop_size * 2; i++) {
		ei = i % gop_size;
		gi = i / gop_size;
		gop_pic_param = &gop_param->pic_param[ei];

		curr_poc = gi * gop_size + gop_pic_param->poc_offset;
		new_gop[i + 1].poc_offset = curr_poc;
		new_gop[i + 1].temporal_id = gop_pic_param->temporal_id;
		new_gop[i + 1].pic_type = gop_pic_param->pic_type;
		new_gop[i + 1].ref_poc_l0 = gop_pic_param->ref_poc_l0 + gi * gop_size;
		new_gop[i + 1].ref_poc_l1 = gop_pic_param->ref_poc_l1 + gi * gop_size;
		new_gop[i + 1].use_multi_ref_p = gop_pic_param->use_multi_ref_p;
		enc_tid[i + 1] = -1;
	}

	for (i = 0; i < gop_size; i++) {
		gop_pic_param = &gop_param->pic_param[i];

		if (gop_pic_param->poc_offset <= 0) {
			dev_err(vpu_dev->dev, "POC of the %u-th pic not greater then -1\n", i + 1);
			return -EINVAL;
		}
		if (gop_pic_param->poc_offset > gop_size) {
			dev_err(vpu_dev->dev, "POC of %uth pic bigger than gop_size\n", i + 1);
			return -EINVAL;
		}
		if (gop_pic_param->temporal_id < 0) {
			dev_err(vpu_dev->dev, "temporal_id of the %d-th  < 0\n", i + 1);
			return -EINVAL;
		}
	}

	for (ei = 1; ei < gop_size * 2 + 1; ei++) {
		struct custom_gop_pic_param *cur_pic = &new_gop[ei];

		if (ei <= gop_size) {
			enc_tid[cur_pic->poc_offset] = cur_pic->temporal_id;
			continue;
		}

		if (new_gop[ei].pic_type != PIC_TYPE_I) {
			ref_poc = cur_pic->ref_poc_l0;

			/* reference picture is not encoded yet */
			if (enc_tid[ref_poc] < 0) {
				dev_err(vpu_dev->dev, "1st ref pic can't be ref of pic (POC: %u)\n",
					cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
			if (enc_tid[ref_poc] > cur_pic->temporal_id) {
				dev_err(vpu_dev->dev, "wrong temporal_id of pic (POC: %u)\n",
					cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
			if (ref_poc >= cur_pic->poc_offset) {
				dev_err(vpu_dev->dev, "POC of 1st ref pic of %u-th pic is wrong\n",
					cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
		}
		if (new_gop[ei].pic_type != PIC_TYPE_P) {
			ref_poc = cur_pic->ref_poc_l1;

			/* reference picture is not encoded yet */
			if (enc_tid[ref_poc] < 0) {
				dev_err(vpu_dev->dev, "2nd ref pic can't be ref of pic (POC: %u)\n"
						, cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
			if (enc_tid[ref_poc] > cur_pic->temporal_id) {
				dev_err(vpu_dev->dev,  "temporal_id of %u-th picture is wrong\n",
					cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
			if (new_gop[ei].pic_type == PIC_TYPE_P && new_gop[ei].use_multi_ref_p > 0) {
				if (ref_poc >= cur_pic->poc_offset) {
					dev_err(vpu_dev->dev,  "bad POC of 2nd ref pic of %uth pic\n",
						cur_pic->poc_offset - gop_size);
					return -EINVAL;
				}
			} else if (ref_poc == cur_pic->poc_offset) {
				/* HOST_PIC_TYPE_B */
				dev_err(vpu_dev->dev,  "POC of 2nd ref pic of %uth pic is wrong\n",
					cur_pic->poc_offset - gop_size);
				return -EINVAL;
			}
		}
		curr_poc = cur_pic->poc_offset;
		enc_tid[curr_poc] = cur_pic->temporal_id;
	}
	return 0;
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

	if (open_param->ring_buffer_enable) {
		if (open_param->bitstream_buffer % 8) {
			dev_err(inst->dev->dev,
				"Bitstream buffer must be aligned to a multiple of 8\n");
			return -EINVAL;
		}
		if (open_param->bitstream_buffer_size % 1024 ||
		    open_param->bitstream_buffer_size < MIN_BITSTREAM_BUFFER_SIZE) {
			dev_err(inst->dev->dev,
				"Bitstream buffer size must be aligned to a multiple of 1024 and have a minimum size of %u\n",
				MIN_BITSTREAM_BUFFER_SIZE);
			return -EINVAL;
		}
		if (product_id == PRODUCT_ID_521) {
			if (open_param->bitstream_buffer % 16) {
				dev_err(inst->dev->dev,
					"Bitstream buffer must be aligned to a multiple of 16\n");
				return -EINVAL;
			}
			if (open_param->bitstream_buffer_size < MIN_BITSTREAM_BUFFER_SIZE_WAVE521) {
				dev_err(inst->dev->dev,
					"Bitstream buffer too small: %u (minimum: %u)\n",
					open_param->bitstream_buffer_size,
					MIN_BITSTREAM_BUFFER_SIZE_WAVE521);
				return -EINVAL;
			}
		}
	}

	if (!open_param->frame_rate_info)
		return -EINVAL;
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

	if (param->gop_preset_idx == PRESET_IDX_CUSTOM_GOP) {
		if (param->gop_param.custom_gop_size < 1 ||
		    param->gop_param.custom_gop_size > MAX_GOP_NUM) {
			dev_err(inst->dev->dev,
				"Invalid custom group of pictures size: %u (valid: 1-%u)\n",
				param->gop_param.custom_gop_size, MAX_GOP_NUM);
			return -EINVAL;
		}
	}

	if (inst->std == W_AVC_ENC && param->custom_lambda_enable) {
		dev_err(inst->dev->dev,
			"Cannot combine AVC encoding with the custom lambda option\n");
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

	if (param->scaling_list_enable > 2) {
		dev_err(inst->dev->dev, "Invalid scaling_list_enable: %u (valid: 0-2)\n",
			param->scaling_list_enable);
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

		if (param->bit_alloc_mode > BIT_ALLOC_MODE_FIXED_RATIO) {
			dev_err(inst->dev->dev, "Invalid bit alloc mode: %u (valid: 0-2)\n",
				param->bit_alloc_mode);
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

	if (wave5_vpu_enc_check_common_param_valid(inst, open_param))
		return -EINVAL;

	if (wave5_vpu_enc_check_param_valid(inst->dev, open_param))
		return -EINVAL;

	if (param->gop_preset_idx == PRESET_IDX_CUSTOM_GOP) {
		if (wave5_vpu_enc_check_custom_gop(inst->dev, open_param))
			return -EINVAL;
	}

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
		if (param->nr_noise_sigma_y > MAX_NOISE_SIGMA ||
		    param->nr_noise_sigma_cb > MAX_NOISE_SIGMA ||
		    param->nr_noise_sigma_cr > MAX_NOISE_SIGMA) {
			dev_err(inst->dev->dev,
				"Invalid noise sigma Y(%u) Cb(%u) Cr(%u) (valid: %u)\n",
				param->nr_noise_sigma_y, param->nr_noise_sigma_cb,
				param->nr_noise_sigma_cr, MAX_NOISE_SIGMA);
			return -EINVAL;
		}

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

		if ((param->nr_y_enable || param->nr_cb_enable || param->nr_cr_enable) &&
		    param->lossless_enable) {
			dev_err(inst->dev->dev,
				"Can't enable lossless mode with either nr_y, nr_cb or nr_cr\n");
			return -EINVAL;
		}
	}

	return 0;
}
