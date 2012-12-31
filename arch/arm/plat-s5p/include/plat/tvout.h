/* linux/arch/arm/plat-s5p/include/plat/tvout.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Platform Header file for Samsung TV driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ARM_PLAT_TVOUT_H
#define __ARM_PLAT_TVOUT_H __FILE__

struct platform_device;

struct s5p_platform_hpd {
	void	(*int_src_hdmi_hpd)(struct platform_device *pdev);
	void	(*int_src_ext_hpd)(struct platform_device *pdev);
	int	(*read_gpio)(struct platform_device *pdev);
};

extern void s5p_hdmi_hpd_set_platdata(struct s5p_platform_hpd *pd);

/* defined by architecture to configure gpio */
extern void s5p_int_src_hdmi_hpd(struct platform_device *pdev);
extern void s5p_int_src_ext_hpd(struct platform_device *pdev);
extern void s5p_v4l2_int_src_hdmi_hpd(void);
extern void s5p_v4l2_int_src_ext_hpd(void);
extern int s5p_hpd_read_gpio(struct platform_device *pdev);
extern int s5p_v4l2_hpd_read_gpio(void);

struct s5p_platform_cec {

	void	(*cfg_gpio)(struct platform_device *pdev);
};

extern void s5p_hdmi_cec_set_platdata(struct s5p_platform_cec *pd);

/* defined by architecture to configure gpio */
extern void s5p_cec_cfg_gpio(struct platform_device *pdev);

extern void s5p_tv_setup(void);

#endif /* __ASM_PLAT_TV_HPD_H */
