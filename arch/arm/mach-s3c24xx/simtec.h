/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Simtec common functions
 */

struct s3c24xx_audio_simtec_pdata;

extern void nor_simtec_init(void);

extern int usb_simtec_init(void);

extern int simtec_audio_add(const char *codec_name, bool has_lr_routing,
			    struct s3c24xx_audio_simtec_pdata *pdata);
