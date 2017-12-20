/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "[WMT-CONSYS-HW]"

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include "osal_typedef.h"
#include "mtk_wcn_consys_hw.h"
#include <linux/mfd/mt6323/registers.h>
#include <soc/mediatek/pmic_wrap.h>
#include <linux/regmap.h>
#if CONSYS_EMI_MPU_SETTING
#include <emi_mpu.h>
#endif

#include <linux/regulator/consumer.h>
#ifdef CONFIG_MTK_HIBERNATION
#include <mtk_hibernate_dpm.h>
#endif

#include <linux/of_reserved_mem.h>

#if CONSYS_CLOCK_BUF_CTRL
#include <mt_clkbuf_ctl.h>
#endif

#include <linux/pm_runtime.h>
#include <linux/reset.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static INT32 mtk_wmt_probe(struct platform_device *pdev);
static INT32 mtk_wmt_remove(struct platform_device *pdev);


/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

struct CONSYS_BASE_ADDRESS conn_reg;
static phys_addr_t gConEmiPhyBase;
static UINT8 __iomem *pEmibaseaddr;
static struct clk *clk_infra_conn_main;	/*ctrl infra_connmcu_bus clk */
static struct platform_device *my_pdev;
static struct reset_control *rstc;
static struct regulator *reg_VCN18;
static struct regulator *reg_VCN28;
static struct regulator *reg_VCN33_BT;
static struct regulator *reg_VCN33_WIFI;
static struct pinctrl *consys_pinctrl;
static struct pinctrl *mt6625_spi_pinctrl;
static struct pinctrl_state *mt6625_spi_default;
static struct regmap *pmic_regmap;
#define DYNAMIC_DUMP_GROUP_NUM 5

static const struct of_device_id apwmt_of_ids[] = {
	{.compatible = "mediatek,mt7623-consys",}
};
MODULE_DEVICE_TABLE(of, apwmt_of_ids);

static struct platform_driver mtk_wmt_dev_drv = {
	.probe = mtk_wmt_probe,
	.remove = mtk_wmt_remove,
	.driver = {
		   .name = "mt7623consys",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(apwmt_of_ids),
		   },
};

static INT32 mtk_wmt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *node = NULL;

	pm_runtime_enable(&pdev->dev);
	my_pdev = pdev;
	mt6625_spi_pinctrl  = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(mt6625_spi_pinctrl)) {
		ret = PTR_ERR(mt6625_spi_pinctrl);
		WMT_PLAT_ERR_FUNC("Wmt cannot find pinctrl!\n");
		goto set_pin_exit;
	}
	mt6625_spi_default = pinctrl_lookup_state(mt6625_spi_pinctrl, "consys_pins_default");
	if (IS_ERR(mt6625_spi_default)) {
		ret = PTR_ERR(mt6625_spi_default);
		WMT_PLAT_ERR_FUNC("Wmt Cannot find pinctrl default!\n");
		goto set_pin_exit;
	}
	pinctrl_select_state(mt6625_spi_pinctrl, mt6625_spi_default);
set_pin_exit:

	node = of_parse_phandle(pdev->dev.of_node, "mediatek,pwrap-regmap", 0);
	if (node) {
		pmic_regmap = pwrap_node_to_regmap(node);
		if (IS_ERR(pmic_regmap))
			goto set_pmic_wrap_exit;
	} else {
		WMT_PLAT_ERR_FUNC("Pwrap node has not register regmap.\n");
		goto set_pmic_wrap_exit;
	}
