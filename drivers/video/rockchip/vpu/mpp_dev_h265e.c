/*
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: hehua,hh@rock-chips.com
 * lixinhuang, buluess.li@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rk_fb.h>
#include <linux/rockchip/pmu.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "vpu_iommu_ops.h"
#include "mpp_dev_common.h"
#include "mpp_dev_h265e.h"
#include "mpp_dev_h265e_define.h"
#include "mpp_dev_h265e_reg.h"

#define MPP_ALIGN_SIZE	0x1000

#define H265E_FIRMWARE_NAME "monet.bin"
#define PRINT_BS_DATA 0
#if PRINT_BS_DATA
#define H265E_BS_DATA_PATH "/h265e/bs_data"
static char buff[1000000];
static struct file *fp_bs[H265E_INSTANCE_NUM];
static u32 w_bs_size;
#endif

#define H265E_WORK_BUFFER_SIZE			(128 * 1024)
#define H265E_TEMP_BUFFER_SIZE			(1024 * 1024)
#define H265E_CODE_BUFFER_SIZE			(1024 * 1024)
#define H265E_SEC_AXI_BUF_SIZE			0x12800
#define H265E_INT_CLOSE 0
#define H265E_INT_OPEN  0x08
#define H265E_REMAP_CODE_INDEX			0

#define H265E_BUSY_CHECK_TIMEOUT		5000

#define DEBUG_H265E_INFO				0x00100000
#define DEBUG_H265E_ENCODE_ONE_FRAME	0x00200000
#define H265E_POWER_SAVE 0
#define H265E_CLK 1
#ifdef CONFIG_MFD_SYSCON
#define H265E_AXI_STATUS 1
#endif
static ktime_t h265e_now, h265e_last;

static
struct mpp_session *rockchip_mpp_h265e_open(struct rockchip_mpp_dev *mpp);
static void rockchip_mpp_h265e_release(struct mpp_session *isession);
static int rockchip_mpp_h265e_load_firmware(struct rockchip_mpp_dev *mpp);
static int rockchip_mpp_h265e_encode_one_frame(struct rockchip_mpp_dev *mpp,
					       struct h265e_ctx *ctx,
					       int index);
static int rockchip_mpp_h265e_get_encode_result(struct rockchip_mpp_dev *mpp,
						struct h265e_ctx *result);
static int rockchip_mpp_h265e_set_gop_parameter(struct rockchip_mpp_dev *mpp,
						int index);
static
int rockchip_mpp_h265e_register_frame_buffer(struct rockchip_mpp_dev *mpp,
					     int index);
static void rockchip_mpp_h265e_enable_clk(struct rockchip_mpp_dev *mpp);
static void rockchip_mpp_h265e_disable_clk(struct rockchip_mpp_dev *mpp);

static int rockchip_mpp_h265e_dma_alloc(struct rockchip_mpp_dev *mpp,
					struct mpp_session *session,
					size_t len,
					size_t align,
					unsigned long *addr)
{
	int ret = 0;
	unsigned long tmp;
	int tmp_hdl;

	tmp_hdl = vpu_iommu_alloc(mpp->iommu_info, session, len, align);
	if (tmp_hdl < 0) {
		dev_err(mpp->dev, "error: ion_alloc fail\n");
		return -1;
	}

	ret = vpu_iommu_map_iommu(mpp->iommu_info,
				  session, tmp_hdl, addr, &tmp);
	if (ret < 0) {
		dev_err(mpp->dev, "get link table dma_addr failed\n");
		goto FAIL;
	}
	return tmp_hdl;
FAIL:
	if (tmp_hdl)
		vpu_iommu_free(mpp->iommu_info, session, tmp_hdl);
	return -1;
}

static int rockchip_mpp_h265e_global_dma_alloc(struct rockchip_mpp_dev *mpp,
					       size_t len,
					       size_t align,
					       unsigned long *addr)
{
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);

	return rockchip_mpp_h265e_dma_alloc(mpp, session, len, align, addr);
}

static void rockchip_mpp_h265e_free_frame_buffer(struct rockchip_mpp_dev *mpp,
						 struct rockchip_h265e_instance *instance)
{
	int i = 0;
	struct mpp_h265e_buffer *buf = NULL;
	struct mpp_h265e_frame_buffer *fb = NULL;
	struct mpp_session *session = instance->session;

	mpp_debug_enter();
	buf = &instance->mv;
	if (buf->hdl >= 0)
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	buf = &instance->fbc_luma;
	if (buf->hdl >= 0)
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	buf = &instance->fbc_chroma;
	if (buf->hdl >= 0)
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	buf = &instance->sub_sample;
	if (buf->hdl >= 0)
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	for (i = 0; i < ARRAY_SIZE(instance->frame_buffer); i++) {
		fb = &instance->frame_buffer[i];
		buf = &fb->buffer;
		if (buf->hdl >= 0)
			vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
		fb->y = 0;
		fb->cb = 0;
		fb->cr = 0;
	}
	mpp_debug_leave();
}

static void rockchip_mpp_h265e_free_instance(struct rockchip_mpp_dev *mpp,
					     int index)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = &enc->instance[index];
	struct mpp_h265e_buffer *buf = &instance->work;
	struct mpp_session *session = instance->session;

	mpp_debug_enter();
#if PRINT_BS_DATA
	filp_close(fp_bs[index], NULL);
#endif
	if (!mpp || !instance)
		return;
	if (buf->hdl >= 0)
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	rockchip_mpp_h265e_free_frame_buffer(mpp, instance);
	atomic_set(&enc->instance[index].is_used, 0);
	mpp_debug_leave();
}

static int rockchip_mpp_h265e_wait_busy(struct rockchip_mpp_dev *mpp)
{
	int reg_val = 0xFFFFFFFF, time_count = 0;

	while (reg_val != 0x0) {
		reg_val = mpp_read(mpp, H265E_VPU_BUSY_STATUS);
		if (time_count++ > H265E_BUSY_CHECK_TIMEOUT)
			return -1;
	}
	return 0;
}

static void rockchip_mpp_h265e_issue_command(struct rockchip_mpp_dev *mpp,
					     u32 index, u32 cmd)
{
	u32 value = 0;

	mpp_write(mpp, 1, H265E_VPU_BUSY_STATUS);
	value = ((index & 0xffff) | (1 << 16));
	mpp_write(mpp, value, H265E_INST_INDEX);
	mpp_write(mpp, cmd, H265E_COMMAND);
	if (cmd != H265E_CMD_INIT_VPU)
		mpp_write(mpp, 1, H265E_VPU_HOST_INT_REQ);
}

#if PRINT_BS_DATA
static int rockchip_mpp_h265e_write_encoder_file(struct rockchip_mpp_dev *mpp)
{
	struct h265e_ctx *ctx = container_of(mpp_srv_get_current_ctx(mpp->srv),
					     struct h265e_ctx, ictx);
	struct h265e_session *session =
					container_of(ctx->ictx.session,
						     struct h265e_session,
						     isession);
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	int index = session->instance_index;
	int nread = 0;
	loff_t pos = 0;
	mm_segment_t old_fs;
	u32 value = 0;
	u32 i = 0;
	char file_name[30];

	mutex_lock(&enc->lock);
	mpp_debug_enter();
	value = w_bs_size;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	sprintf(file_name, "%s_%d.bin", H265E_BS_DATA_PATH, index);
	fp_bs[index] = filp_open(file_name, O_RDWR | O_CREAT | O_APPEND, 0x777);
	if (IS_ERR(fp_bs[index])) {
		mpp_err("error: open yuv failed in load_yuv\n");
		set_fs(old_fs);
		mutex_unlock(&enc->lock);
		return -1;
	}
	for (i = 0; i < (value * 3); i++) {
		if (ctx->bs_data[i] < 0x10) {
			sprintf(&buff[3 * i], "0");
			sprintf(&buff[3 * i + 1], "%-2x", ctx->bs_data[i]);
		} else {
			sprintf(&buff[3 * i], "%-3x", ctx->bs_data[i]);
		}
	}
	nread = (int)vfs_write(fp_bs[index], buff, value * 3, &pos);
	set_fs(old_fs);
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
	return 0;
}
#endif

static int rockchip_mpp_h265e_load_firmware(struct rockchip_mpp_dev *mpp)
{
	const struct firmware *firmware;
	u32 size = 0;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);

	if (request_firmware(&firmware, H265E_FIRMWARE_NAME, mpp->dev) < 0) {
		mpp_err("firmware request failed\n");
		release_firmware(firmware);
		return -1;
	}
	mpp_debug(DEBUG_H265E_INFO, "h265e firmware data %p size %zu\n",
		  firmware->data, firmware->size);
	size = ALIGN(firmware->size, H265E_CODE_BUFFER_SIZE);
	enc->firmware.hdl =
		rockchip_mpp_h265e_global_dma_alloc(mpp,
						    size,
						    MPP_ALIGN_SIZE,
						    &enc->firmware.dma_addr);
	if (enc->firmware.hdl < 0) {
		mpp_err("error: alloc firmware buffer error\n");
		goto FAIL;
	}
	enc->firmware.size = ALIGN(firmware->size, MPP_ALIGN_SIZE);
	enc->firmware_cpu_addr = vpu_iommu_map_kernel(mpp->iommu_info,
						      session,
						      enc->firmware.hdl);
	mpp_debug(DEBUG_H265E_INFO,
		  "firmware_buffer_size = %d,firmware size = %zd,code_base = %x\n",
		  size, firmware->size, (u32)enc->firmware.dma_addr);
	memcpy(enc->firmware_cpu_addr, firmware->data, firmware->size);
	release_firmware(firmware);
	return 0;
FAIL:
	release_firmware(firmware);
	if (enc->firmware.hdl >= 0) {
		vpu_iommu_unmap_kernel(mpp->iommu_info, session,
				       enc->firmware.hdl);
		vpu_iommu_free(mpp->iommu_info, session, enc->firmware.hdl);
	}
	return -1;
}

static struct mpp_ctx *rockchip_mpp_h265e_ctx_init(struct rockchip_mpp_dev *mpp,
						   struct mpp_session *session,
						   void __user *src, u32 dwsize)
{
	struct h265e_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct mpp_mem_region *mem_bs_region = NULL;
	struct mpp_mem_region *mem_src_region = NULL;
	unsigned long size = 0;
	int ret = 0;

	mutex_lock(&enc->lock);
	mpp_debug_enter();
	if (!ctx) {
		mutex_unlock(&enc->lock);
		return NULL;
	}
	mpp_dev_common_ctx_init(mpp, &ctx->ictx);
	ctx->ictx.session = session;
	if (copy_from_user(&ctx->cfg, src, dwsize)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		kfree(ctx);
		mutex_unlock(&enc->lock);
		return NULL;
	}
#if H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	ctx->bs.hdl = vpu_iommu_import(mpp->iommu_info, session,
				       ctx->cfg.bs_fd);
	if (ctx->bs.hdl < 0) {
		mpp_err("import dma-buf from fd %d failed\n", ctx->cfg.bs_fd);
		mutex_unlock(&enc->lock);
		return NULL;
	}

	ret = vpu_iommu_map_iommu(mpp->iommu_info, session,
				  ctx->bs.hdl, &ctx->bs.dma_addr, &size);
	ctx->bs.size = (u32)size;
#if PRINT_BS_DATA
	ctx->bs_data = vpu_iommu_map_kernel(mpp->iommu_info, session,
					    ctx->bs.hdl);
#endif

	if (ret < 0) {
		mpp_err("bs fd %d ion map iommu failed\n", ctx->cfg.bs_fd);
		goto FAIL;
	}

	ctx->src.hdl = vpu_iommu_import(mpp->iommu_info, session,
					ctx->cfg.src_fd);
	if (ctx->src.hdl < 0) {
		mpp_err("import dma-buf from fd %d failed\n", ctx->cfg.src_fd);
		goto FAIL;
	}
	ret = vpu_iommu_map_iommu(mpp->iommu_info, session,
				  ctx->src.hdl, &ctx->src.dma_addr, &size);
	ctx->src.size = (u32)size;

	if (ret < 0) {
		mpp_err("source fd %d ion map iommu failed\n", ctx->cfg.src_fd);
		goto FAIL;
	}

	mem_bs_region = kzalloc(sizeof(*mem_bs_region), GFP_KERNEL);
	if (!mem_bs_region)
		goto FAIL;
	mem_src_region = kzalloc(sizeof(*mem_src_region), GFP_KERNEL);
	if (!mem_src_region)
		goto FAIL;
	mem_bs_region->hdl = ctx->bs.hdl;
	INIT_LIST_HEAD(&mem_bs_region->reg_lnk);
	list_add_tail(&mem_bs_region->reg_lnk, &ctx->ictx.mem_region_list);

	mem_src_region->hdl = ctx->src.hdl;
	INIT_LIST_HEAD(&mem_src_region->reg_lnk);
	list_add_tail(&mem_src_region->reg_lnk, &ctx->ictx.mem_region_list);

	ctx->mode = H265E_MODE_ONEFRAME;
	mpp_debug_leave();
	mutex_unlock(&enc->lock);

	return &ctx->ictx;

FAIL:
	if (ctx->bs.hdl >= 0) {
		vpu_iommu_unmap_kernel(mpp->iommu_info, session, ctx->bs.hdl);
		vpu_iommu_free(mpp->iommu_info, session, ctx->bs.hdl);
	}

	if (ctx->src.hdl >= 0) {
		vpu_iommu_unmap_kernel(mpp->iommu_info, session, ctx->src.hdl);
		vpu_iommu_free(mpp->iommu_info, session, ctx->src.hdl);
	}

	if (!IS_ERR_OR_NULL(mem_bs_region)) {
		kfree(mem_bs_region);
		mem_bs_region = NULL;
	}

	if (!IS_ERR_OR_NULL(mem_src_region)) {
		kfree(mem_src_region);
		mem_src_region = NULL;
	}

	if (!IS_ERR_OR_NULL(ctx)) {
		kfree(ctx);
		ctx = NULL;
	}
	mutex_unlock(&enc->lock);
	return NULL;
}

static int rockchip_mpp_h265e_run(struct rockchip_mpp_dev *mpp)
{
	struct h265e_ctx *ctx = container_of(mpp_srv_get_current_ctx(mpp->srv),
					     struct h265e_ctx, ictx);
	struct h265e_session *session =
					container_of(ctx->ictx.session,
						     struct h265e_session,
						     isession);
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	int index = session->instance_index;

	mpp_debug_enter();
#ifdef CONFIG_MFD_SYSCON
	if (enc->grf) {
		u32 raw;
		u32 bits = BIT(enc->mode_bit);

		regmap_read(enc->grf, enc->mode_ctrl, &raw);
		regmap_write(enc->grf, enc->mode_ctrl,
			     (raw | bits) | (bits << 16));
	}
#endif
	rockchip_mpp_h265e_encode_one_frame(mpp, ctx, index);
	mpp_debug_leave();
	return 0;
}

static int rockchip_mpp_h265e_done(struct rockchip_mpp_dev *mpp)
{
	struct mpp_ctx *ictx = mpp_srv_get_current_ctx(mpp->srv);
	struct h265e_ctx *ctx = container_of(ictx, struct h265e_ctx, ictx);
	int ret = 0;

	mpp_debug_enter();
	if (IS_ERR_OR_NULL(ictx)) {
		mpp_err("Invaidate context to save result\n");
		return -1;
	}
	ret = rockchip_mpp_h265e_get_encode_result(mpp, ctx);
#if PRINT_BS_DATA
	rockchip_mpp_h265e_write_encoder_file(mpp);
#endif
	mpp_debug_leave();

	return ret;
}

static int rockchip_mpp_h265e_irq(struct rockchip_mpp_dev *mpp)
{
	int reason = -1;

	reason = mpp_read(mpp, H265E_VPU_VINT_REASON_USR);
	mpp_write(mpp, reason, H265E_VPU_VINT_REASON_CLR);
	mpp_write(mpp, 1, H265E_VPU_VINT_CLEAR);
	mpp_write(mpp, 0, H265E_VPU_VINT_REASON_USR);
	if (reason & (1 << INT_BIT_BIT_BUF_FULL))
		return -1;
	else if (reason == 0x02)
		return -1;

	return 0;
}

static int rockchip_mpp_h265e_result(struct rockchip_mpp_dev *mpp,
				     struct mpp_ctx *ictx, u32 __user *dst)
{
	struct h265e_ctx *ctx = container_of(ictx, struct h265e_ctx, ictx);

	switch (ctx->mode) {
	case H265E_MODE_ONEFRAME:
		if (copy_to_user(dst, &ctx->result,
				 sizeof(struct h265e_result))) {
			mpp_err("copy result to user failed\n");
			return -1;
		}
		break;
	default:
		mpp_err("invalid context mode %d\n", (int)ctx->mode);
		return -1;
	}

	return 0;
}

int rockchip_mpp_h265e_get_stream_header(struct rockchip_mpp_dev *mpp,
					 int index,
					 struct hal_h265e_header *head)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);

	struct rockchip_h265e_instance *instance = NULL;
	struct mpp_h265e_cfg *cfg = NULL;
	u32 value = 0;
	u32 address = 0;
	int bs_hd;
	unsigned long bs_address;
	unsigned long bs_size;
	int ret = 0;

	if (index < 0 || index >= H265E_INSTANCE_NUM || !head) {
		mpp_err("index = %d is invalid", index);
		return -1;
	}
	mutex_lock(&enc->lock);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_debug_enter();
	head->size = 0;
	instance = &enc->instance[index];
	cfg = &instance->cfg;
	address = head->buf;

	bs_hd = vpu_iommu_import(mpp->iommu_info, instance->session, address);
	if (bs_hd < 0) {
		mpp_err("import dma-buf from fd %d failed\n", address);
		mutex_unlock(&enc->lock);
		return -1;
	}
	ret = vpu_iommu_map_iommu(mpp->iommu_info,
				  instance->session, bs_hd, &bs_address,
				  &bs_size);
	if (ret < 0) {
		mpp_err("bs fd %d ion map iommu failed\n", address);
		goto FAIL;
	}
	mpp_write(mpp, (u32)bs_address, H265E_BS_START_ADDR);
	mpp_write(mpp, (u32)bs_address, H265E_BS_SIZE);

	mpp_write(mpp, (u32)bs_address, H265E_BS_RD_PTR);
	mpp_write(mpp, (u32)bs_address, H265E_BS_WR_PTR);

	value = (cfg->line_buf_int_en << 6) |
		(cfg->slice_int_enable << 5) |
		(cfg->ring_buffer_enable << 4) |
		cfg->bs_endian;
	mpp_write(mpp, value, H265E_BS_PARAM);
	mpp_write(mpp, 0, H265E_BS_OPTION);

	/* Set up work-buffer */
	mpp_write(mpp, instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, instance->work.size, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);

	/* Set up temp-buffer */
	mpp_write(mpp, enc->temp.dma_addr, H265E_ADDR_TEMP_BASE);
	mpp_write(mpp, enc->temp.size, H265E_TEMP_SIZE);
	mpp_write(mpp, 0, H265E_TEMP_PARAM);

	mpp_write(mpp, 0, H265E_ENC_SRC_PIC_IDX);
	if (cfg->code_option.implicit_header_encode == 1) {
		value = CODEOPT_ENC_HEADER_IMPLICIT |
			CODEOPT_ENC_VCL |
			(cfg->code_option.encode_aud << 5) |
			(cfg->code_option.encode_eos << 6) |
			(cfg->code_option.encode_eob << 7);
	} else {
		value = (cfg->code_option.implicit_header_encode << 0) |
			(cfg->code_option.encode_vcl << 1) |
			(cfg->code_option.encode_vps << 2) |
			(cfg->code_option.encode_sps << 3) |
			(cfg->code_option.encode_pps << 4) |
			(cfg->code_option.encode_aud << 5) |
			(cfg->code_option.encode_eos << 6) |
			(cfg->code_option.encode_eob << 7) |
			(cfg->code_option.encode_vui << 9);
	}
	mpp_write(mpp, value, H265E_CMD_ENC_CODE_OPTION);
	rockchip_mpp_h265e_issue_command(mpp, index, H265E_CMD_ENC_PIC);
	if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
		mpp_err("read return register fail\n");
		goto FAIL;
	}
	head->size = mpp_read(mpp, H265E_RET_ENC_PIC_BYTE);
	mpp_debug(DEBUG_H265E_INFO, "%s %d head->size=%d\n",
		  __func__, __LINE__, head->size);
	if (bs_hd >= 0)
		vpu_iommu_free(mpp->iommu_info, instance->session, bs_hd);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();

	return 0;
