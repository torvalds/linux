/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010-2017 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../../include/linux/atomisp_gmin_platform.h"

#include "atomisp_cmd.h"
#include "atomisp_common.h"
#include "atomisp_fops.h"
#include "atomisp_file.h"
#include "atomisp_ioctl.h"
#include "atomisp_internal.h"
#include "atomisp_acc.h"
#include "atomisp-regs.h"
#include "atomisp_dfs_tables.h"
#include "atomisp_drvfs.h"
#include "hmm/hmm.h"
#include "atomisp_trace_event.h"

#include "hrt/hive_isp_css_mm_hrt.h"

#include "device_access.h"
#include <asm/intel-mid.h>

/* G-Min addition: pull this in from intel_mid_pm.h */
#define CSTATE_EXIT_LATENCY_C1  1

static uint skip_fwload;
module_param(skip_fwload, uint, 0644);
MODULE_PARM_DESC(skip_fwload, "Skip atomisp firmware load");

/* set reserved memory pool size in page */
static unsigned int repool_pgnr;
module_param(repool_pgnr, uint, 0644);
MODULE_PARM_DESC(repool_pgnr,
		"Set the reserved memory pool size in page (default:0)");

/* set dynamic memory pool size in page */
unsigned int dypool_pgnr = UINT_MAX;
module_param(dypool_pgnr, uint, 0644);
MODULE_PARM_DESC(dypool_pgnr,
		"Set the dynamic memory pool size in page (default:0)");

bool dypool_enable;
module_param(dypool_enable, bool, 0644);
MODULE_PARM_DESC(dypool_enable,
		"dynamic memory pool enable/disable (default:disable)");

/* memory optimization: deferred firmware loading */
bool defer_fw_load;
module_param(defer_fw_load, bool, 0644);
MODULE_PARM_DESC(defer_fw_load,
		"Defer FW loading until device is opened (default:disable)");

/* cross componnet debug message flag */
int dbg_level;
module_param(dbg_level, int, 0644);
MODULE_PARM_DESC(dbg_level, "debug message on/off (default:off)");

/* log function switch */
int dbg_func = 2;
module_param(dbg_func, int, 0644);
MODULE_PARM_DESC(dbg_func,
		"log function switch non/trace_printk/printk (default:printk)");

int mipicsi_flag;
module_param(mipicsi_flag, int, 0644);
MODULE_PARM_DESC(mipicsi_flag, "mipi csi compression predictor algorithm");

/*set to 16x16 since this is the amount of lines and pixels the sensor
exports extra. If these are kept at the 10x8 that they were on, in yuv
downscaling modes incorrect resolutions where requested to the sensor
driver with strange outcomes as a result. The proper way tot do this
would be to have a list of tables the specify the sensor res, mipi rec,
output res, and isp output res. however since we do not have this yet,
the chosen solution is the next best thing. */
int pad_w = 16;
module_param(pad_w, int, 0644);
MODULE_PARM_DESC(pad_w, "extra data for ISP processing");

int pad_h = 16;
module_param(pad_h, int, 0644);
MODULE_PARM_DESC(pad_h, "extra data for ISP processing");

struct device *atomisp_dev;

void __iomem *atomisp_io_base;

int atomisp_video_init(struct atomisp_video_pipe *video, const char *name)
{
	int ret;
	const char *direction;

	switch (video->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		direction = "output";
		video->pad.flags = MEDIA_PAD_FL_SINK;
		video->vdev.fops = &atomisp_fops;
		video->vdev.ioctl_ops = &atomisp_ioctl_ops;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		direction = "input";
		video->pad.flags = MEDIA_PAD_FL_SOURCE;
		video->vdev.fops = &atomisp_file_fops;
		video->vdev.ioctl_ops = &atomisp_file_ioctl_ops;
		break;
	default:
		return -EINVAL;
	}

	ret = media_entity_pads_init(&video->vdev.entity, 1, &video->pad);
	if (ret < 0)
		return ret;

	/* Initialize the video device. */
	snprintf(video->vdev.name, sizeof(video->vdev.name),
		 "ATOMISP ISP %s %s", name, direction);
	video->vdev.release = video_device_release_empty;
	video_set_drvdata(&video->vdev, video->isp);

	return 0;
}

void atomisp_acc_init(struct atomisp_acc_pipe *video, const char *name)
{
	video->vdev.fops = &atomisp_fops;
	video->vdev.ioctl_ops = &atomisp_ioctl_ops;

	/* Initialize the video device. */
	snprintf(video->vdev.name, sizeof(video->vdev.name),
		 "ATOMISP ISP %s", name);
	video->vdev.release = video_device_release_empty;
	video_set_drvdata(&video->vdev, video->isp);
}

int atomisp_video_register(struct atomisp_video_pipe *video,
	struct v4l2_device *vdev)
{
	int ret;

	video->vdev.v4l2_dev = vdev;

	ret = video_register_device(&video->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		dev_err(vdev->dev, "%s: could not register video device (%d)\n",
			__func__, ret);

	return ret;
}

int atomisp_acc_register(struct atomisp_acc_pipe *video,
		struct v4l2_device *vdev)
{
	int ret;

	video->vdev.v4l2_dev = vdev;

	ret = video_register_device(&video->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		dev_err(vdev->dev, "%s: could not register video device (%d)\n",
			__func__, ret);

	return ret;
}

void atomisp_video_unregister(struct atomisp_video_pipe *video)
{
	if (video_is_registered(&video->vdev)) {
		media_entity_cleanup(&video->vdev.entity);
		video_unregister_device(&video->vdev);
	}
}

void atomisp_acc_unregister(struct atomisp_acc_pipe *video)
{
	if (video_is_registered(&video->vdev))
		video_unregister_device(&video->vdev);
}

static int atomisp_save_iunit_reg(struct atomisp_device *isp)
{
	struct pci_dev *dev = isp->pdev;

	dev_dbg(isp->dev, "%s\n", __func__);

	pci_read_config_word(dev, PCI_COMMAND, &isp->saved_regs.pcicmdsts);
	/* isp->saved_regs.ispmmadr is set from the atomisp_pci_probe() */
	pci_read_config_dword(dev, PCI_MSI_CAPID, &isp->saved_regs.msicap);
	pci_read_config_dword(dev, PCI_MSI_ADDR, &isp->saved_regs.msi_addr);
	pci_read_config_word(dev, PCI_MSI_DATA,  &isp->saved_regs.msi_data);
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &isp->saved_regs.intr);
	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL,
			      &isp->saved_regs.interrupt_control);

