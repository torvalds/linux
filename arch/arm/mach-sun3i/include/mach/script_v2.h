/*
 * arch/arm/mach-sun3i/include/mach/script_v2.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Jerry Wang <wangflord@allwinnertech.com>
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

#ifndef	  __SCRIPT_V2_H__
#define	  __SCRIPT_V2_H__

#define   DATA_TYPE_SINGLE_WORD  (1)
#define   DATA_TYPE_STRING       (2)
#define   DATA_TYPE_MULTI_WORD   (3)
#define   DATA_TYPE_GPIO_WORD    (4)

#define   SCRIPT_PARSER_OK       (0)
#define   SCRIPT_PARSER_EMPTY_BUFFER   	   (-1)
#define   SCRIPT_PARSER_KEYNAME_NULL   	   (-2)
#define   SCRIPT_PARSER_DATA_VALUE_NULL	   (-3)
#define   SCRIPT_PARSER_KEY_NOT_FIND       (-4)
#define   SCRIPT_PARSER_BUFFER_NOT_ENOUGH  (-5)

typedef struct
{
    char  gpio_name[32];
    int port;
    int port_num;
    int mul_sel;
    int pull;
    int drv_level;
    int data;
}
script_gpio_set_t;


extern  int script_parser_init                  (char *script_buf                                       );
extern  int script_parser_exit                  (void                                                   );
extern  int script_parser_fetch                 (char *main_name, char *sub_name, int value[], int count);
extern  int script_parser_subkey_count          (char *main_name                                        );
extern  int script_parser_mainkey_count         (void                                                   );
extern  int script_parser_mainkey_get_gpio_count(char *main_name                                        );
extern  int script_parser_mainkey_get_gpio_cfg  (char *main_name, void *gpio_cfg, int gpio_count        );

#endif	//__SCRIPT_V2_H__

