/* arch/arm/plat-samsung/include/plat/adc.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/	
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C ADC driver information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_ADC_H
#define __ASM_PLAT_ADC_H __FILE__

struct s3c_adc_client;

struct s3c_adc_platdata {
	void (*phy_init)(void);
	void (*phy_exit)(void);
};

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

extern void s3c_adc_phy_init(void);
extern void s3c_adc_phy_exit(void);
extern void s3c_adc_set_platdata(struct s3c_adc_platdata *pd);

#endif /* __ASM_PLAT_ADC_H */
