// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP2/3 System Control Module register access
 *
 * Copyright (C) 2007, 2012 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/cpu_pm.h>

#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "cm-regbits-34xx.h"
#include "prm-regbits-34xx.h"
#include "prm3xxx.h"
#include "cm3xxx.h"
#include "sdrc.h"
#include "pm.h"
#include "control.h"
#include "clock.h"

/* Used by omap3_ctrl_save_padconf() */
#define START_PADCONF_SAVE		0x2
#define PADCONF_SAVE_DONE		0x1

static void __iomem *omap2_ctrl_base;
static s16 omap2_ctrl_offset;

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)
struct omap3_scratchpad {
	u32 boot_config_ptr;
	u32 public_restore_ptr;
	u32 secure_ram_restore_ptr;
	u32 sdrc_module_semaphore;
	u32 prcm_block_offset;
	u32 sdrc_block_offset;
};

struct omap3_scratchpad_prcm_block {
	u32 prm_contents[2];
	u32 cm_contents[11];
	u32 prcm_block_size;
};

struct omap3_scratchpad_sdrc_block {
	u16 sysconfig;
	u16 cs_cfg;
	u16 sharing;
	u16 err_type;
	u32 dll_a_ctrl;
	u32 dll_b_ctrl;
	u32 power;
	u32 cs_0;
	u32 mcfg_0;
	u16 mr_0;
	u16 emr_1_0;
	u16 emr_2_0;
	u16 emr_3_0;
	u32 actim_ctrla_0;
	u32 actim_ctrlb_0;
	u32 rfr_ctrl_0;
	u32 cs_1;
	u32 mcfg_1;
	u16 mr_1;
	u16 emr_1_1;
	u16 emr_2_1;
	u16 emr_3_1;
	u32 actim_ctrla_1;
	u32 actim_ctrlb_1;
	u32 rfr_ctrl_1;
	u16 dcdl_1_ctrl;
	u16 dcdl_2_ctrl;
	u32 flags;
	u32 block_size;
};

void *omap3_secure_ram_storage;

/*
 * This is used to store ARM registers in SDRAM before attempting
 * an MPU OFF. The save and restore happens from the SRAM sleep code.
 * The address is stored in scratchpad, so that it can be used
 * during the restore path.
 */
u32 omap3_arm_context[128];

struct omap3_control_regs {
	u32 sysconfig;
	u32 devconf0;
	u32 mem_dftrw0;
	u32 mem_dftrw1;
	u32 msuspendmux_0;
	u32 msuspendmux_1;
	u32 msuspendmux_2;
	u32 msuspendmux_3;
	u32 msuspendmux_4;
	u32 msuspendmux_5;
	u32 sec_ctrl;
	u32 devconf1;
	u32 csirxfe;
	u32 iva2_bootaddr;
	u32 iva2_bootmod;
	u32 wkup_ctrl;
	u32 debobs_0;
	u32 debobs_1;
	u32 debobs_2;
	u32 debobs_3;
	u32 debobs_4;
	u32 debobs_5;
	u32 debobs_6;
	u32 debobs_7;
	u32 debobs_8;
	u32 prog_io0;
	u32 prog_io1;
	u32 dss_dpll_spreading;
	u32 core_dpll_spreading;
	u32 per_dpll_spreading;
	u32 usbhost_dpll_spreading;
	u32 pbias_lite;
	u32 temp_sensor;
	u32 sramldo4;
	u32 sramldo5;
	u32 csi;
	u32 padconf_sys_nirq;
};

static struct omap3_control_regs control_context;
#endif /* CONFIG_ARCH_OMAP3 && CONFIG_PM */

u8 omap_ctrl_readb(u16 offset)
{
	u32 val;
	u8 byte_offset = offset & 0x3;

	val = omap_ctrl_readl(offset);

	return (val >> (byte_offset * 8)) & 0xff;
}

u16 omap_ctrl_readw(u16 offset)
{
	u32 val;
	u16 byte_offset = offset & 0x2;

	val = omap_ctrl_readl(offset);

	return (val >> (byte_offset * 8)) & 0xffff;
}

u32 omap_ctrl_readl(u16 offset)
{
	offset &= 0xfffc;

	return readl_relaxed(omap2_ctrl_base + offset);
}

void omap_ctrl_writeb(u8 val, u16 offset)
{
	u32 tmp;
	u8 byte_offset = offset & 0x3;

	tmp = omap_ctrl_readl(offset);

	tmp &= 0xffffffff ^ (0xff << (byte_offset * 8));
	tmp |= val << (byte_offset * 8);

	omap_ctrl_writel(tmp, offset);
}

