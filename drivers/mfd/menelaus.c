/*
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * Some parts based tps65010.c:
 * Copyright (C) 2004 Texas Instruments and
 * Copyright (C) 2004-2005 David Brownell
 *
 * Some parts based on tlv320aic24.c:
 * Copyright (C) by Kai Svahn <kai.svahn@nokia.com>
 *
 * Changes for interrupt handling and clean-up by
 * Tony Lindgren <tony@atomide.com> and Imre Deak <imre.deak@nokia.com>
 * Cleanup and generalized support for voltage setting by
 * Juha Yrjola
 * Added support for controlling VCORE and regulator sleep states,
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Copyright (C) 2005, 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/slab.h>

#include <asm/mach/irq.h>

#include <mach/gpio.h>
#include <plat/menelaus.h>

#define DRIVER_NAME			"menelaus"

#define MENELAUS_I2C_ADDRESS		0x72

#define MENELAUS_REV			0x01
#define MENELAUS_VCORE_CTRL1		0x02
#define MENELAUS_VCORE_CTRL2		0x03
#define MENELAUS_VCORE_CTRL3		0x04
#define MENELAUS_VCORE_CTRL4		0x05
#define MENELAUS_VCORE_CTRL5		0x06
#define MENELAUS_DCDC_CTRL1		0x07
#define MENELAUS_DCDC_CTRL2		0x08
#define MENELAUS_DCDC_CTRL3		0x09
#define MENELAUS_LDO_CTRL1		0x0A
#define MENELAUS_LDO_CTRL2		0x0B
#define MENELAUS_LDO_CTRL3		0x0C
#define MENELAUS_LDO_CTRL4		0x0D
#define MENELAUS_LDO_CTRL5		0x0E
#define MENELAUS_LDO_CTRL6		0x0F
#define MENELAUS_LDO_CTRL7		0x10
#define MENELAUS_LDO_CTRL8		0x11
#define MENELAUS_SLEEP_CTRL1		0x12
#define MENELAUS_SLEEP_CTRL2		0x13
#define MENELAUS_DEVICE_OFF		0x14
#define MENELAUS_OSC_CTRL		0x15
#define MENELAUS_DETECT_CTRL		0x16
#define MENELAUS_INT_MASK1		0x17
#define MENELAUS_INT_MASK2		0x18
#define MENELAUS_INT_STATUS1		0x19
#define MENELAUS_INT_STATUS2		0x1A
#define MENELAUS_INT_ACK1		0x1B
#define MENELAUS_INT_ACK2		0x1C
#define MENELAUS_GPIO_CTRL		0x1D
#define MENELAUS_GPIO_IN		0x1E
#define MENELAUS_GPIO_OUT		0x1F
#define MENELAUS_BBSMS			0x20
#define MENELAUS_RTC_CTRL		0x21
#define MENELAUS_RTC_UPDATE		0x22
#define MENELAUS_RTC_SEC		0x23
#define MENELAUS_RTC_MIN		0x24
#define MENELAUS_RTC_HR			0x25
#define MENELAUS_RTC_DAY		0x26
#define MENELAUS_RTC_MON		0x27
#define MENELAUS_RTC_YR			0x28
#define MENELAUS_RTC_WKDAY		0x29
#define MENELAUS_RTC_AL_SEC		0x2A
#define MENELAUS_RTC_AL_MIN		0x2B
#define MENELAUS_RTC_AL_HR		0x2C
#define MENELAUS_RTC_AL_DAY		0x2D
#define MENELAUS_RTC_AL_MON		0x2E
#define MENELAUS_RTC_AL_YR		0x2F
#define MENELAUS_RTC_COMP_MSB		0x30
#define MENELAUS_RTC_COMP_LSB		0x31
#define MENELAUS_S1_PULL_EN		0x32
#define MENELAUS_S1_PULL_DIR		0x33
#define MENELAUS_S2_PULL_EN		0x34
#define MENELAUS_S2_PULL_DIR		0x35
#define MENELAUS_MCT_CTRL1		0x36
#define MENELAUS_MCT_CTRL2		0x37
#define MENELAUS_MCT_CTRL3		0x38
#define MENELAUS_MCT_PIN_ST		0x39
#define MENELAUS_DEBOUNCE1		0x3A

#define IH_MENELAUS_IRQS		12
#define MENELAUS_MMC_S1CD_IRQ		0	/* MMC slot 1 card change */
#define MENELAUS_MMC_S2CD_IRQ		1	/* MMC slot 2 card change */
#define MENELAUS_MMC_S1D1_IRQ		2	/* MMC DAT1 low in slot 1 */
#define MENELAUS_MMC_S2D1_IRQ		3	/* MMC DAT1 low in slot 2 */
#define MENELAUS_LOWBAT_IRQ		4	/* Low battery */
#define MENELAUS_HOTDIE_IRQ		5	/* Hot die detect */
#define MENELAUS_UVLO_IRQ		6	/* UVLO detect */
#define MENELAUS_TSHUT_IRQ		7	/* Thermal shutdown */
#define MENELAUS_RTCTMR_IRQ		8	/* RTC timer */
#define MENELAUS_RTCALM_IRQ		9	/* RTC alarm */
#define MENELAUS_RTCERR_IRQ		10	/* RTC error */
#define MENELAUS_PSHBTN_IRQ		11	/* Push button */
#define MENELAUS_RESERVED12_IRQ		12	/* Reserved */
#define MENELAUS_RESERVED13_IRQ		13	/* Reserved */
#define MENELAUS_RESERVED14_IRQ		14	/* Reserved */
#define MENELAUS_RESERVED15_IRQ		15	/* Reserved */

