#ifndef __FLEXCOP_USB_H_INCLUDED__
#define __FLEXCOP_USB_H_INCLUDED__

#include <linux/usb.h>

/* transfer parameters */
#define B2C2_USB_FRAMES_PER_ISO		4
#define B2C2_USB_NUM_ISO_URB		4

#define B2C2_USB_CTRL_PIPE_IN		usb_rcvctrlpipe(fc_usb->udev,0)
#define B2C2_USB_CTRL_PIPE_OUT		usb_sndctrlpipe(fc_usb->udev,0)
#define B2C2_USB_DATA_PIPE			usb_rcvisocpipe(fc_usb->udev,0x81)

struct flexcop_usb {
	struct usb_device *udev;
	struct usb_interface *uintf;

	u8 *iso_buffer;
	int buffer_size;
	dma_addr_t dma_addr;
	struct urb *iso_urb[B2C2_USB_NUM_ISO_URB];

	struct flexcop_device *fc_dev;

	u8 tmp_buffer[1023+190];
	int tmp_buffer_length;
};

#if 0
/* request types TODO What is its use?*/
typedef enum {

/* something is wrong with this part
	RTYPE_READ_DW         = (1 << 6),
	RTYPE_WRITE_DW_1      = (3 << 6),
	RTYPE_READ_V8_MEMORY  = (6 << 6),
	RTYPE_WRITE_V8_MEMORY = (7 << 6),
	RTYPE_WRITE_V8_FLASH  = (8 << 6),
	RTYPE_GENERIC         = (9 << 6),
*/
} flexcop_usb_request_type_t;
#endif

/* request */
typedef enum {
	B2C2_USB_WRITE_V8_MEM = 0x04,
	B2C2_USB_READ_V8_MEM  = 0x05,
	B2C2_USB_READ_REG     = 0x08,
	B2C2_USB_WRITE_REG    = 0x0A,
/*	B2C2_USB_WRITEREGLO   = 0x0A, */
	B2C2_USB_WRITEREGHI   = 0x0B,
	B2C2_USB_FLASH_BLOCK  = 0x10,
	B2C2_USB_I2C_REQUEST  = 0x11,
	B2C2_USB_UTILITY      = 0x12,
} flexcop_usb_request_t;

/* function definition for I2C_REQUEST */
typedef enum {
	USB_FUNC_I2C_WRITE       = 0x01,
	USB_FUNC_I2C_MULTIWRITE  = 0x02,
	USB_FUNC_I2C_READ        = 0x03,
	USB_FUNC_I2C_REPEATWRITE = 0x04,
	USB_FUNC_GET_DESCRIPTOR  = 0x05,
	USB_FUNC_I2C_REPEATREAD  = 0x06,
/* DKT 020208 - add this to support special case of DiSEqC */
	USB_FUNC_I2C_CHECKWRITE  = 0x07,
	USB_FUNC_I2C_CHECKRESULT = 0x08,
} flexcop_usb_i2c_function_t;

/*
 * function definition for UTILITY request 0x12
 * DKT 020304 - new utility function
 */
typedef enum {
	UTILITY_SET_FILTER          = 0x01,
	UTILITY_DATA_ENABLE         = 0x02,
	UTILITY_FLEX_MULTIWRITE     = 0x03,
	UTILITY_SET_BUFFER_SIZE     = 0x04,
	UTILITY_FLEX_OPERATOR       = 0x05,
	UTILITY_FLEX_RESET300_START = 0x06,
	UTILITY_FLEX_RESET300_STOP  = 0x07,
	UTILITY_FLEX_RESET300       = 0x08,
	UTILITY_SET_ISO_SIZE        = 0x09,
	UTILITY_DATA_RESET          = 0x0A,
	UTILITY_GET_DATA_STATUS     = 0x10,
	UTILITY_GET_V8_REG          = 0x11,
/* DKT 020326 - add function for v1.14 */
	UTILITY_SRAM_WRITE          = 0x12,
	UTILITY_SRAM_READ           = 0x13,
	UTILITY_SRAM_TESTFILL       = 0x14,
	UTILITY_SRAM_TESTSET        = 0x15,
	UTILITY_SRAM_TESTVERIFY     = 0x16,
} flexcop_usb_utility_function_t;

#define B2C2_WAIT_FOR_OPERATION_RW  1*HZ       /* 1 s */
#define B2C2_WAIT_FOR_OPERATION_RDW 3*HZ       /* 3 s */
#define B2C2_WAIT_FOR_OPERATION_WDW 1*HZ       /* 1 s */

#define B2C2_WAIT_FOR_OPERATION_V8READ   3*HZ  /* 3 s */
#define B2C2_WAIT_FOR_OPERATION_V8WRITE  3*HZ  /* 3 s */
#define B2C2_WAIT_FOR_OPERATION_V8FLASH  3*HZ  /* 3 s */

typedef enum {
	V8_MEMORY_PAGE_DVB_CI = 0x20,
	V8_MEMORY_PAGE_DVB_DS = 0x40,
	V8_MEMORY_PAGE_MULTI2 = 0x60,
	V8_MEMORY_PAGE_FLASH  = 0x80
} flexcop_usb_mem_page_t;

#define V8_MEMORY_EXTENDED      (1 << 15)

#define USB_MEM_READ_MAX                32
#define USB_MEM_WRITE_MAX               1
#define USB_FLASH_MAX                   8

#define V8_MEMORY_PAGE_SIZE     0x8000      // 32K
#define V8_MEMORY_PAGE_MASK     0x7FFF

#endif
