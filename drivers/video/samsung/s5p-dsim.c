/* linux/drivers/video/samsung/s5p-dsim.c
 *
 * Samsung MIPI-DSIM driver.
 *
 * InKi Dae, <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modified by Samsung Electronics (UK) on May 2010
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/memory.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <plat/clock.h>
#include <plat/regs-dsim.h>
#include <plat/gpio-cfg.h>
#include <mach/map.h>
#include <mach/dsim.h>
#include <mach/mipi_ddi.h>
#include <mach/irqs.h>

#include "s5p-dsim.h"
#include "s5p_dsim_lowlevel.h"
#include "s3cfb.h"

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#include <linux/suspend.h>
#endif


/* Indicates the state of the device */
struct dsim_global {
	struct device *dev;
	struct clk *clock;
	struct s5p_platform_dsim *pd;
	struct dsim_config *dsim_info;
	struct dsim_lcd_config *dsim_lcd_info;
	/* lcd panel data. */
	struct s3cfb_lcd *lcd_panel_info;
	/* platform and machine specific data for lcd panel driver. */
	struct mipi_ddi_platform_data *mipi_ddi_pd;
	/* lcd panel driver based on MIPI-DSI. */
	struct mipi_lcd_driver *mipi_drv;

	unsigned int irq;
	unsigned int te_irq;
	unsigned int reg_base;
	unsigned char state;
	unsigned int data_lane;
	enum dsim_byte_clk_src e_clk_src;
	unsigned long hs_clk;
	unsigned long byte_clk;
	unsigned long escape_clk;
	unsigned char freq_band;
	char header_fifo_index[DSIM_HEADER_FIFO_SZ];
#ifdef CONFIG_HAS_WAKELOCK
	struct early_suspend    early_suspend;
	struct wake_lock        idle_lock;
#endif
};

struct mipi_lcd_info {
	struct list_head	list;
	struct mipi_lcd_driver	*mipi_drv;
};

static DEFINE_MUTEX(dsim_rd_wr_mutex);
static DECLARE_COMPLETION(dsim_rd_comp);
static DECLARE_COMPLETION(dsim_wr_comp);

#define MIPI_RESP_ERR				0x02
#define MIPI_RESP_EOTP			0x08
#define MIPI_RESP_GENERIC_RD_1		0x11
#define MIPI_RESP_GENERIC_RD_2		0x12
#define MIPI_RESP_GENERIC_RD_LONG		0x1A
#define MIPI_RESP_DCS_RD_LONG		0x1C
#define MIPI_RESP_DCS_RD_1			0x21
#define MIPI_RESP_DCS_RD_2			0x22

#define MIPI_CMD_GENERIC_WR_0		0x03
#define MIPI_CMD_GENERIC_WR_1		0x13
#define MIPI_CMD_GENERIC_WR_2		0x23
#define MIPI_CMD_GENERIC_WR_LONG		0x29

#define MIPI_CMD_DSI_WR_0			0x05
#define MIPI_CMD_DSI_WR_1			0x15
#define MIPI_CMD_DSI_WR_LONG			0x39

#define MIPI_CMD_GENERIC_RD_0		0x04
#define MIPI_CMD_GENERIC_RD_1		0x14
#define MIPI_CMD_GENERIC_RD_2		0x24

#define MIPI_CMD_DSI_RD_0			0x06

#define MIPI_CMD_DSI_SET_PKT_SZ		0x37

#define DSIM_TIMEOUT				msecs_to_jiffies(250)
#define DSIM_RX_FIFO_READ_DONE		0x30800002
#define DSIM_MAX_RX_FIFO			20

#define S5P_DSIM_INT_SFR_FIFO_EMPTY		29
#define S5P_DSIM_INT_BTA			25
#define S5P_DSIM_INT_MSK_FRAME_DONE		24
#define S5P_DSIM_INT_RX_TIMEOUT		21
#define S5P_DSIM_INT_BTA_TIMEOUT		20
#define S5P_DSIM_INT_RX_DONE			18
#define S5P_DSIM_INT_RX_TE			17
#define S5P_DSIM_INT_RX_ACK			16
#define S5P_DSIM_INT_RX_ECC_ERR		15
#define S5P_DSIM_IMT_RX_CRC_ERR		14

static LIST_HEAD(lcd_info_list);
static DEFINE_MUTEX(mipi_lock);
static struct dsim_global dsim;
struct delayed_work dsim_work;
struct delayed_work check_hs_toggle_work;

static unsigned int dsim_toggle_per_frame_count;

void s5p_dsim_frame_done_interrupt_enable(u8 enable)
{
	u32 intmsk;
	u8 state = !enable;

	if (!dsim.mipi_ddi_pd->resume_complete)
		return;

#if 0
	if (state == 0) /* enable Frame Done interrupts */
		writel(0xF237FFFF, dsim.reg_base + S5P_DSIM_INTMSK);
	else /* disable Frame Done interrupts */
		writel(0xF337FFFF, dsim.reg_base + S5P_DSIM_INTMSK);
#else
	intmsk = readl(dsim.reg_base + S5P_DSIM_INTMSK);

	if (state == 0)
		intmsk &= ~(0x01 << S5P_DSIM_INT_MSK_FRAME_DONE);
	else
		intmsk |= (0x01 << S5P_DSIM_INT_MSK_FRAME_DONE);

	writel(intmsk, dsim.reg_base + S5P_DSIM_INTMSK);
#endif
}

void set_dsim_lcd_enabled(void)
{
	dsim.dsim_lcd_info->lcd_enabled = 1;
	if (dsim.dsim_info->hs_toggle)
		s5p_dsim_frame_done_interrupt_enable(1);
}

void set_dsim_hs_clk_toggle_count(u8 count)
{
	dsim_toggle_per_frame_count = count;
	if (dsim.dsim_lcd_info->lcd_enabled)
		s5p_dsim_frame_done_interrupt_enable(count ? 1 : 0);
}

static void dsim_work_q_handler(struct work_struct *work)
{
	s5p_dsim_frame_done_interrupt_enable(1);
}

static void dsim_check_hs_toggle_work_q_handler(struct work_struct *work)
{
	if (dsim.dsim_info->hs_toggle) {
		dev_info(dsim.dev, "check_hs_toggle.\n");
		schedule_delayed_work(&check_hs_toggle_work,\
			msecs_to_jiffies(120000));
	}
}

struct s5p_platform_dsim *to_dsim_plat(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return (struct s5p_platform_dsim *)pdev->dev.platform_data;
}