/* VCORE_CTRL1 register */
#define VCORE_CTRL1_BYP_COMP		(1 << 5)
#define VCORE_CTRL1_HW_NSW		(1 << 7)

/* GPIO_CTRL register */
#define GPIO_CTRL_SLOTSELEN		(1 << 5)
#define GPIO_CTRL_SLPCTLEN		(1 << 6)
#define GPIO1_DIR_INPUT			(1 << 0)
#define GPIO2_DIR_INPUT			(1 << 1)
#define GPIO3_DIR_INPUT			(1 << 2)

/* MCT_CTRL1 register */
#define MCT_CTRL1_S1_CMD_OD		(1 << 2)
#define MCT_CTRL1_S2_CMD_OD		(1 << 3)

/* MCT_CTRL2 register */
#define MCT_CTRL2_VS2_SEL_D0		(1 << 0)
#define MCT_CTRL2_VS2_SEL_D1		(1 << 1)
#define MCT_CTRL2_S1CD_BUFEN		(1 << 4)
#define MCT_CTRL2_S2CD_BUFEN		(1 << 5)
#define MCT_CTRL2_S1CD_DBEN		(1 << 6)
#define MCT_CTRL2_S2CD_BEN		(1 << 7)

/* MCT_CTRL3 register */
#define MCT_CTRL3_SLOT1_EN		(1 << 0)
#define MCT_CTRL3_SLOT2_EN		(1 << 1)
#define MCT_CTRL3_S1_AUTO_EN		(1 << 2)
#define MCT_CTRL3_S2_AUTO_EN		(1 << 3)

/* MCT_PIN_ST register */
#define MCT_PIN_ST_S1_CD_ST		(1 << 0)
#define MCT_PIN_ST_S2_CD_ST		(1 << 1)

static void menelaus_work(struct work_struct *_menelaus);

struct menelaus_chip {
	struct mutex		lock;
	struct i2c_client	*client;
	struct work_struct	work;
#ifdef CONFIG_RTC_DRV_TWL92330
	struct rtc_device	*rtc;
	u8			rtc_control;
	unsigned		uie:1;
#endif
	unsigned		vcore_hw_mode:1;
	u8			mask1, mask2;
	void			(*handlers[16])(struct menelaus_chip *);
	void			(*mmc_callback)(void *data, u8 mask);
	void			*mmc_callback_data;
};

static struct menelaus_chip *the_menelaus;

static int menelaus_write_reg(int reg, u8 value)
{
	int val = i2c_smbus_write_byte_data(the_menelaus->client, reg, value);

	if (val < 0) {
		pr_err(DRIVER_NAME ": write error");
		return val;
	}

	return 0;
}

static int menelaus_read_reg(int reg)
{
	int val = i2c_smbus_read_byte_data(the_menelaus->client, reg);

	if (val < 0)
		pr_err(DRIVER_NAME ": read error");

	return val;
}

static int menelaus_enable_irq(int irq)
{
	if (irq > 7) {
		irq -= 8;
		the_menelaus->mask2 &= ~(1 << irq);
		return menelaus_write_reg(MENELAUS_INT_MASK2,
				the_menelaus->mask2);
	} else {
		the_menelaus->mask1 &= ~(1 << irq);
		return menelaus_write_reg(MENELAUS_INT_MASK1,
				the_menelaus->mask1);
	}
}

static int menelaus_disable_irq(int irq)
{
	if (irq > 7) {
		irq -= 8;
		the_menelaus->mask2 |= (1 << irq);
		return menelaus_write_reg(MENELAUS_INT_MASK2,
				the_menelaus->mask2);
	} else {
		the_menelaus->mask1 |= (1 << irq);
		return menelaus_write_reg(MENELAUS_INT_MASK1,
				the_menelaus->mask1);
	}
}

static int menelaus_ack_irq(int irq)
{
	if (irq > 7)
		return menelaus_write_reg(MENELAUS_INT_ACK2, 1 << (irq - 8));
	else
		return menelaus_write_reg(MENELAUS_INT_ACK1, 1 << irq);
}

/* Adds a handler for an interrupt. Does not run in interrupt context */
static int menelaus_add_irq_work(int irq,
		void (*handler)(struct menelaus_chip *))
{
	int ret = 0;

	mutex_lock(&the_menelaus->lock);
	the_menelaus->handlers[irq] = handler;
	ret = menelaus_enable_irq(irq);
	mutex_unlock(&the_menelaus->lock);

	return ret;
}

/* Removes handler for an interrupt */
static int menelaus_remove_irq_work(int irq)
{
	int ret = 0;

	mutex_lock(&the_menelaus->lock);
	ret = menelaus_disable_irq(irq);
	the_menelaus->handlers[irq] = NULL;
	mutex_unlock(&the_menelaus->lock);

	return ret;
}

/*
 * Gets scheduled when a card detect interrupt happens. Note that in some cases
 * this line is wired to card cover switch rather than the card detect switch
 * in each slot. In this case the cards are not seen by menelaus.
 * FIXME: Add handling for D1 too
 */
static void menelaus_mmc_cd_work(struct menelaus_chip *menelaus_hw)
{
	int reg;
	unsigned char card_mask = 0;

	reg = menelaus_read_reg(MENELAUS_MCT_PIN_ST);
	if (reg < 0)
		return;

	if (!(reg & 0x1))
		card_mask |= MCT_PIN_ST_S1_CD_ST;

	if (!(reg & 0x2))
		card_mask |= MCT_PIN_ST_S2_CD_ST;

	if (menelaus_hw->mmc_callback)
		menelaus_hw->mmc_callback(menelaus_hw->mmc_callback_data,
					  card_mask);
}

