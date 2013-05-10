/*
 * arch/arm/plat-sunxi/include/plat/sys_config.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __SW_SYS_CONFIG_H
#define __SW_SYS_CONFIG_H

#include <plat/script.h>

#define   SCRIPT_PARSER_OK                   (0)
#define   SCRIPT_PARSER_EMPTY_BUFFER         (-1)
#define   SCRIPT_PARSER_KEYNAME_NULL         (-2)
#define   SCRIPT_PARSER_DATA_VALUE_NULL      (-3)
#define   SCRIPT_PARSER_KEY_NOT_FIND         (-4)
#define   SCRIPT_PARSER_BUFFER_NOT_ENOUGH    (-5)

typedef enum
{
	SCRIPT_PARSER_VALUE_TYPE_INVALID = 0,
	SCRIPT_PARSER_VALUE_TYPE_SINGLE_WORD,
	SCRIPT_PARSER_VALUE_TYPE_STRING,
	SCRIPT_PARSER_VALUE_TYPE_MULTI_WORD,
	SCRIPT_PARSER_VALUE_TYPE_GPIO_WORD
} script_parser_value_type_t;

typedef struct
{
	char  gpio_name[32];
	int port;
	int port_num;
	int mul_sel;
	int pull;
	int drv_level;
	int data;
} script_gpio_set_t;

typedef struct sunxi_script script_head_t;
typedef struct sunxi_script_section script_main_key_t;
typedef struct sunxi_script_property script_sub_key_t;

#define   EGPIO_FAIL             (-1)
#define   EGPIO_SUCCESS          (0)

typedef enum
{
	PIN_PULL_DEFAULT 	= 	0xFF,
	PIN_PULL_DISABLE 	=	0x00,
	PIN_PULL_UP			  =	0x01,
	PIN_PULL_DOWN	  	=	0x02,
	PIN_PULL_RESERVED	=	0x03
} pin_pull_level_t;

typedef	enum
{
	PIN_MULTI_DRIVING_DEFAULT	=	0xFF,
	PIN_MULTI_DRIVING_0			=	0x00,
	PIN_MULTI_DRIVING_1			=	0x01,
	PIN_MULTI_DRIVING_2			=	0x02,
	PIN_MULTI_DRIVING_3			=	0x03
} pin_drive_level_t;

typedef enum
{
	PIN_DATA_LOW,
	PIN_DATA_HIGH,
	PIN_DATA_DEFAULT = 0XFF
} pin_data_t;

#define	PIN_PHY_GROUP_A			0x00
#define	PIN_PHY_GROUP_B			0x01
#define	PIN_PHY_GROUP_C			0x02
#define	PIN_PHY_GROUP_D			0x03
#define	PIN_PHY_GROUP_E			0x04
#define	PIN_PHY_GROUP_F			0x05
#define	PIN_PHY_GROUP_G			0x06
#define	PIN_PHY_GROUP_H			0x07
#define	PIN_PHY_GROUP_I			0x08
#define	PIN_PHY_GROUP_J			0x09

typedef script_gpio_set_t user_gpio_set_t;

/* script operations */
extern int script_parser_fetch(char *main_name, char *sub_name, int value[], int count);
extern int script_parser_fetch_ex(char *main_name, char *sub_name, int value[],
               script_parser_value_type_t *type, int count);
extern int script_parser_subkey_count(char *main_name);
extern int script_parser_mainkey_count(void);
extern int script_parser_mainkey_get_gpio_count(char *main_name);
extern int script_parser_mainkey_get_gpio_cfg(char *main_name, void *gpio_cfg, int gpio_count);

/* gpio operations */
extern int gpio_init(void);
extern int gpio_exit(void);
extern unsigned sunxi_gpio_request_array(user_gpio_set_t *gpio_list,
					 unsigned group_count_max);
extern unsigned gpio_request_ex(char *main_name, const char *sub_name);
extern int gpio_release(unsigned p_handler, int if_release_to_default_status);
extern int gpio_get_all_pin_status(unsigned p_handler, user_gpio_set_t *gpio_status, unsigned gpio_count_max, unsigned if_get_from_hardware);
extern int gpio_get_one_pin_status(unsigned p_handler, user_gpio_set_t *gpio_status, const char *gpio_name, unsigned if_get_from_hardware);
extern int gpio_set_one_pin_status(unsigned p_handler, user_gpio_set_t *gpio_status, const char *gpio_name, unsigned if_set_to_current_input_status);
extern int gpio_set_one_pin_io_status(unsigned p_handler, unsigned if_set_to_output_status, const char *gpio_name);
extern int gpio_set_one_pin_pull(unsigned p_handler, unsigned set_pull_status, const char *gpio_name);
extern int gpio_set_one_pin_driver_level(unsigned p_handler, unsigned set_driver_level, const char *gpio_name);
extern int gpio_read_one_pin_value(unsigned p_handler, const char *gpio_name);
extern int gpio_write_one_pin_value(unsigned p_handler, unsigned value_to_gpio, const char *gpio_name);

#endif
