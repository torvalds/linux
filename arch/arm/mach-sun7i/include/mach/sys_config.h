/*
 * arch/arch/mach-sunxi/include/mach/sys_config.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin <Kevin@allwinnertech.com>
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

#include "gpio.h"

/*
 * define types of script item
 * @SCIRPT_ITEM_VALUE_TYPE_INVALID:  invalid item type
 * @SCIRPT_ITEM_VALUE_TYPE_INT: integer item type
 * @SCIRPT_ITEM_VALUE_TYPE_STR: strint item type
 * @SCIRPT_ITEM_VALUE_TYPE_PIO: gpio item type
 */
typedef enum {
    SCIRPT_ITEM_VALUE_TYPE_INVALID = 0,
    SCIRPT_ITEM_VALUE_TYPE_INT,
    SCIRPT_ITEM_VALUE_TYPE_STR,
    SCIRPT_ITEM_VALUE_TYPE_PIO,
} script_item_value_type_e;


/*
 * define data structure script item
 * @val: integer value for integer type item
 * @str: string pointer for sting type item
 * @gpio: gpio config for gpio type item
 */
typedef union {
    int                 val;
    char                *str;
    struct gpio_config  gpio;
} script_item_u;

/*
 * script_get_item
 *      get an item from script based on main_key & sub_key
 * @main_key    main key value in script which is marked by '[]'
 * @sub_key     sub key value in script which is left of '='
 * @item        item pointer for return value
 * @return      type of the item
 */
script_item_value_type_e script_get_item(char *main_key, char *sub_key, script_item_u *item);


/*
 * script_get_pio_list
 *      get gpio list from script baseed on main_key
 * @main_key    main key value in script which is marked by '[]'
 * @list        list pointer for return gpio list
 * @return      count of the gpios
 */
int script_get_pio_list(char *main_key, script_item_u **list);

/*
 * script_dump_mainkey
 *      dump main_key info
 * @main_key    main key value in script which is marked by '[]',
 *              if NULL, dump all main key info in script
 * @return      0
 */
int script_dump_mainkey(char *main_key);

#endif