/*
 * Toggles the MMC slots between open-drain and push-pull mode.
 */
int menelaus_set_mmc_opendrain(int slot, int enable)
{
	int ret, val;

	if (slot != 1 && slot != 2)
		return -EINVAL;
	mutex_lock(&the_menelaus->lock);
	ret = menelaus_read_reg(MENELAUS_MCT_CTRL1);
	if (ret < 0) {
		mutex_unlock(&the_menelaus->lock);
		return ret;
	}
	val = ret;
	if (slot == 1) {
		if (enable)
			val |= MCT_CTRL1_S1_CMD_OD;
		else
			val &= ~MCT_CTRL1_S1_CMD_OD;
	} else {
		if (enable)
			val |= MCT_CTRL1_S2_CMD_OD;
		else
			val &= ~MCT_CTRL1_S2_CMD_OD;
	}
	ret = menelaus_write_reg(MENELAUS_MCT_CTRL1, val);
	mutex_unlock(&the_menelaus->lock);

	return ret;
}
EXPORT_SYMBOL(menelaus_set_mmc_opendrain);

int menelaus_set_slot_sel(int enable)
{
	int ret;

	mutex_lock(&the_menelaus->lock);
	ret = menelaus_read_reg(MENELAUS_GPIO_CTRL);
	if (ret < 0)
		goto out;
	ret |= GPIO2_DIR_INPUT;
	if (enable)
		ret |= GPIO_CTRL_SLOTSELEN;
	else
		ret &= ~GPIO_CTRL_SLOTSELEN;
	ret = menelaus_write_reg(MENELAUS_GPIO_CTRL, ret);
out:
	mutex_unlock(&the_menelaus->lock);
	return ret;
}
EXPORT_SYMBOL(menelaus_set_slot_sel);

int menelaus_set_mmc_slot(int slot, int enable, int power, int cd_en)
{
	int ret, val;

	if (slot != 1 && slot != 2)
		return -EINVAL;
	if (power >= 3)
		return -EINVAL;

	mutex_lock(&the_menelaus->lock);

	ret = menelaus_read_reg(MENELAUS_MCT_CTRL2);
	if (ret < 0)
		goto out;
	val = ret;
	if (slot == 1) {
		if (cd_en)
			val |= MCT_CTRL2_S1CD_BUFEN | MCT_CTRL2_S1CD_DBEN;
		else
			val &= ~(MCT_CTRL2_S1CD_BUFEN | MCT_CTRL2_S1CD_DBEN);
	} else {
		if (cd_en)
			val |= MCT_CTRL2_S2CD_BUFEN | MCT_CTRL2_S2CD_BEN;
		else
			val &= ~(MCT_CTRL2_S2CD_BUFEN | MCT_CTRL2_S2CD_BEN);
	}
	ret = menelaus_write_reg(MENELAUS_MCT_CTRL2, val);
	if (ret < 0)
		goto out;

	ret = menelaus_read_reg(MENELAUS_MCT_CTRL3);
	if (ret < 0)
		goto out;
	val = ret;
	if (slot == 1) {
		if (enable)
			val |= MCT_CTRL3_SLOT1_EN;
		else
			val &= ~MCT_CTRL3_SLOT1_EN;
	} else {
		int b;

		if (enable)
			val |= MCT_CTRL3_SLOT2_EN;
		else
			val &= ~MCT_CTRL3_SLOT2_EN;
		b = menelaus_read_reg(MENELAUS_MCT_CTRL2);
		b &= ~(MCT_CTRL2_VS2_SEL_D0 | MCT_CTRL2_VS2_SEL_D1);
		b |= power;
		ret = menelaus_write_reg(MENELAUS_MCT_CTRL2, b);
		if (ret < 0)
			goto out;
	}
	/* Disable autonomous shutdown */
	val &= ~(MCT_CTRL3_S1_AUTO_EN | MCT_CTRL3_S2_AUTO_EN);
	ret = menelaus_write_reg(MENELAUS_MCT_CTRL3, val);
out:
	mutex_unlock(&the_menelaus->lock);
	return ret;
}
EXPORT_SYMBOL(menelaus_set_mmc_slot);

int menelaus_register_mmc_callback(void (*callback)(void *data, u8 card_mask),
				   void *data)
{
	int ret = 0;

	the_menelaus->mmc_callback_data = data;
	the_menelaus->mmc_callback = callback;
	ret = menelaus_add_irq_work(MENELAUS_MMC_S1CD_IRQ,
				    menelaus_mmc_cd_work);
	if (ret < 0)
		return ret;
	ret = menelaus_add_irq_work(MENELAUS_MMC_S2CD_IRQ,
				    menelaus_mmc_cd_work);
	if (ret < 0)
		return ret;
	ret = menelaus_add_irq_work(MENELAUS_MMC_S1D1_IRQ,
				    menelaus_mmc_cd_work);
	if (ret < 0)
		return ret;
	ret = menelaus_add_irq_work(MENELAUS_MMC_S2D1_IRQ,
				    menelaus_mmc_cd_work);

	return ret;
}
EXPORT_SYMBOL(menelaus_register_mmc_callback);

void menelaus_unregister_mmc_callback(void)
{
	menelaus_remove_irq_work(MENELAUS_MMC_S1CD_IRQ);
	menelaus_remove_irq_work(MENELAUS_MMC_S2CD_IRQ);
	menelaus_remove_irq_work(MENELAUS_MMC_S1D1_IRQ);
	menelaus_remove_irq_work(MENELAUS_MMC_S2D1_IRQ);

	the_menelaus->mmc_callback = NULL;
	the_menelaus->mmc_callback_data = 0;
}
EXPORT_SYMBOL(menelaus_unregister_mmc_callback);

