// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cobalt driver initialization and card probing
 *
 *  Derived from cx18-driver.c
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#include <linux/delay.h>
#include <media/i2c/adv7604.h>
#include <media/i2c/adv7842.h>
#include <media/i2c/adv7511.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>

#include "cobalt-driver.h"
#include "cobalt-irq.h"
#include "cobalt-i2c.h"
#include "cobalt-v4l2.h"
#include "cobalt-flash.h"
#include "cobalt-alsa.h"
#include "cobalt-omnitek.h"

/* add your revision and whatnot here */
static const struct pci_device_id cobalt_pci_tbl[] = {
	{PCI_VENDOR_ID_CISCO, PCI_DEVICE_ID_COBALT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, cobalt_pci_tbl);

static atomic_t cobalt_instance = ATOMIC_INIT(0);

int cobalt_debug;
module_param_named(debug, cobalt_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level. Default: 0\n");

int cobalt_ignore_err;
module_param_named(ignore_err, cobalt_ignore_err, int, 0644);
MODULE_PARM_DESC(ignore_err,
	"If set then ignore missing i2c adapters/receivers. Default: 0\n");

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com> & Morten Hestnes");
MODULE_DESCRIPTION("cobalt driver");
MODULE_LICENSE("GPL");

static u8 edid[256] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x50, 0x21, 0x32, 0x27, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x1a, 0x01, 0x03, 0x80, 0x30, 0x1b, 0x78,
	0x0f, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x2f, 0xcf, 0x00, 0x31, 0x59,
	0x45, 0x59, 0x61, 0x59, 0x81, 0x99, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a,
	0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0xe0, 0x0e, 0x11, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x55, 0x18,
	0x5e, 0x11, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x63,
	0x6f, 0x62, 0x61, 0x6c, 0x74, 0x0a, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x9d,

	0x02, 0x03, 0x1f, 0xf1, 0x4a, 0x10, 0x1f, 0x04,
	0x13, 0x22, 0x21, 0x20, 0x02, 0x11, 0x01, 0x23,
	0x09, 0x07, 0x07, 0x68, 0x03, 0x0c, 0x00, 0x10,
	0x00, 0x00, 0x22, 0x0f, 0xe2, 0x00, 0xca, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
};

static void cobalt_set_interrupt(struct cobalt *cobalt, bool enable)
{
	if (enable) {
		unsigned irqs = COBALT_SYSSTAT_VI0_INT1_MSK |
				COBALT_SYSSTAT_VI1_INT1_MSK |
				COBALT_SYSSTAT_VI2_INT1_MSK |
				COBALT_SYSSTAT_VI3_INT1_MSK |
				COBALT_SYSSTAT_VI0_INT2_MSK |
				COBALT_SYSSTAT_VI1_INT2_MSK |
				COBALT_SYSSTAT_VI2_INT2_MSK |
				COBALT_SYSSTAT_VI3_INT2_MSK |
				COBALT_SYSSTAT_VI0_LOST_DATA_MSK |
				COBALT_SYSSTAT_VI1_LOST_DATA_MSK |
				COBALT_SYSSTAT_VI2_LOST_DATA_MSK |
				COBALT_SYSSTAT_VI3_LOST_DATA_MSK |
				COBALT_SYSSTAT_AUD_IN_LOST_DATA_MSK;

		if (cobalt->have_hsma_rx)
			irqs |= COBALT_SYSSTAT_VIHSMA_INT1_MSK |
				COBALT_SYSSTAT_VIHSMA_INT2_MSK |
				COBALT_SYSSTAT_VIHSMA_LOST_DATA_MSK;

		if (cobalt->have_hsma_tx)
			irqs |= COBALT_SYSSTAT_VOHSMA_INT1_MSK |
				COBALT_SYSSTAT_VOHSMA_LOST_DATA_MSK |
				COBALT_SYSSTAT_AUD_OUT_LOST_DATA_MSK;
		/* Clear any existing interrupts */
		cobalt_write_bar1(cobalt, COBALT_SYS_STAT_EDGE, 0xffffffff);
		/* PIO Core interrupt mask register.
		   Enable ADV7604 INT1 interrupts */
		cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK, irqs);
	} else {
		/* Disable all ADV7604 interrupts */
		cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK, 0);
	}
}