FAIL:
	if (bs_hd >= 0)
		vpu_iommu_free(mpp->iommu_info, instance->session, bs_hd);
	mutex_unlock(&enc->lock);
	mpp_err("fail, index = %d\n", index);
	return -1;
}

/*
 * set/change common parameter
 * when first run this function ,the cfg_mask is 0xffffffff,
 * and to configure all parameter.
 * when dynamically changed in the encoding process,
 * the configure register according to cfg_mask
 */
static int rockchip_mpp_h265e_set_common_parameter(struct rockchip_mpp_dev *mpp,
						   int index)
{
	u32 value = 0;
	struct mpp_h265e_cfg *cfg = NULL;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = NULL;

	mpp_debug_enter();
	if (!mpp || index < 0 || index >= H265E_INSTANCE_NUM) {
		mpp_err("param is invalid,index = %d", index);
		return -1;
	}

	instance = &enc->instance[index];
	cfg = &instance->cfg;

	mpp_write(mpp, 0x00010000 | index, H265E_INST_INDEX);
	mpp_write(mpp, (cfg->line_buf_int_en << 6) |
		  (cfg->slice_int_enable << 5) |
		  (cfg->ring_buffer_enable << 4) |
		  cfg->bs_endian, H265E_BS_PARAM);
	mpp_debug(DEBUG_H265E_INFO, "%s %d W=%d,H=%d,index=%d\n",
		  __func__, __LINE__,
		  cfg->width, cfg->height, index);

	/* Set up work-buffer */
	mpp_write(mpp, (u32)instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, instance->work.size, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);

	/* Set up temp-buffer */
	mpp_write(mpp, (u32)enc->temp.dma_addr, H265E_ADDR_TEMP_BASE);
	mpp_write(mpp, enc->temp.size, H265E_TEMP_SIZE);
	mpp_write(mpp, 0, H265E_TEMP_PARAM);
	/* Secondary AXI */
#if	H265E_AXI_STATUS
	mpp_write(mpp, 0x0, H265E_ADDR_SEC_AXI_BASE);
	mpp_write(mpp, H265E_SEC_AXI_BUF_SIZE, H265E_SEC_AXI_SIZE);
	mpp_write(mpp, 0xffff, H265E_USE_SEC_AXI);
#else
	mpp_write(mpp, 0, H265E_USE_SEC_AXI);
#endif

	/* Set up BitstreamBuffer */
	mpp_write(mpp, 0, H265E_BS_START_ADDR);
	mpp_write(mpp, 0, H265E_BS_SIZE);
	mpp_write(mpp, 0, H265E_BS_RD_PTR);
	mpp_write(mpp, 0, H265E_BS_WR_PTR);

	/* SET_PARAM + COMMON */
	mpp_write(mpp, H265E_OPT_COMMON, H265E_ENC_SET_PARAM_OPTION);
	mpp_write(mpp, (u32)cfg->cfg_mask, H265E_ENC_SET_PARAM_ENABLE);

	if (cfg->cfg_mask & H265E_CFG_SEQ_SRC_SIZE_CHANGE) {
		value = ((cfg->height << 16) | cfg->width);
		mpp_write(mpp, value, H265E_ENC_SEQ_SRC_SIZE);
	}

	if (cfg->cfg_mask & H265E_CFG_SEQ_PARAM_CHANGE) {
		/* set seq parameter*/
		value = (cfg->profile << 0) |
			(cfg->level << 3) |
			(cfg->tier << 12) |
			(cfg->bit_depth << 14) |
			(cfg->chroma_idc << 18) |
			(cfg->lossless_enable << 20) |
			(cfg->const_intra_pred_flag << 21) |
			((cfg->chroma_cb_qp_offset & 0x1f) << 22) |
			((cfg->chroma_cr_qp_offset & 0x1f) << 27);
		mpp_write(mpp, value, H265E_ENC_SEQ_PARAM);
	}

	if (cfg->cfg_mask & H265E_CFG_GOP_PARAM_CHANGE)
		mpp_write(mpp, cfg->gop_idx, H265E_ENC_SEQ_GOP_PARAM);
	if (cfg->cfg_mask & H265E_CFG_INTRA_PARAM_CHANGE) {
		value = (cfg->decoding_refresh_type << 0) |
			(cfg->intra_qp << 3) |
			(cfg->intra_period << 16);
		mpp_write(mpp, value, H265E_ENC_SEQ_INTRA_PARAM);
	}

	if (cfg->cfg_mask & H265E_CFG_CONF_WIN_TOP_BOT_CHANGE) {
		value = (cfg->conf_win_bot << 16) | cfg->conf_win_top;
		mpp_write(mpp, value, H265E_ENC_SEQ_CONF_WIN_TOP_BOT);
	}

	if (cfg->cfg_mask & H265E_CFG_CONF_WIN_LEFT_RIGHT_CHANGE) {
		value = (cfg->conf_win_right << 16) | cfg->conf_win_left;
		mpp_write(mpp, value, H265E_ENC_SEQ_CONF_WIN_LEFT_RIGHT);
	}
	if (cfg->cfg_mask & H265E_CFG_FRAME_RATE_CHANGE)
		mpp_write(mpp, cfg->frame_rate, H265E_ENC_SEQ_FRAME_RATE);

	if (cfg->cfg_mask & H265E_CFG_INDEPENDENT_SLICE_CHANGE) {
		value = (cfg->independ_slice_mode_arg << 16) |
			cfg->independ_slice_mode;
		mpp_write(mpp, value, H265E_ENC_SEQ_INDEPENDENT_SLICE);
	}

	if (cfg->cfg_mask & H265E_CFG_DEPENDENT_SLICE_CHANGE) {
		value = (cfg->depend_slice_mode_arg << 16) |
			cfg->depend_slice_mode;
		mpp_write(mpp, value, H265E_ENC_SEQ_DEPENDENT_SLICE);
	}

	if (cfg->cfg_mask & H265E_CFG_INTRA_REFRESH_CHANGE) {
		value = (cfg->intra_refresh_arg << 16) |
			cfg->intra_refresh_mode;
		mpp_write(mpp, value, H265E_ENC_SEQ_INTRA_REFRESH);
	}

	if (cfg->cfg_mask & H265E_CFG_PARAM_CHANGE) {
		value = (cfg->use_recommend_param) |
			(cfg->ctu.ctu_qp_enable << 2) |
			(cfg->scaling_list_enable << 3) |
			(cfg->cu_size_mode << 4) |
			(cfg->tmvp_enable << 7) |
			(cfg->wpp_enable << 8) |
			(cfg->max_num_merge << 9) |
			(cfg->dynamic_merge_8x8_enable << 12) |
			(cfg->dynamic_merge_16x16_enable << 13) |
			(cfg->dynamic_merge_32x32_enable << 14) |
			(cfg->disable_deblk << 15) |
			(cfg->lf_cross_slice_boundary_enable << 16) |
			((cfg->beta_offset_div2 & 0xF) << 17) |
			((cfg->tc_offset_div2 & 0xF) << 21) |
			(cfg->skip_intra_trans << 25) |
			(cfg->sao_enable << 26) |
			(cfg->intra_in_inter_slice_enable << 27) |
			(cfg->intra_nxn_enable << 28);

		mpp_write(mpp, value, H265E_ENC_PARAM);
	}

	if (cfg->cfg_mask & H265E_CFG_RC_PARAM_CHANGE) {
		value = (cfg->rc_enable << 0) |
			(cfg->cu_level_rc_enable << 1) |
			(cfg->hvs_qp_enable << 2) |
			(cfg->hvs_qp_scale_enable << 3) |
			(cfg->hvs_qp_scale << 4) |
			(cfg->bit_alloc_mode << 7) |
			(cfg->init_buf_levelx8 << 9) |
			(cfg->ctu.roi_enable << 13) |
			(cfg->initial_rc_qp << 14) |
			(cfg->initial_delay << 20);
		mpp_write(mpp, value, H265E_ENC_RC_PARAM);
	}

	if (cfg->cfg_mask & H265E_CFG_RC_MIN_MAX_QP_CHANGE) {
		value = (cfg->min_qp << 0) |
			(cfg->max_qp << 6) |
			(cfg->max_delta_qp << 12) |
			((cfg->intra_qp_offset & 0xFFFF) << 18);
		mpp_write(mpp, value, H265E_ENC_RC_MIN_MAX_QP);
	}

	if (cfg->cfg_mask & H265E_CFG_RC_TARGET_RATE_LAYER_0_3_CHANGE) {
		value = (cfg->fixed_bit_ratio[0] << 0) |
			(cfg->fixed_bit_ratio[1] << 8) |
			(cfg->fixed_bit_ratio[2] << 16) |
			(cfg->fixed_bit_ratio[3] << 24);
		mpp_write(mpp, value, H265E_ENC_RC_BIT_RATIO_LAYER_0_3);
	}

	if (cfg->cfg_mask & H265E_CFG_RC_TARGET_RATE_LAYER_4_7_CHANGE) {
		value = (cfg->fixed_bit_ratio[4] << 0) |
			(cfg->fixed_bit_ratio[5] << 8) |
			(cfg->fixed_bit_ratio[6] << 16) |
			(cfg->fixed_bit_ratio[7] << 24);
		mpp_write(mpp, value, H265E_ENC_RC_BIT_RATIO_LAYER_4_7);
	}

	if (cfg->cfg_mask & H265E_CFG_SET_NUM_UNITS_IN_TICK) {
		mpp_write(mpp, cfg->num_units_in_tick,
			  H265E_ENC_NUM_UNITS_IN_TICK);
	}

	if (cfg->cfg_mask & H265E_CFG_SET_TIME_SCALE) {
		mpp_write(mpp, cfg->time_scale,
			  H265E_ENC_TIME_SCALE);
	}

	if (cfg->cfg_mask & H265E_CFG_SET_NUM_TICKS_POC_DIFF_ONE) {
		mpp_write(mpp, cfg->num_ticks_poc_diff_one,
			  H265E_ENC_NUM_TICKS_POC_DIFF_ONE);
	}

	if (cfg->cfg_mask & H265E_CFG_NR_PARAM_CHANGE) {
		value = (cfg->nr_y_enable << 0) |
			(cfg->nr_cb_enable << 1) |
			(cfg->nr_cr_enable << 2) |
			(cfg->nr_noise_est_enable << 3) |
			(cfg->nr_noise_sigma_y << 4) |
			(cfg->nr_noise_sigma_cb << 12) |
			(cfg->nr_noise_sigma_cr << 20);
		mpp_write(mpp, value, H265E_ENC_NR_PARAM);
	}

	if (cfg->cfg_mask & H265E_CFG_NR_WEIGHT_CHANGE) {
		value = (cfg->nr_intra_weight_y << 0) |
			(cfg->nr_intra_weight_cb << 5) |
			(cfg->nr_intra_weight_cr << 10) |
			(cfg->nr_inter_weight_y << 15) |
			(cfg->nr_inter_weight_cb << 20) |
			(cfg->nr_inter_weight_cr << 25);
		mpp_write(mpp, value, H265E_ENC_NR_WEIGHT);
	}
	if (cfg->cfg_mask & H265E_CFG_RC_TARGET_RATE_CHANGE)
		mpp_write(mpp, cfg->bit_rate, H265E_ENC_RC_TARGET_RATE);
	if (cfg->cfg_mask & H265E_CFG_RC_TRANS_RATE_CHANGE)
		mpp_write(mpp, cfg->trans_rate, H265E_ENC_RC_TRANS_RATE);
	if (cfg->cfg_mask & H265E_CFG_ROT_PARAM_CHANGE)
		mpp_write(mpp, 0, H265E_ENC_ROT_PARAM);
	if (cfg->cfg_mask == H265E_CFG_CHANGE_SET_PARAM_ALL) {
		value = (cfg->intra_max_qp << 6) | cfg->intra_min_qp;
		mpp_write(mpp, value, H265E_ENC_RC_INTRA_MIN_MAX_QP);
	}
	rockchip_mpp_h265e_issue_command(mpp, index, H265E_CMD_SET_PARAM);
	if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
		mpp_err("h265e_wait_busy timeout, index=%d\n", index);
		goto FAIL;
	}
	if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
		mpp_err("h265e set common parameter ret fail\n");
		goto FAIL;
	}
	mpp_debug_leave();
	return 0;
