/* linux/drivers/video/samsung/s5p-dsim_lowlevel.c
 *
 * Samsung MIPI-DSIM lowlevel driver.
 *
 * InKi Dae, <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <mach/map.h>
#include <mach/dsim.h>
#include <mach/mipi_ddi.h>
#include <plat/regs-dsim.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <asm/io.h>
#include <mach/map.h>
#include <plat/regs-fb-s5p.h>

#ifdef DEBUG_DSIM
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif

void s5p_dsim_func_reset(unsigned int dsim_base)
{
	unsigned int cfg = 0;

	cfg = DSIM_FUNCRST;

	writel(cfg, dsim_base + S5P_DSIM_SWRST);

	dprintk("%s : %x\n", __func__, cfg);
}

void s5p_dsim_sw_reset(unsigned int dsim_base)
{
	unsigned int cfg = 0;

	cfg = DSIM_SWRST;

	writel(cfg, dsim_base + S5P_DSIM_SWRST);

	dprintk("%s : %x\n", __func__, cfg);
}

void s5p_dsim_set_interrupt_mask(unsigned int dsim_base, unsigned int mode,
	unsigned char mask)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_INTMSK);

	if (mask)
		reg |= mode;
	else
		reg &= ~(mode);

	writel(reg, dsim_base + S5P_DSIM_INTMSK);

	/* dprintk("%s : %x\n", __func__, reg); */
}