void omap_ctrl_writew(u16 val, u16 offset)
{
	u32 tmp;
	u8 byte_offset = offset & 0x2;

	tmp = omap_ctrl_readl(offset);

	tmp &= 0xffffffff ^ (0xffff << (byte_offset * 8));
	tmp |= val << (byte_offset * 8);

	omap_ctrl_writel(tmp, offset);
}

void omap_ctrl_writel(u32 val, u16 offset)
{
	offset &= 0xfffc;
	writel_relaxed(val, omap2_ctrl_base + offset);
}

#ifdef CONFIG_ARCH_OMAP3

/**
 * omap3_ctrl_write_boot_mode - set scratchpad boot mode for the next boot
 * @bootmode: 8-bit value to pass to some boot code
 *
 * Set the bootmode in the scratchpad RAM.  This is used after the
 * system restarts.  Not sure what actually uses this - it may be the
 * bootloader, rather than the boot ROM - contrary to the preserved
 * comment below.  No return value.
 */
void omap3_ctrl_write_boot_mode(u8 bootmode)
{
	u32 l;

	l = ('B' << 24) | ('M' << 16) | bootmode;

	/*
	 * Reserve the first word in scratchpad for communicating
	 * with the boot ROM. A pointer to a data structure
	 * describing the boot process can be stored there,
	 * cf. OMAP34xx TRM, Initialization / Software Booting
	 * Configuration.
	 *
	 * XXX This should use some omap_ctrl_writel()-type function
	 */
	writel_relaxed(l, OMAP2_L4_IO_ADDRESS(OMAP343X_SCRATCHPAD + 4));
}

#endif

/**
 * omap_ctrl_write_dsp_boot_addr - set boot address for a remote processor
 * @bootaddr: physical address of the boot loader
 *
 * Set boot address for the boot loader of a supported processor
 * when a power ON sequence occurs.
 */
void omap_ctrl_write_dsp_boot_addr(u32 bootaddr)
{
	u32 offset = cpu_is_omap243x() ? OMAP243X_CONTROL_IVA2_BOOTADDR :
		     cpu_is_omap34xx() ? OMAP343X_CONTROL_IVA2_BOOTADDR :
		     cpu_is_omap44xx() ? OMAP4_CTRL_MODULE_CORE_DSP_BOOTADDR :
		     soc_is_omap54xx() ? OMAP4_CTRL_MODULE_CORE_DSP_BOOTADDR :
		     0;

	if (!offset) {
		pr_err("%s: unsupported omap type\n", __func__);
		return;
	}

	omap_ctrl_writel(bootaddr, offset);
}

/**
 * omap_ctrl_write_dsp_boot_mode - set boot mode for a remote processor
 * @bootmode: 8-bit value to pass to some boot code
 *
 * Sets boot mode for the boot loader of a supported processor
 * when a power ON sequence occurs.
 */
void omap_ctrl_write_dsp_boot_mode(u8 bootmode)
{
	u32 offset = cpu_is_omap243x() ? OMAP243X_CONTROL_IVA2_BOOTMOD :
		     cpu_is_omap34xx() ? OMAP343X_CONTROL_IVA2_BOOTMOD :
		     0;

	if (!offset) {
		pr_err("%s: unsupported omap type\n", __func__);
		return;
	}

	omap_ctrl_writel(bootmode, offset);
}

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)
/*
 * Clears the scratchpad contents in case of cold boot-
 * called during bootup
 */
void omap3_clear_scratchpad_contents(void)
{
	u32 max_offset = OMAP343X_SCRATCHPAD_ROM_OFFSET;
	void __iomem *v_addr;
	u32 offset = 0;

	v_addr = OMAP2_L4_IO_ADDRESS(OMAP343X_SCRATCHPAD_ROM);
	if (omap3xxx_prm_clear_global_cold_reset()) {
		for ( ; offset <= max_offset; offset += 0x4)
			writel_relaxed(0x0, (v_addr + offset));
	}
}