FAIL:
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static int rockchip_mpp_h265e_set_gop_parameter(struct rockchip_mpp_dev *mpp,
						int index)
{
	u32 value = 0;
	int int_reason = 0;
	int i = 0, j = 0;
	struct mpp_h265e_cfg *cfg = NULL;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = NULL;

	mpp_debug_enter();

	if (!mpp || index < 0 || index >= H265E_INSTANCE_NUM) {
		mpp_err("param is invalid,index = %d", index);
		return -1;
	}

	instance = &enc->instance[index];
	cfg = &instance->cfg;

	/* Set up work-buffer */
	mpp_write(mpp, (u32)instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, instance->work.size, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);

	/* Set up temp-buffer */
	mpp_write(mpp, (u32)enc->temp.dma_addr, H265E_ADDR_TEMP_BASE);
	mpp_write(mpp, enc->temp.size, H265E_TEMP_SIZE);
	mpp_write(mpp, 0, H265E_TEMP_PARAM);
	/* Secondary AXI */
#if	H265E_AXI_STATUS
	mpp_write(mpp, 0x0, H265E_ADDR_SEC_AXI_BASE);
	mpp_write(mpp, H265E_SEC_AXI_BUF_SIZE, H265E_SEC_AXI_SIZE);
	mpp_write(mpp, 0xffff, H265E_USE_SEC_AXI);
#else
	mpp_write(mpp, 0, H265E_USE_SEC_AXI);
#endif

	/*
	 * SET_PARAM + CUSTOM_GOP
	 * only when gop_size == custom_gop,
	 * custom_gop related registers should be set
	 */
	mpp_write(mpp, 0x00010000 | index, H265E_INST_INDEX);
	int_reason = 0;
	if (cfg->gop_idx == PRESET_IDX_CUSTOM_GOP) {
		mpp_write(mpp, H265E_OPT_CUSTOM_GOP,
			  H265E_ENC_SET_PARAM_OPTION);
		mpp_write(mpp, (u32)H265E_CFG_CHANGE_SET_PARAM_ALL,
			  H265E_ENC_SET_CUSTOM_GOP_ENABLE);

		value = (cfg->gop.custom_gop_size << 0) |
			(cfg->gop.use_derive_lambda_weight << 4);
		mpp_write(mpp, value, H265E_ENC_CUSTOM_GOP_PARAM);

		for (i = 0; i < cfg->gop.custom_gop_size; i++) {
			value = (cfg->gop.pic[i].type << 0) |
				(cfg->gop.pic[i].offset << 2) |
				(cfg->gop.pic[i].qp << 6) |
				((cfg->gop.pic[i].ref_poc_l0 &
				  0x1F) << 14) |
				((cfg->gop.pic[i].ref_poc_l1 &
				  0x1F) << 19) |
				(cfg->gop.pic[i].temporal_id << 24);

			mpp_write(mpp, value,
				  H265E_ENC_CUSTOM_GOP_PIC_PARAM_0 + (i * 4));
			mpp_write(mpp, cfg->gop.gop_pic_lambda[i],
				  H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_0 + (i * 4));
		}
		for (j = i; j < H265E_MAX_GOP_NUM; j++) {
			mpp_write(mpp, 0,
				  H265E_ENC_CUSTOM_GOP_PIC_PARAM_0 + (j * 4));
			mpp_write(mpp, 0,
				  H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_0 + (j * 4));
		}
		rockchip_mpp_h265e_issue_command(mpp,
						 index,
						 H265E_CMD_SET_PARAM);
		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("h265e_wait_busy timeout, index=%d\n", index);
			goto FAIL;
		}
		if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
			mpp_err("h265e set gop ret fail\n");
			goto FAIL;
		}
	}

	value = mpp_read(mpp, H265E_RET_ENC_MIN_FB_NUM);
	if (value > instance->min_frame_buffer_count)
		instance->min_frame_buffer_count = value;

	value = mpp_read(mpp, H265E_RET_ENC_MIN_SRC_BUF_NUM);
	if (value > instance->min_src_frame_count)
		instance->min_src_frame_count = value;
	mpp_debug(DEBUG_H265E_INFO,
		  "%s %d,min_frame_buffer_count = %d,min_src_frame_count=%d\n",
		  __func__, __LINE__, instance->min_frame_buffer_count,
		  instance->min_src_frame_count);
	mpp_debug_leave();
	return 0;

