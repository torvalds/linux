// SPDX-License-Identifier: GPL-2.0+
/*
 * File Name     : video_main.c
 * Description   : AST2600 RVAS hardware engines
 *
 * Copyright (C) ASPEED Technology Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/reset.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mm.h>
#include <linux/of_reserved_mem.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/clk.h>

#include "video_ioctl.h"
#include "hardware_engines.h"
#include "video.h"
#include "video_debug.h"
#include "video_engine.h"

#define TEST_GRCE_DETECT_RESOLUTION_CHG

static long video_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int video_open(struct inode *pInode, struct file *pFile);
static int video_release(struct inode *pInode, struct file *pFile);
static irqreturn_t fge_handler(int irq, void *dev_id);

static void video_os_init_sleep_struct(struct Video_OsSleepStruct *Sleep);
static void video_ss_wakeup_on_timeout(struct Video_OsSleepStruct *Sleep);
static void enable_rvas_engines(struct AstRVAS *pAstRVAS);
static void video_engine_init(void);
static void rvas_init(void);
static void reset_rvas_engine(struct AstRVAS *pAstRVAS);
static void reset_video_engine(struct AstRVAS *pAstRVAS);
static void set_FBInfo_size(struct AstRVAS *pAstRVAS, void __iomem *mcr_base);

static long video_os_sleep_on_timeout(struct Video_OsSleepStruct *Sleep, u8 *Var, long msecs);

static struct AstRVAS *pAstRVAS;
static void __iomem *dp_base;

static long video_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int iResult = 0;
	struct RvasIoctl ri;
	struct VideoConfig video_config;
	struct MultiJpegConfig multi_jpeg;
	u8 bVideoCmd = 0;
	u32 dw_phys = 0;

	VIDEO_DBG("Start\n");
	VIDEO_DBG("pAstRVAS: 0x%p\n", pAstRVAS);
	memset(&ri, 0, sizeof(ri));

	if (cmd != CMD_IOCTL_SET_VIDEO_ENGINE_CONFIG &&
	    cmd != CMD_IOCTL_GET_VIDEO_ENGINE_CONFIG &&
	    cmd != CMD_IOCTL_GET_VIDEO_ENGINE_DATA) {
		if (raw_copy_from_user(&ri, (void *)arg, sizeof(struct RvasIoctl))) {
			dev_err(pAstRVAS->pdev, "Copy from user buffer Failed\n");
			return -EINVAL;
		}

		ri.rs = SuccessStatus;
		bVideoCmd = 0;
	} else {
		bVideoCmd = 1;
	}

	VIDEO_DBG(" Command = 0x%x\n", cmd);

	switch (cmd) {
	case CMD_IOCTL_TURN_LOCAL_MONITOR_ON:
		ioctl_update_lms(0x1, pAstRVAS);
		break;

	case CMD_IOCTL_TURN_LOCAL_MONITOR_OFF:
		ioctl_update_lms(0x0, pAstRVAS);
		break;

	case CMD_IOCTL_IS_LOCAL_MONITOR_ENABLED:
		if (ioctl_get_lm_status(pAstRVAS))
			ri.lms = 0x1;
		else
			ri.lms = 0x0;
		break;

	case CMD_IOCTL_GET_VIDEO_GEOMETRY:
		VIDEO_DBG(" Command CMD_IOCTL_GET_VIDEO_GEOMETRY\n");
		ioctl_get_video_geometry(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_WAIT_FOR_VIDEO_EVENT:
		VIDEO_DBG(" Command CMD_IOCTL_WAIT_FOR_VIDEO_EVENT\n");
		ioctl_wait_for_video_event(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_GET_GRC_REGIESTERS:
		VIDEO_DBG(" Command CMD_IOCTL_GET_GRC_REGIESTERS\n");
		ioctl_get_grc_register(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_READ_SNOOP_MAP:
		VIDEO_DBG(" Command CMD_IOCTL_READ_SNOOP_MAP\n");
		ioctl_read_snoop_map(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_READ_SNOOP_AGGREGATE:
		VIDEO_DBG(" Command CMD_IOCTL_READ_SNOOP_AGGREGATE\n");
		ioctl_read_snoop_aggregate(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_FETCH_VIDEO_TILES: ///
		VIDEO_DBG("CMD_IOCTL_FETCH_VIDEO_TILES\n");
		ioctl_fetch_video_tiles(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_FETCH_VIDEO_SLICES:
		VIDEO_DBG(" Command CMD_IOCTL_FETCH_VIDEO_SLICES\n");
		ioctl_fetch_video_slices(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_RUN_LENGTH_ENCODE_DATA:
		VIDEO_DBG(" Command CMD_IOCTL_RUN_LENGTH_ENCODE_DATA\n");
		ioctl_run_length_encode_data(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_FETCH_TEXT_DATA:
		VIDEO_DBG(" Command CMD_IOCTL_FETCH_TEXT_DATA\n");
		ioctl_fetch_text_data(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_FETCH_MODE13_DATA:
		VIDEO_DBG(" Command CMD_IOCTL_FETCH_MODE13_DATA\n");
		ioctl_fetch_mode_13_data(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_ALLOC:
		VIDEO_DBG(" Command CMD_IOCTL_ALLOC\n");
		ioctl_alloc(file, &ri, pAstRVAS);
		break;

	case CMD_IOCTL_FREE:
		VIDEO_DBG(" Command CMD_IOCTL_FREE\n");
		ioctl_free(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_NEW_CONTEXT:
		VIDEO_DBG(" Command CMD_IOCTL_NEW_CONTEXT\n");
		ioctl_new_context(file, &ri, pAstRVAS);
		break;

	case CMD_IOCTL_DEL_CONTEXT:
		VIDEO_DBG(" Command CMD_IOCTL_DEL_CONTEXT\n");
		ioctl_delete_context(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_SET_TSE_COUNTER:
		VIDEO_DBG(" Command CMD_IOCTL_SET_TSE_COUNTER\n");
		ioctl_set_tse_tsicr(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_GET_TSE_COUNTER:
		VIDEO_DBG(" Command CMD_IOCTL_GET_TSE_COUNTER\n");
		ioctl_get_tse_tsicr(&ri, pAstRVAS);
		break;

	case CMD_IOCTL_VIDEO_ENGINE_RESET:
		VIDEO_ENG_DBG(" Command CMD_IOCTL_VIDEO_ENGINE_RESET\n");
		ioctl_reset_video_engine(&ri, pAstRVAS);
		break;
	case CMD_IOCTL_GET_VIDEO_ENGINE_CONFIG:
		VIDEO_DBG(" Command CMD_IOCTL_GET_VIDEO_ENGINE_CONFIG\n");
		ioctl_get_video_engine_config(&video_config, pAstRVAS);

		iResult = raw_copy_to_user((void *)arg, &video_config, sizeof(video_config));
		break;
	case CMD_IOCTL_SET_VIDEO_ENGINE_CONFIG:
		VIDEO_DBG(" Command CMD_IOCTL_SET_VIDEO_ENGINE_CONFIG\n");
		iResult = raw_copy_from_user(&video_config, (void *)arg, sizeof(video_config));

		ioctl_set_video_engine_config(&video_config, pAstRVAS);
		break;
	case CMD_IOCTL_GET_VIDEO_ENGINE_DATA:
		VIDEO_DBG(" Command CMD_IOCTL_GET_VIDEO_ENGINE_DATA\n");
		iResult = raw_copy_from_user(&multi_jpeg, (void *)arg, sizeof(multi_jpeg));
		dw_phys = get_phys_add_rsvd_mem((u32)multi_jpeg.aStreamHandle, pAstRVAS);
		VIDEO_DBG("physical stream address: %#x\n", dw_phys);

		if (dw_phys == 0)
			dev_err(pAstRVAS->pdev, "Error of getting stream buffer address\n");
		else
			ioctl_get_video_engine_data(&multi_jpeg, pAstRVAS, dw_phys);

		iResult = raw_copy_to_user((void *)arg, &multi_jpeg, sizeof(multi_jpeg));
		break;
	default:
		dev_err(pAstRVAS->pdev, "Unknown Ioctl: %#x\n", cmd);
		iResult = -EINVAL;
		break;
	}

	if (!iResult && !bVideoCmd)
		if (raw_copy_to_user((void *)arg, &ri, sizeof(struct RvasIoctl))) {
			dev_err(pAstRVAS->pdev, "Copy to user buffer Failed\n");
			iResult = -EINVAL;
		}

	return iResult;
}

static int video_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size;
	u32 dw_index;
	u8 found = 0;

	struct MemoryMapTable **pmmt = pAstRVAS->ppmmtMemoryTable;

	size = vma->vm_end - vma->vm_start;
	vma->vm_private_data = pAstRVAS;
	VIDEO_DBG("vma->vm_start 0x%lx, vma->vm_end 0x%lx, vma->vm_pgoff=0x%x\n",
		  vma->vm_start,
		  vma->vm_end,
		  (u32)vma->vm_pgoff);
	VIDEO_DBG("(vma->vm_pgoff << PAGE_SHIFT) = 0x%lx\n", (vma->vm_pgoff << PAGE_SHIFT));
	for (dw_index = 0; dw_index < MAX_NUM_MEM_TBL; ++dw_index) {
		if (pmmt[dw_index]) {
			VIDEO_DBG("index %d, phys_addr=0x%x, virt_addr0x%x, length=0x%x\n",
				  dw_index,
				  pmmt[dw_index]->dwPhysicalAddr,
				  (u32)pmmt[dw_index]->pvVirtualAddr,
				  pmmt[dw_index]->dwLength);
			if ((vma->vm_pgoff << PAGE_SHIFT) == (u32)pmmt[dw_index]->pvVirtualAddr) {
				found = 1;
				if (size > pmmt[dw_index]->dwLength) {
					pr_err("required size exceed alloc size\n");
					return -EAGAIN;
				}
				break;
			}
		}
	}
	if (!found) {
		pr_err("no match mem entry\n");
		return -EAGAIN;
	}

	vm_flags_set(vma, VM_IO);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start,
			       ((u32)vma->vm_pgoff), size,
			       vma->vm_page_prot)) {
		pr_err("remap_pfn_range fail at %s()\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

static int video_open(struct inode *pin, struct file *pf)
{
	VIDEO_DBG("\n");

	// make sure the rvas clk is running.
	//	 if it's already enabled, clk_enable will just return.
	clk_enable(pAstRVAS->rvasclk);

	return 0;
}

void free_all_mem_entries(struct AstRVAS *pAstRVAS)
{
	u32 dw_index;
	struct MemoryMapTable **pmmt = pAstRVAS->ppmmtMemoryTable;
	void *virt_add;
	u32 dw_phys, len;

	VIDEO_DBG("Removing mem map entries...\n");
	for (dw_index = 0; dw_index < MAX_NUM_MEM_TBL; ++dw_index) {
		if (pmmt[dw_index]) {
			if (pmmt[dw_index]->dwPhysicalAddr) {
				virt_add = get_virt_add_rsvd_mem(dw_index, pAstRVAS);
				dw_phys = get_phys_add_rsvd_mem(dw_index, pAstRVAS);
				len = get_len_rsvd_mem(dw_index, pAstRVAS);
				dma_free_coherent(pAstRVAS->pdev, len, virt_add, dw_phys);
			}
			pmmt[dw_index]->pf = NULL;
			kfree(pmmt[dw_index]);
			pmmt[dw_index] = NULL;
		}
	}
}

static int video_release(struct inode *inode, struct file *filp)
{
	u32 dw_index;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	VIDEO_DBG("Start\n");

	free_all_mem_entries(pAstRVAS);

	VIDEO_DBG("ppctContextTable: 0x%p\n", ppctContextTable);

	disable_grce_tse_interrupt(pAstRVAS);

	for (dw_index = 0; dw_index < MAX_NUM_CONTEXT; ++dw_index) {
		if (ppctContextTable[dw_index]) {
			VIDEO_DBG("Releasing Context dw_index: %u\n", dw_index);
			kfree(ppctContextTable[dw_index]);
			ppctContextTable[dw_index] = NULL;
		}
	}
	enable_grce_tse_interrupt(pAstRVAS);
	VIDEO_DBG("End\n");

	return 0;
}

static struct file_operations video_module_ops = { .compat_ioctl = video_ioctl,
	.unlocked_ioctl = video_ioctl, .open = video_open, .release =
		video_release, .mmap = video_mmap, .owner = THIS_MODULE, };

static struct miscdevice video_misc = { .minor = MISC_DYNAMIC_MINOR, .name =
	RVAS_DRIVER_NAME, .fops = &video_module_ops, };

void ioctl_new_context(struct file *file, struct RvasIoctl *pri, struct AstRVAS *pAstRVAS)
{
	struct ContextTable *pct;

	VIDEO_DBG("Start\n");
	pct = get_new_context_table_entry(pAstRVAS);

	if (pct) {
		pct->desc_virt = dma_alloc_coherent(pAstRVAS->pdev, PAGE_SIZE, (dma_addr_t *)&pct->desc_phy, GFP_KERNEL);
		if (!pct->desc_virt) {
			pri->rs = MemoryAllocError;
			return;
		}
		pri->rc = pct->rc;
	} else {
		pri->rs = MemoryAllocError;
	}

	VIDEO_DBG("end: return status: %d\n", pri->rs);
}

void ioctl_delete_context(struct RvasIoctl *pri, struct AstRVAS *pAstRVAS)
{
	VIDEO_DBG("Start\n");

	VIDEO_DBG("pri->rc: %d\n", pri->rc);
	if (remove_context_table_entry(pri->rc, pAstRVAS)) {
		VIDEO_DBG("Success in removing\n");
		pri->rs = SuccessStatus;
	} else {
		VIDEO_DBG("Failed in removing\n");
		pri->rs = InvalidMemoryHandle;
	}
}

int get_mem_entry(struct AstRVAS *pAstRVAS)
{
	int index = 0;
	u32 dw_size = 0;
	bool found = false;

	down(&pAstRVAS->mem_sem);
	do {
		if (pAstRVAS->ppmmtMemoryTable[index]) {
			index++;
		} else {
			found = true;
			break;
		}

	} while (!found && (index < MAX_NUM_MEM_TBL));

	if (found) {
		dw_size = sizeof(struct MemoryMapTable);
		pAstRVAS->ppmmtMemoryTable[index] = kmalloc(dw_size, GFP_KERNEL);
		if (!pAstRVAS->ppmmtMemoryTable[index])
			index = -1;
	} else {
		index = -1;
	}

	up(&pAstRVAS->mem_sem);
	return index;
}

bool delete_mem_entry(const void *crmh, struct AstRVAS *pAstRVAS)
{
	bool b_ret = false;
	u32 dw_index = (u32)crmh;

	VIDEO_DBG("Start, dw_index: %#x\n", dw_index);

	down(&pAstRVAS->mem_sem);
	if (dw_index < MAX_NUM_MEM_TBL && pAstRVAS->ppmmtMemoryTable[dw_index]) {
		VIDEO_DBG("mem: 0x%p\n", pAstRVAS->ppmmtMemoryTable[dw_index]);
		kfree(pAstRVAS->ppmmtMemoryTable[dw_index]);
		pAstRVAS->ppmmtMemoryTable[dw_index] = NULL;
		b_ret = true;
	}
	up(&pAstRVAS->mem_sem);
	VIDEO_DBG("End\n");
	return b_ret;
}

void *get_virt_add_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS)
{
	if (index < MAX_NUM_MEM_TBL && pAstRVAS->ppmmtMemoryTable[index])
		return pAstRVAS->ppmmtMemoryTable[index]->pvVirtualAddr;

	return 0;
}

u32 get_phys_add_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS)
{
	if (index < MAX_NUM_MEM_TBL && pAstRVAS->ppmmtMemoryTable[index])
		return pAstRVAS->ppmmtMemoryTable[index]->dwPhysicalAddr;

	return 0;
}

u32 get_len_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS)
{
	u32 len = 0;

	if (index < MAX_NUM_MEM_TBL && pAstRVAS->ppmmtMemoryTable[index])
		len = pAstRVAS->ppmmtMemoryTable[index]->dwLength;

	return len;
}

bool virt_is_valid_rsvd_mem(u32 index, u32 size, struct AstRVAS *pAstRVAS)
{
	if (index < MAX_NUM_MEM_TBL &&
	    pAstRVAS->ppmmtMemoryTable[index] &&
	    pAstRVAS->ppmmtMemoryTable[index]->dwLength)
		return true;

	return false;
}

void ioctl_alloc(struct file *pfile, struct RvasIoctl *pri, struct AstRVAS *pAstRVAS)
{
	u32 size;
	u32 phys_add = 0;
	u32 virt_add = 0;
	u32 index = get_mem_entry(pAstRVAS);

	if (index < 0 || index >= MAX_NUM_MEM_TBL) {
		pri->rs = MemoryAllocError;
		return;
	}
	if (pri->req_mem_size < PAGE_SIZE)
		pri->req_mem_size = PAGE_SIZE;

	size = pri->req_mem_size;

	VIDEO_DBG("Allocating memory size: 0x%x\n", size);
	virt_add = (u32)dma_alloc_coherent(pAstRVAS->pdev, size, &phys_add,
					   GFP_KERNEL);
	if (virt_add) {
		pri->rmh = (void *)index;
		pri->rvb.pv = (void *)phys_add;
		pri->rvb.cb = size;
		pri->rs = SuccessStatus;
		pAstRVAS->ppmmtMemoryTable[index]->pf = pfile;
		pAstRVAS->ppmmtMemoryTable[index]->dwPhysicalAddr = phys_add;
		pAstRVAS->ppmmtMemoryTable[index]->pvVirtualAddr = (void *)virt_add;
		pAstRVAS->ppmmtMemoryTable[index]->dwLength = size;
		pAstRVAS->ppmmtMemoryTable[index]->byDmaAlloc = 1;
	} else {
		if (pAstRVAS->ppmmtMemoryTable[index])
			delete_mem_entry((void *)index, pAstRVAS);

		pr_err("Cannot alloc video destination data buffer\n");
		pri->rs = MemoryAllocError;
	}
	VIDEO_DBG("Allocated: index: 0x%x phys: %#x cb: 0x%x\n", index,
		  phys_add, pri->rvb.cb);
}

void ioctl_free(struct RvasIoctl *pri, struct AstRVAS *pAstRVAS)
{
	void *virt_add = get_virt_add_rsvd_mem((u32)pri->rmh, pAstRVAS);
	u32 dw_phys = get_phys_add_rsvd_mem((u32)pri->rmh, pAstRVAS);
	u32 len = get_len_rsvd_mem((u32)pri->rmh, pAstRVAS);

	VIDEO_DBG("Start\n");
	VIDEO_DBG("Freeing: rmh: 0x%p, phys: 0x%x, size 0x%x virt_add: 0x%p len: %u\n",
		  pri->rmh, dw_phys, pri->rvb.cb, virt_add, len);

	delete_mem_entry(pri->rmh, pAstRVAS);
	VIDEO_DBG("After delete_mem_entry\n");

	dma_free_coherent(pAstRVAS->pdev, len,
			  virt_add,
			  dw_phys);
	VIDEO_DBG("After dma_free_coherent\n");
}

void ioctl_update_lms(u8 lms_on, struct AstRVAS *pAstRVAS)
{
	u32 reg_scu418 = 0;
	u32 reg_scu0C0 = 0;
	u32 reg_scu0D0 = 0;
	u32 reg_dptx100 = 0;
	u32 reg_dptx104 = 0;

	regmap_read(pAstRVAS->scu, SCU418_Pin_Ctrl, &reg_scu418);
	regmap_read(pAstRVAS->scu, SCU0C0_Misc1_Ctrl, &reg_scu0C0);
	regmap_read(pAstRVAS->scu, SCU0D0_Misc3_Ctrl, &reg_scu0D0);
	if (dp_base) {
		reg_dptx100 = readl(dp_base + DPTX_Configuration_Register);
		reg_dptx104 = readl(dp_base + DPTX_PHY_Configuration_Register);
	}

	if (lms_on) {
		if (!(reg_scu418 & (VGAVS_ENBL | VGAHS_ENBL))) {
			reg_scu418 |= (VGAVS_ENBL | VGAHS_ENBL);
			regmap_write(pAstRVAS->scu, SCU418_Pin_Ctrl, reg_scu418);
		}
		if (reg_scu0C0 & VGA_CRT_DISBL) {
			reg_scu0C0 &= ~VGA_CRT_DISBL;
			regmap_write(pAstRVAS->scu, SCU0C0_Misc1_Ctrl, reg_scu0C0);
		}
		if (reg_scu0D0 & PWR_OFF_VDAC) {
			reg_scu0D0 &= ~PWR_OFF_VDAC;
			regmap_write(pAstRVAS->scu, SCU0D0_Misc3_Ctrl, reg_scu0D0);
		}
		//dp output
		if (dp_base) {
			reg_dptx100 |= 1 << AUX_RESETN;
			writel(reg_dptx100, dp_base + DPTX_Configuration_Register);
		}
	} else { //turn off
		if (reg_scu418 & (VGAVS_ENBL | VGAHS_ENBL)) {
			reg_scu418 &= ~(VGAVS_ENBL | VGAHS_ENBL);
			regmap_write(pAstRVAS->scu, SCU418_Pin_Ctrl, reg_scu418);
		}
		if (!(reg_scu0C0 & VGA_CRT_DISBL)) {
			reg_scu0C0 |= VGA_CRT_DISBL;
			regmap_write(pAstRVAS->scu, SCU0C0_Misc1_Ctrl, reg_scu0C0);
		}
		if (!(reg_scu0D0 & PWR_OFF_VDAC)) {
			reg_scu0D0 |= PWR_OFF_VDAC;
			regmap_write(pAstRVAS->scu, SCU0D0_Misc3_Ctrl, reg_scu0D0);
		}
		//dp output
		if (dp_base) {
			reg_dptx100 &= ~(1 << AUX_RESETN);
			writel(reg_dptx100, dp_base + DPTX_Configuration_Register);
			reg_dptx104 &= ~(1 << DP_TX_I_MAIN_ON);
			writel(reg_dptx104, dp_base + DPTX_PHY_Configuration_Register);
		}
	}
}

u32 ioctl_get_lm_status(struct AstRVAS *pAstRVAS)
{
	u32 reg_val = 0;

	regmap_read(pAstRVAS->scu, SCU418_Pin_Ctrl, &reg_val);
	if (reg_val & (VGAVS_ENBL | VGAHS_ENBL)) {
		regmap_read(pAstRVAS->scu, SCU0C0_Misc1_Ctrl, &reg_val);
		if (!(reg_val & VGA_CRT_DISBL)) {
			regmap_read(pAstRVAS->scu, SCU0D0_Misc3_Ctrl, &reg_val);
			if (!(reg_val & PWR_OFF_VDAC))
				return 1;
		}
	}
	return 0;
}

void init_osr_es(struct AstRVAS *pAstRVAS)
{
	VIDEO_DBG("Start\n");
	sema_init(&pAstRVAS->mem_sem, 1);
	sema_init(&pAstRVAS->context_sem, 1);

	video_os_init_sleep_struct(&pAstRVAS->video_wait);

	memset(&pAstRVAS->tfe_engine, 0x00, sizeof(struct EngineInfo));
	memset(&pAstRVAS->bse_engine, 0x00, sizeof(struct EngineInfo));
	memset(&pAstRVAS->ldma_engine, 0x00, sizeof(struct EngineInfo));
	sema_init(&pAstRVAS->tfe_engine.sem, 1);
	sema_init(&pAstRVAS->bse_engine.sem, 1);
	sema_init(&pAstRVAS->ldma_engine.sem, 1);
	video_os_init_sleep_struct(&pAstRVAS->tfe_engine.wait);
	video_os_init_sleep_struct(&pAstRVAS->bse_engine.wait);
	video_os_init_sleep_struct(&pAstRVAS->ldma_engine.wait);

	memset(pAstRVAS->ppctContextTable, 0x00, MAX_NUM_CONTEXT * sizeof(u32));
	pAstRVAS->dwMemoryTableSize = MAX_NUM_MEM_TBL;
	memset(pAstRVAS->ppmmtMemoryTable, 0x00, MAX_NUM_MEM_TBL * sizeof(u32));
	VIDEO_DBG("End\n");
}

void release_osr_es(struct AstRVAS *pAstRVAS)
{
	u32 dw_index;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	VIDEO_DBG("Removing contexts...\n");
	for (dw_index = 0; dw_index < MAX_NUM_CONTEXT; ++dw_index) {
		//if (ppctContextTable[dw_index]) {
		kfree(ppctContextTable[dw_index]);
		ppctContextTable[dw_index] = NULL;
		//} // kfree(NULL) is safe and this check is probably not require
	}

	free_all_mem_entries(pAstRVAS);
}

//Retrieve a context entry
struct ContextTable *get_context_entry(const void *crc, struct AstRVAS *pAstRVAS)
{
	struct ContextTable *pct = NULL;
	u32 dw_index = (u32)crc;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	if (dw_index < MAX_NUM_CONTEXT && ppctContextTable[dw_index] &&
	    ppctContextTable[dw_index]->rc == crc)
		pct = ppctContextTable[dw_index];

	return pct;
}

struct ContextTable *get_new_context_table_entry(struct AstRVAS *pAstRVAS)
{
	struct ContextTable *pct = NULL;
	u32 dw_index = 0;
	bool b_found = false;
	u32 dw_size = 0;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	disable_grce_tse_interrupt(pAstRVAS);
	down(&pAstRVAS->context_sem);
	while (!b_found && (dw_index < MAX_NUM_CONTEXT)) {
		if (!(ppctContextTable[dw_index]))
			b_found = true;
		else
			++dw_index;
	}
	if (b_found) {
		dw_size = sizeof(struct ContextTable);
		pct = kmalloc(dw_size, GFP_KERNEL);

		if (pct) {
			memset(pct, 0x00, sizeof(struct ContextTable));
			pct->rc = (void *)dw_index;
			memset(&pct->aqwSnoopMap, 0xff,
			       sizeof(pct->aqwSnoopMap));
			memset(&pct->sa, 0xff, sizeof(pct->sa));
			ppctContextTable[dw_index] = pct;
		}
	}
	up(&pAstRVAS->context_sem);
	enable_grce_tse_interrupt(pAstRVAS);

	return pct;
}

bool remove_context_table_entry(const void *crc, struct AstRVAS *pAstRVAS)
{
	bool b_ret = false;
	u32 dw_index = (u32)crc;
	struct ContextTable *ctx_entry;

	VIDEO_DBG("Start\n");

	VIDEO_DBG("dw_index: %u\n", dw_index);

	if (dw_index < MAX_NUM_CONTEXT) {
		ctx_entry = pAstRVAS->ppctContextTable[dw_index];
		VIDEO_DBG("ctx_entry: 0x%p\n", ctx_entry);

		if (ctx_entry) {
			disable_grce_tse_interrupt(pAstRVAS);
			if (!ctx_entry->desc_virt) {
				VIDEO_DBG("Removing memory, virt: 0x%p phys: %#x\n",
					  ctx_entry->desc_virt,
					  ctx_entry->desc_phy);

				dma_free_coherent(pAstRVAS->pdev, PAGE_SIZE, ctx_entry->desc_virt, ctx_entry->desc_phy);
			}
			VIDEO_DBG("Removing memory: 0x%p\n", ctx_entry);
			pAstRVAS->ppctContextTable[dw_index] = NULL;
			kfree(ctx_entry);
			b_ret = true;
			enable_grce_tse_interrupt(pAstRVAS);
		}
	}
	return b_ret;
}

void display_event_map(const struct EventMap *pem)
{
	VIDEO_DBG("EM:\n");
	VIDEO_DBG("*************************\n");
	VIDEO_DBG("  bATTRChanged=      %u\n", pem->bATTRChanged);
	VIDEO_DBG("  bCRTCChanged=      %u\n", pem->bCRTCChanged);
	VIDEO_DBG("  bCRTCEXTChanged=   %u\n", pem->bCRTCEXTChanged);
	VIDEO_DBG("  bDoorbellA=        %u\n", pem->bDoorbellA);
	VIDEO_DBG("  bDoorbellB=        %u\n", pem->bDoorbellB);
	VIDEO_DBG("  bGCTLChanged=      %u\n", pem->bGCTLChanged);
	VIDEO_DBG("  bGeometryChanged=  %u\n", pem->bGeometryChanged);
	VIDEO_DBG("  bPLTRAMChanged=    %u\n", pem->bPLTRAMChanged);
	VIDEO_DBG("  bPaletteChanged=   %u\n", pem->bPaletteChanged);
	VIDEO_DBG("  bSEQChanged=       %u\n", pem->bSEQChanged);
	VIDEO_DBG("  bSnoopChanged=     %u\n", pem->bSnoopChanged);
	VIDEO_DBG("  bTextASCIIChanged= %u\n", pem->bTextASCIIChanged);
	VIDEO_DBG("  bTextATTRChanged=  %u\n", pem->bTextATTRChanged);
	VIDEO_DBG("  bTextFontChanged=  %u\n", pem->bTextFontChanged);
	VIDEO_DBG("  bXCURCOLChanged=   %u\n", pem->bXCURCOLChanged);
	VIDEO_DBG("  bXCURCTLChanged=   %u\n", pem->bXCURCTLChanged);
	VIDEO_DBG("  bXCURPOSChanged=   %u\n", pem->bXCURPOSChanged);
	VIDEO_DBG("*************************\n");
}

void ioctl_wait_for_video_event(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	union EmDwordUnion eduRequested;
	union EmDwordUnion eduReturned;
	union EmDwordUnion eduChanged;
	struct EventMap anEm;
	u32 result = 1;
	int iTimerRemaining = ri->time_out;
	unsigned long ulTimeStart, ulTimeEnd, ulElapsedTime;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	memset(&anEm, 0x0, sizeof(struct EventMap));

	VIDEO_DBG("Calling VideoSleepOnTimeout\n");

	eduRequested.em = ri->em;
	VIDEO_DBG("eduRequested.em:\n");
	display_event_map(&eduRequested.em);
	eduChanged.em = ppctContextTable[(int)ri->rc]->emEventReceived;
	VIDEO_DBG("eduChanged.em:\n");
	display_event_map(&eduChanged.em);

	// While event has not occurred and there is still time remaining for wait
	while (!(eduChanged.dw & eduRequested.dw) && (iTimerRemaining > 0) &&
	       result) {
		pAstRVAS->video_intr_occurred = 0;
		ulTimeStart = jiffies_to_msecs(jiffies);
		result = video_os_sleep_on_timeout(&pAstRVAS->video_wait,
						   &pAstRVAS->video_intr_occurred,
						   iTimerRemaining);
		ulTimeEnd = jiffies_to_msecs(jiffies);
		ulElapsedTime = (ulTimeEnd - ulTimeStart);
		iTimerRemaining -= (int)ulElapsedTime;
		eduChanged.em = ppctContextTable[(int)ri->rc]->emEventReceived;
//    VIDEO_DBG("Elapsedtime [%u], timestart[%u], timeend[%u]\n", dwElapsedTime, dwTimeStart, dwTimeEnd);

		VIDEO_DBG("ulElapsedTime [%lu], ulTimeStart[%lu], ulTimeEnd[%lu]\n",
			  ulElapsedTime, ulTimeStart, ulTimeEnd);
		VIDEO_DBG("HZ [%ul]\n", HZ);
		VIDEO_DBG("result [%u], iTimerRemaining [%d]\n", result,
			  iTimerRemaining);
	}

	if (result == 0 && ri->time_out != 0) {
		VIDEO_DBG("IOCTL Timedout\n");
		ri->rs = TimedOut;
		memset(&ri->em, 0x0, sizeof(struct EventMap));
	} else {
		eduChanged.em = ppctContextTable[(int)ri->rc]->emEventReceived;
		VIDEO_DBG("Event Received[%X]\n", eduChanged.dw);
		// Mask out the changes we are waiting on
		eduReturned.dw = eduChanged.dw & eduRequested.dw;

		// Reset flags of changes that have been returned
		eduChanged.dw &= ~(eduReturned.dw);
		VIDEO_DBG("Event Reset[%X]\n", eduChanged.dw);
		ppctContextTable[(int)ri->rc]->emEventReceived = eduChanged.em;

		// Copy changes back to ri
		ri->em = eduReturned.em;
		VIDEO_DBG("ri->em:\n");
		display_event_map(&ri->em);
		ri->rs = SuccessStatus;
		VIDEO_DBG("Success [%x]\n",
			  eduReturned.dw);
	}
}

static void update_context_events(struct AstRVAS *pAstRVAS,
				  union EmDwordUnion eduFge_status)
{
	union EmDwordUnion eduEmReceived;
	u32 dwIter = 0;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;
	// VIDEO_DBG("Setting up context\n");
	for (dwIter = 0; dwIter < MAX_NUM_CONTEXT; ++dwIter) {
		if (ppctContextTable[dwIter]) {
			memcpy((void *)&eduEmReceived,
			       (void *)&ppctContextTable[dwIter]->emEventReceived,
			       sizeof(union EmDwordUnion));
			eduEmReceived.dw |= eduFge_status.dw;
			memcpy((void *)&ppctContextTable[dwIter]->emEventReceived,
			       (void *)&eduEmReceived,
			       sizeof(union EmDwordUnion));
		}
	}
	pAstRVAS->video_intr_occurred = 1;
	video_ss_wakeup_on_timeout(&pAstRVAS->video_wait);
}

static irqreturn_t fge_handler(int irq, void *dev_id)
{
	union EmDwordUnion eduFge_status;
	u32 tse_sts = 0;
	u32 dwGRCEStatus = 0;
	bool bFgeItr = false;
	bool bTfeItr = false;
	bool bBSEItr = false;
	bool bLdmaItr = false;
	bool vg_changed = false;
	u32 dw_screen_offset = 0;
	struct AstRVAS *pAstRVAS = (struct AstRVAS *)dev_id;
	struct VideoGeometry *cur_vg = NULL;

	memset(&eduFge_status, 0x0, sizeof(union EmDwordUnion));
	bFgeItr = false;
	VIDEO_DBG("fge_handler");
	// Checking for GRC status changes
	dwGRCEStatus = readl((void *)(pAstRVAS->grce_reg_base + GRCE_STATUS_REGISTER));
	if (dwGRCEStatus & GRC_INT_STS_MASK) {
		VIDEO_DBG("GRC Status Changed: %#x\n", dwGRCEStatus);
		eduFge_status.dw |= dwGRCEStatus & GRC_INT_STS_MASK;
		bFgeItr = true;

		if (dwGRCEStatus & 0x30) {
			dw_screen_offset = get_screen_offset(pAstRVAS);

			if (pAstRVAS->dwScreenOffset != dw_screen_offset) {
				pAstRVAS->dwScreenOffset = dw_screen_offset;
				vg_changed = true;
			}
		}
	}
	vg_changed |= video_geometry_change(pAstRVAS, dwGRCEStatus);
	if (vg_changed) {
		eduFge_status.em.bGeometryChanged = true;
		bFgeItr = true;
		set_snoop_engine(vg_changed, pAstRVAS);
		video_set_Window(pAstRVAS);
		VIDEO_DBG("Geometry has changed\n");
		VIDEO_DBG("Reconfigure TSE\n");
	}
	// Checking and clear TSE Intr Status
	tse_sts = clear_tse_interrupt(pAstRVAS);

	if (tse_sts & TSSTS_ALL) {
		bFgeItr = true;
		if (tse_sts & (TSSTS_TC_SCREEN0 | TSSTS_TC_SCREEN1)) {
			eduFge_status.em.bSnoopChanged = 1;
			cur_vg = &pAstRVAS->current_vg;

			if (cur_vg->gmt == TextMode) {
				eduFge_status.em.bTextASCIIChanged = 1;
				eduFge_status.em.bTextATTRChanged = 1;
				eduFge_status.em.bTextFontChanged = 1;
			}
		}
		if (tse_sts & TSSTS_ASCII) {
			//VIDEO_DBG("Text Ascii Changed\n");
			eduFge_status.em.bTextASCIIChanged = 1;
		}

		if (tse_sts & TSSTS_ATTR) {
			//VIDEO_DBG("Text Attr Changed\n");
			eduFge_status.em.bTextATTRChanged = 1;
		}

		if (tse_sts & TSSTS_FONT) {
			//VIDEO_DBG("Text Font Changed\n");
			eduFge_status.em.bTextFontChanged = 1;
		}
	}

	if (clear_ldma_interrupt(pAstRVAS)) {
		bLdmaItr = true;
		pAstRVAS->ldma_engine.finished = 1;
		video_ss_wakeup_on_timeout(&pAstRVAS->ldma_engine.wait);
	}

	if (clear_tfe_interrupt(pAstRVAS)) {
		bTfeItr = true;
		pAstRVAS->tfe_engine.finished = 1;
		video_ss_wakeup_on_timeout(&pAstRVAS->tfe_engine.wait);
	}

	if (clear_bse_interrupt(pAstRVAS)) {
		bBSEItr = true;
		pAstRVAS->bse_engine.finished = 1;
		video_ss_wakeup_on_timeout(&pAstRVAS->bse_engine.wait);
	}

	if (!bFgeItr && !bTfeItr && !bBSEItr && !bLdmaItr) {
		//VIDEO_DBG(" Unknown Interrupt\n");
//      VIDEO_DBG("TFE CRT [%#x].", *fge_intr);
		return IRQ_NONE;
	}

	if (bFgeItr) {
		update_context_events(pAstRVAS, eduFge_status);
		pAstRVAS->video_intr_occurred = 1;
		video_ss_wakeup_on_timeout(&pAstRVAS->video_wait);
	}

	return IRQ_HANDLED;
}

/*Sleep and Wakeup Functions*/

