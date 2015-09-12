#ifndef DDK750_HWI2C_H__
#define DDK750_HWI2C_H__

/* hwi2c functions */
int sm750_hw_i2c_init(unsigned char busSpeedMode);
void sm750_hw_i2c_close(void);

unsigned char hwI2CReadReg(unsigned char deviceAddress, unsigned char registerIndex);
int hwI2CWriteReg(unsigned char deviceAddress, unsigned char registerIndex, unsigned char data);
#endif
