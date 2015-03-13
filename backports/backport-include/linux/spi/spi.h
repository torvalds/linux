#ifndef _BACKPORTS_LINUX_SPI_H
#define _BACKPORTS_LINUX_SPI_H 1

#include_next <linux/spi/spi.h>

#ifndef module_spi_driver
/**
 * module_spi_driver() - Helper macro for registering a SPI driver
 * @__spi_driver: spi_driver struct
 *
 * Helper macro for SPI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_spi_driver(__spi_driver) \
	module_driver(__spi_driver, spi_register_driver, \
			spi_unregister_driver)
#endif

#endif /* _BACKPORTS_LINUX_SPI_H */
