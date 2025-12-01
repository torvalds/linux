/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ras.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v11_0.h"

#include "mp/mp_11_0_offset.h"
#include "mp/mp_11_0_sh_mask.h"
#include "gc/gc_9_0_offset.h"
#include "sdma0/sdma0_4_0_offset.h"
#include "nbio/nbio_7_4_offset.h"

#include "oss/osssys_4_0_offset.h"
#include "oss/osssys_4_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vega20_sos.bin");
MODULE_FIRMWARE("amdgpu/vega20_asd.bin");
MODULE_FIRMWARE("amdgpu/vega20_ta.bin");
MODULE_FIRMWARE("amdgpu/navi10_sos.bin");
MODULE_FIRMWARE("amdgpu/navi10_asd.bin");
MODULE_FIRMWARE("amdgpu/navi10_ta.bin");
MODULE_FIRMWARE("amdgpu/navi14_sos.bin");
MODULE_FIRMWARE("amdgpu/navi14_asd.bin");
MODULE_FIRMWARE("amdgpu/navi14_ta.bin");
MODULE_FIRMWARE("amdgpu/navi12_sos.bin");
MODULE_FIRMWARE("amdgpu/navi12_asd.bin");
MODULE_FIRMWARE("amdgpu/navi12_ta.bin");
MODULE_FIRMWARE("amdgpu/navi12_cap.bin");
MODULE_FIRMWARE("amdgpu/arcturus_sos.bin");
MODULE_FIRMWARE("amdgpu/arcturus_asd.bin");
MODULE_FIRMWARE("amdgpu/arcturus_ta.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_sos.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_ta.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_cap.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_sos.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_ta.bin");
MODULE_FIRMWARE("amdgpu/vangogh_asd.bin");
MODULE_FIRMWARE("amdgpu/vangogh_toc.bin");
MODULE_FIRMWARE("amdgpu/dimgrey_cavefish_sos.bin");
MODULE_FIRMWARE("amdgpu/dimgrey_cavefish_ta.bin");
MODULE_FIRMWARE("amdgpu/beige_goby_sos.bin");
MODULE_FIRMWARE("amdgpu/beige_goby_ta.bin");

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
/* navi10 reg offset define */
#define mmRLC_GPM_UCODE_ADDR_NV10	0x5b61
#define mmRLC_GPM_UCODE_DATA_NV10	0x5b62
#define mmSDMA0_UCODE_ADDR_NV10		0x5880
#define mmSDMA0_UCODE_DATA_NV10		0x5881
/* memory training timeout define */
#define MEM_TRAIN_SEND_MSG_TIMEOUT_US	3000000

/* For large FW files the time to complete can be very long */
#define USBC_PD_POLLING_LIMIT_S 240

/* Read USB-PD from LFB */
#define GFX_CMD_USB_PD_USE_LFB 0x480

static int psp_v11_0_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	char ucode_prefix[30];
	int err = 0;

	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, MP0_HWIP, ucode_prefix, sizeof(ucode_prefix));

	switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 4):
		err = psp_init_sos_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_asd_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_ta_microcode(psp, ucode_prefix);
		adev->psp.securedisplay_context.context.bin_desc.size_bytes = 0;
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 5):
	case IP_VERSION(11, 0, 9):
		err = psp_init_sos_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_asd_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_ta_microcode(psp, ucode_prefix);
		adev->psp.securedisplay_context.context.bin_desc.size_bytes = 0;
		break;
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
		err = psp_init_sos_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_ta_microcode(psp, ucode_prefix);
		break;
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 2):
		err = psp_init_asd_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_toc_microcode(psp, ucode_prefix);
		break;
	default:
		BUG();
	}

	return err;
}

static int psp_v11_wait_for_tos_unload(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg1, sol_reg2;
	int retry_loop;

	/* Wait for the TOS to be unloaded */
	for (retry_loop = 0; retry_loop < 20; retry_loop++) {
		sol_reg1 = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
		usleep_range(1000, 2000);
		sol_reg2 = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
		if (sol_reg1 == sol_reg2)
			return 0;
	}
	dev_err(adev->dev, "TOS unload failed, C2PMSG_33: %x C2PMSG_81: %x",
		RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_33),
		RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81));

	return -ETIME;
}

