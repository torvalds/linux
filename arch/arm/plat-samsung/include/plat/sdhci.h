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

struct platform_device;
struct mmc_host;
struct mmc_card;
struct mmc_ios;

enum cd_types {
	S3C_SDHCI_CD_INTERNAL,	/* use mmc internal CD line */
	S3C_SDHCI_CD_EXTERNAL,	/* use external callback */
	S3C_SDHCI_CD_GPIO,	/* use external gpio pin for CD line */
	S3C_SDHCI_CD_NONE,	/* no CD line, use polling to detect card */
	S3C_SDHCI_CD_PERMANENT,	/* no CD line, card permanently wired to host */
};

enum clk_types {
	S3C_SDHCI_CLK_DIV_INTERNAL,	/* use mmc internal clock divider */
	S3C_SDHCI_CLK_DIV_EXTERNAL,	/* use external clock divider */
};

/**
 * struct s3c_sdhci_platdata() - Platform device data for Samsung SDHCI
 * @max_width: The maximum number of data bits supported.
 * @host_caps: Standard MMC host capabilities bit field.
 * @cd_type: Type of Card Detection method (see cd_types enum above)
 * @clk_type: Type of clock divider method (see clk_types enum above)
 * @ext_cd_init: Initialize external card detect subsystem. Called on
 *		 sdhci-s3c driver probe when cd_type == S3C_SDHCI_CD_EXTERNAL.
 *		 notify_func argument is a callback to the sdhci-s3c driver
 *		 that triggers the card detection event. Callback arguments:
 *		 dev is pointer to platform device of the host controller,
 *		 state is new state of the card (0 - removed, 1 - inserted).
 * @ext_cd_cleanup: Cleanup external card detect subsystem. Called on
 *		 sdhci-s3c driver remove when cd_type == S3C_SDHCI_CD_EXTERNAL.
 *		 notify_func argument is the same callback as for ext_cd_init.
 * @ext_cd_gpio: gpio pin used for external CD line, valid only if
 *		 cd_type == S3C_SDHCI_CD_GPIO
 * @ext_cd_gpio_invert: invert values for external CD gpio line
 * @cfg_gpio: Configure the GPIO for a specific card bit-width
 *
 * Initialisation data specific to either the machine or the platform
 * for the device driver to use or call-back when configuring gpio or
 * card speed information.
*/
struct s3c_sdhci_platdata {
	unsigned int	max_width;
	unsigned int	host_caps;
	enum cd_types	cd_type;
	enum clk_types	clk_type;

	char		**clocks;	/* set of clock sources */

	int		ext_cd_gpio;
	bool		ext_cd_gpio_invert;
	int	(*ext_cd_init)(void (*notify_func)(struct platform_device *,
						   int state));
	int	(*ext_cd_cleanup)(void (*notify_func)(struct platform_device *,
						      int state));

	void	(*cfg_gpio)(struct platform_device *dev, int width);
};

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
extern void s5pc100_setup_sdhci0_cfg_gpio(struct platform_device *, int w);
extern void s5pc100_setup_sdhci1_cfg_gpio(struct platform_device *, int w);
extern void s5pc100_setup_sdhci2_cfg_gpio(struct platform_device *, int w);
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
extern char *s3c2416_hsmmc_clksrcs[4];

static inline void s3c2416_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = s3c2416_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c2416_setup_sdhci0_cfg_gpio;
#endif /* CONFIG_S3C_DEV_HSMMC */
}

static inline void s3c2416_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = s3c2416_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c2416_setup_sdhci1_cfg_gpio;
#endif /* CONFIG_S3C_DEV_HSMMC1 */
}

#else
static inline void s3c2416_default_sdhci0(void) { }
static inline void s3c2416_default_sdhci1(void) { }

#endif /* CONFIG_S3C2416_SETUP_SDHCI */
/* S3C64XX SDHCI setup */

#ifdef CONFIG_S3C64XX_SETUP_SDHCI
extern char *s3c64xx_hsmmc_clksrcs[4];

