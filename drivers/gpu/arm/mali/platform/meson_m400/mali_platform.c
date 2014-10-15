/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from AMLOGIC, INC.
 * (C) COPYRIGHT 2011 AMLOGIC, INC.
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from AMLOGIC, INC.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for meson platform
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <mach/am_regs.h>
#include <mach/clock.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include "mali_poweron_reg.h"
#include "mali_fix.h"
#include "mali_platform.h"

static int last_power_mode = -1;
static int mali_init_flag = 0;
static const u32 poweron_data[] =
{
/* commands */
/* 000 */ 0x00000040, 0x20400000, 0x00000300, 0x30040000,
/* 010 */ 0x00000400, 0x400a0000, 0x0f000033, 0x10000042,
/* 020 */ 0x00300c00, 0x10000040, 0x4c000001, 0x00000000,
/* 030 */ 0x00000000, 0x60000000, 0x00000000, 0x00000000,
/* 040 */ 0x00004000, 0x00002000, 0x00000210, 0x0000203f,
/* 050 */ 0x00000220, 0x0000203f, 0x00000230, 0x0000203f,
/* 060 */ 0x00000240, 0x0000203f, 0x00000250, 0x0000203f,
/* 070 */ 0x00000260, 0x0000203f, 0x00000270, 0x0000203f,
/* 080 */ 0x00000280, 0x0000203f, 0x00000290, 0x0000203f,
/* 090 */ 0x000002a0, 0x0000203f, 0x000002b0, 0x0000203f,
/* 0a0 */ 0x000002c0, 0x0000203f, 0x000002d0, 0x0000203f,
/* 0b0 */ 0x000002e0, 0x0000203f, 0x000002f0, 0x0000203f,
/* 0c0 */ 0x00002000, 0x00002000, 0x00002010, 0x0000203f,
/* 0d0 */ 0x00002020, 0x0000203f, 0x00002030, 0x0000203f,
/* 0e0 */ 0x00002040, 0x0000203f, 0x00002050, 0x0000203f,
/* 0f0 */ 0x00002060, 0x0000203f, 0x00002070, 0x0000203f,
/* 100 */ 0x00002080, 0x0000203f, 0x00002090, 0x0000203f,
/* 110 */ 0x000020a0, 0x0000203f, 0x000020b0, 0x0000203f,
/* 120 */ 0x000020c0, 0x0000203f, 0x000020d0, 0x0000203f,
/* 130 */ 0x000020e0, 0x0000203f, 0x000020f0, 0x0000203f,
/* 140 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 150 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 160 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 170 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 180 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 190 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1a0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1b0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1c0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1d0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1e0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 1f0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 200 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 210 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 220 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 230 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 240 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 250 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 260 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 270 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 280 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 290 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2a0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2b0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2c0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2d0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2e0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 2f0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* const */
/* 300 */ 0x3f2a6400, 0xbf317600, 0x3e8d8e00, 0x00000000,
/* 310 */ 0x3f2f7000, 0x3f36e200, 0x3e10c500, 0x00000000,
/* 320 */ 0xbe974e00, 0x3dc35300, 0x3f735800, 0x00000000,
/* 330 */ 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
/* 340 */ 0x42b00000, 0x42dc0000, 0x3f800000, 0x3f800000,
/* 350 */ 0x42b00000, 0x42dc0000, 0x00000000, 0x00000000,
/* 360 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 370 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 380 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 390 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3a0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3b0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3c0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3d0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3e0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* 3f0 */ 0x00000000, 0x00000000, 0x00000000, 0x00000000,
/* inst */
/* 400 */ 0xad4ad6b5, 0x438002b5, 0x0007ffe0, 0x00001e00,
/* 410 */ 0xad4ad694, 0x038002b5, 0x0087ffe0, 0x00005030,
/* 420 */ 0xad4bda56, 0x038002b5, 0x0007ffe0, 0x00001c10,
/* 430 */ 0xad4ad6b5, 0x038002b5, 0x4007fee0, 0x00001c00
};
static DEFINE_SPINLOCK(lock);
static struct clk *mali_clk = NULL;

