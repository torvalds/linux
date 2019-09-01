#ifdef CONFIG_PXA3xx
extern unsigned	pxa3xx_get_clk_frequency_khz(int);
#else
#define pxa3xx_get_clk_frequency_khz(x)		(0)
#endif