static inline void s3c6400_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c64xx_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s3c6400_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c64xx_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s3c6400_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
	s3c_hsmmc2_def_platdata.cfg_gpio = s3c64xx_setup_sdhci2_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = s3c64xx_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = s3c64xx_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s3c6410_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.clocks = s3c64xx_hsmmc_clksrcs;
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

/* S5PC100 SDHCI setup */

#ifdef CONFIG_S5PC100_SETUP_SDHCI
extern char *s5pc100_hsmmc_clksrcs[4];

static inline void s5pc100_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = s5pc100_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = s5pc100_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s5pc100_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = s5pc100_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = s5pc100_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s5pc100_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.clocks = s5pc100_hsmmc_clksrcs;
	s3c_hsmmc2_def_platdata.cfg_gpio = s5pc100_setup_sdhci2_cfg_gpio;
#endif
}

#else
static inline void s5pc100_default_sdhci0(void) { }
static inline void s5pc100_default_sdhci1(void) { }
static inline void s5pc100_default_sdhci2(void) { }

#endif /* CONFIG_S5PC100_SETUP_SDHCI */

/* S5PV210 SDHCI setup */

#ifdef CONFIG_S5PV210_SETUP_SDHCI
extern char *s5pv210_hsmmc_clksrcs[4];

static inline void s5pv210_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = s5pv210_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = s5pv210_setup_sdhci0_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = s5pv210_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = s5pv210_setup_sdhci1_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.clocks = s5pv210_hsmmc_clksrcs;
	s3c_hsmmc2_def_platdata.cfg_gpio = s5pv210_setup_sdhci2_cfg_gpio;
#endif
}

static inline void s5pv210_default_sdhci3(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_hsmmc3_def_platdata.clocks = s5pv210_hsmmc_clksrcs;
	s3c_hsmmc3_def_platdata.cfg_gpio = s5pv210_setup_sdhci3_cfg_gpio;
#endif
}

#else
static inline void s5pv210_default_sdhci0(void) { }
static inline void s5pv210_default_sdhci1(void) { }
static inline void s5pv210_default_sdhci2(void) { }
static inline void s5pv210_default_sdhci3(void) { }

#endif /* CONFIG_S5PV210_SETUP_SDHCI */

/* EXYNOS4 SDHCI setup */
#ifdef CONFIG_EXYNOS4_SETUP_SDHCI
extern char *exynos4_hsmmc_clksrcs[4];

static inline void exynos4_default_sdhci0(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_hsmmc0_def_platdata.clocks = exynos4_hsmmc_clksrcs;
	s3c_hsmmc0_def_platdata.cfg_gpio = exynos4_setup_sdhci0_cfg_gpio;
#endif
}

static inline void exynos4_default_sdhci1(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_hsmmc1_def_platdata.clocks = exynos4_hsmmc_clksrcs;
	s3c_hsmmc1_def_platdata.cfg_gpio = exynos4_setup_sdhci1_cfg_gpio;
#endif
}

static inline void exynos4_default_sdhci2(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_hsmmc2_def_platdata.clocks = exynos4_hsmmc_clksrcs;
	s3c_hsmmc2_def_platdata.cfg_gpio = exynos4_setup_sdhci2_cfg_gpio;
#endif
}

static inline void exynos4_default_sdhci3(void)
{
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_hsmmc3_def_platdata.clocks = exynos4_hsmmc_clksrcs;
	s3c_hsmmc3_def_platdata.cfg_gpio = exynos4_setup_sdhci3_cfg_gpio;
#endif
}

#else
static inline void exynos4_default_sdhci0(void) { }
static inline void exynos4_default_sdhci1(void) { }
static inline void exynos4_default_sdhci2(void) { }
static inline void exynos4_default_sdhci3(void) { }

#endif /* CONFIG_EXYNOS4_SETUP_SDHCI */

#endif /* __PLAT_S3C_SDHCI_H */