unsigned char s5p_dsim_wr_data(unsigned int dsim_base,
	unsigned int data_id, unsigned int data0, unsigned int data1)
{
	if (dsim.state == DSIM_STATE_ULPS) {
		dev_err(dsim.dev, "state is ULPS\n");
		return DSIM_FALSE;
	}

	if (dsim.mipi_ddi_pd->resume_complete == 0) {
		printk(KERN_INFO "DSIM Status: SUSPEND\n");
		return DSIM_FALSE;
	}

	mutex_lock(&dsim_rd_wr_mutex);

	switch (data_id) {
	/* short packet types of packet types for command. */
	case GEN_SHORT_WR_NO_PARA:
	case GEN_SHORT_WR_1_PARA:
	case GEN_SHORT_WR_2_PARA:
	case DCS_WR_NO_PARA:
	case DCS_WR_1_PARA:
	case SET_MAX_RTN_PKT_SIZE:
		s5p_dsim_wr_tx_header(dsim_base, (unsigned char) data_id,
			(unsigned char) data0, (unsigned char) data1);
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE; /* response should be implemented */
	/* general command */
	case CMD_OFF:
	case CMD_ON:
	case SHUT_DOWN:
	case TURN_ON:
		s5p_dsim_wr_tx_header(dsim_base, (unsigned char) data_id,
			(unsigned char) data0, (unsigned char) data1);
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE; /* response should be implemented. */
	/* packet types for video data */
	case VSYNC_START:
	case VSYNC_END:
	case HSYNC_START:
	case HSYNC_END:
	case EOT_PKT:
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE;

	/* short and response packet types for command */
	case GEN_RD_1_PARA:
	case GEN_RD_2_PARA:
	case GEN_RD_NO_PARA:
	case DCS_RD_NO_PARA:
		s5p_dsim_clear_interrupt(dsim_base, 0xffffffff);
		s5p_dsim_wr_tx_header(dsim_base, (unsigned char) data_id,
			(unsigned char) data0, (unsigned char) data1);
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_FALSE; /* response should be implemented. */

	/* long packet type and null packet */
	case NULL_PKT:
	case BLANKING_PKT:
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE;
	case GEN_LONG_WR:
	case DCS_LONG_WR:
	{
		u32 uCnt = 0;
		u32* pWordPtr = (u32 *)data0;
		INIT_COMPLETION(dsim_wr_comp);

		do {
			s5p_dsim_wr_tx_data(dsim_base, pWordPtr[uCnt]);
		} while (((data1-1) / 4) > uCnt++);

		/* put data into header fifo */
		s5p_dsim_wr_tx_header(dsim_base, (unsigned char) data_id,
			(unsigned char) (((unsigned short) data1) & 0xff),
			(unsigned char) ((((unsigned short) data1) & 0xff00) >> 8));

		if (!wait_for_completion_interruptible_timeout(&dsim_wr_comp, DSIM_TIMEOUT)) {
			printk(KERN_ERR "[DSIM:ERROR] %s Timeout\n", __func__);
			mutex_unlock(&dsim_rd_wr_mutex);
			return DSIM_FALSE;
		}
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE;
	}
	/* packet typo for video data */
	case RGB565_PACKED:
	case RGB666_PACKED:
	case RGB666_LOOSLY:
	case RGB888_PACKED:
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_TRUE; /* response should be implemented. */
	default:
		dev_warn(dsim.dev, "data id %x is not supported current DSI spec.\n", data_id);
		mutex_unlock(&dsim_rd_wr_mutex);
		return DSIM_FALSE;
	}
}

int s5p_dsim_rd_data(unsigned int reg_base, u8 addr, u16 count, u8 *buf)
{
	u32 i, temp;
	u8 response = 0;
	u16 rxsize;
	u32 txhd;
	u32 rxhd;
	int j;

	printk(KERN_INFO "%s called, count : %d\n", __func__, count);

	if (dsim.mipi_ddi_pd->resume_complete == 0) {
		printk(KERN_INFO "DSIM Status: SUSPEND\n");
		return DSIM_FALSE;
	}

	mutex_lock(&dsim_rd_wr_mutex);
	INIT_COMPLETION(dsim_rd_comp);

	switch (count) {
	case 1:
		response = MIPI_RESP_GENERIC_RD_1;
		break;
	case 2:
		response = MIPI_RESP_GENERIC_RD_2;
		break;
	default:
		response = MIPI_RESP_GENERIC_RD_LONG;
		break;
	}

	/* set return packet size */
	txhd = MIPI_CMD_DSI_SET_PKT_SZ | count << 8;

	writel(txhd, reg_base + S5P_DSIM_PKTHDR);

	/* set address to read */
	txhd = MIPI_CMD_GENERIC_RD_1 | addr << 8;

	writel(txhd, reg_base + S5P_DSIM_PKTHDR);

	if (!wait_for_completion_interruptible_timeout(&dsim_rd_comp, DSIM_TIMEOUT)) {
		printk(KERN_ERR "ERROR:%s timout\n", __func__);
		mutex_unlock(&dsim_rd_wr_mutex);
		return 0;
	}

	rxhd = readl(reg_base + S5P_DSIM_RXFIFO);
	printk(KERN_INFO "rxhd : %x\n", rxhd);
	if ((u8)(rxhd & 0xff) != response) {
		printk(KERN_ERR "[DSIM:ERROR]:%s wrong response rxhd : %x, response:%x\n"
		    , __func__, rxhd, response);
		goto clear_rx_fifo;
	}
	/* for short packet */
	if (count <= 2) {
		for (i = 0; i < count; i++)
			buf[i] = (rxhd >> (8+(i*8))) & 0xff;
		rxsize = count;
	} else {
		/* for long packet */
		rxsize = (u16)((rxhd & 0x00ffff00) >> 8);
		printk(KERN_INFO "rcv size : %d\n", rxsize);
		if (rxsize != count) {
			printk(KERN_ERR "[DSIM:ERROR]:%s received data size mismatch received : %d, requested : %d\n",
				__func__, rxsize, count);
			goto clear_rx_fifo;
		}

		for (i = 0; i < rxsize>>2; i++) {
			temp = readl(reg_base + S5P_DSIM_RXFIFO);
			printk(KERN_INFO "pkt : %08x\n", temp);
			for (j = 0; j < 4; j++) {
				buf[(i*4)+j] = (u8)(temp>>(j*8))&0xff;
				/* printk("Value : %02x\n",(temp>>(j*8))&0xff); */
			}
		}
		if (rxsize % 4) {
			temp = readl(reg_base + S5P_DSIM_RXFIFO);
			printk(KERN_INFO "pkt-l : %08x\n", temp);
			for (j = 0; j < rxsize%4; j++) {
				buf[(i*4)+j] = (u8)(temp>>(j*8))&0xff;
				/* printk("Value : %02x\n",(temp>>(j*8))&0xff); */
			}
		}
	}

	temp = readl(reg_base + S5P_DSIM_RXFIFO);

//	if (temp != DSIM_RX_FIFO_READ_DONE) {
	if (temp != DSIM_RX_FIFO_READ_DONE && rxsize != count) {
		printk(KERN_WARNING "[DSIM:WARN]:%s Can't found RX FIFO READ DONE FLAG : %x\n", __func__, temp);
		goto clear_rx_fifo;
	}

	mutex_unlock(&dsim_rd_wr_mutex);
	return rxsize;

clear_rx_fifo:
	i = 0;
	while (1) {
		temp = readl(reg_base+S5P_DSIM_RXFIFO);
		if ((temp == DSIM_RX_FIFO_READ_DONE) || (i > DSIM_MAX_RX_FIFO))
			break;
		printk(KERN_INFO "[DSIM:INFO] : %s clear rx fifo : %08x\n", __func__, temp);
		i++;
	}
	printk(KERN_INFO "[DSIM:INFO] : %s done count : %d, temp : %08x\n", __func__, i, temp);

	mutex_unlock(&dsim_rd_wr_mutex);
	return 0;

}