/* Populate the scratchpad structure with restore structure */
void omap3_save_scratchpad_contents(void)
{
	void  __iomem *scratchpad_address;
	u32 arm_context_addr;
	struct omap3_scratchpad scratchpad_contents;
	struct omap3_scratchpad_prcm_block prcm_block_contents;
	struct omap3_scratchpad_sdrc_block sdrc_block_contents;

	/*
	 * Populate the Scratchpad contents
	 *
	 * The "get_*restore_pointer" functions are used to provide a
	 * physical restore address where the ROM code jumps while waking
	 * up from MPU OFF/OSWR state.
	 * The restore pointer is stored into the scratchpad.
	 */
	scratchpad_contents.boot_config_ptr = 0x0;
	if (cpu_is_omap3630())
		scratchpad_contents.public_restore_ptr =
			__pa_symbol(omap3_restore_3630);
	else if (omap_rev() != OMAP3430_REV_ES3_0 &&
					omap_rev() != OMAP3430_REV_ES3_1 &&
					omap_rev() != OMAP3430_REV_ES3_1_2)
		scratchpad_contents.public_restore_ptr =
			__pa_symbol(omap3_restore);
	else
		scratchpad_contents.public_restore_ptr =
			__pa_symbol(omap3_restore_es3);

	if (omap_type() == OMAP2_DEVICE_TYPE_GP)
		scratchpad_contents.secure_ram_restore_ptr = 0x0;
	else
		scratchpad_contents.secure_ram_restore_ptr =
			(u32) __pa(omap3_secure_ram_storage);
	scratchpad_contents.sdrc_module_semaphore = 0x0;
	scratchpad_contents.prcm_block_offset = 0x2C;
	scratchpad_contents.sdrc_block_offset = 0x64;

	/* Populate the PRCM block contents */
	omap3_prm_save_scratchpad_contents(prcm_block_contents.prm_contents);
	omap3_cm_save_scratchpad_contents(prcm_block_contents.cm_contents);

	prcm_block_contents.prcm_block_size = 0x0;

	/* Populate the SDRC block contents */
	sdrc_block_contents.sysconfig =
			(sdrc_read_reg(SDRC_SYSCONFIG) & 0xFFFF);
	sdrc_block_contents.cs_cfg =
			(sdrc_read_reg(SDRC_CS_CFG) & 0xFFFF);
	sdrc_block_contents.sharing =
			(sdrc_read_reg(SDRC_SHARING) & 0xFFFF);
	sdrc_block_contents.err_type =
			(sdrc_read_reg(SDRC_ERR_TYPE) & 0xFFFF);
	sdrc_block_contents.dll_a_ctrl = sdrc_read_reg(SDRC_DLLA_CTRL);
	sdrc_block_contents.dll_b_ctrl = 0x0;
	/*
	 * Due to a OMAP3 errata (1.142), on EMU/HS devices SRDC should
	 * be programed to issue automatic self refresh on timeout
	 * of AUTO_CNT = 1 prior to any transition to OFF mode.
	 */
	if ((omap_type() != OMAP2_DEVICE_TYPE_GP)
			&& (omap_rev() >= OMAP3430_REV_ES3_0))
		sdrc_block_contents.power = (sdrc_read_reg(SDRC_POWER) &
				~(SDRC_POWER_AUTOCOUNT_MASK|
				SDRC_POWER_CLKCTRL_MASK)) |
				(1 << SDRC_POWER_AUTOCOUNT_SHIFT) |
				SDRC_SELF_REFRESH_ON_AUTOCOUNT;
	else
		sdrc_block_contents.power = sdrc_read_reg(SDRC_POWER);

	sdrc_block_contents.cs_0 = 0x0;
	sdrc_block_contents.mcfg_0 = sdrc_read_reg(SDRC_MCFG_0);
	sdrc_block_contents.mr_0 = (sdrc_read_reg(SDRC_MR_0) & 0xFFFF);
	sdrc_block_contents.emr_1_0 = 0x0;
	sdrc_block_contents.emr_2_0 = 0x0;
	sdrc_block_contents.emr_3_0 = 0x0;
	sdrc_block_contents.actim_ctrla_0 =
			sdrc_read_reg(SDRC_ACTIM_CTRL_A_0);
	sdrc_block_contents.actim_ctrlb_0 =
			sdrc_read_reg(SDRC_ACTIM_CTRL_B_0);
	sdrc_block_contents.rfr_ctrl_0 =
			sdrc_read_reg(SDRC_RFR_CTRL_0);
	sdrc_block_contents.cs_1 = 0x0;
	sdrc_block_contents.mcfg_1 = sdrc_read_reg(SDRC_MCFG_1);
	sdrc_block_contents.mr_1 = sdrc_read_reg(SDRC_MR_1) & 0xFFFF;
	sdrc_block_contents.emr_1_1 = 0x0;
	sdrc_block_contents.emr_2_1 = 0x0;
	sdrc_block_contents.emr_3_1 = 0x0;
	sdrc_block_contents.actim_ctrla_1 =
			sdrc_read_reg(SDRC_ACTIM_CTRL_A_1);
	sdrc_block_contents.actim_ctrlb_1 =
			sdrc_read_reg(SDRC_ACTIM_CTRL_B_1);
	sdrc_block_contents.rfr_ctrl_1 =
			sdrc_read_reg(SDRC_RFR_CTRL_1);
	sdrc_block_contents.dcdl_1_ctrl = 0x0;
	sdrc_block_contents.dcdl_2_ctrl = 0x0;
	sdrc_block_contents.flags = 0x0;
	sdrc_block_contents.block_size = 0x0;

	arm_context_addr = __pa_symbol(omap3_arm_context);

	/* Copy all the contents to the scratchpad location */
	scratchpad_address = OMAP2_L4_IO_ADDRESS(OMAP343X_SCRATCHPAD);
	memcpy_toio(scratchpad_address, &scratchpad_contents,
		 sizeof(scratchpad_contents));
	/* Scratchpad contents being 32 bits, a divide by 4 done here */
	memcpy_toio(scratchpad_address +
		scratchpad_contents.prcm_block_offset,
		&prcm_block_contents, sizeof(prcm_block_contents));
	memcpy_toio(scratchpad_address +
		scratchpad_contents.sdrc_block_offset,
		&sdrc_block_contents, sizeof(sdrc_block_contents));
	/*
	 * Copies the address of the location in SDRAM where ARM
	 * registers get saved during a MPU OFF transition.
	 */
	memcpy_toio(scratchpad_address +
		scratchpad_contents.sdrc_block_offset +
		sizeof(sdrc_block_contents), &arm_context_addr, 4);
}

