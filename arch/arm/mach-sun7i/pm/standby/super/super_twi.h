/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_twi.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:22
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#ifndef __SUPER_TWI_H__
#define __SUPER_TWI_H__

#include "super_cfg.h"


typedef struct tag_twic_reg
{
    volatile unsigned int reg_saddr;
    volatile unsigned int reg_xsaddr;
    volatile unsigned int reg_data;
    volatile unsigned int reg_ctl;
    volatile unsigned int reg_status;
    volatile unsigned int reg_clkr;
    volatile unsigned int reg_reset;
    volatile unsigned int reg_efr;
    volatile unsigned int reg_lctl;

}__twic_reg_t;



enum twi_op_type_e{
    TWI_OP_RD,
    TWI_OP_WR,
};


extern __s32 mem_twi_init(int group);
extern __s32 mem_twi_exit(void);
extern __s32 twi_byte_rw(enum twi_op_type_e op, __u8 saddr, __u8 baddr, __u8 *data);
extern __s32 twi_byte_rw_nommu(enum twi_op_type_e op, __u8 saddr, __u8 baddr, __u8 *data);

extern void setup_twi_env(void);


#endif  /* __SUPER_TWI_H__ */