FAIL:
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static int rockchip_mpp_h265e_set_vui_parameter(struct rockchip_mpp_dev *mpp,
						int index)
{
	struct mpp_h265e_cfg *cfg = NULL;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = NULL;
	u32 value = 0;

	mpp_debug_enter();
	if (!mpp || index < 0 || index >= H265E_INSTANCE_NUM) {
		mpp_err("param is invalid,index = %d", index);
		return -1;
	}

	instance = &enc->instance[index];
	cfg = &instance->cfg;
	mpp_write(mpp, 0x00010000 | index, H265E_INST_INDEX);

	/* Set up work-buffer */
	mpp_write(mpp, (u32)instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, instance->work.size, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);

	if (cfg->vui.flags || cfg->vui_rbsp ||
	    cfg->hrd_rbsp_in_vps || cfg->hrd_rbsp_in_vui) {
		/*** VUI encoding by host registers ***/
		if (cfg->vui.flags) {
			mpp_write(mpp, H265E_OPT_VUI,
				  H265E_ENC_SET_PARAM_OPTION);
			mpp_write(mpp, cfg->vui.flags,
				  H265E_ENC_VUI_PARAM_FLAGS);
			mpp_write(mpp, cfg->vui.aspect_ratio_idc,
				  H265E_ENC_VUI_ASPECT_RATIO_IDC);
			mpp_write(mpp, cfg->vui.sar_size,
				  H265E_ENC_VUI_SAR_SIZE);
			mpp_write(mpp, cfg->vui.over_scan_appropriate,
				  H265E_ENC_VUI_OVERSCAN_APPROPRIATE);
			mpp_write(mpp, cfg->vui.signal,
				  H265E_ENC_VUI_VIDEO_SIGNAL);
			mpp_write(mpp, cfg->vui.chroma_sample_loc,
				  H265E_ENC_VUI_CHROMA_SAMPLE_LOC);
			mpp_write(mpp, cfg->vui.disp_win_left_right,
				  H265E_ENC_VUI_DISP_WIN_LEFT_RIGHT);
			mpp_write(mpp, cfg->vui.disp_win_top_bottom,
				  H265E_ENC_VUI_DISP_WIN_TOP_BOT);
		} else {
			mpp_write(mpp, 0, H265E_ENC_VUI_PARAM_FLAGS);
		}
		if (cfg->vui_rbsp ||
		    cfg->hrd_rbsp_in_vps ||
		    cfg->hrd_rbsp_in_vui) {
			/*** VUI encoding by given rbsp data ***/
			mpp_write(mpp, H265E_OPT_VUI,
				  H265E_ENC_SET_PARAM_OPTION);
			value = (cfg->hrd_rbsp_in_vps << 2) |
				(cfg->hrd_rbsp_in_vui << 1) |
				(cfg->vui_rbsp);
			mpp_write(mpp, value,
				  H265E_ENC_VUI_HRD_RBSP_PARAM_FLAG);
			mpp_write(mpp, cfg->vui_rbsp_data_addr,
				  H265E_ENC_VUI_RBSP_ADDR);
			mpp_write(mpp, cfg->vui_rbsp_data_size,
				  H265E_ENC_VUI_RBSP_SIZE);
			mpp_write(mpp, cfg->hrd_rbsp_data_addr,
				  H265E_ENC_HRD_RBSP_ADDR);
			mpp_write(mpp, cfg->hrd_rbsp_data_size,
				  H265E_ENC_HRD_RBSP_SIZE);
		} else {
			mpp_write(mpp, 0, H265E_ENC_VUI_HRD_RBSP_PARAM_FLAG);
		}
		rockchip_mpp_h265e_issue_command(mpp,
						 index,
						 H265E_CMD_SET_PARAM);
		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("h265e_wait_busy timeout, index=%d\n", index);
			goto FAIL;
		}
		if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
			mpp_err("h265e set vui ret fail\n");
			goto FAIL;
		}
	}

	mpp_debug_leave();
	return 0;
FAIL:
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static int rockchip_mpp_h265e_set_parameter(struct rockchip_mpp_dev *mpp,
					    int index)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = &enc->instance[index];

	mpp_debug_enter();
	mutex_lock(&enc->lock);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_dev_power_on(mpp);
	if (instance->status != H265E_INSTANCE_STATUS_OPENED) {
		mpp_err("error:status = %d\n", instance->status);
		goto FAIL;
	}
	instance->cfg.cfg_mask = H265E_CFG_CHANGE_SET_PARAM_ALL;
	if (rockchip_mpp_h265e_set_common_parameter(mpp, index) != 0)
		goto FAIL;
	if (rockchip_mpp_h265e_set_gop_parameter(mpp, index) != 0)
		goto FAIL;
	if (rockchip_mpp_h265e_set_vui_parameter(mpp, index) != 0)
		goto FAIL;
	if (rockchip_mpp_h265e_register_frame_buffer(mpp, index) != 0)
		goto FAIL;
	instance->status = H265E_INSTANCE_STATUS_SET_PARAMETER;
	instance->cfg.cfg_mask = 0;
	instance->cfg.cfg_option = 0;
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
	return 0;