void omap3_control_save_context(void)
{
	control_context.sysconfig = omap_ctrl_readl(OMAP2_CONTROL_SYSCONFIG);
	control_context.devconf0 = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	control_context.mem_dftrw0 =
			omap_ctrl_readl(OMAP343X_CONTROL_MEM_DFTRW0);
	control_context.mem_dftrw1 =
			omap_ctrl_readl(OMAP343X_CONTROL_MEM_DFTRW1);
	control_context.msuspendmux_0 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_0);
	control_context.msuspendmux_1 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_1);
	control_context.msuspendmux_2 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_2);
	control_context.msuspendmux_3 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_3);
	control_context.msuspendmux_4 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_4);
	control_context.msuspendmux_5 =
			omap_ctrl_readl(OMAP2_CONTROL_MSUSPENDMUX_5);
	control_context.sec_ctrl = omap_ctrl_readl(OMAP2_CONTROL_SEC_CTRL);
	control_context.devconf1 = omap_ctrl_readl(OMAP343X_CONTROL_DEVCONF1);
	control_context.csirxfe = omap_ctrl_readl(OMAP343X_CONTROL_CSIRXFE);
	control_context.iva2_bootaddr =
			omap_ctrl_readl(OMAP343X_CONTROL_IVA2_BOOTADDR);
	control_context.iva2_bootmod =
			omap_ctrl_readl(OMAP343X_CONTROL_IVA2_BOOTMOD);
	control_context.wkup_ctrl = omap_ctrl_readl(OMAP34XX_CONTROL_WKUP_CTRL);
	control_context.debobs_0 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(0));
	control_context.debobs_1 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(1));
	control_context.debobs_2 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(2));
	control_context.debobs_3 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(3));
	control_context.debobs_4 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(4));
	control_context.debobs_5 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(5));
	control_context.debobs_6 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(6));
	control_context.debobs_7 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(7));
	control_context.debobs_8 = omap_ctrl_readl(OMAP343X_CONTROL_DEBOBS(8));
	control_context.prog_io0 = omap_ctrl_readl(OMAP343X_CONTROL_PROG_IO0);
	control_context.prog_io1 = omap_ctrl_readl(OMAP343X_CONTROL_PROG_IO1);
	control_context.dss_dpll_spreading =
			omap_ctrl_readl(OMAP343X_CONTROL_DSS_DPLL_SPREADING);
	control_context.core_dpll_spreading =
			omap_ctrl_readl(OMAP343X_CONTROL_CORE_DPLL_SPREADING);
	control_context.per_dpll_spreading =
			omap_ctrl_readl(OMAP343X_CONTROL_PER_DPLL_SPREADING);
	control_context.usbhost_dpll_spreading =
		omap_ctrl_readl(OMAP343X_CONTROL_USBHOST_DPLL_SPREADING);
	control_context.pbias_lite =
			omap_ctrl_readl(OMAP343X_CONTROL_PBIAS_LITE);
	control_context.temp_sensor =
			omap_ctrl_readl(OMAP343X_CONTROL_TEMP_SENSOR);
	control_context.sramldo4 = omap_ctrl_readl(OMAP343X_CONTROL_SRAMLDO4);
	control_context.sramldo5 = omap_ctrl_readl(OMAP343X_CONTROL_SRAMLDO5);
	control_context.csi = omap_ctrl_readl(OMAP343X_CONTROL_CSI);
	control_context.padconf_sys_nirq =
		omap_ctrl_readl(OMAP343X_CONTROL_PADCONF_SYSNIRQ);
}

