/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_twi.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:22
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#include "standby_i.h"

static __twic_reg_t*   TWI_REG_BASE[3] = {
    (__twic_reg_t*)SW_VA_TWI0_IO_BASE,
    (__twic_reg_t*)SW_VA_TWI1_IO_BASE,
    (__twic_reg_t*)SW_VA_TWI2_IO_BASE
};

static __u32 TwiClkRegBak = 0;
static __u32 TwiCtlRegBak = 0;
static __twic_reg_t *twi_reg  = 0;



/*
*********************************************************************************************************
*                                   standby_twi_init
*
*Description: init twi transfer.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
__s32 standby_twi_init(int group)
{
	twi_reg  = TWI_REG_BASE[group];
	TwiClkRegBak = twi_reg->reg_clkr;
	TwiCtlRegBak = 0x80&twi_reg->reg_ctl;/* backup INT_EN;no need for BUS_EN(0xc0)  */
	//twi_reg->reg_clkr = (2<<3)|3; //100k
	twi_reg->reg_clkr = (5<<3)|0; //400k, M = 5, N=0;

	twi_reg->reg_reset |= 0x1;
	while(twi_reg->reg_reset&0x1);
	
	return 0;
}


/*
*********************************************************************************************************
*                                   standby_twi_exit
*
*Description: exit twi transfer.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
__s32 standby_twi_exit(void)
{
    /* softreset twi module  */
    twi_reg->reg_reset |= 0x1;
    /* delay */
    standby_mdelay(10);

    /* restore clock division */
    twi_reg->reg_clkr = TwiClkRegBak;
    /* restore INT_EN */
    twi_reg->reg_ctl |= TwiCtlRegBak;
    return 0;
}


/*
*********************************************************************************************************
*                                   _standby_twi_stop
*
*Description: stop current twi transfer.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
static int _standby_twi_stop(void)
{
    unsigned int   nop_read;
    unsigned int   timeout = TWI_CHECK_TIMEOUT;

    twi_reg->reg_ctl = (twi_reg->reg_ctl & 0xc0) | 0x10;/* set stop+clear int flag */

    nop_read = twi_reg->reg_ctl;/* apb时钟低时必须假读一次stop bit,下一个周期才生效 */
    nop_read = nop_read;
    // 1. stop bit is zero.
    while((twi_reg->reg_ctl & 0x10)&&(timeout--));
    if(timeout == 0)
    {
        return -1;
    }
    // 2. twi fsm is idle(0xf8).
    timeout = TWI_CHECK_TIMEOUT;
    while((0xf8 != twi_reg->reg_status)&&(timeout--));
    if(timeout == 0)
    {
        return -1;
    }
    // 3. twi scl & sda must high level.
    timeout = TWI_CHECK_TIMEOUT;
    while((0x3a != twi_reg->reg_lctl)&&(timeout--));
    if(timeout == 0)
    {
        return -1;
    }

    return 0;
}


/*
*********************************************************************************************************
*                                   twi_byte_rw
*
*Description: twi byte read and write.
*
*Arguments  : op        operation read or write;
*             saddr     slave address;
*             baddr     byte address;
*             data      pointer to the data to be read or write;
*
*Return     : result;
*               = EPDK_OK,      byte read or write successed;
*               = EPDK_FAIL,    btye read or write failed!
*********************************************************************************************************
*/
__s32 twi_byte_rw(enum twi_op_type_e op, __u8 saddr, __u8 baddr, __u8 *data)
{
    unsigned char state_tmp;
    unsigned int   timeout;
    int   ret = -1;

    twi_reg->reg_efr = 0;/* 标准读写必须置0 */

    state_tmp = twi_reg->reg_status;
    if(state_tmp != 0xf8)
    {
        goto stop_out;
    }

    /* control registser bitmap
         7      6       5     4       3       2    1    0
      INT_EN  BUS_EN  START  STOP  INT_FLAG  ACK  NOT  NOT
    */

    //1.Send Start
    twi_reg->reg_ctl |= 0x20;
    timeout = TWI_CHECK_TIMEOUT;
    while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
    if(timeout == 0)
    {
        goto stop_out;
    }
    state_tmp = twi_reg->reg_status;
    if(state_tmp != 0x08)
    {
        goto stop_out;
    }

    //2.Send Slave Address
    twi_reg->reg_data = (saddr<<1) | 0; /* slave address + write */
    twi_reg->reg_ctl &= 0xf7;/* clear int flag */
    timeout = TWI_CHECK_TIMEOUT;
    while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
    if(timeout == 0)
    {
        goto stop_out;
    }
    state_tmp = twi_reg->reg_status;
    if(state_tmp != 0x18)
    {
        goto stop_out;
    }

    //3.Send Byte Address
    twi_reg->reg_data = baddr;
    twi_reg->reg_ctl &= 0xf7;/* clear int flag */
    timeout = TWI_CHECK_TIMEOUT;
    while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
    if(timeout == 0)
    {
        goto stop_out;
    }
    state_tmp = twi_reg->reg_status;
    if(state_tmp != 0x28)
    {
        goto stop_out;
    }

    if(op == TWI_OP_WR)
    {
        //4.Send Data to be write
        twi_reg->reg_data = *data;
        twi_reg->reg_ctl &= 0xf7;/* clear int flag */
        timeout = TWI_CHECK_TIMEOUT;
        while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
        if(timeout == 0)
        {
            goto stop_out;
        }
        state_tmp = twi_reg->reg_status;
        if(state_tmp != 0x28)
        {
            goto stop_out;
        }
    }
    else
    {
        //4. Send restart for read
        twi_reg->reg_ctl = (twi_reg->reg_ctl & 0xc0) | 0x20;/* set start+clear int flag */
        timeout = TWI_CHECK_TIMEOUT;
        while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
        if(timeout == 0)
        {
            goto stop_out;
        }
        state_tmp = twi_reg->reg_status;
        if(state_tmp != 0x10)
        {
            goto stop_out;
        }

        //5.Send Slave Address
        twi_reg->reg_data = (saddr<<1) | 1;/* slave address+ read */
        twi_reg->reg_ctl &= 0xf7;/* clear int flag then 0x40 come in */
        timeout = TWI_CHECK_TIMEOUT;
        while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
        if(timeout == 0)
        {
            goto stop_out;
        }
        state_tmp = twi_reg->reg_status;
        if(state_tmp != 0x40)
        {
            goto stop_out;
        }

        //6.Get data
        twi_reg->reg_ctl &= 0xf7;/* clear int flag then data come in */
        timeout = TWI_CHECK_TIMEOUT;
        while((!(twi_reg->reg_ctl & 0x08))&&(timeout--));
        if(timeout == 0)
        {
            goto stop_out;
        }
        *data = twi_reg->reg_data;
        state_tmp = twi_reg->reg_status;
        if(state_tmp != 0x58)
        {
          goto stop_out;
        }
    }

    ret = 0;

stop_out:
    //WRITE: step 5; READ: step 7
    //Send Stop
    _standby_twi_stop();

    return ret;
}

