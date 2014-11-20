
/* linux/arch/arm/plat-s3c/pm-gpio.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series GPIO PM code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/gpio-samsung.h>

#include <plat/gpio-core.h>
#include <plat/pm.h>

/* PM GPIO helpers */

#define OFFS_CON	(0x00)
#define OFFS_DAT	(0x04)
#define OFFS_UP		(0x08)

static void samsung_gpio_pm_1bit_save(struct samsung_gpio_chip *chip)
{
	chip->pm_save[0] = __raw_readl(chip->base + OFFS_CON);
	chip->pm_save[1] = __raw_readl(chip->base + OFFS_DAT);
}

static void samsung_gpio_pm_1bit_resume(struct samsung_gpio_chip *chip)
{
	void __iomem *base = chip->base;
	u32 old_gpcon = __raw_readl(base + OFFS_CON);
	u32 old_gpdat = __raw_readl(base + OFFS_DAT);
	u32 gps_gpcon = chip->pm_save[0];
	u32 gps_gpdat = chip->pm_save[1];
	u32 gpcon;

	/* GPACON only has one bit per control / data and no PULLUPs.
	 * GPACON[x] = 0 => Output, 1 => SFN */

	/* first set all SFN bits to SFN */

	gpcon = old_gpcon | gps_gpcon;
	__raw_writel(gpcon, base + OFFS_CON);

	/* now set all the other bits */

	__raw_writel(gps_gpdat, base + OFFS_DAT);
	__raw_writel(gps_gpcon, base + OFFS_CON);

	S3C_PMDBG("%s: CON %08x => %08x, DAT %08x => %08x\n",
		  chip->chip.label, old_gpcon, gps_gpcon, old_gpdat, gps_gpdat);
}

struct samsung_gpio_pm samsung_gpio_pm_1bit = {
	.save	= samsung_gpio_pm_1bit_save,
	.resume = samsung_gpio_pm_1bit_resume,
};

static void samsung_gpio_pm_2bit_save(struct samsung_gpio_chip *chip)
{
	chip->pm_save[0] = __raw_readl(chip->base + OFFS_CON);
	chip->pm_save[1] = __raw_readl(chip->base + OFFS_DAT);
	chip->pm_save[2] = __raw_readl(chip->base + OFFS_UP);
}

/* Test whether the given masked+shifted bits of an GPIO configuration
 * are one of the SFN (special function) modes. */

static inline int is_sfn(unsigned long con)
{
	return con >= 2;
}

/* Test if the given masked+shifted GPIO configuration is an input */

static inline int is_in(unsigned long con)
{
	return con == 0;
}

/* Test if the given masked+shifted GPIO configuration is an output */

static inline int is_out(unsigned long con)
{
	return con == 1;
}

/**
 * samsung_gpio_pm_2bit_resume() - restore the given GPIO bank
 * @chip: The chip information to resume.
 *
 * Restore one of the GPIO banks that was saved during suspend. This is
 * not as simple as once thought, due to the possibility of glitches
 * from the order that the CON and DAT registers are set in.
 *
 * The three states the pin can be are {IN,OUT,SFN} which gives us 9
 * combinations of changes to check. Three of these, if the pin stays
 * in the same configuration can be discounted. This leaves us with
 * the following:
 *
 * { IN => OUT }  Change DAT first
 * { IN => SFN }  Change CON first
 * { OUT => SFN } Change CON first, so new data will not glitch
 * { OUT => IN }  Change CON first, so new data will not glitch
 * { SFN => IN }  Change CON first
 * { SFN => OUT } Change DAT first, so new data will not glitch [1]
 *
 * We do not currently deal with the UP registers as these control
 * weak resistors, so a small delay in change should not need to bring
 * these into the calculations.
 *
 * [1] this assumes that writing to a pin DAT whilst in SFN will set the
 *     state for when it is next output.
 */
