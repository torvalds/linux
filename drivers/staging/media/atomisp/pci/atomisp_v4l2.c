// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010-2017 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/interrupt.h>
#include <linux/bits.h>
#include <media/v4l2-fwnode.h>

#include <asm/iosf_mbi.h>

#include "../../include/linux/atomisp_gmin_platform.h"

#include "atomisp_cmd.h"
#include "atomisp_common.h"
#include "atomisp_fops.h"
#include "atomisp_ioctl.h"
#include "atomisp_internal.h"
#include "atomisp-regs.h"
#include "atomisp_dfs_tables.h"
#include "atomisp_drvfs.h"
#include "hmm/hmm.h"
#include "atomisp_trace_event.h"

#include "sh_css_firmware.h"

#include "device_access.h"

/* Timeouts to wait for all subdevs to be registered */
#define SUBDEV_WAIT_TIMEOUT		50 /* ms */
#define SUBDEV_WAIT_TIMEOUT_MAX_COUNT	40 /* up to 2 seconds */

/* G-Min addition: pull this in from intel_mid_pm.h */
#define CSTATE_EXIT_LATENCY_C1  1

/* cross component debug message flag */
int dbg_level;
module_param(dbg_level, int, 0644);
MODULE_PARM_DESC(dbg_level, "debug message level (default:0)");

/* log function switch */
int dbg_func = 1;
module_param(dbg_func, int, 0644);
MODULE_PARM_DESC(dbg_func,
		 "log function switch non/printk (default:printk)");

/*
 * Set to 16x16 since this is the amount of lines and pixels the sensor
 * exports extra. If these are kept at the 10x8 that they were on, in yuv
 * downscaling modes incorrect resolutions where requested to the sensor
 * driver with strange outcomes as a result. The proper way tot do this
 * would be to have a list of tables the specify the sensor res, mipi rec,
 * output res, and isp output res. however since we do not have this yet,
 * the chosen solution is the next best thing.
 */
int pad_w = 16;
module_param(pad_w, int, 0644);
MODULE_PARM_DESC(pad_w, "extra data for ISP processing");

int pad_h = 16;
module_param(pad_h, int, 0644);
MODULE_PARM_DESC(pad_h, "extra data for ISP processing");

/*
 * FIXME: this is a hack to make easier to support ISP2401 variant.
 * As a given system will either be ISP2401 or not, we can just use
 * a boolean, in order to replace existing #ifdef ISP2401 everywhere.
 *
 * Once this driver gets into a better shape, however, the best would
 * be to replace this to something stored inside atomisp allocated
 * structures.
 */

struct device *atomisp_dev;

static const struct atomisp_freq_scaling_rule dfs_rules_merr[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

/* Merrifield and Moorefield DFS rules */
static const struct atomisp_dfs_config dfs_config_merr = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_457MHZ,
	.dfs_table = dfs_rules_merr,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr),
};

static const struct atomisp_freq_scaling_rule dfs_rules_merr_1179[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

static const struct atomisp_dfs_config dfs_config_merr_1179 = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_merr_1179,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr_1179),
};

static const struct atomisp_freq_scaling_rule dfs_rules_merr_117a[] = {
	{
		.width = 1920,
		.height = 1080,
		.fps = 30,
		.isp_freq = ISP_FREQ_266MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1080,
		.height = 1920,
		.fps = 30,
		.isp_freq = ISP_FREQ_266MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1920,
		.height = 1080,
		.fps = 45,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = 1080,
		.height = 1920,
		.fps = 45,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = 60,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_200MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_200MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

static struct atomisp_dfs_config dfs_config_merr_117a = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_200MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_merr_117a,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_merr_117a),
};

static const struct atomisp_freq_scaling_rule dfs_rules_byt[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_400MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

static const struct atomisp_dfs_config dfs_config_byt = {
	.lowest_freq = ISP_FREQ_200MHZ,
	.max_freq_at_vmin = ISP_FREQ_400MHZ,
	.highest_freq = ISP_FREQ_400MHZ,
	.dfs_table = dfs_rules_byt,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_byt),
};

static const struct atomisp_freq_scaling_rule dfs_rules_cht[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

static const struct atomisp_freq_scaling_rule dfs_rules_cht_soc[] = {
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_VIDEO,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_356MHZ,
		.run_mode = ATOMISP_RUN_MODE_STILL_CAPTURE,
	},
	{
		.width = ISP_FREQ_RULE_ANY,
		.height = ISP_FREQ_RULE_ANY,
		.fps = ISP_FREQ_RULE_ANY,
		.isp_freq = ISP_FREQ_320MHZ,
		.run_mode = ATOMISP_RUN_MODE_PREVIEW,
	},
};

static const struct atomisp_dfs_config dfs_config_cht = {
	.lowest_freq = ISP_FREQ_100MHZ,
	.max_freq_at_vmin = ISP_FREQ_356MHZ,
	.highest_freq = ISP_FREQ_356MHZ,
	.dfs_table = dfs_rules_cht,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_cht),
};

/* This one should be visible also by atomisp_cmd.c */
const struct atomisp_dfs_config dfs_config_cht_soc = {
	.lowest_freq = ISP_FREQ_100MHZ,
	.max_freq_at_vmin = ISP_FREQ_356MHZ,
	.highest_freq = ISP_FREQ_356MHZ,
	.dfs_table = dfs_rules_cht_soc,
	.dfs_table_size = ARRAY_SIZE(dfs_rules_cht_soc),
};

int atomisp_video_init(struct atomisp_video_pipe *video)
{
	int ret;

	video->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&video->vdev.entity, 1, &video->pad);
	if (ret < 0)
		return ret;

	/* Initialize the video device. */
	strscpy(video->vdev.name, "ATOMISP video output", sizeof(video->vdev.name));
	video->vdev.fops = &atomisp_fops;
	video->vdev.ioctl_ops = &atomisp_ioctl_ops;
	video->vdev.lock = &video->isp->mutex;
	video->vdev.release = video_device_release_empty;
	video_set_drvdata(&video->vdev, video->isp);

	return 0;
}

void atomisp_video_unregister(struct atomisp_video_pipe *video)
{
	if (video_is_registered(&video->vdev)) {
		media_entity_cleanup(&video->vdev.entity);
		video_unregister_device(&video->vdev);
	}
}