struct menelaus_vtg {
	const char *name;
	u8 vtg_reg;
	u8 vtg_shift;
	u8 vtg_bits;
	u8 mode_reg;
};

struct menelaus_vtg_value {
	u16 vtg;
	u16 val;
};

static int menelaus_set_voltage(const struct menelaus_vtg *vtg, int mV,
				int vtg_val, int mode)
{
	int val, ret;
	struct i2c_client *c = the_menelaus->client;

	mutex_lock(&the_menelaus->lock);
	if (vtg == 0)
		goto set_voltage;

	ret = menelaus_read_reg(vtg->vtg_reg);
	if (ret < 0)
		goto out;
	val = ret & ~(((1 << vtg->vtg_bits) - 1) << vtg->vtg_shift);
	val |= vtg_val << vtg->vtg_shift;

	dev_dbg(&c->dev, "Setting voltage '%s'"
			 "to %d mV (reg 0x%02x, val 0x%02x)\n",
			vtg->name, mV, vtg->vtg_reg, val);

	ret = menelaus_write_reg(vtg->vtg_reg, val);
	if (ret < 0)
		goto out;
set_voltage:
	ret = menelaus_write_reg(vtg->mode_reg, mode);
out:
	mutex_unlock(&the_menelaus->lock);
	if (ret == 0) {
		/* Wait for voltage to stabilize */
		msleep(1);
	}
	return ret;
}

static int menelaus_get_vtg_value(int vtg, const struct menelaus_vtg_value *tbl,
				  int n)
{
	int i;

	for (i = 0; i < n; i++, tbl++)
		if (tbl->vtg == vtg)
			return tbl->val;
	return -EINVAL;
}

/*
 * Vcore can be programmed in two ways:
 * SW-controlled: Required voltage is programmed into VCORE_CTRL1
 * HW-controlled: Required range (roof-floor) is programmed into VCORE_CTRL3
 * and VCORE_CTRL4
 *
 * Call correct 'set' function accordingly
 */

static const struct menelaus_vtg_value vcore_values[] = {
	{ 1000, 0 },
	{ 1025, 1 },
	{ 1050, 2 },
	{ 1075, 3 },
	{ 1100, 4 },
	{ 1125, 5 },
	{ 1150, 6 },
	{ 1175, 7 },
	{ 1200, 8 },
	{ 1225, 9 },
	{ 1250, 10 },
	{ 1275, 11 },
	{ 1300, 12 },
	{ 1325, 13 },
	{ 1350, 14 },
	{ 1375, 15 },
	{ 1400, 16 },
	{ 1425, 17 },
	{ 1450, 18 },
};

int menelaus_set_vcore_sw(unsigned int mV)
{
	int val, ret;
	struct i2c_client *c = the_menelaus->client;

	val = menelaus_get_vtg_value(mV, vcore_values,
				     ARRAY_SIZE(vcore_values));
	if (val < 0)
		return -EINVAL;

	dev_dbg(&c->dev, "Setting VCORE to %d mV (val 0x%02x)\n", mV, val);

	/* Set SW mode and the voltage in one go. */
	mutex_lock(&the_menelaus->lock);
	ret = menelaus_write_reg(MENELAUS_VCORE_CTRL1, val);
	if (ret == 0)
		the_menelaus->vcore_hw_mode = 0;
	mutex_unlock(&the_menelaus->lock);
	msleep(1);

	return ret;
}

int menelaus_set_vcore_hw(unsigned int roof_mV, unsigned int floor_mV)
{
	int fval, rval, val, ret;
	struct i2c_client *c = the_menelaus->client;

	rval = menelaus_get_vtg_value(roof_mV, vcore_values,
				      ARRAY_SIZE(vcore_values));
	if (rval < 0)
		return -EINVAL;
	fval = menelaus_get_vtg_value(floor_mV, vcore_values,
				      ARRAY_SIZE(vcore_values));
	if (fval < 0)
		return -EINVAL;

	dev_dbg(&c->dev, "Setting VCORE FLOOR to %d mV and ROOF to %d mV\n",
	       floor_mV, roof_mV);

	mutex_lock(&the_menelaus->lock);
	ret = menelaus_write_reg(MENELAUS_VCORE_CTRL3, fval);
	if (ret < 0)
		goto out;
	ret = menelaus_write_reg(MENELAUS_VCORE_CTRL4, rval);
	if (ret < 0)
		goto out;
	if (!the_menelaus->vcore_hw_mode) {
		val = menelaus_read_reg(MENELAUS_VCORE_CTRL1);
		/* HW mode, turn OFF byte comparator */
		val |= (VCORE_CTRL1_HW_NSW | VCORE_CTRL1_BYP_COMP);
		ret = menelaus_write_reg(MENELAUS_VCORE_CTRL1, val);
		the_menelaus->vcore_hw_mode = 1;
	}
	msleep(1);
out:
	mutex_unlock(&the_menelaus->lock);
	return ret;
}

static const struct menelaus_vtg vmem_vtg = {
	.name = "VMEM",
	.vtg_reg = MENELAUS_LDO_CTRL1,
	.vtg_shift = 0,
	.vtg_bits = 2,
	.mode_reg = MENELAUS_LDO_CTRL3,
};

static const struct menelaus_vtg_value vmem_values[] = {
	{ 1500, 0 },
	{ 1800, 1 },
	{ 1900, 2 },
	{ 2500, 3 },
};

int menelaus_set_vmem(unsigned int mV)
{
	int val;

	if (mV == 0)
		return menelaus_set_voltage(&vmem_vtg, 0, 0, 0);

	val = menelaus_get_vtg_value(mV, vmem_values, ARRAY_SIZE(vmem_values));
	if (val < 0)
		return -EINVAL;
	return menelaus_set_voltage(&vmem_vtg, mV, val, 0x02);
}
EXPORT_SYMBOL(menelaus_set_vmem);

