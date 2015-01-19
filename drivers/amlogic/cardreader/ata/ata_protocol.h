#ifndef __ATA_PROTOCOL_H
#define __ATA_PROTOCOL_H

#include "ata_misc.h"
    
//#define CF_USE_PIO_DMA
//#define HD_USE_DMA
    
// ATA register defintions
#define ATA_DATA_REG						0x1F0	// RW
#define ATA_ERROR_REG						0x1F1	// R
#define ATA_REATURES_REG					0x1F1	// W
#define ATA_SECTOR_COUNT_REG				0x1F2	// RW
#define ATA_SECTOR_NUMBER_REG				0x1F3	// RW
#define ATA_CYLINDER_LOW_REG				0x1F4	// RW
#define ATA_CYLINDER_HIGH_REG				0x1F5	// RW
#define ATA_DEVICE_HEAD_REG					0x1F6	// RW
#define ATA_STATUS_REG						0x1F7	// R
#define ATA_COMMAND_REG						0x1F7	// W
#define ATA_ALT_STATUS_REG					0x3F6	// R
#define ATA_DEVICE_CONTROL_REG				0x3F6	// W
#define ATA_DRIVE_ADDRESS_REG				0x3F7	// W
    
// ATA register defintions for operations
#if 0
#define ata_read_reg(reg)					read_pio_8(reg)
#define ata_write_reg(reg, val)				write_pio_8(reg, val)
#define ata_read_data()						read_pio_16(ATA_DATA_REG)
#define ata_write_data(val)					write_pio_16(ATA_DATA_REG, val)
#else				/*  */
#define ata_read_reg(reg)					READ_ATA_REG(reg)
#define ata_write_reg(reg, val)				WRITE_ATA_REG(reg, val)
#define ata_read_data()						READ_ATA_REG(ATA_DATA_REG)
#define ata_write_data(val)					WRITE_ATA_REG(ATA_DATA_REG, val)
#endif				/*  */
    
// ATA/ATAPI-4 command defintions
#define ATA_CFA_ERASE_SECTORS				0xC0
#define ATA_CFA_REQUEST_EXT_ERR_CODE		0x03
#define ATA_CFA_TRANSLATE_SECTOR			0x87
#define ATA_CFA_WRITE_MULTIPLE_WO_ERASE		0xCD
#define ATA_CFA_WRITE_SECTORS_WO_ERASE		0x38
#define ATA_CHECK_POWER_MODE				0xE5
#define ATA_DEVICE_RESET					0x08
#define ATA_DOWNLOAD_MICROCODE				0x92
#define ATA_EXECUTE_DEVICE_DIAGNOSTIC		0x90
#define ATA_FLUSH_CACHE						0xE7
#define ATA_GET_MEDIA_STATUS				0xDA
#define ATA_IDENTIFY_DEVICE					0xEC
#define ATA_IDENTIFY_PACKET_DEVICE			0xA1
#define ATA_IDLE							0xE3
#define ATA_IDLE_IMMEDIATE					0xE1
#define ATA_INITIALIZE_DEVICE_PARAMETERS	0x91
#define ATA_MEDIA_EJECT						0xED
#define ATA_MEDIA_LOCK						0xDE
#define ATA_MEDIA_UNLOCK					0xDF
#define ATA_NOP								0x00
#define ATA_PACKET							0xA0
#define ATA_READ_BUFFER						0xE4
#define ATA_READ_DMA						0xC8
#define ATA_READ_DMA_EXT                    0x25
#define ATA_READ_DMA_QUEUED					0xC7
#define ATA_READ_MULTIPLE					0xC4
#define ATA_READ_NATIVE_MAX_ADDRESS			0xF8
#define ATA_READ_SECTORS					0x20
#define ATA_READ_VERIFY_SECTORS				0x40
#define ATA_SECURITY_DISABLE_PASSWORD		0xF6
#define ATA_SECURITY_ERASE_PREPARE			0xF3
#define ATA_SECURITY_ERASE_UNIT				0xF4
#define ATA_SECURITY_FREEZE_LOCK			0xF5
#define ATA_SECURITY_SET_PASSWORD			0xF1
#define ATA_SECURITY_UNLOCK					0xF2
#define ATA_SEEK							0x70
#define ATA_SERVICE							0xA2
#define ATA_SET_FEATURES					0xEF
#define ATA_SET_MAX_ADDRESS					0xF9
#define ATA_SET_MULTIPLE_MODE				0xC6
#define ATA_SLEEP							0xE6
#define ATA_SMART							0xB0
#define ATA_STANDBY							0xE2
#define ATA_STANDBY_IMMEDIATE				0xE0
#define ATA_WRITE_BUFFER					0xE8
#define ATA_WRITE_DMA						0xCA
#define ATA_WRITE_DMA_EXT                   0x35
#define ATA_WRITE_DMA_QUEUED				0xCC
#define ATA_WRITE_MULTIPLE					0xC5
#define ATA_WRITE_SECTORS					0x30
    
