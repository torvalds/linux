//  ANALOGIX Company
//  ANX7150 Demo Firmware on SST
//  Version 0.50	2006/09/20

#ifndef _ANX7150__I2C_INTF_H
#define _ANX7150__I2C_INTF_H

#include "../hdmi_hal.h"

#define ANX7150_PORT0_ADDR	0x76//0x72 //  //ANX7150
#define ANX7150_PORT1_ADDR	0x7e//0x7A //  //ANX7150


__s32 ANX7150_i2c_Request(void);
__s32 ANX7150_i2c_Release(void);
__s32 ANX7150_i2c_write_p0_reg(__u8 offset, __u8 d);
__s32 ANX7150_i2c_write_p1_reg(__u8 offset, __u8 d);
__s32 ANX7150_i2c_read_p0_reg(__u8 offset, __u8 *d);
__s32 ANX7150_i2c_read_p1_reg(__u8 offset, __u8 *d);


void ANX7150_Resetn_Pin(__u32 value);
#endif