set_pmic_wrap_exit:

	clk_infra_conn_main = devm_clk_get(&pdev->dev, "consysbus");
	if (IS_ERR(clk_infra_conn_main)) {
		WMT_PLAT_ERR_FUNC("sean debug [CCF]cannot get clk_infra_conn_main clock.\n");
		return PTR_ERR(clk_infra_conn_main);
	}
	WMT_PLAT_DBG_FUNC("[CCF]clk_infra_conn_main=%p\n", clk_infra_conn_main);

	reg_VCN18 = devm_regulator_get(&pdev->dev, "vcn18");
	if (IS_ERR(reg_VCN18)) {
		ret = PTR_ERR(reg_VCN18);
		WMT_PLAT_ERR_FUNC("Regulator_get VCN_1V8 fail, ret=%d\n", ret);
	}
	reg_VCN28 = devm_regulator_get(&pdev->dev, "vcn28");
	if (IS_ERR(reg_VCN28)) {
		ret = PTR_ERR(reg_VCN28);
		WMT_PLAT_ERR_FUNC("Regulator_get VCN_2V8 fail, ret=%d\n", ret);
	}
	reg_VCN33_BT = devm_regulator_get(&pdev->dev, "vcn33_bt");
	if (IS_ERR(reg_VCN33_BT)) {
		ret = PTR_ERR(reg_VCN33_BT);
		WMT_PLAT_ERR_FUNC("Regulator_get VCN33_BT fail, ret=%d\n", ret);
	}
	reg_VCN33_WIFI = devm_regulator_get(&pdev->dev, "vcn33_wifi");
	if (IS_ERR(reg_VCN33_WIFI)) {
		ret = PTR_ERR(reg_VCN33_WIFI);
		WMT_PLAT_ERR_FUNC("Regulator_get VCN33_WIFI fail, ret=%d\n", ret);
	}

	rstc = devm_reset_control_get(&pdev->dev, "connsys");
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		WMT_PLAT_ERR_FUNC("CanNot get consys reset. ret=%d\n", ret);
		return PTR_ERR(rstc);
	}

	consys_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(consys_pinctrl)) {
		ret = PTR_ERR(consys_pinctrl);
		WMT_PLAT_ERR_FUNC("CanNot find consys pinctrl. ret=%d\n", ret);
		return PTR_ERR(consys_pinctrl);
	}
	return 0;
}

static INT32 mtk_wmt_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

VOID mtk_wcn_consys_power_on(VOID)
{
	INT32 iRet = -1;
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
	iRet = pm_runtime_get_sync(&my_pdev->dev);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
	if (iRet)
		WMT_PLAT_ERR_FUNC("pm_runtime_get_sync() fail(%d)\n", iRet);
	else
		WMT_PLAT_INFO_FUNC("pm_runtime_get_sync() CONSYS ok\n");

	iRet = device_init_wakeup(&my_pdev->dev, true);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
	if (iRet)
		WMT_PLAT_ERR_FUNC("device_init_wakeup(true) fail.\n");
	else
		WMT_PLAT_INFO_FUNC("device_init_wakeup(true) CONSYS ok\n");
}

VOID mtk_wcn_consys_power_off(VOID)
{
	INT32 iRet = -1;

	iRet = pm_runtime_put_sync(&my_pdev->dev);
	if (iRet)
		WMT_PLAT_ERR_FUNC("pm_runtime_put_sync() fail.\n");
	else
		WMT_PLAT_INFO_FUNC("pm_runtime_put_sync() CONSYS ok\n");

	iRet = device_init_wakeup(&my_pdev->dev, false);
	if (iRet)
		WMT_PLAT_ERR_FUNC("device_init_wakeup(false) fail.\n");
	else
		WMT_PLAT_INFO_FUNC("device_init_wakeup(false) CONSYS ok\n");
}

