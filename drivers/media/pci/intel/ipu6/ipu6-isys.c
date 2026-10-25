// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <media/ipu-bridge.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "ipu6-bus.h"
#include "ipu6-cpd.h"
#include "ipu6-dma.h"
#include "ipu6-isys.h"
#include "ipu6-isys-csi2.h"
#include "ipu6-mmu.h"
#include "ipu6-platform-buttress-regs.h"
#include "ipu6-platform-isys-csi2-reg.h"
#include "ipu6-platform-regs.h"
#include "ipu7-isys-csi-phy.h"
#include "ipu7-isys-csi2-regs.h"
#include "ipu7-platform-regs.h"

#define IPU6_BUTTRESS_FABIC_CONTROL		0x68
#define GDA_ENABLE_IWAKE_INDEX			2
#define GDA_IWAKE_THRESHOLD_INDEX		1
#define GDA_IRQ_CRITICAL_THRESHOLD_INDEX	0
#define GDA_MEMOPEN_THRESHOLD_INDEX		3
#define DEFAULT_DID_RATIO			90
#define DEFAULT_IWAKE_THRESHOLD			0x42
#define DEFAULT_MEM_OPEN_TIME			10
#define ONE_THOUSAND_MICROSECOND		1000
/* One page is 2KB, 8 x 16 x 16 = 2048B = 2KB */
#define ISF_DMA_TOP_GDA_PROFERTY_PAGE_SIZE	0x800

/* LTR & DID value are 10 bit at most */
#define LTR_DID_VAL_MAX				1023
#define LTR_DEFAULT_VALUE			0x70503c19
#define FILL_TIME_DEFAULT_VALUE			0xfff0783c
#define LTR_DID_PKGC_2R				20
#define LTR_SCALE_DEFAULT			5
#define LTR_SCALE_1024NS			2
#define DID_SCALE_1US				2
#define DID_SCALE_32US				3
#define REG_PKGC_PMON_CFG			0xb00

#define VAL_PKGC_PMON_CFG_RESET			0x38
#define VAL_PKGC_PMON_CFG_START			0x7

#define IS_PIXEL_BUFFER_PAGES			0x80
/*
 * when iwake mode is disabled, the critical threshold is statically set
 * to 75% of the IS pixel buffer, criticalThreshold = (128 * 3) / 4
 */
#define CRITICAL_THRESHOLD_IWAKE_DISABLE	(IS_PIXEL_BUFFER_PAGES * 3 / 4)

union fabric_ctrl {
	struct {
		u16 ltr_val   : 10;
		u16 ltr_scale : 3;
		u16 reserved  : 3;
		u16 did_val   : 10;
		u16 did_scale : 3;
		u16 reserved2 : 1;
		u16 keep_power_in_D0   : 1;
		u16 keep_power_override : 1;
	} bits;
	u32 value;
};

enum ltr_did_type {
	LTR_IWAKE_ON,
	LTR_IWAKE_OFF,
	LTR_ISYS_ON,
	LTR_ISYS_OFF,
	LTR_ENHANNCE_IWAKE,
	LTR_TYPE_MAX
};

struct ltr_did {
	union {
		u32 value;
		struct {
			u8 val0;
			u8 val1;
			u8 val2;
			u8 val3;
		} bits;
	} lut_ltr;
	union {
		u32 value;
		struct {
			u8 th0;
			u8 th1;
			u8 th2;
			u8 th3;
		} bits;
	} lut_fill_time;
};

#define ISYS_PM_QOS_VALUE	300

static int
isys_complete_ext_device_registration(struct ipu6_isys *isys,
				      struct v4l2_subdev *sd,
				      struct ipu6_isys_csi2_config *csi2)
{
	struct device *dev = &isys->adev->auxdev.dev;
	unsigned int i;
	int ret;

	for (i = 0; i < sd->entity.num_pads; i++) {
		if (sd->entity.pads[i].flags & MEDIA_PAD_FL_SOURCE)
			break;
	}

	if (i == sd->entity.num_pads) {
		dev_warn(dev, "no src pad in external entity\n");
		ret = -ENOENT;
		goto unregister_subdev;
	}

	ret = media_create_pad_link(&sd->entity, i,
				    &isys->csi2[csi2->port].asd.sd.entity,
				    0, MEDIA_LNK_FL_ENABLED |
				       MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_warn(dev, "can't create link\n");
		goto unregister_subdev;
	}

	isys->csi2[csi2->port].nlanes = csi2->nlanes;
	isys->csi2[csi2->port].phy_mode =
		csi2->bus_type == V4L2_MBUS_CSI2_DPHY ?
		PHY_MODE_DPHY : PHY_MODE_CPHY;

	return 0;

unregister_subdev:
	v4l2_device_unregister_subdev(sd);

	return ret;
}

static void isys_csi2_unregister_subdevices(struct ipu6_isys *isys)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2 =
		&isys->pdata->ipdata->csi2;
	unsigned int i;

	for (i = 0; i < csi2->nports; i++)
		ipu6_isys_csi2_cleanup(&isys->csi2[i]);
}

