/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <mtk_rtc.h>

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-DETECT]"

#include "wmt_detect.h"
#include "wmt_gpio.h"

#define INVALID_PIN_ID (0xFFFFFFFF)

/*copied form WMT module*/
static int wmt_detect_dump_pin_conf(void)
{
	WMT_DETECT_DBG_FUNC("[WMT-DETECT]=>dump wmt pin configuration start<=\n");

	WMT_DETECT_INFO_FUNC("LDO(GPIO%d), PMU(GPIO%d), PMUV28(GPIO%d)\n",
			gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_LDO_EN_PIN].gpio_num,
			gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num,
			gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMUV28_EN_PIN].gpio_num);

	WMT_DETECT_INFO_FUNC("RST(GPIO%d), BGF_EINT(GPIO%d), BGF_EINT_NUM(%d)\n",
			gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num,
			gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_BGF_EINT_PIN].gpio_num,
			gpio_to_irq(gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_BGF_EINT_PIN].gpio_num));

	WMT_DETECT_INFO_FUNC("WIFI_EINT(GPIO%d), WIFI_EINT_NUM(%d)\n",
			gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_num,
			gpio_to_irq(gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_num));

	WMT_DETECT_DBG_FUNC("[WMT-PLAT]=>dump wmt pin configuration ends<=\n");

	return 0;
}

int _wmt_detect_output_low(unsigned int id)
{
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[id].gpio_num) {
		gpio_direction_output(gpio_ctrl_info.gpio_ctrl_state[id].gpio_num, 0);
		WMT_DETECT_DBG_FUNC("WMT-DETECT: set GPIO%d to output %d\n",
				gpio_ctrl_info.gpio_ctrl_state[id].gpio_num,
				gpio_get_value(gpio_ctrl_info.gpio_ctrl_state[id].gpio_num));
	}

	return 0;
}

int _wmt_detect_output_high(unsigned int id)
{
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[id].gpio_num) {
		gpio_direction_output(gpio_ctrl_info.gpio_ctrl_state[id].gpio_num, 1);
		WMT_DETECT_DBG_FUNC("WMT-DETECT: set GPIO%d to output %d\n",
				gpio_ctrl_info.gpio_ctrl_state[id].gpio_num,
				gpio_get_value(gpio_ctrl_info.gpio_ctrl_state[id].gpio_num));
	}

	return 0;
}

int _wmt_detect_read_gpio_input(unsigned int id)
{
	int retval = 0;

	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[id].gpio_num) {
		retval = gpio_get_value(gpio_ctrl_info.gpio_ctrl_state[id].gpio_num);
		WMT_DETECT_DBG_FUNC("WMT-DETECT: get GPIO%d val%d\n",
				gpio_ctrl_info.gpio_ctrl_state[id].gpio_num, retval);
	}

	return retval;
}

/*This power on sequence must support all combo chip's basic power on sequence
 * 1. LDO control is a must, if external LDO exist
 * 2. PMU control is a must
 * 3. RST control is a must
 * 4. WIFI_EINT pin control is a must, used for GPIO mode for EINT status checkup
 * 5. RTC32k clock control is a must
 * */
static int wmt_detect_chip_pwr_on(void)
{
	int retval = -1;
	/*setting validiation check*/
	if ((INVALID_PIN_ID == gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num) ||
		(INVALID_PIN_ID == gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num) ||
		(INVALID_PIN_ID == gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_num)) {
		WMT_DETECT_ERR_FUNC("WMT-DETECT: either PMU(%d) or RST(%d) or WIFI_EINT(%d) is not set\n",
				gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num,
				gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num,
				gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_num);

		return retval;
	}
	/*set LDO/PMU/RST to output 0, no pull*/
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_LDO_EN_PIN].gpio_num)
		_wmt_detect_output_low(GPIO_COMBO_LDO_EN_PIN);
	if (gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_state[GPIO_PULL_DIS]) {
		pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
				gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_state[GPIO_PULL_DIS]);
		WMT_DETECT_INFO_FUNC("wmt_gpio:set GPIO_COMBO_PMU_EN_PIN to GPIO_PULL_DIS done!\n");
	} else
		WMT_DETECT_ERR_FUNC("wmt_gpio:set GPIO_COMBO_PMU_EN_PIN to GPIO_PULL_DIS fail, is NULL!\n");
	_wmt_detect_output_low(GPIO_COMBO_PMU_EN_PIN);
	if (gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_state[GPIO_PULL_DIS]) {
		pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
				gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_state[GPIO_PULL_DIS]);
		WMT_DETECT_INFO_FUNC("wmt_gpio:set GPIO_COMBO_RST_PIN to GPIO_PULL_DIS done!\n");
	} else
		WMT_DETECT_ERR_FUNC("wmt_gpio:set GPIO_COMBO_RST_PIN to GPIO_PULL_DIS fail, is NULL!\n");
	_wmt_detect_output_low(GPIO_COMBO_RST_PIN);