void omap3_control_restore_context(void)
{
	omap_ctrl_writel(control_context.sysconfig, OMAP2_CONTROL_SYSCONFIG);
	omap_ctrl_writel(control_context.devconf0, OMAP2_CONTROL_DEVCONF0);
	omap_ctrl_writel(control_context.mem_dftrw0,
					OMAP343X_CONTROL_MEM_DFTRW0);
	omap_ctrl_writel(control_context.mem_dftrw1,
					OMAP343X_CONTROL_MEM_DFTRW1);
	omap_ctrl_writel(control_context.msuspendmux_0,
					OMAP2_CONTROL_MSUSPENDMUX_0);
	omap_ctrl_writel(control_context.msuspendmux_1,
					OMAP2_CONTROL_MSUSPENDMUX_1);
	omap_ctrl_writel(control_context.msuspendmux_2,
					OMAP2_CONTROL_MSUSPENDMUX_2);
	omap_ctrl_writel(control_context.msuspendmux_3,
					OMAP2_CONTROL_MSUSPENDMUX_3);
	omap_ctrl_writel(control_context.msuspendmux_4,
					OMAP2_CONTROL_MSUSPENDMUX_4);
	omap_ctrl_writel(control_context.msuspendmux_5,
					OMAP2_CONTROL_MSUSPENDMUX_5);
	omap_ctrl_writel(control_context.sec_ctrl, OMAP2_CONTROL_SEC_CTRL);
	omap_ctrl_writel(control_context.devconf1, OMAP343X_CONTROL_DEVCONF1);
	omap_ctrl_writel(control_context.csirxfe, OMAP343X_CONTROL_CSIRXFE);
	omap_ctrl_writel(control_context.iva2_bootaddr,
					OMAP343X_CONTROL_IVA2_BOOTADDR);
	omap_ctrl_writel(control_context.iva2_bootmod,
					OMAP343X_CONTROL_IVA2_BOOTMOD);
	omap_ctrl_writel(control_context.wkup_ctrl, OMAP34XX_CONTROL_WKUP_CTRL);
	omap_ctrl_writel(control_context.debobs_0, OMAP343X_CONTROL_DEBOBS(0));
	omap_ctrl_writel(control_context.debobs_1, OMAP343X_CONTROL_DEBOBS(1));
	omap_ctrl_writel(control_context.debobs_2, OMAP343X_CONTROL_DEBOBS(2));
	omap_ctrl_writel(control_context.debobs_3, OMAP343X_CONTROL_DEBOBS(3));
	omap_ctrl_writel(control_context.debobs_4, OMAP343X_CONTROL_DEBOBS(4));
	omap_ctrl_writel(control_context.debobs_5, OMAP343X_CONTROL_DEBOBS(5));
	omap_ctrl_writel(control_context.debobs_6, OMAP343X_CONTROL_DEBOBS(6));
	omap_ctrl_writel(control_context.debobs_7, OMAP343X_CONTROL_DEBOBS(7));
	omap_ctrl_writel(control_context.debobs_8, OMAP343X_CONTROL_DEBOBS(8));
	omap_ctrl_writel(control_context.prog_io0, OMAP343X_CONTROL_PROG_IO0);
	omap_ctrl_writel(control_context.prog_io1, OMAP343X_CONTROL_PROG_IO1);
	omap_ctrl_writel(control_context.dss_dpll_spreading,
					OMAP343X_CONTROL_DSS_DPLL_SPREADING);
	omap_ctrl_writel(control_context.core_dpll_spreading,
					OMAP343X_CONTROL_CORE_DPLL_SPREADING);
	omap_ctrl_writel(control_context.per_dpll_spreading,
					OMAP343X_CONTROL_PER_DPLL_SPREADING);
	omap_ctrl_writel(control_context.usbhost_dpll_spreading,
				OMAP343X_CONTROL_USBHOST_DPLL_SPREADING);
	omap_ctrl_writel(control_context.pbias_lite,
					OMAP343X_CONTROL_PBIAS_LITE);
	omap_ctrl_writel(control_context.temp_sensor,
					OMAP343X_CONTROL_TEMP_SENSOR);
	omap_ctrl_writel(control_context.sramldo4, OMAP343X_CONTROL_SRAMLDO4);
	omap_ctrl_writel(control_context.sramldo5, OMAP343X_CONTROL_SRAMLDO5);
	omap_ctrl_writel(control_context.csi, OMAP343X_CONTROL_CSI);
	omap_ctrl_writel(control_context.padconf_sys_nirq,
			 OMAP343X_CONTROL_PADCONF_SYSNIRQ);
}

