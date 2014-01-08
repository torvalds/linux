#ifndef __RK_CLKOPS_DTSI_H
#define __RK_CLKOPS_DTSI_H


/* pll id */
#define APLL_ID 	0
#define DPLL_ID 	1
#define CPLL_ID 	2
#define GPLL_ID 	3



/* rate_ops index */
#define CLKOPS_RATE_MUX_DIV		0
#define CLKOPS_RATE_EVENDIV		1
#define CLKOPS_RATE_DCLK_LCDC		2
#define CLKOPS_RATE_CIFOUT		3
#define CLKOPS_RATE_I2S_FRAC		4
#define CLKOPS_RATE_I2S			5
#define CLKOPS_RATE_HSADC_FRAC		6
#define CLKOPS_RATE_UART_FRAC		7
#define CLKOPS_RATE_UART		8
#define CLKOPS_RATE_HSADC		9
#define CLKOPS_RATE_MAC_REF		10
#define CLKOPS_TABLE_END		~0

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif
#define CLK_DIVIDER_PLUS_ONE		(0)
#define CLK_DIVIDER_ONE_BASED		BIT(0)
#define CLK_DIVIDER_POWER_OF_TWO	BIT(1)
#define CLK_DIVIDER_ALLOW_ZERO		BIT(2)

/* Rockchip special defined */
#define CLK_DIVIDER_FIXED		BIT(6)
#define CLK_DIVIDER_USER_DEFINE		BIT(7)
/* CLK_DIVIDER_MASK defined the bits been used above */
#define CLK_DIVIDER_MASK		(0xFF)
#endif /* __RK_CLKOPS_H */
