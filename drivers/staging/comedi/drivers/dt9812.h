#ifndef __DT9812_H__
#define __DT9812_H__

#define DT9812_DIAGS_BOARD_INFO_ADDR        0xFBFF
#define DT9812_MAX_WRITE_CMD_PIPE_SIZE      32
#define DT9812_MAX_READ_CMD_PIPE_SIZE       32

/*
 * See Silican Laboratories C8051F020/1/2/3 manual
 */
#define F020_SFR_P4                       0x84
#define F020_SFR_P1                       0x90
#define F020_SFR_P2                       0xa0
#define F020_SFR_P3                       0xb0
#define F020_SFR_AMX0CF                   0xba
#define F020_SFR_AMX0SL                   0xbb
#define F020_SFR_ADC0CF                   0xbc
#define F020_SFR_ADC0L                    0xbe
#define F020_SFR_ADC0H                    0xbf
#define F020_SFR_DAC0L                    0xd2
#define F020_SFR_DAC0H                    0xd3
#define F020_SFR_DAC0CN                   0xd4
#define F020_SFR_DAC1L                    0xd5
#define F020_SFR_DAC1H                    0xd6
#define F020_SFR_DAC1CN                   0xd7
#define F020_SFR_ADC0CN                   0xe8

#define F020_MASK_ADC0CF_AMP0GN0          0x01
#define F020_MASK_ADC0CF_AMP0GN1          0x02
#define F020_MASK_ADC0CF_AMP0GN2          0x04

#define F020_MASK_ADC0CN_AD0EN            0x80
#define F020_MASK_ADC0CN_AD0INT           0x20
#define F020_MASK_ADC0CN_AD0BUSY          0x10

#define F020_MASK_DACxCN_DACxEN           0x80

typedef enum {			//                      A/D      D/A     DI   DO    CT
	DT9812_DEVID_DT9812_10,	//    8        2       8    8     1   +/- 10V
	DT9812_DEVID_DT9812_2PT5,	//    8        2       8    8     1   0-2.44V
#if 0
	DT9812_DEVID_DT9813,	//    16       2       4    4     1   +/- 10V
	DT9812_DEVID_DT9814	//    24       2       0    0     1   +/- 10V
#endif
} dt9812_devid_t;

typedef enum {
	DT9812_GAIN_0PT25 = 1,
	DT9812_GAIN_0PT5 = 2,
	DT9812_GAIN_1 = 4,
	DT9812_GAIN_2 = 8,
	DT9812_GAIN_4 = 16,
	DT9812_GAIN_8 = 32,
	DT9812_GAIN_16 = 64,
} dt9812_gain_t;

typedef enum {
	DT9812_LEAST_USB_FIRMWARE_CMD_CODE = 0,
	DT9812_W_FLASH_DATA = 0,	// Write Flash memory
	DT9812_R_FLASH_DATA = 1,	// Read Flash memory (misc config info)

	// Register read/write commands for processor
	DT9812_R_SINGLE_BYTE_REG = 2,	// Read a single byte of USB memory
	DT9812_W_SINGLE_BYTE_REG = 3,	// Write a single byte of USB memory
	DT9812_R_MULTI_BYTE_REG = 4,	// Multiple Reads of USB memory
	DT9812_W_MULTI_BYTE_REG = 5,	// Multiple Writes of USB memory
	DT9812_RMW_SINGLE_BYTE_REG = 6,	// Read, (AND) with mask, OR value,
	// then write (single)
	DT9812_RMW_MULTI_BYTE_REG = 7,	// Read, (AND) with mask, OR value,
	// then write (multiple)

	// Register read/write commands for SMBus
	DT9812_R_SINGLE_BYTE_SMBUS = 8,	// Read a single byte of SMBus
	DT9812_W_SINGLE_BYTE_SMBUS = 9,	// Write a single byte of SMBus
	DT9812_R_MULTI_BYTE_SMBUS = 10,	// Multiple Reads of SMBus
	DT9812_W_MULTI_BYTE_SMBUS = 11,	// Multiple Writes of SMBus

	// Register read/write commands for a device
	DT9812_R_SINGLE_BYTE_DEV = 12,	// Read a single byte of a device
	DT9812_W_SINGLE_BYTE_DEV = 13,	// Write a single byte of a device
	DT9812_R_MULTI_BYTE_DEV = 14,	// Multiple Reads of a device
	DT9812_W_MULTI_BYTE_DEV = 15,	// Multiple Writes of a device

	DT9812_W_DAC_THRESHOLD = 16,	// Not sure if we'll need this

	DT9812_W_INT_ON_CHANGE_MASK = 17,	// Set interrupt on change mask

	DT9812_W_CGL = 18,	// Write (or Clear) the CGL for the ADC
	DT9812_R_MULTI_BYTE_USBMEM = 19,	// Multiple Reads of USB memory
	DT9812_W_MULTI_BYTE_USBMEM = 20,	// Multiple Writes to USB memory

	DT9812_START_SUBSYSTEM = 21,	// Issue a start command to a
	// given subsystem
	DT9812_STOP_SUBSYSTEM = 22,	// Issue a stop command to a
	// given subsystem

	DT9812_CALIBRATE_POT = 23,	//calibrate the board using CAL_POT_CMD
	DT9812_W_DAC_FIFO_SIZE = 24,	// set the DAC FIFO size
	DT9812_W_CGL_DAC = 25,	// Write (or Clear) the CGL for the DAC
	DT9812_R_SINGLE_VALUE_CMD = 26,	// Read a single value from a subsystem
	DT9812_W_SINGLE_VALUE_CMD = 27,	// Write a single value to a subsystem
	DT9812_MAX_USB_FIRMWARE_CMD_CODE	// Valid DT9812_USB_FIRMWARE_CMD_CODE's
		// will be less than this number
} dt9812_usb_firmware_cmd_t;