static const struct menelaus_vtg vio_vtg = {
	.name = "VIO",
	.vtg_reg = MENELAUS_LDO_CTRL1,
	.vtg_shift = 2,
	.vtg_bits = 2,
	.mode_reg = MENELAUS_LDO_CTRL4,
};

static const struct menelaus_vtg_value vio_values[] = {
	{ 1500, 0 },
	{ 1800, 1 },
	{ 2500, 2 },
	{ 2800, 3 },
};

int menelaus_set_vio(unsigned int mV)
{
	int val;

	if (mV == 0)
		return menelaus_set_voltage(&vio_vtg, 0, 0, 0);

	val = menelaus_get_vtg_value(mV, vio_values, ARRAY_SIZE(vio_values));
	if (val < 0)
		return -EINVAL;
	return menelaus_set_voltage(&vio_vtg, mV, val, 0x02);
}
EXPORT_SYMBOL(menelaus_set_vio);

static const struct menelaus_vtg_value vdcdc_values[] = {
	{ 1500, 0 },
	{ 1800, 1 },
	{ 2000, 2 },
	{ 2200, 3 },
	{ 2400, 4 },
	{ 2800, 5 },
	{ 3000, 6 },
	{ 3300, 7 },
};

static const struct menelaus_vtg vdcdc2_vtg = {
	.name = "VDCDC2",
	.vtg_reg = MENELAUS_DCDC_CTRL1,
	.vtg_shift = 0,
	.vtg_bits = 3,
	.mode_reg = MENELAUS_DCDC_CTRL2,
};

static const struct menelaus_vtg vdcdc3_vtg = {
	.name = "VDCDC3",
	.vtg_reg = MENELAUS_DCDC_CTRL1,
	.vtg_shift = 3,
	.vtg_bits = 3,
	.mode_reg = MENELAUS_DCDC_CTRL3,
};

int menelaus_set_vdcdc(int dcdc, unsigned int mV)
{
	const struct menelaus_vtg *vtg;
	int val;

	if (dcdc != 2 && dcdc != 3)
		return -EINVAL;
	if (dcdc == 2)
		vtg = &vdcdc2_vtg;
	else
		vtg = &vdcdc3_vtg;

	if (mV == 0)
		return menelaus_set_voltage(vtg, 0, 0, 0);

	val = menelaus_get_vtg_value(mV, vdcdc_values,
				     ARRAY_SIZE(vdcdc_values));
	if (val < 0)
		return -EINVAL;
	return menelaus_set_voltage(vtg, mV, val, 0x03);
}

static const struct menelaus_vtg_value vmmc_values[] = {
	{ 1850, 0 },
	{ 2800, 1 },
	{ 3000, 2 },
	{ 3100, 3 },
};

static const struct menelaus_vtg vmmc_vtg = {
	.name = "VMMC",
	.vtg_reg = MENELAUS_LDO_CTRL1,
	.vtg_shift = 6,
	.vtg_bits = 2,
	.mode_reg = MENELAUS_LDO_CTRL7,
};

int menelaus_set_vmmc(unsigned int mV)
{
	int val;

	if (mV == 0)
		return menelaus_set_voltage(&vmmc_vtg, 0, 0, 0);

	val = menelaus_get_vtg_value(mV, vmmc_values, ARRAY_SIZE(vmmc_values));
	if (val < 0)
		return -EINVAL;
	return menelaus_set_voltage(&vmmc_vtg, mV, val, 0x02);
}
EXPORT_SYMBOL(menelaus_set_vmmc);


static const struct menelaus_vtg_value vaux_values[] = {
	{ 1500, 0 },
	{ 1800, 1 },
	{ 2500, 2 },
	{ 2800, 3 },
};

static const struct menelaus_vtg vaux_vtg = {
	.name = "VAUX",
	.vtg_reg = MENELAUS_LDO_CTRL1,
	.vtg_shift = 4,
	.vtg_bits = 2,
	.mode_reg = MENELAUS_LDO_CTRL6,
};

int menelaus_set_vaux(unsigned int mV)
{
	int val;

	if (mV == 0)
		return menelaus_set_voltage(&vaux_vtg, 0, 0, 0);

	val = menelaus_get_vtg_value(mV, vaux_values, ARRAY_SIZE(vaux_values));
	if (val < 0)
		return -EINVAL;
	return menelaus_set_voltage(&vaux_vtg, mV, val, 0x02);
}
EXPORT_SYMBOL(menelaus_set_vaux);

int menelaus_get_slot_pin_states(void)
{
	return menelaus_read_reg(MENELAUS_MCT_PIN_ST);
}
EXPORT_SYMBOL(menelaus_get_slot_pin_states);

int menelaus_set_regulator_sleep(int enable, u32 val)
{
	int t, ret;
	struct i2c_client *c = the_menelaus->client;

	mutex_lock(&the_menelaus->lock);
	ret = menelaus_write_reg(MENELAUS_SLEEP_CTRL2, val);
	if (ret < 0)
		goto out;

	dev_dbg(&c->dev, "regulator sleep configuration: %02x\n", val);

	ret = menelaus_read_reg(MENELAUS_GPIO_CTRL);
	if (ret < 0)
		goto out;
	t = (GPIO_CTRL_SLPCTLEN | GPIO3_DIR_INPUT);
	if (enable)
		ret |= t;
	else
		ret &= ~t;
	ret = menelaus_write_reg(MENELAUS_GPIO_CTRL, ret);
out:
	mutex_unlock(&the_menelaus->lock);
	return ret;
}

