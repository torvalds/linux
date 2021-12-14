// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <linux/of_graph.h>
#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

static int vin_rstgen_assert_reset(struct stf_vin_dev *vin)
{
	u32 val;
	/*
	 *      Software_RESET_assert1 (0x11840004)
	 *      ------------------------------------
	 *      bit[15]         rstn_vin_src
	 *      bit[16]         rstn_ispslv_axi
	 *      bit[17]         rstn_vin_axi
	 *      bit[18]         rstn_vinnoc_axi
	 *      bit[19]         rstn_isp0_axi
	 *      bit[20]         rstn_isp0noc_axi
	 *      bit[21]         rstn_isp1_axi
	 *      bit[22]         rstn_isp1noc_axi
	 *
	 */
	u32 val_reg_reset_config = 0x7f8000;

	val = ioread32(vin->vin_top_rstgen_base + SOFTWARE_RESET_ASSERT1);
	val |= val_reg_reset_config;
	iowrite32(val, vin->vin_top_rstgen_base + SOFTWARE_RESET_ASSERT1);

	val = ioread32(vin->vin_top_rstgen_base + SOFTWARE_RESET_ASSERT1);
	val &= ~(val_reg_reset_config);

	iowrite32(val, vin->vin_top_rstgen_base + SOFTWARE_RESET_ASSERT1);

	return 0;
}

static void vin_intr_clear(void __iomem * sysctrl_base)
{
	reg_set_bit(sysctrl_base, SYSCTRL_VIN_INTP_CTRL, BIT(0), 0x1);
	reg_set_bit(sysctrl_base, SYSCTRL_VIN_INTP_CTRL, BIT(0), 0x0);
}

