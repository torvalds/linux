// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia MCU GPIO and IRQ driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>

#include <linux/turris-omnia-mcu-interface.h>
#include "turris-omnia-mcu.h"

#define OMNIA_CMD_INT_ARG_LEN		8
#define FRONT_BUTTON_RELEASE_DELAY_MS	50

static const char * const omnia_mcu_gpio_names[64] = {
	/* GPIOs with value read from the 16-bit wide status */
	[4]  = "MiniPCIe0 Card Detect",
	[5]  = "MiniPCIe0 mSATA Indicator",
	[6]  = "Front USB3 port over-current",
	[7]  = "Rear USB3 port over-current",
	[8]  = "Front USB3 port power",
	[9]  = "Rear USB3 port power",
	[12] = "Front Button",

	/* GPIOs with value read from the 32-bit wide extended status */
	[16] = "SFP nDET",
	[28] = "MiniPCIe0 LED",
	[29] = "MiniPCIe1 LED",
	[30] = "MiniPCIe2 LED",
	[31] = "MiniPCIe0 PAN LED",
	[32] = "MiniPCIe1 PAN LED",
	[33] = "MiniPCIe2 PAN LED",
	[34] = "WAN PHY LED0",
	[35] = "WAN PHY LED1",
	[36] = "LAN switch p0 LED0",
	[37] = "LAN switch p0 LED1",
	[38] = "LAN switch p1 LED0",
	[39] = "LAN switch p1 LED1",
	[40] = "LAN switch p2 LED0",
	[41] = "LAN switch p2 LED1",
	[42] = "LAN switch p3 LED0",
	[43] = "LAN switch p3 LED1",
	[44] = "LAN switch p4 LED0",
	[45] = "LAN switch p4 LED1",
	[46] = "LAN switch p5 LED0",
	[47] = "LAN switch p5 LED1",

	/* GPIOs with value read from the 16-bit wide extended control status */
	[48] = "eMMC nRESET",
	[49] = "LAN switch nRESET",
	[50] = "WAN PHY nRESET",
	[51] = "MiniPCIe0 nPERST",
	[52] = "MiniPCIe1 nPERST",
	[53] = "MiniPCIe2 nPERST",
	[54] = "WAN PHY SFP mux",
	[56] = "VHV power disable",
};

struct omnia_gpio {
	u8 cmd;
	u8 ctl_cmd;
	u8 bit;
	u8 ctl_bit;
	u8 int_bit;
	u16 feat;
	u16 feat_mask;
};

#define OMNIA_GPIO_INVALID_INT_BIT	0xff

#define _DEF_GPIO(_cmd, _ctl_cmd, _bit, _ctl_bit, _int_bit, _feat, _feat_mask) \
	{								\
		.cmd = _cmd,						\
		.ctl_cmd = _ctl_cmd,					\
		.bit = _bit,						\
		.ctl_bit = _ctl_bit,					\
		.int_bit = (_int_bit) < 0 ? OMNIA_GPIO_INVALID_INT_BIT	\
					  : (_int_bit),			\
		.feat = _feat,						\
		.feat_mask = _feat_mask,				\
	}