static irqreturn_t s5p_dsim_isr(int irq, void *dev_id)
{
	int i;
	unsigned int intsrc = 0;
	unsigned int intmsk = 0;
	struct dsim_global *pdsim = NULL;

	pdsim = (struct dsim_global *)dev_id;
	if (!pdsim) {
		printk(KERN_ERR "%s:error:wrong parameter\n", __func__);
		return IRQ_HANDLED;
	}

	intsrc = readl(pdsim->reg_base + S5P_DSIM_INTSRC);
	intmsk = readl(pdsim->reg_base + S5P_DSIM_INTMSK);

	intmsk = ~(intmsk) & intsrc;

	for (i = 0; i < 32; i++) {
		if (intmsk & (0x01<<i)) {
			switch (i) {
			case S5P_DSIM_INT_BTA:
				/* printk("S5P_DSIM_INT_BTA\n"); */
				break;
			case S5P_DSIM_INT_RX_TIMEOUT:
				/* printk("S5P_DSIM_INT_RX_TIMEOUT\n"); */
				break;
			case S5P_DSIM_INT_BTA_TIMEOUT:
				/* printk("S5P_DSIM_INT_BTA_TIMEOUT\n"); */
				break;
			case S5P_DSIM_INT_RX_DONE:
				complete_all(&dsim_rd_comp);
				/* printk("S5P_DSIM_INT_RX_DONE\n"); */
				break;
			case S5P_DSIM_INT_RX_TE:
				/* printk("S5P_DSIM_INT_RX_TE\n"); */
				break;
			case S5P_DSIM_INT_RX_ACK:
				/* printk("S5P_DSIM_INT_RX_ACK\n"); */
				break;
			case S5P_DSIM_INT_RX_ECC_ERR:
				/* printk("S5P_DSIM_INT_RX_ECC_ERR\n"); */
				break;
			case S5P_DSIM_IMT_RX_CRC_ERR:
				/* printk("S5P_DSIM_IMT_RX_CRC_ERR\n"); */
				break;
			case S5P_DSIM_INT_SFR_FIFO_EMPTY:
				/* printk("S5P_DSIM_INT_SFR_FIFO_EMPTY\n"); */
				complete_all(&dsim_wr_comp);
				break;
			case S5P_DSIM_INT_MSK_FRAME_DONE:
				/* printk("S5P_DSIM_INT_MSK_FRAME_DONE\n"); */
				if (s3cfb_vsync_status_check() && dsim.mipi_ddi_pd->resume_complete == 1) {
					if (completion_done(&dsim_wr_comp) && completion_done(&dsim_rd_comp)) {
						s5p_dsim_toggle_hs_clock(dsim.reg_base);
						if (!dsim_toggle_per_frame_count) {
							s5p_dsim_frame_done_interrupt_enable(0);
							if (likely(dsim.dsim_info->hs_toggle - 1))
								schedule_delayed_work(&dsim_work, dsim.dsim_info->hs_toggle);
						}
						if (dsim_toggle_per_frame_count)
							dsim_toggle_per_frame_count--;
					}
				}
				break;
			}
		}
	}
	/* clear irq */
	writel(intsrc, pdsim->reg_base + S5P_DSIM_INTSRC);
	return IRQ_HANDLED;
}

static void s5p_dsim_init_header_fifo(void)
{
	unsigned int cnt;

	for (cnt = 0; cnt < DSIM_HEADER_FIFO_SZ; cnt++)
		dsim.header_fifo_index[cnt] = -1;
	return;
}

unsigned char s5p_dsim_pll_on(unsigned int dsim_base, unsigned char enable)
{
	if (enable) {
		int sw_timeout = 1000;
		s5p_dsim_clear_interrupt(dsim_base, DSIM_PLL_STABLE);
		s5p_dsim_enable_pll(dsim_base, 1);
		while (1) {
			sw_timeout--;
			if (s5p_dsim_is_pll_stable(dsim_base))
				return DSIM_TRUE;
			if (sw_timeout == 0)
				return DSIM_FALSE;
		}
	} else
		s5p_dsim_enable_pll(dsim_base, 0);

	return DSIM_TRUE;
}

static unsigned long s5p_dsim_change_pll(unsigned int dsim_base, unsigned char pre_divider,
	unsigned short main_divider, unsigned char scaler)
{
	unsigned long dfin_pll, dfvco, dpll_out;
	unsigned char freq_band;
	unsigned char temp0 = 0, temp1 = 0;

	dfin_pll = (MIPI_FIN / pre_divider);

	if (dfin_pll < 6 * 1000 * 1000 || dfin_pll > 12 * 1000 * 1000) {
		dev_warn(dsim.dev, "warning!!\n");
		dev_warn(dsim.dev, "fin_pll range is 6MHz ~ 12MHz\n");
		dev_warn(dsim.dev, "fin_pll of mipi dphy pll is %luMHz\n", (dfin_pll / 1000000));

		s5p_dsim_enable_afc(dsim_base, 0, 0);
	} else {
		if (dfin_pll < 7 * 1000000)
			s5p_dsim_enable_afc(dsim_base, 1, 0x1);
		else if (dfin_pll < 8 * 1000000)
			s5p_dsim_enable_afc(dsim_base, 1, 0x0);
		else if (dfin_pll < 9 * 1000000)
			s5p_dsim_enable_afc(dsim_base, 1, 0x3);
		else if (dfin_pll < 10 * 1000000)
			s5p_dsim_enable_afc(dsim_base, 1, 0x2);
		else if (dfin_pll < 11 * 1000000)
			s5p_dsim_enable_afc(dsim_base, 1, 0x5);
		else
			s5p_dsim_enable_afc(dsim_base, 1, 0x4);
	}

	dfvco = dfin_pll * main_divider;
	dev_dbg(dsim.dev, "dfvco = %lu, dfin_pll = %lu, main_divider = %d\n",
		dfvco, dfin_pll, main_divider);
	if (dfvco < 500000000 || dfvco > 1000000000) {
		dev_warn(dsim.dev, "Caution!!\n");
		dev_warn(dsim.dev, "fvco range is 500MHz ~ 1000MHz\n");
		dev_warn(dsim.dev, "fvco of mipi dphy pll is %luMHz\n", (dfvco / 1000000));
	}

	dpll_out = dfvco / (1 << scaler);
	dev_dbg(dsim.dev, "dpll_out = %lu, dfvco = %lu, scaler = %d\n",
		dpll_out, dfvco, scaler);
	if (dpll_out < 100 * 1000000)
		freq_band = 0x0;
	else if (dpll_out < 120 * 1000000)
		freq_band = 0x1;
	else if (dpll_out < 170 * 1000000)
		freq_band = 0x2;
	else if (dpll_out < 220 * 1000000)
		freq_band = 0x3;
	else if (dpll_out < 270 * 1000000)
		freq_band = 0x4;
	else if (dpll_out < 320 * 1000000)
		freq_band = 0x5;
	else if (dpll_out < 390 * 1000000)
		freq_band = 0x6;
	else if (dpll_out < 450 * 1000000)
		freq_band = 0x7;
	else if (dpll_out < 510 * 1000000)
		freq_band = 0x8;
	else if (dpll_out < 560 * 1000000)
		freq_band = 0x9;
	else if (dpll_out < 640 * 1000000)
		freq_band = 0xa;
	else if (dpll_out < 690 * 1000000)
		freq_band = 0xb;
	else if (dpll_out < 770 * 1000000)
		freq_band = 0xc;
	else if (dpll_out < 870 * 1000000)
		freq_band = 0xd;
	else if (dpll_out < 950 * 1000000)
		freq_band = 0xe;
	else
		freq_band = 0xf;

	dev_dbg(dsim.dev, "freq_band = %d\n", freq_band);

	s5p_dsim_pll_freq(dsim_base, pre_divider, main_divider, scaler);

	s5p_dsim_hs_zero_ctrl(dsim_base, temp0);
	s5p_dsim_prep_ctrl(dsim_base, temp1);

	/* Freq Band */
	s5p_dsim_pll_freq_band(dsim_base, freq_band);

	/* Stable time */
	s5p_dsim_pll_stable_time(dsim_base, dsim.dsim_info->pll_stable_time);

	/* Enable PLL */
	dev_dbg(dsim.dev, "FOUT of mipi dphy pll is %luMHz\n", (dpll_out / 1000000));

	return dpll_out;
}