void video_os_init_sleep_struct(struct Video_OsSleepStruct *Sleep)
{
	init_waitqueue_head(&Sleep->queue);
	Sleep->Timeout = 0;
}

void video_ss_wakeup_on_timeout(struct Video_OsSleepStruct *Sleep)
{
	/* Wakeup Process and Kill timeout handler */
	wake_up(&Sleep->queue);
}

long video_os_sleep_on_timeout(struct Video_OsSleepStruct *Sleep, u8 *Var, long msecs)
{
	long timeout; /* In jiffies */
	u8 *Condition = Var;
	/* Sleep on the Condition for a wakeup */
	timeout = wait_event_interruptible_timeout(Sleep->queue,
						   (*Condition == 1),
						   msecs_to_jiffies(msecs));

	return timeout;
}

void disable_video_engines(struct AstRVAS *pAstRVAS)
{
	clk_disable(pAstRVAS->eclk);
	clk_disable(pAstRVAS->vclk);
}

void enable_video_engines(struct AstRVAS *pAstRVAS)
{
	clk_enable(pAstRVAS->eclk);
	clk_enable(pAstRVAS->vclk);
}

void disable_rvas_engines(struct AstRVAS *pAstRVAS)
{
	clk_disable(pAstRVAS->rvasclk);
}