INT32 mtk_wcn_consys_hw_reg_ctrl(UINT32 on, UINT32 co_clock_type)
{
	UINT32 retry = 10;
	UINT32 consysHwChipId = 0;

	WMT_PLAT_DBG_FUNC("CONSYS-HW-REG-CTRL(0x%08x),start\n", on);
	if (on) {
		WMT_PLAT_DBG_FUNC("++\n");
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		/*need PMIC driver provide new API protocol */
		/*1.AP power on VCN_1V8 LDO (with PMIC_WRAP API) VCN_1V8  */
		regulator_set_mode(reg_VCN18, REGULATOR_MODE_STANDBY);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		/* VOL_DEFAULT, VOL_1200, VOL_1300, VOL_1500, VOL_1800... */
		if (reg_VCN18) {
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
			regulator_set_voltage(reg_VCN18, 1800000, 1800000);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
			if (regulator_enable(reg_VCN18))
				WMT_PLAT_ERR_FUNC("enable VCN18 fail\n");
			else
				WMT_PLAT_DBG_FUNC("enable VCN18 ok\n");
		}
		udelay(150);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		if (co_clock_type) {
			/*step0,clk buf ctrl */
			WMT_PLAT_INFO_FUNC("co clock type(%d),turn on clk buf\n", co_clock_type);
#if CONSYS_CLOCK_BUF_CTRL
			clk_buf_ctrl(CLK_BUF_CONN, 1);
#endif
			/*if co-clock mode: */
			/*2.set VCN28 to SW control mode (with PMIC_WRAP API) */
			/*turn on VCN28 LDO only when FMSYS is activated"  */
			regmap_update_bits(pmic_regmap, 0x41C, 0x1 << 14, 0x0 << 14);/*V28*/
		} else {
			/*if NOT co-clock: */
			/*2.1.switch VCN28 to HW control mode (with PMIC_WRAP API) */
			regmap_update_bits(pmic_regmap, 0x41C, 0x1 << 14, 0x1 << 14);/*V28*/
			/*2.2.turn on VCN28 LDO (with PMIC_WRAP API)" */
			/*fix vcn28 not balance warning */
			if (reg_VCN28) {
				regulator_set_voltage(reg_VCN28, 2800000, 2800000);
				if (regulator_enable(reg_VCN28))
					WMT_PLAT_ERR_FUNC("enable VCN_2V8 fail!\n");
				else
					WMT_PLAT_DBG_FUNC("enable VCN_2V8 ok\n");
			}
		}

		/*3.assert CONNSYS CPU SW reset  0x10007018 "[12]=1'b1  [31:24]=8'h88 (key)" */
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		reset_control_reset(rstc);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		mtk_wcn_consys_power_on();
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		/*11.26M is ready now, delay 10us for mem_pd de-assert */
		udelay(10);
		/*enable AP bus clock : connmcu_bus_pd  API: enable_clock() ++?? */
		clk_prepare_enable(clk_infra_conn_main);
printk(KERN_ALERT "DEBUG: Passed %s %d \n",__FUNCTION__,__LINE__);
		WMT_PLAT_DBG_FUNC("[CCF]enable clk_infra_conn_main\n");
		/*12.poll CONNSYS CHIP ID until chipid is returned  0x18070008 */
		while (retry-- > 0) {
			consysHwChipId = CONSYS_REG_READ(conn_reg.mcu_base + CONSYS_CHIP_ID_OFFSET) - 0xf6d;

			if ((consysHwChipId == 0x0321) || (consysHwChipId == 0x0335) || (consysHwChipId == 0x0337)) {
				WMT_PLAT_INFO_FUNC("retry(%d)consys chipId(0x%08x)\n", retry, consysHwChipId);
				break;
			}
			if ((consysHwChipId == 0x8163) || (consysHwChipId == 0x8127) || (consysHwChipId == 0x7623)) {
				WMT_PLAT_INFO_FUNC("retry(%d)consys chipId(0x%08x)\n", retry, consysHwChipId);
				break;
			}

			WMT_PLAT_ERR_FUNC("Read CONSYS chipId(0x%08x)", consysHwChipId);
			msleep(20);
		}

		if ((0 == retry) || (0 == consysHwChipId))
			WMT_PLAT_ERR_FUNC("Maybe has a consys power on issue,(0x%08x)\n", consysHwChipId);

		msleep(40);

	} else {

		clk_disable_unprepare(clk_infra_conn_main);
		WMT_PLAT_DBG_FUNC("[CCF] clk_disable_unprepare(clk_infra_conn_main) calling\n");
		mtk_wcn_consys_power_off();

		if (co_clock_type) {
			/*VCN28 has been turned off by GPS OR FM */
#if CONSYS_CLOCK_BUF_CTRL
			clk_buf_ctrl(CLK_BUF_CONN, 0);
#endif
		} else {
			regmap_update_bits(pmic_regmap, 0x41C, 0x1 << 14, 0x0 << 14);/*V28*/
			/*turn off VCN28 LDO (with PMIC_WRAP API)" */
			if (reg_VCN28) {
				if (regulator_disable(reg_VCN28))
					WMT_PLAT_ERR_FUNC("disable VCN_2V8 fail!\n");
				else
					WMT_PLAT_DBG_FUNC("disable VCN_2V8 ok\n");
			}
		}

		/*AP power off MT6625L VCN_1V8 LDO */
		regulator_set_mode(reg_VCN18, REGULATOR_MODE_STANDBY);
		if (reg_VCN18) {
			if (regulator_disable(reg_VCN18))
				WMT_PLAT_ERR_FUNC("disable VCN_1V8 fail!\n");
			else
				WMT_PLAT_DBG_FUNC("disable VCN_1V8 ok\n");
		}

	}
	WMT_PLAT_DBG_FUNC("CONSYS-HW-REG-CTRL(0x%08x),finish\n", on);
	return 0;
}