static unsigned cobalt_get_sd_nr(struct v4l2_subdev *sd)
{
	struct cobalt *cobalt = to_cobalt(sd->v4l2_dev);
	unsigned i;

	for (i = 0; i < COBALT_NUM_NODES; i++)
		if (sd == cobalt->streams[i].sd)
			return i;
	cobalt_err("Invalid adv7604 subdev pointer!\n");
	return 0;
}

static void cobalt_notify(struct v4l2_subdev *sd,
			  unsigned int notification, void *arg)
{
	struct cobalt *cobalt = to_cobalt(sd->v4l2_dev);
	unsigned sd_nr = cobalt_get_sd_nr(sd);
	struct cobalt_stream *s = &cobalt->streams[sd_nr];
	bool hotplug = arg ? *((int *)arg) : false;

	if (s->is_output)
		return;

	switch (notification) {
	case ADV76XX_HOTPLUG:
		cobalt_s_bit_sysctrl(cobalt,
			COBALT_SYS_CTRL_HPD_TO_CONNECTOR_BIT(sd_nr), hotplug);
		cobalt_dbg(1, "Set hotplug for adv %d to %d\n", sd_nr, hotplug);
		break;
	case V4L2_DEVICE_NOTIFY_EVENT:
		cobalt_dbg(1, "Format changed for adv %d\n", sd_nr);
		v4l2_event_queue(&s->vdev, arg);
		break;
	default:
		break;
	}
}

static int get_payload_size(u16 code)
{
	switch (code) {
	case 0: return 128;
	case 1: return 256;
	case 2: return 512;
	case 3: return 1024;
	case 4: return 2048;
	case 5: return 4096;
	default: return 0;
	}
	return 0;
}

static const char *get_link_speed(u16 stat)
{
	switch (stat & PCI_EXP_LNKSTA_CLS) {
	case 1:	return "2.5 Gbit/s";
	case 2:	return "5 Gbit/s";
	case 3:	return "10 Gbit/s";
	}
	return "Unknown speed";
}

void cobalt_pcie_status_show(struct cobalt *cobalt)
{
	struct pci_dev *pci_dev = cobalt->pci_dev;
	struct pci_dev *pci_bus_dev = cobalt->pci_dev->bus->self;
	u32 capa;
	u16 stat, ctrl;

	if (!pci_is_pcie(pci_dev) || !pci_is_pcie(pci_bus_dev))
		return;

	/* Device */
	pcie_capability_read_dword(pci_dev, PCI_EXP_DEVCAP, &capa);
	pcie_capability_read_word(pci_dev, PCI_EXP_DEVCTL, &ctrl);
	pcie_capability_read_word(pci_dev, PCI_EXP_DEVSTA, &stat);
	cobalt_info("PCIe device capability 0x%08x: Max payload %d\n",
		    capa, get_payload_size(capa & PCI_EXP_DEVCAP_PAYLOAD));
	cobalt_info("PCIe device control 0x%04x: Max payload %d. Max read request %d\n",
		    ctrl,
		    get_payload_size((ctrl & PCI_EXP_DEVCTL_PAYLOAD) >> 5),
		    get_payload_size((ctrl & PCI_EXP_DEVCTL_READRQ) >> 12));
	cobalt_info("PCIe device status 0x%04x\n", stat);

	/* Link */
	pcie_capability_read_dword(pci_dev, PCI_EXP_LNKCAP, &capa);
	pcie_capability_read_word(pci_dev, PCI_EXP_LNKCTL, &ctrl);
	pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &stat);
	cobalt_info("PCIe link capability 0x%08x: %s per lane and %u lanes\n",
			capa, get_link_speed(capa),
			(capa & PCI_EXP_LNKCAP_MLW) >> 4);
	cobalt_info("PCIe link control 0x%04x\n", ctrl);
	cobalt_info("PCIe link status 0x%04x: %s per lane and %u lanes\n",
		    stat, get_link_speed(stat),
		    (stat & PCI_EXP_LNKSTA_NLW) >> 4);

	/* Bus */
	pcie_capability_read_dword(pci_bus_dev, PCI_EXP_LNKCAP, &capa);
	cobalt_info("PCIe bus link capability 0x%08x: %s per lane and %u lanes\n",
			capa, get_link_speed(capa),
			(capa & PCI_EXP_LNKCAP_MLW) >> 4);

	/* Slot */
	pcie_capability_read_dword(pci_dev, PCI_EXP_SLTCAP, &capa);
	pcie_capability_read_word(pci_dev, PCI_EXP_SLTCTL, &ctrl);
	pcie_capability_read_word(pci_dev, PCI_EXP_SLTSTA, &stat);
	cobalt_info("PCIe slot capability 0x%08x\n", capa);
	cobalt_info("PCIe slot control 0x%04x\n", ctrl);
	cobalt_info("PCIe slot status 0x%04x\n", stat);
}

