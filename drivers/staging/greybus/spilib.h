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

struct gb_connection;

int gb_spilib_master_init(struct gb_connection *connection, struct device *dev);
void gb_spilib_master_exit(struct gb_connection *connection);

#endif /* __SPILIB_H */
