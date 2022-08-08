// SPDX-License-Identifier: GPL-2.0
/*
 * Raspberry Pi firmware based touchscreen driver
 *
 * Copyright (C) 2015, 2017 Raspberry Pi
 * Copyright (C) 2018 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#define RPI_TS_DEFAULT_WIDTH	800
#define RPI_TS_DEFAULT_HEIGHT	480

#define RPI_TS_MAX_SUPPORTED_POINTS	10

#define RPI_TS_FTS_TOUCH_DOWN		0
#define RPI_TS_FTS_TOUCH_CONTACT	2

#define RPI_TS_POLL_INTERVAL		17	/* 60fps */

#define RPI_TS_NPOINTS_REG_INVALIDATE	99

struct rpi_ts {
	struct platform_device *pdev;
	struct input_dev *input;
	struct touchscreen_properties prop;

	void __iomem *fw_regs_va;
	dma_addr_t fw_regs_phys;

	int known_ids;
};

struct rpi_ts_regs {
	u8 device_mode;
	u8 gesture_id;
	u8 num_points;
	struct rpi_ts_touch {
		u8 xh;
		u8 xl;
		u8 yh;
		u8 yl;
		u8 pressure; /* Not supported */
		u8 area;     /* Not supported */
	} point[RPI_TS_MAX_SUPPORTED_POINTS];
};

static void rpi_ts_poll(struct input_dev *input)
{
	struct rpi_ts *ts = input_get_drvdata(input);
	struct rpi_ts_regs regs;
	int modified_ids = 0;
	long released_ids;
	int event_type;
	int touchid;
	int x, y;
	int i;

	memcpy_fromio(&regs, ts->fw_regs_va, sizeof(regs));
	/*
	 * We poll the memory based register copy of the touchscreen chip using
	 * the number of points register to know whether the copy has been
	 * updated (we write 99 to the memory copy, the GPU will write between
	 * 0 - 10 points)
	 */
	iowrite8(RPI_TS_NPOINTS_REG_INVALIDATE,
		 ts->fw_regs_va + offsetof(struct rpi_ts_regs, num_points));

	if (regs.num_points == RPI_TS_NPOINTS_REG_INVALIDATE ||
	    (regs.num_points == 0 && ts->known_ids == 0))
		return;

	for (i = 0; i < regs.num_points; i++) {
		x = (((int)regs.point[i].xh & 0xf) << 8) + regs.point[i].xl;
		y = (((int)regs.point[i].yh & 0xf) << 8) + regs.point[i].yl;
		touchid = (regs.point[i].yh >> 4) & 0xf;
		event_type = (regs.point[i].xh >> 6) & 0x03;

		modified_ids |= BIT(touchid);

		if (event_type == RPI_TS_FTS_TOUCH_DOWN ||
		    event_type == RPI_TS_FTS_TOUCH_CONTACT) {
			input_mt_slot(input, touchid);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, 1);
			touchscreen_report_pos(input, &ts->prop, x, y, true);
		}
	}

	released_ids = ts->known_ids & ~modified_ids;
	for_each_set_bit(i, &released_ids, RPI_TS_MAX_SUPPORTED_POINTS) {
		input_mt_slot(input, i);
		input_mt_report_slot_inactive(input);
		modified_ids &= ~(BIT(i));
	}
	ts->known_ids = modified_ids;

	input_mt_sync_frame(input);
	input_sync(input);
}

static void rpi_ts_dma_cleanup(void *data)
{
	struct rpi_ts *ts = data;
	struct device *dev = &ts->pdev->dev;

	dma_free_coherent(dev, PAGE_SIZE, ts->fw_regs_va, ts->fw_regs_phys);
}

static int rpi_ts_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct input_dev *input;
	struct device_node *fw_node;
	struct rpi_firmware *fw;
	struct rpi_ts *ts;
	u32 touchbuf;
	int error;

	fw_node = of_get_parent(np);
	if (!fw_node) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	fw = rpi_firmware_get(fw_node);
	of_node_put(fw_node);
	if (!fw)
		return -EPROBE_DEFER;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->pdev = pdev;

	ts->fw_regs_va = dma_alloc_coherent(dev, PAGE_SIZE, &ts->fw_regs_phys,
					    GFP_KERNEL);
	if (!ts->fw_regs_va) {
		dev_err(dev, "failed to dma_alloc_coherent\n");
		return -ENOMEM;
	}

	error = devm_add_action_or_reset(dev, rpi_ts_dma_cleanup, ts);
	if (error) {
		dev_err(dev, "failed to devm_add_action_or_reset, %d\n", error);
		return error;
	}

	touchbuf = (u32)ts->fw_regs_phys;
	error = rpi_firmware_property(fw, RPI_FIRMWARE_FRAMEBUFFER_SET_TOUCHBUF,
				      &touchbuf, sizeof(touchbuf));
	rpi_firmware_put(fw);
	if (error || touchbuf != 0) {
		dev_warn(dev, "Failed to set touchbuf, %d\n", error);
		return error;
	}

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input = input;
	input_set_drvdata(input, ts);

	input->name = "raspberrypi-ts";
	input->id.bustype = BUS_HOST;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0,
			     RPI_TS_DEFAULT_WIDTH, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
			     RPI_TS_DEFAULT_HEIGHT, 0, 0);
	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, RPI_TS_MAX_SUPPORTED_POINTS,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(dev, "could not init mt slots, %d\n", error);
		return error;
	}

	error = input_setup_polling(input, rpi_ts_poll);
	if (error) {
		dev_err(dev, "could not set up polling mode, %d\n", error);
		return error;
	}

	input_set_poll_interval(input, RPI_TS_POLL_INTERVAL);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "could not register input device, %d\n", error);
		return error;
	}

	return 0;
}

static const struct of_device_id rpi_ts_match[] = {
	{ .compatible = "raspberrypi,firmware-ts", },
	{},
};
MODULE_DEVICE_TABLE(of, rpi_ts_match);

static struct platform_driver rpi_ts_driver = {
	.driver = {
		.name = "raspberrypi-ts",
		.of_match_table = rpi_ts_match,
	},
	.probe = rpi_ts_probe,
};
module_platform_driver(rpi_ts_driver);

MODULE_AUTHOR("Gordon Hollingworth");
MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_DESCRIPTION("Raspberry Pi firmware based touchscreen driver");
MODULE_LICENSE("GPL v2");