static unsigned pcie_link_get_lanes(struct cobalt *cobalt)
{
	struct pci_dev *pci_dev = cobalt->pci_dev;
	u16 link;

	if (!pci_is_pcie(pci_dev))
		return 0;
	pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &link);
	return (link & PCI_EXP_LNKSTA_NLW) >> 4;
}

static unsigned pcie_bus_link_get_lanes(struct cobalt *cobalt)
{
	struct pci_dev *pci_dev = cobalt->pci_dev->bus->self;
	u32 link;

	if (!pci_is_pcie(pci_dev))
		return 0;
	pcie_capability_read_dword(pci_dev, PCI_EXP_LNKCAP, &link);
	return (link & PCI_EXP_LNKCAP_MLW) >> 4;
}

static void msi_config_show(struct cobalt *cobalt, struct pci_dev *pci_dev)
{
	u16 ctrl, data;
	u32 adrs_l, adrs_h;

	pci_read_config_word(pci_dev, 0x52, &ctrl);
	cobalt_info("MSI %s\n", ctrl & 1 ? "enable" : "disable");
	cobalt_info("MSI multiple message: Capable %u. Enable %u\n",
		    (1 << ((ctrl >> 1) & 7)), (1 << ((ctrl >> 4) & 7)));
	if (ctrl & 0x80)
		cobalt_info("MSI: 64-bit address capable\n");
	pci_read_config_dword(pci_dev, 0x54, &adrs_l);
	pci_read_config_dword(pci_dev, 0x58, &adrs_h);
	pci_read_config_word(pci_dev, 0x5c, &data);
	if (ctrl & 0x80)
		cobalt_info("MSI: Address 0x%08x%08x. Data 0x%04x\n",
				adrs_h, adrs_l, data);
	else
		cobalt_info("MSI: Address 0x%08x. Data 0x%04x\n",
				adrs_l, data);
}

static void cobalt_pci_iounmap(struct cobalt *cobalt, struct pci_dev *pci_dev)
{
	if (cobalt->bar0) {
		pci_iounmap(pci_dev, cobalt->bar0);
		cobalt->bar0 = NULL;
	}
	if (cobalt->bar1) {
		pci_iounmap(pci_dev, cobalt->bar1);
		cobalt->bar1 = NULL;
	}
}

static void cobalt_free_msi(struct cobalt *cobalt, struct pci_dev *pci_dev)
{
	free_irq(pci_dev->irq, (void *)cobalt);
	pci_free_irq_vectors(pci_dev);
}

