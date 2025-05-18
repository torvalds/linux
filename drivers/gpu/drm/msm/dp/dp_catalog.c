// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/rational.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_print.h>

#include "dp_catalog.h"
#include "dp_reg.h"

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define DP_INTERRUPT_STATUS_ACK_SHIFT	1
#define DP_INTERRUPT_STATUS_MASK_SHIFT	2

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_XFER_DONE| \
	DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | \
	DP_INTR_NACK_DEFER | DP_INTR_WRONG_DATA_CNT | \
	DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER | \
	DP_INTR_PLL_UNLOCKED | DP_INTR_AUX_ERROR)

#define DP_INTERRUPT_STATUS1_ACK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS1_MASK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_MASK_SHIFT)

#define DP_INTERRUPT_STATUS2 \
	(DP_INTR_READY_FOR_VIDEO | DP_INTR_IDLE_PATTERN_SENT | \
	DP_INTR_FRAME_END | DP_INTR_CRC_UPDATED)

#define DP_INTERRUPT_STATUS2_ACK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS2_MASK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_MASK_SHIFT)

#define DP_INTERRUPT_STATUS4 \
	(PSR_UPDATE_INT | PSR_CAPTURE_INT | PSR_EXIT_INT | \
	PSR_UPDATE_ERROR_INT | PSR_WAKE_ERROR_INT)

#define DP_INTERRUPT_MASK4 \
	(PSR_UPDATE_MASK | PSR_CAPTURE_MASK | PSR_EXIT_MASK | \
	PSR_UPDATE_ERROR_MASK | PSR_WAKE_ERROR_MASK)

#define DP_DEFAULT_AHB_OFFSET	0x0000
#define DP_DEFAULT_AHB_SIZE	0x0200
#define DP_DEFAULT_AUX_OFFSET	0x0200
#define DP_DEFAULT_AUX_SIZE	0x0200
#define DP_DEFAULT_LINK_OFFSET	0x0400
#define DP_DEFAULT_LINK_SIZE	0x0C00
#define DP_DEFAULT_P0_OFFSET	0x1000
#define DP_DEFAULT_P0_SIZE	0x0400

struct msm_dp_catalog_private {
	struct device *dev;
	struct drm_device *drm_dev;
	struct msm_dp_catalog msm_dp_catalog;
};

void msm_dp_catalog_snapshot(struct msm_dp_catalog *msm_dp_catalog, struct msm_disp_state *disp_state)
{
	msm_disp_snapshot_add_block(disp_state,
				    msm_dp_catalog->ahb_len, msm_dp_catalog->ahb_base, "dp_ahb");
	msm_disp_snapshot_add_block(disp_state,
				    msm_dp_catalog->aux_len, msm_dp_catalog->aux_base, "dp_aux");
	msm_disp_snapshot_add_block(disp_state,
				    msm_dp_catalog->link_len, msm_dp_catalog->link_base, "dp_link");
	msm_disp_snapshot_add_block(disp_state,
				    msm_dp_catalog->p0_len, msm_dp_catalog->p0_base, "dp_p0");
}

u32 msm_dp_catalog_aux_get_irq(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(msm_dp_catalog, REG_DP_INTR_STATUS);
	intr &= ~DP_INTERRUPT_STATUS1_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS1)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS,
		     intr_ack | DP_INTERRUPT_STATUS1_MASK);

	return intr;

}

void msm_dp_catalog_ctrl_enable_irq(struct msm_dp_catalog *msm_dp_catalog,
						bool enable)
{
	if (enable) {
		msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS,
				DP_INTERRUPT_STATUS1_MASK);
		msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS2,
				DP_INTERRUPT_STATUS2_MASK);
	} else {
		msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS, 0x00);
		msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS2, 0x00);
	}
}

u32 msm_dp_catalog_ctrl_read_psr_interrupt_status(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(msm_dp_catalog, REG_DP_INTR_STATUS4);
	intr_ack = (intr & DP_INTERRUPT_STATUS4)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS4, intr_ack);

	return intr;
}

void msm_dp_catalog_ctrl_config_psr_interrupt(struct msm_dp_catalog *msm_dp_catalog)
{
	msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_MASK4, DP_INTERRUPT_MASK4);
}