FAIL:
	instance->status = H265E_INSTANCE_STATUS_ERROR;
	mutex_unlock(&enc->lock);
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static int rockchip_mpp_h265e_change_parameter(struct rockchip_mpp_dev *mpp,
					       int index)
{
	struct rockchip_h265e_dev *enc =
					container_of(mpp,
						     struct rockchip_h265e_dev,
						     dev);
	struct rockchip_h265e_instance *instance = &enc->instance[index];
	u32 enable = instance->cfg.cfg_option;

	mpp_debug_enter();
	mutex_lock(&enc->lock);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_dev_power_on(mpp);
	if (instance->status == H265E_INSTANCE_STATUS_ERROR ||
	    instance->status == H265E_INSTANCE_STATUS_CLOSE) {
		mpp_err("error:status = %d\n", instance->status);
		goto FAIL;
	}

	instance->status = H265E_INSTANCE_STATUS_OPENED;
	if (enable & H265E_PARAM_CHANEGED_COMMON) {
		if (rockchip_mpp_h265e_set_common_parameter(mpp, index) != 0)
			goto FAIL;
	}
	if (enable & H265E_PARAM_CHANEGED_CUSTOM_GOP) {
		if (rockchip_mpp_h265e_set_gop_parameter(mpp, index) != 0)
			goto FAIL;
	}
	if (enable & H265E_PARAM_CHANEGED_REGISTER_BUFFER) {
		rockchip_mpp_h265e_free_frame_buffer(mpp, instance);
		if (rockchip_mpp_h265e_register_frame_buffer(mpp, index) != 0)
			goto FAIL;
	}
	instance->status = H265E_INSTANCE_STATUS_SET_PARAMETER;
	instance->cfg.cfg_mask = 0;
	instance->cfg.cfg_option = 0;
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
	return 0;
FAIL:
	instance->status = H265E_INSTANCE_STATUS_ERROR;
	mutex_unlock(&enc->lock);
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static u32 rockchip_mpp_h265e_get_fb_luma_size(u32 map_type,
					       u32 stride, u32 height)
{
	u32 size = stride * height;

	if (map_type == LINEAR_FRAME_MAP)
		size = stride * height;
	else if (map_type == COMPRESSED_FRAME_MAP)
		size = stride * height;
	else if (map_type == TILED_SUB_CTU_MAP)
		size = (ALIGN(stride, 32) * ALIGN(height, 32));
	else
		mpp_err("unsupport may_type = %d\n", map_type);

	return size;
}

static u32 rockchip_mpp_h265e_get_fb_chroma_size(u32 map_type,
						 u32 stride, u32 height)
{
	u32 size = 0;
	u32 chroma_width = stride >> 1;
	u32 chroma_height = height >> 1;

	if (map_type == LINEAR_FRAME_MAP) {
		size = chroma_width * chroma_height;
	} else if (map_type == COMPRESSED_FRAME_MAP) {
		chroma_width = ALIGN(chroma_width, 16);
		size = chroma_width * chroma_height;
	} else if (map_type == TILED_SUB_CTU_MAP) {
		chroma_width = ALIGN(chroma_width, 16);
		size = chroma_width * chroma_height / 2;
	} else {
		mpp_err("unsupport may_type = %d\n", map_type);
	}

	return size;
}

static
int rockchip_mpp_h265e_register_frame_buffer(struct rockchip_mpp_dev *mpp,
					     int index)
{
	struct mpp_h265e_cfg *cfg = NULL;
	struct rockchip_h265e_dev *enc =
				container_of(mpp,
					     struct rockchip_h265e_dev,
					     dev);
	struct rockchip_h265e_instance *instance = NULL;
	int buf_width = 0;
	int buf_height = 0;
	int luma_stride = 0;
	int chroma_stride = 0;
	int count = 0;
	u32 value, mv_col_size;
	u32 fbc_y_table_size = 0, fbc_c_table_size = 0, sub_sampled_size = 0;
	int q, j, i, remain, idx;
	int start_no, end_no;
	u32 addr_y, addr_cb, addr_cr;
	int stride;
	u32 axi_id = 0;
	int	size_rec_luma, size_rec_chroma;
	struct mpp_h265e_buffer *buffer = NULL;
	int interlace = 0;
	struct mpp_h265e_frame_buffer *frame_buffer = NULL;

	mpp_debug_enter();
	if (!mpp || index < 0 || index >= H265E_INSTANCE_NUM) {
		mpp_err("parameter is invalid, index = %d\n", index);
		return -1;
	}
	instance = &enc->instance[index];
	cfg = &instance->cfg;
	interlace = (cfg->src_format == H265E_SRC_YUV_420_NV12) ||
		    (cfg->src_format == H265E_SRC_YUV_420_NV21);

	stride = ALIGN(cfg->width, 32);

	buf_width = ALIGN(cfg->width, 8);
	buf_height = ALIGN(cfg->height, 8);

	size_rec_luma =
			rockchip_mpp_h265e_get_fb_luma_size(cfg->map_type,
							    stride, buf_height);
	size_rec_chroma =
			  rockchip_mpp_h265e_get_fb_chroma_size(cfg->map_type,
								stride,
								buf_height);

	count = instance->min_frame_buffer_count;
	memset(&instance->mv, 0, sizeof(struct mpp_h265e_buffer));
	memset(&instance->fbc_luma, 0, sizeof(struct mpp_h265e_buffer));
	memset(&instance->fbc_chroma, 0, sizeof(struct mpp_h265e_buffer));
	if (cfg->map_type == COMPRESSED_FRAME_MAP) {
		mv_col_size = H265E_MVCOL_BUF_SIZE(buf_width, buf_height);
		mv_col_size = ALIGN(mv_col_size, 16);

		instance->mv.size = ALIGN(mv_col_size * count, 4096) + 4096;
		instance->mv.hdl =
			rockchip_mpp_h265e_dma_alloc(mpp,
						     instance->session,
						     instance->mv.size,
						     MPP_ALIGN_SIZE,
						     &instance->mv.dma_addr);
		if (instance->mv.hdl < 0) {
			mpp_err("alloc mv buffer fail,index = %d\n", index);
			goto FAIL;
		}

		fbc_y_table_size =
			H265E_FBC_LUMA_TABLE_SIZE(buf_width,
						  buf_height);
		fbc_y_table_size =
			ALIGN(fbc_y_table_size, 16);
		instance->fbc_luma.size =
			ALIGN(fbc_y_table_size * count, 4096) + 4096;
		instance->fbc_luma.hdl =
			rockchip_mpp_h265e_dma_alloc(mpp,
						     instance->session,
						     instance->fbc_luma.size,
						     MPP_ALIGN_SIZE,
						     &instance->fbc_luma.dma_addr);
		if (instance->fbc_luma.hdl < 0) {
			mpp_err("alloc fbc y buffer fail,index = %d\n", index);
			goto FAIL;
		}

		fbc_c_table_size =
				H265E_FBC_CHROMA_TABLE_SIZE(buf_width,
							    buf_height);
		fbc_c_table_size = ALIGN(fbc_c_table_size, 16);
		instance->fbc_chroma.size = ALIGN(fbc_c_table_size * count,
						  4096) + 4096;
		instance->fbc_chroma.hdl =
			rockchip_mpp_h265e_dma_alloc(mpp,
						     instance->session,
						     instance->fbc_chroma.size,
						     MPP_ALIGN_SIZE,
						     &instance->fbc_chroma.dma_addr);
		if (instance->fbc_chroma.hdl < 0) {
			mpp_err("alloc fbc c buffer fail,index = %d\n", index);
			goto FAIL;
		}
	}

	sub_sampled_size = H265E_SUBSAMPLED_ONE_SIZE(buf_width, buf_height);
	memset(&instance->sub_sample, 0, sizeof(struct mpp_h265e_buffer));
	instance->sub_sample.size =
		ALIGN(sub_sampled_size * count, 4096) + 4096;
	instance->sub_sample.hdl =
		rockchip_mpp_h265e_dma_alloc(mpp,
					     instance->session,
					     instance->sub_sample.size,
					     MPP_ALIGN_SIZE,
					     &instance->sub_sample.dma_addr);
	if (instance->sub_sample.hdl < 0) {
		mpp_err("alloc fbc c buffer fail,index = %d\n", index);
		goto FAIL;
	}
	mpp_write(mpp, (u32)instance->sub_sample.dma_addr,
		  H265E_ADDR_SUB_SAMPLED_FB_BASE);
	mpp_write(mpp, sub_sampled_size, H265E_SUB_SAMPLED_ONE_FB_SIZE);

	value = (buf_width << 16) | buf_height;
	mpp_write(mpp, value, H265E_PIC_SIZE);

	luma_stride = ALIGN(cfg->width, 16) * 4;
	luma_stride = ALIGN(luma_stride, 32);
	chroma_stride = ALIGN(cfg->width / 2, 16) * 4;
	chroma_stride = ALIGN(chroma_stride, 32);
	value = (luma_stride << 16) | chroma_stride;
	mpp_write(mpp, value, H265E_FBC_STRIDE);

	value = ((cfg->src_format == H265E_SRC_YUV_420_NV21) << 29) |
		((cfg->map_type == LINEAR_FRAME_MAP) << 28) |
		(axi_id << 24) |
		(interlace << 16) |
		stride;
	mpp_write(mpp, value, H265E_COMMON_PIC_INFO);

	memset(&instance->frame_buffer, 0, sizeof(instance->frame_buffer));
	/* set frame buffer address*/
	for (i = 0; i < count; i++) {
		frame_buffer = &instance->frame_buffer[i];
		buffer = &frame_buffer->buffer;
		buffer->size = size_rec_luma + 2 * size_rec_chroma;
		buffer->hdl = rockchip_mpp_h265e_dma_alloc(mpp,
							   instance->session,
							   buffer->size,
							   MPP_ALIGN_SIZE,
							   &buffer->dma_addr);
		if (buffer->hdl < 0) {
			mpp_err("alloc fbc y buffer fail,index = %d\n", index);
			goto FAIL;
		}

		frame_buffer->y = (u32)buffer->dma_addr;
		frame_buffer->cb = frame_buffer->y + size_rec_luma;
		frame_buffer->cr = frame_buffer->cb + size_rec_chroma;
	}

	remain = count;
	q      = (remain + 7) / 8;
	idx    = 0;
	for (j = 0; j < q; j++) {
		value = (cfg->fb_endian << 16) |
			((j == q - 1) << 4) |
			((j == 0) << 3);
		mpp_write(mpp, value, H265E_SFB_OPTION);
		start_no = j * 8;
		end_no   = start_no + (remain >= 8 ? 8 : remain) - 1;
		value = (start_no << 8) | end_no;
		mpp_write(mpp, value, H265E_SET_FB_NUM);
		for (i = 0; i < 8 && i < remain; i++) {
			frame_buffer = &instance->frame_buffer[i];
			addr_y  = frame_buffer->y;
			addr_cb = frame_buffer->cb;
			addr_cr = frame_buffer->cr;
			mpp_write(mpp, addr_y,
				  H265E_ADDR_LUMA_BASE0 + (i << 4));
			mpp_write(mpp, addr_cb,
				  H265E_ADDR_CB_BASE0 + (i << 4));
			if (cfg->map_type == COMPRESSED_FRAME_MAP) {
				mpp_write(mpp,
					  ((u32)instance->fbc_luma.dma_addr) +
					  idx * fbc_y_table_size,
					  H265E_ADDR_FBC_Y_OFFSET0 + (i << 4));
				mpp_write(mpp,
					  ((u32)instance->fbc_chroma.dma_addr) +
					  idx * fbc_c_table_size,
					  H265E_ADDR_FBC_C_OFFSET0 + (i << 4));
				mpp_write(mpp, ((u32)instance->mv.dma_addr) +
					  idx * mv_col_size,
					  H265E_ADDR_MV_COL0 + (i << 2));
			} else {
				mpp_write(mpp, addr_cr,
					  H265E_ADDR_CR_BASE0 + (i << 4));
				mpp_write(mpp, 0,
					  H265E_ADDR_FBC_C_OFFSET0 + (i << 4));
				mpp_write(mpp, 0,
					  H265E_ADDR_MV_COL0 + (i << 2));
			}
			idx++;
		}
		remain -= i;
		mpp_write(mpp, (u32)instance->work.dma_addr,
			  H265E_ADDR_WORK_BASE);
		mpp_write(mpp, (u32)instance->work.size, H265E_WORK_SIZE);
		mpp_write(mpp, 0, H265E_WORK_PARAM);
		rockchip_mpp_h265e_issue_command(mpp,
						 index,
						 H265E_CMD_SET_FRAMEBUF);
		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("rockchip_mpp_h265e_wait_busy timeout\n");
			goto FAIL;
		}
	}
	if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
		mpp_err("h265e register frame buffer ret fail\n");
		goto FAIL;
	}
	mpp_debug_leave();
	return 0;