static void s5p_dsim_set_clock(unsigned int dsim_base,
	unsigned char byte_clk_sel, unsigned char enable)
{
	unsigned int esc_div;
	unsigned long esc_clk_error_rate;

	if (enable) {
		dsim.e_clk_src = byte_clk_sel;

		/* Escape mode clock and byte clock source */
		s5p_dsim_set_byte_clock_src(dsim_base, byte_clk_sel);

		/* DPHY, DSIM Link : D-PHY clock out */
		if (byte_clk_sel == DSIM_PLL_OUT_DIV8) {
			dsim.hs_clk = s5p_dsim_change_pll(dsim_base, dsim.dsim_info->p,
				dsim.dsim_info->m, dsim.dsim_info->s);
			dsim.byte_clk = dsim.hs_clk / 8;
			s5p_dsim_enable_pll_bypass(dsim_base, 0);
			s5p_dsim_pll_on(dsim_base, 1);
			usleep_range(1000, 1000);
		/* DPHY : D-PHY clock out, DSIM link : external clock out */
		} else if (byte_clk_sel == DSIM_EXT_CLK_DIV8)
			dev_warn(dsim.dev, "this project is not supported external clock source for MIPI DSIM\n");
		else if (byte_clk_sel == DSIM_EXT_CLK_BYPASS)
			dev_warn(dsim.dev, "this project is not supported external clock source for MIPI DSIM\n");

		/* escape clock divider */
		esc_div = dsim.byte_clk / (dsim.dsim_info->esc_clk);
		dev_dbg(dsim.dev, "esc_div = %d, byte_clk = %lu, esc_clk = %lu\n",
			esc_div, dsim.byte_clk, dsim.dsim_info->esc_clk);
		if ((dsim.byte_clk / esc_div) >= 20000000 ||
			(dsim.byte_clk / esc_div) > dsim.dsim_info->esc_clk)
			esc_div += 1;

		dsim.escape_clk = dsim.byte_clk / esc_div;
		dev_dbg(dsim.dev, "escape_clk = %lu, byte_clk = %lu, esc_div = %d\n",
			dsim.escape_clk, dsim.byte_clk, esc_div);

		/*
		 * enable escclk on lane
		 */
		s5p_dsim_enable_byte_clock(dsim_base, DSIM_TRUE);

		/* enable byte clk and escape clock */
		s5p_dsim_set_esc_clk_prs(dsim_base, 1, esc_div);
		/* escape clock on lane */
		s5p_dsim_enable_esc_clk_on_lane(dsim_base, (DSIM_LANE_CLOCK | dsim.data_lane), 1);

		dev_dbg(dsim.dev, "byte clock is %luMHz\n", (dsim.byte_clk / 1000000));
		dev_dbg(dsim.dev, "escape clock that user's need is %lu\n", (dsim.dsim_info->esc_clk / 1000000));
		dev_dbg(dsim.dev, "escape clock divider is %x\n", esc_div);
		dev_dbg(dsim.dev, "escape clock is %luMHz\n", ((dsim.byte_clk / esc_div) / 1000000));

		if ((dsim.byte_clk / esc_div) > dsim.escape_clk) {
			esc_clk_error_rate = dsim.escape_clk / (dsim.byte_clk / esc_div);
			dev_warn(dsim.dev, "error rate is %lu over.\n", (esc_clk_error_rate / 100));
		} else if ((dsim.byte_clk / esc_div) < (dsim.escape_clk)) {
			esc_clk_error_rate = (dsim.byte_clk / esc_div) / dsim.escape_clk;
			dev_warn(dsim.dev, "error rate is %lu under.\n", (esc_clk_error_rate / 100));
		}
	} else {
		s5p_dsim_enable_esc_clk_on_lane(dsim_base, (DSIM_LANE_CLOCK | dsim.data_lane), 0);
		s5p_dsim_set_esc_clk_prs(dsim_base, 0, 0);

		s5p_dsim_enable_byte_clock(dsim_base, DSIM_FALSE);

		if (byte_clk_sel == DSIM_PLL_OUT_DIV8)
			s5p_dsim_pll_on(dsim_base, 0);
	}
}

static int s5p_dsim_late_resume_init_dsim(unsigned int dsim_base)
{
	if (dsim.pd->init_d_phy)
		dsim.pd->init_d_phy(dsim.reg_base);

	dsim.state = DSIM_STATE_RESET;

	switch (dsim.dsim_info->e_no_data_lane) {
	case DSIM_DATA_LANE_1:
		dsim.data_lane = DSIM_LANE_DATA0;
		break;
	case DSIM_DATA_LANE_2:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case DSIM_DATA_LANE_3:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case DSIM_DATA_LANE_4:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim.dev, "data lane is invalid.\n");
		return -1;
	};

	s5p_dsim_init_header_fifo();
	s5p_dsim_sw_reset(dsim_base);
	s5p_dsim_dp_dn_swap(dsim_base, dsim.dsim_info->e_lane_swap);

	/* enable only frame done interrupt */
	/* s5p_dsim_clear_interrupt(dsim_base, AllDsimIntr); */
	/* s5p_dsim_set_interrupt_mask(dsim.reg_base, AllDsimIntr, 1); */

	return 0;
}