static irqreturn_t stf_vin_wr_irq_handler(int irq, void *priv)
{
#if 0

	static struct vin_params params;
	static struct vin_params vparams;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	vin_dev->hw_ops->isr_buffer_done(&vin_dev->line[VIN_LINE_WR], &params);

	vin_intr_clear(vin->sysctrl_base);
#endif
	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_irq_handler(int irq, void *priv)
{
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status, value;
	int isp_id = irq == vin->isp0_irq ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	// if (int_status & BIT(24))
	vin_dev->hw_ops->isr_buffer_done(
		&vin_dev->line[VIN_LINE_ISP0 + isp_id], &params);

	value = reg_read(ispbase, ISP_REG_CIS_MODULE_CFG);
	if ((value & BIT(19)) && (int_status & BIT(25)))
		vin_dev->hw_ops->isr_buffer_done(
			&vin_dev->line[VIN_LINE_ISP0_RAW + isp_id], &params);

	/* clear interrupt */
	reg_write(ispbase, ISP_REG_ISP_CTRL_0, int_status);

	return IRQ_HANDLED;
}


static int stf_vin_clk_init(struct stf_vin2_dev *vin_dev)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	return 0;

}
extern  void stf_dvp_io_pad_config(struct stf_vin_dev *vin);
void stf_do_port_configure(struct stfcamss *stfcamss)
{
	struct stf_vin_dev *vin = stfcamss->vin;
	struct device *dev = stfcamss->dev;
	struct device_node *node = NULL;
	struct device_node *remote = NULL;

	for_each_endpoint_of_node(dev->of_node, node) {
		struct stfcamss_async_subdev *csd;
		struct v4l2_async_subdev *asd;

		if (!of_device_is_available(node))
			continue;
		remote = of_graph_get_remote_port_parent(node);
		if (!remote)
			st_err(ST_VIN, "Cannot get remote parent1\n");
		
		list_for_each_entry(asd, &stfcamss->notifier.asd_list, asd_list) {
			csd = container_of(asd, struct stfcamss_async_subdev, asd);
			switch(csd->port){
				case CSI2RX0_PORT_NUMBER:
				case CSI2RX1_PORT_NUMBER:
					break;
				case DVP_SENSOR_PORT_NUMBER:
					stf_dvp_io_pad_config(vin);
					break;
				case CSI2RX0_SENSOR_PORT_NUMBER:
				case CSI2RX1_SENSOR_PORT_NUMBER:
					break;
				default:
					break;
			}
		}

		of_node_put(remote);
		if (IS_ERR(asd)) {
				st_err(ST_CAMSS, "Cannot get remote parent2\n");
		}
	}
}
static int stf_vin_clk_enable(struct stf_vin2_dev *vin_dev)
{

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	//stf_do_port_configure(vin_dev->stfcamss);

	reg_set_bit(vin->clkgen_base, CLK_DOM4_APB_FUNC, CLK_MUX_SEL, 0x8);
	reg_set_bit(vin->clkgen_base, CLK_U0_VIN_PCLK, CLK_U0_VIN_PCLK_ICG, 0x1<<31);
	reg_set_bit(vin->clkgen_base, CLK_U0_VIN_SYS_CLK, CLK_MUX_SEL, 0x2);
	reg_set_bit(vin->clkgen_base, CLK_U0_ISPV2_TOP_WRAPPER_CLK_C, CLK_U0_ISPV2_CLK_ICG, 0x1<<31);
	reg_clear_rst(vin->clkgen_base, SOFTWARE_RESET_ASSERT0_ASSERT_SET, 
		SOFTWARE_RESET_ASSERT0_ASSERT_SET_STATE,
		RST_U0_ISPV2_TOP_WRAPPER_RST_P);
	reg_set_bit(vin->clkgen_base, CLK_U0_ISPV2_TOP_WRAPPER_CLK_C, CLK_U0_ISPV2_MUX_SEL, 0x0);
	reg_clear_rst(vin->clkgen_base, SOFTWARE_RESET_ASSERT0_ASSERT_SET, 
		SOFTWARE_RESET_ASSERT0_ASSERT_SET_STATE,
		RST_U0_ISPV2_TOP_WRAPPER_RST_C);
	reg_set_bit(vin->clkgen_base, CLK_DVP_INV, CLK_POLARITY, 0x0);
	reg_set_bit(vin->clkgen_base, CLK_U0_ISPV2_TOP_WRAPPER_CLK_C, CLK_U0_ISPV2_MUX_SEL, 0x1<<24);
	reg_clear_rst(vin->clkgen_base, SOFTWARE_RESET_ASSERT0_ASSERT_SET, 
		SOFTWARE_RESET_ASSERT0_ASSERT_SET_STATE,
		RSTN_U0_VIN_RST_N_PCLK 
		| RSTN_U0_VIN_RST_P_AXIRD 
		| RSTN_U0_VIN_RST_N_SYS_CLK);
	reg_set_bit(vin->clkgen_base,	CLK_U0_VIN_CLK_P_AXIWR,	CLK_U0_VIN_MUX_SEL, 0x0);
	reg_clear_rst(vin->clkgen_base, SOFTWARE_RESET_ASSERT0_ASSERT_SET, 
		SOFTWARE_RESET_ASSERT0_ASSERT_SET_STATE, RSTN_U0_VIN_RST_P_AXIWR);	 
	reg_set_bit(vin->clkgen_base,   CLK_U0_VIN_CLK_P_AXIWR, CLK_U0_VIN_MUX_SEL, 0x1<<24);

	return 0;
}


static int stf_vin_clk_disable(struct stf_vin2_dev *vin_dev)
{

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	reg_assert_rst(vin->clkgen_base,SOFTWARE_RESET_ASSERT0_ASSERT_SET,
		SOFTWARE_RESET_ASSERT0_ASSERT_SET_STATE, RSTN_U0_VIN_RST_N_PCLK 
		| RSTN_U0_VIN_RST_N_SYS_CLK
		| RSTN_U0_VIN_RST_P_AXIRD
		| RSTN_U0_VIN_RST_P_AXIWR);   
	reg_set_bit(vin->clkgen_base, CLK_U0_VIN_PCLK, CLK_U0_VIN_PCLK_ICG, 0x0);

	return 0;
}