static void samsung_gpio_pm_2bit_resume(struct samsung_gpio_chip *chip)
{
	void __iomem *base = chip->base;
	u32 old_gpcon = __raw_readl(base + OFFS_CON);
	u32 old_gpdat = __raw_readl(base + OFFS_DAT);
	u32 gps_gpcon = chip->pm_save[0];
	u32 gps_gpdat = chip->pm_save[1];
	u32 gpcon, old, new, mask;
	u32 change_mask = 0x0;
	int nr;

	/* restore GPIO pull-up settings */
	__raw_writel(chip->pm_save[2], base + OFFS_UP);

	/* Create a change_mask of all the items that need to have
	 * their CON value changed before their DAT value, so that
	 * we minimise the work between the two settings.
	 */

	for (nr = 0, mask = 0x03; nr < 32; nr += 2, mask <<= 2) {
		old = (old_gpcon & mask) >> nr;
		new = (gps_gpcon & mask) >> nr;

		/* If there is no change, then skip */

		if (old == new)
			continue;

		/* If both are special function, then skip */

		if (is_sfn(old) && is_sfn(new))
			continue;

		/* Change is IN => OUT, do not change now */

		if (is_in(old) && is_out(new))
			continue;

		/* Change is SFN => OUT, do not change now */

		if (is_sfn(old) && is_out(new))
			continue;

		/* We should now be at the case of IN=>SFN,
		 * OUT=>SFN, OUT=>IN, SFN=>IN. */

		change_mask |= mask;
	}


	/* Write the new CON settings */

	gpcon = old_gpcon & ~change_mask;
	gpcon |= gps_gpcon & change_mask;

	__raw_writel(gpcon, base + OFFS_CON);

	/* Now change any items that require DAT,CON */

	__raw_writel(gps_gpdat, base + OFFS_DAT);
	__raw_writel(gps_gpcon, base + OFFS_CON);

	S3C_PMDBG("%s: CON %08x => %08x, DAT %08x => %08x\n",
		  chip->chip.label, old_gpcon, gps_gpcon, old_gpdat, gps_gpdat);
}

struct samsung_gpio_pm samsung_gpio_pm_2bit = {
	.save	= samsung_gpio_pm_2bit_save,
	.resume = samsung_gpio_pm_2bit_resume,
};

#if defined(CONFIG_ARCH_S3C64XX)
static void samsung_gpio_pm_4bit_save(struct samsung_gpio_chip *chip)
{
	chip->pm_save[1] = __raw_readl(chip->base + OFFS_CON);
	chip->pm_save[2] = __raw_readl(chip->base + OFFS_DAT);
	chip->pm_save[3] = __raw_readl(chip->base + OFFS_UP);

	if (chip->chip.ngpio > 8)
		chip->pm_save[0] = __raw_readl(chip->base - 4);
}

static u32 samsung_gpio_pm_4bit_mask(u32 old_gpcon, u32 gps_gpcon)
{
	u32 old, new, mask;
	u32 change_mask = 0x0;
	int nr;

	for (nr = 0, mask = 0x0f; nr < 16; nr += 4, mask <<= 4) {
		old = (old_gpcon & mask) >> nr;
		new = (gps_gpcon & mask) >> nr;

		/* If there is no change, then skip */

		if (old == new)
			continue;

		/* If both are special function, then skip */

		if (is_sfn(old) && is_sfn(new))
			continue;

		/* Change is IN => OUT, do not change now */

		if (is_in(old) && is_out(new))
			continue;

		/* Change is SFN => OUT, do not change now */

		if (is_sfn(old) && is_out(new))
			continue;

		/* We should now be at the case of IN=>SFN,
		 * OUT=>SFN, OUT=>IN, SFN=>IN. */

		change_mask |= mask;
	}

	return change_mask;
}

static void samsung_gpio_pm_4bit_con(struct samsung_gpio_chip *chip, int index)
{
	void __iomem *con = chip->base + (index * 4);
	u32 old_gpcon = __raw_readl(con);
	u32 gps_gpcon = chip->pm_save[index + 1];
	u32 gpcon, mask;

	mask = samsung_gpio_pm_4bit_mask(old_gpcon, gps_gpcon);

	gpcon = old_gpcon & ~mask;
	gpcon |= gps_gpcon & mask;

	__raw_writel(gpcon, con);
}