void omap3630_ctrl_disable_rta(void)
{
	if (!cpu_is_omap3630())
		return;
	omap_ctrl_writel(OMAP36XX_RTA_DISABLE, OMAP36XX_CONTROL_MEM_RTA_CTRL);
}

/**
 * omap3_ctrl_save_padconf - save padconf registers to scratchpad RAM
 *
 * Tell the SCM to start saving the padconf registers, then wait for
 * the process to complete.  Returns 0 unconditionally, although it
 * should also eventually be able to return -ETIMEDOUT, if the save
 * does not complete.
 *
 * XXX This function is missing a timeout.  What should it be?
 */
int omap3_ctrl_save_padconf(void)
{
	u32 cpo;

	/* Save the padconf registers */
	cpo = omap_ctrl_readl(OMAP343X_CONTROL_PADCONF_OFF);
	cpo |= START_PADCONF_SAVE;
	omap_ctrl_writel(cpo, OMAP343X_CONTROL_PADCONF_OFF);

	/* wait for the save to complete */
	while (!(omap_ctrl_readl(OMAP343X_CONTROL_GENERAL_PURPOSE_STATUS)
		 & PADCONF_SAVE_DONE))
		udelay(1);

	return 0;
}

/**
 * omap3_ctrl_set_iva_bootmode_idle - sets the IVA2 bootmode to idle
 *
 * Sets the bootmode for IVA2 to idle. This is needed by the PM code to
 * force disable IVA2 so that it does not prevent any low-power states.
 */
static void __init omap3_ctrl_set_iva_bootmode_idle(void)
{
	omap_ctrl_writel(OMAP3_IVA2_BOOTMOD_IDLE,
			 OMAP343X_CONTROL_IVA2_BOOTMOD);
}

/**
 * omap3_ctrl_setup_d2d_padconf - setup stacked modem pads for idle
 *
 * Sets up the pads controlling the stacked modem in such way that the
 * device can enter idle.
 */
static void __init omap3_ctrl_setup_d2d_padconf(void)
{
	u16 mask, padconf;

	/*
	 * In a stand alone OMAP3430 where there is not a stacked
	 * modem for the D2D Idle Ack and D2D MStandby must be pulled
	 * high. S CONTROL_PADCONF_SAD2D_IDLEACK and
	 * CONTROL_PADCONF_SAD2D_MSTDBY to have a pull up.
	 */
	mask = (1 << 4) | (1 << 3); /* pull-up, enabled */
	padconf = omap_ctrl_readw(OMAP3_PADCONF_SAD2D_MSTANDBY);
	padconf |= mask;
	omap_ctrl_writew(padconf, OMAP3_PADCONF_SAD2D_MSTANDBY);

	padconf = omap_ctrl_readw(OMAP3_PADCONF_SAD2D_IDLEACK);
	padconf |= mask;
	omap_ctrl_writew(padconf, OMAP3_PADCONF_SAD2D_IDLEACK);
}

/**
 * omap3_ctrl_init - does static initializations for control module
 *
 * Initializes system control module. This sets up the sysconfig autoidle,
 * and sets up modem and iva2 so that they can be idled properly.
 */
void __init omap3_ctrl_init(void)
{
	omap_ctrl_writel(OMAP3430_AUTOIDLE_MASK, OMAP2_CONTROL_SYSCONFIG);

	omap3_ctrl_set_iva_bootmode_idle();

	omap3_ctrl_setup_d2d_padconf();
}
#endif /* CONFIG_ARCH_OMAP3 && CONFIG_PM */

