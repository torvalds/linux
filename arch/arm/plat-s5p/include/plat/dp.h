/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * Samsung S5P series DP device support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PLAT_S5P_DP_H_
#define PLAT_S5P_DP_H_ __FILE__

#include <video/s5p-dp.h>

extern void s5p_dp_set_platdata(struct s5p_dp_platdata *pd);
extern void s5p_dp_phy_init(void);
extern void s5p_dp_phy_exit(void);

#endif /* PLAT_S5P_DP_H_ */