void enable_rvas_engines(struct AstRVAS *pAstRVAS)
{
	// clk enable does
	//	reset engine reset at SCU040
	//	delay 100 us
	//	enable clock at SCU080
	//	delay 10ms
	//	disable engine reset at SCU040
	clk_enable(pAstRVAS->rvasclk);
}

static void reset_rvas_engine(struct AstRVAS *pAstRVAS)
{
	disable_rvas_engines(pAstRVAS);
	enable_rvas_engines(pAstRVAS);
	rvas_init();
}

static void reset_video_engine(struct AstRVAS *pAstRVAS)
{
	disable_video_engines(pAstRVAS);
	enable_video_engines(pAstRVAS);
	video_engine_init();
}

void ioctl_reset_video_engine(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	enum ResetEngineMode resetMode = ri->resetMode;

	switch (resetMode) {
	case  ResetAll:
		VIDEO_ENG_DBG("reset all engine\n");
		reset_rvas_engine(pAstRVAS);
		reset_video_engine(pAstRVAS);
		break;
	case ResetRvasEngine:
		VIDEO_ENG_DBG("reset rvas engine\n");
		reset_rvas_engine(pAstRVAS);
		break;
	case ResetVeEngine:
		VIDEO_ENG_DBG("reset video engine\n");
		reset_video_engine(pAstRVAS);
		break;
	default:
		dev_err(pAstRVAS->pdev, "Error resetting: no such mode: %d\n", resetMode);
		break;
	}

	if (ri)
		ri->rs = SuccessStatus;
}