static unsigned long am43xx_control_reg_offsets[] = {
	AM33XX_CONTROL_SYSCONFIG_OFFSET,
	AM33XX_CONTROL_STATUS_OFFSET,
	AM43XX_CONTROL_MPU_L2_CTRL_OFFSET,
	AM33XX_CONTROL_CORE_SLDO_CTRL_OFFSET,
	AM33XX_CONTROL_MPU_SLDO_CTRL_OFFSET,
	AM33XX_CONTROL_CLK32KDIVRATIO_CTRL_OFFSET,
	AM33XX_CONTROL_BANDGAP_CTRL_OFFSET,
	AM33XX_CONTROL_BANDGAP_TRIM_OFFSET,
	AM33XX_CONTROL_PLL_CLKINPULOW_CTRL_OFFSET,
	AM33XX_CONTROL_MOSC_CTRL_OFFSET,
	AM33XX_CONTROL_DEEPSLEEP_CTRL_OFFSET,
	AM43XX_CONTROL_DISPLAY_PLL_SEL_OFFSET,
	AM33XX_CONTROL_INIT_PRIORITY_0_OFFSET,
	AM33XX_CONTROL_INIT_PRIORITY_1_OFFSET,
	AM33XX_CONTROL_TPTC_CFG_OFFSET,
	AM33XX_CONTROL_USB_CTRL0_OFFSET,
	AM33XX_CONTROL_USB_CTRL1_OFFSET,
	AM43XX_CONTROL_USB_CTRL2_OFFSET,
	AM43XX_CONTROL_GMII_SEL_OFFSET,
	AM43XX_CONTROL_MPUSS_CTRL_OFFSET,
	AM43XX_CONTROL_TIMER_CASCADE_CTRL_OFFSET,
	AM43XX_CONTROL_PWMSS_CTRL_OFFSET,
	AM33XX_CONTROL_MREQPRIO_0_OFFSET,
	AM33XX_CONTROL_MREQPRIO_1_OFFSET,
	AM33XX_CONTROL_HW_EVENT_SEL_GRP1_OFFSET,
	AM33XX_CONTROL_HW_EVENT_SEL_GRP2_OFFSET,
	AM33XX_CONTROL_HW_EVENT_SEL_GRP3_OFFSET,
	AM33XX_CONTROL_HW_EVENT_SEL_GRP4_OFFSET,
	AM33XX_CONTROL_SMRT_CTRL_OFFSET,
	AM33XX_CONTROL_MPUSS_HW_DEBUG_SEL_OFFSET,
	AM43XX_CONTROL_CQDETECT_STS_OFFSET,
	AM43XX_CONTROL_CQDETECT_STS2_OFFSET,
	AM43XX_CONTROL_VTP_CTRL_OFFSET,
	AM33XX_CONTROL_VREF_CTRL_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_0_3_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_4_7_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_8_11_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_12_15_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_16_19_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_20_23_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_24_27_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_28_31_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_32_35_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_36_39_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_40_43_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_44_47_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_48_51_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_52_55_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_56_59_OFFSET,
	AM33XX_CONTROL_TPCC_EVT_MUX_60_63_OFFSET,
	AM33XX_CONTROL_TIMER_EVT_CAPT_OFFSET,
	AM33XX_CONTROL_ECAP_EVT_CAPT_OFFSET,
	AM33XX_CONTROL_ADC_EVT_CAPT_OFFSET,
	AM43XX_CONTROL_ADC1_EVT_CAPT_OFFSET,
	AM33XX_CONTROL_RESET_ISO_OFFSET,
};

static u32 am33xx_control_vals[ARRAY_SIZE(am43xx_control_reg_offsets)];

/**
 * am43xx_control_save_context - Save the wakeup domain registers
 *
 * Save the wkup domain registers
 */
static void am43xx_control_save_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(am43xx_control_reg_offsets); i++)
		am33xx_control_vals[i] =
				omap_ctrl_readl(am43xx_control_reg_offsets[i]);
}

/**
 * am43xx_control_restore_context - Restore the wakeup domain registers
 *
 * Restore the wkup domain registers
 */
static void am43xx_control_restore_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(am43xx_control_reg_offsets); i++)
		omap_ctrl_writel(am33xx_control_vals[i],
				 am43xx_control_reg_offsets[i]);
}