INT32 mtk_wcn_consys_hw_gpio_ctrl(UINT32 on)
{
	INT32 iRet = 0;

	WMT_PLAT_DBG_FUNC("CONSYS-HW-GPIO-CTRL(0x%08x), start\n", on);

	if (on) {

		/* TODO: [FixMe][GeorgeKuo] double check if BGF_INT is implemented ok */
		/* iRet += wmt_plat_gpio_ctrl(PIN_BGF_EINT, PIN_STA_MUX); */
		iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_INIT);
		iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS);
		WMT_PLAT_DBG_FUNC("CONSYS-HW, BGF IRQ registered and disabled\n");

	} else {

		/* set bgf eint/all eint to deinit state, namely input low state */
		iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS);
		iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_DEINIT);
		WMT_PLAT_DBG_FUNC("CONSYS-HW, BGF IRQ unregistered and disabled\n");
		/* iRet += wmt_plat_gpio_ctrl(PIN_BGF_EINT, PIN_STA_DEINIT); */
	}
	WMT_PLAT_DBG_FUNC("CONSYS-HW-GPIO-CTRL(0x%08x), finish\n", on);
	return iRet;

}

INT32 mtk_wcn_consys_hw_pwr_on(UINT32 co_clock_type)
{
	INT32 iRet = 0;

	WMT_PLAT_INFO_FUNC("CONSYS-HW-PWR-ON, start\n");

	iRet += mtk_wcn_consys_hw_reg_ctrl(1, co_clock_type);
	iRet += mtk_wcn_consys_hw_gpio_ctrl(1);

	WMT_PLAT_INFO_FUNC("CONSYS-HW-PWR-ON, finish(%d)\n", iRet);
	return iRet;
}

INT32 mtk_wcn_consys_hw_pwr_off(VOID)
{
	INT32 iRet = 0;

	WMT_PLAT_INFO_FUNC("CONSYS-HW-PWR-OFF, start\n");

	iRet += mtk_wcn_consys_hw_reg_ctrl(0, 0);
	iRet += mtk_wcn_consys_hw_gpio_ctrl(0);

	WMT_PLAT_INFO_FUNC("CONSYS-HW-PWR-OFF, finish(%d)\n", iRet);
	return iRet;
}

INT32 mtk_wcn_consys_hw_rst(UINT32 co_clock_type)
{
	INT32 iRet = 0;

	WMT_PLAT_INFO_FUNC("CONSYS-HW, hw_rst start, eirq should be disabled before this step\n");

	/*1. do whole hw power off flow */
	iRet += mtk_wcn_consys_hw_reg_ctrl(0, co_clock_type);

	/*2. do whole hw power on flow */
	iRet += mtk_wcn_consys_hw_reg_ctrl(1, co_clock_type);

	WMT_PLAT_INFO_FUNC("CONSYS-HW, hw_rst finish, eirq should be enabled after this step\n");
	return iRet;
}

#if CONSYS_BT_WIFI_SHARE_V33
INT32 mtk_wcn_consys_hw_bt_paldo_ctrl(UINT32 enable)
{
	/* spin_lock_irqsave(&gBtWifiV33.lock,gBtWifiV33.flags); */
	if (enable) {
		if (1 == gBtWifiV33.counter) {
			gBtWifiV33.counter++;
			WMT_PLAT_DBG_FUNC("V33 has been enabled,counter(%d)\n", gBtWifiV33.counter);
		} else if (2 == gBtWifiV33.counter) {
			WMT_PLAT_DBG_FUNC("V33 has been enabled,counter(%d)\n", gBtWifiV33.counter);
		} else {
#if CONSYS_PMIC_CTRL_ENABLE
			/*do BT PMIC on,depenency PMIC API ready */
			/*switch BT PALDO control from SW mode to HW mode:0x416[5]-->0x1 */
			/* VOL_DEFAULT, VOL_3300, VOL_3400, VOL_3500, VOL_3600 */
			hwPowerOn(MT6323_POWER_LDO_VCN33, VOL_3300, "wcn_drv");
			upmu_set_vcn33_on_ctrl_bt(1);
#endif
			WMT_PLAT_INFO_FUNC("WMT do BT/WIFI v3.3 on\n");
			gBtWifiV33.counter++;
		}

	} else {
		if (1 == gBtWifiV33.counter) {
			/*do BT PMIC off */
			/*switch BT PALDO control from HW mode to SW mode:0x416[5]-->0x0 */
#if CONSYS_PMIC_CTRL_ENABLE
		    upmu_set_vcn33_on_ctrl_bt(0);
			hwPowerDown(MT6323_POWER_LDO_VCN33, "wcn_drv");
#endif
			WMT_PLAT_INFO_FUNC("WMT do BT/WIFI v3.3 off\n");
			gBtWifiV33.counter--;
		} else if (2 == gBtWifiV33.counter) {
			gBtWifiV33.counter--;
			WMT_PLAT_DBG_FUNC("V33 no need disabled,counter(%d)\n", gBtWifiV33.counter);
		} else {
			WMT_PLAT_DBG_FUNC("V33 has been disabled,counter(%d)\n", gBtWifiV33.counter);
		}

	}
	/* spin_unlock_irqrestore(&gBtWifiV33.lock,gBtWifiV33.flags); */
	return 0;
}

