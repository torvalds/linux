// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) STMicroelectronics 2025 - All Rights Reserved
 * Author: Clément Le Goffic <clement.legoffic@foss.st.com> for STMicroelectronics.
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include "../core.h"

#define DRIVER_NAME		"stm32_hdp"
#define HDP_CTRL_ENABLE		1
#define HDP_CTRL_DISABLE	0

#define HDP_CTRL		0x000
#define HDP_MUX			0x004
#define HDP_VAL			0x010
#define HDP_GPOSET		0x014
#define HDP_GPOCLR		0x018
#define HDP_GPOVAL		0x01c
#define HDP_VERR		0x3f4
#define HDP_IPIDR		0x3f8
#define HDP_SIDR		0x3fc

#define HDP_MUX_SHIFT(n)	((n) * 4)
#define HDP_MUX_MASK(n)		(GENMASK(3, 0) << HDP_MUX_SHIFT(n))
#define HDP_MUX_GPOVAL(n)	(0xf << HDP_MUX_SHIFT(n))

#define HDP_PIN			8
#define HDP_FUNC		16
#define HDP_FUNC_TOTAL		(HDP_PIN * HDP_FUNC)

struct stm32_hdp {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct pinctrl_dev *pctl_dev;
	struct gpio_generic_chip gpio_chip;
	u32 mux_conf;
	u32 gposet_conf;
	const char * const *func_name;
};

static const struct pinctrl_pin_desc stm32_hdp_pins[] = {
	PINCTRL_PIN(0, "HDP0"),
	PINCTRL_PIN(1, "HDP1"),
	PINCTRL_PIN(2, "HDP2"),
	PINCTRL_PIN(3, "HDP3"),
	PINCTRL_PIN(4, "HDP4"),
	PINCTRL_PIN(5, "HDP5"),
	PINCTRL_PIN(6, "HDP6"),
	PINCTRL_PIN(7, "HDP7"),
};

