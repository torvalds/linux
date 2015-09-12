#ifndef DDK750_HWI2C_H__
#define DDK750_HWI2C_H__

/* hwi2c functions */
int sm750_hw_i2c_init(unsigned char busSpeedMode);
void sm750_hw_i2c_close(void);

unsigned char sm750_hw_i2c_read_reg(unsigned char deviceAddress, unsigned char registerIndex);
int sm750_hw_i2c_write_reg(unsigned char deviceAddress, unsigned char registerIndex, unsigned char data);
#endif
