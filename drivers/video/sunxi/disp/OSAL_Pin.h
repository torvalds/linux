/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

#ifndef __OSAL_PIN_H__
#define __OSAL_PIN_H__

__hdle OSAL_GPIO_Request(user_gpio_set_t *gpio_list, __u32 group_count_max);

__hdle OSAL_GPIO_Request_Ex(char *main_name, const char *sub_name);

__s32 OSAL_GPIO_Release(__hdle p_handler, __s32 if_release_to_default_status);

__s32 OSAL_GPIO_DevGetAllPins_Status(unsigned p_handler,
				     user_gpio_set_t *gpio_status,
				     unsigned gpio_count_max,
				     unsigned if_get_from_hardware);

__s32 OSAL_GPIO_DevGetONEPins_Status(unsigned p_handler,
				     user_gpio_set_t *gpio_status,
				     const char *gpio_name,
				     unsigned if_get_from_hardware);

__s32 OSAL_GPIO_DevSetONEPin_Status(u32 p_handler,
				    user_gpio_set_t *gpio_status,
				    const char *gpio_name,
				    __u32 if_set_to_current_input_status);

__s32 OSAL_GPIO_DevSetONEPIN_IO_STATUS(u32 p_handler,
				       __u32 if_set_to_output_status,
				       const char *gpio_name);

__s32 OSAL_GPIO_DevSetONEPIN_PULL_STATUS(u32 p_handler, __u32 set_pull_status,
					 const char *gpio_name);

__s32 OSAL_GPIO_DevREAD_ONEPIN_DATA(u32 p_handler, const char *gpio_name);

__s32 OSAL_GPIO_DevWRITE_ONEPIN_DATA(u32 p_handler, __u32 value_to_gpio,
				     const char *gpio_name);

#endif /* __OSAL_PIN_H__ */