static int isys_csi2_register_subdevices(struct ipu6_isys *isys)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		void __iomem *base = isys->pdata->base;

		if (IS_IPU7(isys->adev->isp) || IS_IPU8(isys->adev->isp)) {
			u32 mask = IS_IPU7_MTL(isys->adev->isp) ?
					IPU7_CSI_LEGACY_IRQ_MASK(i) :
					IPU7P5_CSI_LEGACY_IRQ_MASK(i);

			isys->isr_csi2_bits |= mask;
			isys->csi2[i].legacy_irq_mask = mask;
		} else {
			base += CSI_REG_PORT_BASE(i);
			isys->isr_csi2_bits |= IPU6_ISYS_UNISPART_IRQ_CSI2(i);
		}

		ret = ipu6_isys_csi2_init(&isys->csi2[i], isys, base, i);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	while (i--)
		ipu6_isys_csi2_cleanup(&isys->csi2[i]);

	return ret;
}

static int isys_csi2_create_media_links(struct ipu6_isys *isys)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	struct device *dev = &isys->adev->auxdev.dev;
	unsigned int i, j;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		struct media_entity *sd = &isys->csi2[i].asd.sd.entity;

		for (j = 0; j < NR_OF_CSI2_SRC_PADS; j++) {
			struct ipu6_isys_video *av = &isys->csi2[i].av[j];

			ret = media_create_pad_link(sd, CSI2_PAD_SRC + j,
						    &av->vdev.entity, 0, 0);
			if (ret) {
				dev_err(dev, "CSI2 can't create link\n");
				return ret;
			}

			av->csi2 = &isys->csi2[i];
		}
	}

	return 0;
}

static void isys_unregister_video_devices(struct ipu6_isys *isys)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i, j;

	for (i = 0; i < csi2_pdata->nports; i++)
		for (j = 0; j < NR_OF_CSI2_SRC_PADS; j++)
			ipu6_isys_video_cleanup(&isys->csi2[i].av[j]);
}

