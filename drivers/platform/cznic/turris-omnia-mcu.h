/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CZ.NIC's Turris Omnia MCU driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#ifndef __TURRIS_OMNIA_MCU_H
#define __TURRIS_OMNIA_MCU_H

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/gpio/driver.h>
#include <linux/hw_random.h>
#include <linux/if_ether.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/workqueue.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

struct i2c_client;
struct rtc_device;

struct omnia_mcu {
	struct i2c_client *client;
	const char *type;
	u32 features;

	/* board information */
	u64 board_serial_number;
	u8 board_first_mac[ETH_ALEN];
	u8 board_revision;

#ifdef CONFIG_TURRIS_OMNIA_MCU_GPIO
	/* GPIO chip */
	struct gpio_chip gc;
	struct mutex lock;
	unsigned long mask, rising, falling, both, cached, is_cached;
	/* Old MCU firmware handling needs the following */
	struct delayed_work button_release_emul_work;
	unsigned long last_status;
	bool button_pressed_emul;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_SYSOFF_WAKEUP
	/* RTC device for configuring wake-up */
	struct rtc_device *rtcdev;
	u32 rtc_alarm;
	bool front_button_poweron;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_WATCHDOG
	/* MCU watchdog */
	struct watchdog_device wdt;
#endif

#ifdef CONFIG_TURRIS_OMNIA_MCU_TRNG
	/* true random number generator */
	struct hwrng trng;
	struct completion trng_entropy_ready;
#endif
};

int omnia_cmd_write_read(const struct i2c_client *client,
			 void *cmd, unsigned int cmd_len,
			 void *reply, unsigned int reply_len);

static inline int omnia_cmd_write(const struct i2c_client *client, void *cmd,
				  unsigned int len)
{
	return omnia_cmd_write_read(client, cmd, len, NULL, 0);
}

static inline int omnia_cmd_write_u8(const struct i2c_client *client, u8 cmd,
				     u8 val)
{
	u8 buf[2] = { cmd, val };

	return omnia_cmd_write(client, buf, sizeof(buf));
}

static inline int omnia_cmd_write_u16(const struct i2c_client *client, u8 cmd,
				      u16 val)
{
	u8 buf[3];

	buf[0] = cmd;
	put_unaligned_le16(val, &buf[1]);

	return omnia_cmd_write(client, buf, sizeof(buf));
}

static inline int omnia_cmd_write_u32(const struct i2c_client *client, u8 cmd,
				      u32 val)
{
	u8 buf[5];

	buf[0] = cmd;
	put_unaligned_le32(val, &buf[1]);

	return omnia_cmd_write(client, buf, sizeof(buf));
}

static inline int omnia_cmd_read(const struct i2c_client *client, u8 cmd,
				 void *reply, unsigned int len)
{
	return omnia_cmd_write_read(client, &cmd, 1, reply, len);
}

static inline unsigned int
omnia_compute_reply_length(unsigned long mask, bool interleaved,
			   unsigned int offset)
{
	if (!mask)
		return 0;

	return ((__fls(mask) >> 3) << interleaved) + 1 + offset;
}

/* Returns 0 on success */
static inline int omnia_cmd_read_bits(const struct i2c_client *client, u8 cmd,
				      unsigned long bits, unsigned long *dst)
{
	__le32 reply;
	int err;

	if (!bits) {
		*dst = 0;
		return 0;
	}

	err = omnia_cmd_read(client, cmd, &reply,
			     omnia_compute_reply_length(bits, false, 0));
	if (err)
		return err;

	*dst = le32_to_cpu(reply) & bits;

	return 0;
}

static inline int omnia_cmd_read_bit(const struct i2c_client *client, u8 cmd,
				     unsigned long bit)
{
	unsigned long reply;
	int err;

	err = omnia_cmd_read_bits(client, cmd, bit, &reply);
	if (err)
		return err;

	return !!reply;
}

static inline int omnia_cmd_read_u32(const struct i2c_client *client, u8 cmd,
				     u32 *dst)
{
	__le32 reply;
	int err;

	err = omnia_cmd_read(client, cmd, &reply, sizeof(reply));
	if (err)
		return err;

	*dst = le32_to_cpu(reply);

	return 0;
}

static inline int omnia_cmd_read_u16(const struct i2c_client *client, u8 cmd,
				     u16 *dst)
{
	__le16 reply;
	int err;

	err = omnia_cmd_read(client, cmd, &reply, sizeof(reply));
	if (err)
		return err;

	*dst = le16_to_cpu(reply);

	return 0;
}

static inline int omnia_cmd_read_u8(const struct i2c_client *client, u8 cmd,
				    u8 *reply)
{
	return omnia_cmd_read(client, cmd, reply, sizeof(*reply));
}

#ifdef CONFIG_TURRIS_OMNIA_MCU_GPIO
extern const u8 omnia_int_to_gpio_idx[32];
extern const struct attribute_group omnia_mcu_gpio_group;
int omnia_mcu_register_gpiochip(struct omnia_mcu *mcu);
#else
static inline int omnia_mcu_register_gpiochip(struct omnia_mcu *mcu)
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