	pci_read_config_dword(dev, MRFLD_PCI_PMCS,
			      &isp->saved_regs.pmcs);
	/* Ensure read/write combining is enabled. */
	pci_read_config_dword(dev, PCI_I_CONTROL,
			&isp->saved_regs.i_control);
	isp->saved_regs.i_control |=
			MRFLD_PCI_I_CONTROL_ENABLE_READ_COMBINING |
			MRFLD_PCI_I_CONTROL_ENABLE_WRITE_COMBINING;
	pci_read_config_dword(dev, MRFLD_PCI_CSI_ACCESS_CTRL_VIOL,
			      &isp->saved_regs.csi_access_viol);
	pci_read_config_dword(dev, MRFLD_PCI_CSI_RCOMP_CONTROL,
			      &isp->saved_regs.csi_rcomp_config);
	/*
	 * Hardware bugs require setting CSI_HS_OVR_CLK_GATE_ON_UPDATE.
	 * ANN/CHV: RCOMP updates do not happen when using CSI2+ path
	 * and sensor sending "continuous clock".
	 * TNG/ANN/CHV: MIPI packets are lost if the HS entry sequence
	 * is missed, and IUNIT can hang.
	 * For both issues, setting this bit is a workaround.
	 */
	isp->saved_regs.csi_rcomp_config |=
		MRFLD_PCI_CSI_HS_OVR_CLK_GATE_ON_UPDATE;
	pci_read_config_dword(dev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
			      &isp->saved_regs.csi_afe_dly);
	pci_read_config_dword(dev, MRFLD_PCI_CSI_CONTROL,
			      &isp->saved_regs.csi_control);
	if (isp->media_dev.hw_revision >=
	    (ATOMISP_HW_REVISION_ISP2401 << ATOMISP_HW_REVISION_SHIFT))
		isp->saved_regs.csi_control |=
			MRFLD_PCI_CSI_CONTROL_PARPATHEN;
	/*
	 * On CHT CSI_READY bit should be enabled before stream on
	 */
	if (IS_CHT && (isp->media_dev.hw_revision >= ((ATOMISP_HW_REVISION_ISP2401 <<
	    ATOMISP_HW_REVISION_SHIFT) | ATOMISP_HW_STEPPING_B0)))
		isp->saved_regs.csi_control |=
			MRFLD_PCI_CSI_CONTROL_CSI_READY;
	pci_read_config_dword(dev, MRFLD_PCI_CSI_AFE_RCOMP_CONTROL,
			      &isp->saved_regs.csi_afe_rcomp_config);
	pci_read_config_dword(dev, MRFLD_PCI_CSI_AFE_HS_CONTROL,
			      &isp->saved_regs.csi_afe_hs_control);
	pci_read_config_dword(dev, MRFLD_PCI_CSI_DEADLINE_CONTROL,
			      &isp->saved_regs.csi_deadline_control);
	return 0;
}

static int __maybe_unused atomisp_restore_iunit_reg(struct atomisp_device *isp)
{
	struct pci_dev *dev = isp->pdev;

	dev_dbg(isp->dev, "%s\n", __func__);

	pci_write_config_word(dev, PCI_COMMAND, isp->saved_regs.pcicmdsts);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       isp->saved_regs.ispmmadr);
	pci_write_config_dword(dev, PCI_MSI_CAPID, isp->saved_regs.msicap);
	pci_write_config_dword(dev, PCI_MSI_ADDR, isp->saved_regs.msi_addr);
	pci_write_config_word(dev, PCI_MSI_DATA, isp->saved_regs.msi_data);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, isp->saved_regs.intr);
	pci_write_config_dword(dev, PCI_INTERRUPT_CTRL,
			       isp->saved_regs.interrupt_control);
	pci_write_config_dword(dev, PCI_I_CONTROL,
					isp->saved_regs.i_control);

	pci_write_config_dword(dev, MRFLD_PCI_PMCS,
					isp->saved_regs.pmcs);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_ACCESS_CTRL_VIOL,
			      isp->saved_regs.csi_access_viol);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_RCOMP_CONTROL,
			      isp->saved_regs.csi_rcomp_config);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
			      isp->saved_regs.csi_afe_dly);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_CONTROL,
			      isp->saved_regs.csi_control);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_AFE_RCOMP_CONTROL,
			      isp->saved_regs.csi_afe_rcomp_config);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_AFE_HS_CONTROL,
			      isp->saved_regs.csi_afe_hs_control);
	pci_write_config_dword(dev, MRFLD_PCI_CSI_DEADLINE_CONTROL,
			      isp->saved_regs.csi_deadline_control);

	/*
	 * for MRFLD, Software/firmware needs to write a 1 to bit0
	 * of the register at CSI_RECEIVER_SELECTION_REG to enable
	 * SH CSI backend write 0 will enable Arasan CSI backend,
	 * which has bugs(like sighting:4567697 and 4567699) and
	 * will be removed in B0
	 */
	atomisp_store_uint32(MRFLD_CSI_RECEIVER_SELECTION_REG, 1);
	return 0;
}