static int isys_register_video_devices(struct ipu6_isys *isys)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i, j;
	int ret;

	for (i = 0; i < csi2_pdata->nports; i++) {
		for (j = 0; j < NR_OF_CSI2_SRC_PADS; j++) {
			struct ipu6_isys_video *av = &isys->csi2[i].av[j];

			snprintf(av->vdev.name, sizeof(av->vdev.name),
				 "Intel IPU%s ISYS Capture %u",
				 ipu6_isys_entity_version(isys->adev->isp),
				 i * NR_OF_CSI2_SRC_PADS + j);
			av->isys = isys;
			av->aq.vbq.buf_struct_size =
				sizeof(struct ipu6_isys_video_buffer);

			ret = ipu6_isys_video_init(av);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	while (i--) {
		while (j--)
			ipu6_isys_video_cleanup(&isys->csi2[i].av[j]);
		j = NR_OF_CSI2_SRC_PADS;
	}

	return ret;
}

static void ipu7_isys_setup_hw(struct ipu6_isys *isys)
{
	u32 offset, mask;
	void __iomem *base = isys->pdata->base;

	offset = IPU7_IS_IO_GPREGS_BASE;

	writel(0x0, base + offset + IPU7_CLK_EN_TXCLKESC);
	/* Update if ISYS freq updated (0: 400/1, 1:400/2, 63:400/64) */
	writel(0x0, base + offset + IPU7_CLK_DIV_FACTOR_IS_CLK);
	/* correct the initial printf configuration */
	writel(0x200, base + IPU7_IS_UC_CTRL_BASE + IPU7_REG_PRINTF_AXI_CNTL);

	offset = IPU7_IS_UC_CTRL_BASE;
	mask = IPU7_IS_UC_TO_SW_IRQ_MASK;

	writel(mask, base + offset + IPU7_TO_SW_IRQ_CNTL_CLEAR);
	writel(mask, base + offset + IPU7_TO_SW_IRQ_CNTL_MASK_N);
	writel(mask, base + offset + IPU7_TO_SW_IRQ_CNTL_ENABLE);

	offset = IPU7_IS_IO_CSI2_LEGACY_IRQ_CTRL_BASE;
	mask = IPU7_CSI_RX_LEGACY_IRQ_MASK;

	writel(mask, base + offset + IPU7_IRQ_CTL_EDGE);
	writel(mask, base + offset + IPU7_IRQ_CTL_CLEAR);
	writel(mask, base + offset + IPU7_IRQ_CTL_MASK);
	writel(mask, base + offset + IPU7_IRQ_CTL_ENABLE);
}

static void ipu6_isys_setup_hw(struct ipu6_isys *isys)
{
	void __iomem *base = isys->pdata->base;
	const u8 *thd = isys->pdata->ipdata->hw_variant.cdc_fifo_threshold;
	u32 irqs = 0;
	unsigned int i, nports;

	nports = isys->pdata->ipdata->csi2.nports;

	/* Enable irqs for all MIPI ports */
	for (i = 0; i < nports; i++)
		irqs |= IPU6_ISYS_UNISPART_IRQ_CSI2(i);

	writel(irqs, base + isys->pdata->ipdata->csi2.ctrl0_irq_edge);
	writel(irqs, base + isys->pdata->ipdata->csi2.ctrl0_irq_lnp);
	writel(irqs, base + isys->pdata->ipdata->csi2.ctrl0_irq_mask);
	writel(irqs, base + isys->pdata->ipdata->csi2.ctrl0_irq_enable);
	writel(GENMASK(19, 0),
	       base + isys->pdata->ipdata->csi2.ctrl0_irq_clear);

	irqs = ISYS_UNISPART_IRQS;
	writel(irqs, base + IPU6_REG_ISYS_UNISPART_IRQ_EDGE);
	writel(irqs, base + IPU6_REG_ISYS_UNISPART_IRQ_LEVEL_NOT_PULSE);
	writel(GENMASK(28, 0), base + IPU6_REG_ISYS_UNISPART_IRQ_CLEAR);
	writel(irqs, base + IPU6_REG_ISYS_UNISPART_IRQ_MASK);
	writel(irqs, base + IPU6_REG_ISYS_UNISPART_IRQ_ENABLE);

	writel(0, base + IPU6_REG_ISYS_UNISPART_SW_IRQ_REG);
	writel(0, base + IPU6_REG_ISYS_UNISPART_SW_IRQ_MUX_REG);

	/* Write CDC FIFO threshold values for isys */
	for (i = 0; i < isys->pdata->ipdata->hw_variant.cdc_fifos; i++)
		writel(thd[i], base + IPU6_REG_ISYS_CDC_THRESHOLD(i));
}

static int set_iwake_register(struct ipu6_isys *isys, u32 index, u32 value)
{
	struct device *dev = &isys->adev->auxdev.dev;
	u32 req_id = index;
	u32 offset = 0;
	int ret;

	ret = ipu6_fw_isys_send_proxy_token(isys, req_id, index, offset, value);
	if (ret)
		dev_err(dev, "write %d failed %d", index, ret);

	return ret;
}

/*
 * When input system is powered up and before enabling any new sensor capture,
 * or after disabling any sensor capture the following values need to be set:
 * LTR_value = LTR(usec) from calculation;
 * LTR_scale = 2;
 * DID_value = DID(usec) from calculation;
 * DID_scale = 2;
 *
 * When input system is powered down, the LTR and DID values
 * must be returned to the default values:
 * LTR_value = 1023;
 * LTR_scale = 5;
 * DID_value = 1023;
 * DID_scale = 2;
 */
static void set_iwake_ltrdid(struct ipu6_isys *isys, u16 ltr, u16 did,
			     enum ltr_did_type use)
{
	struct device *dev = &isys->adev->auxdev.dev;
	u16 ltr_val, ltr_scale = LTR_SCALE_1024NS;
	u16 did_val, did_scale = DID_SCALE_1US;
	struct ipu6_device *isp = isys->adev->isp;
	union fabric_ctrl fc;

	switch (use) {
	case LTR_IWAKE_ON:
		ltr_val = min_t(u16, ltr, (u16)LTR_DID_VAL_MAX);
		did_val = min_t(u16, did, (u16)LTR_DID_VAL_MAX);
		ltr_scale = (ltr == LTR_DID_VAL_MAX &&
			     did == LTR_DID_VAL_MAX) ?
			LTR_SCALE_DEFAULT : LTR_SCALE_1024NS;
		break;
	case LTR_ISYS_ON:
	case LTR_IWAKE_OFF:
		ltr_val = LTR_DID_PKGC_2R;
		did_val = LTR_DID_PKGC_2R;
		break;
	case LTR_ISYS_OFF:
		ltr_val   = LTR_DID_VAL_MAX;
		did_val   = LTR_DID_VAL_MAX;
		ltr_scale = LTR_SCALE_DEFAULT;
		break;
	case LTR_ENHANNCE_IWAKE:
		if (ltr == LTR_DID_VAL_MAX && did == LTR_DID_VAL_MAX) {
			ltr_val = LTR_DID_VAL_MAX;
			did_val = LTR_DID_VAL_MAX;
			ltr_scale = LTR_SCALE_DEFAULT;
		} else if (did < ONE_THOUSAND_MICROSECOND) {
			ltr_val = ltr;
			did_val = did;
		} else {
			ltr_val = ltr;
			/* div 90% value by 32 to account for scale change */
			did_val = did / 32;
			did_scale = DID_SCALE_32US;
		}
		break;
	default:
		ltr_val   = LTR_DID_VAL_MAX;
		did_val   = LTR_DID_VAL_MAX;
		ltr_scale = LTR_SCALE_DEFAULT;
		break;
	}

	fc.value = readl(isp->base + IPU6_BUTTRESS_FABIC_CONTROL);
	fc.bits.ltr_val = ltr_val;
	fc.bits.ltr_scale = ltr_scale;
	fc.bits.did_val = did_val;
	fc.bits.did_scale = did_scale;

	dev_dbg(dev, "ltr: value %u scale %u, did: value %u scale %u\n",
		ltr_val, ltr_scale, did_val, did_scale);
	writel(fc.value, isp->base + IPU6_BUTTRESS_FABIC_CONTROL);
}

/*
 * Driver may clear register GDA_ENABLE_IWAKE before FW configures the
 * stream for debug purpose. Otherwise driver should not access this register.
 */
static void enable_iwake(struct ipu6_isys *isys, bool enable)
{
	int ret;

	if (isys->iwake_watermark_enabled == enable)
		return;

	ret = set_iwake_register(isys, GDA_ENABLE_IWAKE_INDEX, enable);
	if (!ret)
		isys->iwake_watermark_enabled = enable;
}

void ipu6_isys_update_watermark_setting(struct ipu6_isys *isys)
{
	u32 iwake_threshold, iwake_critical_threshold, page_num;
	struct device *dev = &isys->adev->auxdev.dev;
	u32 calc_fill_time_us = 0, ltr = 0, did = 0;
	enum ltr_did_type ltr_did_type;
	u64 isys_pb_datarate_mbs = 0;
	u32 mem_open_threshold = 0;
	struct ltr_did ltrdid;
	u64 threshold_bytes;
	u32 max_sram_size;
	u32 shift;
	bool force_iwake_disable = false;

	lockdep_assert_held(&isys->stream_mutex);

	shift = isys->pdata->ipdata->sram_gran_shift;
	max_sram_size = isys->pdata->ipdata->max_sram_size;

	for (unsigned int i = 0; i < isys->pdata->ipdata->csi2.nports; i++) {
		isys_pb_datarate_mbs +=
			isys->csi2[i].watermark.stream_data_rate;
		force_iwake_disable |=
			isys->csi2[i].watermark.force_iwake_disable;
	}

	if (force_iwake_disable) {
		dev_dbg(dev, "watermark: forcing iwake disabled\n");
		set_iwake_ltrdid(isys, 0, 0, LTR_IWAKE_OFF);
		set_iwake_register(isys, GDA_IRQ_CRITICAL_THRESHOLD_INDEX,
				   CRITICAL_THRESHOLD_IWAKE_DISABLE);
		return;
	}

	if (!isys_pb_datarate_mbs) {
		dev_dbg(dev, "watermark: disabled iwake\n");
		enable_iwake(isys, false);
		set_iwake_ltrdid(isys, 0, 0, LTR_IWAKE_OFF);
		set_iwake_register(isys, GDA_IRQ_CRITICAL_THRESHOLD_INDEX,
				   CRITICAL_THRESHOLD_IWAKE_DISABLE);
		return;
	}

	enable_iwake(isys, true);
	calc_fill_time_us = div64_u64(max_sram_size, isys_pb_datarate_mbs);

	if (isys->pdata->ipdata->enhanced_iwake) {
		ltr = isys->pdata->ipdata->ltr;
		did = calc_fill_time_us * DEFAULT_DID_RATIO / 100;
		ltr_did_type = LTR_ENHANNCE_IWAKE;
	} else {
		ltrdid.lut_ltr.value = LTR_DEFAULT_VALUE;
		ltrdid.lut_fill_time.value = FILL_TIME_DEFAULT_VALUE;

		if (calc_fill_time_us <= ltrdid.lut_fill_time.bits.th0)
			ltr = 0;
		else if (calc_fill_time_us <= ltrdid.lut_fill_time.bits.th1)
			ltr = ltrdid.lut_ltr.bits.val0;
		else if (calc_fill_time_us <= ltrdid.lut_fill_time.bits.th2)
			ltr = ltrdid.lut_ltr.bits.val1;
		else if (calc_fill_time_us <= ltrdid.lut_fill_time.bits.th3)
			ltr = ltrdid.lut_ltr.bits.val2;
		else
			ltr = ltrdid.lut_ltr.bits.val3;

		did = calc_fill_time_us - ltr;
		ltr_did_type = LTR_IWAKE_ON;
	}

	set_iwake_ltrdid(isys, ltr, did, ltr_did_type);

	/* calculate iwake threshold with 2KB granularity pages */
	threshold_bytes = did * isys_pb_datarate_mbs;
	iwake_threshold = max_t(u32, 1, threshold_bytes >> shift);
	iwake_threshold = min_t(u32, iwake_threshold, max_sram_size);

	if (isys->pdata->ipdata->enhanced_iwake) {
		set_iwake_register(isys, GDA_IWAKE_THRESHOLD_INDEX,
				   DEFAULT_IWAKE_THRESHOLD);
		/* calculate number of pages that will be filled in 10 usec */
		page_num = (DEFAULT_MEM_OPEN_TIME * isys_pb_datarate_mbs) /
			ISF_DMA_TOP_GDA_PROFERTY_PAGE_SIZE;
		page_num += ((DEFAULT_MEM_OPEN_TIME * isys_pb_datarate_mbs) %
			     ISF_DMA_TOP_GDA_PROFERTY_PAGE_SIZE) ? 1 : 0;
		mem_open_threshold = isys->pdata->ipdata->memopen_threshold;
		mem_open_threshold = max_t(u32, mem_open_threshold, page_num);
		dev_dbg(dev, "mem_open_threshold: %u\n", mem_open_threshold);
		set_iwake_register(isys, GDA_MEMOPEN_THRESHOLD_INDEX,
				   mem_open_threshold);
	} else {
		set_iwake_register(isys, GDA_IWAKE_THRESHOLD_INDEX,
				   iwake_threshold);
	}

	iwake_critical_threshold = iwake_threshold +
		(IS_PIXEL_BUFFER_PAGES - iwake_threshold) / 2;

	dev_dbg(dev, "watermark: threshold: %u critical: %u\n", iwake_threshold,
		iwake_critical_threshold);

	set_iwake_register(isys, GDA_IRQ_CRITICAL_THRESHOLD_INDEX,
			   iwake_critical_threshold);

	writel(VAL_PKGC_PMON_CFG_RESET,
	       isys->adev->isp->base + REG_PKGC_PMON_CFG);
	writel(VAL_PKGC_PMON_CFG_START,
	       isys->adev->isp->base + REG_PKGC_PMON_CFG);
}

/* The .bound() notifier callback when a match is found */
static int isys_notifier_bound(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd,
			       struct v4l2_async_connection *asc)
{
	struct ipu6_isys *isys =
		container_of(notifier, struct ipu6_isys, notifier);
	struct sensor_async_sd *s_asd =
		container_of(asc, struct sensor_async_sd, asc);
	int ret;

	if (s_asd->csi2.port >= isys->pdata->ipdata->csi2.nports) {
		dev_err(&isys->adev->auxdev.dev, "invalid csi2 port %u\n",
			s_asd->csi2.port);
		return -EINVAL;
	}

	ret = ipu_bridge_instantiate_vcm(sd->dev);
	if (ret) {
		dev_err(&isys->adev->auxdev.dev, "instantiate vcm failed\n");
		return ret;
	}

	dev_dbg(&isys->adev->auxdev.dev, "bind %s nlanes is %d port is %d\n",
		sd->name, s_asd->csi2.nlanes, s_asd->csi2.port);
	ret = isys_complete_ext_device_registration(isys, sd, &s_asd->csi2);
	if (ret)
		return ret;

	return v4l2_device_register_subdev_nodes(&isys->v4l2_dev);
}

static int isys_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct ipu6_isys *isys =
		container_of(notifier, struct ipu6_isys, notifier);

	return v4l2_device_register_subdev_nodes(&isys->v4l2_dev);
}