INT32 mtk_wcn_consys_hw_wifi_paldo_ctrl(UINT32 enable)
{
	mtk_wcn_consys_hw_bt_paldo_ctrl(enable);
	return 0;
}

#else
INT32 mtk_wcn_consys_hw_bt_paldo_ctrl(UINT32 enable)
{

	if (enable) {
		/*do BT PMIC on,depenency PMIC API ready */
		/*switch BT PALDO control from SW mode to HW mode:0x416[5]-->0x1 */
		if (reg_VCN33_BT) {
			regulator_set_voltage(reg_VCN33_BT, 3300000, 3300000);
			if (regulator_enable(reg_VCN33_BT))
				WMT_PLAT_ERR_FUNC("WMT do BT PMIC on fail!\n");
		}
		regmap_update_bits(pmic_regmap, 0x416, 0x1 << 5, 0x1 << 5);/*BT*/
		WMT_PLAT_INFO_FUNC("WMT do BT PMIC on\n");
	} else {
		/*do BT PMIC off */
		/*switch BT PALDO control from HW mode to SW mode:0x416[5]-->0x0 */
		regmap_update_bits(pmic_regmap, 0x416, 0x1 << 5, 0x0 << 5);/*BT*/
		if (reg_VCN33_BT)
			if (regulator_disable(reg_VCN33_BT))
				WMT_PLAT_ERR_FUNC("WMT do BT PMIC off fail!\n");
		WMT_PLAT_INFO_FUNC("WMT do BT PMIC off\n");
	}

	return 0;

}

INT32 mtk_wcn_consys_hw_wifi_paldo_ctrl(UINT32 enable)
{

	if (enable) {
		/*do WIFI PMIC on,depenency PMIC API ready */
		/*switch WIFI PALDO control from SW mode to HW mode:0x418[14]-->0x1 */
		if (reg_VCN33_WIFI) {
			regulator_set_voltage(reg_VCN33_WIFI, 3300000, 3300000);
			if (regulator_enable(reg_VCN33_WIFI))
				WMT_PLAT_ERR_FUNC("WMT do WIFI PMIC on fail!\n");
			else
				WMT_PLAT_INFO_FUNC("WMT do WIFI PMIC on !\n");
		}
		regmap_update_bits(pmic_regmap, 0x418, 0x1 << 14, 0x1 << 14);/*WIFI*/
		WMT_PLAT_INFO_FUNC("WMT do WIFI PMIC on\n");
	} else {
		/*do WIFI PMIC off */
		/*switch WIFI PALDO control from HW mode to SW mode:0x418[14]-->0x0 */
		regmap_update_bits(pmic_regmap, 0x418, 0x1 << 14, 0x0 << 14);/*WIFI*/
		if (reg_VCN33_WIFI)
			if (regulator_disable(reg_VCN33_WIFI))
				WMT_PLAT_ERR_FUNC("WMT do WIFI PMIC off fail!\n");
		WMT_PLAT_INFO_FUNC("WMT do WIFI PMIC off\n");
	}

	return 0;
}

#endif
INT32 mtk_wcn_consys_hw_vcn28_ctrl(UINT32 enable)
{
	if (enable) {
		/*in co-clock mode,need to turn on vcn28 when fm on */
		if (reg_VCN28) {
			regulator_set_voltage(reg_VCN28, 2800000, 2800000);
			if (regulator_enable(reg_VCN28))
				WMT_PLAT_ERR_FUNC("WMT do VCN28 PMIC on fail!\n");
		}
		WMT_PLAT_INFO_FUNC("turn on vcn28 for fm/gps usage in co-clock mode\n");
	} else {
		/*in co-clock mode,need to turn off vcn28 when fm off */
		if (reg_VCN28)
			if (regulator_disable(reg_VCN28))
				WMT_PLAT_ERR_FUNC("WMT do VCN28 PMIC off fail!\n");
		WMT_PLAT_INFO_FUNC("turn off vcn28 for fm/gps usage in co-clock mode\n");
	}
	return 0;
}