static int atomisp_save_iunit_reg(struct atomisp_device *isp)
{
	struct pci_dev *pdev = to_pci_dev(isp->dev);

	dev_dbg(isp->dev, "%s\n", __func__);

	pci_read_config_word(pdev, PCI_COMMAND, &isp->saved_regs.pcicmdsts);
	/* isp->saved_regs.ispmmadr is set from the atomisp_pci_probe() */
	pci_read_config_dword(pdev, PCI_MSI_CAPID, &isp->saved_regs.msicap);
	pci_read_config_dword(pdev, PCI_MSI_ADDR, &isp->saved_regs.msi_addr);
	pci_read_config_word(pdev, PCI_MSI_DATA,  &isp->saved_regs.msi_data);
	pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &isp->saved_regs.intr);
	pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &isp->saved_regs.interrupt_control);

	pci_read_config_dword(pdev, MRFLD_PCI_PMCS, &isp->saved_regs.pmcs);
	/* Ensure read/write combining is enabled. */
	pci_read_config_dword(pdev, PCI_I_CONTROL, &isp->saved_regs.i_control);
	isp->saved_regs.i_control |=
	    MRFLD_PCI_I_CONTROL_ENABLE_READ_COMBINING |
	    MRFLD_PCI_I_CONTROL_ENABLE_WRITE_COMBINING;
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_ACCESS_CTRL_VIOL,
			      &isp->saved_regs.csi_access_viol);
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_RCOMP_CONTROL,
			      &isp->saved_regs.csi_rcomp_config);
	/*
	 * Hardware bugs require setting CSI_HS_OVR_CLK_GATE_ON_UPDATE.
	 * ANN/CHV: RCOMP updates do not happen when using CSI2+ path
	 * and sensor sending "continuous clock".
	 * TNG/ANN/CHV: MIPI packets are lost if the HS entry sequence
	 * is missed, and IUNIT can hang.
	 * For both issues, setting this bit is a workaround.
	 */
	isp->saved_regs.csi_rcomp_config |= MRFLD_PCI_CSI_HS_OVR_CLK_GATE_ON_UPDATE;
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
			      &isp->saved_regs.csi_afe_dly);
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_CONTROL,
			      &isp->saved_regs.csi_control);
	if (isp->media_dev.hw_revision >=
	    (ATOMISP_HW_REVISION_ISP2401 << ATOMISP_HW_REVISION_SHIFT))
		isp->saved_regs.csi_control |= MRFLD_PCI_CSI_CONTROL_PARPATHEN;
	/*
	 * On CHT CSI_READY bit should be enabled before stream on
	 */
	if (IS_CHT && (isp->media_dev.hw_revision >= ((ATOMISP_HW_REVISION_ISP2401 <<
		       ATOMISP_HW_REVISION_SHIFT) | ATOMISP_HW_STEPPING_B0)))
		isp->saved_regs.csi_control |= MRFLD_PCI_CSI_CONTROL_CSI_READY;
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_AFE_RCOMP_CONTROL,
			      &isp->saved_regs.csi_afe_rcomp_config);
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_AFE_HS_CONTROL,
			      &isp->saved_regs.csi_afe_hs_control);
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_DEADLINE_CONTROL,
			      &isp->saved_regs.csi_deadline_control);
	return 0;
}

static int atomisp_restore_iunit_reg(struct atomisp_device *isp)
{
	struct pci_dev *pdev = to_pci_dev(isp->dev);

	dev_dbg(isp->dev, "%s\n", __func__);

	pci_write_config_word(pdev, PCI_COMMAND, isp->saved_regs.pcicmdsts);
	pci_write_config_dword(pdev, PCI_BASE_ADDRESS_0, isp->saved_regs.ispmmadr);
	pci_write_config_dword(pdev, PCI_MSI_CAPID, isp->saved_regs.msicap);
	pci_write_config_dword(pdev, PCI_MSI_ADDR, isp->saved_regs.msi_addr);
	pci_write_config_word(pdev, PCI_MSI_DATA, isp->saved_regs.msi_data);
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, isp->saved_regs.intr);
	pci_write_config_dword(pdev, PCI_INTERRUPT_CTRL, isp->saved_regs.interrupt_control);
	pci_write_config_dword(pdev, PCI_I_CONTROL, isp->saved_regs.i_control);

	pci_write_config_dword(pdev, MRFLD_PCI_PMCS, isp->saved_regs.pmcs);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_ACCESS_CTRL_VIOL,
			       isp->saved_regs.csi_access_viol);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_RCOMP_CONTROL,
			       isp->saved_regs.csi_rcomp_config);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
			       isp->saved_regs.csi_afe_dly);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_CONTROL,
			       isp->saved_regs.csi_control);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_AFE_RCOMP_CONTROL,
			       isp->saved_regs.csi_afe_rcomp_config);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_AFE_HS_CONTROL,
			       isp->saved_regs.csi_afe_hs_control);
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_DEADLINE_CONTROL,
			       isp->saved_regs.csi_deadline_control);

	/*
	 * for MRFLD, Software/firmware needs to write a 1 to bit0
	 * of the register at CSI_RECEIVER_SELECTION_REG to enable
	 * SH CSI backend write 0 will enable Arasan CSI backend,
	 * which has bugs(like sighting:4567697 and 4567699) and
	 * will be removed in B0
	 */
	atomisp_css2_hw_store_32(MRFLD_CSI_RECEIVER_SELECTION_REG, 1);
	return 0;
}