typedef struct {
	u16 numbytes;
	u16 address;
} dt9812_flash_data_t;

#define DT9812_MAX_NUM_MULTI_BYTE_RDS  \
    ((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / sizeof(u8))

typedef struct {
	u8 count;
	u8 address[DT9812_MAX_NUM_MULTI_BYTE_RDS];
} dt9812_read_multi_t;

typedef struct {
	u8 address;
	u8 value;
} dt9812_write_byte_t;

#define DT9812_MAX_NUM_MULTI_BYTE_WRTS  \
    ((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / sizeof(dt9812_write_byte_t))

typedef struct {
	u8 count;
	dt9812_write_byte_t write[DT9812_MAX_NUM_MULTI_BYTE_WRTS];
} dt9812_write_multi_t;

typedef struct {
	u8 address;
	u8 and_mask;
	u8 or_value;
} dt9812_rmw_byte_t;

#define DT9812_MAX_NUM_MULTI_BYTE_RMWS  \
    ((DT9812_MAX_WRITE_CMD_PIPE_SIZE - 4 - 1) / sizeof(dt9812_rmw_byte_t))

typedef struct {
	u8 count;
	dt9812_rmw_byte_t rmw[DT9812_MAX_NUM_MULTI_BYTE_RMWS];
} dt9812_rmw_multi_t;

typedef struct dt9812_usb_cmd {

	u32 cmd;
	union {
		dt9812_flash_data_t flash_data_info;
		dt9812_read_multi_t read_multi_info;
		dt9812_write_multi_t write_multi_info;
		dt9812_rmw_multi_t rmw_multi_info;
	} u;
#if 0
	WRITE_BYTE_INFO WriteByteInfo;
	READ_BYTE_INFO ReadByteInfo;
	WRITE_MULTI_INFO WriteMultiInfo;
	READ_MULTI_INFO ReadMultiInfo;
	RMW_BYTE_INFO RMWByteInfo;
	RMW_MULTI_INFO RMWMultiInfo;
	DAC_THRESHOLD_INFO DacThresholdInfo;
	INT_ON_CHANGE_MASK_INFO IntOnChangeMaskInfo;
	CGL_INFO CglInfo;
	SUBSYSTEM_INFO SubsystemInfo;
	CAL_POT_CMD CalPotCmd;
	WRITE_DEV_BYTE_INFO WriteDevByteInfo;
	READ_DEV_BYTE_INFO ReadDevByteInfo;
	WRITE_DEV_MULTI_INFO WriteDevMultiInfo;
	READ_DEV_MULTI_INFO ReadDevMultiInfo;
	READ_SINGLE_VALUE_INFO ReadSingleValueInfo;
	WRITE_SINGLE_VALUE_INFO WriteSingleValueInfo;
#endif
} dt9812_usb_cmd_t;

#endif