static const struct v4l2_async_notifier_operations isys_async_ops = {
	.bound = isys_notifier_bound,
	.complete = isys_notifier_complete,
};

#define ISYS_MAX_PORTS 8
static int isys_notifier_init(struct ipu6_isys *isys)
{
	struct ipu6_device *isp = isys->adev->isp;
	struct device *dev = &isp->pdev->dev;
	unsigned int i;
	int ret;

	v4l2_async_nf_init(&isys->notifier, &isys->v4l2_dev);

	for (i = 0; i < ISYS_MAX_PORTS; i++) {
		struct v4l2_fwnode_endpoint vep = {
			.bus_type = V4L2_MBUS_CSI2_DPHY
		};
		struct sensor_async_sd *s_asd;
		struct fwnode_handle *ep;

		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), i, 0,
						FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret && (IS_IPU7(isp) || IS_IPU8(isp))) {
			vep.bus_type = V4L2_MBUS_CSI2_CPHY;
			ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		}
		if (ret) {
			dev_err(dev, "fwnode endpoint parse failed: %d\n", ret);
			goto err_parse;
		}

		s_asd = v4l2_async_nf_add_fwnode_remote(&isys->notifier, ep,
							struct sensor_async_sd);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			dev_err(dev, "add remove fwnode failed: %d\n", ret);
			goto err_parse;
		}

		s_asd->csi2.port = vep.base.port;
		s_asd->csi2.nlanes = vep.bus.mipi_csi2.num_data_lanes;
		s_asd->csi2.bus_type = vep.bus_type;

		dev_dbg(dev, "remote endpoint port %d with %d lanes added\n",
			s_asd->csi2.port, s_asd->csi2.nlanes);

		fwnode_handle_put(ep);

		continue;

