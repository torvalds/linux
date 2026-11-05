/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 *
 */
#include <drm/drm_drv.h>
#include <linux/vmalloc.h>
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v15_0_3.h"

#include "mp/mp_15_0_3_offset.h"
//#include "mp/mp_15_0_3_sh_mask.h"

MODULE_FIRMWARE("amdgpu/psp_15_0_3_sos.bin");
MODULE_FIRMWARE("amdgpu/psp_15_0_3_sos_sriov.bin");
MODULE_FIRMWARE("amdgpu/psp_15_0_3_ta.bin");

/* For large FW files the time to complete can be very long */
#define USBC_PD_POLLING_LIMIT_S 240

/* Read USB-PD from LFB */
#define GFX_CMD_USB_PD_USE_LFB 0x480

/* VBIOS gfl defines */
#define MBOX_READY_MASK 0x80000000
#define MBOX_STATUS_MASK 0x0000FFFF
#define MBOX_COMMAND_MASK 0x00FF0000
#define MBOX_READY_FLAG 0x80000000
#define C2PMSG_CMD_SPI_UPDATE_ROM_IMAGE_ADDR_LO 0x2
#define C2PMSG_CMD_SPI_UPDATE_ROM_IMAGE_ADDR_HI 0x3
#define C2PMSG_CMD_SPI_UPDATE_FLASH_IMAGE 0x4

/* memory training timeout define */
#define MEM_TRAIN_SEND_MSG_TIMEOUT_US	3000000
static int psp_v15_0_3_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	char ucode_prefix[30];
	int err = 0;

	amdgpu_ucode_ip_version_decode(adev, MP0_HWIP, ucode_prefix, sizeof(ucode_prefix));

	switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
	case IP_VERSION(15, 0, 3):
		err = psp_init_sos_microcode(psp, ucode_prefix);
		if (err)
			return err;
		err = psp_init_ta_microcode(psp, ucode_prefix);
		if (err)
			return err;
		break;
	default:
		BUG();
	}

	return 0;
}

static bool psp_v15_0_3_is_sos_alive(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t reg_val;

	reg_val = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35);

	return reg_val == 0xaaaa;
}

static int psp_v15_0_3_wait_for_bootloader(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	int ret;
	int retry_loop;

	for (retry_loop = 0; retry_loop < 10; retry_loop++) {
		/* Wait for bootloader to signify that is
		    ready having bit 31 of C2PMSG_35 set to 1 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35),
				   0x80000000, 0x80000000, PSP_WAITREG_NOVERBOSE);

		if (ret == 0)
			return 0;
	}

	return ret;
}

static int psp_v15_0_3_bootloader_load_component(struct psp_context  	*psp,
					        struct psp_bin_desc 	*bin_desc,
					        enum psp_bootloader_cmd  bl_cmd)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check tOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v15_0_3_is_sos_alive(psp))
		return 0;

	ret = psp_v15_0_3_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	memcpy(psp->fw_pri_buf, bin_desc->start_addr, bin_desc->size_bytes);

	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_36, (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = bl_cmd;
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35, psp_gfxdrv_command_reg);

	ret = psp_v15_0_3_wait_for_bootloader(psp);

	return ret;
}

static int psp_v15_0_3_bootloader_load_kdb(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->kdb, PSP_BL__LOAD_KEY_DATABASE);
}

static int psp_v15_0_3_bootloader_load_spl(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->spl, PSP_BL__LOAD_TOS_SPL_TABLE);
}

static int psp_v15_0_3_bootloader_load_sysdrv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->sys, PSP_BL__LOAD_SYSDRV);
}

static int psp_v15_0_3_bootloader_load_soc_drv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->soc_drv, PSP_BL__LOAD_SOCDRV);
}

static int psp_v15_0_3_bootloader_load_intf_drv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->intf_drv, PSP_BL__LOAD_INTFDRV);
}

static int psp_v15_0_3_bootloader_load_dbg_drv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->dbg_drv, PSP_BL__LOAD_HADDRV);
}

static int psp_v15_0_3_bootloader_load_ras_drv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp, &psp->ras_drv, PSP_BL__LOAD_RASDRV);
}

static int psp_v15_0_3_bootloader_load_ipkeymgr_drv(struct psp_context *psp)
{
	return psp_v15_0_3_bootloader_load_component(psp,&psp->ipkeymgr_drv, PSP_BL__LOAD_IPKEYMGRDRV);
}

static int psp_v15_0_3_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v15_0_3_is_sos_alive(psp))
		return 0;

	ret = psp_v15_0_3_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy Secure OS binary to PSP memory */
	memcpy(psp->fw_pri_buf, psp->sos.start_addr, psp->sos.size_bytes);

	/* Provide the PSP secure OS to bootloader */
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_36, (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SOSDRV;
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35, psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_81),
			   RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_81), 0,
			   PSP_WAITREG_CHANGED);

	return ret;
}

