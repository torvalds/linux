#ifndef DDK750_HWI2C_H__
#define DDK750_HWI2C_H__

/* hwi2c functions */
int hwI2CInit(unsigned char busSpeedMode);
void hwI2CClose(void);

unsigned char hwI2CReadReg(unsigned char deviceAddress, unsigned char registerIndex);
int hwI2CWriteReg(unsigned char deviceAddress, unsigned char registerIndex, unsigned char data);
#endif
