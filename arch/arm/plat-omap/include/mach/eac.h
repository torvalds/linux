/*
 * arch/arm/plat-omap/include/mach2/eac.h
 *
 * Defines for Enhanced Audio Controller
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
 *
 * Copyright (C) 2006 Nokia Corporation
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __ASM_ARM_ARCH_OMAP2_EAC_H
#define __ASM_ARM_ARCH_OMAP2_EAC_H

#include <mach/io.h>
#include <mach/hardware.h>
#include <asm/irq.h>

#include <sound/core.h>

/* master codec clock source */
#define EAC_MCLK_EXT_MASK	0x100
enum eac_mclk_src {
	EAC_MCLK_INT_11290000, /* internal 96 MHz / 8.5 = 11.29 Mhz */
	EAC_MCLK_EXT_11289600 = EAC_MCLK_EXT_MASK,
	EAC_MCLK_EXT_12288000,
	EAC_MCLK_EXT_2x11289600,
	EAC_MCLK_EXT_2x12288000,
};

/* codec port interface mode */
enum eac_codec_mode {
	EAC_CODEC_PCM,
	EAC_CODEC_AC97,
	EAC_CODEC_I2S_MASTER, /* codec port, I.e. EAC is the master */
	EAC_CODEC_I2S_SLAVE,
};

/* configuration structure for I2S mode */
struct eac_i2s_conf {
	/* if enabled, then first data slot (left channel) is signaled as
	 * positive level of frame sync EAC.AC_FS */
	unsigned	polarity_changed_mode:1;
	/* if enabled, then serial data starts one clock cycle after the
	 * of EAC.AC_FS for first audio slot */
	unsigned	sync_delay_enable:1;
};

/* configuration structure for EAC codec port */
struct eac_codec {
	enum eac_mclk_src	mclk_src;

	enum eac_codec_mode	codec_mode;
	union {
		struct eac_i2s_conf	i2s;
	} codec_conf;

	int		default_rate; /* audio sampling rate */

	int		(* set_power)(void *private_data, int dac, int adc);
	int		(* register_controls)(void *private_data,
					      struct snd_card *card);
	const char 	*short_name;

	void		*private_data;
};

/* structure for passing platform dependent data to the EAC driver */
struct eac_platform_data {
        int	(* init)(struct device *eac_dev);
	void	(* cleanup)(struct device *eac_dev);
	/* these callbacks are used to configure & control external MCLK
	 * source. NULL if not used */
	int	(* enable_ext_clocks)(struct device *eac_dev);
	void	(* disable_ext_clocks)(struct device *eac_dev);
};

extern void omap_init_eac(struct eac_platform_data *pdata);

extern int eac_register_codec(struct device *eac_dev, struct eac_codec *codec);
extern void eac_unregister_codec(struct device *eac_dev);

extern int eac_set_mode(struct device *eac_dev, int play, int rec);

#endif /* __ASM_ARM_ARCH_OMAP2_EAC_H */
