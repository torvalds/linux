/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/	
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C ADC driver information
 */

#ifndef __ASM_PLAT_ADC_H
#define __ASM_PLAT_ADC_H __FILE__

struct s3c_adc_client;
struct platform_device;

extern int s3c_adc_start(struct s3c_adc_client *client,
			 unsigned int channel, unsigned int nr_samples);

extern int s3c_adc_read(struct s3c_adc_client *client, unsigned int ch);

extern struct s3c_adc_client *
	s3c_adc_register(struct platform_device *pdev,
			 void (*select)(struct s3c_adc_client *client,
					unsigned selected),
			 void (*conv)(struct s3c_adc_client *client,
				      unsigned d0, unsigned d1,
				      unsigned *samples_left),
			 unsigned int is_ts);

extern void s3c_adc_release(struct s3c_adc_client *client);

#endif /* __ASM_PLAT_ADC_H */