err_parse:
		fwnode_handle_put(ep);
		v4l2_async_nf_cleanup(&isys->notifier);
		return ret;
	}

	isys->notifier.ops = &isys_async_ops;
	ret = v4l2_async_nf_register(&isys->notifier);
	if (ret) {
		dev_err(dev, "failed to register async notifier : %d\n", ret);
		v4l2_async_nf_cleanup(&isys->notifier);
	}

	return ret;
}

static void isys_notifier_cleanup(struct ipu6_isys *isys)
{
	v4l2_async_nf_unregister(&isys->notifier);
	v4l2_async_nf_cleanup(&isys->notifier);
}

static int isys_register_devices(struct ipu6_isys *isys)
{
	struct device *dev = &isys->adev->auxdev.dev;
	struct pci_dev *pdev = isys->adev->isp->pdev;
	int ret;

	isys->media_dev.dev = dev;
	media_device_pci_init(&isys->media_dev,
			      pdev, isys->adev->isp->model_name);

	strscpy(isys->v4l2_dev.name, isys->media_dev.model,
		sizeof(isys->v4l2_dev.name));

	ret = media_device_register(&isys->media_dev);
	if (ret < 0)
		goto out_media_device_unregister;

	isys->v4l2_dev.mdev = &isys->media_dev;
	isys->v4l2_dev.ctrl_handler = NULL;

	ret = v4l2_device_register(dev, &isys->v4l2_dev);
	if (ret < 0)
		goto out_media_device_unregister;

	ret = isys_register_video_devices(isys);
	if (ret)
		goto out_v4l2_device_unregister;

	ret = isys_csi2_register_subdevices(isys);
	if (ret)
		goto out_isys_unregister_video_device;

	ret = isys_csi2_create_media_links(isys);
	if (ret)
		goto out_isys_unregister_subdevices;

	ret = isys_notifier_init(isys);
	if (ret)
		goto out_isys_unregister_subdevices;

	return 0;

out_isys_unregister_subdevices:
	isys_csi2_unregister_subdevices(isys);

out_isys_unregister_video_device:
	isys_unregister_video_devices(isys);

out_v4l2_device_unregister:
	v4l2_device_unregister(&isys->v4l2_dev);

out_media_device_unregister:
	media_device_unregister(&isys->media_dev);
	media_device_cleanup(&isys->media_dev);

	dev_err(dev, "failed to register isys devices\n");

	return ret;
}

