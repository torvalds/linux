/*
 * arch/arm/mach-sun3i/pm/standby/sun3i_sram_twi.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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


 #include <mach/platform.h>
 #include <sun3i_standby.h>

static __twic_reg_t*   TWI_REG_BASE[3] = {
		(__twic_reg_t*)SW_VA_TWI0_IO_BASE,
		(__twic_reg_t*)SW_VA_TWI1_IO_BASE,
		(__twic_reg_t*)SW_VA_TWI2_IO_BASE};
static unsigned int      TwiClkRegBak = 0;
static unsigned int      TwiCtlRegBak = 0;/* mainly for interrup enable bit */
static __twic_reg_t     *twi_reg  = 0;

/*
*********************************************************************************************************
*                                   TWI TRANSFER INIT
*
*Description: init twi transfer.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
int standby_twi_init(unsigned int group)
{
    twi_reg  = TWI_REG_BASE[group];
    TwiClkRegBak = twi_reg->reg_clkr;
    TwiCtlRegBak = 0x80&twi_reg->reg_ctl;/* backup INT_EN;no need for BUS_EN(0xc0)  */
    twi_reg->reg_clkr = (2<<3)|1;
    twi_reg->reg_reset |= 0x1;

    return EPDK_OK;
}


/*
*********************************************************************************************************
*                                   TWI TRANSFER EXIT
*
*Description: exit twi transfer.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
int standby_twi_exit(void)
{
    unsigned int i = 0xff;
    /* softreset twi module  */
    twi_reg->reg_reset |= 0x1;
    /* delay */
    for(; i > 0; i--);

    /* restore clock division */
    twi_reg->reg_clkr = TwiClkRegBak;
    /* restore INT_EN */
    twi_reg->reg_ctl |= TwiCtlRegBak;
    return EPDK_OK;
}


/*
*********************************************************************************************************
*                                   STOP CURRENT TWI TRANSFER
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
    unsigned int   timeout = 0xff;

    twi_reg->reg_ctl = (twi_reg->reg_ctl & 0xc0) | 0x10;/* set stop+clear int flag */

    nop_read = twi_reg->reg_ctl;/* apb时钟低时必须假读一次stop bit,下一个周期才生效 */
    nop_read = nop_read;
    // 1. stop bit is zero.
    while((twi_reg->reg_ctl & 0x10)&&(timeout--));
    if(timeout == 0)
    {
        return EPDK_FAIL;
    }
    // 2. twi fsm is idle(0xf8).
    timeout = 0xff;
    while((0xf8 != twi_reg->reg_status)&&(timeout--));
    if(timeout == 0)
    {
        return EPDK_FAIL;
    }
    // 3. twi scl & sda must high level.
    timeout = 0xff;
    while((0x3a != twi_reg->reg_lctl)&&(timeout--));
    if(timeout == 0)
    {
        return EPDK_FAIL;
    }

    return EPDK_OK;
}


/*
*********************************************************************************************************
*                                   TWI BYTE READ AND WRITE
*
*Description: twi byte read and write.
*
*Arguments  : op_type   operation read or write;
*             saddr     slave address;
*             baddr     byte address;
*             data      pointer to the data to be read or write;
*
*Return     : result;
*               = EPDK_OK,      byte read or write successed;
*               = EPDK_FAIL,    btye read or write failed!
*********************************************************************************************************
*/
int twi_byte_rw(int op_type, unsigned char saddr, unsigned char baddr, unsigned char *data)
{
    unsigned char state_tmp;
    unsigned int   timeout;
    int   ret = EPDK_FAIL;

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
    timeout = 0xff;
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
    timeout = 0xff;
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
    timeout = 0xff;
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

    if(op_type == TWI_OP_WR)
    {
        //4.Send Data to be write
        twi_reg->reg_data = *data;
        twi_reg->reg_ctl &= 0xf7;/* clear int flag */
        timeout = 0xff;
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
        timeout = 0xff;
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
        timeout = 0xff;
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
        timeout = 0xff;
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

    ret = EPDK_OK;

stop_out:
    //WRITE: step 5; READ: step 7
    //Send Stop
    _standby_twi_stop();

    return ret;
}

