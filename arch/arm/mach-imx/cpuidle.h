/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 */

#ifdef CONFIG_CPU_IDLE
extern int imx5_cpuidle_init(void);
extern int imx6q_cpuidle_init(void);
extern int imx6sl_cpuidle_init(void);
extern int imx6sx_cpuidle_init(void);
extern int imx7ulp_cpuidle_init(void);
#else
static inline int imx5_cpuidle_init(void)
{
	return 0;
}
static inline int imx6q_cpuidle_init(void)
{
	return 0;
}
static inline int imx6sl_cpuidle_init(void)
{
	return 0;
}
static inline int imx6sx_cpuidle_init(void)
{
	return 0;
}
static inline int imx7ulp_cpuidle_init(void)
{
	return 0;
}
#endif