static int psp_v11_0_wait_for_bootloader(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;
	int retry_loop;

	/* For a reset done at the end of S3, only wait for TOS to be unloaded */
	if (adev->in_s3 && !(adev->flags & AMD_IS_APU) && amdgpu_in_reset(adev))
		return psp_v11_wait_for_tos_unload(psp);

	for (retry_loop = 0; retry_loop < 20; retry_loop++) {
		/* Wait for bootloader to signify that is
		    ready having bit 31 of C2PMSG_35 set to 1 */
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			0x80000000, 0x8000FFFF, PSP_WAITREG_NOVERBOSE);

		if (ret == 0)
			return 0;
	}

	return ret;
}

static bool psp_v11_0_is_sos_alive(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg;

	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);

	return sol_reg != 0x0;
}

static int psp_v11_0_bootloader_load_component(struct psp_context  	*psp,
					       struct psp_bin_desc 	*bin_desc,
					       enum psp_bootloader_cmd  bl_cmd)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	/* Copy PSP System Driver binary to memory */
	psp_copy_fw(psp, bin_desc->start_addr, bin_desc->size_bytes);

	/* Provide the sys driver to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = bl_cmd;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	ret = psp_v11_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v11_0_bootloader_load_kdb(struct psp_context *psp)
{
	return psp_v11_0_bootloader_load_component(psp, &psp->kdb, PSP_BL__LOAD_KEY_DATABASE);
}

static int psp_v11_0_bootloader_load_spl(struct psp_context *psp)
{
	return psp_v11_0_bootloader_load_component(psp, &psp->spl, PSP_BL__LOAD_TOS_SPL_TABLE);
}

static int psp_v11_0_bootloader_load_sysdrv(struct psp_context *psp)
{
	return psp_v11_0_bootloader_load_component(psp, &psp->sys, PSP_BL__LOAD_SYSDRV);
}

static int psp_v11_0_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	/* Copy Secure OS binary to PSP memory */
	psp_copy_fw(psp, psp->sos.start_addr, psp->sos.size_bytes);

	/* Provide the PSP secure OS to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SOSDRV;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_81),
			   RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81), 0,
			   PSP_WAITREG_CHANGED);

	return ret;
}

static int psp_v11_0_ring_stop(struct psp_context *psp,
			      enum psp_ring_type ring_type)
{
	int ret = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Write the ring destroy command*/
	if (amdgpu_sriov_vf(adev))
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
				     GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
	else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64,
				     GFX_CTRL_CMD_ID_DESTROY_RINGS);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	/* Wait for response flag (bit 31) */
	if (amdgpu_sriov_vf(adev))
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
			MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);
	else
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
			MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);

	return ret;
}

static int psp_v11_0_ring_create(struct psp_context *psp,
				enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		ring->ring_wptr = 0;
		ret = psp_v11_0_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v11_0_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_103, psp_ring_reg);

		/* Write the ring initialization command to C2PMSG_101 */
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
					     GFX_CTRL_CMD_ID_INIT_GPCOM_RING);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
			MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);

	} else {
		/* Wait for sOS ready for ring creation */
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
			MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, 0);
		if (ret) {
			DRM_ERROR("Failed to wait for sOS ready for ring creation\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_69 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_70 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, psp_ring_reg);
		/* Write size of ring to C2PMSG_71 */
		psp_ring_reg = ring->ring_size;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_71, psp_ring_reg);
		/* Write the ring initialization command to C2PMSG_64 */
		psp_ring_reg = ring_type;
		psp_ring_reg = psp_ring_reg << 16;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, psp_ring_reg);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
			MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);
	}

	return ret;
}


static int psp_v11_0_ring_destroy(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v11_0_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static int psp_v11_0_mode1_reset(struct psp_context *psp)
{
	int ret;
	uint32_t offset;
	struct amdgpu_device *adev = psp->adev;

	offset = SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64);

	ret = psp_wait_for(psp, offset, MBOX_TOS_READY_FLAG,
			   MBOX_TOS_READY_MASK, 0);

	if (ret) {
		DRM_INFO("psp is not working correctly before mode1 reset!\n");
		return -EINVAL;
	}

	/*send the mode 1 reset command*/
	WREG32(offset, GFX_CTRL_CMD_ID_MODE1_RST);

	msleep(500);

	return 0;
}