static int cobalt_setup_pci(struct cobalt *cobalt, struct pci_dev *pci_dev,
			    const struct pci_device_id *pci_id)
{
	u32 ctrl;
	int ret;

	cobalt_dbg(1, "enabling pci device\n");

	ret = pci_enable_device(pci_dev);
	if (ret) {
		cobalt_err("can't enable device\n");
		return ret;
	}
	pci_set_master(pci_dev);
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &cobalt->card_rev);
	pci_read_config_word(pci_dev, PCI_DEVICE_ID, &cobalt->device_id);

	switch (cobalt->device_id) {
	case PCI_DEVICE_ID_COBALT:
		cobalt_info("PCI Express interface from Omnitek\n");
		break;
	default:
		cobalt_info("PCI Express interface provider is unknown!\n");
		break;
	}

	if (pcie_link_get_lanes(cobalt) != 8) {
		cobalt_warn("PCI Express link width is %d lanes.\n",
				pcie_link_get_lanes(cobalt));
		if (pcie_bus_link_get_lanes(cobalt) < 8)
			cobalt_warn("The current slot only supports %d lanes, for best performance 8 are needed\n",
					pcie_bus_link_get_lanes(cobalt));
		if (pcie_link_get_lanes(cobalt) != pcie_bus_link_get_lanes(cobalt)) {
			cobalt_err("The card is most likely not seated correctly in the PCIe slot\n");
			ret = -EIO;
			goto err_disable;
		}
	}

	if (pci_set_dma_mask(pci_dev, DMA_BIT_MASK(64))) {
		ret = pci_set_dma_mask(pci_dev, DMA_BIT_MASK(32));
		if (ret) {
			cobalt_err("no suitable DMA available\n");
			goto err_disable;
		}
	}

	ret = pci_request_regions(pci_dev, "cobalt");
	if (ret) {
		cobalt_err("error requesting regions\n");
		goto err_disable;
	}

	cobalt_pcie_status_show(cobalt);

	cobalt->bar0 = pci_iomap(pci_dev, 0, 0);
	cobalt->bar1 = pci_iomap(pci_dev, 1, 0);
	if (cobalt->bar1 == NULL) {
		cobalt->bar1 = pci_iomap(pci_dev, 2, 0);
		cobalt_info("64-bit BAR\n");
	}
	if (!cobalt->bar0 || !cobalt->bar1) {
		ret = -EIO;
		goto err_release;
	}

	/* Reset the video inputs before enabling any interrupts */
	ctrl = cobalt_read_bar1(cobalt, COBALT_SYS_CTRL_BASE);
	cobalt_write_bar1(cobalt, COBALT_SYS_CTRL_BASE, ctrl & ~0xf00);

	/* Disable interrupts to prevent any spurious interrupts
	   from being generated. */
	cobalt_set_interrupt(cobalt, false);

	if (pci_alloc_irq_vectors(pci_dev, 1, 1, PCI_IRQ_MSI) < 1) {
		cobalt_err("Could not enable MSI\n");
		ret = -EIO;
		goto err_release;
	}
	msi_config_show(cobalt, pci_dev);

	/* Register IRQ */
	if (request_irq(pci_dev->irq, cobalt_irq_handler, IRQF_SHARED,
			cobalt->v4l2_dev.name, (void *)cobalt)) {
		cobalt_err("Failed to register irq %d\n", pci_dev->irq);
		ret = -EIO;
		goto err_msi;
	}

	omni_sg_dma_init(cobalt);
	return 0;

err_msi:
	pci_disable_msi(pci_dev);

err_release:
	cobalt_pci_iounmap(cobalt, pci_dev);
	pci_release_regions(pci_dev);

err_disable:
	pci_disable_device(cobalt->pci_dev);
	return ret;
}