static int atomisp_mrfld_pre_power_down(struct atomisp_device *isp)
{
	struct pci_dev *dev = isp->pdev;
	u32 irq;
	unsigned long flags;

	spin_lock_irqsave(&isp->lock, flags);
	if (isp->sw_contex.power_state == ATOM_ISP_POWER_DOWN) {
		spin_unlock_irqrestore(&isp->lock, flags);
		dev_dbg(isp->dev, "<%s %d.\n", __func__, __LINE__);
		return 0;
	}
	/*
	 * MRFLD HAS requirement: cannot power off i-unit if
	 * ISP has IRQ not serviced.
	 * So, here we need to check if there is any pending
	 * IRQ, if so, waiting for it to be served
	 */
	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
	irq = irq & 1 << INTR_IIR;
	pci_write_config_dword(dev, PCI_INTERRUPT_CTRL, irq);

	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
	if (!(irq & (1 << INTR_IIR)))
		goto done;

	atomisp_store_uint32(MRFLD_INTR_CLEAR_REG, 0xFFFFFFFF);
	atomisp_load_uint32(MRFLD_INTR_STATUS_REG, &irq);
	if (irq != 0) {
		dev_err(isp->dev,
			 "%s: fail to clear isp interrupt status reg=0x%x\n",
			 __func__, irq);
		spin_unlock_irqrestore(&isp->lock, flags);
		return -EAGAIN;
	} else {
		pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
		irq = irq & 1 << INTR_IIR;
		pci_write_config_dword(dev, PCI_INTERRUPT_CTRL, irq);

		pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
		if (!(irq & (1 << INTR_IIR))) {
			atomisp_store_uint32(MRFLD_INTR_ENABLE_REG, 0x0);
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
	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
	irq &= ~(1 << INTR_IER);
	pci_write_config_dword(dev, PCI_INTERRUPT_CTRL, irq);

	atomisp_msi_irq_uninit(isp, dev);
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
	int reg = intel_mid_msgbus_read32(PUNIT_PORT, MRFLD_ISPSSDVFS);
	int door_bell = 1 << 8;
	int max_wait = 30;

	if (enable) {
		reg &= ~(MRFLD_BIT0 | MRFLD_BIT1);
	} else {
		reg |= (MRFLD_BIT1 | door_bell);
		reg &= ~(MRFLD_BIT0);
	}

	intel_mid_msgbus_write32(PUNIT_PORT, MRFLD_ISPSSDVFS, reg);

	/*Check Req_ACK to see freq status, wait until door_bell is cleared*/
	if (reg & door_bell) {
		while (max_wait--) {
			if (0 == (intel_mid_msgbus_read32(PUNIT_PORT,
				MRFLD_ISPSSDVFS) & door_bell))
					break;

			usleep_range(100, 500);
		}
	}

	if (max_wait == -1)
		pr_info("DDR DVFS, door bell is not cleared within 3ms\n");
}

/* Workaround for pmu_nc_set_power_state not ready in MRFLD */
int atomisp_mrfld_power_down(struct atomisp_device *isp)
{
	unsigned long timeout;
	u32 reg_value;

	/* writing 0x3 to ISPSSPM0 bit[1:0] to power off the IUNIT */
	reg_value = intel_mid_msgbus_read32(PUNIT_PORT, MRFLD_ISPSSPM0);
	reg_value &= ~MRFLD_ISPSSPM0_ISPSSC_MASK;
	reg_value |= MRFLD_ISPSSPM0_IUNIT_POWER_OFF;
	intel_mid_msgbus_write32(PUNIT_PORT, MRFLD_ISPSSPM0, reg_value);

	/*WA:Enable DVFS*/
	if (IS_CHT)
		punit_ddr_dvfs_enable(true);

	/*
	 * There should be no iunit access while power-down is
	 * in progress HW sighting: 4567865
	 * FIXME: msecs_to_jiffies(50)- experienced value
	 */
	timeout = jiffies + msecs_to_jiffies(50);
	while (1) {
		reg_value = intel_mid_msgbus_read32(PUNIT_PORT,
							MRFLD_ISPSSPM0);
		dev_dbg(isp->dev, "power-off in progress, ISPSSPM0: 0x%x\n",
				reg_value);
		/* wait until ISPSSPM0 bit[25:24] shows 0x3 */
		if ((reg_value >> MRFLD_ISPSSPM0_ISPSSS_OFFSET) ==
			MRFLD_ISPSSPM0_IUNIT_POWER_OFF) {
			trace_ipu_cstate(0);
			return 0;
		}

		if (time_after(jiffies, timeout)) {
			dev_err(isp->dev, "power-off iunit timeout.\n");
			return -EBUSY;
		}
		/* FIXME: experienced value for delay */
		usleep_range(100, 150);
	}
}


/* Workaround for pmu_nc_set_power_state not ready in MRFLD */
int atomisp_mrfld_power_up(struct atomisp_device *isp)
{
	unsigned long timeout;
	u32 reg_value;

	/*WA for PUNIT, if DVFS enabled, ISP timeout observed*/
	if (IS_CHT)
		punit_ddr_dvfs_enable(false);

	/*
	 * FIXME:WA for ECS28A, with this sleep, CTS
	 * android.hardware.camera2.cts.CameraDeviceTest#testCameraDeviceAbort
	 * PASS, no impact on other platforms
	*/
	if (IS_BYT)
		msleep(10);

	/* writing 0x0 to ISPSSPM0 bit[1:0] to power off the IUNIT */
	reg_value = intel_mid_msgbus_read32(PUNIT_PORT, MRFLD_ISPSSPM0);
	reg_value &= ~MRFLD_ISPSSPM0_ISPSSC_MASK;
	intel_mid_msgbus_write32(PUNIT_PORT, MRFLD_ISPSSPM0, reg_value);

	/* FIXME: experienced value for delay */
	timeout = jiffies + msecs_to_jiffies(50);
	while (1) {
		reg_value = intel_mid_msgbus_read32(PUNIT_PORT, MRFLD_ISPSSPM0);
		dev_dbg(isp->dev, "power-on in progress, ISPSSPM0: 0x%x\n",
				reg_value);
		/* wait until ISPSSPM0 bit[25:24] shows 0x0 */
		if ((reg_value >> MRFLD_ISPSSPM0_ISPSSS_OFFSET) ==
			MRFLD_ISPSSPM0_IUNIT_POWER_ON) {
			trace_ipu_cstate(1);
			return 0;
		}

		if (time_after(jiffies, timeout)) {
			dev_err(isp->dev, "power-on iunit timeout.\n");
			return -EBUSY;
		}
		/* FIXME: experienced value for delay */
		usleep_range(100, 150);
	}
}

int atomisp_runtime_suspend(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
		dev_get_drvdata(dev);
	int ret;

	ret = atomisp_mrfld_pre_power_down(isp);
	if (ret)
		return ret;

	/*Turn off the ISP d-phy*/
	ret = atomisp_ospm_dphy_down(isp);
	if (ret)
		return ret;
	pm_qos_update_request(&isp->pm_qos, PM_QOS_DEFAULT_VALUE);
	return atomisp_mrfld_power_down(isp);
}

int atomisp_runtime_resume(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
		dev_get_drvdata(dev);
	int ret;

	ret = atomisp_mrfld_power_up(isp);
	if (ret)
			return ret;

	pm_qos_update_request(&isp->pm_qos, isp->max_isr_latency);
	if (isp->sw_contex.power_state == ATOM_ISP_POWER_DOWN) {
		/*Turn on ISP d-phy */
		ret = atomisp_ospm_dphy_up(isp);
		if (ret) {
			dev_err(isp->dev, "Failed to power up ISP!.\n");
			return -EINVAL;
		}
	}

	/*restore register values for iUnit and iUnitPHY registers*/
	if (isp->saved_regs.pcicmdsts)
		atomisp_restore_iunit_reg(isp);

	atomisp_freq_scaling(isp, ATOMISP_DFS_MODE_LOW, true);
	return 0;
}

static int __maybe_unused atomisp_suspend(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
		dev_get_drvdata(dev);
	/* FIXME: only has one isp_subdev at present */
	struct atomisp_sub_device *asd = &isp->asd[0];
	unsigned long flags;
	int ret;

	/*
	 * FIXME: Suspend is not supported by sensors. Abort if any video
	 * node was opened.
	 */
	if (atomisp_dev_users(isp))
		return -EBUSY;

	spin_lock_irqsave(&isp->lock, flags);
	if (asd->streaming != ATOMISP_DEVICE_STREAMING_DISABLED) {
		spin_unlock_irqrestore(&isp->lock, flags);
		dev_err(isp->dev, "atomisp cannot suspend at this time.\n");
		return -EINVAL;
	}
	spin_unlock_irqrestore(&isp->lock, flags);

	ret = atomisp_mrfld_pre_power_down(isp);
	if (ret)
		return ret;

	/*Turn off the ISP d-phy */
	ret = atomisp_ospm_dphy_down(isp);
	if (ret) {
		dev_err(isp->dev, "fail to power off ISP\n");
		return ret;
	}
	pm_qos_update_request(&isp->pm_qos, PM_QOS_DEFAULT_VALUE);
	return atomisp_mrfld_power_down(isp);
}

static int __maybe_unused atomisp_resume(struct device *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
		dev_get_drvdata(dev);
	int ret;

	ret = atomisp_mrfld_power_up(isp);
	if (ret)
		return ret;

	pm_qos_update_request(&isp->pm_qos, isp->max_isr_latency);

	/*Turn on ISP d-phy */
	ret = atomisp_ospm_dphy_up(isp);
	if (ret) {
		dev_err(isp->dev, "Failed to power up ISP!.\n");
		return -EINVAL;
	}

	/*restore register values for iUnit and iUnitPHY registers*/
	if (isp->saved_regs.pcicmdsts)
		atomisp_restore_iunit_reg(isp);

	atomisp_freq_scaling(isp, ATOMISP_DFS_MODE_LOW, true);
	return 0;
}

int atomisp_csi_lane_config(struct atomisp_device *isp)
{
	static const struct {
		u8 code;
		u8 lanes[MRFLD_PORT_NUM];
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
	u8 sensor_lanes[MRFLD_PORT_NUM] = { 0 };
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

	for (i = 0; i < isp->input_cnt; i++) {
		struct camera_mipi_info *mipi_info;

		if (isp->inputs[i].type != RAW_CAMERA &&
		    isp->inputs[i].type != SOC_CAMERA)
			continue;

		mipi_info = atomisp_to_sensor_mipi_info(isp->inputs[i].camera);
		if (!mipi_info)
			continue;

		switch (mipi_info->port) {
		case ATOMISP_CAMERA_PORT_PRIMARY:
			sensor_lanes[0] = mipi_info->num_lanes;
			break;
		case ATOMISP_CAMERA_PORT_SECONDARY:
			sensor_lanes[1] = mipi_info->num_lanes;
			break;
		case ATOMISP_CAMERA_PORT_TERTIARY:
			sensor_lanes[2] = mipi_info->num_lanes;
			break;
		default:
			dev_err(isp->dev,
				"%s: invalid port: %d for the %dth sensor\n",
				__func__, mipi_info->port, i);
			return -EINVAL;
		}
	}

	for (i = 0; i < nportconfigs; i++) {
		for (j = 0; j < MRFLD_PORT_NUM; j++)
			if (sensor_lanes[j] &&
			    sensor_lanes[j] != portconfigs[i].lanes[j])
				break;

		if (j == MRFLD_PORT_NUM)
			break;			/* Found matching setting */
	}

	if (i >= nportconfigs) {
		dev_err(isp->dev,
			"%s: could not find the CSI port setting for %d-%d-%d\n",
			__func__,
			sensor_lanes[0], sensor_lanes[1], sensor_lanes[2]);
		return -EINVAL;
	}

	pci_read_config_dword(isp->pdev, MRFLD_PCI_CSI_CONTROL, &csi_control);
	csi_control &= ~port_config_mask;
	csi_control |= (portconfigs[i].code << MRFLD_PORT_CONFIGCODE_SHIFT)
		| (portconfigs[i].lanes[0] ? 0 : (1 << MRFLD_PORT1_ENABLE_SHIFT))
		| (portconfigs[i].lanes[1] ? 0 : (1 << MRFLD_PORT2_ENABLE_SHIFT))
		| (portconfigs[i].lanes[2] ? 0 : (1 << MRFLD_PORT3_ENABLE_SHIFT))
		| (((1 << portconfigs[i].lanes[0]) - 1) << MRFLD_PORT1_LANES_SHIFT)
		| (((1 << portconfigs[i].lanes[1]) - 1) << MRFLD_PORT2_LANES_SHIFT)
		| (((1 << portconfigs[i].lanes[2]) - 1) << port3_lanes_shift);

	pci_write_config_dword(isp->pdev, MRFLD_PCI_CSI_CONTROL, csi_control);

	dev_dbg(isp->dev,
		"%s: the portconfig is %d-%d-%d, CSI_CONTROL is 0x%08X\n",
		__func__, portconfigs[i].lanes[0], portconfigs[i].lanes[1],
		portconfigs[i].lanes[2], csi_control);

	return 0;
}

static int atomisp_subdev_probe(struct atomisp_device *isp)
{
	const struct atomisp_platform_data *pdata;
	struct intel_v4l2_subdev_table *subdevs;
	int ret, raw_index = -1;

	pdata = atomisp_get_platform_data();
	if (pdata == NULL) {
		dev_err(isp->dev, "no platform data available\n");
		return 0;
	}

	for (subdevs = pdata->subdevs; subdevs->type; ++subdevs) {
		struct v4l2_subdev *subdev;
		struct i2c_board_info *board_info =
			&subdevs->v4l2_subdev.board_info;
		struct i2c_adapter *adapter =
			i2c_get_adapter(subdevs->v4l2_subdev.i2c_adapter_id);
		struct camera_sensor_platform_data *sensor_pdata;
		int sensor_num, i;

		if (adapter == NULL) {
			dev_err(isp->dev,
				"Failed to find i2c adapter for subdev %s\n",
				board_info->type);
			break;
		}

		/* In G-Min, the sensor devices will already be probed
		 * (via ACPI) and registered, do not create new
		 * ones */
		subdev = atomisp_gmin_find_subdev(adapter, board_info);
		ret = v4l2_device_register_subdev(&isp->v4l2_dev, subdev);
		if (ret) {
			dev_warn(isp->dev, "Subdev %s detection fail\n",
				 board_info->type);
			continue;
		}

		if (subdev == NULL) {
			dev_warn(isp->dev, "Subdev %s detection fail\n",
				 board_info->type);
			continue;
		}

		dev_info(isp->dev, "Subdev %s successfully register\n",
			 board_info->type);

		switch (subdevs->type) {
		case RAW_CAMERA:
			raw_index = isp->input_cnt;
			dev_dbg(isp->dev, "raw_index: %d\n", raw_index);
		case SOC_CAMERA:
			dev_dbg(isp->dev, "SOC_INDEX: %d\n", isp->input_cnt);
			if (isp->input_cnt >= ATOM_ISP_MAX_INPUTS) {
				dev_warn(isp->dev,
					 "too many atomisp inputs, ignored\n");
				break;
			}

			isp->inputs[isp->input_cnt].type = subdevs->type;
			isp->inputs[isp->input_cnt].port = subdevs->port;
			isp->inputs[isp->input_cnt].camera = subdev;
			isp->inputs[isp->input_cnt].sensor_index = 0;
			/*
			 * initialize the subdev frame size, then next we can
			 * judge whether frame_size store effective value via
			 * pixel_format.
			 */
			isp->inputs[isp->input_cnt].frame_size.pixel_format = 0;
			sensor_pdata = (struct camera_sensor_platform_data *)
					board_info->platform_data;
			if (sensor_pdata->get_camera_caps)
				isp->inputs[isp->input_cnt].camera_caps =
					sensor_pdata->get_camera_caps();
			else
				isp->inputs[isp->input_cnt].camera_caps =
					atomisp_get_default_camera_caps();
			sensor_num = isp->inputs[isp->input_cnt]
				.camera_caps->sensor_num;
			isp->input_cnt++;
			for (i = 1; i < sensor_num; i++) {
				if (isp->input_cnt >= ATOM_ISP_MAX_INPUTS) {
					dev_warn(isp->dev,
						"atomisp inputs out of range\n");
					break;
				}
				isp->inputs[isp->input_cnt] =
					isp->inputs[isp->input_cnt - 1];
				isp->inputs[isp->input_cnt].sensor_index = i;
				isp->input_cnt++;
			}
			break;
		case CAMERA_MOTOR:
			isp->motor = subdev;
			break;
		case LED_FLASH:
		case XENON_FLASH:
			isp->flash = subdev;
			break;
		default:
			dev_dbg(isp->dev, "unknown subdev probed\n");
			break;
		}

	}

	/*
	 * HACK: Currently VCM belongs to primary sensor only, but correct
	 * approach must be to acquire from platform code which sensor
	 * owns it.
	 */
	if (isp->motor && raw_index >= 0)
		isp->inputs[raw_index].motor = isp->motor;

	/* Proceed even if no modules detected. For COS mode and no modules. */
	if (!isp->inputs[0].camera)
		dev_warn(isp->dev, "no camera attached or fail to detect\n");

	return atomisp_csi_lane_config(isp);
}

static void atomisp_unregister_entities(struct atomisp_device *isp)
{
	unsigned int i;
	struct v4l2_subdev *sd, *next;

	for (i = 0; i < isp->num_of_streams; i++)
		atomisp_subdev_unregister_entities(&isp->asd[i]);
	atomisp_tpg_unregister_entities(&isp->tpg);
	atomisp_file_input_unregister_entities(&isp->file_dev);
	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++)
		atomisp_mipi_csi2_unregister_entities(&isp->csi2_port[i]);

	list_for_each_entry_safe(sd, next, &isp->v4l2_dev.subdevs, list)
		v4l2_device_unregister_subdev(sd);

	v4l2_device_unregister(&isp->v4l2_dev);
	media_device_unregister(&isp->media_dev);
}

static int atomisp_register_entities(struct atomisp_device *isp)
{
	int ret = 0;
	unsigned int i;

	isp->media_dev.dev = isp->dev;

	strlcpy(isp->media_dev.model, "Intel Atom ISP",
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

	ret =
	atomisp_file_input_register_entities(&isp->file_dev, &isp->v4l2_dev);
	if (ret < 0) {
		dev_err(isp->dev, "atomisp_file_input_register_entities\n");
		goto file_input_register_failed;
	}

	ret = atomisp_tpg_register_entities(&isp->tpg, &isp->v4l2_dev);
	if (ret < 0) {
		dev_err(isp->dev, "atomisp_tpg_register_entities\n");
		goto tpg_register_failed;
	}

	for (i = 0; i < isp->num_of_streams; i++) {
		struct atomisp_sub_device *asd = &isp->asd[i];

		ret = atomisp_subdev_register_entities(asd, &isp->v4l2_dev);
		if (ret < 0) {
			dev_err(isp->dev,
				"atomisp_subdev_register_entities fail\n");
			for (; i > 0; i--)
				atomisp_subdev_unregister_entities(
						&isp->asd[i - 1]);
			goto subdev_register_failed;
		}
	}

	for (i = 0; i < isp->num_of_streams; i++) {
		struct atomisp_sub_device *asd = &isp->asd[i];

		init_completion(&asd->init_done);

		asd->delayed_init_workq =
			alloc_workqueue(isp->v4l2_dev.name, WQ_CPU_INTENSIVE,
					1);
		if (asd->delayed_init_workq == NULL) {
			dev_err(isp->dev,
					"Failed to initialize delayed init workq\n");
			ret = -ENOMEM;

			for (; i > 0; i--)
				destroy_workqueue(isp->asd[i - 1].
						delayed_init_workq);
			goto wq_alloc_failed;
		}
		INIT_WORK(&asd->delayed_init_work, atomisp_delayed_init_work);
	}

	for (i = 0; i < isp->input_cnt; i++) {
		if (isp->inputs[i].port >= ATOMISP_CAMERA_NR_PORTS) {
			dev_err(isp->dev, "isp->inputs port %d not supported\n",
					isp->inputs[i].port);
			ret = -EINVAL;
			goto link_failed;
		}
	}

	dev_dbg(isp->dev,
		"FILE_INPUT enable, camera_cnt: %d\n", isp->input_cnt);
	isp->inputs[isp->input_cnt].type = FILE_INPUT;
	isp->inputs[isp->input_cnt].port = -1;
	isp->inputs[isp->input_cnt].camera_caps =
		    atomisp_get_default_camera_caps();
	isp->inputs[isp->input_cnt++].camera = &isp->file_dev.sd;

	if (isp->input_cnt < ATOM_ISP_MAX_INPUTS) {
		dev_dbg(isp->dev,
			"TPG detected, camera_cnt: %d\n", isp->input_cnt);
		isp->inputs[isp->input_cnt].type = TEST_PATTERN;
		isp->inputs[isp->input_cnt].port = -1;
		isp->inputs[isp->input_cnt].camera_caps =
		    atomisp_get_default_camera_caps();
		isp->inputs[isp->input_cnt++].camera = &isp->tpg.sd;
	} else {
		dev_warn(isp->dev, "too many atomisp inputs, TPG ignored.\n");
	}

	ret = v4l2_device_register_subdev_nodes(&isp->v4l2_dev);
	if (ret < 0)
		goto link_failed;

	return media_device_register(&isp->media_dev);

link_failed:
	for (i = 0; i < isp->num_of_streams; i++)
		destroy_workqueue(isp->asd[i].
				delayed_init_workq);
wq_alloc_failed:
	for (i = 0; i < isp->num_of_streams; i++)
		atomisp_subdev_unregister_entities(
					&isp->asd[i]);
subdev_register_failed:
	atomisp_tpg_unregister_entities(&isp->tpg);
tpg_register_failed:
	atomisp_file_input_unregister_entities(&isp->file_dev);
file_input_register_failed:
	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++)
		atomisp_mipi_csi2_unregister_entities(&isp->csi2_port[i]);
csi_and_subdev_probe_failed:
	v4l2_device_unregister(&isp->v4l2_dev);
v4l2_device_failed:
	media_device_unregister(&isp->media_dev);
    media_device_cleanup(&isp->media_dev);
	return ret;
}

static int atomisp_initialize_modules(struct atomisp_device *isp)
{
	int ret;

	ret = atomisp_mipi_csi2_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "mipi csi2 initialization failed\n");
		goto error_mipi_csi2;
	}

	ret = atomisp_file_input_init(isp);
	if (ret < 0) {
		dev_err(isp->dev,
			"file input device initialization failed\n");
		goto error_file_input;
	}

	ret = atomisp_tpg_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "tpg initialization failed\n");
		goto error_tpg;
	}

	ret = atomisp_subdev_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "ISP subdev initialization failed\n");
		goto error_isp_subdev;
	}


	return 0;