static int psp_v11_0_memory_training_send_msg(struct psp_context *psp, int msg)
{
	int ret;
	int i;
	uint32_t data_32;
	int max_wait;
	struct amdgpu_device *adev = psp->adev;

	data_32 = (psp->mem_train_ctx.c2p_train_data_offset >> 20);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36, data_32);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, msg);

	max_wait = MEM_TRAIN_SEND_MSG_TIMEOUT_US / adev->usec_timeout;
	for (i = 0; i < max_wait; i++) {
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			0x80000000, 0x80000000, PSP_WAITREG_NOVERBOSE);
		if (ret == 0)
			break;
	}
	if (i < max_wait)
		ret = 0;
	else
		ret = -ETIME;

	DRM_DEBUG("training %s %s, cost %d @ %d ms\n",
		  (msg == PSP_BL__DRAM_SHORT_TRAIN) ? "short" : "long",
		  (ret == 0) ? "succeed" : "failed",
		  i, adev->usec_timeout/1000);
	return ret;
}

/*
 * save and restore process
 */
static int psp_v11_0_memory_training(struct psp_context *psp, uint32_t ops)
{
	struct psp_memory_training_context *ctx = &psp->mem_train_ctx;
	uint32_t *pcache = (uint32_t *)ctx->sys_cache;
	struct amdgpu_device *adev = psp->adev;
	uint32_t p2c_header[4];
	uint32_t sz;
	void *buf;
	int ret, idx;

	if (ctx->init == PSP_MEM_TRAIN_NOT_SUPPORT) {
		DRM_DEBUG("Memory training is not supported.\n");
		return 0;
	} else if (ctx->init != PSP_MEM_TRAIN_INIT_SUCCESS) {
		DRM_ERROR("Memory training initialization failure.\n");
		return -EINVAL;
	}

	if (psp_v11_0_is_sos_alive(psp)) {
		DRM_DEBUG("SOS is alive, skip memory training.\n");
		return 0;
	}

	amdgpu_device_vram_access(adev, ctx->p2c_train_data_offset, p2c_header, sizeof(p2c_header), false);
	DRM_DEBUG("sys_cache[%08x,%08x,%08x,%08x] p2c_header[%08x,%08x,%08x,%08x]\n",
		  pcache[0], pcache[1], pcache[2], pcache[3],
		  p2c_header[0], p2c_header[1], p2c_header[2], p2c_header[3]);

	if (ops & PSP_MEM_TRAIN_SEND_SHORT_MSG) {
		DRM_DEBUG("Short training depends on restore.\n");
		ops |= PSP_MEM_TRAIN_RESTORE;
	}

	if ((ops & PSP_MEM_TRAIN_RESTORE) &&
	    pcache[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		DRM_DEBUG("sys_cache[0] is invalid, restore depends on save.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if (p2c_header[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	    !(pcache[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	      pcache[3] == p2c_header[3])) {
		DRM_DEBUG("sys_cache is invalid or out-of-date, need save training data to sys_cache.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if ((ops & PSP_MEM_TRAIN_SAVE) &&
	    p2c_header[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		DRM_DEBUG("p2c_header[0] is invalid, save depends on long training.\n");
		ops |= PSP_MEM_TRAIN_SEND_LONG_MSG;
	}

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		ops &= ~PSP_MEM_TRAIN_SEND_SHORT_MSG;
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	DRM_DEBUG("Memory training ops:%x.\n", ops);

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		/*
		 * Long training will encroach a certain amount on the bottom of VRAM;
		 * save the content from the bottom of VRAM to system memory
		 * before training, and restore it after training to avoid
		 * VRAM corruption.
		 */
		sz = BIST_MEM_TRAINING_ENCROACHED_SIZE;

		if (adev->gmc.visible_vram_size < sz || !adev->mman.aper_base_kaddr) {
			DRM_ERROR("visible_vram_size %llx or aper_base_kaddr %p is not initialized.\n",
				  adev->gmc.visible_vram_size,
				  adev->mman.aper_base_kaddr);
			return -EINVAL;
		}

		buf = vmalloc(sz);
		if (!buf) {
			DRM_ERROR("failed to allocate system memory.\n");
			return -ENOMEM;
		}

		if (drm_dev_enter(adev_to_drm(adev), &idx)) {
			memcpy_fromio(buf, adev->mman.aper_base_kaddr, sz);
			ret = psp_v11_0_memory_training_send_msg(psp, PSP_BL__DRAM_LONG_TRAIN);
			if (ret) {
				DRM_ERROR("Send long training msg failed.\n");
				vfree(buf);
				drm_dev_exit(idx);
				return ret;
			}

			memcpy_toio(adev->mman.aper_base_kaddr, buf, sz);
			amdgpu_device_flush_hdp(adev, NULL);
			vfree(buf);
			drm_dev_exit(idx);
		} else {
			vfree(buf);
			return -ENODEV;
		}
	}

	if (ops & PSP_MEM_TRAIN_SAVE) {
		amdgpu_device_vram_access(psp->adev, ctx->p2c_train_data_offset, ctx->sys_cache, ctx->train_data_size, false);
	}

	if (ops & PSP_MEM_TRAIN_RESTORE) {
		amdgpu_device_vram_access(psp->adev, ctx->c2p_train_data_offset, ctx->sys_cache, ctx->train_data_size, true);
	}

	if (ops & PSP_MEM_TRAIN_SEND_SHORT_MSG) {
		ret = psp_v11_0_memory_training_send_msg(psp, (amdgpu_force_long_training > 0) ?
							 PSP_BL__DRAM_LONG_TRAIN : PSP_BL__DRAM_SHORT_TRAIN);
		if (ret) {
			DRM_ERROR("send training msg failed.\n");
			return ret;
		}
	}
	ctx->training_cnt++;
	return 0;
}

static uint32_t psp_v11_0_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = psp->km_ring.ring_wptr;
	else
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);

	return data;
}

static void psp_v11_0_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, value);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101, GFX_CTRL_CMD_ID_CONSUME_CMD);
		psp->km_ring.ring_wptr = value;
	} else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67, value);
}

