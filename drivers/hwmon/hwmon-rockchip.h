/*
 * Copyright (C) ST-Ericsson 2010 - 2013
 * License terms: GNU General Public License v2
 * Author: Martin Persson <martin.persson@stericsson.com>
 *         Hongbo Zhang <hongbo.zhang@linaro.com>
 */

#ifndef _ROCKCHIP_H
#define _ROCKCHIP_H

#define NUM_SENSORS 5

struct rockchip_temp;

/*
 * struct rockchip_temp_ops - rockchip chip specific ops
 * @read_sensor: reads gpadc output
 * @irq_handler: irq handler
 * @show_name: hwmon device name
 * @show_label: hwmon attribute label
 * @is_visible: is attribute visible
 */
struct rockchip_temp_ops {
	int (*read_sensor)(int);
	int (*irq_handler)(int, struct rockchip_temp *);
	ssize_t (*show_name)(struct device *,
			struct device_attribute *, char *);
	ssize_t (*show_label) (struct device *,
			struct device_attribute *, char *);
	int (*is_visible)(struct attribute *, int);
};

/*
 * struct rockchip_temp - representation of temp mon device
 * @pdev: platform device
 * @hwmon_dev: hwmon device
 * @ops: rockchip chip specific ops
 * @gpadc_addr: gpadc channel address
 * @min: sensor temperature min value
 * @max: sensor temperature max value
 * @max_hyst: sensor temperature hysteresis value for max limit
 * @min_alarm: sensor temperature min alarm
 * @max_alarm: sensor temperature max alarm
 * @work: delayed work scheduled to monitor temperature periodically
 * @work_active: True if work is active
 * @lock: mutex
 * @monitored_sensors: number of monitored sensors
 * @plat_data: private usage, usually points to platform specific data
 */
struct rockchip_temp {
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct rockchip_temp_ops ops;
	u8 tsadc_addr[NUM_SENSORS];
	unsigned long min[NUM_SENSORS];
	unsigned long max[NUM_SENSORS];
	unsigned long max_hyst[NUM_SENSORS];
	bool min_alarm[NUM_SENSORS];
	bool max_alarm[NUM_SENSORS];
	struct delayed_work work;
	bool work_active;
	struct mutex lock;
	int monitored_sensors;
	void *plat_data;
	void __iomem		*regs;
	struct clk		*clk;
	struct clk		*pclk;
	struct resource		*ioarea;
	struct tsadc_host		*tsadc;
	struct work_struct 	auto_ht_irq_work;
	struct workqueue_struct  *workqueue;
	struct workqueue_struct  *tsadc_workqueue;
};

int rockchip_hwmon_init(struct rockchip_temp *data);

#endif /* _ROCKCHIP_H */