// ATA3 status bits defintions
#define ATA_STATUS_BSY						0x80	// Busy
#define ATA_STATUS_DRDY						0x40	// Device Ready
#define ATA_STATUS_DF						0x20	// Device Fault
#define ATA_STATUS_DSC						0x10	// Device Seek Complete
#define ATA_STATUS_DRQ						0x08	// Data Request
#define ATA_STATUS_CORR						0x04	// Corrected Data
#define ATA_STATUS_IDX						0x02	// Index
#define ATA_STATUS_ERR						0x01	// Error
    
// ATA4 device control bits defintions
#define ATA_DEV_CTL_nIEN					0x02
#define ATA_DEV_CTL_SRST					0x04
    
// ATA4 device head bits defintions
#define ATA_DRIVE0_MASK						0xA0	//0x00
#define ATA_DRIVE1_MASK						0xB0	//0x10
#define ATA_LBA_MODE						0xE0	//0x40
#define ATA_CHS_MODE						0xA0	//0x00
    
#define ATA_CMD_READY_TIMEOUT				450	// unit: ms
#define ATA_CMD_INIT_TIMEOUT				1000	// unit: ms
//#define ATA_CMD_COMPLETE_TIMEOUT                      3000            // unit: ms
//#define ATA_CMD_DATA_ACCESS_TIMEOUT                   5000            // unit: ms
#define ATA_CMD_ISSUE_DELAY					(4*100)	// unit: 100ns, 400ns is enough according to spec
    
#define ATA_DRQ_BLK_LENGTH_BYTE				512
#define ATA_DRQ_BLK_LENGTH_WORD				256
    
#define ATA_CMD_DEFAULT_RETRY	3
/*#define ATA_CMD_RETRY(action, retry_time, ata_err) \
	{\
		for(int ata_retry = 0; ata_retry < retry_time; ata_retry++)\
		{\
			ata_err = action;\
			if(ata_err)\
			{\
				ata_sw_reset(ata_dev);\
			}\
			else\
				break;\
		}\
	}*/ 
    
#pragma pack(1)
typedef struct _ATA_Identify_Information  {	// Word
	unsigned short general_configuration;	// 0
	unsigned short logical_cylinders;	// 1
	unsigned short Reserved2;	// 2
	unsigned short logical_heads;	// 3
	unsigned short retired4[2];	// 4~5
	unsigned short logical_sectors_per_track;	// 6
	unsigned short retired7[3];	// 7~9
	unsigned short serial_number[10];	// 10~19
	unsigned short retired20[2];	// 20~21
	unsigned short obsolete22;	// 22
	unsigned short firmware_revision[4];	// 23~26
	unsigned short model_number[20];	// 27~46
	unsigned short rw_multipile_support;	// 47
	unsigned short Reserved48;	// 48
	unsigned short capabilities49;	// 49
	unsigned short capabilities50;	// 50
	unsigned short pio_mode_number;	// 51
	unsigned short retired52;	// 52
	unsigned short field_validity;	// 53
	unsigned short current_logical_cylinders;	// 54
	unsigned short current_logical_heads;	// 55
	unsigned short current_logical_sectors_per_track;	// 56
	unsigned short current_capacity_in_sectors[2];	// 57~58
	unsigned short multiple_sector_setting;	// 59
	unsigned short total_addressable_sectors[2];	// 60~61
	unsigned short retired62;	// 62
	unsigned short multiword_dma_transfer;	// 63
	unsigned short advanced_pio_modes;	// 64
	unsigned short minimum_dma_cycle;	// 65
	unsigned short recommended_dma_cycle;	// 66
	unsigned short minimum_pio_cycle;	// 67
	unsigned short minimum_pio_cycle_with_iordy;	// 68
	unsigned short Reserved69[2];	// 69~70
	unsigned short Reserved71[4];	// 71~74
	unsigned short queue_depth;	// 75
	unsigned short Reserved76[4];	// 76~79
	unsigned short major_version;	// 80
	unsigned short minor_version;	// 81
	unsigned short command_sets_supported82;	// 82
	unsigned short command_sets_supported83;	// 83
	unsigned short command_sets_supported_ext;	// 84
	unsigned short command_sets_enabled85;	// 85
	unsigned short command_sets_enabled86;	// 86
	unsigned short command_sets_default;	// 87
	unsigned short ultra_dma_modes;	// 88
	unsigned short security_erase_time;	// 89
	unsigned short enhanced_security_erase_time;	// 90
	unsigned short current_power_management;	// 91
	unsigned short Reserved92[35];	// 92~126
	unsigned short removable_media_notification_support;	// 127
	unsigned short security_status;	// 128
	unsigned short vendor_specific129[31];	// 129~159
	unsigned short Reserved160[96];	// 160~255
} ATA_Identify_Information_t;