static void isys_unregister_devices(struct ipu6_isys *isys)
{
	isys_unregister_video_devices(isys);
	isys_csi2_unregister_subdevices(isys);
	v4l2_device_unregister(&isys->v4l2_dev);
	media_device_unregister(&isys->media_dev);
	media_device_cleanup(&isys->media_dev);
}

static int isys_runtime_pm_resume(struct device *dev)
{
	struct ipu6_bus_device *adev = to_ipu6_bus_device(dev);
	const struct ipu6_fw_isys_ops *fw_ops = adev->auxdrv_data->fw_ops;
	struct ipu6_isys *isys = ipu6_bus_get_drvdata(adev);
	const struct ipu6_isys_internal_pdata *ipdata = isys->pdata->ipdata;
	struct ipu6_device *isp = adev->isp;
	int ret;

	ret = ipu6_mmu_hw_init(adev->mmu);
	if (ret)
		return ret;

	cpu_latency_qos_update_request(&isys->pm_qos, ISYS_PM_QOS_VALUE);

	ret = ipu6_buttress_start_tsc_sync(isp);
	if (ret)
		goto err_mmu_hw_cleanup;

	if (IS_IPU7(isp) || IS_IPU8(isp)) {
		ipu7_isys_setup_hw(isys);
	} else {
		ipu6_isys_setup_hw(isys);
		set_iwake_ltrdid(isys, 0, 0, LTR_ISYS_ON);
	}

	ipu6_configure_spc(adev->isp, &ipdata->hw_variant,
			   IPU6_CPD_PKG_DIR_ISYS_SERVER_IDX, isys->pdata->base,
			   adev->pkg_dir, adev->pkg_dir_dma_addr);

	/*
	 * Buffers could have been left to wrong queue at last closure.
	 * Move them now back to empty buffer queue.
	 */
	ipu6_cleanup_fw_msg_bufs(isys);

	if (isys->fwctx) {
		/*
		 * Something went wrong in previous shutdown. As we are now
		 * restarting isys we can safely delete old context.
		 */
		dev_warn(&adev->auxdev.dev, "clearing old context\n");
		fw_ops->cleanup(isys);
	}

	ret = fw_ops->init(isys, ipdata->num_parallel_streams);
	if (!ret)
		return 0;

	isys->phy_termcal_val = 0;
	cpu_latency_qos_update_request(&isys->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (!IS_IPU7(isp) && !IS_IPU8(isp))
		set_iwake_ltrdid(isys, 0, 0, LTR_ISYS_OFF);

err_mmu_hw_cleanup:
	ipu6_mmu_hw_cleanup(adev->mmu);

	return ret;
}

static int isys_runtime_pm_suspend(struct device *dev)
{
	struct ipu6_bus_device *adev = to_ipu6_bus_device(dev);
	struct ipu6_isys *isys = dev_get_drvdata(dev);
	struct ipu6_device *isp = adev->isp;
	int ret = 0;

	isys->adev->auxdrv_data->fw_ops->close(isys);
	if (isys->fwctx) {
		dev_warn(&isys->adev->auxdev.dev, "failed to close fw isys\n");
		ret = -EIO;
	}

	mutex_lock(&isys->mutex);
	isys->need_reset = false;
	mutex_unlock(&isys->mutex);

	isys->phy_termcal_val = 0;
	cpu_latency_qos_update_request(&isys->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (!IS_IPU7(isp) && !IS_IPU8(isp))
		set_iwake_ltrdid(isys, 0, 0, LTR_ISYS_OFF);

	ipu6_mmu_hw_cleanup(adev->mmu);

	return ret;
}

static int isys_suspend(struct device *dev)
{
	struct ipu6_isys *isys = dev_get_drvdata(dev);

	guard(mutex)(&isys->stream_mutex);

	/* If stream is open, refuse to suspend */
	if (!ida_is_empty(&isys->streams))
		return -EBUSY;

	return 0;
}

static int isys_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops isys_pm_ops = {
	.runtime_suspend = isys_runtime_pm_suspend,
	.runtime_resume = isys_runtime_pm_resume,
	.suspend = isys_suspend,
	.resume = isys_resume,
};

static void free_fw_msg_bufs(struct ipu6_isys *isys)
{
	struct isys_fw_msgs *fwmsg, *safe;

	list_for_each_entry_safe(fwmsg, safe, &isys->framebuflist, head)
		ipu6_dma_free(isys->adev, sizeof(struct isys_fw_msgs), fwmsg,
			      fwmsg->dma_addr, 0);

	list_for_each_entry_safe(fwmsg, safe, &isys->framebuflist_fw, head)
		ipu6_dma_free(isys->adev, sizeof(struct isys_fw_msgs), fwmsg,
			      fwmsg->dma_addr, 0);
}

static int alloc_fw_msg_bufs(struct ipu6_isys *isys, int amount)
{
	struct isys_fw_msgs *addr;
	dma_addr_t dma_addr;
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < amount; i++) {
		addr = ipu6_dma_alloc(isys->adev, sizeof(*addr),
				      &dma_addr, GFP_KERNEL, 0);
		if (!addr)
			break;
		addr->dma_addr = dma_addr;

		spin_lock_irqsave(&isys->listlock, flags);
		list_add(&addr->head, &isys->framebuflist);
		spin_unlock_irqrestore(&isys->listlock, flags);
	}

	if (i == amount)
		return 0;

	spin_lock_irqsave(&isys->listlock, flags);
	while (!list_empty(&isys->framebuflist)) {
		addr = list_first_entry(&isys->framebuflist,
					struct isys_fw_msgs, head);
		list_del(&addr->head);
		spin_unlock_irqrestore(&isys->listlock, flags);
		ipu6_dma_free(isys->adev, sizeof(struct isys_fw_msgs), addr,
			      addr->dma_addr, 0);
		spin_lock_irqsave(&isys->listlock, flags);
	}
	spin_unlock_irqrestore(&isys->listlock, flags);

	return -ENOMEM;
}

struct isys_fw_msgs *ipu6_get_fw_msg_buf(struct ipu6_isys_stream *stream)
{
	struct ipu6_isys *isys = stream->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	struct isys_fw_msgs *msg;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&isys->listlock, flags);
	if (list_empty(&isys->framebuflist)) {
		spin_unlock_irqrestore(&isys->listlock, flags);
		dev_dbg(dev, "Frame list empty\n");

		ret = alloc_fw_msg_bufs(isys, 5);
		if (ret < 0)
			return NULL;

		spin_lock_irqsave(&isys->listlock, flags);
		if (list_empty(&isys->framebuflist)) {
			spin_unlock_irqrestore(&isys->listlock, flags);
			dev_err(dev, "Frame list empty\n");
			return NULL;
		}
	}
	msg = list_last_entry(&isys->framebuflist, struct isys_fw_msgs, head);
	list_move(&msg->head, &isys->framebuflist_fw);
	spin_unlock_irqrestore(&isys->listlock, flags);
	memset(&msg->ipu6, 0, sizeof(msg->ipu6));

	return msg;
}