FAIL:
	rockchip_mpp_h265e_free_frame_buffer(mpp, instance);
	mpp_err("fail,index = %d\n", index);
	return -1;
}

static int rockchip_mpp_h265e_encode_one_frame(struct rockchip_mpp_dev *mpp,
					       struct h265e_ctx *ctx,
					       int index)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct mpp_h265e_encode_info *en_info = &ctx->cfg;
	struct rockchip_h265e_instance *instance = &enc->instance[index];
	struct mpp_h265e_cfg *cfg = &instance->cfg;
	int luma_stride = 0;
	int chroma_stride = 0;
	int src_format = 0;
	u32 value, src_y, src_cb, src_cr;
	int interlace = 0;
	u32 roi_enable = 0;
	u32 ctu_qp_enable = 0;

	mpp_debug_enter();
	if (!cfg || !ctx)
		return -1;

	mutex_lock(&enc->lock);
	h265e_last = ktime_get();
#if H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_dev_power_on(mpp);
	if (instance->status != H265E_INSTANCE_STATUS_SET_PARAMETER) {
		mutex_unlock(&enc->lock);
		mpp_err("fail,status = %d,index = %d\n",
			instance->status, index);
		return -1;
	}

	luma_stride = cfg->width_stride;
	interlace = (cfg->src_format == H265E_SRC_YUV_420_NV12) ||
		    (cfg->src_format == H265E_SRC_YUV_420_NV21);
	if (cfg->src_format == H265E_SRC_YUV_420_NV12)
		src_format = 0x02;
	else if (cfg->src_format == H265E_SRC_YUV_420_NV21)
		src_format = 0x03;
	if (cfg->map_type == TILED_SUB_CTU_MAP)
		src_format = 0x1;
	mpp_write(mpp, 0xfffffff2, H265E_PERF_LATENCY_CTRL0);
	mpp_write(mpp, (u32)ctx->bs.dma_addr, H265E_BS_START_ADDR);
	mpp_write(mpp, ctx->bs.size, H265E_BS_SIZE);
	mpp_write(mpp, (u32)ctx->bs.dma_addr, H265E_BS_RD_PTR);
	mpp_write(mpp, (u32)ctx->bs.dma_addr, H265E_BS_WR_PTR);

	value = (cfg->line_buf_int_en << 6) |
		(cfg->slice_int_enable << 5) |
		(cfg->ring_buffer_enable << 4) |
		cfg->bs_endian;
	mpp_write(mpp, value, H265E_BS_PARAM);
	mpp_write(mpp, 0, H265E_BS_OPTION);

	mpp_write(mpp, instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, instance->work.size, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);

	mpp_write(mpp, enc->temp.dma_addr, H265E_ADDR_TEMP_BASE);
	mpp_write(mpp, enc->temp.size, H265E_TEMP_SIZE);
	mpp_write(mpp, 0, H265E_TEMP_PARAM);

#if	H265E_AXI_STATUS
	mpp_write(mpp, 0x0, H265E_ADDR_SEC_AXI_BASE);
	mpp_write(mpp, H265E_SEC_AXI_BUF_SIZE, H265E_SEC_AXI_SIZE);
	mpp_write(mpp, 0xffff, H265E_USE_SEC_AXI);
#else
	mpp_write(mpp, 0, H265E_USE_SEC_AXI);
#endif
	if (cfg->code_option.implicit_header_encode == 1) {
		value = CODEOPT_ENC_HEADER_IMPLICIT	|
			CODEOPT_ENC_VCL	|
			(cfg->code_option.encode_aud << 5) |
			(cfg->code_option.encode_eos << 6) |
			(cfg->code_option.encode_eob << 7);
	} else {
		value = (cfg->code_option.implicit_header_encode << 0) |
			(cfg->code_option.encode_vcl << 1) |
			(cfg->code_option.encode_vps << 2) |
			(cfg->code_option.encode_sps << 3) |
			(cfg->code_option.encode_pps << 4) |
			(cfg->code_option.encode_aud << 5) |
			(cfg->code_option.encode_eos << 6) |
			(cfg->code_option.encode_eob << 7) |
			(cfg->code_option.encode_vui << 9);
	}

	mpp_write(mpp, value, H265E_CMD_ENC_CODE_OPTION);

	value = (en_info->skip_pic << 0) |
		(en_info->force_qp_enable << 1) |
		(en_info->force_qp_i << 2) |
		(en_info->force_qp_p << 8) |
		(0 << 14) |
		(en_info->force_frame_type_enable << 20) |
		(en_info->force_frame_type << 21);
	mpp_write(mpp, value, H265E_CMD_ENC_PIC_PARAM);
	if (en_info->stream_end == 1) {
		mpp_debug(DEBUG_H265E_INFO,
			  "%s %d instance %d en_info->stream_end\n",
			  __func__, __LINE__, index);
		mpp_write(mpp, 0xFFFFFFFE, H265E_CMD_ENC_SRC_PIC_IDX);
	} else {
		mpp_write(mpp, instance->src_idx, H265E_CMD_ENC_SRC_PIC_IDX);
	}
	instance->src_idx++;
	instance->src_idx = instance->src_idx % instance->min_src_frame_count;
	src_y = (u32)ctx->src.dma_addr;
	src_cb = src_y + luma_stride * cfg->height_stride;
	src_cr = src_cb + luma_stride * cfg->height_stride / 4;
	mpp_write(mpp, src_y, H265E_CMD_ENC_SRC_ADDR_Y);
	if (cfg->src_format == H265E_SRC_YUV_420_YV12) {
		mpp_write(mpp, src_cb, H265E_CMD_ENC_SRC_ADDR_V);
		mpp_write(mpp, src_cr, H265E_CMD_ENC_SRC_ADDR_U);
	} else {
		mpp_write(mpp, src_cb, H265E_CMD_ENC_SRC_ADDR_U);
		mpp_write(mpp, src_cr, H265E_CMD_ENC_SRC_ADDR_V);
	}
	chroma_stride = (interlace == 1) ? luma_stride : (luma_stride >> 1);
	if (cfg->map_type == TILED_SUB_CTU_MAP)
		chroma_stride = luma_stride;
	mpp_write(mpp, (luma_stride << 16) | chroma_stride,
		  H265E_CMD_ENC_SRC_STRIDE);
	value = (src_format << 0) | (cfg->src_endian << 6);
	mpp_write(mpp, value, H265E_CMD_ENC_SRC_FORMAT);
	value = 0;
	if (cfg->sei.prefix_sei_nal_enable) {
		mpp_write(mpp, cfg->sei.prefix_sei_nal_addr,
			  H265E_CMD_ENC_PREFIX_SEI_NAL_ADDR);
		value = cfg->sei.prefix_sei_data_size << 16 |
			cfg->sei.prefix_sei_data_order << 1 |
			cfg->sei.prefix_sei_nal_enable;
	}
	mpp_write(mpp, value, H265E_CMD_ENC_PREFIX_SEI_INFO);

	value = 0;
	if (cfg->sei.suffix_sei_nal_enable) {
		mpp_write(mpp, cfg->sei.suffix_sei_nal_addr,
			  H265E_CMD_ENC_SUFFIX_SEI_NAL_ADDR);
		value = (cfg->sei.suffix_sei_data_size << 16) |
			(cfg->sei.suffix_sei_data_enc_order << 1) |
			cfg->sei.suffix_sei_nal_enable;
	}
	mpp_write(mpp, value, H265E_CMD_ENC_SUFFIX_SEI_INFO);

	mpp_write(mpp, (u32)ctx->roi.dma_addr,
		  H265E_CMD_ENC_ROI_ADDR_CTU_MAP);
	mpp_write(mpp, (u32)ctx->ctu.dma_addr,
		  H265E_CMD_ENC_CTU_QP_MAP_ADDR);

	if (ctx->roi.dma_addr == 0 || ctx->roi.hdl < 0)
		roi_enable = 0;
	else
		roi_enable = cfg->ctu.roi_enable;

	if (ctx->ctu.dma_addr == 0 || ctx->ctu.hdl < 0)
		ctu_qp_enable = 0;
	else
		ctu_qp_enable = cfg->ctu.ctu_qp_enable;
	value = ((roi_enable) << 0) |
			(cfg->ctu.roi_delta_qp << 1) |
			(ctu_qp_enable << 9) |
			(cfg->ctu.map_endian << 12) |
			(cfg->ctu.map_stride << 16);

	mpp_debug(DEBUG_H265E_INFO,
		  "roi_enable = %d,roi_delta_qp = %d,ctu_qp_enable = %d\n",
		  cfg->ctu.roi_enable, cfg->ctu.roi_delta_qp, ctu_qp_enable);
	mpp_write(mpp, value,
		  H265E_CMD_ENC_CTU_OPT_PARAM);

	mpp_write(mpp, 0, H265E_CMD_ENC_SRC_TIMESTAMP_LOW);
	mpp_write(mpp, 0, H265E_CMD_ENC_SRC_TIMESTAMP_HIGH);

	value = (cfg->use_cur_as_longterm_pic << 0) |
		(cfg->use_longterm_ref << 1);
	mpp_write(mpp, value, H265E_CMD_ENC_LONGTERM_PIC);

	mpp_write(mpp, 0, H265E_CMD_ENC_SUB_FRAME_SYNC_CONFIG);
	rockchip_mpp_h265e_issue_command(mpp, index, H265E_CMD_ENC_PIC);
	mpp_debug_leave();
	return 0;
}

