/* linux/arch/arm/mach-exynos/include/mach/c2c.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Platform header file for Samsung C2C Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __ASM_PLAT_C2C_H
#define __ASM_PLAT_C2C_H __FILE__

#define C2C_SHAREDMEM_BASE 0x60000000

enum c2c_opp_mode {
	C2C_OPP0 = 0,
	C2C_OPP25 = 1,
	C2C_OPP50 = 2,
	C2C_OPP100 = 3,
};

enum c2c_buswidth {
	C2C_BUSWIDTH_8 = 0,
	C2C_BUSWIDTH_10 = 1,
	C2C_BUSWIDTH_16 = 2,
};

enum c2c_shrdmem_size {
	C2C_MEMSIZE_4 = 0,
	C2C_MEMSIZE_8 = 1,
	C2C_MEMSIZE_16 = 2,
	C2C_MEMSIZE_32 = 3,
	C2C_MEMSIZE_64 = 4,
	C2C_MEMSIZE_128 = 5,
	C2C_MEMSIZE_256 = 6,
	C2C_MEMSIZE_512 = 7,
};

struct exynos_c2c_platdata {
	void (*setup_gpio)(enum c2c_buswidth rx_width, enum c2c_buswidth tx_width);
	void (*set_cprst)(void);
	void (*clear_cprst)(void);
	u32 (*get_c2c_state)(void);

	u32 shdmem_addr;
	enum c2c_shrdmem_size shdmem_size;

	void __iomem *ap_sscm_addr;
	void __iomem *cp_sscm_addr;

	enum c2c_buswidth rx_width;
	enum c2c_buswidth tx_width;
	u32 clk_opp100;	/* clock of OPP100 mode */
	u32 clk_opp50;	/* clock of OPP50 mode */
	u32 clk_opp25;	/* clock of OPP25 */
	enum c2c_opp_mode default_opp_mode;

	void __iomem *c2c_sysreg;	/* System Register address for C2C */
	char *c2c_clk;
};

void exynos_c2c_set_platdata(struct exynos_c2c_platdata *pd);
extern void exynos_c2c_cfg_gpio(enum c2c_buswidth rx_width, enum c2c_buswidth tx_width);
extern void exynos_c2c_set_cprst(void);
extern void exynos_c2c_clear_cprst(void);
#endif /*__ASM_PLAT_C2C_H */