#if 0
	_wmt_detect_output_high(GPIO_WIFI_EINT_PIN);
#endif

	/*pull high LDO*/
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_LDO_EN_PIN].gpio_num)
		_wmt_detect_output_high(GPIO_COMBO_LDO_EN_PIN);
	/*sleep for LDO stable time*/
	msleep(MAX_LDO_STABLE_TIME);

	/*export RTC clock, sleep for RTC stable time*/
	rtc_gpio_enable_32k(RTC_GPIO_USER_GPS);
	msleep(MAX_RTC_STABLE_TIME);
	/*PMU output low, RST output low, to make chip power off completely*/
	/*always done*/
	/*sleep for power off stable time*/
	msleep(MAX_OFF_STABLE_TIME);
	/*PMU output high, and sleep for reset stable time*/
	_wmt_detect_output_high(GPIO_COMBO_PMU_EN_PIN);
#ifdef CONFIG_MTK_COMBO_COMM_NPWR
	if ((gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_I2S_DAT_PIN].gpio_num != INVALID_PIN_ID) &&
		(gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAISYNC_PIN].gpio_num != INVALID_PIN_ID)) {
		msleep(20);
		_wmt_detect_output_high(GPIO_PCM_DAISYNC_PIN);

		msleep(20);
		_wmt_detect_output_high(GPIO_COMBO_I2S_DAT_PIN);

		msleep(20);
		_wmt_detect_output_low(GPIO_COMBO_I2S_DAT_PIN);

		msleep(20);
		_wmt_detect_output_low(GPIO_PCM_DAISYNC_PIN);

		msleep(20);
	}
#endif
	msleep(MAX_RST_STABLE_TIME);
	/*RST output high, and sleep for power on stable time */
	_wmt_detect_output_high(GPIO_COMBO_RST_PIN);
	msleep(MAX_ON_STABLE_TIME);

	retval = 0;
	return retval;
}

static int wmt_detect_chip_pwr_off(void)
{

	/*set RST pin to input low status*/
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_LDO_EN_PIN].gpio_num)
		_wmt_detect_output_low(GPIO_COMBO_LDO_EN_PIN);
	/*set RST pin to input low status*/
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num)
		_wmt_detect_output_low(GPIO_COMBO_RST_PIN);
	/*set PMU pin to input low status*/
	if (INVALID_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num)
		_wmt_detect_output_low(GPIO_COMBO_PMU_EN_PIN);
	return 0;
}

int wmt_detect_read_ext_cmb_status(void)
{
	int retval = 0;
	/*read WIFI_EINT pin status*/
	if (INVALID_PIN_ID == gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_num) {
		retval = 0;
		WMT_DETECT_ERR_FUNC("WMT-DETECT: no WIFI_EINT pin set\n");
	} else {
		retval = _wmt_detect_read_gpio_input(GPIO_WIFI_EINT_PIN);
		WMT_DETECT_ERR_FUNC("WMT-DETECT: WIFI_EINT input status:%d\n", retval);
	}
	return retval;
}

int wmt_detect_chip_pwr_ctrl(int on)
{
	int retval = -1;

	if (0 == on) {
		/*power off combo chip */
		retval = wmt_detect_chip_pwr_off();
	} else {
		wmt_detect_dump_pin_conf();
		/*power on combo chip */
		retval = wmt_detect_chip_pwr_on();
	}
	return retval;
}

int wmt_detect_sdio_pwr_ctrl(int on)
{
	int retval = -1;
#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
	if (0 == on) {
		/*power off SDIO slot */
		retval = board_sdio_ctrl(1, 0);
	} else {
		/*power on SDIO slot */
		retval = board_sdio_ctrl(1, 1);
	}
#else
	WMT_DETECT_WARN_FUNC("WMT-DETECT: MTK_WCN_COMBO_CHIP_SUPPORT is not set\n");
#endif
	return retval;
}
