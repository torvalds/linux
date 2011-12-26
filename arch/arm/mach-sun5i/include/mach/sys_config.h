/*
 * arch/arch/mach-sun5i/include/mach/sys_config.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * sys_config utils (porting from 2.6.36)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_SYS_CONFIG_H
#define __SW_SYS_CONFIG_H


#define   SYS_CONFIG_MEMBASE                 (PLAT_PHYS_OFFSET + SZ_32M + SZ_16M)
#define   SYS_CONFIG_MEMSIZE                 (SZ_64K)
#define   SCRIPT_PARSER_OK                   (0)
#define   SCRIPT_PARSER_EMPTY_BUFFER         (-1)
#define   SCRIPT_PARSER_KEYNAME_NULL         (-2)
#define   SCRIPT_PARSER_DATA_VALUE_NULL      (-3)
#define   SCRIPT_PARSER_KEY_NOT_FIND         (-4)
#define   SCRIPT_PARSER_BUFFER_NOT_ENOUGH    (-5)

typedef enum
{
	SCIRPT_PARSER_VALUE_TYPE_INVALID = 0,
	SCIRPT_PARSER_VALUE_TYPE_SINGLE_WORD,
	SCIRPT_PARSER_VALUE_TYPE_STRING,
	SCIRPT_PARSER_VALUE_TYPE_MULTI_WORD,
	SCIRPT_PARSER_VALUE_TYPE_GPIO_WORD
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

typedef struct
{
	int  main_key_count;
	int  version[3];
} script_head_t;

typedef struct
{
	char main_name[32];
	int  lenth;
	int  offset;
} script_main_key_t;

typedef struct
{
	char sub_name[32];
	int  offset;
	int  pattern;
} script_sub_key_t;


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

typedef struct
{
    char  gpio_name[32];
    int port;
    int port_num;
    int mul_sel;
    int pull;
    int drv_level;
    int data;
} user_gpio_set_t;

/* functions for early boot */
extern int sw_cfg_get_int(const char *script_buf, const char *main_key, const char *sub_key);
extern char *sw_cfg_get_str(const char *script_buf, const char *main_key, const char *sub_key, char *buf);

/* script operations */
extern int script_parser_init(char *script_buf);
extern int script_parser_exit(void);
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
extern unsigned gpio_request(user_gpio_set_t *gpio_list, unsigned group_count_max);
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