#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6

#define OFFSET_MMU_DTE          0
#define OFFSET_MMU_PTE          4096
#define OFFSET_MMU_VIRTUAL_ZERO 8192

#define INT_MALI_GP_BITS     (1<<16)
#define INT_MALI_PP_BITS     (1<<18)
#define INT_MALI_PP_MMU_BITS (1<<19)
#define INT_ALL              (0xffffffff)

#define INT_MALI_PP_MMU_PAGE_FAULT (1<<0)

#define MMU_FLAG_DTE_PRESENT            0x01
#define MMU_FLAG_PTE_PAGE_PRESENT       0x01
#define MMU_FLAG_PTE_RD_PERMISSION      0x02
#define MMU_FLAG_PTE_WR_PERMISSION      0x04

//static int mali_revb_flag = -1;
extern int mali_revb_flag;
int mali_meson_is_revb(void)
{
	printk("mail version=%d\n",mali_revb_flag);
	if (mali_revb_flag == -1)
		mali_revb_flag = 1;
	else if (mali_revb_flag == 0)
	    panic("rev-a! you should neet earlier version of mali_driver.!\n");

    return mali_revb_flag;
}

static void mali_meson_poweron(int first_poweron)
{
	unsigned long flags;
	u32 p, p_aligned;
	dma_addr_t p_phy;
	int i;
	unsigned int_mask;

	if(!first_poweron) {
		if ((last_power_mode != -1) && (last_power_mode != MALI_POWER_MODE_DEEP_SLEEP)) {
			 MALI_DEBUG_PRINT(3, ("Maybe your system not deep sleep now.......\n"));
			//printk("Maybe your system not deep sleep now.......\n");
			return;
		}
	}

	MALI_DEBUG_PRINT(2, ("mali_meson_poweron: Mali APB bus accessing\n"));
	if (READ_MALI_REG(MALI_PP_PP_VERSION) != MALI_PP_PP_VERSION_MAGIC) {
	MALI_DEBUG_PRINT(3, ("mali_meson_poweron: Mali APB bus access failed\n"));
	//printk("mali_meson_poweron: Mali APB bus access failed.");
	return;
	}
	MALI_DEBUG_PRINT(2, ("..........accessing done.\n"));
	if (READ_MALI_REG(MALI_MMU_DTE_ADDR) != 0) {
		MALI_DEBUG_PRINT(3, ("mali_meson_poweron: Mali is not really powered off\n"));
		//printk("mali_meson_poweron: Mali is not really powered off.");
		return;
	}

	p = (u32)kcalloc(4096 * 4, 1, GFP_KERNEL);
	if (!p) {
		printk("mali_meson_poweron: NOMEM in meson_poweron\n");
		return;
	}

	p_aligned = __ALIGN_MASK(p, 4096);

	/* DTE */
	*(u32 *)(p_aligned) = (virt_to_phys((void *)p_aligned) + OFFSET_MMU_PTE) | MMU_FLAG_DTE_PRESENT;
	/* PTE */
	for (i=0; i<1024; i++) {
		*(u32 *)(p_aligned + OFFSET_MMU_PTE + i*4) =
		    (virt_to_phys((void *)p_aligned) + OFFSET_MMU_VIRTUAL_ZERO + 4096 * i) |
		    MMU_FLAG_PTE_PAGE_PRESENT |
		    MMU_FLAG_PTE_RD_PERMISSION;
	}

	/* command & data */
	memcpy((void *)(p_aligned + OFFSET_MMU_VIRTUAL_ZERO), poweron_data, 4096);

	p_phy = dma_map_single(NULL, (void *)p_aligned, 4096 * 3, DMA_TO_DEVICE);

	/* Set up Mali GP MMU */
	WRITE_MALI_REG(MALI_MMU_DTE_ADDR, p_phy);
	WRITE_MALI_REG(MALI_MMU_CMD, 0);

	if ((READ_MALI_REG(MALI_MMU_STATUS) & 1) != 1)
		printk("mali_meson_poweron: MMU enabling failed.\n");

	/* Set up Mali command registers */
	WRITE_MALI_REG(MALI_APB_GP_VSCL_START, 0);
	WRITE_MALI_REG(MALI_APB_GP_VSCL_END, 0x38);

	spin_lock_irqsave(&lock, flags);

	int_mask = READ_MALI_REG(MALI_APB_GP_INT_MASK);
	WRITE_MALI_REG(MALI_APB_GP_INT_CLEAR, 0x707bff);
	WRITE_MALI_REG(MALI_APB_GP_INT_MASK, 0);

	/* Start GP */
	WRITE_MALI_REG(MALI_APB_GP_CMD, 1);

	for (i = 0; i<100; i++)
		udelay(500);

	/* check Mali GP interrupt */
	if (READ_MALI_REG(MALI_APB_GP_INT_RAWSTAT) & 0x707bff)
		printk("mali_meson_poweron: Interrupt received.\n");
	else
		printk("mali_meson_poweron: No interrupt received.\n");

	/* force reset GP */
	WRITE_MALI_REG(MALI_APB_GP_CMD, 1 << 5);

	/* stop MMU paging and reset */
	WRITE_MALI_REG(MALI_MMU_CMD, 1);
	WRITE_MALI_REG(MALI_MMU_CMD, 6);

	for (i = 0; i<100; i++)
		udelay(500);

	WRITE_MALI_REG(MALI_APB_GP_INT_CLEAR, 0x3ff);
	WRITE_MALI_REG(MALI_MMU_INT_CLEAR, INT_ALL);
	WRITE_MALI_REG(MALI_MMU_INT_MASK, 0);

	WRITE_MALI_REG(MALI_APB_GP_INT_CLEAR, 0x707bff);
	WRITE_MALI_REG(MALI_APB_GP_INT_MASK, int_mask);

	spin_unlock_irqrestore(&lock, flags);

	dma_unmap_single(NULL, p_phy, 4096 * 3, DMA_TO_DEVICE);

	kfree((void *)p);

	/* Mali revision detection */
	if (last_power_mode == -1)
		mali_revb_flag = mali_meson_is_revb();
}
#else
static void mali_meson_poweron(int first_poweron) {
	return;
}
#endif /*MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6 */

