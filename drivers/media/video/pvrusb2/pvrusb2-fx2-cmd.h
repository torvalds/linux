#ifndef _PVRUSB2_FX2_CMD_H_
#define _PVRUSB2_FX2_CMD_H_

#define FX2CMD_MEM_WRITE_DWORD  0x01
#define FX2CMD_MEM_READ_DWORD   0x02

#define FX2CMD_MEM_READ_64BYTES 0x28

#define FX2CMD_REG_WRITE        0x04
#define FX2CMD_REG_READ         0x05

#define FX2CMD_I2C_WRITE        0x08
#define FX2CMD_I2C_READ         0x09

#define FX2CMD_GET_USB_SPEED    0x0b

#define FX2CMD_STREAMING_ON     0x36
#define FX2CMD_STREAMING_OFF    0x37

#define FX2CMD_POWER_OFF        0xdc
#define FX2CMD_POWER_ON         0xde

#define FX2CMD_DEEP_RESET       0xdd

#define FX2CMD_GET_EEPROM_ADDR  0xeb
#define FX2CMD_GET_IR_CODE      0xec

#endif /* _PVRUSB2_FX2_CMD_H_ */
