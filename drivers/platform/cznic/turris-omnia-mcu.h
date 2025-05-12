/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CZ.NIC's Turris Omnia MCU driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#ifndef __TURRIS_OMNIA_MCU_H
#define __TURRIS_OMNIA_MCU_H

#include <linux/completion.h>
#include <linux/gpio/driver.h>
#include <linux/hw_random.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/workqueue.h>

enum {
	OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN	= 1 + 32,
	OMNIA_MCU_CRYPTO_SIGNATURE_LEN	= 64,
};

struct i2c_client;
struct rtc_device;

/**
 * struct omnia_mcu - driver private data structure
 * @client:			I2C client
 * @type:			MCU type (STM32, GD32, MKL, or unknown)
 * @features:			bitmap of features supported by the MCU firmware
 * @board_serial_number:	board serial number, if stored in MCU
 * @board_first_mac:		board first MAC address, if stored in MCU
 * @board_revision:		board revision, if stored in MCU
 * @gc:				GPIO chip
 * @lock:			mutex to protect internal GPIO chip state
 * @mask:			bitmap of masked IRQs
 * @rising:			bitmap of rising edge IRQs
 * @falling:			bitmap of falling edge IRQs
 * @both:			bitmap of both edges IRQs
 * @cached:			bitmap of cached IRQ line values (when an IRQ line is configured for
 *				both edges, we cache the corresponding GPIO values in the IRQ
 *				handler)
 * @is_cached:			bitmap of which IRQ line values are cached
 * @button_release_emul_work:	front button release emulation work, used with old MCU firmware
 *				versions which did not send button release events, only button press
 *				events
 * @last_status:		cached value of the status word, to be compared with new value to
 *				determine which interrupt events occurred, used with old MCU
 *				firmware versions which only informed that the status word changed,
 *				but not which bits of the status word changed
 * @button_pressed_emul:	the front button is still emulated to be pressed
 * @rtcdev:			RTC device, does not actually count real-time, the device is only
 *				used for the RTC alarm mechanism, so that the board can be
 *				configured to wake up from poweroff state at a specific time
 * @rtc_alarm:			RTC alarm that was set for the board to wake up on, in MCU time
 *				(seconds since last MCU reset)
 * @front_button_poweron:	the front button should power on the device after it is powered off
 * @wdt:			watchdog driver structure
 * @trng:			RNG driver structure
 * @trng_entropy_ready:		RNG entropy ready completion
 * @msg_signed:			message signed completion
 * @sign_lock:			mutex to protect message signing state
 * @sign_requested:		flag indicating that message signing was requested but not completed
 * @sign_err:			message signing error number, filled in interrupt handler
 * @signature:			message signing signature, filled in interrupt handler
 * @board_public_key:		board public key, if stored in MCU
 */
struct omnia_mcu {
	struct i2c_client *client;
	const char *type;
	u32 features;

	u64 board_serial_number;
	u8 board_first_mac[ETH_ALEN];
	u8 board_revision;

#ifdef CONFIG_TURRIS_OMNIA_MCU_GPIO
	struct gpio_chip gc;
	struct mutex lock;
	unsigned long mask, rising, falling, both, cached, is_cached;
	struct delayed_work button_release_emul_work;
	unsigned long last_status;
	bool button_pressed_emul;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_SYSOFF_WAKEUP
	struct rtc_device *rtcdev;
	u32 rtc_alarm;
	bool front_button_poweron;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_WATCHDOG
	struct watchdog_device wdt;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_TRNG
	struct hwrng trng;
	struct completion trng_entropy_ready;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_KEYCTL
	struct completion msg_signed;
	struct mutex sign_lock;
	bool sign_requested;
	int sign_err;
	u8 signature[OMNIA_MCU_CRYPTO_SIGNATURE_LEN];
	u8 board_public_key[OMNIA_MCU_CRYPTO_PUBLIC_KEY_LEN];
#endif
};

#ifdef CONFIG_TURRIS_OMNIA_MCU_GPIO
extern const struct attribute_group omnia_mcu_gpio_group;
int omnia_mcu_register_gpiochip(struct omnia_mcu *mcu);
int omnia_mcu_request_irq(struct omnia_mcu *mcu, u32 spec,
			  irq_handler_t thread_fn, const char *devname);
#else
static inline int omnia_mcu_register_gpiochip(struct omnia_mcu *mcu)
{
	return 0;
}
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_KEYCTL
int omnia_mcu_register_keyctl(struct omnia_mcu *mcu);
#else
static inline int omnia_mcu_register_keyctl(struct omnia_mcu *mcu)
{
	return 0;
}
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_SYSOFF_WAKEUP
extern const struct attribute_group omnia_mcu_poweroff_group;
int omnia_mcu_register_sys_off_and_wakeup(struct omnia_mcu *mcu);
#else
static inline int omnia_mcu_register_sys_off_and_wakeup(struct omnia_mcu *mcu)
{
	return 0;
}
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_TRNG
int omnia_mcu_register_trng(struct omnia_mcu *mcu);
#else
static inline int omnia_mcu_register_trng(struct omnia_mcu *mcu)
{
	return 0;
}
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_WATCHDOG
int omnia_mcu_register_watchdog(struct omnia_mcu *mcu);
#else
static inline int omnia_mcu_register_watchdog(struct omnia_mcu *mcu)
{
	return 0;
}
#endif

#endif /* __TURRIS_OMNIA_MCU_H */