static int psp_v15_0_3_ring_stop(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_101, GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_101),
				   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);
	} else {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_64, GFX_CTRL_CMD_ID_DESTROY_RINGS);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_64),
				   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);
	}

	return ret;
}

static int psp_v15_0_3_ring_create(struct psp_context *psp,
				   enum psp_ring_type ring_type)
{
	int ret = 0;
	int i;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		ret = psp_v15_0_3_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v15_0_3_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_103, psp_ring_reg);

		/* Write the ring initialization command to C2PMSG_101 */
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_101, GFX_CTRL_CMD_ID_INIT_GPCOM_RING);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_101),
				   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);

	} else {
		/* Wait for sOS ready for ring creation */
		if (adev->aid_mask) {
			for_each_inst(i, adev->aid_mask) {
				ret = psp_wait_for(psp,
						   SOC15_REG_OFFSET(MP0, i,
								    regMPASP_PCRU0_MPASP_C2PMSG_64),
						   MBOX_TOS_READY_FLAG,
						   MBOX_TOS_READY_MASK, 0);
				if (ret) {
					DRM_ERROR("TOS ready timeout for ring creation on aid %d\n",
						  i);
					return ret;
				}
			}
		} else {
			ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0,
								 regMPASP_PCRU0_MPASP_C2PMSG_64),
					   MBOX_TOS_READY_FLAG,
					   MBOX_TOS_READY_MASK, 0);
			if (ret) {
				DRM_ERROR("Failed to wait for trust OS ready for ring creation\n");
				return ret;
			}
		}

		/* Write low address of the ring to C2PMSG_69 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_69, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_70 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_70, psp_ring_reg);
		/* Write size of ring to C2PMSG_71 */
		psp_ring_reg = ring->ring_size;
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_71, psp_ring_reg);
		/* Write the ring initialization command to C2PMSG_64 */
		psp_ring_reg = ring_type;
		psp_ring_reg = psp_ring_reg << 16;
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_64, psp_ring_reg);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_64),
				   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);
	}

	return ret;
}

static int psp_v15_0_3_ring_destroy(struct psp_context *psp,
				  enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v15_0_3_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static uint32_t psp_v15_0_3_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_102);
	else
		data = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_67);

	return data;
}

static void psp_v15_0_3_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_102, value);
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_101,
			     GFX_CTRL_CMD_ID_CONSUME_CMD);
	} else
		WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_67, value);
}

static int psp_v15_0_3_memory_training_send_msg(struct psp_context *psp, int msg)
{
	int ret;
	int i;
	uint32_t data_32;
	int max_wait;
	struct amdgpu_device *adev = psp->adev;

	data_32 = (psp->mem_train_ctx.c2p_train_data_offset >> 20);
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_36, data_32);
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35, msg);

	max_wait = MEM_TRAIN_SEND_MSG_TIMEOUT_US / adev->usec_timeout;
	for (i = 0; i < max_wait; i++) {
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35),
			0x80000000, 0x80000000, PSP_WAITREG_NOVERBOSE);
		if (ret == 0)
			break;
	}
	if (i < max_wait)
		ret = 0;
	else
		ret = -ETIME;

	dev_dbg(adev->dev, "training %s %s, cost %d @ %d ms\n",
		  (msg == PSP_BL__DRAM_SHORT_TRAIN) ? "short" : "long",
		  (ret == 0) ? "succeed" : "failed",
		  i, adev->usec_timeout/1000);
	return ret;
}