static int atomisp_mrfld_pre_power_down(struct atomisp_device *isp)
{
	struct pci_dev *pdev = to_pci_dev(isp->dev);
	u32 irq;
	unsigned long flags;

	spin_lock_irqsave(&isp->lock, flags);

	/*
	 * MRFLD HAS requirement: cannot power off i-unit if
	 * ISP has IRQ not serviced.
	 * So, here we need to check if there is any pending
	 * IRQ, if so, waiting for it to be served
	 */
	pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &irq);
	irq &= BIT(INTR_IIR);
	pci_write_config_dword(pdev, PCI_INTERRUPT_CTRL, irq);

	pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &irq);
	if (!(irq & BIT(INTR_IIR)))
		goto done;

	atomisp_css2_hw_store_32(MRFLD_INTR_CLEAR_REG, 0xFFFFFFFF);
	atomisp_load_uint32(MRFLD_INTR_STATUS_REG, &irq);
	if (irq != 0) {
		dev_err(isp->dev,
			"%s: fail to clear isp interrupt status reg=0x%x\n",
			__func__, irq);
		spin_unlock_irqrestore(&isp->lock, flags);
		return -EAGAIN;
	} else {
		pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &irq);
		irq &= BIT(INTR_IIR);
		pci_write_config_dword(pdev, PCI_INTERRUPT_CTRL, irq);

		pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &irq);
		if (!(irq & BIT(INTR_IIR))) {
			atomisp_css2_hw_store_32(MRFLD_INTR_ENABLE_REG, 0x0);
			goto done;
		}
		dev_err(isp->dev,
			"%s: error in iunit interrupt. status reg=0x%x\n",
			__func__, irq);
		spin_unlock_irqrestore(&isp->lock, flags);
		return -EAGAIN;
	}
done:
	/*
	 * MRFLD WORKAROUND:
	 * before powering off IUNIT, clear the pending interrupts
	 * and disable the interrupt. driver should avoid writing 0
	 * to IIR. It could block subsequent interrupt messages.
	 * HW sighting:4568410.
	 */
	pci_read_config_dword(pdev, PCI_INTERRUPT_CTRL, &irq);
	irq &= ~BIT(INTR_IER);
	pci_write_config_dword(pdev, PCI_INTERRUPT_CTRL, irq);

	atomisp_msi_irq_uninit(isp);
	atomisp_freq_scaling(isp, ATOMISP_DFS_MODE_LOW, true);
	spin_unlock_irqrestore(&isp->lock, flags);

	return 0;
}

/*
 * WA for DDR DVFS enable/disable
 * By default, ISP will force DDR DVFS 1600MHz before disable DVFS
 */
static void punit_ddr_dvfs_enable(bool enable)
{
	int reg;

	iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ, MRFLD_ISPSSDVFS, &reg);
	if (enable) {
		reg &= ~(MRFLD_BIT0 | MRFLD_BIT1);
	} else {
		reg |= MRFLD_BIT1;
		reg &= ~(MRFLD_BIT0);
	}
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE, MRFLD_ISPSSDVFS, reg);
}

static int atomisp_mrfld_power(struct atomisp_device *isp, bool enable)
{
	struct pci_dev *pdev = to_pci_dev(isp->dev);
	unsigned long timeout;
	u32 val = enable ? MRFLD_ISPSSPM0_IUNIT_POWER_ON :
			   MRFLD_ISPSSPM0_IUNIT_POWER_OFF;

	dev_dbg(isp->dev, "IUNIT power-%s.\n", enable ? "on" : "off");

	/* WA for P-Unit, if DVFS enabled, ISP timeout observed */
	if (IS_CHT && enable && !isp->pm_only) {
		punit_ddr_dvfs_enable(false);
		msleep(20);
	}

	/* Write to ISPSSPM0 bit[1:0] to power on/off the IUNIT */
	iosf_mbi_modify(BT_MBI_UNIT_PMC, MBI_REG_READ, MRFLD_ISPSSPM0,
			val, MRFLD_ISPSSPM0_ISPSSC_MASK);

	/* WA:Enable DVFS */
	if (IS_CHT && !enable && !isp->pm_only)
		punit_ddr_dvfs_enable(true);

	/*
	 * There should be no IUNIT access while power-down is
	 * in progress. HW sighting: 4567865.
	 * Wait up to 50 ms for the IUNIT to shut down.
	 * And we do the same for power on.
	 */
	timeout = jiffies + msecs_to_jiffies(50);
	do {
		u32 tmp;

		/* Wait until ISPSSPM0 bit[25:24] shows the right value */
		iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ, MRFLD_ISPSSPM0, &tmp);
		tmp = (tmp >> MRFLD_ISPSSPM0_ISPSSS_OFFSET) & MRFLD_ISPSSPM0_ISPSSC_MASK;
		if (tmp == val) {
			trace_ipu_cstate(enable);
			pdev->current_state = enable ? PCI_D0 : PCI_D3cold;
			return 0;
		}

		if (time_after(jiffies, timeout))
			break;

		/* FIXME: experienced value for delay */
		usleep_range(100, 150);
	} while (1);

	dev_err(isp->dev, "IUNIT power-%s timeout.\n", enable ? "on" : "off");
	return -EBUSY;
}

int atomisp_power_off(struct device *dev)
{
	struct atomisp_device *isp = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;
	u32 reg;

	if (isp->pm_only) {
		pci_write_config_dword(pdev, PCI_INTERRUPT_CTRL, 0);
	} else {
		atomisp_css_uninit(isp);

		ret = atomisp_mrfld_pre_power_down(isp);
		if (ret)
			return ret;
	}

	/*
	 * MRFLD IUNIT DPHY is located in an always-power-on island
	 * MRFLD HW design need all CSI ports are disabled before
	 * powering down the IUNIT.
	 */
	pci_read_config_dword(pdev, MRFLD_PCI_CSI_CONTROL, &reg);
	reg |= MRFLD_ALL_CSI_PORTS_OFF_MASK;
	pci_write_config_dword(pdev, MRFLD_PCI_CSI_CONTROL, reg);

	cpu_latency_qos_update_request(&isp->pm_qos, PM_QOS_DEFAULT_VALUE);
	pci_save_state(pdev);
	return atomisp_mrfld_power(isp, false);
}

int atomisp_power_on(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
				     dev_get_drvdata(dev);
	int ret;

	ret = atomisp_mrfld_power(isp, true);
	if (ret)
		return ret;

	pci_restore_state(to_pci_dev(dev));
	cpu_latency_qos_update_request(&isp->pm_qos, isp->max_isr_latency);

	if (isp->pm_only)
		return 0;

	/*restore register values for iUnit and iUnitPHY registers*/
	if (isp->saved_regs.pcicmdsts)
		atomisp_restore_iunit_reg(isp);

	atomisp_freq_scaling(isp, ATOMISP_DFS_MODE_LOW, true);

	return atomisp_css_init(isp);
}