/*-----------------------------------------------------------------------*/

/* Handles Menelaus interrupts. Does not run in interrupt context */
static void menelaus_work(struct work_struct *_menelaus)
{
	struct menelaus_chip *menelaus =
			container_of(_menelaus, struct menelaus_chip, work);
	void (*handler)(struct menelaus_chip *menelaus);

	while (1) {
		unsigned isr;

		isr = (menelaus_read_reg(MENELAUS_INT_STATUS2)
				& ~menelaus->mask2) << 8;
		isr |= menelaus_read_reg(MENELAUS_INT_STATUS1)
				& ~menelaus->mask1;
		if (!isr)
			break;

		while (isr) {
			int irq = fls(isr) - 1;
			isr &= ~(1 << irq);

			mutex_lock(&menelaus->lock);
			menelaus_disable_irq(irq);
			menelaus_ack_irq(irq);
			handler = menelaus->handlers[irq];
			if (handler)
				handler(menelaus);
			menelaus_enable_irq(irq);
			mutex_unlock(&menelaus->lock);
		}
	}
	enable_irq(menelaus->client->irq);
}

/*
 * We cannot use I2C in interrupt context, so we just schedule work.
 */
static irqreturn_t menelaus_irq(int irq, void *_menelaus)
{
	struct menelaus_chip *menelaus = _menelaus;

	disable_irq_nosync(irq);
	(void)schedule_work(&menelaus->work);

	return IRQ_HANDLED;
}

/*-----------------------------------------------------------------------*/

/*
 * The RTC needs to be set once, then it runs on backup battery power.
 * It supports alarms, including system wake alarms (from some modes);
 * and 1/second IRQs if requested.
 */
#ifdef CONFIG_RTC_DRV_TWL92330

#define RTC_CTRL_RTC_EN		(1 << 0)
#define RTC_CTRL_AL_EN		(1 << 1)
#define RTC_CTRL_MODE12		(1 << 2)
#define RTC_CTRL_EVERY_MASK	(3 << 3)
#define RTC_CTRL_EVERY_SEC	(0 << 3)
#define RTC_CTRL_EVERY_MIN	(1 << 3)
#define RTC_CTRL_EVERY_HR	(2 << 3)
#define RTC_CTRL_EVERY_DAY	(3 << 3)

#define RTC_UPDATE_EVERY	0x08

#define RTC_HR_PM		(1 << 7)

static void menelaus_to_time(char *regs, struct rtc_time *t)
{
	t->tm_sec = bcd2bin(regs[0]);
	t->tm_min = bcd2bin(regs[1]);
	if (the_menelaus->rtc_control & RTC_CTRL_MODE12) {
		t->tm_hour = bcd2bin(regs[2] & 0x1f) - 1;
		if (regs[2] & RTC_HR_PM)
			t->tm_hour += 12;
	} else
		t->tm_hour = bcd2bin(regs[2] & 0x3f);
	t->tm_mday = bcd2bin(regs[3]);
	t->tm_mon = bcd2bin(regs[4]) - 1;
	t->tm_year = bcd2bin(regs[5]) + 100;
}

static int time_to_menelaus(struct rtc_time *t, int regnum)
{
	int	hour, status;

	status = menelaus_write_reg(regnum++, bin2bcd(t->tm_sec));
	if (status < 0)
		goto fail;

	status = menelaus_write_reg(regnum++, bin2bcd(t->tm_min));
	if (status < 0)
		goto fail;

	if (the_menelaus->rtc_control & RTC_CTRL_MODE12) {
		hour = t->tm_hour + 1;
		if (hour > 12)
			hour = RTC_HR_PM | bin2bcd(hour - 12);
		else
			hour = bin2bcd(hour);
	} else
		hour = bin2bcd(t->tm_hour);
	status = menelaus_write_reg(regnum++, hour);
	if (status < 0)
		goto fail;

	status = menelaus_write_reg(regnum++, bin2bcd(t->tm_mday));
	if (status < 0)
		goto fail;

	status = menelaus_write_reg(regnum++, bin2bcd(t->tm_mon + 1));
	if (status < 0)
		goto fail;

	status = menelaus_write_reg(regnum++, bin2bcd(t->tm_year - 100));
	if (status < 0)
		goto fail;

	return 0;
fail:
	dev_err(&the_menelaus->client->dev, "rtc write reg %02x, err %d\n",
			--regnum, status);
	return status;
}

static int menelaus_read_time(struct device *dev, struct rtc_time *t)
{
	struct i2c_msg	msg[2];
	char		regs[7];
	int		status;

	/* block read date and time registers */
	regs[0] = MENELAUS_RTC_SEC;

	msg[0].addr = MENELAUS_I2C_ADDRESS;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = regs;

	msg[1].addr = MENELAUS_I2C_ADDRESS;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(regs);
	msg[1].buf = regs;

	status = i2c_transfer(the_menelaus->client->adapter, msg, 2);
	if (status != 2) {
		dev_err(dev, "%s error %d\n", "read", status);
		return -EIO;
	}

	menelaus_to_time(regs, t);
	t->tm_wday = bcd2bin(regs[6]);

	return 0;
}

static int menelaus_set_time(struct device *dev, struct rtc_time *t)
{
	int		status;

	/* write date and time registers */
	status = time_to_menelaus(t, MENELAUS_RTC_SEC);
	if (status < 0)
		return status;
	status = menelaus_write_reg(MENELAUS_RTC_WKDAY, bin2bcd(t->tm_wday));
	if (status < 0) {
		dev_err(&the_menelaus->client->dev, "rtc write reg %02x "
				"err %d\n", MENELAUS_RTC_WKDAY, status);
		return status;
	}

	/* now commit the write */
	status = menelaus_write_reg(MENELAUS_RTC_UPDATE, RTC_UPDATE_EVERY);
	if (status < 0)
		dev_err(&the_menelaus->client->dev, "rtc commit time, err %d\n",
				status);

	return 0;
}

