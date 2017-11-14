#ifndef SMSC_SIO_H
#define SMSC_SIO_H

/******************************************
 Keys. They should work with every SMsC SIO
 ******************************************/

#define SMSCSIO_CFGACCESSKEY		0x55
#define SMSCSIO_CFGEXITKEY			0xaa

/*****************************
 * Generic SIO Flat (!?)     *
 *****************************/
 
/* Register 0x0d */
#define SMSCSIOFLAT_DEVICEID_REG				0x0d

/* Register 0x0c */
#define SMSCSIOFLAT_UARTMODE0C_REG				0x0c
#define 	SMSCSIOFLAT_UART2MODE_MASK			0x38
#define 	SMSCSIOFLAT_UART2MODE_VAL_COM		0x00
#define 	SMSCSIOFLAT_UART2MODE_VAL_IRDA		0x08
#define 	SMSCSIOFLAT_UART2MODE_VAL_ASKIR		0x10

/* Register 0x25 */
#define SMSCSIOFLAT_UART2BASEADDR_REG			0x25

/* Register 0x2b */
#define SMSCSIOFLAT_FIRBASEADDR_REG				0x2b

/* Register 0x2c */
#define SMSCSIOFLAT_FIRDMASELECT_REG			0x2c
#define 	SMSCSIOFLAT_FIRDMASELECT_MASK		0x0f

/* Register 0x28 */
#define SMSCSIOFLAT_UARTIRQSELECT_REG			0x28
#define 	SMSCSIOFLAT_UART2IRQSELECT_MASK		0x0f
#define 	SMSCSIOFLAT_UART1IRQSELECT_MASK		0xf0
#define 	SMSCSIOFLAT_UARTIRQSELECT_VAL_NONE	0x00


/*********************
 * LPC47N227         *
 *********************/

#define LPC47N227_CFGACCESSKEY		0x55
#define LPC47N227_CFGEXITKEY		0xaa

/* Register 0x00 */
#define LPC47N227_FDCPOWERVALIDCONF_REG		0x00
#define 	LPC47N227_FDCPOWER_MASK			0x08
#define 	LPC47N227_VALID_MASK				0x80

/* Register 0x02 */
#define LPC47N227_UART12POWER_REG				0x02
#define 	LPC47N227_UART1POWERDOWN_MASK		0x08
#define 	LPC47N227_UART2POWERDOWN_MASK		0x80

/* Register 0x07 */
#define LPC47N227_APMBOOTDRIVE_REG				0x07
#define 	LPC47N227_PARPORT2AUTOPWRDOWN_MASK	0x10 /* auto power down on if set */
#define 	LPC47N227_UART2AUTOPWRDOWN_MASK	0x20 /* auto power down on if set */
#define 	LPC47N227_UART1AUTOPWRDOWN_MASK	0x40 /* auto power down on if set */

/* Register 0x0c */
#define LPC47N227_UARTMODE0C_REG				0x0c
#define 	LPC47N227_UART2MODE_MASK			0x38
#define 	LPC47N227_UART2MODE_VAL_COM		0x00
#define 	LPC47N227_UART2MODE_VAL_IRDA		0x08
#define 	LPC47N227_UART2MODE_VAL_ASKIR		0x10

/* Register 0x0d */
#define LPC47N227_DEVICEID_REG					0x0d
#define 	LPC47N227_DEVICEID_DEFVAL			0x5a

/* Register 0x0e */
#define LPC47N227_REVISIONID_REG				0x0e

/* Register 0x25 */
#define LPC47N227_UART2BASEADDR_REG			0x25

/* Register 0x28 */
#define LPC47N227_UARTIRQSELECT_REG			0x28
#define 	LPC47N227_UART2IRQSELECT_MASK		0x0f
#define 	LPC47N227_UART1IRQSELECT_MASK		0xf0
#define 	LPC47N227_UARTIRQSELECT_VAL_NONE	0x00

/* Register 0x2b */
#define LPC47N227_FIRBASEADDR_REG				0x2b

/* Register 0x2c */
#define LPC47N227_FIRDMASELECT_REG				0x2c
#define 	LPC47N227_FIRDMASELECT_MASK		0x0f
#define 	LPC47N227_FIRDMASELECT_VAL_DMA1	0x01 /* 47n227 has three dma channels */
#define 	LPC47N227_FIRDMASELECT_VAL_DMA2	0x02
#define 	LPC47N227_FIRDMASELECT_VAL_DMA3	0x03
#define 	LPC47N227_FIRDMASELECT_VAL_NONE	0x0f


#endif
