#ifndef __PLATFORM_INTERFACE_H__
#define __PLATFORM_INTERFACE_H__

// for timer function
void TIMER_Init(void);
void TIMER_Set(uint8_t index, uint16_t value);
uint8_t TIMER_Expired(uint8_t index);

// for i2c function
uint8_t I2C_ReadByte(uint8_t deviceID, uint8_t offset);
void I2C_WriteByte(uint8_t deviceID, uint8_t offset, uint8_t value);
uint8_t I2C_ReadBlock(uint8_t deviceID, uint8_t offset, uint8_t *buffer, uint16_t length);
uint8_t I2C_WriteBlock(uint8_t deviceID, uint8_t offset, uint8_t *buffer, uint16_t length);


#endif