static int atomisp_suspend(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
				     dev_get_drvdata(dev);
	unsigned long flags;

	/* FIXME: Suspend is not supported by sensors. Abort if streaming. */
	spin_lock_irqsave(&isp->lock, flags);
	if (isp->asd.streaming) {
		spin_unlock_irqrestore(&isp->lock, flags);
		dev_err(isp->dev, "atomisp cannot suspend at this time.\n");
		return -EINVAL;
	}
	spin_unlock_irqrestore(&isp->lock, flags);

	pm_runtime_resume(dev);

	isp->asd.recreate_streams_on_resume = isp->asd.stream_prepared;
	atomisp_destroy_pipes_stream(&isp->asd);

	return atomisp_power_off(dev);
}

static int atomisp_resume(struct device *dev)
{
	struct atomisp_device *isp = dev_get_drvdata(dev);
	int ret;

	ret = atomisp_power_on(dev);
	if (ret)
		return ret;

	if (isp->asd.recreate_streams_on_resume)
		ret = atomisp_create_pipes_stream(&isp->asd);

	return ret;
}

int atomisp_csi_lane_config(struct atomisp_device *isp)
{
	struct pci_dev *pdev = to_pci_dev(isp->dev);
	static const struct {
		u8 code;
		u8 lanes[N_MIPI_PORT_ID];
	} portconfigs[] = {
		/* Tangier/Merrifield available lane configurations */
		{ 0x00, { 4, 1, 0 } },		/* 00000 */
		{ 0x01, { 3, 1, 0 } },		/* 00001 */
		{ 0x02, { 2, 1, 0 } },		/* 00010 */
		{ 0x03, { 1, 1, 0 } },		/* 00011 */
		{ 0x04, { 2, 1, 2 } },		/* 00100 */
		{ 0x08, { 3, 1, 1 } },		/* 01000 */
		{ 0x09, { 2, 1, 1 } },		/* 01001 */
		{ 0x0a, { 1, 1, 1 } },		/* 01010 */

		/* Anniedale/Moorefield only configurations */
		{ 0x10, { 4, 2, 0 } },		/* 10000 */
		{ 0x11, { 3, 2, 0 } },		/* 10001 */
		{ 0x12, { 2, 2, 0 } },		/* 10010 */
		{ 0x13, { 1, 2, 0 } },		/* 10011 */
		{ 0x14, { 2, 2, 2 } },		/* 10100 */
		{ 0x18, { 3, 2, 1 } },		/* 11000 */
		{ 0x19, { 2, 2, 1 } },		/* 11001 */
		{ 0x1a, { 1, 2, 1 } },		/* 11010 */
	};

	unsigned int i, j;
	u32 csi_control;
	int nportconfigs;
	u32 port_config_mask;
	int port3_lanes_shift;

	if (isp->media_dev.hw_revision <
	    ATOMISP_HW_REVISION_ISP2401_LEGACY <<
	    ATOMISP_HW_REVISION_SHIFT) {
		/* Merrifield */
		port_config_mask = MRFLD_PORT_CONFIG_MASK;
		port3_lanes_shift = MRFLD_PORT3_LANES_SHIFT;
	} else {
		/* Moorefield / Cherryview */
		port_config_mask = CHV_PORT_CONFIG_MASK;
		port3_lanes_shift = CHV_PORT3_LANES_SHIFT;
	}

	if (isp->media_dev.hw_revision <
	    ATOMISP_HW_REVISION_ISP2401 <<
	    ATOMISP_HW_REVISION_SHIFT) {
		/* Merrifield / Moorefield legacy input system */
		nportconfigs = MRFLD_PORT_CONFIG_NUM;
	} else {
		/* Moorefield / Cherryview new input system */
		nportconfigs = ARRAY_SIZE(portconfigs);
	}

	for (i = 0; i < nportconfigs; i++) {
		for (j = 0; j < N_MIPI_PORT_ID; j++)
			if (isp->sensor_lanes[j] &&
			    isp->sensor_lanes[j] != portconfigs[i].lanes[j])
				break;

		if (j == N_MIPI_PORT_ID)
			break;			/* Found matching setting */
	}

	if (i >= nportconfigs) {
		dev_err(isp->dev,
			"%s: could not find the CSI port setting for %d-%d-%d\n",
			__func__,
			isp->sensor_lanes[0], isp->sensor_lanes[1], isp->sensor_lanes[2]);
		return -EINVAL;
	}

	pci_read_config_dword(pdev, MRFLD_PCI_CSI_CONTROL, &csi_control);
	csi_control &= ~port_config_mask;
	csi_control |= (portconfigs[i].code << MRFLD_PORT_CONFIGCODE_SHIFT)
		       | (portconfigs[i].lanes[0] ? 0 : (1 << MRFLD_PORT1_ENABLE_SHIFT))
		       | (portconfigs[i].lanes[1] ? 0 : (1 << MRFLD_PORT2_ENABLE_SHIFT))
		       | (portconfigs[i].lanes[2] ? 0 : (1 << MRFLD_PORT3_ENABLE_SHIFT))
		       | (((1 << portconfigs[i].lanes[0]) - 1) << MRFLD_PORT1_LANES_SHIFT)
		       | (((1 << portconfigs[i].lanes[1]) - 1) << MRFLD_PORT2_LANES_SHIFT)
		       | (((1 << portconfigs[i].lanes[2]) - 1) << port3_lanes_shift);

	pci_write_config_dword(pdev, MRFLD_PCI_CSI_CONTROL, csi_control);

	dev_dbg(isp->dev,
		"%s: the portconfig is %d-%d-%d, CSI_CONTROL is 0x%08X\n",
		__func__, portconfigs[i].lanes[0], portconfigs[i].lanes[1],
		portconfigs[i].lanes[2], csi_control);

	return 0;
}