INT32 mtk_wcn_consys_hw_state_show(VOID)
{
	return 0;
}

INT32 mtk_wcn_consys_hw_restore(struct device *device)
{
	UINT32 addrPhy = 0;

	if (gConEmiPhyBase) {

#if CONSYS_EMI_MPU_SETTING
		/*set MPU for EMI share Memory */
		WMT_PLAT_INFO_FUNC("setting MPU for EMI share memory\n");

#if 0
	emi_mpu_set_region_protection(gConEmiPhyBase + SZ_1M/2,
		gConEmiPhyBase + SZ_1M,
		5,
		SET_ACCESS_PERMISSON(FORBIDDEN, NO_PROTECTION, FORBIDDEN, NO_PROTECTION));


#else
		WMT_PLAT_WARN_FUNC("not define platform config\n");
#endif

#endif
		/*consys to ap emi remapping register:10001310, cal remapping address */
		addrPhy = (gConEmiPhyBase & 0xFFF00000) >> 20;

		/*enable consys to ap emi remapping bit12 */
		addrPhy = addrPhy | 0x1000;

		CONSYS_REG_WRITE(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET,
				 CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET) | addrPhy);

		WMT_PLAT_INFO_FUNC("CONSYS_EMI_MAPPING dump in restore cb(0x%08x)\n",
				   CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET));

#if 1
		pEmibaseaddr = ioremap_nocache(gConEmiPhyBase + CONSYS_EMI_AP_PHY_OFFSET, CONSYS_EMI_MEM_SIZE);
#else
		pEmibaseaddr = ioremap_nocache(CONSYS_EMI_AP_PHY_BASE, CONSYS_EMI_MEM_SIZE);
#endif
		if (pEmibaseaddr) {
			WMT_PLAT_INFO_FUNC("EMI mapping OK(0x%p)\n", pEmibaseaddr);
			memset_io(pEmibaseaddr, 0, CONSYS_EMI_MEM_SIZE);
		} else {
			WMT_PLAT_ERR_FUNC("EMI mapping fail\n");
		}
	} else {
		WMT_PLAT_ERR_FUNC("consys emi memory address gConEmiPhyBase invalid\n");
	}

	return 0;
}

/*Reserved memory by device tree!*/
int reserve_memory_consys_fn(struct reserved_mem *rmem)
{
	WMT_PLAT_WARN_FUNC(" name: %s, base: 0x%llx, size: 0x%llx\n", rmem->name,
			   (unsigned long long)rmem->base, (unsigned long long)rmem->size);
	gConEmiPhyBase = rmem->base;
	return 0;
}

RESERVEDMEM_OF_DECLARE(reserve_memory_test, "mediatek,consys-reserve-memory", reserve_memory_consys_fn);