error_isp_subdev:
error_tpg:
	atomisp_tpg_cleanup(isp);
error_file_input:
	atomisp_file_input_cleanup(isp);
error_mipi_csi2:
	atomisp_mipi_csi2_cleanup(isp);
	return ret;
}

static void atomisp_uninitialize_modules(struct atomisp_device *isp)
{
	atomisp_tpg_cleanup(isp);
	atomisp_file_input_cleanup(isp);
	atomisp_mipi_csi2_cleanup(isp);
}

const struct firmware *
atomisp_load_firmware(struct atomisp_device *isp)
{
	const struct firmware *fw;
	int rc;
	char *fw_path = NULL;

	if (skip_fwload)
		return NULL;

	if (isp->media_dev.hw_revision ==
	    ((ATOMISP_HW_REVISION_ISP2401 << ATOMISP_HW_REVISION_SHIFT)
	     | ATOMISP_HW_STEPPING_A0))
		fw_path = "shisp_2401a0_v21.bin";

	if (isp->media_dev.hw_revision ==
	    ((ATOMISP_HW_REVISION_ISP2401_LEGACY << ATOMISP_HW_REVISION_SHIFT)
	     | ATOMISP_HW_STEPPING_A0))
		fw_path = "shisp_2401a0_legacy_v21.bin";

	if (isp->media_dev.hw_revision ==
	    ((ATOMISP_HW_REVISION_ISP2400 << ATOMISP_HW_REVISION_SHIFT)
	     | ATOMISP_HW_STEPPING_B0))
		fw_path = "shisp_2400b0_v21.bin";

	if (!fw_path) {
		dev_err(isp->dev,
			"Unsupported driver_version 0x%x, hw_revision 0x%x\n",
			isp->media_dev.driver_version,
			isp->media_dev.hw_revision);
		return NULL;
	}

	rc = request_firmware(&fw, fw_path, isp->dev);
	if (rc) {
		dev_err(isp->dev,
			"atomisp: Error %d while requesting firmware %s\n",
			rc, fw_path);
		return NULL;
	}

	return fw;
}