static int menelaus_read_alarm(struct device *dev, struct rtc_wkalrm *w)
{
	struct i2c_msg	msg[2];
	char		regs[6];
	int		status;

	/* block read alarm registers */
	regs[0] = MENELAUS_RTC_AL_SEC;

	msg[0].addr = MENELAUS_I2C_ADDRESS;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = regs;

	msg[1].addr = MENELAUS_I2C_ADDRESS;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(regs);
	msg[1].buf = regs;

	status = i2c_transfer(the_menelaus->client->adapter, msg, 2);
	if (status != 2) {
		dev_err(dev, "%s error %d\n", "alarm read", status);
		return -EIO;
	}

	menelaus_to_time(regs, &w->time);

	w->enabled = !!(the_menelaus->rtc_control & RTC_CTRL_AL_EN);

	/* NOTE we *could* check if actually pending... */
	w->pending = 0;

	return 0;
}

static int menelaus_set_alarm(struct device *dev, struct rtc_wkalrm *w)
{
	int		status;

	if (the_menelaus->client->irq <= 0 && w->enabled)
		return -ENODEV;

	/* clear previous alarm enable */
	if (the_menelaus->rtc_control & RTC_CTRL_AL_EN) {
		the_menelaus->rtc_control &= ~RTC_CTRL_AL_EN;
		status = menelaus_write_reg(MENELAUS_RTC_CTRL,
				the_menelaus->rtc_control);
		if (status < 0)
			return status;
	}

	/* write alarm registers */
	status = time_to_menelaus(&w->time, MENELAUS_RTC_AL_SEC);
	if (status < 0)
		return status;

	/* enable alarm if requested */
	if (w->enabled) {
		the_menelaus->rtc_control |= RTC_CTRL_AL_EN;
		status = menelaus_write_reg(MENELAUS_RTC_CTRL,
				the_menelaus->rtc_control);
	}

	return status;
}

#ifdef CONFIG_RTC_INTF_DEV

static void menelaus_rtc_update_work(struct menelaus_chip *m)
{
	/* report 1/sec update */
	local_irq_disable();
	rtc_update_irq(m->rtc, 1, RTC_IRQF | RTC_UF);
	local_irq_enable();
}

static int menelaus_ioctl(struct device *dev, unsigned cmd, unsigned long arg)
{
	int	status;

	if (the_menelaus->client->irq <= 0)
		return -ENOIOCTLCMD;

	switch (cmd) {
	/* alarm IRQ */
	case RTC_AIE_ON:
		if (the_menelaus->rtc_control & RTC_CTRL_AL_EN)
			return 0;
		the_menelaus->rtc_control |= RTC_CTRL_AL_EN;
		break;
	case RTC_AIE_OFF:
		if (!(the_menelaus->rtc_control & RTC_CTRL_AL_EN))
			return 0;
		the_menelaus->rtc_control &= ~RTC_CTRL_AL_EN;
		break;
	/* 1/second "update" IRQ */
	case RTC_UIE_ON:
		if (the_menelaus->uie)
			return 0;
		status = menelaus_remove_irq_work(MENELAUS_RTCTMR_IRQ);
		status = menelaus_add_irq_work(MENELAUS_RTCTMR_IRQ,
				menelaus_rtc_update_work);
		if (status == 0)
			the_menelaus->uie = 1;
		return status;
	case RTC_UIE_OFF:
		if (!the_menelaus->uie)
			return 0;
		status = menelaus_remove_irq_work(MENELAUS_RTCTMR_IRQ);
		if (status == 0)
			the_menelaus->uie = 0;
		return status;
	default:
		return -ENOIOCTLCMD;
	}
	return menelaus_write_reg(MENELAUS_RTC_CTRL, the_menelaus->rtc_control);
}

#else
#define menelaus_ioctl	NULL
#endif

/* REVISIT no compensation register support ... */

static const struct rtc_class_ops menelaus_rtc_ops = {
	.ioctl			= menelaus_ioctl,
	.read_time		= menelaus_read_time,
	.set_time		= menelaus_set_time,
	.read_alarm		= menelaus_read_alarm,
	.set_alarm		= menelaus_set_alarm,
};

static void menelaus_rtc_alarm_work(struct menelaus_chip *m)
{
	/* report alarm */
	local_irq_disable();
	rtc_update_irq(m->rtc, 1, RTC_IRQF | RTC_AF);
	local_irq_enable();

	/* then disable it; alarms are oneshot */
	the_menelaus->rtc_control &= ~RTC_CTRL_AL_EN;
	menelaus_write_reg(MENELAUS_RTC_CTRL, the_menelaus->rtc_control);
}

