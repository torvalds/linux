/* linux/arch/arm/plat-samsung/include/plat/sdhci.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Platform - SDHCI (HSMMC) platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_S3C_SDHCI_H
#define __PLAT_S3C_SDHCI_H __FILE__

#include <linux/platform_data/mmc-sdhci-s3c.h>
#include <plat/devs.h>

/* s3c_sdhci_set_platdata() - common helper for setting SDHCI platform data
 * @pd: The default platform data for this device.
 * @set: Pointer to the platform data to fill in.
 */
extern void s3c_sdhci_set_platdata(struct s3c_sdhci_platdata *pd,
				    struct s3c_sdhci_platdata *set);

/**
 * s3c_sdhci0_set_platdata - Set platform data for S3C SDHCI device.
 * @pd: Platform data to register to device.
 *
 * Register the given platform data for use withe S3C SDHCI device.
 * The call will copy the platform data, so the board definitions can
 * make the structure itself __initdata.
 */
extern void s3c_sdhci0_set_platdata(struct s3c_sdhci_platdata *pd);
extern void s3c_sdhci1_set_platdata(struct s3c_sdhci_platdata *pd);
extern void s3c_sdhci2_set_platdata(struct s3c_sdhci_platdata *pd);
extern void s3c_sdhci3_set_platdata(struct s3c_sdhci_platdata *pd);

/* Default platform data, exported so that per-cpu initialisation can
 * set the correct one when there are more than one cpu type selected.
*/

extern struct s3c_sdhci_platdata s3c_hsmmc0_def_platdata;
extern struct s3c_sdhci_platdata s3c_hsmmc1_def_platdata;
extern struct s3c_sdhci_platdata s3c_hsmmc2_def_platdata;
extern struct s3c_sdhci_platdata s3c_hsmmc3_def_platdata;

/* Helper function availability */

extern void s3c2416_setup_sdhci0_cfg_gpio(struct platform_device *, int w);
extern void s3c2416_setup_sdhci1_cfg_gpio(struct platform_device *, int w);
extern void s3c64xx_setup_sdhci0_cfg_gpio(struct platform_device *, int w);
extern void s3c64xx_setup_sdhci1_cfg_gpio(struct platform_device *, int w);
extern void s3c64xx_setup_sdhci2_cfg_gpio(struct platform_device *, int w);
extern void s5pv210_setup_sdhci0_cfg_gpio(struct platform_device *, int w);
extern void s5pv210_setup_sdhci1_cfg_gpio(struct platform_device *, int w);
extern void s5pv210_setup_sdhci2_cfg_gpio(struct platform_device *, int w);
extern void s5pv210_setup_sdhci3_cfg_gpio(struct platform_device *, int w);
extern void exynos4_setup_sdhci0_cfg_gpio(struct platform_device *, int w);
extern void exynos4_setup_sdhci1_cfg_gpio(struct platform_device *, int w);
extern void exynos4_setup_sdhci2_cfg_gpio(struct platform_device *, int w);
extern void exynos4_setup_sdhci3_cfg_gpio(struct platform_device *, int w);

/* S3C2416 SDHCI setup */

#ifdef CONFIG_S3C2416_SETUP_SDHCI
static inline void s3c2416_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c2416_setup_sdhci0_cfg_gpio;
#endif /* CONFIG_S3C_DEV_HSMMC */
}

static inline void s3c2416_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c2416_setup_sdhci1_cfg_gpio;
#endif /* CONFIG_S3C_DEV_HSMMC1 */
}

#else
static inline void s3c2416_default_sdhci0(void) { }
static inline void s3c2416_default_sdhci1(void) { }

#endif /* CONFIG_S3C2416_SETUP_SDHCI */

/* S3C64XX SDHCI setup */

#ifdef CONFIG_S3C64XX_SETUP_SDHCI
static inline void s3c6400_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c64xx_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s3c6400_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c64xx_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s3c6400_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.cfg_gpio = s3c64xx_setup_sdhci2_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c64xx_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c64xx_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.cfg_gpio = s3c64xx_setup_sdhci2_cfg_gpio;
#endif
}

#else
static inline void s3c6410_default_sdhci0(void) { }
static inline void s3c6410_default_sdhci1(void) { }
static inline void s3c6410_default_sdhci2(void) { }
static inline void s3c6400_default_sdhci0(void) { }
static inline void s3c6400_default_sdhci1(void) { }
static inline void s3c6400_default_sdhci2(void) { }

#endif /* CONFIG_S3C64XX_SETUP_SDHCI */

/* S5PV210 SDHCI setup */

#ifdef CONFIG_S5PV210_SETUP_SDHCI
static inline void s5pv210_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.cfg_gpio = s5pv210_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.cfg_gpio = s5pv210_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.cfg_gpio = s5pv210_setup_sdhci2_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci3(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_hsmmc3_def_platdata.cfg_gpio = s5pv210_setup_sdhci3_cfg_gpio;
#endif
}

#else
static inline void s5pv210_default_sdhci0(void) { }
static inline void s5pv210_default_sdhci1(void) { }
static inline void s5pv210_default_sdhci2(void) { }
static inline void s5pv210_default_sdhci3(void) { }

#endif /* CONFIG_S5PV210_SETUP_SDHCI */

static inline void s3c_sdhci_setname(int id, char *name)
{
	switch (id) {
#ifdef CONFIG_S3C_DEV_HSMMC
	case 0:
		s3c_device_hsmmc0.name = name;
		break;
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	case 1:
		s3c_device_hsmmc1.name = name;
		break;
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	case 2:
		s3c_device_hsmmc2.name = name;
		break;
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	case 3:
		s3c_device_hsmmc3.name = name;
		break;
#endif
	default:
		break;
	}
}
#endif /* __PLAT_S3C_SDHCI_H */