static void samsung_gpio_pm_4bit_resume(struct samsung_gpio_chip *chip)
{
	void __iomem *base = chip->base;
	u32 old_gpcon[2];
	u32 old_gpdat = __raw_readl(base + OFFS_DAT);
	u32 gps_gpdat = chip->pm_save[2];

	/* First, modify the CON settings */

	old_gpcon[0] = 0;
	old_gpcon[1] = __raw_readl(base + OFFS_CON);

	samsung_gpio_pm_4bit_con(chip, 0);
	if (chip->chip.ngpio > 8) {
		old_gpcon[0] = __raw_readl(base - 4);
		samsung_gpio_pm_4bit_con(chip, -1);
	}

	/* Now change the configurations that require DAT,CON */

	__raw_writel(chip->pm_save[2], base + OFFS_DAT);
	__raw_writel(chip->pm_save[1], base + OFFS_CON);
	if (chip->chip.ngpio > 8)
		__raw_writel(chip->pm_save[0], base - 4);

	__raw_writel(chip->pm_save[2], base + OFFS_DAT);
	__raw_writel(chip->pm_save[3], base + OFFS_UP);

	if (chip->chip.ngpio > 8) {
		S3C_PMDBG("%s: CON4 %08x,%08x => %08x,%08x, DAT %08x => %08x\n",
			  chip->chip.label, old_gpcon[0], old_gpcon[1],
			  __raw_readl(base - 4),
			  __raw_readl(base + OFFS_CON),
			  old_gpdat, gps_gpdat);
	} else
		S3C_PMDBG("%s: CON4 %08x => %08x, DAT %08x => %08x\n",
			  chip->chip.label, old_gpcon[1],
			  __raw_readl(base + OFFS_CON),
			  old_gpdat, gps_gpdat);
}

struct samsung_gpio_pm samsung_gpio_pm_4bit = {
	.save	= samsung_gpio_pm_4bit_save,
	.resume = samsung_gpio_pm_4bit_resume,
};
#endif /* CONFIG_ARCH_S3C64XX */

/**
 * samsung_pm_save_gpio() - save gpio chip data for suspend
 * @ourchip: The chip for suspend.
 */
static void samsung_pm_save_gpio(struct samsung_gpio_chip *ourchip)
{
	struct samsung_gpio_pm *pm = ourchip->pm;

	if (pm == NULL || pm->save == NULL)
		S3C_PMDBG("%s: no pm for %s\n", __func__, ourchip->chip.label);
	else
		pm->save(ourchip);
}

/**
 * samsung_pm_save_gpios() - Save the state of the GPIO banks.
 *
 * For all the GPIO banks, save the state of each one ready for going
 * into a suspend mode.
 */
void samsung_pm_save_gpios(void)
{
	struct samsung_gpio_chip *ourchip;
	unsigned int gpio_nr;

	for (gpio_nr = 0; gpio_nr < S3C_GPIO_END;) {
		ourchip = samsung_gpiolib_getchip(gpio_nr);
		if (!ourchip) {
			gpio_nr++;
			continue;
		}

		samsung_pm_save_gpio(ourchip);

		S3C_PMDBG("%s: save %08x,%08x,%08x,%08x\n",
			  ourchip->chip.label,
			  ourchip->pm_save[0],
			  ourchip->pm_save[1],
			  ourchip->pm_save[2],
			  ourchip->pm_save[3]);

		gpio_nr += ourchip->chip.ngpio;
		gpio_nr += CONFIG_S3C_GPIO_SPACE;
	}
}

/**
 * samsung_pm_resume_gpio() - restore gpio chip data after suspend
 * @ourchip: The suspended chip.
 */
static void samsung_pm_resume_gpio(struct samsung_gpio_chip *ourchip)
{
	struct samsung_gpio_pm *pm = ourchip->pm;

	if (pm == NULL || pm->resume == NULL)
		S3C_PMDBG("%s: no pm for %s\n", __func__, ourchip->chip.label);
	else
		pm->resume(ourchip);
}

void samsung_pm_restore_gpios(void)
{
	struct samsung_gpio_chip *ourchip;
	unsigned int gpio_nr;

	for (gpio_nr = 0; gpio_nr < S3C_GPIO_END;) {
		ourchip = samsung_gpiolib_getchip(gpio_nr);
		if (!ourchip) {
			gpio_nr++;
			continue;
		}

		samsung_pm_resume_gpio(ourchip);

		gpio_nr += ourchip->chip.ngpio;
		gpio_nr += CONFIG_S3C_GPIO_SPACE;
	}
}
