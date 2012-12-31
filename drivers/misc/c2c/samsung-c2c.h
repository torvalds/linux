/*
 * Samsung C2C driver
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Kisang Lee <kisang80.lee@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef SAMSUNG_C2C_H
#define SAMSUNG_C2C_H

/* This timer will be only used for debugging
#define ENABLE_C2CSTATE_TIMER
*/
#define C2C_DEV_NAME "c2c_dev"
#define C2C_SYSREG_DEFAULT 0x832AA803

#ifdef CONFIG_C2C_IPC_ENABLE
#define C2C_CP_RGN_ADDR		0x60000000
#define C2C_CP_RGN_SIZE		(56 * SZ_1M)
#define C2C_SH_RGN_ADDR		(C2C_CP_RGN_ADDR + C2C_CP_RGN_SIZE)
#define C2C_SH_RGN_SIZE		(8 * SZ_1M)

extern void __iomem *c2c_request_cp_region(unsigned int cp_addr,
		unsigned int size);
extern void __iomem *c2c_request_sh_region(unsigned int sh_addr,
		unsigned int size);
extern void c2c_release_cp_region(void *rgn);
extern void c2c_release_sh_region(void *rgn);

extern int c2c_register_handler(void (*handler)(void *), void *data);
extern int c2c_unregister_handler(void (*handler)(void *));
extern void c2c_send_interrupt(void);
extern void c2c_reset_interrupt(void);

struct c2c_ipc_handler {
	void *data;
	void (*handler)(void *);
};
#endif

enum c2c_set_clear {
	C2C_CLEAR = 0,
	C2C_SET = 1,
};

enum c2c_interrupt {
	C2C_INT_TOGGLE = 0,
	C2C_INT_HIGH = 1,
	C2C_INT_LOW = 2,
};

struct c2c_state_control {
	void __iomem *ap_sscm_addr;
	void __iomem *cp_sscm_addr;
#ifdef CONFIG_C2C_IPC_ENABLE
	void *shd_pages;
	struct c2c_ipc_handler hd;
#endif
	struct device *c2c_dev;

	u32 rx_width;
	u32 tx_width;

	u32 clk_opp100;
	u32 clk_opp50;
	u32 clk_opp25;

	struct clk *c2c_sclk;
	struct clk *c2c_aclk;

	enum c2c_opp_mode opp_mode;
	/* Below variables are needed in reset for retention */
	u32 retention_reg;
	void __iomem *c2c_sysreg;
};

static struct c2c_state_control c2c_con;

static inline void c2c_writel(u32 val, int reg)
{
	writel(val, c2c_con.ap_sscm_addr + reg);
}

static inline void c2c_writew(u16 val, int reg)
{
	writew(val, c2c_con.ap_sscm_addr + reg);
}

static inline void c2c_writeb(u8 val, int reg)
{
	writeb(val, c2c_con.ap_sscm_addr + reg);
}

static inline u32 c2c_readl(int reg)
{
	return readl(c2c_con.ap_sscm_addr + reg);
}

static inline u16 c2c_readw(int reg)
{
	return readw(c2c_con.ap_sscm_addr + reg);
}

static inline u8 c2c_readb(int reg)
{
	return readb(c2c_con.ap_sscm_addr + reg);
}

static inline void c2c_writel_cp(u32 val, int reg)
{
	writel(val, c2c_con.cp_sscm_addr + reg);
}

static inline void c2c_writew_cp(u16 val, int reg)
{
	writew(val, c2c_con.cp_sscm_addr + reg);
}

static inline void c2c_writeb_cp(u8 val, int reg)
{
	writeb(val, c2c_con.cp_sscm_addr + reg);
}

static inline u32 c2c_readl_cp(int reg)
{
	return readl(c2c_con.cp_sscm_addr + reg);
}

static inline u16 c2c_readw_cp(int reg)
{
	return readw(c2c_con.cp_sscm_addr + reg);
}

static inline u8 c2c_readb_cp(int reg)
{
	return readb(c2c_con.cp_sscm_addr + reg);
}