void ipu6_cleanup_fw_msg_bufs(struct ipu6_isys *isys)
{
	struct isys_fw_msgs *fwmsg, *fwmsg0;
	unsigned long flags;

	spin_lock_irqsave(&isys->listlock, flags);
	list_for_each_entry_safe(fwmsg, fwmsg0, &isys->framebuflist_fw, head)
		list_move(&fwmsg->head, &isys->framebuflist);
	spin_unlock_irqrestore(&isys->listlock, flags);
}

void ipu6_put_fw_msg_buf(struct ipu6_isys *isys, struct isys_fw_msgs *msg)
{
	unsigned long flags;

	if (!msg)
		return;

	spin_lock_irqsave(&isys->listlock, flags);
	list_move(&msg->head, &isys->framebuflist);
	spin_unlock_irqrestore(&isys->listlock, flags);
}

static const struct ipu6_auxdrv_data ipu6_isys_auxdrv_data;
static const struct ipu6_auxdrv_data ipu7_isys_auxdrv_data;

static int isys_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *auxdev_id)
{
	const struct ipu6_isys_internal_csi2_pdata *csi2_pdata;
	struct ipu6_bus_device *adev = auxdev_to_adev(auxdev);
	struct ipu6_device *isp = adev->isp;
	struct ipu6_isys *isys;
	int ret;

	if (!isp->bus_ready_to_probe)
		return -EPROBE_DEFER;

	isys = devm_kzalloc(&auxdev->dev, sizeof(*isys), GFP_KERNEL);
	if (!isys)
		return -ENOMEM;

	adev->auxdrv_data = (IS_IPU7(isp) || IS_IPU8(isp)) ? &ipu7_isys_auxdrv_data :
					   &ipu6_isys_auxdrv_data;
	adev->auxdrv = to_auxiliary_drv(auxdev->dev.driver);
	isys->adev = adev;
	isys->pdata = adev->pdata;
	csi2_pdata = &isys->pdata->ipdata->csi2;

	isys->csi2 = devm_kcalloc(&auxdev->dev, csi2_pdata->nports,
				  sizeof(*isys->csi2), GFP_KERNEL);
	if (!isys->csi2)
		return -ENOMEM;

	/* initial sensor type */
	isys->sensor_type = isys->pdata->ipdata->sensor_type_start;

	spin_lock_init(&isys->streams_lock);
	isys->phy_termcal_val = 0;

	mutex_init(&isys->mutex);
	mutex_init(&isys->stream_mutex);

	spin_lock_init(&isys->listlock);
	INIT_LIST_HEAD(&isys->framebuflist);
	INIT_LIST_HEAD(&isys->framebuflist_fw);

	isys->icache_prefetch = 0;

	dev_set_drvdata(&auxdev->dev, isys);

	ida_init(&isys->streams);

	cpu_latency_qos_add_request(&isys->pm_qos, PM_QOS_DEFAULT_VALUE);

	ret = alloc_fw_msg_bufs(isys, 20);
	if (ret < 0)
		goto out_remove_pkg_dir_shared_buffer;

	if (IS_IPU7(adev->isp) || IS_IPU8(adev->isp))
		isys->phy_set_power = ipu7_isys_csi_phy_set_power;
	else if (IS_IPU6SE(adev->isp))
		isys->phy_set_power = ipu6_isys_jsl_phy_set_power;
	else if (IS_IPU6EP_MTL(adev->isp))
		isys->phy_set_power = ipu6_isys_dwc_phy_set_power;
	else
		isys->phy_set_power = ipu6_isys_mcd_phy_set_power;

	ret = isys_register_devices(isys);
	if (ret)
		goto free_fw_msg_bufs;

	return 0;

free_fw_msg_bufs:
	free_fw_msg_bufs(isys);
out_remove_pkg_dir_shared_buffer:
	cpu_latency_qos_remove_request(&isys->pm_qos);

	mutex_destroy(&isys->mutex);
	mutex_destroy(&isys->stream_mutex);

	return ret;
}