static const char * const func_name_mp13[] = {
	//HDP0 functions:
	"pwr_pwrwake_sys",
	"pwr_stop_forbidden",
	"pwr_stdby_wakeup",
	"pwr_encomp_vddcore",
	"bsec_out_sec_niden",
	"aiec_sys_wakeup",
	"none",
	"none",
	"ddrctrl_lp_req",
	"pwr_ddr_ret_enable_n",
	"dts_clk_ptat",
	"none",
	"sram3ctrl_tamp_erase_act",
	"none",
	"none",
	"gpoval0",
	//HDP1 functions:
	"pwr_sel_vth_vddcpu",
	"pwr_mpu_ram_lowspeed",
	"ca7_naxierrirq",
	"pwr_okin_mr",
	"bsec_out_sec_dbgen",
	"aiec_c1_wakeup",
	"rcc_pwrds_mpu",
	"none",
	"ddrctrl_dfi_ctrlupd_req",
	"ddrctrl_cactive_ddrc_asr",
	"none",
	"none",
	"sram3ctrl_hw_erase_act",
	"nic400_s0_bready",
	"none",
	"gpoval1",
	//HDP2 functions:
	"pwr_pwrwake_mpu",
	"pwr_mpu_clock_disable_ack",
	"ca7_ndbgreset_i",
	"none",
	"bsec_in_rstcore_n",
	"bsec_out_sec_bsc_dis",
	"none",
	"none",
	"ddrctrl_dfi_init_complete",
	"ddrctrl_perf_op_is_refresh",
	"ddrctrl_gskp_dfi_lp_req",
	"none",
	"sram3ctrl_sw_erase_act",
	"nic400_s0_bvalid",
	"none",
	"gpoval2",
	//HDP3 functions:
	"pwr_sel_vth_vddcore",
	"pwr_mpu_clock_disable_req",
	"ca7_npmuirq0",
	"ca7_nfiqout0",
	"bsec_out_sec_dftlock",
	"bsec_out_sec_jtag_dis",
	"rcc_pwrds_sys",
	"sram3ctrl_tamp_erase_req",
	"ddrctrl_stat_ddrc_reg_selfref_type0",
	"none",
	"dts_valobus1_0",
	"dts_valobus2_0",
	"tamp_potential_tamp_erfcfg",
	"nic400_s0_wready",
	"nic400_s0_rready",
	"gpoval3",
	//HDP4 functions:
	"none",
	"pwr_stop2_active",
	"ca7_nl2reset_i",
	"ca7_npreset_varm_i",
	"bsec_out_sec_dften",
	"bsec_out_sec_dbgswenable",
	"eth1_out_pmt_intr_o",
	"eth2_out_pmt_intr_o",
	"ddrctrl_stat_ddrc_reg_selfref_type1",
	"ddrctrl_cactive_0",
	"dts_valobus1_1",
	"dts_valobus2_1",
	"tamp_nreset_sram_ercfg",
	"nic400_s0_wlast",
	"nic400_s0_rlast",
	"gpoval4",
	//HDP5 functions:
	"ca7_standbywfil2",
	"pwr_vth_vddcore_ack",
	"ca7_ncorereset_i",
	"ca7_nirqout0",
	"bsec_in_pwrok",
	"bsec_out_sec_deviceen",
	"eth1_out_lpi_intr_o",
	"eth2_out_lpi_intr_o",
	"ddrctrl_cactive_ddrc",
	"ddrctrl_wr_credit_cnt",
	"dts_valobus1_2",
	"dts_valobus2_2",
	"pka_pka_itamp_out",
	"nic400_s0_wvalid",
	"nic400_s0_rvalid",
	"gpoval5",
	//HDP6 functions:
	"ca7_standbywfe0",
	"pwr_vth_vddcpu_ack",
	"ca7_evento",
	"none",
	"bsec_in_tamper_det",
	"bsec_out_sec_spniden",
	"eth1_out_mac_speed_o1",
	"eth2_out_mac_speed_o1",
	"ddrctrl_csysack_ddrc",
	"ddrctrl_lpr_credit_cnt",
	"dts_valobus1_3",
	"dts_valobus2_3",
	"saes_tamper_out",
	"nic400_s0_awready",
	"nic400_s0_arready",
	"gpoval6",
	//HDP7 functions:
	"ca7_standbywfi0",
	"pwr_rcc_vcpu_rdy",
	"ca7_eventi",
	"ca7_dbgack0",
	"bsec_out_fuse_ok",
	"bsec_out_sec_spiden",
	"eth1_out_mac_speed_o0",
	"eth2_out_mac_speed_o0",
	"ddrctrl_csysreq_ddrc",
	"ddrctrl_hpr_credit_cnt",
	"dts_valobus1_4",
	"dts_valobus2_4",
	"rng_tamper_out",
	"nic400_s0_awavalid",
	"nic400_s0_aravalid",
	"gpoval7",
};