static inline enum c2c_set_clear c2c_get_clock_gating(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);
	if (sysreg & (1 << C2C_SYSREG_CG))
		return C2C_SET;
	else
		return C2C_CLEAR;
}

static inline void c2c_set_clock_gating(enum c2c_set_clear val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	if (val == C2C_SET)
		sysreg |= (1 << C2C_SYSREG_CG);
	else
		sysreg &= ~(1 << C2C_SYSREG_CG);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline enum c2c_set_clear c2c_get_memdone(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);
	if (sysreg & (1 << C2C_SYSREG_MD))
		return C2C_SET;
	else
		return C2C_CLEAR;
}

static inline void c2c_set_memdone(enum c2c_set_clear val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	if (val == C2C_SET)
		sysreg |= (1 << C2C_SYSREG_MD);
	else
		sysreg &= ~(1 << C2C_SYSREG_MD);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline enum c2c_set_clear c2c_get_master_on(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);
	if (sysreg & (1 << C2C_SYSREG_MO))
		return C2C_SET;
	else
		return C2C_CLEAR;
}

static inline void c2c_set_master_on(enum c2c_set_clear val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	if (val == C2C_SET)
		sysreg |= (1 << C2C_SYSREG_MO);
	else
		sysreg &= ~(1 << C2C_SYSREG_MO);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline u32 c2c_get_func_clk(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= (0x3ff << C2C_SYSREG_FCLK);

	return sysreg >> C2C_SYSREG_FCLK;
}

static inline void c2c_set_func_clk(u32 val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= ~(0x3ff << C2C_SYSREG_FCLK);
	sysreg |= (val << C2C_SYSREG_FCLK);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline u32 c2c_get_tx_buswidth(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= (0x3 << C2C_SYSREG_TXW);

	return sysreg >> C2C_SYSREG_TXW;
}

static inline void c2c_set_tx_buswidth(u32 val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= ~(0x3 << C2C_SYSREG_TXW);
	sysreg |= (val << C2C_SYSREG_TXW);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline u32 c2c_get_rx_buswidth(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= (0x3 << C2C_SYSREG_RXW);

	return sysreg >> C2C_SYSREG_RXW;
}

static inline void c2c_set_rx_buswidth(u32 val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= ~(0x3 << C2C_SYSREG_RXW);
	sysreg |= (val << C2C_SYSREG_RXW);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline enum c2c_set_clear c2c_get_reset(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);
	if (sysreg & (1 << C2C_SYSREG_RST))
		return C2C_SET;
	else
		return C2C_CLEAR;
}

static inline void c2c_set_reset(enum c2c_set_clear val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	if (val == C2C_SET)
		sysreg |= (1 << C2C_SYSREG_RST);
	else
		sysreg &= ~(1 << C2C_SYSREG_RST);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline void c2c_set_rtrst(enum c2c_set_clear val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	if (val == C2C_SET)
		sysreg |= (1 << C2C_SYSREG_RTRST);
	else
		sysreg &= ~(1 << C2C_SYSREG_RTRST);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline u32 c2c_get_base_addr(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= (0x3ff << C2C_SYSREG_BASE_ADDR);

	return sysreg >> C2C_SYSREG_BASE_ADDR;
}

static inline void c2c_set_base_addr(u32 val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= ~(0x3ff << C2C_SYSREG_BASE_ADDR);
	sysreg |= (val << C2C_SYSREG_BASE_ADDR);

	writel(sysreg, c2c_con.c2c_sysreg);
}

static inline u32 c2c_get_shdmem_size(void)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= (0x7 << C2C_SYSREG_DRAM_SIZE);

	return sysreg >> C2C_SYSREG_DRAM_SIZE;
}

static inline void c2c_set_shdmem_size(u32 val)
{
	u32 sysreg = readl(c2c_con.c2c_sysreg);

	sysreg &= ~(0x7 << C2C_SYSREG_DRAM_SIZE);
	sysreg |= (val << C2C_SYSREG_DRAM_SIZE);

	writel(sysreg, c2c_con.c2c_sysreg);
}

#endif
