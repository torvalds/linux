/*
 * wifi_power.h
 *
 * WIFI power control.
 *
 * Yongle Lai
 */

#ifndef WIFI_POWER_H
#define WIFI_POWER_H

#include <linux/version.h>

#define WIFI_GPIO_POWER_CONTROL 1

#if (WIFI_GPIO_POWER_CONTROL == 1)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
#include <asm/arch/gpio.h>
#include <asm/arch/iomux.h>
#else
#include <mach/gpio.h>
#include <mach/iomux.h>

#endif

#define WIFI_CHIP_MV8686		0
#define WIFI_CHIP_AR6002		1
#define WIFI_CHIP_BCM4319		2
#define WIFI_CHIP_NRX700		3
#define WIFI_CHIP_RT3070		4

#define POWER_NOT_USE_GPIO      0
#define POWER_USE_GPIO          1
#define POWER_USE_EXT_GPIO      2  /* External GPIO chip is used, such as PCA9554. */

#define POWER_GPIO_NOT_IOMUX	0
#define POWER_GPIO_IOMUX	    1

#define GPIO_SWITCH_OFF				0
#define GPIO_SWITCH_ON				1

struct wifi_power
{
	u8 use_gpio;			/* If uses GPIO to control wifi power supply. 0 - no, 1 - yes. */
	u8 gpio_iomux;		/* If the GPIO is iomux. 0 - no, 1 - yes. */
	char *iomux_name;	/* IOMUX name */
	u8	iomux_value;	/* IOMUX value - which function is choosen. */
	u8	gpio_id;			/* GPIO number */
	u8	sensi_level;	/* GPIO sensitive level. */
};

int wifi_turn_on_card(int module);
int wifi_turn_off_card(void);
int wifi_reset_card(void);
void wifi_extgpio_operation(u8 id, u8 sens);

void rockchip_wifi_shutdown(void);

#endif /* WIFI_GPIO_POWER_CONTROL */

#define WIFI_NETWORK_BUSY	0
#define WIFI_NETWORK_IDLE	1

int wifi_power_save_init(void);
int wifi_power_save_exit(void);
int wifi_power_save_stop(void);
int wifi_power_save_state(void);
void wifi_power_save_suspend(void);
void wifi_power_save_resume(void);
int wifi_power_save_register_callback(int (*callback)(int status));

void wifi_turn_on_callback(void);
void wifi_turn_off_callback(void);

#if defined(CONFIG_RALINK_RT3070) || defined(CONFIG_REALTEK_RTL8192)

int wifi_activate_usb(void);
int wifi_deactivate_usb(void);
void wifi_usb_init(void);

#endif

#endif /* WIFI_POWER_H */