/*
 * Check for flags the driver was compiled with against the PCI
 * device. Always returns true on other than ISP 2400.
 */
static bool is_valid_device(struct pci_dev *dev,
			    const struct pci_device_id *id)
{
	unsigned int a0_max_id;

	switch (id->device & ATOMISP_PCI_DEVICE_SOC_MASK) {
	case ATOMISP_PCI_DEVICE_SOC_MRFLD:
		a0_max_id = ATOMISP_PCI_REV_MRFLD_A0_MAX;
		break;
	case ATOMISP_PCI_DEVICE_SOC_BYT:
		a0_max_id = ATOMISP_PCI_REV_BYT_A0_MAX;
		break;
	default:
		return true;
	}

	return dev->revision > a0_max_id;
}

static int init_atomisp_wdts(struct atomisp_device *isp)
{
	int i, err;

	atomic_set(&isp->wdt_work_queued, 0);
	isp->wdt_work_queue = alloc_workqueue(isp->v4l2_dev.name, 0, 1);
	if (isp->wdt_work_queue == NULL) {
		dev_err(isp->dev, "Failed to initialize wdt work queue\n");
		err = -ENOMEM;
		goto alloc_fail;
	}
	INIT_WORK(&isp->wdt_work, atomisp_wdt_work);

	for (i = 0; i < isp->num_of_streams; i++) {
		struct atomisp_sub_device *asd = &isp->asd[i];
		asd = &isp->asd[i];
#ifndef ISP2401
		setup_timer(&asd->wdt, atomisp_wdt, (unsigned long)isp);
#else
		setup_timer(&asd->video_out_capture.wdt,
			atomisp_wdt, (unsigned long)&asd->video_out_capture);
		setup_timer(&asd->video_out_preview.wdt,
			atomisp_wdt, (unsigned long)&asd->video_out_preview);
		setup_timer(&asd->video_out_vf.wdt,
			atomisp_wdt, (unsigned long)&asd->video_out_vf);
		setup_timer(&asd->video_out_video_capture.wdt,
			atomisp_wdt,
			(unsigned long)&asd->video_out_video_capture);
#endif
	}
	return 0;
alloc_fail:
	return err;
}