static int atomisp_subdev_probe(struct atomisp_device *isp)
{
	const struct intel_v4l2_subdev_table *subdevs;
	int ret, mipi_port;

	ret = atomisp_csi2_bridge_parse_firmware(isp);
	if (ret)
		return ret;

	/*
	 * TODO: this is left here for now to allow testing atomisp-sensor
	 * drivers which are still using the atomisp_gmin_platform infra before
	 * converting them to standard v4l2 sensor drivers using runtime-pm +
	 * ACPI for pm and v4l2_async_register_subdev_sensor() registration.
	 */
	for (subdevs = atomisp_platform_get_subdevs(); subdevs->subdev; subdevs++) {
		ret = v4l2_device_register_subdev(&isp->v4l2_dev, subdevs->subdev);
		if (ret)
			continue;

		if (subdevs->port >= ATOMISP_CAMERA_NR_PORTS) {
			dev_err(isp->dev, "port %d not supported\n", subdevs->port);
			continue;
		}

		if (isp->sensor_subdevs[subdevs->port]) {
			dev_err(isp->dev, "port %d already has a sensor attached\n",
				subdevs->port);
			continue;
		}

		mipi_port = atomisp_port_to_mipi_port(isp, subdevs->port);
		isp->sensor_lanes[mipi_port] = subdevs->lanes;
		isp->sensor_subdevs[subdevs->port] = subdevs->subdev;
	}

	return atomisp_csi_lane_config(isp);
}

static void atomisp_unregister_entities(struct atomisp_device *isp)
{
	unsigned int i;
	struct v4l2_subdev *sd, *next;

	atomisp_subdev_unregister_entities(&isp->asd);
	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++)
		atomisp_mipi_csi2_unregister_entities(&isp->csi2_port[i]);

	list_for_each_entry_safe(sd, next, &isp->v4l2_dev.subdevs, list)
		v4l2_device_unregister_subdev(sd);

	v4l2_device_unregister(&isp->v4l2_dev);
	media_device_unregister(&isp->media_dev);
	media_device_cleanup(&isp->media_dev);

	for (i = 0; i < isp->input_cnt; i++)
		__v4l2_subdev_state_free(isp->inputs[i].try_sd_state);
}

static int atomisp_register_entities(struct atomisp_device *isp)
{
	int ret = 0;
	unsigned int i;

	isp->media_dev.dev = isp->dev;

	strscpy(isp->media_dev.model, "Intel Atom ISP",
		sizeof(isp->media_dev.model));

	media_device_init(&isp->media_dev);
	isp->v4l2_dev.mdev = &isp->media_dev;
	ret = v4l2_device_register(isp->dev, &isp->v4l2_dev);
	if (ret < 0) {
		dev_err(isp->dev, "%s: V4L2 device registration failed (%d)\n",
			__func__, ret);
		goto v4l2_device_failed;
	}

	ret = atomisp_subdev_probe(isp);
	if (ret < 0)
		goto csi_and_subdev_probe_failed;

	/* Register internal entities */
	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++) {
		ret = atomisp_mipi_csi2_register_entities(&isp->csi2_port[i],
			&isp->v4l2_dev);
		if (ret == 0)
			continue;

		/* error case */
		dev_err(isp->dev, "failed to register the CSI port: %d\n", i);
		/* deregister all registered CSI ports */
		while (i--)
			atomisp_mipi_csi2_unregister_entities(
			    &isp->csi2_port[i]);

		goto csi_and_subdev_probe_failed;
	}

	ret = atomisp_subdev_register_subdev(&isp->asd, &isp->v4l2_dev);
	if (ret < 0) {
		dev_err(isp->dev, "atomisp_subdev_register_subdev fail\n");
		goto subdev_register_failed;
	}

	return 0;

subdev_register_failed:
	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++)
		atomisp_mipi_csi2_unregister_entities(&isp->csi2_port[i]);
csi_and_subdev_probe_failed:
	v4l2_device_unregister(&isp->v4l2_dev);
v4l2_device_failed:
	media_device_unregister(&isp->media_dev);
	media_device_cleanup(&isp->media_dev);
	return ret;
}

static void atomisp_init_sensor(struct atomisp_input_subdev *input)
{
	static struct lock_class_key try_sd_state_key;
	struct v4l2_subdev_mbus_code_enum mbus_code_enum = { };
	struct v4l2_subdev_frame_size_enum fse = { };
	struct v4l2_subdev_selection sel = { };
	struct v4l2_subdev_state *try_sd_state, *act_sd_state;
	int i, err;

	/*
	 * FIXME: Drivers are not supposed to use __v4l2_subdev_state_alloc()
	 * but atomisp needs this for try_fmt on its /dev/video# node since
	 * it emulates a normal v4l2 device there, passing through try_fmt /
	 * set_fmt to the sensor.
	 */
	try_sd_state = __v4l2_subdev_state_alloc(input->camera,
				"atomisp:try_sd_state->lock", &try_sd_state_key);
	if (IS_ERR(try_sd_state))
		return;

	input->try_sd_state = try_sd_state;

	act_sd_state = v4l2_subdev_lock_and_get_active_state(input->camera);

	mbus_code_enum.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	err = v4l2_subdev_call(input->camera, pad, enum_mbus_code,
			       act_sd_state, &mbus_code_enum);
	if (!err)
		input->code = mbus_code_enum.code;

	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.target = V4L2_SEL_TGT_NATIVE_SIZE;
	err = v4l2_subdev_call(input->camera, pad, get_selection,
			       act_sd_state, &sel);
	if (err)
		goto unlock_act_sd_state;

	input->native_rect = sel.r;

	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.target = V4L2_SEL_TGT_CROP_DEFAULT;
	err = v4l2_subdev_call(input->camera, pad, get_selection,
			       act_sd_state, &sel);
	if (err)
		goto unlock_act_sd_state;

	input->active_rect = sel.r;

	/*
	 * Check for a framesize with half active_rect width and height,
	 * if found assume the sensor supports binning.
	 * Do this before changing the crop-rect since that may influence
	 * enum_frame_size results.
	 */
	for (i = 0; ; i++) {
		fse.index = i;
		fse.code = input->code;
		fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;

		err = v4l2_subdev_call(input->camera, pad, enum_frame_size,
				       act_sd_state, &fse);
		if (err)
			break;

		if (fse.min_width <= (input->active_rect.width / 2) &&
		    fse.min_height <= (input->active_rect.height / 2)) {
			input->binning_support = true;
			break;
		}
	}

	/*
	 * The ISP also wants the non-active pixels at the border of the sensor
	 * for padding, set the crop rect to cover the entire sensor instead
	 * of only the default active area.
	 *
	 * Do this for both try and active formats since the crop rect in
	 * try_sd_state may influence (clamp size) in calls with which == try.
	 */
	sel.which = V4L2_SUBDEV_FORMAT_TRY;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = input->native_rect;
	v4l2_subdev_lock_state(input->try_sd_state);
	err = v4l2_subdev_call(input->camera, pad, set_selection,
			       input->try_sd_state, &sel);
	v4l2_subdev_unlock_state(input->try_sd_state);
	if (err)
		goto unlock_act_sd_state;

	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = input->native_rect;
	err = v4l2_subdev_call(input->camera, pad, set_selection,
			       act_sd_state, &sel);
	if (err)
		goto unlock_act_sd_state;

	dev_info(input->camera->dev, "Supports crop native %dx%d active %dx%d binning %d\n",
		 input->native_rect.width, input->native_rect.height,
		 input->active_rect.width, input->active_rect.height,
		 input->binning_support);

	input->crop_support = true;

unlock_act_sd_state:
	if (act_sd_state)
		v4l2_subdev_unlock_state(act_sd_state);
}