static const char * const func_name_mp15[] = {
	//HDP0 functions:
	"pwr_pwrwake_sys",
	"cm4_sleepdeep",
	"pwr_stdby_wkup",
	"pwr_encomp_vddcore",
	"bsec_out_sec_niden",
	"none",
	"rcc_cm4_sleepdeep",
	"gpu_dbg7",
	"ddrctrl_lp_req",
	"pwr_ddr_ret_enable_n",
	"dts_clk_ptat",
	"none",
	"none",
	"none",
	"none",
	"gpoval0",
	//HDP1 functions:
	"pwr_pwrwake_mcu",
	"cm4_halted",
	"ca7_naxierrirq",
	"pwr_okin_mr",
	"bsec_out_sec_dbgen",
	"exti_sys_wakeup",
	"rcc_pwrds_mpu",
	"gpu_dbg6",
	"ddrctrl_dfi_ctrlupd_req",
	"ddrctrl_cactive_ddrc_asr",
	"none",
	"none",
	"none",
	"none",
	"none",
	"gpoval1",
	//HDP2 functions:
	"pwr_pwrwake_mpu",
	"cm4_rxev",
	"ca7_npmuirq1",
	"ca7_nfiqout1",
	"bsec_in_rstcore_n",
	"exti_c2_wakeup",
	"rcc_pwrds_mcu",
	"gpu_dbg5",
	"ddrctrl_dfi_init_complete",
	"ddrctrl_perf_op_is_refresh",
	"ddrctrl_gskp_dfi_lp_req",
	"none",
	"none",
	"none",
	"none",
	"gpoval2",
	//HDP3 functions:
	"pwr_sel_vth_vddcore",
	"cm4_txev",
	"ca7_npmuirq0",
	"ca7_nfiqout0",
	"bsec_out_sec_dftlock",
	"exti_c1_wakeup",
	"rcc_pwrds_sys",
	"gpu_dbg4",
	"ddrctrl_stat_ddrc_reg_selfref_type0",
	"ddrctrl_cactive_1",
	"dts_valobus1_0",
	"dts_valobus2_0",
	"none",
	"none",
	"none",
	"gpoval3",
	//HDP4 functions:
	"pwr_mpu_pdds_not_cstbydis",
	"cm4_sleeping",
	"ca7_nreset1",
	"ca7_nirqout1",
	"bsec_out_sec_dften",
	"bsec_out_sec_dbgswenable",
	"eth_out_pmt_intr_o",
	"gpu_dbg3",
	"ddrctrl_stat_ddrc_reg_selfref_type1",
	"ddrctrl_cactive_0",
	"dts_valobus1_1",
	"dts_valobus2_1",
	"none",
	"none",
	"none",
	"gpoval4",
	//HDP5 functions:
	"ca7_standbywfil2",
	"pwr_vth_vddcore_ack",
	"ca7_nreset0",
	"ca7_nirqout0",
	"bsec_in_pwrok",
	"bsec_out_sec_deviceen",
	"eth_out_lpi_intr_o",
	"gpu_dbg2",
	"ddrctrl_cactive_ddrc",
	"ddrctrl_wr_credit_cnt",
	"dts_valobus1_2",
	"dts_valobus2_2",
	"none",
	"none",
	"none",
	"gpoval5",
	//HDP6 functions:
	"ca7_standbywfi1",
	"ca7_standbywfe1",
	"ca7_evento",
	"ca7_dbgack1",
	"none",
	"bsec_out_sec_spniden",
	"eth_out_mac_speed_o1",
	"gpu_dbg1",
	"ddrctrl_csysack_ddrc",
	"ddrctrl_lpr_credit_cnt",
	"dts_valobus1_3",
	"dts_valobus2_3",
	"none",
	"none",
	"none",
	"gpoval6",
	//HDP7 functions:
	"ca7_standbywfi0",
	"ca7_standbywfe0",
	"none",
	"ca7_dbgack0",
	"bsec_out_fuse_ok",
	"bsec_out_sec_spiden",
	"eth_out_mac_speed_o0",
	"gpu_dbg0",
	"ddrctrl_csysreq_ddrc",
	"ddrctrl_hpr_credit_cnt",
	"dts_valobus1_4",
	"dts_valobus2_4",
	"none",
	"none",
	"none",
	"gpoval7"
};