static int psp_v15_0_3_memory_training(struct psp_context *psp, uint32_t ops)
{
	struct psp_memory_training_context *ctx = &psp->mem_train_ctx;
	uint32_t *pcache = (uint32_t *)ctx->sys_cache;
	struct amdgpu_device *adev = psp->adev;
	uint32_t p2c_header[4];
	uint32_t sz;
	void *buf;
	int ret, idx;

	if (amdgpu_emu_mode == 1)
		return 0;

	if (ctx->init == PSP_MEM_TRAIN_NOT_SUPPORT) {
		dev_dbg(adev->dev, "Memory training is not supported.\n");
		return 0;
	} else if (ctx->init != PSP_MEM_TRAIN_INIT_SUCCESS) {
		dev_err(adev->dev, "Memory training initialization failure.\n");
		return -EINVAL;
	}

	if (psp_v15_0_3_is_sos_alive(psp)) {
		dev_dbg(adev->dev, "SOS is alive, skip memory training.\n");
		return 0;
	}

	amdgpu_device_vram_access(adev, ctx->p2c_train_data_offset, p2c_header, sizeof(p2c_header), false);
	dev_dbg(adev->dev, "sys_cache[%08x,%08x,%08x,%08x] p2c_header[%08x,%08x,%08x,%08x]\n",
		  pcache[0], pcache[1], pcache[2], pcache[3],
		  p2c_header[0], p2c_header[1], p2c_header[2], p2c_header[3]);

	if (ops & PSP_MEM_TRAIN_SEND_SHORT_MSG) {
		dev_dbg(adev->dev, "Short training depends on restore.\n");
		ops |= PSP_MEM_TRAIN_RESTORE;
	}

	if ((ops & PSP_MEM_TRAIN_RESTORE) &&
	    pcache[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		dev_dbg(adev->dev, "sys_cache[0] is invalid, restore depends on save.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if (p2c_header[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	    !(pcache[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	      pcache[3] == p2c_header[3])) {
		dev_dbg(adev->dev, "sys_cache is invalid or out-of-date, need save training data to sys_cache.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if ((ops & PSP_MEM_TRAIN_SAVE) &&
	    p2c_header[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		dev_dbg(adev->dev, "p2c_header[0] is invalid, save depends on long training.\n");
		ops |= PSP_MEM_TRAIN_SEND_LONG_MSG;
	}

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		ops &= ~PSP_MEM_TRAIN_SEND_SHORT_MSG;
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	dev_dbg(adev->dev, "Memory training ops:%x.\n", ops);

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		/*
		 * Long training will encroach a certain amount on the bottom of VRAM;
		 * save the content from the bottom of VRAM to system memory
		 * before training, and restore it after training to avoid
		 * VRAM corruption.
		 */
		sz = BIST_MEM_TRAINING_ENCROACHED_SIZE;

		if (adev->gmc.visible_vram_size < sz || !adev->mman.aper_base_kaddr) {
			dev_err(adev->dev, "visible_vram_size %llx or aper_base_kaddr %p is not initialized.\n",
				  adev->gmc.visible_vram_size,
				  adev->mman.aper_base_kaddr);
			return -EINVAL;
		}

		buf = vmalloc(sz);
		if (!buf) {
			dev_err(adev->dev, "failed to allocate system memory.\n");
			return -ENOMEM;
		}

		if (drm_dev_enter(adev_to_drm(adev), &idx)) {
			memcpy_fromio(buf, adev->mman.aper_base_kaddr, sz);
			ret = psp_v15_0_3_memory_training_send_msg(psp, PSP_BL__DRAM_LONG_TRAIN);
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
		ret = psp_v15_0_3_memory_training_send_msg(psp, (amdgpu_force_long_training > 0) ?
							 PSP_BL__DRAM_LONG_TRAIN : PSP_BL__DRAM_SHORT_TRAIN);
		if (ret) {
			dev_err(adev->dev, "send training msg failed.\n");
			return ret;
		}
	}
	ctx->training_cnt++;
	return 0;
}

static int psp_v15_0_3_load_usbc_pd_fw(struct psp_context *psp, uint64_t fw_pri_mc_addr)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t reg_status;
	int ret, i = 0;

	if (amdgpu_emu_mode == 1)
		return 0;

	/*
	 * LFB address which is aligned to 1MB address and has to be
	 * right-shifted by 20 so that LFB address can be passed on a 32-bit C2P
	 * register
	 */
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_36, (fw_pri_mc_addr >> 20));

	ret = psp_wait_for(psp,
			   SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35),
			   0x80000000, 0x80000000, 0);
	if (ret)
		return ret;

	/* Fireup interrupt so PSP can pick up the address */
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35, (GFX_CMD_USB_PD_USE_LFB << 16));

	/* FW load takes very long time */
	do {
		msleep(1000);
		reg_status = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35);

		if (reg_status & 0x80000000)
			goto done;

	} while (++i < USBC_PD_POLLING_LIMIT_S);

	return -ETIME;