#if 0
static int s5p_dsim_init_dsim(unsigned int dsim_base)
{
	if (dsim.pd->init_d_phy)
		dsim.pd->init_d_phy(dsim.reg_base);

	dsim.state = DSIM_STATE_RESET;

	switch (dsim.dsim_info->e_no_data_lane) {
	case DSIM_DATA_LANE_1:
		dsim.data_lane = DSIM_LANE_DATA0;
		break;
	case DSIM_DATA_LANE_2:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case DSIM_DATA_LANE_3:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case DSIM_DATA_LANE_4:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim.dev, "data lane is invalid.\n");
		return -1;
	};

	s5p_dsim_init_header_fifo();
	s5p_dsim_dp_dn_swap(dsim_base, dsim.dsim_info->e_lane_swap);

	/* enable only frame done interrupt */
	/* s5p_dsim_clear_interrupt(dsim_base, AllDsimIntr); */
	/* s5p_dsim_set_interrupt_mask(dsim.reg_base, AllDsimIntr, 1); */

	return 0;
}
#endif

static void s5p_dsim_set_display_mode(unsigned int dsim_base,
	struct dsim_lcd_config *main_lcd, struct dsim_lcd_config *sub_lcd)
{
	struct s3cfb_lcd *main_lcd_panel_info = NULL, *sub_lcd_panel_info = NULL;
	struct s3cfb_lcd_timing *main_timing = NULL;

	if (main_lcd != NULL) {
		if (main_lcd->lcd_panel_info != NULL) {
			main_lcd_panel_info =
				(struct s3cfb_lcd *) main_lcd->lcd_panel_info;

			s5p_dsim_set_main_disp_resol(dsim_base,
				main_lcd_panel_info->height,
				main_lcd_panel_info->width);
		} else
			dev_warn(dsim.dev, "lcd panel info of main lcd is NULL.\n");
	} else {
		dev_err(dsim.dev, "main lcd is NULL.\n");
		return;
	}

	/* in case of VIDEO MODE (RGB INTERFACE) */
	if (dsim.dsim_lcd_info->e_interface == (u32) DSIM_VIDEO) {

		main_timing = &main_lcd_panel_info->timing;
		if (main_timing == NULL) {
			dev_err(dsim.dev, "main_timing is NULL.\n");
			return;
		}

		s5p_dsim_set_main_disp_vporch(dsim_base,
				main_timing->cmd_allow_len,
				main_timing->stable_vfp, (u16) main_timing->v_bp);
		s5p_dsim_set_main_disp_hporch(dsim_base,
				main_timing->h_fp, (u16) main_timing->h_bp);
		s5p_dsim_set_main_disp_sync_area(dsim_base,
				main_timing->v_sw, (u16) main_timing->h_sw);

	/* in case of COMMAND MODE (CPU or I80 INTERFACE) */
	} else {
		if (sub_lcd != NULL) {
			if (sub_lcd->lcd_panel_info != NULL) {
				sub_lcd_panel_info =
					(struct s3cfb_lcd *)
						sub_lcd->lcd_panel_info;

				s5p_dsim_set_sub_disp_resol(dsim_base,
					sub_lcd_panel_info->height,
					sub_lcd_panel_info->width);
			} else
				dev_warn(dsim.dev, "lcd panel info of sub lcd is NULL.\n");
		}
	}

	s5p_dsim_display_config(dsim_base, dsim.dsim_lcd_info, NULL);
}

static int s5p_dsim_init_link(unsigned int dsim_base)
{
	unsigned int time_out = 100;

	switch (dsim.state) {
	case DSIM_STATE_RESET:
	case DSIM_STATE_INIT:
		printk(KERN_INFO "%s\n", __func__);
		s5p_dsim_init_fifo_pointer(dsim_base, 0x0);
		usleep_range(10000, 10000);
		s5p_dsim_init_fifo_pointer(dsim_base, 0x1f);

		/* dsi configuration */
		s5p_dsim_init_config(dsim_base, dsim.dsim_lcd_info, NULL, dsim.dsim_info);
		s5p_dsim_enable_lane(dsim_base, DSIM_LANE_CLOCK, 1);
		s5p_dsim_enable_lane(dsim_base, dsim.data_lane, 1);

		/* set clock configuration */
		s5p_dsim_set_clock(dsim_base, dsim.dsim_info->e_byte_clk, 1);
		usleep_range(5000, 5000);
		/* check clock and data lane state is stop state */
		while (!(s5p_dsim_is_lane_state(dsim_base, DSIM_LANE_CLOCK) == DSIM_LANE_STATE_STOP) &&
			!(s5p_dsim_is_lane_state(dsim_base, dsim.data_lane) == DSIM_LANE_STATE_STOP)) {
			time_out--;
			if (time_out == 0) {
				dev_info(dsim.dev, "DSI Master state is not stop state!!!\n");
				dev_info(dsim.dev, "Please check initialization process\n");

				return DSIM_FALSE;
			}
		}

		if (time_out != 0) {
			/* dev_info(dsim.dev, "initialization of DSI Master is successful\n"); */
			/* dev_info(dsim.dev, "DSI Master state is stop state\n"); */
		}

		dsim.state = DSIM_STATE_STOP;

		/* BTA sequence counters */
		s5p_dsim_set_stop_state_counter(dsim_base, dsim.dsim_info->stop_holding_cnt);
		s5p_dsim_set_bta_timeout(dsim_base, dsim.dsim_info->bta_timeout);
		s5p_dsim_set_lpdr_timeout(dsim_base, dsim.dsim_info->rx_timeout);

		/* default LPDT by both cpu and lcd controller */
		s5p_dsim_set_data_mode(dsim_base, DSIM_TRANSFER_BOTH,
				DSIM_STATE_STOP);

		return DSIM_TRUE;
	default:
		dev_info(dsim.dev, "DSI Master is already init.\n");

		return DSIM_FALSE;
	}
}

unsigned char s5p_dsim_set_hs_enable(unsigned int dsim_base)
{
	u8 ret = DSIM_FALSE;

	if (dsim.state == DSIM_STATE_STOP) {
		if (dsim.e_clk_src != DSIM_EXT_CLK_BYPASS) {
			dsim.state = DSIM_STATE_HSCLKEN;
			s5p_dsim_set_data_mode(dsim_base, DSIM_TRANSFER_BOTH,
				DSIM_STATE_HSCLKEN);
			s5p_dsim_enable_hs_clock(dsim_base, 1);

			ret = DSIM_TRUE;
		} else
			dev_warn(dsim.dev, "clock source is external bypass.\n");
	} else
		dev_warn(dsim.dev, "DSIM is not stop state.\n");

	return ret;
}

#if 0
static unsigned char s5p_dsim_set_stopstate(unsigned int dsim_base)
{
	u8 ret =  DSIM_FALSE;

	if (dsim.state == DSIM_STATE_HSCLKEN) {
		if (dsim.e_clk_src != DSIM_EXT_CLK_BYPASS) {
			dsim.state = DSIM_STATE_STOP;
			s5p_dsim_enable_hs_clock(dsim_base, 0);
			ret = DSIM_TRUE;
		} else
			dev_warn(dsim.dev, "clock source is external bypass.\n");
	} else if (dsim.state == DSIM_STATE_ULPS) {
		/* will be update for exiting ulps */
		ret = DSIM_TRUE;
	} else if (dsim.state == DSIM_STATE_STOP) {
		dev_warn(dsim.dev, "DSIM is already stop state.\n");
		ret = DSIM_TRUE;
	} else
		dev_warn(dsim.dev, "DSIM is not stop state.\n");

	return ret;
}
#endif