static ssize_t rvas_reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct AstRVAS *pAstRVAS = dev_get_drvdata(dev);
	u32 val = kstrtoul(buf, 10, NULL);

	if (val)
		ioctl_reset_video_engine(NULL, pAstRVAS);

	return count;
}

static DEVICE_ATTR_WO(rvas_reset);

static struct attribute *ast_rvas_attributes[] = {
	&dev_attr_rvas_reset.attr,
	NULL
};

static const struct attribute_group rvas_attribute_group = {
	.attrs = ast_rvas_attributes
};

bool sleep_on_tfe_busy(struct AstRVAS *pAstRVAS, u32 dwTFEDescriptorAddr,
		       u32 dwTFEControlR, u32 dwTFERleLimitor,
		       u32 *pdwRLESize,	u32 *pdwCheckSum)
{
	u32 addrTFEDTBR = pAstRVAS->fg_reg_base + TFE_Descriptor_Table_Offset;
	u32 addrTFECR = pAstRVAS->fg_reg_base + TFE_Descriptor_Control_Resgister;
	u32 addrTFERleL = pAstRVAS->fg_reg_base + TFE_RLE_LIMITOR;
	u32 addrTFERSTS = pAstRVAS->fg_reg_base + TFE_Status_Register;
	bool bResult = true;

	down(&pAstRVAS->tfe_engine.sem);
	VIDEO_DBG("In Busy Semaphore......\n");

	VIDEO_DBG("Before change, TFECR: %#x\n", readl((void *)addrTFECR));
	writel(dwTFEControlR, (void *)addrTFECR);
	VIDEO_DBG("After change, TFECR: %#x\n", readl((void *)addrTFECR));
	writel(dwTFERleLimitor, (void *)addrTFERleL);
	VIDEO_DBG("dwTFEControlR: %#x\n", dwTFEControlR);
	VIDEO_DBG("dwTFERleLimitor: %#x\n", dwTFERleLimitor);
	VIDEO_DBG("dwTFEDescriptorAddr: %#x\n", dwTFEDescriptorAddr);
	// put descriptor add to TBR and Fetch start
	writel(dwTFEDescriptorAddr, (void *)addrTFEDTBR);
	//wTFETiles = 1;
	pAstRVAS->tfe_engine.finished = 0;
	video_os_sleep_on_timeout(&pAstRVAS->tfe_engine.wait,
				  &pAstRVAS->tfe_engine.finished,
				  TFE_TIMEOUT_IN_MS);

	if (!pAstRVAS->tfe_engine.finished) {
		dev_err(pAstRVAS->pdev, "Video TFE failed\n");
		writel(0x00, (void *)addrTFERSTS);
		pAstRVAS->tfe_engine.finished = 1;
		bResult = false;
	}

	writel((readl((void *)addrTFECR) & (~0x3)), (void *)addrTFECR); // Disable IRQ and Turn off TFE when done
	*pdwRLESize = readl((void *)(pAstRVAS->fg_reg_base + TFE_RLE_Byte_Count));
	*pdwCheckSum = readl((void *)(pAstRVAS->fg_reg_base + TFE_RLE_CheckSum));

	up(&pAstRVAS->tfe_engine.sem);
	VIDEO_DBG("Done Busy: bResult: %d\n", bResult);

	return bResult;
}

