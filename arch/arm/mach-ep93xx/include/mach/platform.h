/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/mach-ep93xx/include/mach/platform.h
 */

#ifndef __ASSEMBLY__

#include <linux/reboot.h>

struct device;
struct i2c_gpio_platform_data;
struct i2c_board_info;
struct spi_board_info;
struct platform_device;
struct ep93xxfb_mach_info;
struct ep93xx_keypad_platform_data;
struct ep93xx_spi_info;

struct ep93xx_eth_data
{
	unsigned char	dev_addr[6];
	unsigned char	phy_id;
};

void ep93xx_map_io(void);
void ep93xx_init_irq(void);

#define EP93XX_CHIP_REV_D0	3
#define EP93XX_CHIP_REV_D1	4
#define EP93XX_CHIP_REV_E0	5
#define EP93XX_CHIP_REV_E1	6
#define EP93XX_CHIP_REV_E2	7

unsigned int ep93xx_chip_revision(void);

void ep93xx_register_flash(unsigned int width,
			   resource_size_t start, resource_size_t size);

void ep93xx_register_eth(struct ep93xx_eth_data *data, int copy_addr);
void ep93xx_register_i2c(struct i2c_gpio_platform_data *data,
			 struct i2c_board_info *devices, int num);
void ep93xx_register_spi(struct ep93xx_spi_info *info,
			 struct spi_board_info *devices, int num);
void ep93xx_register_fb(struct ep93xxfb_mach_info *data);
void ep93xx_register_pwm(int pwm0, int pwm1);
int ep93xx_pwm_acquire_gpio(struct platform_device *pdev);
void ep93xx_pwm_release_gpio(struct platform_device *pdev);
void ep93xx_register_keypad(struct ep93xx_keypad_platform_data *data);
int ep93xx_keypad_acquire_gpio(struct platform_device *pdev);
void ep93xx_keypad_release_gpio(struct platform_device *pdev);
void ep93xx_register_i2s(void);
int ep93xx_i2s_acquire(void);
void ep93xx_i2s_release(void);
void ep93xx_register_ac97(void);
void ep93xx_register_ide(void);
void ep93xx_register_adc(void);
int ep93xx_ide_acquire_gpio(struct platform_device *pdev);
void ep93xx_ide_release_gpio(struct platform_device *pdev);

struct device *ep93xx_init_devices(void);
extern void ep93xx_timer_init(void);

void ep93xx_restart(enum reboot_mode, const char *);
void ep93xx_init_late(void);

#ifdef CONFIG_CRUNCH
int crunch_init(void);
#else
static inline int crunch_init(void) { return 0; }
#endif

#endif