static unsigned char s5p_dsim_set_data_transfer_mode(unsigned int dsim_base,
	unsigned char data_path, unsigned char hs_enable)
{
	u8 ret = DSIM_FALSE;

	if (hs_enable) {
		if (dsim.state == DSIM_STATE_HSCLKEN) {
			s5p_dsim_set_data_mode(dsim_base, data_path, DSIM_STATE_HSCLKEN);
			ret = DSIM_TRUE;
		} else {
			dev_err(dsim.dev, "HS Clock lane is not enabled.\n");
			ret = DSIM_FALSE;
		}
	} else {
		if (dsim.state == DSIM_STATE_INIT || dsim.state == DSIM_STATE_ULPS) {
			dev_err(dsim.dev, "DSI Master is not STOP or HSDT state.\n");
			ret = DSIM_FALSE;
		} else {
			s5p_dsim_set_data_mode(dsim_base, data_path, DSIM_STATE_STOP);
			ret = DSIM_TRUE;
		}
	}

	return ret;
}

int s5p_dsim_register_lcd_driver(struct mipi_lcd_driver *lcd_drv)
{
	struct mipi_lcd_info	*lcd_info = NULL;

	lcd_info = kmalloc(sizeof(struct mipi_lcd_info), GFP_KERNEL);
	if (lcd_info == NULL)
		return -ENOMEM;

	lcd_info->mipi_drv = kmalloc(sizeof(struct mipi_lcd_driver), GFP_KERNEL);
	if (lcd_info->mipi_drv == NULL) {
		kfree(lcd_info);
		return -ENOMEM;
	}

	memcpy(lcd_info->mipi_drv, lcd_drv, sizeof(struct mipi_lcd_driver));

	mutex_lock(&mipi_lock);
	list_add_tail(&lcd_info->list, &lcd_info_list);
	mutex_unlock(&mipi_lock);

	dev_dbg(dsim.dev, "registered lcd panel driver(%s) to mipi-dsi driver.\n",
		lcd_drv->name);

	return 0;
}

struct mipi_lcd_driver *scan_mipi_driver(const char *name)
{
	struct mipi_lcd_info *lcd_info;
	struct mipi_lcd_driver *mipi_drv = NULL;

	mutex_lock(&mipi_lock);

	dev_dbg(dsim.dev, "find lcd panel driver(%s).\n",
		name);

	list_for_each_entry(lcd_info, &lcd_info_list, list) {
		mipi_drv = lcd_info->mipi_drv;

		if ((strcmp(mipi_drv->name, name)) == 0) {
			mutex_unlock(&mipi_lock);
			dev_dbg(dsim.dev, "found!!!(%s).\n", mipi_drv->name);
			return mipi_drv;
		}
	}

	dev_warn(dsim.dev, "failed to find lcd panel driver(%s).\n",
		name);

	mutex_unlock(&mipi_lock);

	return NULL;
}

void s5p_dsim_interrupt_mask_set(void)
{
	u32 int_stat;

	int_stat = readl(dsim.reg_base + S5P_DSIM_INTMSK);

	int_stat &= ~((0x01<<S5P_DSIM_INT_BTA) | (0x01<<S5P_DSIM_INT_RX_TIMEOUT) |
		(0x01<<S5P_DSIM_INT_BTA_TIMEOUT) | (0x01 << S5P_DSIM_INT_RX_DONE) |
		(0x01<<S5P_DSIM_INT_RX_TE) | (0x01<<S5P_DSIM_INT_RX_ACK) |
		(0x01<<S5P_DSIM_INT_RX_ECC_ERR) | (0x01<<S5P_DSIM_IMT_RX_CRC_ERR) |
		(0x01<<S5P_DSIM_INT_SFR_FIFO_EMPTY));

	writel(int_stat, dsim.reg_base + S5P_DSIM_INTMSK);
}

int s5p_dsim_fifo_clear(void)
{
	int dsim_count = 0, ret;

	writel(SwRstRelease, dsim.reg_base + S5P_DSIM_INTSRC);

	writel(DSIM_FUNCRST, dsim.reg_base + S5P_DSIM_SWRST);

	do {
		if (++dsim_count > 90000) {
			printk(KERN_ERR "dsim fifo clear fail re_try dsim resume\n");
			ret = 0;
			break;
		}

		if (readl(dsim.reg_base + S5P_DSIM_INTSRC) & SwRstRelease) {
			s5p_dsim_interrupt_mask_set();
			ret = 1;
			break;
		}
	} while (1);

	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void s5p_dsim_early_suspend(void)
{
	u32 int_stat = 0;
	pm_message_t state;

	printk(KERN_INFO "+%s\n", __func__);

	if (dsim.mipi_ddi_pd->resume_complete == 0)
		return;

	dsim.mipi_ddi_pd->resume_complete = 0;
	dsim.dsim_lcd_info->lcd_enabled = 0;

	/* int_stat = readl(dsim.reg_base + S5P_DSIM_INTMSK); */

	int_stat |= ((0x01<<S5P_DSIM_INT_BTA) | (0x01<<S5P_DSIM_INT_RX_TIMEOUT) |
		(0x01<<S5P_DSIM_INT_BTA_TIMEOUT) | (0x01 << S5P_DSIM_INT_RX_DONE) |
		(0x01<<S5P_DSIM_INT_RX_TE) | (0x01<<S5P_DSIM_INT_RX_ACK) |
		(0x01<<S5P_DSIM_INT_RX_ECC_ERR) | (0x01<<S5P_DSIM_IMT_RX_CRC_ERR) |
		(0x01<<S5P_DSIM_INT_SFR_FIFO_EMPTY) | (0x01 << S5P_DSIM_INT_MSK_FRAME_DONE));

	writel(int_stat, dsim.reg_base + S5P_DSIM_INTMSK);

	/* disable_irq(dsim.irq); */
	state.event = PM_EVENT_SUSPEND;

	if (dsim.mipi_drv->suspend)
		dsim.mipi_drv->suspend(dsim.dev, state);

	if (dsim.mipi_ddi_pd->lcd_power_on)
		dsim.mipi_ddi_pd->lcd_power_on(dsim.dev, 0);

	s5p_dsim_enable_hs_clock(dsim.reg_base, 0);
	s5p_dsim_set_clock(dsim.reg_base, dsim.dsim_info->e_byte_clk, 0);

#if defined(CONFIG_CPU_EXYNOS4210)
	writel(0x1, dsim.reg_base + S5P_DSIM_SWRST);
#endif

	if (dsim.pd->exit_d_phy)
		dsim.pd->exit_d_phy(dsim.reg_base);

	clk_disable(dsim.clock);

	if (dsim.pd->mipi_power)
		dsim.pd->mipi_power(0);

	printk(KERN_INFO "-%s\n", __func__);

	return;
}

void s5p_dsim_late_resume(void)
{
	printk(KERN_INFO "+%s\n", __func__);

	/* MIPI SIGNAL ON */
	if (dsim.pd->mipi_power)
		dsim.pd->mipi_power(1);

	clk_enable(dsim.clock);
	usleep_range(10000, 10000);

	if (dsim.mipi_ddi_pd->lcd_power_on)
		dsim.mipi_ddi_pd->lcd_power_on(dsim.dev, 1);
	usleep_range(25000, 25000);

	if (dsim.mipi_ddi_pd->lcd_reset)
		dsim.mipi_ddi_pd->lcd_reset();
	usleep_range(5000, 5000);

	s5p_dsim_late_resume_init_dsim(dsim.reg_base);
	s5p_dsim_init_link(dsim.reg_base);
	usleep_range(10000, 10000);
	s5p_dsim_set_hs_enable(dsim.reg_base);
	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYCPU, 1);
	s5p_dsim_set_display_mode(dsim.reg_base, dsim.dsim_lcd_info, NULL);
	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYLCDC, 1);
	/* s5p_dsim_set_interrupt_mask(dsim.reg_base, AllDsimIntr, 0); */

	dsim.mipi_ddi_pd->resume_complete = 1;

	printk(KERN_INFO "-%s\n", __func__);

	return;
}