static void isys_remove(struct auxiliary_device *auxdev)
{
	struct ipu6_isys *isys = dev_get_drvdata(&auxdev->dev);

	ida_destroy(&isys->streams);

	free_fw_msg_bufs(isys);

	isys_unregister_devices(isys);
	isys_notifier_cleanup(isys);

	cpu_latency_qos_remove_request(&isys->pm_qos);

	mutex_destroy(&isys->stream_mutex);
	mutex_destroy(&isys->mutex);
}

static const struct ipu6_auxdrv_data ipu6_isys_auxdrv_data = {
	.isr = ipu6_isys_isr,
	.isr_threaded = NULL,
	.wake_isr_thread = false,
	.fw_ops = &ipu6_fw_isys_ops,
};

static const struct ipu6_auxdrv_data ipu7_isys_auxdrv_data = {
	.isr = ipu7_isys_isr,
	.isr_threaded = NULL,
	.wake_isr_thread = false,
	.fw_ops = &ipu7_fw_isys_ops,
};

static const struct auxiliary_device_id ipu6_isys_id_table[] = {
	{
		.name = "intel_ipu6.isys",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ipu6_isys_id_table);

static struct auxiliary_driver isys_driver = {
	.name = IPU6_ISYS_NAME,
	.probe = isys_probe,
	.remove = isys_remove,
	.id_table = ipu6_isys_id_table,
	.driver = {
		.pm = &isys_pm_ops,
	},
};

module_auxiliary_driver(isys_driver);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Yunliang Ding <yunliang.ding@intel.com>");
MODULE_AUTHOR("Hongju Wang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel IPU6 input system driver");
MODULE_IMPORT_NS("INTEL_IPU6");
MODULE_IMPORT_NS("INTEL_IPU_BRIDGE");