int atomisp_register_device_nodes(struct atomisp_device *isp)
{
	struct atomisp_input_subdev *input;
	int i, err;

	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++) {
		err = media_create_pad_link(&isp->csi2_port[i].subdev.entity,
					    CSI2_PAD_SOURCE, &isp->asd.subdev.entity,
					    ATOMISP_SUBDEV_PAD_SINK, 0);
		if (err)
			return err;

		if (!isp->sensor_subdevs[i])
			continue;

		input = &isp->inputs[isp->input_cnt];

		input->port = i;
		input->camera = isp->sensor_subdevs[i];
		input->csi_port = &isp->csi2_port[i].subdev;

		atomisp_init_sensor(input);

		err = media_create_pad_link(&input->camera->entity, 0,
					    &isp->csi2_port[i].subdev.entity,
					    CSI2_PAD_SINK,
					    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
		if (err)
			return err;

		isp->input_cnt++;
	}

	if (!isp->input_cnt)
		dev_warn(isp->dev, "no camera attached or fail to detect\n");
	else
		dev_info(isp->dev, "detected %d camera sensors\n", isp->input_cnt);

	mutex_lock(&isp->media_dev.graph_mutex);
	atomisp_setup_input_links(isp);
	mutex_unlock(&isp->media_dev.graph_mutex);

	isp->asd.video_out.vdev.v4l2_dev = &isp->v4l2_dev;
	isp->asd.video_out.vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	err = video_register_device(&isp->asd.video_out.vdev, VFL_TYPE_VIDEO, -1);
	if (err)
		return err;

	err = media_create_pad_link(&isp->asd.subdev.entity, ATOMISP_SUBDEV_PAD_SOURCE,
				    &isp->asd.video_out.vdev.entity, 0,
				    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (err)
		return err;

	err = v4l2_device_register_subdev_nodes(&isp->v4l2_dev);
	if (err)
		return err;

	return media_device_register(&isp->media_dev);
}

static int atomisp_initialize_modules(struct atomisp_device *isp)
{
	int ret;

	ret = atomisp_mipi_csi2_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "mipi csi2 initialization failed\n");
		goto error_mipi_csi2;
	}

	ret = atomisp_subdev_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "ISP subdev initialization failed\n");
		goto error_isp_subdev;
	}

	return 0;

error_isp_subdev:
error_mipi_csi2:
	atomisp_mipi_csi2_cleanup(isp);
	return ret;
}

static void atomisp_uninitialize_modules(struct atomisp_device *isp)
{
	atomisp_mipi_csi2_cleanup(isp);
}

const struct firmware *
atomisp_load_firmware(struct atomisp_device *isp)
{
	const struct firmware *fw;
	int rc;
	char *fw_path = NULL;

	if ((isp->media_dev.hw_revision >> ATOMISP_HW_REVISION_SHIFT) ==
	    ATOMISP_HW_REVISION_ISP2401)
		fw_path = "intel/ipu/shisp_2401a0_v21.bin";

	if (isp->media_dev.hw_revision ==
	    ((ATOMISP_HW_REVISION_ISP2401_LEGACY << ATOMISP_HW_REVISION_SHIFT) |
	     ATOMISP_HW_STEPPING_A0))
		fw_path = "intel/ipu/shisp_2401a0_legacy_v21.bin";

	if (isp->media_dev.hw_revision ==
	    ((ATOMISP_HW_REVISION_ISP2400 << ATOMISP_HW_REVISION_SHIFT) |
	     ATOMISP_HW_STEPPING_B0))
		fw_path = "intel/ipu/shisp_2400b0_v21.bin";

	if (!fw_path) {
		dev_err(isp->dev, "Unsupported hw_revision 0x%x\n",
			isp->media_dev.hw_revision);
		return NULL;
	}

	rc = request_firmware(&fw, fw_path, isp->dev);
	/* Fallback to old fw_path without "intel/ipu/" prefix */
	if (rc)
		rc = request_firmware(&fw, kbasename(fw_path), isp->dev);
	if (rc) {
		dev_err(isp->dev,
			"atomisp: Error %d while requesting firmware %s\n",
			rc, fw_path);
		return NULL;
	}

	return fw;
}

static void atomisp_pm_init(struct atomisp_device *isp)
{
	/*
	 * The atomisp does not use standard PCI power-management through the
	 * PCI config space. Instead this driver directly tells the P-Unit to
	 * disable the ISP over the IOSF. The standard PCI subsystem pm_ops will
	 * try to access the config space before (resume) / after (suspend) this
	 * driver has turned the ISP on / off, resulting in the following errors:
	 *
	 * "Unable to change power state from D0 to D3hot, device inaccessible"
	 * "Unable to change power state from D3cold to D0, device inaccessible"
	 *
	 * To avoid these errors override the pm_domain so that all the PCI
	 * subsys suspend / resume handling is skipped.
	 */
	isp->pm_domain.ops.runtime_suspend = atomisp_power_off;
	isp->pm_domain.ops.runtime_resume = atomisp_power_on;
	isp->pm_domain.ops.suspend = atomisp_suspend;
	isp->pm_domain.ops.resume = atomisp_resume;

	cpu_latency_qos_add_request(&isp->pm_qos, PM_QOS_DEFAULT_VALUE);
	dev_pm_domain_set(isp->dev, &isp->pm_domain);

	pm_runtime_allow(isp->dev);
	pm_runtime_put_sync_suspend(isp->dev);
}

