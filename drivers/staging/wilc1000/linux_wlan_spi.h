#ifndef LINUX_WLAN_SPI_H
#define LINUX_WLAN_SPI_H

#include <linux/spi/spi.h>
extern struct spi_device *wilc_spi_dev;
extern struct spi_driver wilc_bus;

int wilc_spi_init(void);
int wilc_spi_write(u8 *b, u32 len);
int wilc_spi_read(u8 *rb, u32 rlen);
int wilc_spi_write_read(u8 *wb, u8 *rb, u32 rlen);
int wilc_spi_set_max_speed(void);
#endif
