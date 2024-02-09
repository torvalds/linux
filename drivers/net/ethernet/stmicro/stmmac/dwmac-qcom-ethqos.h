/* SPDX-License-Identifier: GPL-2.0-only */
/*Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.*/
#ifndef	_DWMAC_QCOM_ETHQOS_H
#define	_DWMAC_QCOM_ETHQOS_H

#define DRV_NAME "qcom-ethqos"
#define ETHQOSDBG(fmt, args...) \
	pr_debug(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args)
#define ETHQOSERR(fmt, args...) \
	pr_err(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args)
#define ETHQOSINFO(fmt, args...) \
	pr_info(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args)

#define PM_WAKEUP_MS			5000

#define RGMII_IO_MACRO_CONFIG		0x0
#define SDCC_HC_REG_DLL_CONFIG		0x4
#define SDCC_TEST_CTL			0x8
#define SDCC_HC_REG_DDR_CONFIG		0xC
#define SDCC_HC_REG_DLL_CONFIG2		0x10
#define SDC4_STATUS			0x14
#define SDCC_USR_CTL			0x18
#define RGMII_IO_MACRO_CONFIG2		0x1C

#define EMAC_WRAPPER_SGMII_PHY_CNTRL0_v3	0xF0
#define EMAC_WRAPPER_SGMII_PHY_CNTRL1_v3	0xF4
#define EMAC_WRAPPER_SGMII_PHY_CNTRL0	0x170
#define EMAC_WRAPPER_SGMII_PHY_CNTRL1	0x174
#define EMAC_WRAPPER_USXGMII_MUX_SEL	0x1D0
#define RGMII_IO_MACRO_SCRATCH_2	0x44
#define RGMII_IO_MACRO_BYPASS		0x16C

#define EMAC_HW_NONE 0
#define EMAC_HW_v2_1_1 0x20010001
#define EMAC_HW_v2_1_2 0x20010002
#define EMAC_HW_v2_3_0 0x20030000
#define EMAC_HW_v2_3_1 0x20030001
#define EMAC_HW_v3_0_0_RG 0x30000000
#define EMAC_HW_v3_1_0 0x30010000
#define EMAC_HW_v4_0_0 0x40000000
#define EMAC_HW_vMAX 9

#define EMAC_GDSC_EMAC_NAME "gdsc_emac"

struct ethqos_emac_por {
	unsigned int offset;
	unsigned int value;
};

struct ethqos_emac_driver_data {
	struct ethqos_emac_por *por;
	unsigned int num_por;
};

struct qcom_ethqos {
	struct platform_device *pdev;
	void __iomem *rgmii_base;
	void __iomem *sgmii_base;
	void __iomem *ioaddr;
	unsigned int rgmii_clk_rate;
	struct clk *rgmii_clk;
	struct clk *phyaux_clk;
	struct clk *sgmiref_clk;

	unsigned int speed;

	int gpio_phy_intr_redirect;
	u32 phy_intr;
	/* Work struct for handling phy interrupt */
	struct work_struct emac_phy_work;

	const struct ethqos_emac_por *por;
	unsigned int num_por;
	unsigned int emac_ver;

	struct regulator *gdsc_emac;
	struct regulator *reg_rgmii;
	struct regulator *reg_emac_phy;
	struct regulator *reg_rgmii_io_pads;

	int curr_serdes_speed;

	/* Boolean to check if clock is suspended*/
	int clks_suspended;
	struct completion clk_enable_done;
	/* Boolean flag for turning off GDSC during suspend */
	bool gdsc_off_on_suspend;

};

int ethqos_init_regulators(struct qcom_ethqos *ethqos);
void ethqos_disable_regulators(struct qcom_ethqos *ethqos);
int ethqos_init_gpio(struct qcom_ethqos *ethqos);
void ethqos_free_gpios(struct qcom_ethqos *ethqos);
void *qcom_ethqos_get_priv(struct qcom_ethqos *ethqos);
#endif