done:

	if ((reg_status & 0xFFFF) != 0) {
		DRM_ERROR("Address load failed - MP0_SMN_C2PMSG_35.Bits [15:0] = %04x\n",
				reg_status & 0xFFFF);
		return -EIO;
	}

	return 0;
}

static int psp_v15_0_3_read_usbc_pd_fw(struct psp_context *psp, uint32_t *fw_ver)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	if (amdgpu_emu_mode == 1)
		return 0;

	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35, C2PMSG_CMD_GFX_USB_PD_FW_VER);

	ret = psp_wait_for(psp,
			   SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_35),
			   0x80000000, 0x80000000, 0);
	if (!ret)
		*fw_ver = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_36);

	return ret;
}

static int psp_v15_0_3_exec_spi_cmd(struct psp_context *psp, int cmd)
{
	uint32_t reg_status = 0, reg_val = 0;
	struct amdgpu_device *adev = psp->adev;
	int ret;

	/* clear MBX ready (MBOX_READY_MASK bit is 0) and set update command */
	reg_val |= (cmd << 16);
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115,  reg_val);

	/* Ring the doorbell */
	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_73, 1);

	if (cmd == C2PMSG_CMD_SPI_UPDATE_FLASH_IMAGE)
		ret = psp_wait_for_spirom_update(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115),
						 MBOX_READY_FLAG, MBOX_READY_MASK, PSP_SPIROM_UPDATE_TIMEOUT);
	else
		ret = psp_wait_for(
			psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115),
			MBOX_READY_FLAG, MBOX_READY_MASK, 0);

	ret = psp_wait_for(psp,
			   SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115),
			   MBOX_READY_FLAG, MBOX_READY_MASK, 0);
	if (ret) {
		dev_err(adev->dev, "SPI cmd %x timed out, ret = %d", cmd, ret);
		return ret;
	}

	reg_status = RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115);
	if ((reg_status & 0xFFFF) != 0) {
		dev_err(adev->dev, "SPI cmd %x failed, fail status = %04x\n",
				cmd, reg_status & 0xFFFF);
		return -EIO;
	}

	return 0;
}

static int psp_v15_0_3_update_spirom(struct psp_context *psp,
				   uint64_t fw_pri_mc_addr)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	if (amdgpu_emu_mode == 1)
		return 0;

	/* Confirm PSP is ready to start */
	ret = psp_wait_for(psp,
			   SOC15_REG_OFFSET(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115),
			   MBOX_READY_FLAG, MBOX_READY_MASK, 0);
	if (ret) {
		dev_err(adev->dev, "PSP Not ready to start processing, ret = %d", ret);
		return ret;
	}

	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_116, lower_32_bits(fw_pri_mc_addr));

	ret = psp_v15_0_3_exec_spi_cmd(psp, C2PMSG_CMD_SPI_UPDATE_ROM_IMAGE_ADDR_LO);
	if (ret)
		return ret;

	WREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_116, upper_32_bits(fw_pri_mc_addr));

	ret = psp_v15_0_3_exec_spi_cmd(psp, C2PMSG_CMD_SPI_UPDATE_ROM_IMAGE_ADDR_HI);
	if (ret)
		return ret;

	psp->vbflash_done = true;

	ret = psp_v15_0_3_exec_spi_cmd(psp, C2PMSG_CMD_SPI_UPDATE_FLASH_IMAGE);
	if (ret)
		return ret;

	return 0;
}

static int psp_v15_0_3_vbflash_status(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	return RREG32_SOC15(MP0, 0, regMPASP_PCRU0_MPASP_C2PMSG_115);
}