static int cobalt_hdl_info_get(struct cobalt *cobalt)
{
	int i;

	for (i = 0; i < COBALT_HDL_INFO_SIZE; i++)
		cobalt->hdl_info[i] =
			ioread8(cobalt->bar1 + COBALT_HDL_INFO_BASE + i);
	cobalt->hdl_info[COBALT_HDL_INFO_SIZE - 1] = '\0';
	if (strstr(cobalt->hdl_info, COBALT_HDL_SEARCH_STR))
		return 0;

	return 1;
}

static void cobalt_stream_struct_init(struct cobalt *cobalt)
{
	int i;

	for (i = 0; i < COBALT_NUM_STREAMS; i++) {
		struct cobalt_stream *s = &cobalt->streams[i];

		s->cobalt = cobalt;
		s->flags = 0;
		s->is_audio = false;
		s->is_output = false;
		s->is_dummy = true;

		/* The Memory DMA channels will always get a lower channel
		 * number than the FIFO DMA. Video input should map to the
		 * stream 0-3. The other can use stream struct from 4 and
		 * higher */
		if (i <= COBALT_HSMA_IN_NODE) {
			s->dma_channel = i + cobalt->first_fifo_channel;
			s->video_channel = i;
			s->dma_fifo_mask =
				COBALT_SYSSTAT_VI0_LOST_DATA_MSK << (4 * i);
			s->adv_irq_mask =
				COBALT_SYSSTAT_VI0_INT1_MSK << (4 * i);
		} else if (i >= COBALT_AUDIO_IN_STREAM &&
			   i <= COBALT_AUDIO_IN_STREAM + 4) {
			unsigned idx = i - COBALT_AUDIO_IN_STREAM;

			s->dma_channel = 6 + idx;
			s->is_audio = true;
			s->video_channel = idx;
			s->dma_fifo_mask = COBALT_SYSSTAT_AUD_IN_LOST_DATA_MSK;
		} else if (i == COBALT_HSMA_OUT_NODE) {
			s->dma_channel = 11;
			s->is_output = true;
			s->video_channel = 5;
			s->dma_fifo_mask = COBALT_SYSSTAT_VOHSMA_LOST_DATA_MSK;
			s->adv_irq_mask = COBALT_SYSSTAT_VOHSMA_INT1_MSK;
		} else if (i == COBALT_AUDIO_OUT_STREAM) {
			s->dma_channel = 12;
			s->is_audio = true;
			s->is_output = true;
			s->video_channel = 5;
			s->dma_fifo_mask = COBALT_SYSSTAT_AUD_OUT_LOST_DATA_MSK;
		} else {
			/* FIXME: Memory DMA for debug purpose */
			s->dma_channel = i - COBALT_NUM_NODES;
		}
		cobalt_info("stream #%d -> dma channel #%d <- video channel %d\n",
			    i, s->dma_channel, s->video_channel);
	}
}