static inline void menelaus_rtc_init(struct menelaus_chip *m)
{
	int	alarm = (m->client->irq > 0);

	/* assume 32KDETEN pin is pulled high */
	if (!(menelaus_read_reg(MENELAUS_OSC_CTRL) & 0x80)) {
		dev_dbg(&m->client->dev, "no 32k oscillator\n");
		return;
	}

	/* support RTC alarm; it can issue wakeups */
	if (alarm) {
		if (menelaus_add_irq_work(MENELAUS_RTCALM_IRQ,
				menelaus_rtc_alarm_work) < 0) {
			dev_err(&m->client->dev, "can't handle RTC alarm\n");
			return;
		}
		device_init_wakeup(&m->client->dev, 1);
	}

	/* be sure RTC is enabled; allow 1/sec irqs; leave 12hr mode alone */
	m->rtc_control = menelaus_read_reg(MENELAUS_RTC_CTRL);
	if (!(m->rtc_control & RTC_CTRL_RTC_EN)
			|| (m->rtc_control & RTC_CTRL_AL_EN)
			|| (m->rtc_control & RTC_CTRL_EVERY_MASK)) {
		if (!(m->rtc_control & RTC_CTRL_RTC_EN)) {
			dev_warn(&m->client->dev, "rtc clock needs setting\n");
			m->rtc_control |= RTC_CTRL_RTC_EN;
		}
		m->rtc_control &= ~RTC_CTRL_EVERY_MASK;
		m->rtc_control &= ~RTC_CTRL_AL_EN;
		menelaus_write_reg(MENELAUS_RTC_CTRL, m->rtc_control);
	}

	m->rtc = rtc_device_register(DRIVER_NAME,
			&m->client->dev,
			&menelaus_rtc_ops, THIS_MODULE);
	if (IS_ERR(m->rtc)) {
		if (alarm) {
			menelaus_remove_irq_work(MENELAUS_RTCALM_IRQ);
			device_init_wakeup(&m->client->dev, 0);
		}
		dev_err(&m->client->dev, "can't register RTC: %d\n",
				(int) PTR_ERR(m->rtc));
		the_menelaus->rtc = NULL;
	}
}

#else

static inline void menelaus_rtc_init(struct menelaus_chip *m)
{
	/* nothing */
}

#endif

/*-----------------------------------------------------------------------*/

static struct i2c_driver menelaus_i2c_driver;

static int menelaus_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct menelaus_chip	*menelaus;
	int			rev = 0, val;
	int			err = 0;
	struct menelaus_platform_data *menelaus_pdata =
					client->dev.platform_data;

	if (the_menelaus) {
		dev_dbg(&client->dev, "only one %s for now\n",
				DRIVER_NAME);
		return -ENODEV;
	}

	menelaus = kzalloc(sizeof *menelaus, GFP_KERNEL);
	if (!menelaus)
		return -ENOMEM;

	i2c_set_clientdata(client, menelaus);

	the_menelaus = menelaus;
	menelaus->client = client;

	/* If a true probe check the device */
	rev = menelaus_read_reg(MENELAUS_REV);
	if (rev < 0) {
		pr_err(DRIVER_NAME ": device not found");
		err = -ENODEV;
		goto fail1;
	}

	/* Ack and disable all Menelaus interrupts */
	menelaus_write_reg(MENELAUS_INT_ACK1, 0xff);
	menelaus_write_reg(MENELAUS_INT_ACK2, 0xff);
	menelaus_write_reg(MENELAUS_INT_MASK1, 0xff);
	menelaus_write_reg(MENELAUS_INT_MASK2, 0xff);
	menelaus->mask1 = 0xff;
	menelaus->mask2 = 0xff;

	/* Set output buffer strengths */
	menelaus_write_reg(MENELAUS_MCT_CTRL1, 0x73);

	if (client->irq > 0) {
		err = request_irq(client->irq, menelaus_irq, IRQF_DISABLED,
				  DRIVER_NAME, menelaus);
		if (err) {
			dev_dbg(&client->dev,  "can't get IRQ %d, err %d\n",
					client->irq, err);
			goto fail1;
		}
	}

	mutex_init(&menelaus->lock);
	INIT_WORK(&menelaus->work, menelaus_work);

	pr_info("Menelaus rev %d.%d\n", rev >> 4, rev & 0x0f);

	val = menelaus_read_reg(MENELAUS_VCORE_CTRL1);
	if (val < 0)
		goto fail2;
	if (val & (1 << 7))
		menelaus->vcore_hw_mode = 1;
	else
		menelaus->vcore_hw_mode = 0;

	if (menelaus_pdata != NULL && menelaus_pdata->late_init != NULL) {
		err = menelaus_pdata->late_init(&client->dev);
		if (err < 0)
			goto fail2;
	}

	menelaus_rtc_init(menelaus);

	return 0;
fail2:
	free_irq(client->irq, menelaus);
	flush_work_sync(&menelaus->work);
fail1:
	kfree(menelaus);
	return err;
}

static int __exit menelaus_remove(struct i2c_client *client)
{
	struct menelaus_chip	*menelaus = i2c_get_clientdata(client);

	free_irq(client->irq, menelaus);
	flush_work_sync(&menelaus->work);
	kfree(menelaus);
	the_menelaus = NULL;
	return 0;
}

static const struct i2c_device_id menelaus_id[] = {
	{ "menelaus", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, menelaus_id);

static struct i2c_driver menelaus_i2c_driver = {
	.driver = {
		.name		= DRIVER_NAME,
	},
	.probe		= menelaus_probe,
	.remove		= __exit_p(menelaus_remove),
	.id_table	= menelaus_id,
};

static int __init menelaus_init(void)
{
	int res;

	res = i2c_add_driver(&menelaus_i2c_driver);
	if (res < 0) {
		pr_err(DRIVER_NAME ": driver registration failed\n");
		return res;
	}

	return 0;
}

static void __exit menelaus_exit(void)
{
	i2c_del_driver(&menelaus_i2c_driver);

	/* FIXME: Shutdown menelaus parts that can be shut down */
}

MODULE_AUTHOR("Texas Instruments, Inc. (and others)");
MODULE_DESCRIPTION("I2C interface for Menelaus.");
MODULE_LICENSE("GPL");

module_init(menelaus_init);
module_exit(menelaus_exit);
