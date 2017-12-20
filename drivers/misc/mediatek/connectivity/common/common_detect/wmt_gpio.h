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

#ifndef _WMT_GPIO_H_
#define _WMT_GPIO_H_

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include "osal.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#define DEFAULT_PIN_ID (0xffffffff)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_GPIO_PIN_ID {
	GPIO_COMBO_LDO_EN_PIN = 0,
	GPIO_COMBO_PMUV28_EN_PIN,
	GPIO_COMBO_PMU_EN_PIN,
	GPIO_COMBO_RST_PIN,
	GPIO_COMBO_BGF_EINT_PIN,
	GPIO_WIFI_EINT_PIN,
	GPIO_COMBO_ALL_EINT_PIN,
	GPIO_COMBO_URXD_PIN,
	GPIO_COMBO_UTXD_PIN,
	GPIO_PCM_DAICLK_PIN,
	GPIO_PCM_DAIPCMIN_PIN,
	GPIO_PCM_DAIPCMOUT_PIN,
	GPIO_PCM_DAISYNC_PIN,
	GPIO_COMBO_I2S_CK_PIN,
	GPIO_COMBO_I2S_WS_PIN,
	GPIO_COMBO_I2S_DAT_PIN,
	GPIO_GPS_SYNC_PIN,
	GPIO_GPS_LNA_PIN,
	GPIO_PIN_ID_MAX
} ENUM_GPIO_PIN_ID, *P_ENUM_GPIO_PIN_ID;

typedef enum _ENUM_GPIO_STATE_ID {
	GPIO_PULL_DIS = 0,
	GPIO_PULL_DOWN,
	GPIO_PULL_UP,
	GPIO_OUT_LOW,
	GPIO_OUT_HIGH,
	GPIO_IN_DIS,
	GPIO_IN_EN,
	GPIO_IN_PULL_DIS,
	GPIO_IN_PULLDOWN,
	GPIO_IN_PULLUP,
	GPIO_STATE_MAX,
} ENUM_GPIO_STATE_ID, *P_ENUM_GPIO_STATE_ID;

typedef struct _GPIO_CTRL_STATE {
	INT32 gpio_num;
	struct pinctrl_state *gpio_state[GPIO_STATE_MAX];
} GPIO_CTRL_STATE, *P_GPIO_CTRL_STATE;

typedef struct _GPIO_CTRL_INFO {
	struct pinctrl *pinctrl_info;
	GPIO_CTRL_STATE gpio_ctrl_state[GPIO_PIN_ID_MAX];
} GPIO_CTRL_INFO, *P_GPIO_CTRL_INFO;

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
extern const PUINT8 gpio_state_name[GPIO_PIN_ID_MAX][GPIO_STATE_MAX];
extern const PUINT8 gpio_pin_name[GPIO_PIN_ID_MAX];
extern GPIO_CTRL_INFO gpio_ctrl_info;

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
INT32 wmt_gpio_init(struct platform_device *pdev);

INT32 wmt_gpio_deinit(VOID);

#endif