int msm_dp_catalog_ctrl_get_interrupt(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(msm_dp_catalog, REG_DP_INTR_STATUS2);
	intr &= ~DP_INTERRUPT_STATUS2_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS2)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(msm_dp_catalog, REG_DP_INTR_STATUS2,
			intr_ack | DP_INTERRUPT_STATUS2_MASK);

	return intr;
}

static void __iomem *msm_dp_ioremap(struct platform_device *pdev, int idx, size_t *len)
{
	struct resource *res;
	void __iomem *base;

	base = devm_platform_get_and_ioremap_resource(pdev, idx, &res);
	if (!IS_ERR(base))
		*len = resource_size(res);

	return base;
}

static int msm_dp_catalog_get_io(struct msm_dp_catalog_private *catalog)
{
	struct msm_dp_catalog *msm_dp_catalog = &catalog->msm_dp_catalog;
	struct platform_device *pdev = to_platform_device(catalog->dev);

	msm_dp_catalog->ahb_base = msm_dp_ioremap(pdev, 0, &msm_dp_catalog->ahb_len);
	if (IS_ERR(msm_dp_catalog->ahb_base))
		return PTR_ERR(msm_dp_catalog->ahb_base);

	msm_dp_catalog->aux_base = msm_dp_ioremap(pdev, 1, &msm_dp_catalog->aux_len);
	if (IS_ERR(msm_dp_catalog->aux_base)) {
		/*
		 * The initial binding had a single reg, but in order to
		 * support variation in the sub-region sizes this was split.
		 * msm_dp_ioremap() will fail with -EINVAL here if only a single
		 * reg is specified, so fill in the sub-region offsets and
		 * lengths based on this single region.
		 */
		if (PTR_ERR(msm_dp_catalog->aux_base) == -EINVAL) {
			if (msm_dp_catalog->ahb_len < DP_DEFAULT_P0_OFFSET + DP_DEFAULT_P0_SIZE) {
				DRM_ERROR("legacy memory region not large enough\n");
				return -EINVAL;
			}

			msm_dp_catalog->ahb_len = DP_DEFAULT_AHB_SIZE;
			msm_dp_catalog->aux_base = msm_dp_catalog->ahb_base + DP_DEFAULT_AUX_OFFSET;
			msm_dp_catalog->aux_len = DP_DEFAULT_AUX_SIZE;
			msm_dp_catalog->link_base = msm_dp_catalog->ahb_base +
				DP_DEFAULT_LINK_OFFSET;
			msm_dp_catalog->link_len = DP_DEFAULT_LINK_SIZE;
			msm_dp_catalog->p0_base = msm_dp_catalog->ahb_base + DP_DEFAULT_P0_OFFSET;
			msm_dp_catalog->p0_len = DP_DEFAULT_P0_SIZE;
		} else {
			DRM_ERROR("unable to remap aux region: %pe\n", msm_dp_catalog->aux_base);
			return PTR_ERR(msm_dp_catalog->aux_base);
		}
	} else {
		msm_dp_catalog->link_base = msm_dp_ioremap(pdev, 2, &msm_dp_catalog->link_len);
		if (IS_ERR(msm_dp_catalog->link_base)) {
			DRM_ERROR("unable to remap link region: %pe\n", msm_dp_catalog->link_base);
			return PTR_ERR(msm_dp_catalog->link_base);
		}

		msm_dp_catalog->p0_base = msm_dp_ioremap(pdev, 3, &msm_dp_catalog->p0_len);
		if (IS_ERR(msm_dp_catalog->p0_base)) {
			DRM_ERROR("unable to remap p0 region: %pe\n", msm_dp_catalog->p0_base);
			return PTR_ERR(msm_dp_catalog->p0_base);
		}
	}

	return 0;
}

struct msm_dp_catalog *msm_dp_catalog_get(struct device *dev)
{
	struct msm_dp_catalog_private *catalog;
	int ret;

	catalog  = devm_kzalloc(dev, sizeof(*catalog), GFP_KERNEL);
	if (!catalog)
		return ERR_PTR(-ENOMEM);

	catalog->dev = dev;

	ret = msm_dp_catalog_get_io(catalog);
	if (ret)
		return ERR_PTR(ret);

	return &catalog->msm_dp_catalog;
}