static void atomisp_pm_uninit(struct atomisp_device *isp)
{
	pm_runtime_get_sync(isp->dev);
	pm_runtime_forbid(isp->dev);
	dev_pm_domain_set(isp->dev, NULL);
	cpu_latency_qos_remove_request(&isp->pm_qos);
}

#define ATOM_ISP_PCI_BAR	0

static int atomisp_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct atomisp_device *isp;
	unsigned int start;
	u32 val;
	int err;

	/* Pointer to struct device. */
	atomisp_dev = &pdev->dev;

	start = pci_resource_start(pdev, ATOM_ISP_PCI_BAR);
	dev_dbg(&pdev->dev, "start: 0x%x\n", start);

	isp = devm_kzalloc(&pdev->dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->dev = &pdev->dev;
	isp->saved_regs.ispmmadr = start;
	isp->asd.isp = isp;

	mutex_init(&isp->mutex);
	spin_lock_init(&isp->lock);

	/* This is not a true PCI device on SoC, so the delay is not needed. */
	pdev->d3hot_delay = 0;

	pci_set_drvdata(pdev, isp);

	switch (id->device) {
	case ATOMISP_PCI_DEVICE_SOC_MRFLD:
	case ATOMISP_PCI_DEVICE_SOC_MRFLD_1179:
	case ATOMISP_PCI_DEVICE_SOC_MRFLD_117A:
		isp->media_dev.hw_revision =
		    (ATOMISP_HW_REVISION_ISP2400
		     << ATOMISP_HW_REVISION_SHIFT) |
		    ATOMISP_HW_STEPPING_B0;

		switch (id->device) {
		case ATOMISP_PCI_DEVICE_SOC_MRFLD_1179:
			isp->dfs = &dfs_config_merr_1179;
			break;
		case ATOMISP_PCI_DEVICE_SOC_MRFLD_117A:
			isp->dfs = &dfs_config_merr_117a;

			break;
		default:
			isp->dfs = &dfs_config_merr;
			break;
		}
		isp->hpll_freq = HPLL_FREQ_1600MHZ;
		break;
	case ATOMISP_PCI_DEVICE_SOC_BYT:
		isp->media_dev.hw_revision =
		    (ATOMISP_HW_REVISION_ISP2400
		     << ATOMISP_HW_REVISION_SHIFT) |
		    ATOMISP_HW_STEPPING_B0;

		/*
		 * Note: some Intel-based tablets with Android use a different
		 * DFS table. Based on the comments at the Yocto Aero meta
		 * version of this driver (at the ssid.h header), they're
		 * identified via a "spid" var:
		 *
		 *	androidboot.spid=vend:cust:manu:plat:prod:hard
		 *
		 * As we don't have this upstream, nor we know enough details
		 * to use a DMI or PCI match table, the old code was just
		 * removed, but let's keep a note here as a reminder that,
		 * for certain devices, we may need to limit the max DFS
		 * frequency to be below certain values, adjusting the
		 * resolution accordingly.
		 */
		isp->dfs = &dfs_config_byt;

		/*
		 * HPLL frequency is known to be device-specific, but we don't
		 * have specs yet for exactly how it varies.  Default to
		 * BYT-CR but let provisioning set it via EFI variable
		 */
		isp->hpll_freq = gmin_get_var_int(&pdev->dev, false, "HpllFreq", HPLL_FREQ_2000MHZ);

		/*
		 * for BYT/CHT we are put isp into D3cold to avoid pci registers access
		 * in power off. Set d3cold_delay to 0 since default 100ms is not
		 * necessary.
		 */
		pdev->d3cold_delay = 0;
		break;
	case ATOMISP_PCI_DEVICE_SOC_ANN:
		isp->media_dev.hw_revision = (ATOMISP_HW_REVISION_ISP2401
						 << ATOMISP_HW_REVISION_SHIFT);
		isp->media_dev.hw_revision |= pdev->revision < 2 ?
					      ATOMISP_HW_STEPPING_A0 : ATOMISP_HW_STEPPING_B0;
		isp->dfs = &dfs_config_merr;
		isp->hpll_freq = HPLL_FREQ_1600MHZ;
		break;
	case ATOMISP_PCI_DEVICE_SOC_CHT:
		isp->media_dev.hw_revision = (ATOMISP_HW_REVISION_ISP2401
						 << ATOMISP_HW_REVISION_SHIFT);
		isp->media_dev.hw_revision |= pdev->revision < 2 ?
					      ATOMISP_HW_STEPPING_A0 : ATOMISP_HW_STEPPING_B0;

		isp->dfs = &dfs_config_cht;
		pdev->d3cold_delay = 0;

		iosf_mbi_read(BT_MBI_UNIT_CCK, MBI_REG_READ, CCK_FUSE_REG_0, &val);
		switch (val & CCK_FUSE_HPLL_FREQ_MASK) {
		case 0x00:
			isp->hpll_freq = HPLL_FREQ_800MHZ;
			break;
		case 0x01:
			isp->hpll_freq = HPLL_FREQ_1600MHZ;
			break;
		case 0x02:
			isp->hpll_freq = HPLL_FREQ_2000MHZ;
			break;
		default:
			isp->hpll_freq = HPLL_FREQ_1600MHZ;
			dev_warn(&pdev->dev, "read HPLL from cck failed. Default to 1600 MHz.\n");
		}
		break;
	default:
		dev_err(&pdev->dev, "un-supported IUNIT device\n");
		return -ENODEV;
	}

	if (pdev->revision <= ATOMISP_PCI_REV_BYT_A0_MAX) {
		dev_err(&pdev->dev, "revision %d is not supported\n", pdev->revision);
		return -ENODEV;
	}

	dev_info(&pdev->dev, "ISP HPLL frequency base = %d MHz\n", isp->hpll_freq);

	isp->max_isr_latency = ATOMISP_MAX_ISR_LATENCY;

	/* Load isp firmware from user space */
	isp->firmware = atomisp_load_firmware(isp);
	if (!isp->firmware) {
		/* No firmware continue in pm-only mode for S0i3 support */
		dev_info(&pdev->dev, "Continuing in power-management only mode\n");
		isp->pm_only = true;
		atomisp_pm_init(isp);
		return 0;
	}

	err = sh_css_check_firmware_version(isp->dev, isp->firmware->data);
	if (err) {
		dev_dbg(&pdev->dev, "Firmware version check failed\n");
		goto error_release_firmware;
	}

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable ISP PCI device (%d)\n", err);
		goto error_release_firmware;
	}

	err = pcim_iomap_regions(pdev, BIT(ATOM_ISP_PCI_BAR), pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "Failed to I/O memory remapping (%d)\n", err);
		goto error_release_firmware;
	}

	isp->base = pcim_iomap_table(pdev)[ATOM_ISP_PCI_BAR];

	pci_set_master(pdev);

	err = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to enable msi (%d)\n", err);
		goto error_release_firmware;
	}

	atomisp_msi_irq_init(isp);

	/*
	 * for MRFLD, Software/firmware needs to write a 1 to bit 0 of
	 * the register at CSI_RECEIVER_SELECTION_REG to enable SH CSI
	 * backend write 0 will enable Arasan CSI backend, which has
	 * bugs(like sighting:4567697 and 4567699) and will be removed
	 * in B0
	 */
	atomisp_css2_hw_store_32(MRFLD_CSI_RECEIVER_SELECTION_REG, 1);

	switch (id->device) {
	case ATOMISP_PCI_DEVICE_SOC_MRFLD:
	case ATOMISP_PCI_DEVICE_SOC_MRFLD_1179:
	case ATOMISP_PCI_DEVICE_SOC_MRFLD_117A:
		/*
		 * Workaround for imbalance data eye issue which is observed
		 * on TNG B0.
		 */
		pci_read_config_dword(pdev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL, &val);
		val &= ~((MRFLD_PCI_CSI_HSRXCLKTRIM_MASK << MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT) |
			 (MRFLD_PCI_CSI_HSRXCLKTRIM_MASK << MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT) |
			 (MRFLD_PCI_CSI_HSRXCLKTRIM_MASK << MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT));
		val |= (MRFLD_PCI_CSI1_HSRXCLKTRIM << MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT) |
		       (MRFLD_PCI_CSI2_HSRXCLKTRIM << MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT) |
		       (MRFLD_PCI_CSI3_HSRXCLKTRIM << MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT);
		pci_write_config_dword(pdev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL, val);
		break;
	default:
		break;
	}

	err = atomisp_initialize_modules(isp);
	if (err < 0) {
		dev_err(&pdev->dev, "atomisp_initialize_modules (%d)\n", err);
		goto error_irq_uninit;
	}

	err = atomisp_register_entities(isp);
	if (err < 0) {
		dev_err(&pdev->dev, "atomisp_register_entities failed (%d)\n", err);
		goto error_uninitialize_modules;
	}

	INIT_WORK(&isp->assert_recovery_work, atomisp_assert_recovery_work);

	/* save the iunit context only once after all the values are init'ed. */
	atomisp_save_iunit_reg(isp);

	/* Init ISP memory management */
	hmm_init();

	err = devm_request_threaded_irq(&pdev->dev, pdev->irq,
					atomisp_isr, atomisp_isr_thread,
					IRQF_SHARED, "isp_irq", isp);
	if (err) {
		dev_err(&pdev->dev, "Failed to request irq (%d)\n", err);
		goto error_unregister_entities;
	}

	/* Load firmware into ISP memory */
	err = atomisp_css_load_firmware(isp);
	if (err) {
		dev_err(&pdev->dev, "Failed to init css.\n");
		goto error_free_irq;
	}
	/* Clear FW image from memory */
	release_firmware(isp->firmware);
	isp->firmware = NULL;
	isp->css_env.isp_css_fw.data = NULL;

	atomisp_pm_init(isp);

	err = v4l2_async_nf_register(&isp->notifier);
	if (err) {
		dev_err(isp->dev, "failed to register async notifier : %d\n", err);
		goto error_unload_firmware;
	}

	return 0;