bool sleep_on_tfe_text_busy(struct AstRVAS *pAstRVAS, u32 dwTFEDescriptorAddr,
			    u32 dwTFEControlR, u32 dwTFERleLimitor, u32 *pdwRLESize,
			    u32 *pdwCheckSum)
{
	u32 addrTFEDTBR = pAstRVAS->fg_reg_base + TFE_Descriptor_Table_Offset;
	u32 addrTFECR = pAstRVAS->fg_reg_base + TFE_Descriptor_Control_Resgister;
	u32 addrTFERleL = pAstRVAS->fg_reg_base + TFE_RLE_LIMITOR;
	u32 addrTFERSTS = pAstRVAS->fg_reg_base + TFE_Status_Register;
	bool bResult = true;

	down(&pAstRVAS->tfe_engine.sem);
	VIDEO_DBG("In Busy Semaphore......\n");

	VIDEO_DBG("Before change, TFECR: %#x\n", readl((void *)addrTFECR));
	writel(dwTFEControlR, (void *)addrTFECR);
	VIDEO_DBG("After change, TFECR: %#x\n", readl((void *)addrTFECR));
	writel(dwTFERleLimitor, (void *)addrTFERleL);
	VIDEO_DBG("dwTFEControlR: %#x\n", dwTFEControlR);
	VIDEO_DBG("dwTFERleLimitor: %#x\n", dwTFERleLimitor);
	VIDEO_DBG("dwTFEDescriptorAddr: %#x\n", dwTFEDescriptorAddr);
	// put descriptor add to TBR and Fetch start
	writel(dwTFEDescriptorAddr, (void *)addrTFEDTBR);
	//wTFETiles = 1;
	pAstRVAS->tfe_engine.finished = 0;
	video_os_sleep_on_timeout(&pAstRVAS->tfe_engine.wait,
				  &pAstRVAS->tfe_engine.finished, TFE_TIMEOUT_IN_MS);

	if (!pAstRVAS->tfe_engine.finished) {
		dev_err(pAstRVAS->pdev, "Video TFE failed\n");
		writel(0x00, (void *)addrTFERSTS);
		pAstRVAS->tfe_engine.finished = 1;
		bResult = false;
	}

	writel((readl((void *)addrTFECR) & (~0x3)), (void *)addrTFECR);// Disable IRQ and Turn off TFE when done
	writel((readl((void *)addrTFERSTS) | 0x2), (void *)addrTFERSTS); // clear status bit
	*pdwRLESize = readl((void *)(pAstRVAS->fg_reg_base + TFE_RLE_Byte_Count));
	*pdwCheckSum = readl((void *)(pAstRVAS->fg_reg_base + TFE_RLE_CheckSum));

	up(&pAstRVAS->tfe_engine.sem);
	VIDEO_DBG("Done Busy: bResult: %d\n", bResult);

	return bResult;
}