static const char * const func_name_mp25[] = {
	//HDP0 functions:
	"pwr_pwrwake_sys",
	"cpu2_sleep_deep",
	"bsec_out_tst_sdr_unlock_or_disable_scan",
	"bsec_out_nidenm",
	"bsec_out_nidena",
	"cpu2_state_0",
	"rcc_pwrds_sys",
	"gpu_dbg7",
	"ddrss_csysreq_ddrc",
	"ddrss_dfi_phyupd_req",
	"cpu3_sleep_deep",
	"d2_gbl_per_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_0",
	"pcie_usb_cxpl_debug_info_ei_8",
	"d3_state_0",
	"gpoval0",
	//HDP1 functions:
	"pwr_pwrwake_cpu2",
	"cpu2_halted",
	"cpu2_state_1",
	"bsec_out_dbgenm",
	"bsec_out_dbgena",
	"exti1_sys_wakeup",
	"rcc_pwrds_cpu2",
	"gpu_dbg6",
	"ddrss_csysack_ddrc",
	"ddrss_dfi_phymstr_req",
	"cpu3_halted",
	"d2_gbl_per_dma_req",
	"pcie_usb_cxpl_debug_info_ei_1",
	"pcie_usb_cxpl_debug_info_ei_9",
	"d3_state_1",
	"gpoval1",
	//HDP2 functions:
	"pwr_pwrwake_cpu1",
	"cpu2_rxev",
	"cpu1_npumirq1",
	"cpu1_nfiqout1",
	"bsec_out_shdbgen",
	"exti1_cpu2_wakeup",
	"rcc_pwrds_cpu1",
	"gpu_dbg5",
	"ddrss_cactive_ddrc",
	"ddrss_dfi_lp_req",
	"cpu3_rxev",
	"hpdma1_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_2",
	"pcie_usb_cxpl_debug_info_ei_10",
	"d3_state_2",
	"gpoval2",
	//HDP3 functions:
	"pwr_sel_vth_vddcpu",
	"cpu2_txev",
	"cpu1_npumirq0",
	"cpu1_nfiqout0",
	"bsec_out_ddbgen",
	"exti1_cpu1_wakeup",
	"cpu3_state_0",
	"gpu_dbg4",
	"ddrss_mcdcg_en",
	"ddrss_dfi_freq_0",
	"cpu3_txev",
	"hpdma2_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_3",
	"pcie_usb_cxpl_debug_info_ei_11",
	"d1_state_0",
	"gpoval3",
	//HDP4 functions:
	"pwr_sel_vth_vddcore",
	"cpu2_sleeping",
	"cpu1_evento",
	"cpu1_nirqout1",
	"bsec_out_spnidena",
	"exti2_d3_wakeup",
	"eth1_out_pmt_intr_o",
	"gpu_dbg3",
	"ddrss_dphycg_en",
	"ddrss_obsp0",
	"cpu3_sleeping",
	"hpdma3_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_4",
	"pcie_usb_cxpl_debug_info_ei_12",
	"d1_state_1",
	"gpoval4",
	//HDP5 functions:
	"cpu1_standby_wfil2",
	"none",
	"none",
	"cpu1_nirqout0",
	"bsec_out_spidena",
	"exti2_cpu3_wakeup",
	"eth1_out_lpi_intr_o",
	"gpu_dbg2",
	"ddrctrl_dfi_init_start",
	"ddrss_obsp1",
	"cpu3_state_1",
	"d3_gbl_per_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_5",
	"pcie_usb_cxpl_debug_info_ei_13",
	"d1_state_2",
	"gpoval5",
	//HDP6 functions:
	"cpu1_standby_wfi1",
	"cpu1_standby_wfe1",
	"cpu1_halted1",
	"cpu1_naxierrirq",
	"bsec_out_spnidenm",
	"exti2_cpu2_wakeup",
	"eth2_out_pmt_intr_o",
	"gpu_dbg1",
	"ddrss_dfi_init_complete",
	"ddrss_obsp2",
	"d2_state_0",
	"d3_gbl_per_dma_req",
	"pcie_usb_cxpl_debug_info_ei_6",
	"pcie_usb_cxpl_debug_info_ei_14",
	"cpu1_state_0",
	"gpoval6",
	//HDP7 functions:
	"cpu1_standby_wfi0",
	"cpu1_standby_wfe0",
	"cpu1_halted0",
	"none",
	"bsec_out_spidenm",
	"exti2_cpu1__wakeup",
	"eth2_out_lpi_intr_o",
	"gpu_dbg0",
	"ddrss_dfi_ctrlupd_req",
	"ddrss_obsp3",
	"d2_state_1",
	"lpdma1_clk_bus_req",
	"pcie_usb_cxpl_debug_info_ei_7",
	"pcie_usb_cxpl_debug_info_ei_15",
	"cpu1_state_1",
	"gpoval7",
};