void s5p_dsim_init(void)
{
#if 0
    s5p_dsim_late_resume();
    s5p_dsim_early_suspend();
    s5p_dsim_late_resume();
#endif

#if 1
	clk_enable(dsim.clock);
	usleep_range(10000, 10000);

	if (dsim.mipi_ddi_pd->lcd_reset)
		dsim.mipi_ddi_pd->lcd_reset();
	usleep_range(5000, 5000);

	if (dsim.pd->init_d_phy)
		dsim.pd->init_d_phy(dsim.reg_base);

	dsim.state = DSIM_STATE_RESET;

	s5p_dsim_init_config(dsim.reg_base, dsim.dsim_lcd_info, NULL, dsim.dsim_info);

	/* set clock configuration */
	s5p_dsim_set_clock(dsim.reg_base, dsim.dsim_info->e_byte_clk, 0);
	usleep_range(5000, 5000);

	switch (dsim.dsim_info->e_no_data_lane) {
	case DSIM_DATA_LANE_1:
		dsim.data_lane = DSIM_LANE_DATA0;
		break;
	case DSIM_DATA_LANE_2:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case DSIM_DATA_LANE_3:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case DSIM_DATA_LANE_4:
		dsim.data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim.dev, "data lane is invalid.\n");
		return ;
	};

	s5p_dsim_init_header_fifo();
	s5p_dsim_sw_reset(dsim.reg_base);
	s5p_dsim_dp_dn_swap(dsim.reg_base, dsim.dsim_info->e_lane_swap);

	s5p_dsim_init_link(dsim.reg_base);
	usleep_range(10000, 10000);

	s5p_dsim_set_hs_enable(dsim.reg_base);
	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYCPU, 1);
	s5p_dsim_set_display_mode(dsim.reg_base, dsim.dsim_lcd_info, NULL);
	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYLCDC, 1);
	/* s5p_dsim_set_interrupt_mask(dsim.reg_base, AllDsimIntr, 0); */

	dsim.mipi_ddi_pd->resume_complete = 1;

#endif
    mdelay(10);
	writel(0x81003fff, dsim.reg_base + S5P_DSIM_INTMSK);
}

EXPORT_SYMBOL(s5p_dsim_init);

#else
#ifdef CONFIG_PM
int s5p_dsim_suspend(struct platform_device *pdev, pm_message_t state)
{
	printk(KERN_INFO "%s\n", __func__);

	dsim.mipi_ddi_pd->resume_complete = 0;

	if (dsim.mipi_drv->suspend)
		dsim.mipi_drv->suspend(&pdev->dev, state);
	else
		dev_warn(&pdev->dev, "suspend func is null.\n");

	clk_disable(dsim.clock);

	if (dsim.pd->mipi_power)
		dsim.pd->mipi_power(0);
	else
		dev_warn(&pdev->dev, "mipi_power func is null.\n");

	return 0;
}

int s5p_dsim_resume(struct platform_device *pdev)
{
	u32 int_stat;

	printk(KERN_INFO "%s\n", __func__);

	if (dsim.pd->mipi_power)
		dsim.pd->mipi_power(1);
	else
		dev_warn(&pdev->dev, "mipi_power func is null.\n");

	usleep_range(10000, 10000);

	clk_enable(dsim.clock);

	if (dsim.mipi_drv->resume)
		dsim.mipi_drv->resume(&pdev->dev);
	else
		dev_warn(&pdev->dev, "resume func is null.\n");

	s5p_dsim_init_dsim(dsim.reg_base);
	s5p_dsim_init_link(dsim.reg_base);

	s5p_dsim_set_hs_enable(dsim.reg_base);
	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYCPU, 1);

	msleep(120);

	/* initialize lcd panel */
	if (dsim.mipi_drv->init)
		dsim.mipi_drv->init(&pdev->dev);
	else
		dev_warn(&pdev->dev, "init func is null.\n");

	s5p_dsim_set_display_mode(dsim.reg_base, dsim.dsim_lcd_info, NULL);

	s5p_dsim_set_data_transfer_mode(dsim.reg_base, DSIM_TRANSFER_BYLCDC, 1);

	int_stat = readl(dsim.reg_base + S5P_DSIM_INTMSK);

	int_stat &= ~((0x01<<S5P_DSIM_INT_BTA) | (0x01<<S5P_DSIM_INT_RX_TIMEOUT) |
		(0x01<<S5P_DSIM_INT_BTA_TIMEOUT) | (0x01 << S5P_DSIM_INT_RX_DONE) |
		(0x01<<S5P_DSIM_INT_RX_TE) | (0x01<<S5P_DSIM_INT_RX_ACK) |
		(0x01<<S5P_DSIM_INT_RX_ECC_ERR) | (0x01<<S5P_DSIM_IMT_RX_CRC_ERR) |
		(0x01<<S5P_DSIM_INT_SFR_FIFO_EMPTY));

	writel(int_stat, dsim.reg_base + S5P_DSIM_INTMSK);

	dsim.mipi_ddi_pd->resume_complete = 1;

	return 0;
}
#else
#define s5p_dsim_suspend NULL
#define s5p_dsim_resume NULL
#endif
#endif
u32 read_dsim_register(u32 num)
{
	return readl(dsim.reg_base + (num*4));
}

static ssize_t hs_toggle_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[3];

	sprintf(temp, "%d\n", jiffies_to_msecs(dsim.dsim_info->hs_toggle));
	strcpy(buf, temp);

	return strlen(buf);
}

static int hs_toggle_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int rc;

	rc = strict_strtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		dev_info(dev, "%s - %d, %d\n", __func__, jiffies_to_msecs(dsim.dsim_info->hs_toggle), value);

		if (value == 1) {
			dsim.dsim_info->hs_toggle = msecs_to_jiffies(3000);
			s5p_dsim_frame_done_interrupt_enable(1);
			schedule_delayed_work(&check_hs_toggle_work,\
				msecs_to_jiffies(120000));
		} else {
			dsim.dsim_info->hs_toggle = 0;
			s5p_dsim_frame_done_interrupt_enable(0);
			cancel_delayed_work(&check_hs_toggle_work);
		}
	}
	return size;
}

