/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus SPI library header
 *
 * copyright 2016 google inc.
 * copyright 2016 linaro ltd.
 *
 * released under the gplv2 only.
 */

#ifndef __SPILIB_H
#define __SPILIB_H

struct device;
struct gb_connection;

struct spilib_ops {
	int (*prepare_transfer_hardware)(struct device *dev);
	void (*unprepare_transfer_hardware)(struct device *dev);
};

int gb_spilib_master_init(struct gb_connection *connection,
			  struct device *dev, struct spilib_ops *ops);
void gb_spilib_master_exit(struct gb_connection *connection);

#endif /* __SPILIB_H */