static struct pci_driver atomisp_pci_driver;

#define ATOM_ISP_PCI_BAR	0

static int atomisp_pci_probe(struct pci_dev *dev,
				       const struct pci_device_id *id)
{
	const struct atomisp_platform_data *pdata;
	struct atomisp_device *isp;
	unsigned int start;
	void __iomem *base;
	int err, val;
	u32 irq;

	if (!dev) {
		dev_err(&dev->dev, "atomisp: error device ptr\n");
		return -EINVAL;
	}

	if (!is_valid_device(dev, id))
		return -ENODEV;
	/* Pointer to struct device. */
	atomisp_dev = &dev->dev;

	pdata = atomisp_get_platform_data();
	if (pdata == NULL)
		dev_warn(&dev->dev, "no platform data available\n");

	err = pcim_enable_device(dev);
	if (err) {
		dev_err(&dev->dev, "Failed to enable CI ISP device (%d)\n",
			err);
		return err;
	}

	start = pci_resource_start(dev, ATOM_ISP_PCI_BAR);
	dev_dbg(&dev->dev, "start: 0x%x\n", start);

	err = pcim_iomap_regions(dev, 1 << ATOM_ISP_PCI_BAR, pci_name(dev));
	if (err) {
		dev_err(&dev->dev, "Failed to I/O memory remapping (%d)\n",
			err);
		return err;
	}

	base = pcim_iomap_table(dev)[ATOM_ISP_PCI_BAR];
	dev_dbg(&dev->dev, "base: %p\n", base);

	atomisp_io_base = base;

	dev_dbg(&dev->dev, "atomisp_io_base: %p\n", atomisp_io_base);

	isp = devm_kzalloc(&dev->dev, sizeof(struct atomisp_device), GFP_KERNEL);
	if (!isp) {
		dev_err(&dev->dev, "Failed to alloc CI ISP structure\n");
		return -ENOMEM;
	}
	isp->pdev = dev;
	isp->dev = &dev->dev;
	isp->sw_contex.power_state = ATOM_ISP_POWER_UP;
	isp->pci_root = pci_get_bus_and_slot(0, 0);
	if (!isp->pci_root) {
		dev_err(&dev->dev, "Unable to find PCI host\n");
		return -ENODEV;
	}
	isp->saved_regs.ispmmadr = start;

	rt_mutex_init(&isp->mutex);
	mutex_init(&isp->streamoff_mutex);
	spin_lock_init(&isp->lock);

	/* This is not a true PCI device on SoC, so the delay is not needed. */
	isp->pdev->d3_delay = 0;

	isp->media_dev.driver_version = LINUX_VERSION_CODE;

	switch (id->device & ATOMISP_PCI_DEVICE_SOC_MASK) {
	case ATOMISP_PCI_DEVICE_SOC_MRFLD:
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
#ifdef FIXME			
		if (INTEL_MID_BOARD(3, TABLET, BYT, BLK, PRO, CRV2) ||
			INTEL_MID_BOARD(3, TABLET, BYT, BLK, ENG, CRV2)) {
			isp->dfs = &dfs_config_byt_cr;
			isp->hpll_freq = HPLL_FREQ_2000MHZ;
		} else
#endif		
		{
			isp->dfs = &dfs_config_byt;
			isp->hpll_freq = HPLL_FREQ_1600MHZ;
		}
		/* HPLL frequency is known to be device-specific, but we don't
		 * have specs yet for exactly how it varies.  Default to
		 * BYT-CR but let provisioning set it via EFI variable */
		isp->hpll_freq = gmin_get_var_int(&dev->dev, "HpllFreq",
					HPLL_FREQ_2000MHZ);

		/*
		 * for BYT/CHT we are put isp into D3cold to avoid pci registers access
		 * in power off. Set d3cold_delay to 0 since default 100ms is not
		 * necessary.
		 */
		isp->pdev->d3cold_delay = 0;
		break;
	case ATOMISP_PCI_DEVICE_SOC_ANN:
		isp->media_dev.hw_revision = (
#ifdef ISP2401_NEW_INPUT_SYSTEM
			 ATOMISP_HW_REVISION_ISP2401
#else
			 ATOMISP_HW_REVISION_ISP2401_LEGACY
#endif
			 << ATOMISP_HW_REVISION_SHIFT);
		isp->media_dev.hw_revision |= isp->pdev->revision < 2 ?
			ATOMISP_HW_STEPPING_A0 : ATOMISP_HW_STEPPING_B0;
		isp->dfs = &dfs_config_merr;
		isp->hpll_freq = HPLL_FREQ_1600MHZ;
		break;
	case ATOMISP_PCI_DEVICE_SOC_CHT:
		isp->media_dev.hw_revision = (
#ifdef ISP2401_NEW_INPUT_SYSTEM
			 ATOMISP_HW_REVISION_ISP2401
#else
			 ATOMISP_HW_REVISION_ISP2401_LEGACY
#endif
			<< ATOMISP_HW_REVISION_SHIFT);
		isp->media_dev.hw_revision |= isp->pdev->revision < 2 ?
			 ATOMISP_HW_STEPPING_A0 : ATOMISP_HW_STEPPING_B0;

		isp->dfs = &dfs_config_cht;
		isp->pdev->d3cold_delay = 0;

		val = intel_mid_msgbus_read32(CCK_PORT, CCK_FUSE_REG_0);
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
			dev_warn(isp->dev,
				 "read HPLL from cck failed.default 1600MHz.\n");
		}
		break;
	default:
		dev_err(&dev->dev, "un-supported IUNIT device\n");
		return -ENODEV;
	}

	dev_info(&dev->dev, "ISP HPLL frequency base = %d MHz\n",
		 isp->hpll_freq);

	isp->max_isr_latency = ATOMISP_MAX_ISR_LATENCY;

	/* Load isp firmware from user space */
	if (!defer_fw_load) {
		isp->firmware = atomisp_load_firmware(isp);
		if (!isp->firmware) {
			err = -ENOENT;
			goto load_fw_fail;
		}

		err = atomisp_css_check_firmware_version(isp);
		if (err) {
			dev_dbg(&dev->dev, "Firmware version check failed\n");
			goto fw_validation_fail;
		}
	}

	pci_set_master(dev);
	pci_set_drvdata(dev, isp);

	err = pci_enable_msi(dev);
	if (err) {
		dev_err(&dev->dev, "Failed to enable msi (%d)\n", err);
		goto enable_msi_fail;
	}

	atomisp_msi_irq_init(isp, dev);

	pm_qos_add_request(&isp->pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	/*
	 * for MRFLD, Software/firmware needs to write a 1 to bit 0 of
	 * the register at CSI_RECEIVER_SELECTION_REG to enable SH CSI
	 * backend write 0 will enable Arasan CSI backend, which has
	 * bugs(like sighting:4567697 and 4567699) and will be removed
	 * in B0
	 */
	atomisp_store_uint32(MRFLD_CSI_RECEIVER_SELECTION_REG, 1);

	if ((id->device & ATOMISP_PCI_DEVICE_SOC_MASK) ==
			ATOMISP_PCI_DEVICE_SOC_MRFLD) {
		u32 csi_afe_trim;

		/*
		 * Workaround for imbalance data eye issue which is observed
		 * on TNG B0.
		 */
		pci_read_config_dword(dev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
				      &csi_afe_trim);
		csi_afe_trim &= ~((MRFLD_PCI_CSI_HSRXCLKTRIM_MASK <<
					MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT) |
				  (MRFLD_PCI_CSI_HSRXCLKTRIM_MASK <<
					MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT) |
				  (MRFLD_PCI_CSI_HSRXCLKTRIM_MASK <<
					MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT));
		csi_afe_trim |= (MRFLD_PCI_CSI1_HSRXCLKTRIM <<
					MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT) |
				(MRFLD_PCI_CSI2_HSRXCLKTRIM <<
					MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT) |
				(MRFLD_PCI_CSI3_HSRXCLKTRIM <<
					MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT);
		pci_write_config_dword(dev, MRFLD_PCI_CSI_AFE_TRIM_CONTROL,
				      csi_afe_trim);
	}

	err = atomisp_initialize_modules(isp);
	if (err < 0) {
		dev_err(&dev->dev, "atomisp_initialize_modules (%d)\n", err);
		goto initialize_modules_fail;
	}

	err = atomisp_register_entities(isp);
	if (err < 0) {
		dev_err(&dev->dev, "atomisp_register_entities failed (%d)\n",
			err);
		goto register_entities_fail;
	}
	err = atomisp_create_pads_links(isp);
	if (err < 0)
		goto register_entities_fail;
	/* init atomisp wdts */
	if (init_atomisp_wdts(isp) != 0)
		goto wdt_work_queue_fail;

	/* save the iunit context only once after all the values are init'ed. */
	atomisp_save_iunit_reg(isp);

	pm_runtime_put_noidle(&dev->dev);
	pm_runtime_allow(&dev->dev);

	hmm_init_mem_stat(repool_pgnr, dypool_enable, dypool_pgnr);
	err = hmm_pool_register(repool_pgnr, HMM_POOL_TYPE_RESERVED);
	if (err) {
		dev_err(&dev->dev, "Failed to register reserved memory pool.\n");
		goto hmm_pool_fail;
	}

	/* Init ISP memory management */
	hmm_init();

	err = devm_request_threaded_irq(&dev->dev, dev->irq,
					atomisp_isr, atomisp_isr_thread,
					IRQF_SHARED, "isp_irq", isp);
	if (err) {
		dev_err(&dev->dev, "Failed to request irq (%d)\n", err);
		goto request_irq_fail;
	}

	/* Load firmware into ISP memory */
	if (!defer_fw_load) {
		err = atomisp_css_load_firmware(isp);
		if (err) {
			dev_err(&dev->dev, "Failed to init css.\n");
			goto css_init_fail;
		}
	} else {
		dev_dbg(&dev->dev, "Skip css init.\n");
	}
	/* Clear FW image from memory */
	release_firmware(isp->firmware);
	isp->firmware = NULL;
	isp->css_env.isp_css_fw.data = NULL;

	atomisp_drvfs_init(&atomisp_pci_driver, isp);

	return 0;

css_init_fail:
	devm_free_irq(&dev->dev, dev->irq, isp);
request_irq_fail:
	hmm_cleanup();
	hmm_pool_unregister(HMM_POOL_TYPE_RESERVED);
hmm_pool_fail:
	destroy_workqueue(isp->wdt_work_queue);
wdt_work_queue_fail:
	atomisp_acc_cleanup(isp);
	atomisp_unregister_entities(isp);
register_entities_fail:
	atomisp_uninitialize_modules(isp);
initialize_modules_fail:
	pm_qos_remove_request(&isp->pm_qos);
	atomisp_msi_irq_uninit(isp, dev);
enable_msi_fail:
fw_validation_fail:
	release_firmware(isp->firmware);
load_fw_fail:
	/*
	 * Switch off ISP, as keeping it powered on would prevent
	 * reaching S0ix states.
	 *
	 * The following lines have been copied from atomisp suspend path
	 */

	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
	irq = irq & 1 << INTR_IIR;
	pci_write_config_dword(dev, PCI_INTERRUPT_CTRL, irq);

	pci_read_config_dword(dev, PCI_INTERRUPT_CTRL, &irq);
	irq &= ~(1 << INTR_IER);
	pci_write_config_dword(dev, PCI_INTERRUPT_CTRL, irq);

	atomisp_msi_irq_uninit(isp, dev);

	atomisp_ospm_dphy_down(isp);

	/* Address later when we worry about the ...field chips */
	if (IS_ENABLED(CONFIG_PM) && atomisp_mrfld_power_down(isp))
		dev_err(&dev->dev, "Failed to switch off ISP\n");
	pci_dev_put(isp->pci_root);
	return err;
}

