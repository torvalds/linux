/* linux/arch/arm/plat-samsung/include/plat/mshci.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * EXYNOS4 - MSHCI (HSMMC) platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_S3C_MSHCI_H
#define __PLAT_S3C_MSHCI_H __FILE__

struct platform_device;
struct mmc_host;
struct mmc_card;
struct mmc_ios;

enum ms_cd_types {
	S3C_MSHCI_CD_INTERNAL,	/* use mmc internal CD line */
	S3C_MSHCI_CD_EXTERNAL,	/* use external callback */
	S3C_MSHCI_CD_GPIO,	/* use external gpio pin for CD line */
	S3C_MSHCI_CD_NONE,	/* no CD line, use polling to detect card */
	S3C_MSHCI_CD_PERMANENT,	/* no CD line, card permanently wired to host */
};

/**
 * struct s3c_mshci_platdata() - Platform device data for Samsung MSHCI
 * @max_width: The maximum number of data bits supported.
 * @host_caps: Standard MMC host capabilities bit field.
 * @cd_type: Type of Card Detection method (see cd_types enum above)
 * @wp_gpio: The gpio number using for WP.
 * @has_wp_gpio: Check using wp_gpio or not.
 * @ext_cd_init: Initialize external card detect subsystem. Called on
 *		 mshci-s3c driver probe when cd_type == S3C_MSHCI_CD_EXTERNAL.
 *		 notify_func argument is a callback to the mshci-s3c driver
 *		 that triggers the card detection event. Callback arguments:
 *		 dev is pointer to platform device of the host controller,
 *		 state is new state of the card (0 - removed, 1 - inserted).
 * @ext_cd_cleanup: Cleanup external card detect subsystem. Called on
 *		 mshci-s3c driver remove when cd_type == S3C_MSHCI_CD_EXTERNAL.
 *		 notify_func argument is the same callback as for ext_cd_init.
 * @ext_cd_gpio: gpio pin used for external CD line, valid only if
 *		 cd_type == S3C_MSHCI_CD_GPIO
 * @ext_cd_gpio_invert: invert values for external CD gpio line
 * @cfg_gpio: Configure the GPIO for a specific card bit-width
 * @cfg_card: Configure the interface for a specific card and speed. This
 *            is necessary the controllers and/or GPIO blocks require the
 *	      changing of driver-strength and other controls dependant on
 *	      the card and speed of operation.
 *
 * Initialisation data specific to either the machine or the platform
 * for the device driver to use or call-back when configuring gpio or
 * card speed information.
*/
struct s3c_mshci_platdata {
	unsigned int	max_width;
	unsigned int	host_caps;
	enum ms_cd_types	cd_type;

	char		**clocks;	/* set of clock sources */

	int		wp_gpio;
	int		ext_cd_gpio;
	bool		ext_cd_gpio_invert;
	bool		has_wp_gpio;
	int	(*ext_cd_init)(void (*notify_func)(struct platform_device *,
						   int state));
	int	(*ext_cd_cleanup)(void (*notify_func)(struct platform_device *,
						      int state));

	void	(*cfg_gpio)(struct platform_device *dev, int width);
#if defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ) || \
		defined(CONFIG_EXYNOS4_MSHC_EPLL_45MHZ)
	void	(*cfg_ddr)(struct platform_device *dev, int ddr);
#endif
	void	(*init_card)(struct platform_device *dev);

	void	(*cfg_card)(struct platform_device *dev,
			    void __iomem *regbase,
			    struct mmc_ios *ios,
			    struct mmc_card *card);
};

/**
 * s3c_mshci_set_platdata - Set platform data for S3C MSHCI device.
 * @pd: Platform data to register to device.
 *
 * Register the given platform data for use withe S3C MSHCI device.
 * The call will copy the platform data, so the board definitions can
 * make the structure itself __initdata.
 */
extern void s3c_mshci_set_platdata(struct s3c_mshci_platdata *pd);

/* Default platform data, exported so that per-cpu initialisation can
 * set the correct one when there are more than one cpu type selected.
*/

extern struct s3c_mshci_platdata s3c_mshci_def_platdata;

/* Helper function availablity */

extern void s5p6450_setup_mshci_cfg_gpio(struct platform_device *, int w);

/* S5P6450 MSHCI setup */
extern char *s5p6450_mshc_clksrcs[1];

extern void s5p6450_setup_mshci_cfg_card(struct platform_device *dev,
					   void __iomem *r,
					   struct mmc_ios *ios,
					   struct mmc_card *card);

static inline void s5p6450_default_mshci(void)
{
#ifdef CONFIG_S5P_DEV_MSHC
	s3c_mshci_def_platdata.clocks = s5p6450_mshc_clksrcs;
	s3c_mshci_def_platdata.cfg_gpio = s5p6450_setup_mshci_cfg_gpio;
	s3c_mshci_def_platdata.cfg_card = s5p6450_setup_mshci_cfg_card;
#endif /* CONFIG_S3C_DEV_MSHC */
}

extern void exynos4_setup_mshci_cfg_gpio(struct platform_device *, int w);

/* EXYNOS4 MSHCI setup */
#ifdef CONFIG_EXYNOS4_SETUP_MSHCI
extern char *exynos4_mshci_clksrcs[1];
#endif

extern void exynos4_setup_mshci_cfg_card(struct platform_device *dev,
					   void __iomem *r,
					   struct mmc_ios *ios,
					   struct mmc_card *card);

#if defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ) || \
	defined(CONFIG_EXYNOS4_MSHC_EPLL_45MHZ)
extern void exynos4_setup_mshci_cfg_ddr(struct platform_device *dev,
						int ddr);
#endif

extern void exynos4_setup_mshci_init_card(struct platform_device *dev);

#ifdef CONFIG_S5P_DEV_MSHC
static inline void exynos4_default_mshci(void)
{
	s3c_mshci_def_platdata.clocks = exynos4_mshci_clksrcs;
	s3c_mshci_def_platdata.cfg_gpio = exynos4_setup_mshci_cfg_gpio;
	s3c_mshci_def_platdata.cfg_card = exynos4_setup_mshci_cfg_card;
#if defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ) || \
		defined(CONFIG_EXYNOS4_MSHC_EPLL_45MHZ)
	s3c_mshci_def_platdata.cfg_ddr = exynos4_setup_mshci_cfg_ddr;
#endif

	s3c_mshci_def_platdata.init_card = exynos4_setup_mshci_init_card;
}
#else
static inline void exynos4_default_mshci(void) { }
#endif /* CONFIG_S5P_DEV_MSHC */

#endif /* __PLAT_S3C_MSHCI_H */