static int stf_vin_config_set(struct stf_vin2_dev *vin_dev)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	return 0;
}

static int stf_vin_wr_stream_set(struct stf_vin2_dev *vin_dev, int on)
{

#if 0
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	print_reg(ST_VIN, vin->sysctrl_base, SYSCTRL_VIN_AXI_CTRL);
	if (on) {
		reg_set(vin->sysctrl_base,
				SYSCTRL_VIN_AXI_CTRL, BIT(1));
	} else {
		reg_clear(vin->sysctrl_base,
				SYSCTRL_VIN_AXI_CTRL, BIT(1));
	}
	print_reg(ST_VIN, vin->sysctrl_base, SYSCTRL_VIN_AXI_CTRL);
#endif

	return 0;
}

static void stf_vin_wr_irq_enable(struct stf_vin2_dev *vin_dev,
		int enable)
{

#if 0
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	unsigned int mask_value = 0, value = 0;

	if (enable) {
		// value = ~((0x1 << 4) | (0x1 << 20));
		value = ~(0x1 << 4);

		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_INTP_CTRL, BIT(4), value);
	} else {
		/* mask and clear vin interrupt */
		// mask_value = (0x1 << 4) | (0x1 << 20);
		// value = 0x1 | (0x1 << 16) | mask_value;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_INTP_CTRL, BIT(0), 0x1);
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_INTP_CTRL, BIT(0), 0x0);

		value = 0x1 << 4;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_INTP_CTRL, BIT(4), value);
	}
#endif
}

static void stf_vin_power_on(struct stf_vin2_dev *vin_dev,	int enable)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	if(enable){
		reg_write(vin->pmu_test, SW_DEST_POWER_ON, (0x1<<5));
		reg_write(vin->pmu_test, SW_ENCOURAGE, 0xff);
 		reg_write(vin->pmu_test, SW_ENCOURAGE, 0x05);
		reg_write(vin->pmu_test, SW_ENCOURAGE, 0x50);
		
		reg_set_highest_bit(vin->sys_crg, 0xCCU);
		reg_set_highest_bit(vin->sys_crg, 0xD0U);
		reg_clear_rst(vin->sys_crg, 0x2FCU,0x30CU, (0x1 << 9));
		reg_clear_rst(vin->sys_crg, 0x2FCU,0x30CU, (0x1 << 10));	
	} else {
		reg_assert_rst(vin->sys_crg, 0x2FCU ,0x30cu, BIT(9));  //u0_dom_isp_top_disable
		reg_assert_rst(vin->sys_crg, 0x2FCU ,0x30cu, BIT(10)); 
		reg_set_bit(vin->sys_crg, 0xccu, BIT(31), 0x0);
		reg_set_bit(vin->sys_crg, 0xd0u, BIT(31), 0x0);
		reg_write(vin->pmu_test, SW_DEST_POWER_ON, (0x1<<5));
		reg_write(vin->pmu_test, SW_ENCOURAGE, 0xff);
		reg_write(vin->pmu_test, SW_ENCOURAGE, 0x0a);
		reg_write(vin->pmu_test, SW_ENCOURAGE, 0xa0);
	}
}



static void stf_vin_wr_rd_set_addr(struct stf_vin2_dev *vin_dev,
		dma_addr_t wr_addr, dma_addr_t rd_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

#if 0

	/* set the start address*/
	reg_write(vin->sysctrl_base,
			SYSCTRL_VIN_WR_START_ADDR, (long)wr_addr);
	reg_write(vin->sysctrl_base,
			SYSCTRL_VIN_RD_END_ADDR, (long)rd_addr);
#endif
}

void stf_vin_wr_set_ping_addr(struct stf_vin2_dev *vin_dev,
		dma_addr_t addr)
{

#if 0

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	/* set the start address */
	reg_write(vin->sysctrl_base, SYSCTRL_VIN_WR_START_ADDR, (long)addr);
#endif
}

