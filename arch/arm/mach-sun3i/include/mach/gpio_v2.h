/*
 * arch/arm/mach-sun3i/include/mach/gpio_v2.h
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

#ifndef	  __GPIO_V2_H__
#define	  __GPIO_V2_H__

#define   EGPIO_FAIL             (-1)
#define   EGPIO_SUCCESS          (0)

typedef enum
{
	PIN_PULL_DEFAULT 	= 	0xFF,
	PIN_PULL_DISABLE 	=	0x00,
	PIN_PULL_UP			=	0x01,
	PIN_PULL_DOWN		=	0x02,
	PIN_PULL_RESERVED	=	0x03
}pin_pull_level_t;



typedef	enum
{
	PIN_MULTI_DRIVING_DEFAULT	=	0xFF,
	PIN_MULTI_DRIVING_0			=	0x00,
	PIN_MULTI_DRIVING_1			=	0x01,
	PIN_MULTI_DRIVING_2			=	0x02,
	PIN_MULTI_DRIVING_3			=	0x03
}pin_drive_level_t;

typedef enum
{
    PIN_DATA_LOW    ,
    PIN_DATA_HIGH   ,
    PIN_DATA_DEFAULT = 0XFF
}pin_data_t;


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
}
user_gpio_set_t;

/*
************************************************************************************************************
*
*                                             gpio_init
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ： GPIO管理初始化，传递一个GPIO基地址
*
*
************************************************************************************************************
*/
extern  int      gpio_init(void);
/*
************************************************************************************************************
*
*                                             gpio_exit
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：  GPIO管理退出，无操作，直接退出
*
*
************************************************************************************************************
*/
extern  int      gpio_exit(void);
/*
************************************************************************************************************
*
*                                             gpio_Request
*
*    函数名称：
*
*    参数列表： gpio_list 用户数据地址，用于传递用户的GPIO数据信息
*               group_count_max  用户数据的个数。这个数值应该大于或者等于用户实际的GPIO个数
*
*    返回值  ： 申请成功，返回一个句柄。否则返回0值
*
*    说明    ： GPIO请求。用户数据按照结构体传递，一个结构体保存一个GPIO信息。
*
*
************************************************************************************************************
*/
extern  unsigned gpio_request                 (user_gpio_set_t *gpio_list,                                                              unsigned group_count_max               );
extern  unsigned gpio_request_ex(char *main_name, const char *sub_name);  //设备申请GPIO函数扩展接口
/*
************************************************************************************************************
*
*                                             gpio_Release
*
*    函数名称：
*
*    参数列表：  p_handler  申请到的句柄
*                if_release_to_default_status   释放后的状态。可以释放后状态不变，可以变成全输入状态，可以变成申请前的状态
*
*    返回值  ：
*
*    说明    ： 用户不再使用GPIO，释放掉。
*
*
************************************************************************************************************
*/
extern  int      gpio_release                 (unsigned p_handler,                                                                      int if_release_to_default_status       );
/*
************************************************************************************************************
*
*                                             gpio_get_all_pin_status
*
*    函数名称：
*
*    参数列表： p_handler  申请到的句柄
*               gpio_status  保存用户数据的地址
*               gpio_count_max 保存用户数据的结构体的个数，这个数值应该大于或者等于实际的GPIO个数
*               if_get_from_hardware  希望获取到的GPIO信息来源于实际的寄存器状态，或者是
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_get_all_pin_status(unsigned p_handler, user_gpio_set_t *gpio_status, unsigned gpio_count_max, unsigned if_get_from_hardware);
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_get_one_pin_status      (unsigned p_handler, user_gpio_set_t *gpio_status,             const char *gpio_name,     unsigned if_get_from_hardware          );
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_set_one_pin_status      (unsigned p_handler, user_gpio_set_t *gpio_status,             const char *gpio_name,     unsigned if_set_to_current_input_status);
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_set_one_pin_io_status   (unsigned p_handler, unsigned         if_set_to_output_status, const char *gpio_name                                             );
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_set_one_pin_pull        (unsigned p_handler, unsigned         set_pull_status,         const char *gpio_name                                             );
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_set_one_pin_driver_level(unsigned p_handler, unsigned         set_driver_level,        const char *gpio_name                                             );
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_read_one_pin_value      (unsigned p_handler,                                           const char *gpio_name                                             );
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
extern  int      gpio_write_one_pin_value     (unsigned p_handler, unsigned         value_to_gpio,           const char *gpio_name                                             );



#endif	//__GPIO_V2_H__