static const char * const stm32_hdp_pins_group[] = {
	"HDP0",
	"HDP1",
	"HDP2",
	"HDP3",
	"HDP4",
	"HDP5",
	"HDP6",
	"HDP7"
};

static int stm32_hdp_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int stm32_hdp_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(stm32_hdp_pins);
}

static const char *stm32_hdp_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						    unsigned int selector)
{
	return stm32_hdp_pins[selector].name;
}

static int stm32_hdp_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
					    const unsigned int **pins, unsigned int *num_pins)
{
	*pins = &stm32_hdp_pins[selector].number;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops stm32_hdp_pinctrl_ops = {
	.get_groups_count = stm32_hdp_pinctrl_get_groups_count,
	.get_group_name	  = stm32_hdp_pinctrl_get_group_name,
	.get_group_pins	  = stm32_hdp_pinctrl_get_group_pins,
	.dt_node_to_map	  = pinconf_generic_dt_node_to_map_all,
	.dt_free_map	  = pinconf_generic_dt_free_map,
};

static int stm32_hdp_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	return HDP_FUNC_TOTAL;
}

static const char *stm32_hdp_pinmux_get_function_name(struct pinctrl_dev *pctldev,
							  unsigned int selector)
{
	struct stm32_hdp *hdp = pinctrl_dev_get_drvdata(pctldev);

	return hdp->func_name[selector];
}

static int stm32_hdp_pinmux_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
						const char *const **groups,
						unsigned int *num_groups)
{
	u32 index = selector / HDP_FUNC;

	*groups = &stm32_hdp_pins[index].name;
	*num_groups = 1;

	return 0;
}

static int stm32_hdp_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
				    unsigned int group_selector)
{
	struct stm32_hdp *hdp = pinctrl_dev_get_drvdata(pctldev);

	unsigned int pin = stm32_hdp_pins[group_selector].number;
	u32 mux;

	func_selector %= HDP_FUNC;
	mux = readl_relaxed(hdp->base + HDP_MUX);
	mux &= ~HDP_MUX_MASK(pin);
	mux |= func_selector << HDP_MUX_SHIFT(pin);

	writel_relaxed(mux, hdp->base + HDP_MUX);
	hdp->mux_conf = mux;

	return 0;
}

static const struct pinmux_ops stm32_hdp_pinmux_ops = {
	.get_functions_count = stm32_hdp_pinmux_get_functions_count,
	.get_function_name   = stm32_hdp_pinmux_get_function_name,
	.get_function_groups = stm32_hdp_pinmux_get_function_groups,
	.set_mux	     = stm32_hdp_pinmux_set_mux,
	.gpio_set_direction  = NULL,
};

static const struct pinctrl_desc stm32_hdp_pdesc = {
	.name	 = DRIVER_NAME,
	.pins	 = stm32_hdp_pins,
	.npins	 = ARRAY_SIZE(stm32_hdp_pins),
	.pctlops = &stm32_hdp_pinctrl_ops,
	.pmxops	 = &stm32_hdp_pinmux_ops,
	.owner	 = THIS_MODULE,
};

static const struct of_device_id stm32_hdp_of_match[] = {
	{
		.compatible = "st,stm32mp131-hdp",
		.data = &func_name_mp13,
	},
	{
		.compatible = "st,stm32mp151-hdp",
		.data = &func_name_mp15,
	},
	{
		.compatible = "st,stm32mp251-hdp",
		.data = &func_name_mp25,
	},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_hdp_of_match);