void stf_vin_wr_set_pong_addr(struct stf_vin2_dev *vin_dev, dma_addr_t addr)
{

#if 0

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	/* set the start address */
	reg_write(vin->sysctrl_base, SYSCTRL_VIN_RD_END_ADDR, (long)addr);
#endif
}

void stf_vin_isp_set_yuv_addr(struct stf_vin2_dev *vin_dev, int isp_id,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase =
		isp_id ? vin->isp_isp1_base : vin->isp_isp0_base;

	reg_write(ispbase, ISP_REG_Y_PLANE_START_ADDR, y_addr);
	reg_write(ispbase, ISP_REG_UV_PLANE_START_ADDR, uv_addr);
	// shadow update
	//reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, BIT(1) | BIT(0), 0x3);    //fw no configure  2021 1110
}

void stf_vin_isp_set_raw_addr(struct stf_vin2_dev *vin_dev, int isp_id,
				dma_addr_t raw_addr)
{

#if 0
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase =
		isp_id ? vin->isp_isp1_base : vin->isp_isp0_base;

	reg_write(ispbase, ISP_REG_DUMP_CFG_0, raw_addr);
	reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, 0x3FFFF, 0x3000a);
#endif
}

void dump_vin_reg(void *__iomem regbase)
{
	st_debug(ST_VIN, "DUMP VIN register:\n");
	print_reg(ST_VIN, regbase, 0x00);
	print_reg(ST_VIN, regbase, 0x04);
	print_reg(ST_VIN, regbase, 0x08);
	print_reg(ST_VIN, regbase, 0x0c);
	print_reg(ST_VIN, regbase, 0x10);
	print_reg(ST_VIN, regbase, 0x14);
	print_reg(ST_VIN, regbase, 0x18);
	print_reg(ST_VIN, regbase, 0x1c);
	print_reg(ST_VIN, regbase, 0x20);
	print_reg(ST_VIN, regbase, 0x24);
	print_reg(ST_VIN, regbase, 0x28);
	print_reg(ST_VIN, regbase, 0x2c);
	print_reg(ST_VIN, regbase, 0x30);
	print_reg(ST_VIN, regbase, 0x34);
	print_reg(ST_VIN, regbase, 0x38);
	print_reg(ST_VIN, regbase, 0x3c);
	print_reg(ST_VIN, regbase, 0x40);
	print_reg(ST_VIN, regbase, 0x44);
	print_reg(ST_VIN, regbase, 0x48);
	print_reg(ST_VIN, regbase, 0x4c);
	print_reg(ST_VIN, regbase, 0x50);
	print_reg(ST_VIN, regbase, 0x54);
	print_reg(ST_VIN, regbase, 0x58);
	print_reg(ST_VIN, regbase, 0x5c);
}

struct vin_hw_ops vin_ops = {
	
	.vin_clk_init          = stf_vin_clk_init,
	.vin_clk_enable        = stf_vin_clk_enable,
	.vin_clk_disable       = stf_vin_clk_disable,
	.vin_config_set        = stf_vin_config_set,
	.vin_wr_stream_set     = stf_vin_wr_stream_set,
	.vin_wr_irq_enable     = stf_vin_wr_irq_enable,
	.vin_power_on		   = stf_vin_power_on,
	.wr_rd_set_addr        = stf_vin_wr_rd_set_addr,
	.vin_wr_set_ping_addr  = stf_vin_wr_set_ping_addr,
	.vin_wr_set_pong_addr  = stf_vin_wr_set_pong_addr,
	.vin_isp_set_yuv_addr  = stf_vin_isp_set_yuv_addr,
	.vin_isp_set_raw_addr  = stf_vin_isp_set_raw_addr,
	.vin_wr_irq_handler    = stf_vin_wr_irq_handler,
	.vin_isp_irq_handler   = stf_vin_isp_irq_handler,
};