static int psp_v15_0_3_get_fw_type(struct amdgpu_firmware_info *ucode,
			   enum psp_gfx_fw_type *type)
{
	switch (ucode->ucode_id) {
	case AMDGPU_UCODE_ID_CAP:
		*type = GFX_FW_TYPE_CAP;
		break;
	case AMDGPU_UCODE_ID_SDMA_RS64:
		*type = GFX_FW_TYPE_SDMA0;
		break;
	case AMDGPU_UCODE_ID_CP_MES:
		*type = GFX_FW_TYPE_CP_MES;
		break;
	case AMDGPU_UCODE_ID_CP_MES_DATA:
		*type = GFX_FW_TYPE_MES_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_MES1:
		*type = GFX_FW_TYPE_CP_MES_KIQ;
		break;
	case AMDGPU_UCODE_ID_CP_MES1_DATA:
		*type = GFX_FW_TYPE_MES_KIQ_STACK;
		break;
	case AMDGPU_UCODE_ID_RLC_V:
		*type = GFX_FW_TYPE_RLC_V;
		break;
	case AMDGPU_UCODE_ID_RLC_G:
		*type = GFX_FW_TYPE_RLC_G;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_CNTL;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_GPM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_IRAM:
		*type = GFX_FW_TYPE_RLC_IRAM;
		break;
	case AMDGPU_UCODE_ID_RLC_DRAM:
		*type = GFX_FW_TYPE_RLC_DRAM_BOOT;
		break;
	case AMDGPU_UCODE_ID_SMC:
		*type = GFX_FW_TYPE_SMU;
		break;
	case AMDGPU_UCODE_ID_PPTABLE:
		*type = GFX_FW_TYPE_PPTABLE;
		break;
	case AMDGPU_UCODE_ID_DMCUB:
		*type = GFX_FW_TYPE_DMUB;
		break;
	case AMDGPU_UCODE_ID_IMU_I:
		*type = GFX_FW_TYPE_IMU_I;
		break;
	case AMDGPU_UCODE_ID_IMU_D:
		*type = GFX_FW_TYPE_IMU_D;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP:
		*type = GFX_FW_TYPE_RS64_PFP;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME:
		*type = GFX_FW_TYPE_RS64_ME;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC:
		*type = GFX_FW_TYPE_RS64_MEC;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK:
		*type = GFX_FW_TYPE_RS64_PFP_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK:
		*type = GFX_FW_TYPE_RS64_PFP_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK:
		*type = GFX_FW_TYPE_RS64_ME_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK:
		*type = GFX_FW_TYPE_RS64_ME_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P2_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P3_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P4_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P4_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P5_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P5_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P6_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P6_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P7_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P7_STACK;
		break;
	case AMDGPU_UCODE_ID_VPE_CTX:
		*type = GFX_FW_TYPE_VPEC_FW1;
		break;
	case AMDGPU_UCODE_ID_VPE_CTL:
		*type = GFX_FW_TYPE_VPEC_FW2;
		break;
	case AMDGPU_UCODE_ID_VPE:
		*type = GFX_FW_TYPE_VPE;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_UCODE:
		*type = GFX_FW_TYPE_UMSCH_UCODE;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_DATA:
		*type = GFX_FW_TYPE_UMSCH_DATA;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_CMD_BUFFER:
		*type = GFX_FW_TYPE_UMSCH_CMD_BUFFER;
		break;
	case AMDGPU_UCODE_ID_MP5:
		*type = GFX_FW_TYPE_MP5;
		break;
	case AMDGPU_UCODE_ID_MAXIMUM:
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct psp_funcs psp_v15_0_3_funcs = {
	.init_microcode = psp_v15_0_3_init_microcode,
	.bootloader_load_kdb = psp_v15_0_3_bootloader_load_kdb,
	.bootloader_load_spl = psp_v15_0_3_bootloader_load_spl,
	.bootloader_load_sysdrv = psp_v15_0_3_bootloader_load_sysdrv,
	.bootloader_load_soc_drv = psp_v15_0_3_bootloader_load_soc_drv,
	.bootloader_load_intf_drv = psp_v15_0_3_bootloader_load_intf_drv,
	.bootloader_load_dbg_drv = psp_v15_0_3_bootloader_load_dbg_drv,
	.bootloader_load_ras_drv = psp_v15_0_3_bootloader_load_ras_drv,
	.bootloader_load_ipkeymgr_drv = psp_v15_0_3_bootloader_load_ipkeymgr_drv,
	.bootloader_load_sos = psp_v15_0_3_bootloader_load_sos,
	.ring_create = psp_v15_0_3_ring_create,
	.ring_stop = psp_v15_0_3_ring_stop,
	.ring_destroy = psp_v15_0_3_ring_destroy,
	.ring_get_wptr = psp_v15_0_3_ring_get_wptr,
	.ring_set_wptr = psp_v15_0_3_ring_set_wptr,
	.mem_training = psp_v15_0_3_memory_training,
	.load_usbc_pd_fw = psp_v15_0_3_load_usbc_pd_fw,
	.read_usbc_pd_fw = psp_v15_0_3_read_usbc_pd_fw,
	.update_spirom = psp_v15_0_3_update_spirom,
	.vbflash_stat = psp_v15_0_3_vbflash_status,
	.get_fw_type = psp_v15_0_3_get_fw_type,
};

void psp_v15_0_3_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v15_0_3_funcs;
}
