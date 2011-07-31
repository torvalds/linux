/* arch/arm/plat-s3c24xx/include/plat/audio-simtec.h
 *
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Simtec Audio support.
*/

/**
 * struct s3c24xx_audio_simtec_pdata - platform data for simtec audio
 * @use_mpllin: Select codec clock from MPLLin
 * @output_cdclk: Need to output CDCLK to the codec
 * @have_mic: Set if we have a MIC socket
 * @have_lout: Set if we have a LineOut socket
 * @amp_gpio: GPIO pin to enable the AMP
 * @amp_gain: Option GPIO to control AMP gain
 */
struct s3c24xx_audio_simtec_pdata {
	unsigned int	use_mpllin:1;
	unsigned int	output_cdclk:1;

	unsigned int	have_mic:1;
	unsigned int	have_lout:1;

	int		amp_gpio;
	int		amp_gain[2];

	void	(*startup)(void);
};

extern int simtec_audio_add(const char *codec_name, bool has_lr_routing,
			    struct s3c24xx_audio_simtec_pdata *pdata);
