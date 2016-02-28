/*
 * Greybus GPBridge phy driver
 *
 * Copyright 2016 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __GPBRIDGE_H
#define __GPBRIDGE_H

extern int gb_gpio_protocol_init(void);
extern void gb_gpio_protocol_exit(void);

extern int gb_pwm_protocol_init(void);
extern void gb_pwm_protocol_exit(void);

extern int gb_uart_protocol_init(void);
extern void gb_uart_protocol_exit(void);

extern int gb_sdio_protocol_init(void);
extern void gb_sdio_protocol_exit(void);

extern int gb_usb_protocol_init(void);
extern void gb_usb_protocol_exit(void);

extern int gb_i2c_protocol_init(void);
extern void gb_i2c_protocol_exit(void);

extern int gb_spi_protocol_init(void);
extern void gb_spi_protocol_exit(void);

#endif /* __GPBRIDGE_H */