static int stm32_hdp_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct stm32_hdp *hdp;
	u8 version;
	int err;

	hdp = devm_kzalloc(dev, sizeof(*hdp), GFP_KERNEL);
	if (!hdp)
		return -ENOMEM;
	hdp->dev = dev;

	platform_set_drvdata(pdev, hdp);

	hdp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdp->base))
		return PTR_ERR(hdp->base);

	hdp->func_name = of_device_get_match_data(dev);
	if (!hdp->func_name)
		return dev_err_probe(dev, -ENODEV, "No function name provided\n");

	hdp->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(hdp->clk))
		return dev_err_probe(dev, PTR_ERR(hdp->clk), "No HDP clock provided\n");

	err = devm_pinctrl_register_and_init(dev, &stm32_hdp_pdesc, hdp, &hdp->pctl_dev);
	if (err)
		return dev_err_probe(dev, err, "Failed to register pinctrl\n");

	err = pinctrl_enable(hdp->pctl_dev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable pinctrl\n");

	hdp->gpio_chip.gc.get_direction = stm32_hdp_gpio_get_direction;
	hdp->gpio_chip.gc.ngpio	     = ARRAY_SIZE(stm32_hdp_pins);
	hdp->gpio_chip.gc.can_sleep     = true;
	hdp->gpio_chip.gc.names	     = stm32_hdp_pins_group;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = hdp->base + HDP_GPOVAL,
		.set = hdp->base + HDP_GPOSET,
		.clr = hdp->base + HDP_GPOCLR,
		.flags = GPIO_GENERIC_NO_INPUT,
	};

	err = gpio_generic_chip_init(&hdp->gpio_chip, &config);
	if (err)
		return dev_err_probe(dev, err, "Failed to init the generic GPIO chip\n");

	err = devm_gpiochip_add_data(dev, &hdp->gpio_chip.gc, hdp);
	if (err)
		return dev_err_probe(dev, err, "Failed to add gpiochip\n");

	writel_relaxed(HDP_CTRL_ENABLE, hdp->base + HDP_CTRL);

	version = readl_relaxed(hdp->base + HDP_VERR);
	dev_dbg(dev, "STM32 HDP version %u.%u initialized\n", version >> 4, version & 0x0f);

	return 0;
}

static void stm32_hdp_remove(struct platform_device *pdev)
{
	struct stm32_hdp *hdp = platform_get_drvdata(pdev);

	writel_relaxed(HDP_CTRL_DISABLE, hdp->base + HDP_CTRL);
}

static int stm32_hdp_suspend(struct device *dev)
{
	struct stm32_hdp *hdp = dev_get_drvdata(dev);

	hdp->gposet_conf = readl_relaxed(hdp->base + HDP_GPOSET);

	pinctrl_pm_select_sleep_state(dev);

	clk_disable_unprepare(hdp->clk);

	return 0;
}

static int stm32_hdp_resume(struct device *dev)
{
	struct stm32_hdp *hdp = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(hdp->clk);
	if (err) {
		dev_err(dev, "Failed to prepare_enable clk (%d)\n", err);
		return err;
	}

	writel_relaxed(HDP_CTRL_ENABLE, hdp->base + HDP_CTRL);
	writel_relaxed(hdp->gposet_conf, hdp->base + HDP_GPOSET);
	writel_relaxed(hdp->mux_conf, hdp->base + HDP_MUX);

	pinctrl_pm_select_default_state(dev);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(stm32_hdp_pm_ops, stm32_hdp_suspend, stm32_hdp_resume);

static struct platform_driver stm32_hdp_driver = {
	.probe = stm32_hdp_probe,
	.remove = stm32_hdp_remove,
	.driver = {
		.name = DRIVER_NAME,
		.pm = pm_sleep_ptr(&stm32_hdp_pm_ops),
		.of_match_table = stm32_hdp_of_match,
	}
};

module_platform_driver(stm32_hdp_driver);

MODULE_AUTHOR("Clément Le Goffic");
MODULE_DESCRIPTION("STMicroelectronics STM32 Hardware Debug Port driver");
MODULE_LICENSE("GPL");