static DEVICE_ATTR(hs_toggle, 0644, hs_toggle_show, hs_toggle_store);

static ssize_t dsim_dump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int reg_val, i;
	char temp[50];

	for (i = 0; i < 25; i++) {
		reg_val = readl(dsim.reg_base + i*4);
		sprintf(temp, "[DSIM]0x11c8_00%02X = 0x%08X\n", (i*4), reg_val);
		strcat(buf, temp);
	}

	return strlen(buf);
}
static DEVICE_ATTR(dsim_dump, 0444, dsim_dump_show, NULL);

static int s5p_dsim_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = -1;
	u32 int_stat;
	INIT_DELAYED_WORK(&dsim_work, dsim_work_q_handler);
	INIT_DELAYED_WORK(&check_hs_toggle_work, \
		dsim_check_hs_toggle_work_q_handler);

	dsim.pd = to_dsim_plat(&pdev->dev);
	if (!dsim.pd) {
		dev_err(&pdev->dev, "platform data is NULL\n");
		return -EINVAL;
	}

	dsim.dev = &pdev->dev;

	/* set dsim config data, dsim lcd config data and lcd panel data. */
	dsim.dsim_info = dsim.pd->dsim_info;
	dsim.dsim_lcd_info = dsim.pd->dsim_lcd_info;
	dsim.lcd_panel_info = (struct s3cfb_lcd *) dsim.dsim_lcd_info->lcd_panel_info;
	dsim.mipi_ddi_pd = (struct mipi_ddi_platform_data *) dsim.dsim_lcd_info->mipi_ddi_pd;
	dsim.mipi_ddi_pd->te_irq = dsim.pd->te_irq;

	if (dsim.pd->mipi_power)
		dsim.pd->mipi_power(1);

	strcpy(dsim.pd->lcd_panel_name, dsim.lcd_panel_info->name);

	/* clock */
	dsim.clock = clk_get(&pdev->dev, dsim.pd->clk_name);
	if (IS_ERR(dsim.clock)) {
		dev_err(&pdev->dev, "failed to get dsim clock source\n");
		return -EINVAL;
	}

	clk_enable(dsim.clock);

	/* io memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get io memory region\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	/* request mem region */
	res = request_mem_region(res->start,
				 res->end - res->start + 1, pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request io memory region\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	/* ioremap for register block */
	dsim.reg_base = (unsigned int) ioremap(res->start,
			res->end - res->start + 1);
	if (!dsim.reg_base) {
		dev_err(&pdev->dev, "failed to remap io region\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to request dsim irq resource\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}
	/* dsim.irq = res->start; */

	/* clear interrupt */
	/* int_stat = readl(dsim.reg_base + S5P_DSIM_INTSRC); */
	int_stat = 0xffffffff;
	writel(int_stat, dsim.reg_base + S5P_DSIM_INTSRC);

	/* enable interrupts */
	int_stat = readl(dsim.reg_base + S5P_DSIM_INTMSK);

	int_stat &= ~((0x01<<S5P_DSIM_INT_BTA) | (0x01<<S5P_DSIM_INT_RX_TIMEOUT) |
		(0x01<<S5P_DSIM_INT_BTA_TIMEOUT) | (0x01 << S5P_DSIM_INT_RX_DONE) |
		(0x01<<S5P_DSIM_INT_RX_TE) | (0x01<<S5P_DSIM_INT_RX_ACK) |
		(0x01<<S5P_DSIM_INT_RX_ECC_ERR) | (0x01<<S5P_DSIM_IMT_RX_CRC_ERR) |
		(0x01<<S5P_DSIM_INT_SFR_FIFO_EMPTY));

	writel(int_stat, dsim.reg_base + S5P_DSIM_INTMSK);

	init_completion(&dsim_rd_comp);
	init_completion(&dsim_wr_comp);
	mutex_init(&dsim_rd_wr_mutex);

	dsim.mipi_ddi_pd->resume_complete = 1;
	dsim.dsim_lcd_info->lcd_enabled = 1;

	ret = request_irq(res->start, (void *)s5p_dsim_isr, IRQF_DISABLED, pdev->name, &dsim);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to request dsim irq\n");
		ret = -EINVAL;
		goto err_clk_disable;
	}

	/* find lcd panel driver registered to mipi-dsi driver. */
	dsim.mipi_drv = scan_mipi_driver(dsim.pd->lcd_panel_name);
	if (dsim.mipi_drv == NULL) {
		dev_err(&pdev->dev, "mipi_drv is NULL.\n");
		ret = -EINVAL;
		goto mipi_drv_err;
	}

	/* set lcd panel driver link */
	ret = dsim.mipi_drv->set_link((void *) dsim.mipi_ddi_pd, dsim.reg_base,
		s5p_dsim_wr_data, s5p_dsim_rd_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "[DSIM : ERROR] faild set_link()\n");
		ret = -EINVAL;
		goto mipi_drv_err;
	}

	ret = dsim.mipi_drv->probe(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "[DSIM : ERROR] faild probe()\n");
		ret = -EINVAL;
		goto mipi_drv_err;
	}

	dsim.state = DSIM_STATE_HSCLKEN;

	ret = device_create_file(&(pdev->dev), &dev_attr_dsim_dump);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&(pdev->dev), &dev_attr_hs_toggle);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to add sysfs entries, %d\n", __LINE__);

	if (dsim.dsim_info->hs_toggle)
		s5p_dsim_frame_done_interrupt_enable(1);

	dev_info(&pdev->dev, "mipi-dsi driver has been probed.\n");

#if 0
#ifdef CONFIG_HAS_WAKELOCK
#ifdef CONFIG_HAS_EARLYSUSPEND
	dsim.early_suspend.suspend = s5p_dsim_early_suspend;
	dsim.early_suspend.resume = s5p_dsim_late_resume;
	dsim.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&dsim.early_suspend);
#endif
#endif
#endif
	return 0;

mipi_drv_err:
	free_irq(res->start, &dsim);
	dsim.pd->mipi_power(0);

err_clk_disable:
	clk_disable(dsim.clock);

	return ret;
}

static int s5p_dsim_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver s5p_dsim_driver = {
	.probe = s5p_dsim_probe,
	.remove = s5p_dsim_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = s5p_dsim_suspend,
	.resume = s5p_dsim_resume,
#endif
	.driver = {
		   .name = "s5p-dsim",
		   .owner = THIS_MODULE,
	},
};

static int s5p_dsim_register(void)
{
	return platform_driver_register(&s5p_dsim_driver);
}

static void s5p_dsim_unregister(void)
{
	platform_driver_unregister(&s5p_dsim_driver);
}

module_init(s5p_dsim_register);
module_exit(s5p_dsim_unregister);

MODULE_AUTHOR("InKi Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samusung MIPI-DSIM driver");
MODULE_LICENSE("GPL");