error_unload_firmware:
	atomisp_pm_uninit(isp);
	ia_css_unload_firmware();
error_free_irq:
	devm_free_irq(&pdev->dev, pdev->irq, isp);
error_unregister_entities:
	hmm_cleanup();
	atomisp_unregister_entities(isp);
error_uninitialize_modules:
	atomisp_uninitialize_modules(isp);
error_irq_uninit:
	atomisp_msi_irq_uninit(isp);
	pci_free_irq_vectors(pdev);
error_release_firmware:
	release_firmware(isp->firmware);
	return err;
}

static void atomisp_pci_remove(struct pci_dev *pdev)
{
	struct atomisp_device *isp = pci_get_drvdata(pdev);

	atomisp_pm_uninit(isp);

	if (isp->pm_only)
		return;

	/* Undo ia_css_init() from atomisp_power_on() */
	atomisp_css_uninit(isp);
	ia_css_unload_firmware();
	devm_free_irq(&pdev->dev, pdev->irq, isp);
	hmm_cleanup();

	atomisp_unregister_entities(isp);
	atomisp_uninitialize_modules(isp);
	atomisp_msi_irq_uninit(isp);
	pci_free_irq_vectors(pdev);
}

static const struct pci_device_id atomisp_pci_tbl[] = {
	/* Merrifield */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_MRFLD)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_MRFLD_1179)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_MRFLD_117A)},
	/* Baytrail */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_BYT)},
	/* Anniedale (Merrifield+ / Moorefield) */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_ANN)},
	/* Cherrytrail */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, ATOMISP_PCI_DEVICE_SOC_CHT)},
	{0,}
};
MODULE_DEVICE_TABLE(pci, atomisp_pci_tbl);

static struct pci_driver atomisp_pci_driver = {
	.driver = {
		.dev_groups = dbg_attr_groups,
	},
	.name = "atomisp-isp2",
	.id_table = atomisp_pci_tbl,
	.probe = atomisp_pci_probe,
	.remove = atomisp_pci_remove,
};

module_pci_driver(atomisp_pci_driver);

MODULE_AUTHOR("Wen Wang <wen.w.wang@intel.com>");
MODULE_AUTHOR("Xiaolin Zhang <xiaolin.zhang@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel ATOM Platform ISP Driver");
MODULE_IMPORT_NS(INTEL_IPU_BRIDGE);