static int psp_v11_0_load_usbc_pd_fw(struct psp_context *psp, uint64_t fw_pri_mc_addr)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t reg_status;
	int ret, i = 0;

	/*
	 * LFB address which is aligned to 1MB address and has to be
	 * right-shifted by 20 so that LFB address can be passed on a 32-bit C2P
	 * register
	 */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36, (fw_pri_mc_addr >> 20));

	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			   0x80000000, 0x80000000, 0);
	if (ret)
		return ret;

	/* Fireup interrupt so PSP can pick up the address */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, (GFX_CMD_USB_PD_USE_LFB << 16));

	/* FW load takes very long time */
	do {
		msleep(1000);
		reg_status = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35);

		if (reg_status & 0x80000000)
			goto done;

	} while (++i < USBC_PD_POLLING_LIMIT_S);

	return -ETIME;
done:

	if ((reg_status & 0xFFFF) != 0) {
		DRM_ERROR("Address load failed - MP0_SMN_C2PMSG_35.Bits [15:0] = 0x%04x\n",
				reg_status & 0xFFFF);
		return -EIO;
	}

	return 0;
}

static int psp_v11_0_read_usbc_pd_fw(struct psp_context *psp, uint32_t *fw_ver)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, C2PMSG_CMD_GFX_USB_PD_FW_VER);

	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			   0x80000000, 0x80000000, 0);
	if (!ret)
		*fw_ver = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36);

	return ret;
}

static const struct psp_funcs psp_v11_0_funcs = {
	.init_microcode = psp_v11_0_init_microcode,
	.bootloader_load_kdb = psp_v11_0_bootloader_load_kdb,
	.bootloader_load_spl = psp_v11_0_bootloader_load_spl,
	.bootloader_load_sysdrv = psp_v11_0_bootloader_load_sysdrv,
	.bootloader_load_sos = psp_v11_0_bootloader_load_sos,
	.ring_create = psp_v11_0_ring_create,
	.ring_stop = psp_v11_0_ring_stop,
	.ring_destroy = psp_v11_0_ring_destroy,
	.mode1_reset = psp_v11_0_mode1_reset,
	.mem_training = psp_v11_0_memory_training,
	.ring_get_wptr = psp_v11_0_ring_get_wptr,
	.ring_set_wptr = psp_v11_0_ring_set_wptr,
	.load_usbc_pd_fw = psp_v11_0_load_usbc_pd_fw,
	.read_usbc_pd_fw = psp_v11_0_read_usbc_pd_fw,
	.wait_for_bootloader = psp_v11_0_wait_for_bootloader
};

void psp_v11_0_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v11_0_funcs;
}