static int cobalt_subdevs_init(struct cobalt *cobalt)
{
	static struct adv76xx_platform_data adv7604_pdata = {
		.disable_pwrdnb = 1,
		.ain_sel = ADV7604_AIN7_8_9_NC_SYNC_3_1,
		.bus_order = ADV7604_BUS_ORDER_BRG,
		.blank_data = 1,
		.op_format_mode_sel = ADV7604_OP_FORMAT_MODE0,
		.int1_config = ADV76XX_INT1_CONFIG_ACTIVE_HIGH,
		.dr_str_data = ADV76XX_DR_STR_HIGH,
		.dr_str_clk = ADV76XX_DR_STR_HIGH,
		.dr_str_sync = ADV76XX_DR_STR_HIGH,
		.hdmi_free_run_mode = 1,
		.inv_vs_pol = 1,
		.inv_hs_pol = 1,
	};
	static struct i2c_board_info adv7604_info = {
		.type = "adv7604",
		.addr = 0x20,
		.platform_data = &adv7604_pdata,
	};

	struct cobalt_stream *s = cobalt->streams;
	int i;

	for (i = 0; i < COBALT_NUM_INPUTS; i++) {
		struct v4l2_subdev_format sd_fmt = {
			.pad = ADV7604_PAD_SOURCE,
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
			.format.code = MEDIA_BUS_FMT_YUYV8_1X16,
		};
		struct v4l2_subdev_edid cobalt_edid = {
			.pad = ADV76XX_PAD_HDMI_PORT_A,
			.start_block = 0,
			.blocks = 2,
			.edid = edid,
		};
		int err;

		s[i].pad_source = ADV7604_PAD_SOURCE;
		s[i].i2c_adap = &cobalt->i2c_adap[i];
		if (s[i].i2c_adap->dev.parent == NULL)
			continue;
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_NRESET_TO_HDMI_BIT(i), 1);
		s[i].sd = v4l2_i2c_new_subdev_board(&cobalt->v4l2_dev,
			s[i].i2c_adap, &adv7604_info, NULL);
		if (!s[i].sd) {
			if (cobalt_ignore_err)
				continue;
			return -ENODEV;
		}
		err = v4l2_subdev_call(s[i].sd, video, s_routing,
				ADV76XX_PAD_HDMI_PORT_A, 0, 0);
		if (err)
			return err;
		err = v4l2_subdev_call(s[i].sd, pad, set_edid,
				&cobalt_edid);
		if (err)
			return err;
		err = v4l2_subdev_call(s[i].sd, pad, set_fmt, NULL,
				&sd_fmt);
		if (err)
			return err;
		/* Reset channel video module */
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(i), 0);
		mdelay(2);
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(i), 1);
		mdelay(1);
		s[i].is_dummy = false;
		cobalt->streams[i + COBALT_AUDIO_IN_STREAM].is_dummy = false;
	}
	return 0;
}

