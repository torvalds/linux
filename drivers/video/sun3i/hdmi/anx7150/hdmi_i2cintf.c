#include "hdmi_i2cintf.h"
/*

static ES_FILE      * p_i2c = 0;;

__s32 ANX7150_i2c_Request(void)
{
	__u32 clock = 400000;

    p_i2c = eLIBs_fopen("b:\\BUS\\TWI1", "r");//第1路twi
    if(!p_i2c)
    {
        __err("open i2c device fail!\n");
        return -1;
    }
	eLIBs_fioctrl(p_i2c, TWI_SET_SCL_CLOCK, 0, (void *)clock);

	return 0;
}

__s32 ANX7150_i2c_Release(void)
{
	eLIBs_fclose(p_i2c);
	p_i2c = 0;

	return EPDK_OK;
}

__s32 ANX7150_i2c_write_p0_reg(__u8 offset, __u8 d)
{
	__twi_dev_para_ex_t stwi;
	__u8			 data;
	__u8            byte_addr[2];    // 从设备的寄存器地址，低字节存放低地址，高字节存放高地址
	__s32            ret;

	eLIBs_memset(&stwi, 0, sizeof(__twi_dev_para_ex_t));

	data = d;
	byte_addr[0] = offset;
	byte_addr[1] = 0;
	stwi.slave_addr 	  =  ANX7150_PORT0_ADDR/2;	   // 从设备中的地址
	stwi.slave_addr_flag	=  0   ;	 // 7 bit address
	stwi.byte_addr		= byte_addr; //从设备的寄存器地址
	stwi.byte_addr_width	=  1;		 // 可以是一个到多个数据
	stwi.byte_count	   =   1;		 //要发送或接收的数据大小
	stwi.data 			= &data; 	 // 数据buffer的地址

	ret = eLIBs_fioctrl(p_i2c, TWI_WRITE_SPEC_RS, 0, (void *)&stwi);
	if(ret != EPDK_OK)
	{
		__wrn("i2c 7150 write port0:%x,%x failed!\n",offset,d);

		return EPDK_FAIL;
	}
	else
	{
		//__wrn("i2c 7150 write port0:%x,%x ok!\n",offset,d);
	}

	return EPDK_OK;
}

__s32 ANX7150_i2c_write_p1_reg(__u8 offset, __u8 d)
{
	__twi_dev_para_ex_t stwi;
	__u8			 data;
	__u8			byte_addr[2];	 // 从设备的寄存器地址，低字节存放低地址，高字节存放高地址
	__s32			 ret;

	eLIBs_memset(&stwi, 0, sizeof(__twi_dev_para_ex_t));

	data = d;
	byte_addr[0] = offset;
	byte_addr[1] = 0;
	stwi.slave_addr 	  =  ANX7150_PORT1_ADDR/2;	   // 从设备中的地址
	stwi.slave_addr_flag	=  0   ;	 // 7 bit address
	stwi.byte_addr		= byte_addr; //从设备的寄存器地址
	stwi.byte_addr_width	=  1;		 // 可以是一个到多个数据
	stwi.byte_count    =   1;		 //要发送或接收的数据大小
	stwi.data			= &data;	 // 数据buffer的地址

	ret = eLIBs_fioctrl(p_i2c, TWI_WRITE_SPEC_RS, 0, (void *)&stwi);
	if(ret != EPDK_OK)
	{
		__wrn("i2c 7150 write port1:%x,%x failed!\n",offset,d);

		return EPDK_FAIL;
	}
	else
	{
		//__wrn("i2c 7150 write port1:%x,%x ok!\n",offset,d);
	}

	return EPDK_OK;
}


__s32 ANX7150_i2c_read_p0_reg(__u8 offset, __u8 *d)
{
	__twi_dev_para_ex_t stwi;
	__u8			byte_addr[2];	 // 从设备的寄存器地址，低字节存放低地址，高字节存放高地址
	__s32			 ret;

	eLIBs_memset(&stwi, 0, sizeof(__twi_dev_para_ex_t));

	byte_addr[0] = offset;
	byte_addr[1] = 0;
	stwi.slave_addr 	  =  ANX7150_PORT0_ADDR/2;	   // 从设备中的地址
	stwi.slave_addr_flag	=  0   ;	 // 7 bit address
	stwi.byte_addr		= byte_addr; //从设备的寄存器地址
	stwi.byte_addr_width	=  1;		 // 可以是一个到多个数据
	stwi.byte_count    =   1;		 //要发送或接收的数据大小
	stwi.data			= d;	 // 数据buffer的地址

	ret = eLIBs_fioctrl(p_i2c, TWI_READ_SPEC_RS, 0, (void *)&stwi);
	if(ret != EPDK_OK)
	{
		__wrn("i2c 7150 read port0:%x failed!\n",offset);

		return EPDK_FAIL;
	}
	else
	{
		//__wrn("i2c 7150 read port0:%x,%x ok!\n",offset,*d);
	}

	return EPDK_OK;
}


__s32 ANX7150_i2c_read_p1_reg(__u8 offset, __u8 *d)
{
	__twi_dev_para_ex_t stwi;
	__u8			byte_addr[2];	 // 从设备的寄存器地址，低字节存放低地址，高字节存放高地址
	__s32			 ret;

	eLIBs_memset(&stwi, 0, sizeof(__twi_dev_para_ex_t));

	byte_addr[0] = offset;
	byte_addr[1] = 0;
	stwi.slave_addr 	  =  ANX7150_PORT1_ADDR/2;	   // 从设备中的地址
	stwi.slave_addr_flag	=  0   ;	 // 7 bit address
	stwi.byte_addr		= byte_addr; //从设备的寄存器地址
	stwi.byte_addr_width	=  1;		 // 可以是一个到多个数据
	stwi.byte_count    =   1;		 //要发送或接收的数据大小
	stwi.data			= d;	 // 数据buffer的地址

	ret = eLIBs_fioctrl(p_i2c, TWI_READ_SPEC_RS, 0, (void *)&stwi);
	if(ret != EPDK_OK)
	{
		__wrn("i2c 7150 read port1:%x failed!\n", offset);

		return EPDK_FAIL;
	}
	else
	{
		//__wrn("i2c 7150 read port1:%x,%x ok!\n",offset,*d);
	}

	return EPDK_OK;
}

*/