static int cpu_notifier(struct notifier_block *nb, unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		if (enable_off_mode)
			am43xx_control_save_context();
		break;
	case CPU_CLUSTER_PM_EXIT:
		if (enable_off_mode)
			am43xx_control_restore_context();
		break;
	}

	return NOTIFY_OK;
}

struct control_init_data {
	int index;
	void __iomem *mem;
	s16 offset;
};

static struct control_init_data ctrl_data = {
	.index = TI_CLKM_CTRL,
};

static const struct control_init_data omap2_ctrl_data = {
	.index = TI_CLKM_CTRL,
	.offset = -OMAP2_CONTROL_GENERAL,
};

static const struct control_init_data ctrl_aux_data = {
	.index = TI_CLKM_CTRL_AUX,
};

static const struct of_device_id omap_scrm_dt_match_table[] = {
	{ .compatible = "ti,am3-scm", .data = &ctrl_data },
	{ .compatible = "ti,am4-scm", .data = &ctrl_data },
	{ .compatible = "ti,omap2-scm", .data = &omap2_ctrl_data },
	{ .compatible = "ti,omap3-scm", .data = &omap2_ctrl_data },
	{ .compatible = "ti,dm814-scm", .data = &ctrl_data },
	{ .compatible = "ti,dm816-scrm", .data = &ctrl_data },
	{ .compatible = "ti,omap4-scm-core", .data = &ctrl_data },
	{ .compatible = "ti,omap5-scm-core", .data = &ctrl_data },
	{ .compatible = "ti,omap5-scm-wkup-pad-conf", .data = &ctrl_aux_data },
	{ .compatible = "ti,dra7-scm-core", .data = &ctrl_data },
	{ }
};

/**
 * omap2_control_base_init - initialize iomappings for the control driver
 *
 * Detects and initializes the iomappings for the control driver, based
 * on the DT data. Returns 0 in success, negative error value
 * otherwise.
 */
int __init omap2_control_base_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	struct control_init_data *data;
	void __iomem *mem;

	for_each_matching_node_and_match(np, omap_scrm_dt_match_table, &match) {
		data = (struct control_init_data *)match->data;

		mem = of_iomap(np, 0);
		if (!mem) {
			of_node_put(np);
			return -ENOMEM;
		}

		if (data->index == TI_CLKM_CTRL) {
			omap2_ctrl_base = mem;
			omap2_ctrl_offset = data->offset;
		}

		data->mem = mem;
	}

	return 0;
}

/**
 * omap_control_init - low level init for the control driver
 *
 * Initializes the low level clock infrastructure for control driver.
 * Returns 0 in success, negative error value in failure.
 */
int __init omap_control_init(void)
{
	struct device_node *np, *scm_conf;
	const struct of_device_id *match;
	const struct omap_prcm_init_data *data;
	int ret;
	struct regmap *syscon;
	static struct notifier_block nb;

	for_each_matching_node_and_match(np, omap_scrm_dt_match_table, &match) {
		data = match->data;

		/*
		 * Check if we have scm_conf node, if yes, use this to
		 * access clock registers.
		 */
		scm_conf = of_get_child_by_name(np, "scm_conf");

		if (scm_conf) {
			syscon = syscon_node_to_regmap(scm_conf);

			if (IS_ERR(syscon)) {
				ret = PTR_ERR(syscon);
				goto of_node_put;
			}

			if (of_get_child_by_name(scm_conf, "clocks")) {
				ret = omap2_clk_provider_init(scm_conf,
							      data->index,
							      syscon, NULL);
				if (ret)
					goto of_node_put;
			}
		} else {
			/* No scm_conf found, direct access */
			ret = omap2_clk_provider_init(np, data->index, NULL,
						      data->mem);
			if (ret)
				goto of_node_put;
		}
	}

	/* Only AM43XX can lose ctrl registers context during rtc-ddr suspend */
	if (soc_is_am43xx()) {
		nb.notifier_call = cpu_notifier;
		cpu_pm_register_notifier(&nb);
	}

	return 0;

of_node_put:
	of_node_put(np);
	return ret;

}

/**
 * omap3_control_legacy_iomap_init - legacy iomap init for clock providers
 *
 * Legacy iomap init for clock provider. Needed only by legacy boot mode,
 * where the base addresses are not parsed from DT, but still required
 * by the clock driver to be setup properly.
 */
void __init omap3_control_legacy_iomap_init(void)
{
	omap2_clk_legacy_provider_init(TI_CLKM_SCRM, omap2_ctrl_base);
}