static int cobalt_subdevs_hsma_init(struct cobalt *cobalt)
{
	static struct adv7842_platform_data adv7842_pdata = {
		.disable_pwrdnb = 1,
		.ain_sel = ADV7842_AIN1_2_3_NC_SYNC_1_2,
		.bus_order = ADV7842_BUS_ORDER_RBG,
		.op_format_mode_sel = ADV7842_OP_FORMAT_MODE0,
		.blank_data = 1,
		.dr_str_data = 3,
		.dr_str_clk = 3,
		.dr_str_sync = 3,
		.mode = ADV7842_MODE_HDMI,
		.hdmi_free_run_enable = 1,
		.vid_std_select = ADV7842_HDMI_COMP_VID_STD_HD_1250P,
		.i2c_sdp_io = 0x4a,
		.i2c_sdp = 0x48,
		.i2c_cp = 0x22,
		.i2c_vdp = 0x24,
		.i2c_afe = 0x26,
		.i2c_hdmi = 0x34,
		.i2c_repeater = 0x32,
		.i2c_edid = 0x36,
		.i2c_infoframe = 0x3e,
		.i2c_cec = 0x40,
		.i2c_avlink = 0x42,
	};
	static struct i2c_board_info adv7842_info = {
		.type = "adv7842",
		.addr = 0x20,
		.platform_data = &adv7842_pdata,
	};
	struct v4l2_subdev_format sd_fmt = {
		.pad = ADV7842_PAD_SOURCE,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format.code = MEDIA_BUS_FMT_YUYV8_1X16,
	};
	static struct adv7511_platform_data adv7511_pdata = {
		.i2c_edid = 0x7e >> 1,
		.i2c_cec = 0x7c >> 1,
		.i2c_pktmem = 0x70 >> 1,
		.cec_clk = 12000000,
	};
	static struct i2c_board_info adv7511_info = {
		.type = "adv7511-v4l2",
		.addr = 0x39, /* 0x39 or 0x3d */
		.platform_data = &adv7511_pdata,
	};
	struct v4l2_subdev_edid cobalt_edid = {
		.pad = ADV7842_EDID_PORT_A,
		.start_block = 0,
		.blocks = 2,
		.edid = edid,
	};
	struct cobalt_stream *s = &cobalt->streams[COBALT_HSMA_IN_NODE];

	s->i2c_adap = &cobalt->i2c_adap[COBALT_NUM_ADAPTERS - 1];
	if (s->i2c_adap->dev.parent == NULL)
		return 0;
	cobalt_s_bit_sysctrl(cobalt, COBALT_SYS_CTRL_NRESET_TO_HDMI_BIT(4), 1);

	s->sd = v4l2_i2c_new_subdev_board(&cobalt->v4l2_dev,
			s->i2c_adap, &adv7842_info, NULL);
	if (s->sd) {
		int err = v4l2_subdev_call(s->sd, pad, set_edid, &cobalt_edid);

		if (err)
			return err;
		err = v4l2_subdev_call(s->sd, pad, set_fmt, NULL,
				&sd_fmt);
		if (err)
			return err;
		cobalt->have_hsma_rx = true;
		s->pad_source = ADV7842_PAD_SOURCE;
		s->is_dummy = false;
		cobalt->streams[4 + COBALT_AUDIO_IN_STREAM].is_dummy = false;
		/* Reset channel video module */
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(4), 0);
		mdelay(2);
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(4), 1);
		mdelay(1);
		return err;
	}
	cobalt_s_bit_sysctrl(cobalt, COBALT_SYS_CTRL_NRESET_TO_HDMI_BIT(4), 0);
	cobalt_s_bit_sysctrl(cobalt, COBALT_SYS_CTRL_PWRDN0_TO_HSMA_TX_BIT, 0);
	s++;
	s->i2c_adap = &cobalt->i2c_adap[COBALT_NUM_ADAPTERS - 1];
	s->sd = v4l2_i2c_new_subdev_board(&cobalt->v4l2_dev,
			s->i2c_adap, &adv7511_info, NULL);
	if (s->sd) {
		/* A transmitter is hooked up, so we can set this bit */
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_HSMA_TX_ENABLE_BIT, 1);
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(4), 0);
		cobalt_s_bit_sysctrl(cobalt,
				COBALT_SYS_CTRL_VIDEO_TX_RESETN_BIT, 1);
		cobalt->have_hsma_tx = true;
		v4l2_subdev_call(s->sd, core, s_power, 1);
		v4l2_subdev_call(s->sd, video, s_stream, 1);
		v4l2_subdev_call(s->sd, audio, s_stream, 1);
		v4l2_ctrl_s_ctrl(v4l2_ctrl_find(s->sd->ctrl_handler,
				 V4L2_CID_DV_TX_MODE), V4L2_DV_TX_MODE_HDMI);
		s->is_dummy = false;
		cobalt->streams[COBALT_AUDIO_OUT_STREAM].is_dummy = false;
		return 0;
	}
	return -ENODEV;
}