bool sleep_on_bse_busy(struct AstRVAS *pAstRVAS, u32 dwBSEDescriptorAddr,
		       struct BSEAggregateRegister aBSEAR, u32 size)
{
	u32 addrBSEDTBR = pAstRVAS->fg_reg_base + BSE_Descriptor_Table_Base_Register;
	u32 addrBSCR = pAstRVAS->fg_reg_base + BSE_Command_Register;
	u32 addrBSDBS = pAstRVAS->fg_reg_base + BSE_Destination_Buket_Size_Resgister;
	u32 addrBSBPS0 = pAstRVAS->fg_reg_base + BSE_Bit_Position_Register_0;
	u32 addrBSBPS1 = pAstRVAS->fg_reg_base + BSE_Bit_Position_Register_1;
	u32 addrBSBPS2 = pAstRVAS->fg_reg_base + BSE_Bit_Position_Register_2;
	u32 addrBSESSTS = pAstRVAS->fg_reg_base + BSE_Status_Register;
	u8 byCounter = 0;
	bool bResult = true;

	down(&pAstRVAS->bse_engine.sem);
	pAstRVAS->bse_engine.finished = 0;

    // Set BSE Temp buffer address, and clear lower u16
	writel(BSE_LMEM_Temp_Buffer_Offset << 16, (void *)addrBSCR);
	writel(readl((void *)addrBSCR) | (aBSEAR.dwBSCR & 0X00000FFF), (void *)addrBSCR);
	writel(aBSEAR.dwBSDBS, (void *)addrBSDBS);
	writel(aBSEAR.adwBSBPS[0], (void *)addrBSBPS0);
	writel(aBSEAR.adwBSBPS[1], (void *)addrBSBPS1);
	writel(aBSEAR.adwBSBPS[2], (void *)addrBSBPS2);

	writel(dwBSEDescriptorAddr, (void *)addrBSEDTBR);