#pragma pack()
typedef enum _ATA_Error_Status 
    { ATA_NO_ERROR, ATA_ERROR_TIMEOUT, ATA_ERROR_HARDWARE_FAILURE,
	ATA_ERROR_DEVICE_TYPE, ATA_ERROR_NO_DEVICE 
} ATA_Error_Status_t;
typedef struct _ATA_Parameter  {
	unsigned char Features;	// Command specific
	unsigned char Sector_Count;	// Sector count
	unsigned char Sector_Number;	// LBA: LBA7_0, CHS: Sector Number
	unsigned char Cylinder_Low;	// LBA: LBA15_8, CHS: Cylinder Low
	unsigned char Cylinder_High;	// LBA: LBA23_16, CHS: Cylinder High
	unsigned char Device_Head;	// LBA: LBA27_24, CHS: Head Number
	unsigned char Command;	// Command code
} ATA_Parameter_t;
typedef struct _ATA_Device_Info  {
	unsigned long sector_nums;
	unsigned long sector_size;
	int device_existed;
	int device_inited;
	ATA_Identify_Information_t identify_info;
	char serial_number[21];
	char model_number[41];
} ATA_Device_Info_t;
typedef struct _ATA_Device  {
	unsigned char ata_buf[ATA_DRQ_BLK_LENGTH_BYTE];
	unsigned char current_dev;
	unsigned char current_addr_mode;
	int master_disabled;
	int slave_enabled;
	ATA_Device_Info_t device_info[2];
	ATA_Parameter_t ata_param;
} ATA_Device_t;
extern ATA_Device_t *ata_dev;
extern int ATA_MASTER_DISABLED;
extern int ATA_SLAVE_ENABLED;
int ata_init_device(ATA_Device_t * ata_dev);
int ata_sw_reset(ATA_Device_t * ata_dev);
int ata_sw_reset_dev(ATA_Device_t * ata_dev);
int ata_sleep_device(ATA_Device_t * ata_dev);
int ata_identify_device(ATA_Device_t * ata_dev);
int ata_identify_dev(ATA_Device_t * ata_dev, unsigned char *ata_identify_buf);
int ata_select_device(ATA_Device_t * ata_dev, int dev_no);
int ata_issue_pio_in_cmd(ATA_Device_t * ata_dev, unsigned long sector_cnt,
			  unsigned char *data_buf);
int ata_issue_pio_out_cmd(ATA_Device_t * ata_dev, unsigned long sector_cnt,
			   unsigned char *data_buf);
int ata_issue_no_data_cmd(ATA_Device_t * ata_dev);
int ata_read_data_dma(unsigned long lba, unsigned long byte_cnt,
		       unsigned char *data_buf);
int ata_issue_dma_out_cmd(ATA_Device_t * ata_dev, unsigned long sector_cnt,
			   unsigned char *data_buf);
int ata_write_data_dma(unsigned long lba, unsigned long byte_cnt,
			unsigned char *data_buf);
int ata_wait_status_bits(unsigned char bits_mask, unsigned char bits_value,
			   unsigned long timeout);
void ata_clear_ata_param(ATA_Device_t * ata_dev);
char *ata_error_to_string(int errcode);
int ata_read_data_pio(unsigned long lba, unsigned long byte_cnt,
		       unsigned char *data_buf);
int ata_write_data_pio(unsigned long lba, unsigned long byte_cnt,
			unsigned char *data_buf);
int ata_check_data_consistency(ATA_Device_t * ata_dev);
void ata_remove_device(ATA_Device_t * ata_dev, int dev_no);
int ata_check_cmd_validity(ATA_Device_t * ata_dev);
int ata_set_features(ATA_Device_t * ata_dev);

#endif				// __ATA_PROTOCOL_H
