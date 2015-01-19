#ifndef __CLK_SET_HEADER_
#define __CLK_SET_HEADER_
/*
    select clk:
    7-SYS_PLL_DIV2_CLK
    6-VID2_PLL_CLK
    5-VID_PLL_CLK
    4-AUDIO_PLL_CLK
    3-DDR_PLL_CLK
    2-MISC_PLL_CLK
    1-SYS_PLL_CLK
    0-XTAL (25Mhz)

    clk_freq:50M=50000000
    output_clk:50000000;
    aways,maybe changed for others?

*/
#define ETH_CLKSRC_XTAL_CLK             (0)
#define ETH_CLKSRC_SYS_CLK              (1)
#define ETH_CLKSRC_MISC_CLK             (2)
#define ETH_CLKSRC_DDR_CLK              (3)
#define ETH_CLKSRC_AUDIO_CLK            (4)
#define ETH_CLKSRC_VID_CLK              (5)
#define ETH_CLKSRC_VID2_CLK             (6)
#define ETH_CLKSRC_SYS_DIV2_CLK         (7)
#define CLK_1M                          (1000000)
#define ETH_VALIDE_CLKSRC(clk,out_clk)  ((clk%out_clk)==0)

int eth_clk_set(int selectclk, unsigned long clk_freq, unsigned long out_clk, unsigned int clk_invert);
int sys_clkpll_setting(unsigned crystal_freq, unsigned out_freq);
unsigned long get_xtal_clock(void);
int misc_pll_setting(unsigned crystal_freq, unsigned  out_freq);
int audio_pll_setting(unsigned crystal_freq, unsigned  out_freq);
int video_pll_setting(unsigned crystal_freq, unsigned  out_freq, int powerdown, int flags);

#endif