INT32 mtk_wcn_consys_hw_init(void)
{

	INT32 iRet = -1;
	UINT32 addrPhy = 0;
	INT32 i = 0;
	struct device_node *node = NULL;

	node = of_find_compatible_node(NULL, NULL, "mediatek,mt7623-consys");
	if (node) {
		/* registers base address */
		conn_reg.mcu_base = (SIZE_T) of_iomap(node, i);
		WMT_PLAT_DBG_FUNC("Get mcu register base(0x%zx)\n", conn_reg.mcu_base);
		i++;

		conn_reg.topckgen_base = (SIZE_T) of_iomap(node, i);
		WMT_PLAT_DBG_FUNC("Get topckgen register base(0x%zx)\n", conn_reg.topckgen_base);
		i++;
	} else {
		WMT_PLAT_ERR_FUNC("[%s] can't find CONSYS compatible node\n", __func__);
		return iRet;
	}
	if (gConEmiPhyBase) {
#if CONSYS_EMI_MPU_SETTING
		/*set MPU for EMI share Memory */
		WMT_PLAT_INFO_FUNC("setting MPU for EMI share memory\n");

#if 0
	emi_mpu_set_region_protection(gConEmiPhyBase + SZ_1M/2,
		gConEmiPhyBase + SZ_1M,
		5,
		SET_ACCESS_PERMISSON(FORBIDDEN, NO_PROTECTION, FORBIDDEN, NO_PROTECTION));
#else
		WMT_PLAT_WARN_FUNC("not define platform config\n");
#endif

#endif
		WMT_PLAT_DBG_FUNC("get consys start phy address(0x%zx)\n", (SIZE_T) gConEmiPhyBase);

		/*consys to ap emi remapping register:10001310, cal remapping address */
		addrPhy = (gConEmiPhyBase & 0xFFF00000) >> 20;

		/*enable consys to ap emi remapping bit12 */
		addrPhy = addrPhy | 0x1000;

		CONSYS_REG_WRITE(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET,
				 CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET) | addrPhy);

		WMT_PLAT_INFO_FUNC("CONSYS_EMI_MAPPING dump(0x%08x)\n",
				   CONSYS_REG_READ(conn_reg.topckgen_base + CONSYS_EMI_MAPPING_OFFSET));

#if 1
		pEmibaseaddr = ioremap_nocache(gConEmiPhyBase + CONSYS_EMI_AP_PHY_OFFSET, CONSYS_EMI_MEM_SIZE);
#else
		pEmibaseaddr = ioremap_nocache(CONSYS_EMI_AP_PHY_BASE, CONSYS_EMI_MEM_SIZE);
#endif
		/* pEmibaseaddr = ioremap_nocache(0x80090400,270*KBYTE); */
		if (pEmibaseaddr) {
			WMT_PLAT_INFO_FUNC("EMI mapping OK(0x%p)\n", pEmibaseaddr);
			memset_io(pEmibaseaddr, 0, CONSYS_EMI_MEM_SIZE);
			iRet = 0;
		} else {
			WMT_PLAT_ERR_FUNC("EMI mapping fail\n");
		}
	} else {
		WMT_PLAT_ERR_FUNC("consys emi memory address gConEmiPhyBase invalid\n");
	}
#ifdef CONFIG_MTK_HIBERNATION
	WMT_PLAT_INFO_FUNC("register connsys restore cb for complying with IPOH function\n");
	register_swsusp_restore_noirq_func(ID_M_CONNSYS, mtk_wcn_consys_hw_restore, NULL);
#endif
	iRet = platform_driver_register(&mtk_wmt_dev_drv);
	if (iRet)
		WMT_PLAT_ERR_FUNC("WMT platform driver registered failed(%d)\n", iRet);
	return iRet;
}

INT32 mtk_wcn_consys_hw_deinit(void)
{
	if (pEmibaseaddr) {
		iounmap(pEmibaseaddr);
		pEmibaseaddr = NULL;
	}
#ifdef CONFIG_MTK_HIBERNATION
	unregister_swsusp_restore_noirq_func(ID_M_CONNSYS);
#endif

	platform_driver_unregister(&mtk_wmt_dev_drv);
	return 0;
}

UINT8 *mtk_wcn_consys_emi_virt_addr_get(UINT32 ctrl_state_offset)
{
	UINT8 *p_virtual_addr = NULL;

	if (!pEmibaseaddr) {
		WMT_PLAT_ERR_FUNC("EMI base address is NULL\n");
		return NULL;
	}
	WMT_PLAT_DBG_FUNC("ctrl_state_offset(%08x)\n", ctrl_state_offset);
	p_virtual_addr = pEmibaseaddr + ctrl_state_offset;

	return p_virtual_addr;
}

UINT32 mtk_wcn_consys_soc_chipid(void)
{
	return PLATFORM_SOC_CHIP;
}

struct pinctrl *mtk_wcn_consys_get_pinctrl()
{
	return consys_pinctrl;
}
INT32 mtk_wcn_consys_set_dynamic_dump(PUINT32 str_buf)
{
	PUINT8 vir_addr = NULL;

	vir_addr = mtk_wcn_consys_emi_virt_addr_get(EXP_APMEM_CTRL_CHIP_DYNAMIC_DUMP);
	if (!vir_addr) {
		WMT_PLAT_ERR_FUNC("get vir address fail\n");
		return -2;
	}
	memcpy(vir_addr, str_buf, DYNAMIC_DUMP_GROUP_NUM*8);
	WMT_PLAT_INFO_FUNC("dynamic dump register value(0x%08x)\n", CONSYS_REG_READ(vir_addr));
	return 0;
}