static int cobalt_probe(struct pci_dev *pci_dev,
				  const struct pci_device_id *pci_id)
{
	struct cobalt *cobalt;
	int retval = 0;
	int i;

	/* FIXME - module parameter arrays constrain max instances */
	i = atomic_inc_return(&cobalt_instance) - 1;

	cobalt = kzalloc(sizeof(struct cobalt), GFP_KERNEL);
	if (cobalt == NULL)
		return -ENOMEM;
	cobalt->pci_dev = pci_dev;
	cobalt->instance = i;

	retval = v4l2_device_register(&pci_dev->dev, &cobalt->v4l2_dev);
	if (retval) {
		pr_err("cobalt: v4l2_device_register of card %d failed\n",
				cobalt->instance);
		kfree(cobalt);
		return retval;
	}
	snprintf(cobalt->v4l2_dev.name, sizeof(cobalt->v4l2_dev.name),
		 "cobalt-%d", cobalt->instance);
	cobalt->v4l2_dev.notify = cobalt_notify;
	cobalt_info("Initializing card %d\n", cobalt->instance);

	cobalt->irq_work_queues =
		create_singlethread_workqueue(cobalt->v4l2_dev.name);
	if (cobalt->irq_work_queues == NULL) {
		cobalt_err("Could not create workqueue\n");
		retval = -ENOMEM;
		goto err;
	}

	INIT_WORK(&cobalt->irq_work_queue, cobalt_irq_work_handler);

	/* PCI Device Setup */
	retval = cobalt_setup_pci(cobalt, pci_dev, pci_id);
	if (retval != 0)
		goto err_wq;

	/* Show HDL version info */
	if (cobalt_hdl_info_get(cobalt))
		cobalt_info("Not able to read the HDL info\n");
	else
		cobalt_info("%s", cobalt->hdl_info);

	retval = cobalt_i2c_init(cobalt);
	if (retval)
		goto err_pci;

	cobalt_stream_struct_init(cobalt);

	retval = cobalt_subdevs_init(cobalt);
	if (retval)
		goto err_i2c;

	if (!(cobalt_read_bar1(cobalt, COBALT_SYS_STAT_BASE) &
			COBALT_SYSSTAT_HSMA_PRSNTN_MSK)) {
		retval = cobalt_subdevs_hsma_init(cobalt);
		if (retval)
			goto err_i2c;
	}

	retval = cobalt_nodes_register(cobalt);
	if (retval) {
		cobalt_err("Error %d registering device nodes\n", retval);
		goto err_i2c;
	}
	cobalt_set_interrupt(cobalt, true);
	v4l2_device_call_all(&cobalt->v4l2_dev, 0, core,
					interrupt_service_routine, 0, NULL);

	cobalt_info("Initialized cobalt card\n");

	cobalt_flash_probe(cobalt);

	return 0;

err_i2c:
	cobalt_i2c_exit(cobalt);
	cobalt_s_bit_sysctrl(cobalt, COBALT_SYS_CTRL_HSMA_TX_ENABLE_BIT, 0);
err_pci:
	cobalt_free_msi(cobalt, pci_dev);
	cobalt_pci_iounmap(cobalt, pci_dev);
	pci_release_regions(cobalt->pci_dev);
	pci_disable_device(cobalt->pci_dev);
err_wq:
	destroy_workqueue(cobalt->irq_work_queues);
err:
	cobalt_err("error %d on initialization\n", retval);

	v4l2_device_unregister(&cobalt->v4l2_dev);
	kfree(cobalt);
	return retval;
}

static void cobalt_remove(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct cobalt *cobalt = to_cobalt(v4l2_dev);
	int i;

	cobalt_flash_remove(cobalt);
	cobalt_set_interrupt(cobalt, false);
	flush_workqueue(cobalt->irq_work_queues);
	cobalt_nodes_unregister(cobalt);
	for (i = 0; i < COBALT_NUM_ADAPTERS; i++) {
		struct v4l2_subdev *sd = cobalt->streams[i].sd;
		struct i2c_client *client;

		if (sd == NULL)
			continue;
		client = v4l2_get_subdevdata(sd);
		v4l2_device_unregister_subdev(sd);
		i2c_unregister_device(client);
	}
	cobalt_i2c_exit(cobalt);
	cobalt_free_msi(cobalt, pci_dev);
	cobalt_s_bit_sysctrl(cobalt, COBALT_SYS_CTRL_HSMA_TX_ENABLE_BIT, 0);
	cobalt_pci_iounmap(cobalt, pci_dev);
	pci_release_regions(cobalt->pci_dev);
	pci_disable_device(cobalt->pci_dev);
	destroy_workqueue(cobalt->irq_work_queues);

	cobalt_info("removed cobalt card\n");

	v4l2_device_unregister(v4l2_dev);
	kfree(cobalt);
}

/* define a pci_driver for card detection */
static struct pci_driver cobalt_pci_driver = {
	.name =     "cobalt",
	.id_table = cobalt_pci_tbl,
	.probe =    cobalt_probe,
	.remove =   cobalt_remove,
};

module_pci_driver(cobalt_pci_driver);