static int rockchip_mpp_h265e_get_encode_result(struct rockchip_mpp_dev *mpp,
						struct h265e_ctx *ctx)
{
	u32 value, rd, wt;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct h265e_result *result = NULL;
	struct h265e_session *session = NULL;
	int index;

	mpp_debug_enter();
	if (!mpp || !ctx) {
		mpp_err("param is invalid");
		return -1;
	}
	session = container_of(ctx->ictx.session,
			       struct h265e_session,
			       isession);
	index = session->instance_index;
	result = &ctx->result;
	value = mpp_read(mpp, H265E_RET_SUCCESS);
	if (value == 0) {
		result->fail_reason = mpp_read(mpp, H265E_RET_FAIL_REASON);
		mpp_err("fail reason = 0x%x", result->fail_reason);
		mutex_unlock(&enc->lock);
		mpp_debug_leave();
		return -1;
	}
	result->fail_reason = 0;
	result->enc_pic_cnt = mpp_read(mpp, H265E_RET_ENC_PIC_NUM);
	value = mpp_read(mpp, H265E_RET_ENC_PIC_TYPE);
	result->pic_type         = value & 0xFFFF;
	result->recon_frame_index = mpp_read(mpp, H265E_RET_ENC_PIC_IDX);
	result->num_of_slice     = mpp_read(mpp, H265E_RET_ENC_PIC_SLICE_NUM);
	result->pick_skipped     = mpp_read(mpp, H265E_RET_ENC_PIC_SKIP);
	result->num_intra        = mpp_read(mpp, H265E_RET_ENC_PIC_NUM_INTRA);
	result->num_merge        = mpp_read(mpp, H265E_RET_ENC_PIC_NUM_MERGE);
	result->num_skip_block   = mpp_read(mpp, H265E_RET_ENC_PIC_NUM_SKIP);
	result->avg_ctu_qp       = mpp_read(mpp, H265E_RET_ENC_PIC_AVG_CU_QP);
	result->bs_size          = mpp_read(mpp, H265E_RET_ENC_PIC_BYTE);
	result->gop_idx    = mpp_read(mpp, H265E_RET_ENC_GOP_PIC_IDX);
	result->poc       = mpp_read(mpp, H265E_RET_ENC_PIC_POC);
	result->src_idx       = mpp_read(mpp, H265E_RET_ENC_USED_SRC_IDX);
	rd = mpp_read(mpp, H265E_BS_RD_PTR);
	wt = mpp_read(mpp, H265E_BS_WR_PTR);
#if PRINT_BS_DATA
	w_bs_size = result->bs_size;
#endif
	h265e_now = ktime_get();
	mpp_debug(DEBUG_H265E_ENCODE_ONE_FRAME,
		  "h265e encode time is:%d us\n",
		  (int)ktime_to_us(ktime_sub(h265e_now, h265e_last)));
	mpp_debug(DEBUG_H265E_ENCODE_ONE_FRAME,
		  "RD_AXI_BYTE=%d,WR_AXI_BYTE=%d,WORK_CNT=%d\n",
		  mpp_read(mpp, H265E_PERF_RD_AXI_TOTAL_BYTE),
		  mpp_read(mpp, H265E_PERF_WR_AXI_TOTAL_BYTE),
		  mpp_read(mpp, H265E_PERF_WORKING_CNT));
	mpp_debug(DEBUG_H265E_ENCODE_ONE_FRAME,
		  "index = %d, bs_size = %d,size = %d\n",
		  index, result->bs_size, wt - rd);
	if (result->recon_frame_index < 0)
		result->bs_size   = 0;
#if H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
	return 0;
}

static long rockchip_mpp_h265e_ioctl(struct mpp_session *isession,
				     unsigned int cmd,
				     unsigned long arg)
{
	struct rockchip_mpp_dev *mpp = isession->mpp;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);

	struct rockchip_h265e_instance *instance = NULL;
	struct h265e_session *session =
					container_of(isession,
						     struct h265e_session,
						     isession);

	int ret = 0;
	int index = session->instance_index;

	if (index < 0 || index >= H265E_INSTANCE_NUM) {
		mpp_err("error: index = %d is invalid\n", index);
		return -1;
	}

	instance = &enc->instance[index];
	switch (cmd) {
	case MPP_DEV_H265E_SET_COLOR_PALETTE:
		break;
	case MPP_DEV_H265E_SET_PARAMETER:
		if (copy_from_user(&instance->cfg, (void __user *)arg,
				   sizeof(struct mpp_h265e_cfg))) {
			mpp_err("error: set reg copy_from_user failed\n");
			return -EFAULT;
		}
		if (instance->status == H265E_INSTANCE_STATUS_OPENED)
			ret = rockchip_mpp_h265e_set_parameter(mpp, index);
		else
			ret = rockchip_mpp_h265e_change_parameter(mpp, index);
		break;
	case MPP_DEV_H265E_GET_HEAD_PARAMETER:
		{
			struct hal_h265e_header head;

			if (copy_from_user(&head,
					   (void __user *)arg, sizeof(head))) {
				mpp_err("error: set reg copy_from_user failed\n");
				return -EFAULT;
			}
			head.size = 0;
#ifdef	H265E_STREAM_HEADER
			if (rockchip_mpp_h265e_get_stream_header(mpp,
								 index, &head))
				head.size = 0;
#endif
			if (copy_to_user((void __user *)arg,
					 &head, sizeof(head))) {
				mpp_err("copy result to user failed\n");
				return -1;
			}
		}
		break;
	default:
		break;
	}
	return ret;
}

struct mpp_dev_ops h265e_ops = {
	.init = rockchip_mpp_h265e_ctx_init,
	.prepare = NULL,
	.run = rockchip_mpp_h265e_run,
	.done = rockchip_mpp_h265e_done,
	.irq = rockchip_mpp_h265e_irq,
	.result = rockchip_mpp_h265e_result,
	.ioctl = rockchip_mpp_h265e_ioctl,
	.open = rockchip_mpp_h265e_open,
	.release = rockchip_mpp_h265e_release,
};

static void rockchip_mpp_h265e_enable_clk(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);

	if (enc->aclk)
		clk_prepare_enable(enc->aclk);
	if (enc->pclk)
		clk_prepare_enable(enc->pclk);
	if (enc->core)
		clk_prepare_enable(enc->core);
	if (enc->dsp)
		clk_prepare_enable(enc->dsp);
#if H265E_AXI_STATUS
	if (enc->aclk_axi2sram)
		clk_prepare_enable(enc->aclk_axi2sram);
#endif
}

static void rockchip_mpp_h265e_disable_clk(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);

	if (enc->dsp)
		clk_disable_unprepare(enc->dsp);
	if (enc->core)
		clk_disable_unprepare(enc->core);
	if (enc->pclk)
		clk_disable_unprepare(enc->pclk);
	if (enc->aclk)
		clk_disable_unprepare(enc->aclk);
#if H265E_AXI_STATUS
	if (enc->aclk_axi2sram)
		clk_disable_unprepare(enc->aclk_axi2sram);
#endif
}

static void rockchip_mpp_h265e_power_on(struct rockchip_mpp_dev *mpp)
{
	rockchip_mpp_h265e_enable_clk(mpp);
}

static void rockchip_mpp_h265e_power_off(struct rockchip_mpp_dev *mpp)
{
	rockchip_mpp_h265e_disable_clk(mpp);
}

