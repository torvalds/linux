/* arch/arm/plat-samsung/include/plat/audio.h
 *
 * Copyright (c) 2009 Samsung Electronics Co. Ltd
 * Author: Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* The machine init code calls s3c*_ac97_setup_gpio with
 * one of these defines in order to select appropriate bank
 * of GPIO for AC97 pins
 */
#define S3C64XX_AC97_GPD  0
#define S3C64XX_AC97_GPE  1
extern void s3c64xx_ac97_setup_gpio(int);

/*
 * The machine init code calls s5p*_spdif_setup_gpio with
 * one of these defines in order to select appropriate bank
 * of GPIO for S/PDIF pins
 */
#define S5PC100_SPDIF_GPD  0
#define S5PC100_SPDIF_GPG3 1
extern void s5pc100_spdif_setup_gpio(int);

/**
 * struct s3c_audio_pdata - common platform data for audio device drivers
 * @cfg_gpio: Callback function to setup mux'ed pins in I2S/PCM/AC97 mode
 */
struct s3c_audio_pdata {
	int (*cfg_gpio)(struct platform_device *);
};