_mali_osk_errcode_t mali_platform_init(void)
{
	mali_clk = clk_get_sys("mali", "pll_fixed");

	if (mali_clk ) {
		if (!mali_init_flag) {
			clk_set_rate(mali_clk, 333000000);
			mali_clk->enable(mali_clk);
			malifix_init();
			mali_meson_poweron(1);
			mali_init_flag = 1;
		}
		MALI_SUCCESS;
	} else 
		panic("linux kernel should > 3.0\n");

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    MALI_PRINT_ERROR(("Failed to lookup mali clock"));
	MALI_ERROR(_MALI_OSK_ERR_FAULT);
#else
	MALI_SUCCESS;
#endif /* CONFIG_ARCH_MESON6 */
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
	mali_init_flag =0;
	printk("MALI:mali_platform_deinit\n");
	malifix_exit();

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	MALI_DEBUG_PRINT(3, ( "mali_platform_power_mode_change power_mode=%d\n", power_mode));

	switch (power_mode) {
	case MALI_POWER_MODE_LIGHT_SLEEP:
	case MALI_POWER_MODE_DEEP_SLEEP:
		/* Turn off mali clock gating */
		mali_clk->disable(mali_clk);
		break;

        case MALI_POWER_MODE_ON:
		/* Turn on MALI clock gating */
		mali_clk->enable(mali_clk);
		mali_meson_poweron(0);
		break;
	}
	last_power_mode = power_mode;
	MALI_SUCCESS;
}