static void atomisp_pci_remove(struct pci_dev *dev)
{
	struct atomisp_device *isp = (struct atomisp_device *)
		pci_get_drvdata(dev);

	atomisp_drvfs_exit();

	atomisp_acc_cleanup(isp);

	atomisp_css_unload_firmware(isp);
	hmm_cleanup();

	pm_runtime_forbid(&dev->dev);
	pm_runtime_get_noresume(&dev->dev);
	pm_qos_remove_request(&isp->pm_qos);

	atomisp_msi_irq_uninit(isp, dev);
	pci_dev_put(isp->pci_root);

	atomisp_unregister_entities(isp);

	destroy_workqueue(isp->wdt_work_queue);
	atomisp_file_input_cleanup(isp);

	release_firmware(isp->firmware);

	hmm_pool_unregister(HMM_POOL_TYPE_RESERVED);
}

static const struct pci_device_id atomisp_pci_tbl[] = {
#if defined(ISP2400) || defined(ISP2400B0)
	/* Merrifield */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x1178)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x1179)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x117a)},
	/* Baytrail */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0f38)},
#elif defined(ISP2401)
	/* Anniedale (Merrifield+ / Moorefield) */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x1478)},
	/* Cherrytrail */
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x22b8)},
#endif
	{0,}
};

MODULE_DEVICE_TABLE(pci, atomisp_pci_tbl);

static const struct dev_pm_ops atomisp_pm_ops = {
	.runtime_suspend = atomisp_runtime_suspend,
	.runtime_resume = atomisp_runtime_resume,
	.suspend = atomisp_suspend,
	.resume = atomisp_resume,
};

static struct pci_driver atomisp_pci_driver = {
	.driver = {
		.pm = &atomisp_pm_ops,
	},
	.name = "atomisp-isp2",
	.id_table = atomisp_pci_tbl,
	.probe = atomisp_pci_probe,
	.remove = atomisp_pci_remove,
};

static int __init atomisp_init(void)
{
	return pci_register_driver(&atomisp_pci_driver);
}

static void __exit atomisp_exit(void)
{
	pci_unregister_driver(&atomisp_pci_driver);
}

module_init(atomisp_init);
module_exit(atomisp_exit);

MODULE_AUTHOR("Wen Wang <wen.w.wang@intel.com>");
MODULE_AUTHOR("Xiaolin Zhang <xiaolin.zhang@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel ATOM Platform ISP Driver");
