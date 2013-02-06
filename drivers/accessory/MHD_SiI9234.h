/*
*/


#ifndef __MHD_SiI9234_H
#define __MHD_SiI9234_H


/*typedef unsigned char		bool;*/
typedef unsigned char		byte;
typedef unsigned short		word;



#define DRV_NAME "MHD_sii9234"
#define DRV_VERSION "0.1"
#define MHD_MAX_LENGTH	4096
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define TX_HW_RESET_PERIOD	10
#define TPI_SLAVE_ADDR		0x72

#define ENABLE_AUTO_SOFT_RESET	0x04
#define ASR_VALUE	ENABLE_AUTO_SOFT_RESET

/*TPI Identification Registers*/
/*=============================*/

#define TPI_DEVICE_ID		(0x1B)
#define TPI_DEVICE_REV_ID	(0x1C)
#define TPI_RESERVED2		(0x1D)

#define SiI_DEVICE_ID		0xB0

#define RSEN			0x04


/*Indexed access defines*/
#define TPI_INDEXED_PAGE_REG		0xBC
#define TPI_INDEXED_OFFSET_REG	0xBD
#define TPI_INDEXED_VALUE_REG		0xBE

/*Generic Masks*/
#define SI_ZERO		0x00
#define SI_BIT_0                   0x01
#define SI_BIT_1                   0x02
#define SI_BIT_2                   0x04
#define SI_BIT_3                   0x08
#define SI_BIT_4                   0x10
#define SI_BIT_5                   0x20
#define SI_BIT_6                   0x40
#define SI_BIT_7                   0x80

#define DDC_XLTN_TIMEOUT_MAX_VAL		0x30

/*Indexed register access*/

#define INDEXED_PAGE_0		0x01
#define INDEXED_PAGE_1		0x02
#define INDEXED_PAGE_2		0x03


#define NON_MASKABLE_INT	0xFF
#define CBUS_SLAVE_ADDR	0xC8
#define CEC_SLAVE_ADDR		0xC0

#define FALSE	0
#define TRUE	1

typedef enum {
	TX_HW_RESET,
	GPIO_MAX
} GPIO_SignalType;

#define TPI_DEBUG_PRINT(x) printk x


#endif/*__MHD_SiI9234_H*/