	while (!pAstRVAS->bse_engine.finished) {
		VIDEO_DBG("BSE Sleeping...\n");
		video_os_sleep_on_timeout(&pAstRVAS->bse_engine.wait,
					  &pAstRVAS->bse_engine.finished, 1000); // loop if bse timedout
		byCounter++;
		VIDEO_DBG("Back from BSE Sleeping, finished: %u\n",
			  pAstRVAS->bse_engine.finished);

		if (byCounter == ENGINE_TIMEOUT_IN_SECONDS) {
			writel(0x00, (void *)addrBSESSTS);
			pAstRVAS->bse_engine.finished = 1;
			dev_err(pAstRVAS->pdev, "TIMEOUT::Waiting BSE\n");
			bResult = false;
		}
	}

	VIDEO_DBG("*pdwBSESSTS = %#x\n", readl((void *)addrBSESSTS));
	writel(readl((void *)addrBSCR) & (~0x3), (void *)addrBSCR);

	up(&pAstRVAS->bse_engine.sem);

	return bResult;
}

void sleep_on_ldma_busy(struct AstRVAS *pAstRVAS, u32 dwDescriptorAddress)
{
	u32 addrLDMADTBR = pAstRVAS->fg_reg_base + LDMA_Descriptor_Table_Base_Register;
	u32 addrLDMAControlR = pAstRVAS->fg_reg_base + LDMA_Control_Register;

	VIDEO_DBG("In sleepONldma busy\n");

	down(&pAstRVAS->ldma_engine.sem);

	pAstRVAS->ldma_engine.finished = 0;

	writel(0x83, (void *)addrLDMAControlR);// descriptor can only in LMEM FOR LDMA
	writel(dwDescriptorAddress, (void *)addrLDMADTBR);
	VIDEO_DBG("LDMA: control [%#x]\n", readl((void *)addrLDMAControlR));
	VIDEO_DBG("LDMA:  DTBR  [%#x]\n", readl((void *)addrLDMADTBR));

	while (!pAstRVAS->ldma_engine.finished)
		video_os_sleep_on_timeout(&pAstRVAS->ldma_engine.wait, (u8 *)&pAstRVAS->ldma_engine.finished, 1000); // loop if bse timedout

	VIDEO_DBG("LDMA wake up\n");
	writel(readl((void *)addrLDMAControlR) & (~0x3), (void *)addrLDMAControlR);
	up(&pAstRVAS->ldma_engine.sem);
}

static int video_drv_get_resources(struct platform_device *pdev)
{
	int result = 0;

	struct resource *io_fg;
	struct resource *io_grc;
	struct resource *io_video;

	//get resources from platform
	io_fg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	VIDEO_DBG("io_fg: 0x%p\n", io_fg);

	if (!io_fg) {
		dev_err(&pdev->dev, "No Frame Grabber IORESOURCE_MEM entry\n");
		return -ENOENT;
	}
	io_grc = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	VIDEO_DBG("io_grc: 0x%p\n", io_grc);
	if (!io_grc) {
		dev_err(&pdev->dev, "No GRCE IORESOURCE_MEM entry\n");
		return -ENOENT;
	}
	io_video = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	VIDEO_DBG("io_video: 0x%p\n", io_video);
	if (!io_video) {
		dev_err(&pdev->dev, "No video compression IORESOURCE_MEM entry\n");
		return -ENOENT;
	}

	//map resource by device
	pAstRVAS->fg_reg_base = (u32)devm_ioremap_resource(&pdev->dev, io_fg);
	VIDEO_DBG("fg_reg_base: %#x\n", pAstRVAS->fg_reg_base);
	if (IS_ERR((void *)pAstRVAS->fg_reg_base)) {
		result = PTR_ERR((void *)pAstRVAS->fg_reg_base);
		dev_err(&pdev->dev, "Cannot map FG registers\n");
		pAstRVAS->fg_reg_base = 0;
		return result;
	}
	pAstRVAS->grce_reg_base = (u32)devm_ioremap_resource(&pdev->dev,
			  io_grc);
	VIDEO_DBG("grce_reg_base: %#x\n", pAstRVAS->grce_reg_base);
	if (IS_ERR((void *)pAstRVAS->grce_reg_base)) {
		result = PTR_ERR((void *)pAstRVAS->grce_reg_base);
		dev_err(&pdev->dev, "Cannot map GRC registers\n");
		pAstRVAS->grce_reg_base = 0;
		return result;
	}
	pAstRVAS->video_reg_base = (u32)devm_ioremap_resource(&pdev->dev,
				  io_video);
	VIDEO_DBG("video_reg_base: %#x\n", pAstRVAS->video_reg_base);
	if (IS_ERR((void *)pAstRVAS->video_reg_base)) {
		result = PTR_ERR((void *)pAstRVAS->video_reg_base);
		dev_err(&pdev->dev, "Cannot map video registers\n");
		pAstRVAS->video_reg_base = 0;
		return result;
	}
	return 0;
}

static int video_drv_get_irqs(struct platform_device *pdev)
{
	pAstRVAS->irq_fge = platform_get_irq(pdev, 0);
	VIDEO_DBG("irq_fge: %#x\n", pAstRVAS->irq_fge);
	if (pAstRVAS->irq_fge < 0) {
		dev_err(&pdev->dev, "NO FGE irq entry\n");
		return -ENOENT;
	}
	pAstRVAS->irq_vga = platform_get_irq(pdev, 1);
	VIDEO_DBG("irq_vga: %#x\n", pAstRVAS->irq_vga);
		if (pAstRVAS->irq_vga < 0) {
			dev_err(&pdev->dev, "NO VGA irq entry\n");
			return -ENOENT;
		}
	pAstRVAS->irq_video = platform_get_irq(pdev, 2);
	VIDEO_DBG("irq_video: %#x\n", pAstRVAS->irq_video);
	if (pAstRVAS->irq_video < 0) {
		dev_err(&pdev->dev, "NO video compression entry\n");
		return -ENOENT;
	}
	return 0;
}

static int video_drv_get_clock(struct platform_device *pdev)
{
	pAstRVAS->eclk = devm_clk_get(&pdev->dev, "eclk");
	if (IS_ERR(pAstRVAS->eclk)) {
		dev_err(&pdev->dev, "no eclk clock defined\n");
		return PTR_ERR(pAstRVAS->eclk);
	}

	clk_prepare_enable(pAstRVAS->eclk);

	pAstRVAS->vclk = devm_clk_get(&pdev->dev, "vclk");
	if (IS_ERR(pAstRVAS->vclk)) {
		dev_err(&pdev->dev, "no vclk clock defined\n");
		return PTR_ERR(pAstRVAS->vclk);
	}

	clk_prepare_enable(pAstRVAS->vclk);

	pAstRVAS->rvasclk = devm_clk_get(&pdev->dev, "rvasclk-gate");
	if (IS_ERR(pAstRVAS->rvasclk)) {
		dev_err(&pdev->dev, "no rvasclk clock defined\n");
		return PTR_ERR(pAstRVAS->rvasclk);
	}

	clk_prepare_enable(pAstRVAS->rvasclk);
	return 0;
}

static int video_drv_map_irqs(struct platform_device *pdev)
{
	int result = 0;
	//Map IRQS to handler
	VIDEO_DBG("Requesting IRQs, irq_fge: %d, irq_vga: %d, irq_video: %d\n",
		  pAstRVAS->irq_fge, pAstRVAS->irq_vga, pAstRVAS->irq_video);

	result = devm_request_irq(&pdev->dev, pAstRVAS->irq_fge, fge_handler, 0,
				  dev_name(&pdev->dev), pAstRVAS);
	if (result) {
		pr_err("Error in requesting IRQ\n");
		pr_err("RVAS: Failed request FGE irq %d\n", pAstRVAS->irq_fge);
		misc_deregister(&video_misc);
		return result;
	}

	result = devm_request_irq(&pdev->dev, pAstRVAS->irq_vga, fge_handler, 0,
				  dev_name(&pdev->dev), pAstRVAS);
	if (result) {
		pr_err("Error in requesting IRQ\n");
		pr_err("RVAS: Failed request vga irq %d\n", pAstRVAS->irq_vga);
		misc_deregister(&video_misc);
		return result;
	}

	result = devm_request_irq(&pdev->dev, pAstRVAS->irq_video, ast_video_isr, 0,
				  dev_name(&pdev->dev), pAstRVAS);
	if (result) {
		pr_err("Error in requesting IRQ\n");
		pr_err("RVAS: Failed request video irq %d\n", pAstRVAS->irq_video);
		misc_deregister(&video_misc);
		return result;
	}

	return result;
}