void s5p_dsim_init_fifo_pointer(unsigned int dsim_base, unsigned char cfg)
{
	unsigned int reg;

	reg = readl(dsim_base + S5P_DSIM_FIFOCTRL);

	writel(reg & ~(cfg), dsim_base + S5P_DSIM_FIFOCTRL);
	msleep(10);
	reg |= cfg;

	writel(reg, dsim_base + S5P_DSIM_FIFOCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

/*
 * this function set PLL P, M and S value in D-PHY
 */
void s5p_dsim_set_phy_tunning(unsigned int dsim_base, unsigned int value)
{
	writel(DSIM_AFC_CTL(value), dsim_base + S5P_DSIM_PHYACCHR);

	dprintk("%s : %x\n", __func__, DSIM_AFC_CTL(value));
}

void s5p_dsim_set_main_disp_resol(unsigned int dsim_base, unsigned short vert_resol,
	unsigned short hori_resol)
{
	unsigned int reg;

	/* standby should be set after configuration so set to not ready*/
	reg = (readl(dsim_base + S5P_DSIM_MDRESOL)) & ~(DSIM_MAIN_STAND_BY);
	writel(reg, dsim_base + S5P_DSIM_MDRESOL);

	dprintk("%s : %x\n", __func__, reg);

	reg &= ~(0x7ff << 16) & ~(0x7ff << 0);
	reg |= DSIM_MAIN_VRESOL(vert_resol) | DSIM_MAIN_HRESOL(hori_resol);

	reg |= DSIM_MAIN_STAND_BY;
	writel(reg, dsim_base + S5P_DSIM_MDRESOL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_main_disp_vporch(unsigned int dsim_base, unsigned int cmd_allow,
	unsigned int vfront, unsigned int vback)
{
	unsigned int reg;

	reg = (readl(dsim_base + S5P_DSIM_MVPORCH)) & ~(DSIM_CMD_ALLOW_MASK) &
		~(DSIM_STABLE_VFP_MASK) & ~(DSIM_MAIN_VBP_MASK);

	reg |= ((cmd_allow & 0xf) << DSIM_CMD_ALLOW_SHIFT) |
		((vfront & 0x7ff) << DSIM_STABLE_VFP_SHIFT) |
		((vback & 0x7ff) << DSIM_MAIN_VBP_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_MVPORCH);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_main_disp_hporch(unsigned int dsim_base, unsigned short front,
	unsigned short back)
{
	unsigned int reg;

	reg = (readl(dsim_base + S5P_DSIM_MHPORCH)) & ~(DSIM_MAIN_HFP_MASK) &
		~(DSIM_MAIN_HBP_MASK);

	reg |= (front << DSIM_MAIN_HFP_SHIFT) | (back << DSIM_MAIN_HBP_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_MHPORCH);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_main_disp_sync_area(unsigned int dsim_base, unsigned short vert,
	unsigned short hori)
{
	unsigned int reg;

	reg = (readl(dsim_base + S5P_DSIM_MSYNC)) & ~(DSIM_MAIN_VSA_MASK) &
		~(DSIM_MAIN_HSA_MASK);

	reg |= ((vert & 0x3ff) << DSIM_MAIN_VSA_SHIFT) | (hori << DSIM_MAIN_HSA_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_MSYNC);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_sub_disp_resol(unsigned int dsim_base, unsigned short vert,
	unsigned short hori)
{
	unsigned int reg;

	reg = (readl(dsim_base + S5P_DSIM_SDRESOL)) & ~(DSIM_SUB_STANDY_MASK);
	writel(reg, dsim_base + S5P_DSIM_SDRESOL);

	dprintk("%s : %x\n", __func__, reg);

	reg &= ~(DSIM_SUB_VRESOL_MASK) | ~(DSIM_SUB_HRESOL_MASK);
	reg |= ((vert & 0x7ff) << DSIM_SUB_VRESOL_SHIFT) |
		((hori & 0x7ff) << DSIM_SUB_HRESOL_SHIFT);
	writel(reg, dsim_base + S5P_DSIM_SDRESOL);

	dprintk("%s : %x\n", __func__, reg);

	reg |= (1 << DSIM_SUB_STANDY_SHIFT);
	writel(reg, dsim_base + S5P_DSIM_SDRESOL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_init_config(unsigned int dsim_base, struct dsim_lcd_config *main_lcd_info,
	struct dsim_lcd_config *sub_lcd_info, struct dsim_config *dsim_info)
{
	unsigned int cfg = (readl(dsim_base + S5P_DSIM_CONFIG)) &
		~(1 << 28) & ~(1 << 27) & ~(0x1f << 20) & ~(0x3 << 5);

	cfg =	(dsim_info->auto_flush << 29) |		/* evt1 */
		(dsim_info->eot_disable << 28) |	/* evt0 or evt1 */
		(dsim_info->auto_vertical_cnt << DSIM_AUTO_MODE_SHIFT) |
		(dsim_info->hse << DSIM_HSE_MODE_SHIFT) |
		(dsim_info->hfp << DSIM_HFP_MODE_SHIFT) |
		(dsim_info->hbp << DSIM_HBP_MODE_SHIFT) |
		(dsim_info->hsa << DSIM_HSA_MODE_SHIFT) |
		(dsim_info->e_no_data_lane << DSIM_NUM_OF_DATALANE_SHIFT);

	writel(cfg, dsim_base + S5P_DSIM_CONFIG);

	dprintk("%s : %x\n", __func__, cfg);
}

void s5p_dsim_display_config(unsigned int dsim_base,
	struct dsim_lcd_config *main_lcd, struct dsim_lcd_config *sub_lcd)
{
	u32 reg = (readl(dsim_base + S5P_DSIM_CONFIG)) &
		~(0x3 << 26) & ~(1 << 25) & ~(0x3 << 18) & ~(0x7 << 12) &
		~(0x3 << 16) & ~(0x7 << 8);

	if (main_lcd->e_interface == DSIM_VIDEO)
		reg |= (1 << 25);
	else if (main_lcd->e_interface == DSIM_COMMAND)
		reg &= ~(1 << 25);
	else {
		printk(KERN_ERR "this ddi is not MIPI interface.\n");
		return;
	}

	/* main lcd */
	reg |= ((u8) (main_lcd->parameter[DSI_VIDEO_MODE_SEL]) & 0x3) << 26 |
		((u8) (main_lcd->parameter[DSI_VIRTUAL_CH_ID]) & 0x3) << 18 |
		((u8) (main_lcd->parameter[DSI_FORMAT]) & 0x7) << 12;

	/* sub lcd */
	if (main_lcd->e_interface == DSIM_COMMAND && sub_lcd != NULL) {
		reg |= ((u8) (sub_lcd->parameter[DSI_VIRTUAL_CH_ID]) & 0x3) << 16 |
			((u8) (sub_lcd->parameter[DSI_FORMAT]) & 0x7) << 8;
	}

	writel(reg, dsim_base + S5P_DSIM_CONFIG);
	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_enable_lane(unsigned int dsim_base, unsigned char lane, unsigned char enable)
{
	unsigned int reg;

	dprintk("%s : %x\n", __func__, lane);
	reg = readl(dsim_base + S5P_DSIM_CONFIG);

	if (lane == DSIM_LANE_CLOCK) {
		if (enable)
			reg |= (1 << 0);
		else
			reg &= ~(1 << 0);
	} else {
		if (enable)
			reg |= (lane << 1);
		else
			reg &= ~(lane << 1);
	}

	writel(reg, dsim_base + S5P_DSIM_CONFIG);

	dprintk("%s : %x\n", __func__, reg);
}


void s5p_dsim_set_data_lane_number(unsigned int dsim_base, unsigned char count)
{
	unsigned int cfg = 0;

	/* set the data lane number. */
	cfg = DSIM_NUM_OF_DATA_LANE(count);

	writel(cfg, dsim_base + S5P_DSIM_CONFIG);

	dprintk("%s : %x\n", __func__, cfg);
}

void s5p_dsim_enable_afc(unsigned int dsim_base, unsigned char enable,
	unsigned char afc_code)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_PHYACCHR);

	if (enable) {
		reg |= (1 << 14);
		reg &= ~(0x7 << 5);
		reg |= (afc_code & 0x7) << 5;
	} else
		reg &= ~(1 << 14);

	writel(reg, dsim_base + S5P_DSIM_PHYACCHR);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_enable_pll_bypass(unsigned int dsim_base, unsigned char enable)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(DSIM_PLL_BYPASS_EXTERNAL);

	reg |= enable << DSIM_PLL_BYPASS_SHIFT;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_pll_pms(unsigned int dsim_base, unsigned char p,
	unsigned short m, unsigned short s)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_PLLCTRL);

	reg |= ((p & 0x3f) << 13) | ((m & 0x1ff) << 4) | ((s & 0x7) << 1);

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_pll_freq_band(unsigned int dsim_base, unsigned char freq_band)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_PLLCTRL)) &
		~(0x1f << DSIM_FREQ_BAND_SHIFT);

	reg |= ((freq_band & 0x1f) << DSIM_FREQ_BAND_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_pll_freq(unsigned int dsim_base, unsigned char pre_divider,
	unsigned short main_divider, unsigned char scaler)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_PLLCTRL)) &
		~(0x7ffff << 1);

	reg |= (pre_divider & 0x3f) << 13 | (main_divider & 0x1ff) << 4 |
		(scaler & 0x7) << 1;

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);
}

void s5p_dsim_pll_stable_time(unsigned int dsim_base, unsigned int lock_time)
{
	writel(lock_time, dsim_base + S5P_DSIM_PLLTMR);

	dprintk("%s : %x\n", __func__, lock_time);
}

void s5p_dsim_enable_pll(unsigned int dsim_base, unsigned char enable)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_PLLCTRL)) &
		~(0x1 << DSIM_PLL_EN_SHIFT);

	reg |= ((enable & 0x1) << DSIM_PLL_EN_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_byte_clock_src(unsigned int dsim_base, unsigned char src)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(0x3 << DSIM_BYTE_CLK_SRC_SHIFT);

	reg |= ((unsigned int) src) << DSIM_BYTE_CLK_SRC_SHIFT;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_enable_byte_clock(unsigned int dsim_base, unsigned char enable)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(1 << DSIM_BYTE_CLKEN_SHIFT);

	reg |= enable << DSIM_BYTE_CLKEN_SHIFT;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_esc_clk_prs(unsigned int dsim_base, unsigned char enable,
	unsigned short prs_val)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(1 << DSIM_ESC_CLKEN_SHIFT) & ~(0xffff);

	reg |= enable << DSIM_ESC_CLKEN_SHIFT;
	if (enable)
		reg |= prs_val;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_enable_esc_clk_on_lane(unsigned int dsim_base,
	unsigned char lane_sel, unsigned char enable)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_CLKCTRL);

	if (enable) {
		if (lane_sel & DSIM_LANE_CLOCK)
			reg |= 1 << DSIM_LANE_ESC_CLKEN_SHIFT;
		if (lane_sel & DSIM_LANE_DATA0)
			reg |= 1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 1);
		if (lane_sel & DSIM_LANE_DATA1)
			reg |= 1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 2);
		if (lane_sel & DSIM_LANE_DATA2)
			reg |= 1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 3);
		if (lane_sel & DSIM_LANE_DATA2)
			reg |= 1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 4);
	} else {
		if (lane_sel & DSIM_LANE_CLOCK)
			reg &= ~(1 << DSIM_LANE_ESC_CLKEN_SHIFT);
		if (lane_sel & DSIM_LANE_DATA0)
			reg &= ~(1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 1));
		if (lane_sel & DSIM_LANE_DATA1)
			reg &= ~(1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 2));
		if (lane_sel & DSIM_LANE_DATA2)
			reg &= ~(1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 3));
		if (lane_sel & DSIM_LANE_DATA2)
			reg &= ~(1 << (DSIM_LANE_ESC_CLKEN_SHIFT + 4));
	}

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_force_dphy_stop_state(unsigned int dsim_base, unsigned char enable)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_ESCMODE)) &
		~(0x1 << DSIM_FORCE_STOP_STATE_SHIFT);

	reg |= ((enable & 0x1) << DSIM_FORCE_STOP_STATE_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_ESCMODE);

	dprintk("%s : %x\n", __func__, reg);
}

