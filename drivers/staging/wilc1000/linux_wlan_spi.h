#ifndef LINUX_WLAN_SPI_H
#define LINUX_WLAN_SPI_H

#include <linux/spi/spi.h>
extern struct spi_device *wilc_spi_dev;
extern struct spi_driver wilc_bus;

int linux_spi_init(void *vp);
void linux_spi_deinit(void *vp);
int linux_spi_write(u8 *b, u32 len);
int linux_spi_read(u8 *rb, u32 rlen);
int linux_spi_write_read(u8 *wb, u8 *rb, u32 rlen);
int linux_spi_set_max_speed(void);
#endif