#define _DEF_GPIO_STS(_name) \
	_DEF_GPIO(OMNIA_CMD_GET_STATUS_WORD, 0, __bf_shf(OMNIA_STS_ ## _name), \
		  0, __bf_shf(OMNIA_INT_ ## _name), 0, 0)

#define _DEF_GPIO_CTL(_name) \
	_DEF_GPIO(OMNIA_CMD_GET_STATUS_WORD, OMNIA_CMD_GENERAL_CONTROL, \
		  __bf_shf(OMNIA_STS_ ## _name), __bf_shf(OMNIA_CTL_ ## _name), \
		  -1, 0, 0)

#define _DEF_GPIO_EXT_STS(_name, _feat) \
	_DEF_GPIO(OMNIA_CMD_GET_EXT_STATUS_DWORD, 0, \
		  __bf_shf(OMNIA_EXT_STS_ ## _name), 0, \
		  __bf_shf(OMNIA_INT_ ## _name), \
		  OMNIA_FEAT_ ## _feat | OMNIA_FEAT_EXT_CMDS, \
		  OMNIA_FEAT_ ## _feat | OMNIA_FEAT_EXT_CMDS)

#define _DEF_GPIO_EXT_STS_LED(_name, _ledext) \
	_DEF_GPIO(OMNIA_CMD_GET_EXT_STATUS_DWORD, 0, \
		  __bf_shf(OMNIA_EXT_STS_ ## _name), 0, \
		  __bf_shf(OMNIA_INT_ ## _name), \
		  OMNIA_FEAT_LED_STATE_ ## _ledext, \
		  OMNIA_FEAT_LED_STATE_EXT_MASK)

#define _DEF_GPIO_EXT_STS_LEDALL(_name) \
	_DEF_GPIO(OMNIA_CMD_GET_EXT_STATUS_DWORD, 0, \
		  __bf_shf(OMNIA_EXT_STS_ ## _name), 0, \
		  __bf_shf(OMNIA_INT_ ## _name), \
		  OMNIA_FEAT_LED_STATE_EXT_MASK, 0)

#define _DEF_GPIO_EXT_CTL(_name, _feat) \
	_DEF_GPIO(OMNIA_CMD_GET_EXT_CONTROL_STATUS, OMNIA_CMD_EXT_CONTROL, \
		  __bf_shf(OMNIA_EXT_CTL_ ## _name), \
		  __bf_shf(OMNIA_EXT_CTL_ ## _name), -1, \
		  OMNIA_FEAT_ ## _feat | OMNIA_FEAT_EXT_CMDS, \
		  OMNIA_FEAT_ ## _feat | OMNIA_FEAT_EXT_CMDS)

#define _DEF_INT(_name) \
	_DEF_GPIO(0, 0, 0, 0, __bf_shf(OMNIA_INT_ ## _name), 0, 0)

static inline bool is_int_bit_valid(const struct omnia_gpio *gpio)
{
	return gpio->int_bit != OMNIA_GPIO_INVALID_INT_BIT;
}

static const struct omnia_gpio omnia_gpios[64] = {
	/* GPIOs with value read from the 16-bit wide status */
	[4]  = _DEF_GPIO_STS(CARD_DET),
	[5]  = _DEF_GPIO_STS(MSATA_IND),
	[6]  = _DEF_GPIO_STS(USB30_OVC),
	[7]  = _DEF_GPIO_STS(USB31_OVC),
	[8]  = _DEF_GPIO_CTL(USB30_PWRON),
	[9]  = _DEF_GPIO_CTL(USB31_PWRON),

	/* brightness changed interrupt, no GPIO */
	[11] = _DEF_INT(BRIGHTNESS_CHANGED),

	[12] = _DEF_GPIO_STS(BUTTON_PRESSED),

	/* TRNG interrupt, no GPIO */
	[13] = _DEF_INT(TRNG),

	/* MESSAGE_SIGNED interrupt, no GPIO */
	[14] = _DEF_INT(MESSAGE_SIGNED),

	/* GPIOs with value read from the 32-bit wide extended status */
	[16] = _DEF_GPIO_EXT_STS(SFP_nDET, PERIPH_MCU),
	[28] = _DEF_GPIO_EXT_STS_LEDALL(WLAN0_MSATA_LED),
	[29] = _DEF_GPIO_EXT_STS_LEDALL(WLAN1_LED),
	[30] = _DEF_GPIO_EXT_STS_LEDALL(WLAN2_LED),
	[31] = _DEF_GPIO_EXT_STS_LED(WPAN0_LED, EXT),
	[32] = _DEF_GPIO_EXT_STS_LED(WPAN1_LED, EXT),
	[33] = _DEF_GPIO_EXT_STS_LED(WPAN2_LED, EXT),
	[34] = _DEF_GPIO_EXT_STS_LEDALL(WAN_LED0),
	[35] = _DEF_GPIO_EXT_STS_LED(WAN_LED1, EXT_V32),
	[36] = _DEF_GPIO_EXT_STS_LEDALL(LAN0_LED0),
	[37] = _DEF_GPIO_EXT_STS_LEDALL(LAN0_LED1),
	[38] = _DEF_GPIO_EXT_STS_LEDALL(LAN1_LED0),
	[39] = _DEF_GPIO_EXT_STS_LEDALL(LAN1_LED1),
	[40] = _DEF_GPIO_EXT_STS_LEDALL(LAN2_LED0),
	[41] = _DEF_GPIO_EXT_STS_LEDALL(LAN2_LED1),
	[42] = _DEF_GPIO_EXT_STS_LEDALL(LAN3_LED0),
	[43] = _DEF_GPIO_EXT_STS_LEDALL(LAN3_LED1),
	[44] = _DEF_GPIO_EXT_STS_LEDALL(LAN4_LED0),
	[45] = _DEF_GPIO_EXT_STS_LEDALL(LAN4_LED1),
	[46] = _DEF_GPIO_EXT_STS_LEDALL(LAN5_LED0),
	[47] = _DEF_GPIO_EXT_STS_LEDALL(LAN5_LED1),

	/* GPIOs with value read from the 16-bit wide extended control status */
	[48] = _DEF_GPIO_EXT_CTL(nRES_MMC, PERIPH_MCU),
	[49] = _DEF_GPIO_EXT_CTL(nRES_LAN, PERIPH_MCU),
	[50] = _DEF_GPIO_EXT_CTL(nRES_PHY, PERIPH_MCU),
	[51] = _DEF_GPIO_EXT_CTL(nPERST0, PERIPH_MCU),
	[52] = _DEF_GPIO_EXT_CTL(nPERST1, PERIPH_MCU),
	[53] = _DEF_GPIO_EXT_CTL(nPERST2, PERIPH_MCU),
	[54] = _DEF_GPIO_EXT_CTL(PHY_SFP, PERIPH_MCU),
	[56] = _DEF_GPIO_EXT_CTL(nVHV_CTRL, PERIPH_MCU),
};

/* mapping from interrupts to indexes of GPIOs in the omnia_gpios array */
static const u8 omnia_int_to_gpio_idx[32] = {
	[__bf_shf(OMNIA_INT_CARD_DET)]			= 4,
	[__bf_shf(OMNIA_INT_MSATA_IND)]			= 5,
	[__bf_shf(OMNIA_INT_USB30_OVC)]			= 6,
	[__bf_shf(OMNIA_INT_USB31_OVC)]			= 7,
	[__bf_shf(OMNIA_INT_BUTTON_PRESSED)]		= 12,
	[__bf_shf(OMNIA_INT_TRNG)]			= 13,
	[__bf_shf(OMNIA_INT_MESSAGE_SIGNED)]		= 14,
	[__bf_shf(OMNIA_INT_SFP_nDET)]			= 16,
	[__bf_shf(OMNIA_INT_BRIGHTNESS_CHANGED)]	= 11,
	[__bf_shf(OMNIA_INT_WLAN0_MSATA_LED)]		= 28,
	[__bf_shf(OMNIA_INT_WLAN1_LED)]			= 29,
	[__bf_shf(OMNIA_INT_WLAN2_LED)]			= 30,
	[__bf_shf(OMNIA_INT_WPAN0_LED)]			= 31,
	[__bf_shf(OMNIA_INT_WPAN1_LED)]			= 32,
	[__bf_shf(OMNIA_INT_WPAN2_LED)]			= 33,
	[__bf_shf(OMNIA_INT_WAN_LED0)]			= 34,
	[__bf_shf(OMNIA_INT_WAN_LED1)]			= 35,
	[__bf_shf(OMNIA_INT_LAN0_LED0)]			= 36,
	[__bf_shf(OMNIA_INT_LAN0_LED1)]			= 37,
	[__bf_shf(OMNIA_INT_LAN1_LED0)]			= 38,
	[__bf_shf(OMNIA_INT_LAN1_LED1)]			= 39,
	[__bf_shf(OMNIA_INT_LAN2_LED0)]			= 40,
	[__bf_shf(OMNIA_INT_LAN2_LED1)]			= 41,
	[__bf_shf(OMNIA_INT_LAN3_LED0)]			= 42,
	[__bf_shf(OMNIA_INT_LAN3_LED1)]			= 43,
	[__bf_shf(OMNIA_INT_LAN4_LED0)]			= 44,
	[__bf_shf(OMNIA_INT_LAN4_LED1)]			= 45,
	[__bf_shf(OMNIA_INT_LAN5_LED0)]			= 46,
	[__bf_shf(OMNIA_INT_LAN5_LED1)]			= 47,
};

/* index of PHY_SFP GPIO in the omnia_gpios array */
#define OMNIA_GPIO_PHY_SFP_OFFSET	54

static int omnia_ctl_cmd_locked(struct omnia_mcu *mcu, u8 cmd, u16 val, u16 mask)
{
	unsigned int len;
	u8 buf[5];

	buf[0] = cmd;

	switch (cmd) {
	case OMNIA_CMD_GENERAL_CONTROL:
		buf[1] = val;
		buf[2] = mask;
		len = 3;
		break;

	case OMNIA_CMD_EXT_CONTROL:
		put_unaligned_le16(val, &buf[1]);
		put_unaligned_le16(mask, &buf[3]);
		len = 5;
		break;

	default:
		BUG();
	}

	return omnia_cmd_write(mcu->client, buf, len);
}

static int omnia_ctl_cmd(struct omnia_mcu *mcu, u8 cmd, u16 val, u16 mask)
{
	guard(mutex)(&mcu->lock);

	return omnia_ctl_cmd_locked(mcu, cmd, val, mask);
}

static int omnia_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	if (!omnia_gpios[offset].cmd)
		return -EINVAL;

	return 0;
}

static int omnia_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	if (offset == OMNIA_GPIO_PHY_SFP_OFFSET) {
		int val;

		scoped_guard(mutex, &mcu->lock) {
			val = omnia_cmd_read_bit(mcu->client,
						 OMNIA_CMD_GET_EXT_CONTROL_STATUS,
						 OMNIA_EXT_CTL_PHY_SFP_AUTO);
			if (val < 0)
				return val;
		}

		if (val)
			return GPIO_LINE_DIRECTION_IN;

		return GPIO_LINE_DIRECTION_OUT;
	}

	if (omnia_gpios[offset].ctl_cmd)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int omnia_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	const struct omnia_gpio *gpio = &omnia_gpios[offset];
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	if (offset == OMNIA_GPIO_PHY_SFP_OFFSET)
		return omnia_ctl_cmd(mcu, OMNIA_CMD_EXT_CONTROL,
				     OMNIA_EXT_CTL_PHY_SFP_AUTO,
				     OMNIA_EXT_CTL_PHY_SFP_AUTO);

	if (gpio->ctl_cmd)
		return -ENOTSUPP;

	return 0;
}

static int omnia_gpio_direction_output(struct gpio_chip *gc,
				       unsigned int offset, int value)
{
	const struct omnia_gpio *gpio = &omnia_gpios[offset];
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	u16 val, mask;

	if (!gpio->ctl_cmd)
		return -ENOTSUPP;

	mask = BIT(gpio->ctl_bit);
	val = value ? mask : 0;

	if (offset == OMNIA_GPIO_PHY_SFP_OFFSET)
		mask |= OMNIA_EXT_CTL_PHY_SFP_AUTO;

	return omnia_ctl_cmd(mcu, gpio->ctl_cmd, val, mask);
}

static int omnia_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	const struct omnia_gpio *gpio = &omnia_gpios[offset];
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	/*
	 * If firmware does not support the new interrupt API, we are informed
	 * of every change of the status word by an interrupt from MCU and save
	 * its value in the interrupt service routine. Simply return the saved
	 * value.
	 */
	if (gpio->cmd == OMNIA_CMD_GET_STATUS_WORD &&
	    !(mcu->features & OMNIA_FEAT_NEW_INT_API))
		return test_bit(gpio->bit, &mcu->last_status);

	guard(mutex)(&mcu->lock);

	/*
	 * If firmware does support the new interrupt API, we may have cached
	 * the value of a GPIO in the interrupt service routine. If not, read
	 * the relevant bit now.
	 */
	if (is_int_bit_valid(gpio) && test_bit(gpio->int_bit, &mcu->is_cached))
		return test_bit(gpio->int_bit, &mcu->cached);

	return omnia_cmd_read_bit(mcu->client, gpio->cmd, BIT(gpio->bit));
}

static unsigned long *
_relevant_field_for_sts_cmd(u8 cmd, unsigned long *sts, unsigned long *ext_sts,
			    unsigned long *ext_ctl)
{
	switch (cmd) {
	case OMNIA_CMD_GET_STATUS_WORD:
		return sts;
	case OMNIA_CMD_GET_EXT_STATUS_DWORD:
		return ext_sts;
	case OMNIA_CMD_GET_EXT_CONTROL_STATUS:
		return ext_ctl;
	default:
		return NULL;
	}
}

static int omnia_gpio_get_multiple(struct gpio_chip *gc, unsigned long *mask,
				   unsigned long *bits)
{
	unsigned long sts = 0, ext_sts = 0, ext_ctl = 0, *field;
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	struct i2c_client *client = mcu->client;
	unsigned int i;
	int err;

	/* determine which bits to read from the 3 possible commands */
	for_each_set_bit(i, mask, ARRAY_SIZE(omnia_gpios)) {
		field = _relevant_field_for_sts_cmd(omnia_gpios[i].cmd,
						    &sts, &ext_sts, &ext_ctl);
		if (!field)
			continue;

		__set_bit(omnia_gpios[i].bit, field);
	}

	guard(mutex)(&mcu->lock);

	if (mcu->features & OMNIA_FEAT_NEW_INT_API) {
		/* read relevant bits from status */
		err = omnia_cmd_read_bits(client, OMNIA_CMD_GET_STATUS_WORD,
					  sts, &sts);
		if (err)
			return err;
	} else {
		/*
		 * Use status word value cached in the interrupt service routine
		 * if firmware does not support the new interrupt API.
		 */
		sts = mcu->last_status;
	}

	/* read relevant bits from extended status */
	err = omnia_cmd_read_bits(client, OMNIA_CMD_GET_EXT_STATUS_DWORD,
				  ext_sts, &ext_sts);
	if (err)
		return err;

	/* read relevant bits from extended control */
	err = omnia_cmd_read_bits(client, OMNIA_CMD_GET_EXT_CONTROL_STATUS,
				  ext_ctl, &ext_ctl);
	if (err)
		return err;

	/* assign relevant bits in result */
	for_each_set_bit(i, mask, ARRAY_SIZE(omnia_gpios)) {
		field = _relevant_field_for_sts_cmd(omnia_gpios[i].cmd,
						    &sts, &ext_sts, &ext_ctl);
		if (!field)
			continue;

		__assign_bit(i, bits, test_bit(omnia_gpios[i].bit, field));
	}

	return 0;
}

static int omnia_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	const struct omnia_gpio *gpio = &omnia_gpios[offset];
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	u16 val, mask;

	if (!gpio->ctl_cmd)
		return -EINVAL;

	mask = BIT(gpio->ctl_bit);
	val = value ? mask : 0;

	return omnia_ctl_cmd(mcu, gpio->ctl_cmd, val, mask);
}

static int omnia_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				   unsigned long *bits)
{
	unsigned long ctl = 0, ctl_mask = 0, ext_ctl = 0, ext_ctl_mask = 0;
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	unsigned int i;
	int err;

	for_each_set_bit(i, mask, ARRAY_SIZE(omnia_gpios)) {
		unsigned long *field, *field_mask;
		u8 bit = omnia_gpios[i].ctl_bit;

		switch (omnia_gpios[i].ctl_cmd) {
		case OMNIA_CMD_GENERAL_CONTROL:
			field = &ctl;
			field_mask = &ctl_mask;
			break;
		case OMNIA_CMD_EXT_CONTROL:
			field = &ext_ctl;
			field_mask = &ext_ctl_mask;
			break;
		default:
			field = field_mask = NULL;
			break;
		}

		if (!field)
			continue;

		__set_bit(bit, field_mask);
		__assign_bit(bit, field, test_bit(i, bits));
	}

	guard(mutex)(&mcu->lock);

	if (ctl_mask) {
		err = omnia_ctl_cmd_locked(mcu, OMNIA_CMD_GENERAL_CONTROL,
					   ctl, ctl_mask);
		if (err)
			return err;
	}

	if (ext_ctl_mask) {
		err = omnia_ctl_cmd_locked(mcu, OMNIA_CMD_EXT_CONTROL,
					   ext_ctl, ext_ctl_mask);
		if (err)
			return err;
	}

	return 0;
}

static bool omnia_gpio_available(struct omnia_mcu *mcu,
				 const struct omnia_gpio *gpio)
{
	if (gpio->feat_mask)
		return (mcu->features & gpio->feat_mask) == gpio->feat;

	if (gpio->feat)
		return mcu->features & gpio->feat;

	return true;
}

static int omnia_gpio_init_valid_mask(struct gpio_chip *gc,
				      unsigned long *valid_mask,
				      unsigned int ngpios)
{
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	for (unsigned int i = 0; i < ngpios; i++) {
		const struct omnia_gpio *gpio = &omnia_gpios[i];

		if (gpio->cmd || is_int_bit_valid(gpio))
			__assign_bit(i, valid_mask,
				     omnia_gpio_available(mcu, gpio));
		else
			__clear_bit(i, valid_mask);
	}

	return 0;
}

static int omnia_gpio_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	u32 bank, gpio;

	if (WARN_ON(gpiospec->args_count != 3))
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[2];

	bank = gpiospec->args[0];
	gpio = gpiospec->args[1];

	switch (bank) {
	case 0:
		return gpio < 16 ? gpio : -EINVAL;
	case 1:
		return gpio < 32 ? 16 + gpio : -EINVAL;
	case 2:
		return gpio < 16 ? 48 + gpio : -EINVAL;
	default:
		return -EINVAL;
	}
}

static void omnia_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u8 bit = omnia_gpios[hwirq].int_bit;

	__clear_bit(bit, &mcu->rising);
	__clear_bit(bit, &mcu->falling);
}

static void omnia_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u8 bit = omnia_gpios[hwirq].int_bit;

	if (!omnia_gpios[hwirq].cmd)
		__clear_bit(bit, &mcu->rising);
	__clear_bit(bit, &mcu->mask);
	gpiochip_disable_irq(gc, hwirq);
}

static void omnia_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u8 bit = omnia_gpios[hwirq].int_bit;

	gpiochip_enable_irq(gc, hwirq);
	__set_bit(bit, &mcu->mask);
	if (!omnia_gpios[hwirq].cmd)
		__set_bit(bit, &mcu->rising);
}

static int omnia_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct device *dev = &mcu->client->dev;
	u8 bit = omnia_gpios[hwirq].int_bit;

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(dev, "irq %u: unsupported type %u\n", d->irq, type);
		return -EINVAL;
	}

	__assign_bit(bit, &mcu->rising, type & IRQ_TYPE_EDGE_RISING);
	__assign_bit(bit, &mcu->falling, type & IRQ_TYPE_EDGE_FALLING);

	return 0;
}

static void omnia_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	/* nothing to do if MCU firmware does not support new interrupt API */
	if (!(mcu->features & OMNIA_FEAT_NEW_INT_API))
		return;

	mutex_lock(&mcu->lock);
}

/**
 * omnia_mask_interleave - Interleaves the bytes from @rising and @falling
 * @dst: the destination u8 array of interleaved bytes
 * @rising: rising mask
 * @falling: falling mask
 *
 * Interleaves the little-endian bytes from @rising and @falling words.
 *
 * If @rising = (r0, r1, r2, r3) and @falling = (f0, f1, f2, f3), the result is
 * @dst = (r0, f0, r1, f1, r2, f2, r3, f3).
 *
 * The MCU receives an interrupt mask and reports a pending interrupt bitmap in
 * this interleaved format. The rationale behind this is that the low-indexed
 * bits are more important - in many cases, the user will be interested only in
 * interrupts with indexes 0 to 7, and so the system can stop reading after
 * first 2 bytes (r0, f0), to save time on the slow I2C bus.
 *
 * Feel free to remove this function and its inverse, omnia_mask_deinterleave,
 * and use an appropriate bitmap_*() function once such a function exists.
 */
static void
omnia_mask_interleave(u8 *dst, unsigned long rising, unsigned long falling)
{
	for (unsigned int i = 0; i < sizeof(u32); i++) {
		dst[2 * i] = rising >> (8 * i);
		dst[2 * i + 1] = falling >> (8 * i);
	}
}

/**
 * omnia_mask_deinterleave - Deinterleaves the bytes into @rising and @falling
 * @src: the source u8 array containing the interleaved bytes
 * @rising: pointer where to store the rising mask gathered from @src
 * @falling: pointer where to store the falling mask gathered from @src
 *
 * This is the inverse function to omnia_mask_interleave.
 */
static void omnia_mask_deinterleave(const u8 *src, unsigned long *rising,
				    unsigned long *falling)
{
	*rising = *falling = 0;

	for (unsigned int i = 0; i < sizeof(u32); i++) {
		*rising |= src[2 * i] << (8 * i);
		*falling |= src[2 * i + 1] << (8 * i);
	}
}

static void omnia_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	struct device *dev = &mcu->client->dev;
	u8 cmd[1 + OMNIA_CMD_INT_ARG_LEN];
	unsigned long rising, falling;
	int err;

	/* nothing to do if MCU firmware does not support new interrupt API */
	if (!(mcu->features & OMNIA_FEAT_NEW_INT_API))
		return;

	cmd[0] = OMNIA_CMD_SET_INT_MASK;

	rising = mcu->rising & mcu->mask;
	falling = mcu->falling & mcu->mask;

	/* interleave the rising and falling bytes into the command arguments */
	omnia_mask_interleave(&cmd[1], rising, falling);

	dev_dbg(dev, "set int mask %8ph\n", &cmd[1]);

	err = omnia_cmd_write(mcu->client, cmd, sizeof(cmd));
	if (err) {
		dev_err(dev, "Cannot set mask: %d\n", err);
		goto unlock;
	}

	/*
	 * Remember which GPIOs have both rising and falling interrupts enabled.
	 * For those we will cache their value so that .get() method is faster.
	 * We also need to forget cached values of GPIOs that aren't cached
	 * anymore.
	 */
	mcu->both = rising & falling;
	mcu->is_cached &= mcu->both;

unlock:
	mutex_unlock(&mcu->lock);
}

static const struct irq_chip omnia_mcu_irq_chip = {
	.name			= "Turris Omnia MCU interrupts",
	.irq_shutdown		= omnia_irq_shutdown,
	.irq_mask		= omnia_irq_mask,
	.irq_unmask		= omnia_irq_unmask,
	.irq_set_type		= omnia_irq_set_type,
	.irq_bus_lock		= omnia_irq_bus_lock,
	.irq_bus_sync_unlock	= omnia_irq_bus_sync_unlock,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void omnia_irq_init_valid_mask(struct gpio_chip *gc,
				      unsigned long *valid_mask,
				      unsigned int ngpios)
{
	struct omnia_mcu *mcu = gpiochip_get_data(gc);

	for (unsigned int i = 0; i < ngpios; i++) {
		const struct omnia_gpio *gpio = &omnia_gpios[i];

		if (is_int_bit_valid(gpio))
			__assign_bit(i, valid_mask,
				     omnia_gpio_available(mcu, gpio));
		else
			__clear_bit(i, valid_mask);
	}
}

static int omnia_irq_init_hw(struct gpio_chip *gc)
{
	struct omnia_mcu *mcu = gpiochip_get_data(gc);
	u8 cmd[1 + OMNIA_CMD_INT_ARG_LEN] = {};

	cmd[0] = OMNIA_CMD_SET_INT_MASK;

	return omnia_cmd_write(mcu->client, cmd, sizeof(cmd));
}

/*
 * Determine how many bytes we need to read from the reply to the
 * OMNIA_CMD_GET_INT_AND_CLEAR command in order to retrieve all unmasked
 * interrupts.
 */
static unsigned int
omnia_irq_compute_pending_length(unsigned long rising, unsigned long falling)
{
	return max(omnia_compute_reply_length(rising, true, 0),
		   omnia_compute_reply_length(falling, true, 1));
}

static bool omnia_irq_read_pending_new(struct omnia_mcu *mcu,
				       unsigned long *pending)
{
	struct device *dev = &mcu->client->dev;
	u8 reply[OMNIA_CMD_INT_ARG_LEN] = {};
	unsigned long rising, falling;
	unsigned int len;
	int err;

	len = omnia_irq_compute_pending_length(mcu->rising & mcu->mask,
					       mcu->falling & mcu->mask);
	if (!len)
		return false;

	guard(mutex)(&mcu->lock);

	err = omnia_cmd_read(mcu->client, OMNIA_CMD_GET_INT_AND_CLEAR, reply,
			     len);
	if (err) {
		dev_err(dev, "Cannot read pending IRQs: %d\n", err);
		return false;
	}

	/* deinterleave the reply bytes into rising and falling */
	omnia_mask_deinterleave(reply, &rising, &falling);

	rising &= mcu->mask;
	falling &= mcu->mask;
	*pending = rising | falling;

	/* cache values for GPIOs that have both edges enabled */
	mcu->is_cached &= ~(rising & falling);
	mcu->is_cached |= mcu->both & (rising ^ falling);
	mcu->cached = (mcu->cached | rising) & ~falling;

	return true;
}

static int omnia_read_status_word_old_fw(struct omnia_mcu *mcu,
					 unsigned long *status)
{
	u16 raw_status;
	int err;

	err = omnia_cmd_read_u16(mcu->client, OMNIA_CMD_GET_STATUS_WORD,
				 &raw_status);
	if (err)
		return err;

	/*
	 * Old firmware has a bug wherein it never resets the USB port
	 * overcurrent bits back to zero. Ignore them.
	 */
	*status = raw_status & ~(OMNIA_STS_USB30_OVC | OMNIA_STS_USB31_OVC);

	return 0;
}

static void button_release_emul_fn(struct work_struct *work)
{
	struct omnia_mcu *mcu = container_of(to_delayed_work(work),
					     struct omnia_mcu,
					     button_release_emul_work);

	mcu->button_pressed_emul = false;
	generic_handle_irq_safe(mcu->client->irq);
}

static void
fill_int_from_sts(unsigned long *rising, unsigned long *falling,
		  unsigned long rising_sts, unsigned long falling_sts,
		  unsigned long sts_bit, unsigned long int_bit)
{
	if (rising_sts & sts_bit)
		*rising |= int_bit;
	if (falling_sts & sts_bit)
		*falling |= int_bit;
}

static bool omnia_irq_read_pending_old(struct omnia_mcu *mcu,
				       unsigned long *pending)
{
	unsigned long status, rising_sts, falling_sts, rising, falling;
	struct device *dev = &mcu->client->dev;
	int err;

	guard(mutex)(&mcu->lock);

	err = omnia_read_status_word_old_fw(mcu, &status);
	if (err) {
		dev_err(dev, "Cannot read pending IRQs: %d\n", err);
		return false;
	}

	/*
	 * The old firmware triggers an interrupt whenever status word changes,
	 * but does not inform about which bits rose or fell. We need to compute
	 * this here by comparing with the last status word value.
	 *
	 * The OMNIA_STS_BUTTON_PRESSED bit needs special handling, because the
	 * old firmware clears the OMNIA_STS_BUTTON_PRESSED bit on successful
	 * completion of the OMNIA_CMD_GET_STATUS_WORD command, resulting in
	 * another interrupt:
	 * - first we get an interrupt, we read the status word where
	 *   OMNIA_STS_BUTTON_PRESSED is present,
	 * - MCU clears the OMNIA_STS_BUTTON_PRESSED bit because we read the
	 *   status word,
	 * - we get another interrupt because the status word changed again
	 *   (the OMNIA_STS_BUTTON_PRESSED bit was cleared).
	 *
	 * The gpiolib-cdev, gpiolib-sysfs and gpio-keys input driver all call
	 * the gpiochip's .get() method after an edge event on a requested GPIO
	 * occurs.
	 *
	 * We ensure that the .get() method reads 1 for the button GPIO for some
	 * time.
	 */

	if (status & OMNIA_STS_BUTTON_PRESSED) {
		mcu->button_pressed_emul = true;
		mod_delayed_work(system_wq, &mcu->button_release_emul_work,
				 msecs_to_jiffies(FRONT_BUTTON_RELEASE_DELAY_MS));
	} else if (mcu->button_pressed_emul) {
		status |= OMNIA_STS_BUTTON_PRESSED;
	}

	rising_sts = ~mcu->last_status & status;
	falling_sts = mcu->last_status & ~status;

	mcu->last_status = status;

	/*
	 * Fill in the relevant interrupt bits from status bits for CARD_DET,
	 * MSATA_IND and BUTTON_PRESSED.
	 */
	rising = 0;
	falling = 0;
	fill_int_from_sts(&rising, &falling, rising_sts, falling_sts,
			  OMNIA_STS_CARD_DET, OMNIA_INT_CARD_DET);
	fill_int_from_sts(&rising, &falling, rising_sts, falling_sts,
			  OMNIA_STS_MSATA_IND, OMNIA_INT_MSATA_IND);
	fill_int_from_sts(&rising, &falling, rising_sts, falling_sts,
			  OMNIA_STS_BUTTON_PRESSED, OMNIA_INT_BUTTON_PRESSED);

	/* Use only bits that are enabled */
	rising &= mcu->rising & mcu->mask;
	falling &= mcu->falling & mcu->mask;
	*pending = rising | falling;

	return true;
}

static bool omnia_irq_read_pending(struct omnia_mcu *mcu,
				   unsigned long *pending)
{
	if (mcu->features & OMNIA_FEAT_NEW_INT_API)
		return omnia_irq_read_pending_new(mcu, pending);
	else
		return omnia_irq_read_pending_old(mcu, pending);
}

static irqreturn_t omnia_irq_thread_handler(int irq, void *dev_id)
{
	struct omnia_mcu *mcu = dev_id;
	struct irq_domain *domain;
	unsigned long pending;
	unsigned int i;

	if (!omnia_irq_read_pending(mcu, &pending))
		return IRQ_NONE;

	domain = mcu->gc.irq.domain;

	for_each_set_bit(i, &pending, 32) {
		unsigned int nested_irq;

		nested_irq = irq_find_mapping(domain, omnia_int_to_gpio_idx[i]);

		handle_nested_irq(nested_irq);
	}

	return IRQ_RETVAL(pending);
}

static const char * const front_button_modes[] = { "mcu", "cpu" };

static ssize_t front_button_mode_show(struct device *dev,
				      struct device_attribute *a, char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);
	int val;

	if (mcu->features & OMNIA_FEAT_NEW_INT_API) {
		val = omnia_cmd_read_bit(mcu->client, OMNIA_CMD_GET_STATUS_WORD,
					 OMNIA_STS_BUTTON_MODE);
		if (val < 0)
			return val;
	} else {
		val = !!(mcu->last_status & OMNIA_STS_BUTTON_MODE);
	}

	return sysfs_emit(buf, "%s\n", front_button_modes[val]);
}

static ssize_t front_button_mode_store(struct device *dev,
				       struct device_attribute *a,
				       const char *buf, size_t count)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);
	int err, i;

	i = sysfs_match_string(front_button_modes, buf);
	if (i < 0)
		return i;

	err = omnia_ctl_cmd_locked(mcu, OMNIA_CMD_GENERAL_CONTROL,
				   i ? OMNIA_CTL_BUTTON_MODE : 0,
				   OMNIA_CTL_BUTTON_MODE);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(front_button_mode);

static struct attribute *omnia_mcu_gpio_attrs[] = {
	&dev_attr_front_button_mode.attr,
	NULL
};

const struct attribute_group omnia_mcu_gpio_group = {
	.attrs = omnia_mcu_gpio_attrs,
};

int omnia_mcu_register_gpiochip(struct omnia_mcu *mcu)
{
	bool new_api = mcu->features & OMNIA_FEAT_NEW_INT_API;
	struct device *dev = &mcu->client->dev;
	unsigned long irqflags;
	int err;

	err = devm_mutex_init(dev, &mcu->lock);
	if (err)
		return err;

	mcu->gc.request = omnia_gpio_request;
	mcu->gc.get_direction = omnia_gpio_get_direction;
	mcu->gc.direction_input = omnia_gpio_direction_input;
	mcu->gc.direction_output = omnia_gpio_direction_output;
	mcu->gc.get = omnia_gpio_get;
	mcu->gc.get_multiple = omnia_gpio_get_multiple;
	mcu->gc.set = omnia_gpio_set;
	mcu->gc.set_multiple = omnia_gpio_set_multiple;
	mcu->gc.init_valid_mask = omnia_gpio_init_valid_mask;
	mcu->gc.can_sleep = true;
	mcu->gc.names = omnia_mcu_gpio_names;
	mcu->gc.base = -1;
	mcu->gc.ngpio = ARRAY_SIZE(omnia_gpios);
	mcu->gc.label = "Turris Omnia MCU GPIOs";
	mcu->gc.parent = dev;
	mcu->gc.owner = THIS_MODULE;
	mcu->gc.of_gpio_n_cells = 3;
	mcu->gc.of_xlate = omnia_gpio_of_xlate;

	gpio_irq_chip_set_chip(&mcu->gc.irq, &omnia_mcu_irq_chip);
	/* This will let us handle the parent IRQ in the driver */
	mcu->gc.irq.parent_handler = NULL;
	mcu->gc.irq.num_parents = 0;
	mcu->gc.irq.parents = NULL;
	mcu->gc.irq.default_type = IRQ_TYPE_NONE;
	mcu->gc.irq.handler = handle_bad_irq;
	mcu->gc.irq.threaded = true;
	if (new_api)
		mcu->gc.irq.init_hw = omnia_irq_init_hw;
	mcu->gc.irq.init_valid_mask = omnia_irq_init_valid_mask;

	err = devm_gpiochip_add_data(dev, &mcu->gc, mcu);
	if (err)
		return dev_err_probe(dev, err, "Cannot add GPIO chip\n");

	/*
	 * Before requesting the interrupt, if firmware does not support the new
	 * interrupt API, we need to cache the value of the status word, so that
	 * when it changes, we may compare the new value with the cached one in
	 * the interrupt handler.
	 */
	if (!new_api) {
		err = omnia_read_status_word_old_fw(mcu, &mcu->last_status);
		if (err)
			return dev_err_probe(dev, err,
					     "Cannot read status word\n");

		INIT_DELAYED_WORK(&mcu->button_release_emul_work,
				  button_release_emul_fn);
	}

	irqflags = IRQF_ONESHOT;
	if (new_api)
		irqflags |= IRQF_TRIGGER_LOW;
	else
		irqflags |= IRQF_TRIGGER_FALLING;

	err = devm_request_threaded_irq(dev, mcu->client->irq, NULL,
					omnia_irq_thread_handler, irqflags,
					"turris-omnia-mcu", mcu);
	if (err)
		return dev_err_probe(dev, err, "Cannot request IRQ\n");

	if (!new_api) {
		/*
		 * The button_release_emul_work has to be initialized before the
		 * thread is requested, and on driver remove it needs to be
		 * canceled before the thread is freed. Therefore we can't use
		 * devm_delayed_work_autocancel() directly, because the order
		 *   devm_delayed_work_autocancel();
		 *   devm_request_threaded_irq();
		 * would cause improper release order:
		 *   free_irq();
		 *   cancel_delayed_work_sync();
		 * Instead we first initialize the work above, and only now
		 * after IRQ is requested we add the work devm action.
		 */
		err = devm_add_action(dev, devm_delayed_work_drop,
				      &mcu->button_release_emul_work);
		if (err)
			return err;
	}

	return 0;
}

int omnia_mcu_request_irq(struct omnia_mcu *mcu, u32 spec,
			  irq_handler_t thread_fn, const char *devname)
{
	u8 irq_idx;
	int irq;

	if (!spec)
		return -EINVAL;

	irq_idx = omnia_int_to_gpio_idx[ffs(spec) - 1];
	irq = gpiod_to_irq(gpio_device_get_desc(mcu->gc.gpiodev, irq_idx));
	if (irq < 0)
		return irq;

	return devm_request_threaded_irq(&mcu->client->dev, irq, NULL,
					 thread_fn, IRQF_ONESHOT, devname, mcu);
}
