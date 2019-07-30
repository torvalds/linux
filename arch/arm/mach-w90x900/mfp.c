// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-w90x900/mfp.c
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include <mach/hardware.h>

#define REG_MFSEL	(W90X900_VA_GCR + 0xC)

#define GPSELF		(0x01 << 1)
#define GPSELC		(0x03 << 2)
#define GPSELD		(0x0f << 4)

#define GPSELEI0	(0x01 << 26)
#define GPSELEI1	(0x01 << 27)

#define GPIOG0TO1	(0x03 << 14)
#define GPIOG2TO3	(0x03 << 16)
#define GPIOG22TO23	(0x03 << 22)
#define GPIOG18TO20	(0x07 << 18)

#define ENSPI		(0x0a << 14)
#define ENI2C0		(0x01 << 14)
#define ENI2C1		(0x01 << 16)
#define ENAC97		(0x02 << 22)
#define ENSD1		(0x02 << 18)
#define ENSD0		(0x0a << 4)
#define ENKPI		(0x02 << 2)
#define ENNAND		(0x01 << 2)

static DEFINE_MUTEX(mfp_mutex);

void mfp_set_groupf(struct device *dev)
{
	unsigned long mfpen;
	const char *dev_id;

	BUG_ON(!dev);

	mutex_lock(&mfp_mutex);

	dev_id = dev_name(dev);

	mfpen = __raw_readl(REG_MFSEL);

	if (strcmp(dev_id, "nuc900-emc") == 0)
		mfpen |= GPSELF;/*enable mac*/
	else
		mfpen &= ~GPSELF;/*GPIOF[9:0]*/

	__raw_writel(mfpen, REG_MFSEL);

	mutex_unlock(&mfp_mutex);
}
EXPORT_SYMBOL(mfp_set_groupf);

void mfp_set_groupc(struct device *dev)
{
	unsigned long mfpen;
	const char *dev_id;

	BUG_ON(!dev);

	mutex_lock(&mfp_mutex);

	dev_id = dev_name(dev);

	mfpen = __raw_readl(REG_MFSEL);

	if (strcmp(dev_id, "nuc900-lcd") == 0)
		mfpen |= GPSELC;/*enable lcd*/
	else if (strcmp(dev_id, "nuc900-kpi") == 0) {
		mfpen &= (~GPSELC);/*enable kpi*/
		mfpen |= ENKPI;
	} else if (strcmp(dev_id, "nuc900-nand") == 0) {
		mfpen &= (~GPSELC);/*enable nand*/
		mfpen |= ENNAND;
	} else
		mfpen &= (~GPSELC);/*GPIOC[14:0]*/

	__raw_writel(mfpen, REG_MFSEL);

	mutex_unlock(&mfp_mutex);
}
EXPORT_SYMBOL(mfp_set_groupc);

void mfp_set_groupi(struct device *dev)
{
	unsigned long mfpen;
	const char *dev_id;

	BUG_ON(!dev);

	mutex_lock(&mfp_mutex);

	dev_id = dev_name(dev);

	mfpen = __raw_readl(REG_MFSEL);

	mfpen &= ~GPSELEI1;/*default gpio16*/

	if (strcmp(dev_id, "nuc900-wdog") == 0)
		mfpen |= GPSELEI1;/*enable wdog*/
	else if (strcmp(dev_id, "nuc900-atapi") == 0)
		mfpen |= GPSELEI0;/*enable atapi*/
	else if (strcmp(dev_id, "nuc900-keypad") == 0)
		mfpen &= ~GPSELEI0;/*enable keypad*/

	__raw_writel(mfpen, REG_MFSEL);

	mutex_unlock(&mfp_mutex);
}
EXPORT_SYMBOL(mfp_set_groupi);

void mfp_set_groupg(struct device *dev, const char *subname)
{
	unsigned long mfpen;
	const char *dev_id;

	BUG_ON((!dev) && (!subname));

	mutex_lock(&mfp_mutex);

	if (subname != NULL)
		dev_id = subname;
	else
		dev_id = dev_name(dev);

	mfpen = __raw_readl(REG_MFSEL);

	if (strcmp(dev_id, "nuc900-spi") == 0) {
		mfpen &= ~(GPIOG0TO1 | GPIOG2TO3);
		mfpen |= ENSPI;/*enable spi*/
	} else if (strcmp(dev_id, "nuc900-i2c0") == 0) {
		mfpen &= ~(GPIOG0TO1);
		mfpen |= ENI2C0;/*enable i2c0*/
	} else if (strcmp(dev_id, "nuc900-i2c1") == 0) {
		mfpen &= ~(GPIOG2TO3);
		mfpen |= ENI2C1;/*enable i2c1*/
	} else if (strcmp(dev_id, "nuc900-ac97") == 0) {
		mfpen &= ~(GPIOG22TO23);
		mfpen |= ENAC97;/*enable AC97*/
	} else if (strcmp(dev_id, "nuc900-mmc-port1") == 0) {
		mfpen &= ~(GPIOG18TO20);
		mfpen |= (ENSD1 | 0x01);/*enable sd1*/
	} else {
		mfpen &= ~(GPIOG0TO1 | GPIOG2TO3);/*GPIOG[3:0]*/
	}

	__raw_writel(mfpen, REG_MFSEL);

	mutex_unlock(&mfp_mutex);
}
EXPORT_SYMBOL(mfp_set_groupg);

void mfp_set_groupd(struct device *dev, const char *subname)
{
	unsigned long mfpen;
	const char *dev_id;

	BUG_ON((!dev) && (!subname));

	mutex_lock(&mfp_mutex);

	if (subname != NULL)
		dev_id = subname;
	else
		dev_id = dev_name(dev);

	mfpen = __raw_readl(REG_MFSEL);

	if (strcmp(dev_id, "nuc900-mmc-port0") == 0) {
		mfpen &= ~GPSELD;/*enable sd0*/
		mfpen |= ENSD0;
	} else
		mfpen &= (~GPSELD);

	__raw_writel(mfpen, REG_MFSEL);

	mutex_unlock(&mfp_mutex);
}
EXPORT_SYMBOL(mfp_set_groupd);