//
//
//
static int video_drv_probe(struct platform_device *pdev)
{
	int result = 0;
	struct regmap *sdram_scu;
	struct device_node *dp_node;
	struct device_node *edac_node;
	void __iomem *mcr_base;

	pr_info("RVAS driver probe\n");
	pAstRVAS = devm_kzalloc(&pdev->dev, sizeof(struct AstRVAS), GFP_KERNEL);
	VIDEO_DBG("pAstRVAS: 0x%p\n", pAstRVAS);

	if (!pAstRVAS) {
		dev_err(pAstRVAS->pdev, "Cannot allocate device structure\n");
		return -ENOMEM;
	}
	pAstRVAS->pdev = (void *)&pdev->dev;

	// Get resources
	result = video_drv_get_resources(pdev);
	if (result < 0) {
		dev_err(pAstRVAS->pdev, "video_probe: Error getting resources\n");
		return result;
	}

	//get irqs
	result = video_drv_get_irqs(pdev);
	if (result < 0) {
		dev_err(pAstRVAS->pdev, "video_probe: Error getting irqs\n");
		return result;
	}

	pAstRVAS->rvas_reset = devm_reset_control_get_by_index(&pdev->dev, 0);
	if (IS_ERR(pAstRVAS->rvas_reset)) {
		dev_err(&pdev->dev, "can't get rvas reset\n");
		return -ENOENT;
	}

	pAstRVAS->video_engine_reset = devm_reset_control_get_by_index(&pdev->dev, 1);
	if (IS_ERR(pAstRVAS->video_engine_reset)) {
		dev_err(&pdev->dev, "can't get video engine reset\n");
		return -ENOENT;
	}

	//prepare video engine clock
	result = video_drv_get_clock(pdev);
	if (result < 0) {
		dev_err(pAstRVAS->pdev, "video_probe: Error getting clocks\n");
		return result;
	}

	dp_node = of_find_compatible_node(NULL, NULL, "aspeed,ast2600-displayport");
	if (!dp_node) {
		dev_err(&pdev->dev, "cannot find dp node\n");
	} else {
		dp_base = of_iomap(dp_node, 0);
		if (!dp_base)
			dev_err(&pdev->dev, "failed to iomem of display port\n");
	}

	edac_node = of_find_compatible_node(NULL, NULL, "aspeed,ast2600-sdram-edac");
	if (!edac_node) {
		dev_err(&pdev->dev, "cannot find edac node\n");
	} else {
		mcr_base = of_iomap(edac_node, 0);
		if (!mcr_base)
			dev_err(&pdev->dev, "failed to iomem of MCR\n");
	}

	set_FBInfo_size(pAstRVAS, mcr_base);

	//scu
	sdram_scu = syscon_regmap_lookup_by_compatible("aspeed,ast2600-scu");
	VIDEO_DBG("sdram_scu: 0x%p\n", sdram_scu);
	if (IS_ERR(sdram_scu)) {
		dev_err(&pdev->dev, "failed to find ast2600-scu regmap\n");
		return PTR_ERR(sdram_scu);
	}
	pAstRVAS->scu = sdram_scu;

	result = misc_register(&video_misc);
	if (result) {
		pr_err("Failed in miscellaneous register (err: %d)\n", result);
		return result;
	}
	pr_info("Video misc minor %d\n", video_misc.minor);

	if (sysfs_create_group(&pdev->dev.kobj, &rvas_attribute_group)) {
		pr_err("Failed in creating group\n");
		return -1;
	}

	VIDEO_DBG("Disabling interrupts...\n");
	disable_grce_tse_interrupt(pAstRVAS);

	//reserve memory
	of_reserved_mem_device_init(&pdev->dev);
	result = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (result) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		of_reserved_mem_device_release(&pdev->dev);
	}

	// map irqs to irq_handlers
	result = video_drv_map_irqs(pdev);
	if (result < 0) {
		dev_err(pAstRVAS->pdev, "video_probe: Error mapping irqs\n");
		return result;
	}
	VIDEO_DBG("After IRQ registration\n");

	platform_set_drvdata(pdev, pAstRVAS);
	pAstRVAS->rvas_dev = &video_misc;
	VIDEO_DBG("pdev: 0x%p dev: 0x%p pAstRVAS: 0x%p rvas_dev: 0x%p\n", pdev,
		  &pdev->dev, pAstRVAS, pAstRVAS->rvas_dev);

	init_osr_es(pAstRVAS);
	rvas_init();
	video_engine_reserveMem(pAstRVAS);
	video_engine_init();

	pr_info("RVAS: driver successfully loaded.\n");
	return result;
}

static void rvas_init(void)
{
	VIDEO_ENG_DBG("\n");

	reset_snoop_engine(pAstRVAS);
	update_video_geometry(pAstRVAS);

	set_snoop_engine(true, pAstRVAS);
	enable_grce_tse_interrupt(pAstRVAS);
}

static void video_engine_init(void)
{
	VIDEO_ENG_DBG("\n");
	// video engine
	disable_video_interrupt(pAstRVAS);
	video_ctrl_init(pAstRVAS);
	video_engine_rc4Reset(pAstRVAS);
	set_direct_mode(pAstRVAS);
	video_set_Window(pAstRVAS);
	enable_video_interrupt(pAstRVAS);
}

static int video_drv_remove(struct platform_device *pdev)
{
	struct AstRVAS *pAstRVAS = NULL;

	VIDEO_DBG("\n");
	pAstRVAS = platform_get_drvdata(pdev);

	VIDEO_DBG("disable_grce_tse_interrupt...\n");
	disable_grce_tse_interrupt(pAstRVAS);
	disable_video_interrupt(pAstRVAS);

	sysfs_remove_group(&pdev->dev.kobj, &rvas_attribute_group);

	VIDEO_DBG("misc_deregister...\n");
	misc_deregister(&video_misc);

	VIDEO_DBG("Releasing OSRes...\n");
	release_osr_es(pAstRVAS);
	pr_info("RVAS: driver successfully unloaded.\n");
	free_video_engine_memory(pAstRVAS);
	return 0;
}

static const u32 ast2400_dram_table[] = {
	0x04000000,     //64MB
	0x08000000,     //128MB
	0x10000000,     //256MB
	0x20000000,     //512MB
};

static const u32 ast2500_dram_table[] = {
	0x08000000,     //128MB
	0x10000000,     //256MB
	0x20000000,     //512MB
	0x40000000,     //1024MB
};

static const u32 ast2600_dram_table[] = {
	0x10000000,     //256MB
	0x20000000,     //512MB
	0x40000000,     //1024MB
	0x80000000,     //2048MB
};

static const u32 aspeed_vga_table[] = {
	0x800000,       //8MB
	0x1000000,      //16MB
	0x2000000,      //32MB
	0x4000000,      //64MB
};

static void set_FBInfo_size(struct AstRVAS *pAstRVAS, void __iomem *mcr_base)
{
	u32 reg_mcr004 = readl(mcr_base + MCR_CONF);

#if defined(CONFIG_MACH_ASPEED_G6)
	pAstRVAS->FBInfo.dwDRAMSize = ast2600_dram_table[reg_mcr004 & 0x3];
#elif defined(CONFIG_MACH_ASPEED_G5)
	pAstRVAS->FBInfo.dwDRAMSize = ast2500_dram_table[reg_mcr004 & 0x3];
#else
	pAstRVAS->FBInfo.dwDRAMSize = ast2400_dram_table[reg_mcr004 & 0x3];
#endif

	pAstRVAS->FBInfo.dwVGASize = aspeed_vga_table[((reg_mcr004 & 0xC) >> 2)];
}

static const struct of_device_id ast_rvas_match[] = { { .compatible =
	"aspeed,ast2600-rvas", }, { }, };

MODULE_DEVICE_TABLE(of, ast_rvas_match);

static struct platform_driver video_driver = {
	.probe = video_drv_probe,
	.remove = video_drv_remove,
	.driver = { .of_match_table = of_match_ptr(ast_rvas_match), .name =
		RVAS_DRIVER_NAME, .owner = THIS_MODULE, }, };

module_platform_driver(video_driver);

MODULE_AUTHOR("ASPEED Technology");
MODULE_DESCRIPTION("RVAS video driver module for AST2600");
MODULE_LICENSE("GPL");