unsigned char s5p_dsim_is_lane_state(unsigned int dsim_base, unsigned char lane)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_STATUS);

	dprintk("%s : %x\n", __func__, dsim_base);
	dprintk("%s : %x\n", __func__, lane);
	dprintk("%s : %x\n", __func__, reg);

	if ((lane & DSIM_LANE_ALL) > DSIM_LANE_CLOCK) { /* all lane state */
		if ((reg & 0x7ff) ^ (((lane & 0xf) << 4) | (1 << 9)))
			return DSIM_LANE_STATE_ULPS;
		else if ((reg & 0x7ff) ^ (((lane & 0xf) << 0) | (1 << 8)))
			return DSIM_LANE_STATE_STOP;
		else {
			printk(KERN_ERR "lane state is unknown.\n");
			return -1;
		}
	} else if (lane & DSIM_LANE_DATA_ALL) {	/* data lane */
		if (reg & (lane << 4)) {
			return DSIM_LANE_STATE_ULPS;
		} else if (reg & (lane << 0)) {
			return DSIM_LANE_STATE_STOP;
		} else {
			printk(KERN_ERR "data lane state is unknown.\n");
			return -1;
		}
	} else if (lane & DSIM_LANE_CLOCK) { /* clock lane */
		if (reg & (1 << 9)) {
			return DSIM_LANE_STATE_ULPS;
		} else if (reg & (1 << 8)) {
			return DSIM_LANE_STATE_STOP;
		} else if (reg & (1 << 10)) {
			return DSIM_LANE_STATE_HS_READY;
		} else {
			printk(KERN_ERR "clock lane state is unknown.\n");
			return -1;
		}
	}

	return 0;
}

