#ifndef LINUX_WLAN_SPI_H
#define LINUX_WLAN_SPI_H

#include <linux/spi/spi.h>
#include "wilc_wfi_netdevice.h"

int wilc_spi_init(void);
int wilc_spi_write(struct wilc *wilc, u8 *b, u32 len);
int wilc_spi_read(u8 *rb, u32 rlen);
int wilc_spi_write_read(u8 *wb, u8 *rb, u32 rlen);
#endif