static struct mpp_session *rockchip_mpp_h265e_open(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct h265e_session *session = kzalloc(sizeof(*session), GFP_KERNEL);
	u32 code_base;
	u32	i, reg_val = 0, remap_size = 0, ret;
	struct rockchip_h265e_instance *instance = NULL;
	int index = 0;

	mpp_debug_enter();
	mutex_lock(&enc->lock);
	if (!session) {
		mpp_err("failed to allocate h265e_session data");
		goto NFREE_FAIL;
	}
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_dev_power_on(mpp);

	if (!atomic_read(&enc->load_firmware)) {
		ret = rockchip_mpp_h265e_load_firmware(mpp);
		if (ret)
			goto NFREE_FAIL;
		atomic_inc(&enc->load_firmware);
		enc->temp.size = H265E_TEMP_BUFFER_SIZE;
		enc->temp.hdl =
			rockchip_mpp_h265e_global_dma_alloc(mpp,
							    enc->temp.size,
							    MPP_ALIGN_SIZE,
							    &enc->temp.dma_addr);
		if (enc->temp.hdl < 0) {
			mpp_err("error: alloc temp buffer error\n");
			goto NFREE_FAIL;
		}
	}
	for (i = 0; i < H265E_INSTANCE_NUM; i++) {
		instance = &enc->instance[i];
		if (!atomic_read(&instance->is_used)) {
			instance->work.size = H265E_WORK_BUFFER_SIZE;
			instance->work.hdl =
				rockchip_mpp_h265e_global_dma_alloc(mpp,
								    instance->work.size,
								    MPP_ALIGN_SIZE,
								    &instance->work.dma_addr);
			instance->index = i;
			atomic_set(&instance->is_used, 1);
			break;
		}
	}
	if (i == H265E_INSTANCE_NUM) {
		mpp_err("error: the num of instance up to H265E_INSTANCE_NUM\n");
		goto NFREE_FAIL;
	}
	index = instance->index;
	instance->status = H265E_INSTANCE_STATUS_ERROR;
	mpp_debug(DEBUG_H265E_INFO,
		  "%s = %d\n", __func__, index);
	instance->session = &session->isession;
	session->instance_index = index;
	code_base = (u32)enc->firmware.dma_addr;
	mpp_debug(DEBUG_H265E_INFO, "h265e code_base = %x\n", code_base);
	if (!atomic_read(&enc->is_init)) {
		mpp_write(mpp, 0x0, H265E_PO_CONF);
		mpp_write(mpp, 0x7ffffff, H265E_VPU_RESET_REQ);

		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("rockchip_mpp_h265e_wait_busy timeout\n");
			mpp_write(mpp, 0, H265E_VPU_RESET_REQ);
			goto FAIL;
		}
		mpp_write(mpp, 0, H265E_VPU_RESET_REQ);
		for (i = H265E_COMMAND; i < H265E_CMD_REG_END; i += 4)
			mpp_write(mpp, 0x00, i);
		remap_size = 0x100;
		reg_val = 0x80000000 | (0 << 16) |
			  (H265E_REMAP_CODE_INDEX << 12) |
			  (1 << 11) | remap_size;
		mpp_write(mpp, reg_val, H265E_VPU_REMAP_CTRL);
		mpp_write(mpp, 0x00000000, H265E_VPU_REMAP_VADDR);
		mpp_write(mpp, code_base, H265E_VPU_REMAP_PADDR);
		mpp_write(mpp, code_base, H265E_ADDR_CODE_BASE);
		mpp_write(mpp, H265E_CODE_BUFFER_SIZE, H265E_CODE_SIZE);
		mpp_write(mpp, 0, H265E_CODE_PARAM);
		mpp_write(mpp, 0, H265E_HW_OPTION);
		mpp_write(mpp, H265E_INT_OPEN, H265E_VPU_VINT_ENABLE);
		mpp_write(mpp, 0xfffffff2, H265E_PERF_LATENCY_CTRL0);
		mpp_write(mpp, 0x0, H265E_PERF_LATENCY_CTRL1);
		mpp_write(mpp, 0x1, H265E_PERF_AXI_CTRL);
		mpp_write(mpp, 0x01, H265E_VPU_BUSY_STATUS);
		mpp_write(mpp, H265E_CMD_INIT_VPU, H265E_COMMAND);
		mpp_write(mpp, 0x01, H265E_VPU_REMAP_CORE_START);
		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("rockchip_mpp_h265e_wait_busy timeout\n");
			goto FAIL;
		}
		if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
			mpp_err("h265e init ret fail\n");
			goto FAIL;
		}
		/* start Init command*/
		rockchip_mpp_h265e_issue_command(mpp,
						 index,
						 H265E_CMD_GET_FW_VERSION);
		if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
			mpp_err("rockchip_mpp_h265e_wait_busy timeout\n");
			goto FAIL;
		}
		if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
			mpp_err("h265e creat instance ret fail\n");
			goto FAIL;
		}
		reg_val = mpp_read(mpp, H265E_RET_FW_VERSION);
		mpp_debug(DEBUG_H265E_INFO,
			  "get_firmware_version:VERSION=%d\n", reg_val);
		atomic_inc(&enc->is_init);
	}
	mpp_write(mpp, 0x0, H265E_CORE_INDEX);
	mpp_write(mpp, 0x00010000 | index, H265E_INST_INDEX);
	mpp_write(mpp, (u32)instance->work.dma_addr, H265E_ADDR_WORK_BASE);
	mpp_write(mpp, H265E_WORK_BUFFER_SIZE, H265E_WORK_SIZE);
	mpp_write(mpp, 0, H265E_WORK_PARAM);
	mpp_debug(DEBUG_H265E_INFO,
		  "open instance=%d work addr=%x\n",
		  index,
		  (u32)instance->work.dma_addr);
	/* create instance*/
	rockchip_mpp_h265e_issue_command(mpp, index, H265E_CMD_CREATE_INSTANCE);
	if (rockchip_mpp_h265e_wait_busy(mpp) == -1) {
		mpp_err("rockchip_mpp_h265e_wait_busy timeout\n");
		goto FAIL;
	}
	if (mpp_read(mpp, H265E_RET_SUCCESS) == 0) {
		mpp_err("h265e creat instance ret fail\n");
		goto FAIL;
	}
	/* set default buffer counter*/
	instance->min_frame_buffer_count = 2;
	instance->min_src_frame_count = 2;
	instance->src_idx = 0;
	instance->status = H265E_INSTANCE_STATUS_OPENED;
#if H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
	return &session->isession;
FAIL:
	rockchip_mpp_h265e_free_instance(mpp, index);
NFREE_FAIL:
	kfree(session);
	session = NULL;
	mutex_unlock(&enc->lock);
	mpp_err("h265e open fail\n");
	return NULL;
}

static void rockchip_mpp_h265e_release(struct mpp_session *isession)
{
	struct h265e_session *session =
					container_of(isession,
						     struct h265e_session,
						     isession);
	struct rockchip_mpp_dev *mpp = session->isession.mpp;
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	int index = 0;

	mpp_debug_enter();
	mutex_lock(&enc->lock);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_enable_clk(mpp);
#endif
	mpp_dev_power_on(mpp);
	index = session->instance_index;
	rockchip_mpp_h265e_issue_command(mpp, index, H265E_CMD_FINI_SEQ);
	if (rockchip_mpp_h265e_wait_busy(mpp) == -1)
		mpp_err("h265e_wait_busy timeout,index=%d\n", index);
	if (mpp_read(mpp, H265E_RET_SUCCESS) == 0)
		mpp_err("h265e close instance %d ret fail\n", index);
	rockchip_mpp_h265e_free_instance(mpp, index);
	kfree(session);
#if	H265E_POWER_SAVE
	rockchip_mpp_h265e_disable_clk(mpp);
#endif
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
}

static int rockchip_mpp_h265e_probe(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct device_node *np = mpp->dev->of_node;
	int i;

	enc->dev.ops = &h265e_ops;
	for (i = 0; i < H265E_INSTANCE_NUM; i++)
		atomic_set(&enc->instance[i].is_used, 0);
	atomic_set(&enc->load_firmware, 0);
	atomic_set(&enc->is_init, 0);
	mutex_init(&enc->lock);
#if H265E_CLK
	enc->aclk = devm_clk_get(mpp->dev, "aclk_h265");
	if (IS_ERR_OR_NULL(enc->aclk)) {
		dev_err(mpp->dev, "failed on clk_get aclk\n");
		enc->aclk = NULL;
		goto fail;
	}
	enc->pclk = devm_clk_get(mpp->dev, "pclk_h265");
	if (IS_ERR_OR_NULL(enc->pclk)) {
		dev_err(mpp->dev, "failed on clk_get pclk\n");
		enc->pclk = NULL;
		goto fail;
	}
	enc->core = devm_clk_get(mpp->dev, "clk_core");
	if (IS_ERR_OR_NULL(enc->core)) {
		dev_err(mpp->dev, "failed on clk_get core\n");
		enc->core = NULL;
		goto fail;
	}
	enc->dsp = devm_clk_get(mpp->dev, "clk_dsp");
	if (IS_ERR_OR_NULL(enc->dsp)) {
		dev_err(mpp->dev, "failed on clk_get dsp\n");
		enc->dsp = NULL;
		goto fail;
	}
#if H265E_AXI_STATUS
	enc->aclk_axi2sram = devm_clk_get(mpp->dev, "aclk_axi2sram");
	if (IS_ERR_OR_NULL(enc->aclk_axi2sram)) {
		dev_err(mpp->dev, "failed on clk_get aclk_axi2sram\n");
		enc->aclk_axi2sram = NULL;
		goto fail;
	}
#endif
#endif
	if (of_property_read_bool(np, "mode_ctrl")) {
		of_property_read_u32(np, "mode_bit", &enc->mode_bit);
		of_property_read_u32(np, "mode_ctrl", &enc->mode_ctrl);
#ifdef CONFIG_MFD_SYSCON
		enc->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR_OR_NULL(enc->grf)) {
			enc->grf = NULL;
			mpp_err("can't find vpu grf property\n");
			return -1;
		}
#endif
	}

	return 0;
#if H265E_CLK
fail:
	return -1;
#endif
}

static void rockchip_mpp_h265e_remove(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_h265e_dev *enc =
					 container_of(mpp,
						      struct rockchip_h265e_dev,
						      dev);
	struct rockchip_h265e_instance *instance = NULL;
	struct mpp_h265e_buffer *buf = NULL;
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);
	int i = 0;

	mpp_debug_enter();
	mutex_lock(&enc->lock);
	for (i = 0; i < H265E_INSTANCE_NUM; i++) {
		instance = &enc->instance[i];
		if (atomic_read(&instance->is_used) == 1) {
			buf = &instance->work;
			if (buf->hdl >= 0) {
				vpu_iommu_unmap_kernel(mpp->iommu_info,
						       session, buf->hdl);
				vpu_iommu_free(mpp->iommu_info, session,
					       buf->hdl);
			}
			rockchip_mpp_h265e_free_frame_buffer(mpp, instance);
			atomic_set(&instance->is_used, 0);
		}
	}
	atomic_set(&enc->is_init, 0);
	atomic_set(&enc->load_firmware, 0);
	buf = &enc->temp;
	if (buf->hdl >= 0) {
		vpu_iommu_unmap_kernel(mpp->iommu_info, session, buf->hdl);
		vpu_iommu_free(mpp->iommu_info, session, buf->hdl);
	}

	if (enc->firmware.hdl >= 0) {
		vpu_iommu_unmap_kernel(mpp->iommu_info, session,
				       enc->firmware.hdl);
		vpu_iommu_free(mpp->iommu_info, session, enc->firmware.hdl);
	}
	mutex_unlock(&enc->lock);
	mpp_debug_leave();
}

const struct rockchip_mpp_dev_variant h265e_variant = {
	.data_len = sizeof(struct rockchip_h265e_dev),
	.trans_info = NULL,
	.mmu_dev_dts_name = NULL,
	.hw_probe = rockchip_mpp_h265e_probe,
	.hw_remove = rockchip_mpp_h265e_remove,
	.power_on = rockchip_mpp_h265e_power_on,
	.power_off = rockchip_mpp_h265e_power_off,
	.reset = NULL,
};
EXPORT_SYMBOL(h265e_variant);