void s5p_dsim_set_stop_state_counter(unsigned int dsim_base, unsigned short cnt_val)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_ESCMODE)) &
		~(0x7ff << DSIM_STOP_STATE_CNT_SHIFT);

	reg |= ((cnt_val & 0x7ff) << DSIM_STOP_STATE_CNT_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_ESCMODE);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_bta_timeout(unsigned int dsim_base, unsigned char timeout)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_TIMEOUT)) &
		~(0xff << DSIM_BTA_TOUT_SHIFT);

	reg |= (timeout << DSIM_BTA_TOUT_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_TIMEOUT);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_lpdr_timeout(unsigned int dsim_base, unsigned short timeout)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_TIMEOUT)) &
		~(0xffff << DSIM_LPDR_TOUT_SHIFT);

	reg |= (timeout << DSIM_LPDR_TOUT_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_TIMEOUT);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_set_data_mode(unsigned int dsim_base, unsigned char data,
	unsigned char state)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_ESCMODE);

	if (state == DSIM_STATE_HSCLKEN)
		reg &= ~data;
	else
		reg |= data;

	writel(reg, dsim_base + S5P_DSIM_ESCMODE);

	dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_enable_hs_clock(unsigned int dsim_base, unsigned char enable)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(1 << DSIM_TX_REQUEST_HSCLK_SHIFT);

	reg |= enable << DSIM_TX_REQUEST_HSCLK_SHIFT;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);
}

void s5p_dsim_toggle_hs_clock(unsigned int dsim_base)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_CLKCTRL)) &
		~(1 << DSIM_TX_REQUEST_HSCLK_SHIFT);

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	reg |= 1 << DSIM_TX_REQUEST_HSCLK_SHIFT;

	writel(reg, dsim_base + S5P_DSIM_CLKCTRL);

	dprintk("%s\n", __func__);
}


void s5p_dsim_dp_dn_swap(unsigned int dsim_base, unsigned char swap_en)
{
	unsigned int reg = readl(dsim_base + S5P_DSIM_PHYACCHR1);

	reg &= ~(0x3 << 0);
	reg |= (swap_en & 0x3) << 0;

	writel(reg, dsim_base + S5P_DSIM_PHYACCHR1);

	dprintk("%s : %x\n", __func__, readl(dsim_base + S5P_DSIM_PHYACCHR1));
}

void s5p_dsim_hs_zero_ctrl(unsigned int dsim_base, unsigned char hs_zero)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_PLLCTRL)) &
		~(0xf << 28);

	reg |= ((hs_zero & 0xf) << 28);

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);
	//dprintk("%s : %x\n", __func__, readl(dsim_base + S5P_DSIM_PLLCTRL));
}

void s5p_dsim_prep_ctrl(unsigned int dsim_base, unsigned char prep)
{
	unsigned int reg = (readl(dsim_base + S5P_DSIM_PLLCTRL)) &
		~(0x7 << 20);

	reg |= ((prep & 0x7) << 20);

	writel(reg, dsim_base + S5P_DSIM_PLLCTRL);
	dprintk("%s : %x\n", __func__, readl(dsim_base + S5P_DSIM_PLLCTRL));
}

void s5p_dsim_clear_interrupt(unsigned int dsim_base, unsigned int int_src)
{
	writel(int_src, dsim_base + S5P_DSIM_INTSRC);
}

unsigned char s5p_dsim_is_pll_stable(unsigned int dsim_base)
{
	return (unsigned char) ((readl(dsim_base + S5P_DSIM_STATUS) &
		    (1 << 31)) >> 31);
}

unsigned int s5p_dsim_get_fifo_state(unsigned int dsim_base)
{
	return ((readl(dsim_base + S5P_DSIM_FIFOCTRL)) & ~(0x1f));
}

void s5p_dsim_wr_tx_header(unsigned int dsim_base,
	unsigned char di, unsigned char data0, unsigned char data1)
{
	unsigned int reg = (data1 << 16) | (data0 << 8) | ((di & 0x3F) << 0);

	writel(reg, dsim_base + S5P_DSIM_PKTHDR);

	//dprintk("%s : %x\n", __func__, reg);
}

void s5p_dsim_wr_tx_data(unsigned int dsim_base, unsigned int tx_data)
{
	writel(tx_data, dsim_base + S5P_DSIM_PAYLOAD);

	//dprintk("%s : %x\n", __func__, tx_data);
}

int s5p_dsim_rd_rx_data(unsigned int dsim_base)
{
	return readl(dsim_base + S5P_DSIM_RXFIFO);
}

