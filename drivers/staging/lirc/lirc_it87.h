/* lirc_it87.h */
/* SECTION: Definitions */

/********************************* ITE IT87xx ************************/

/* based on the following documentation from ITE:
   a) IT8712F Preliminary CIR Programming Guide V0.1
   b) IT8705F Simple LPC I/O Preliminary Specification V0.3
   c) IT8712F EC-LPC I/O Preliminary Specification V0.5
*/

/* IT8712/05 Ports: */
#define IT87_ADRPORT      0x2e
#define IT87_DATAPORT     0x2f
#define IT87_INIT         {0x87, 0x01, 0x55, 0x55}

/* alternate Ports: */
/*
#define IT87_ADRPORT      0x4e
#define IT87_DATAPORT     0x4f
#define IT87_INIT         {0x87, 0x01, 0x55, 0xaa}
 */

/* IT8712/05 Registers */
#define IT87_CFGCTRL      0x2
#define IT87_LDN          0x7
#define IT87_CHIP_ID1     0x20
#define IT87_CHIP_ID2     0x21
#define IT87_CFG_VERSION  0x22
#define IT87_SWSUSPEND    0x23

#define IT8712_CIR_LDN    0xa
#define IT8705_CIR_LDN    0x7

/* CIR Configuration Registers: */
#define IT87_CIR_ACT      0x30
#define IT87_CIR_BASE_MSB 0x60
#define IT87_CIR_BASE_LSB 0x61
#define IT87_CIR_IRQ      0x70
#define IT87_CIR_CONFIG   0xf0

/* List of IT87_CIR registers: offset to BaseAddr */
#define IT87_CIR_DR   0
#define IT87_CIR_IER  1
#define IT87_CIR_RCR  2
#define IT87_CIR_TCR1 3
#define IT87_CIR_TCR2 4
#define IT87_CIR_TSR  5
#define IT87_CIR_RSR  6
#define IT87_CIR_BDLR 5
#define IT87_CIR_BDHR 6
#define IT87_CIR_IIR  7

/* Bit Definition */
/* IER: */
#define IT87_CIR_IER_TM_EN   0x80
#define IT87_CIR_IER_RESEVED 0x40
#define IT87_CIR_IER_RESET   0x20
#define IT87_CIR_IER_BR      0x10
#define IT87_CIR_IER_IEC     0x8
#define IT87_CIR_IER_RFOIE   0x4
#define IT87_CIR_IER_RDAIE   0x2
#define IT87_CIR_IER_TLDLIE  0x1

/* RCR: */
#define IT87_CIR_RCR_RDWOS  0x80
#define IT87_CIR_RCR_HCFS   0x40
#define IT87_CIR_RCR_RXEN   0x20
#define IT87_CIR_RCR_RXEND  0x10
#define IT87_CIR_RCR_RXACT  0x8
#define IT87_CIR_RCR_RXDCR  0x7

/* TCR1: */
#define IT87_CIR_TCR1_FIFOCLR 0x80
#define IT87_CIR_TCR1_ILE     0x40
#define IT87_CIR_TCR1_FIFOTL  0x30
#define IT87_CIR_TCR1_TXRLE   0x8
#define IT87_CIR_TCR1_TXENDF  0x4
#define IT87_CIR_TCR1_TXMPM   0x3

/* TCR2: */
#define IT87_CIR_TCR2_CFQ   0xf8
#define IT87_CIR_TCR2_TXMPW 0x7

/* TSR: */
#define IT87_CIR_TSR_RESERVED 0xc0
#define IT87_CIR_TSR_TXFBC    0x3f

/* RSR: */
#define IT87_CIR_RSR_RXFTO    0x80
#define IT87_CIR_RSR_RESERVED 0x40
#define IT87_CIR_RSR_RXFBC    0x3f

/* IIR: */
#define IT87_CIR_IIR_RESERVED 0xf8
#define IT87_CIR_IIR_IID      0x6
#define IT87_CIR_IIR_IIP      0x1

/* TM: */
#define IT87_CIR_TM_IL_SEL    0x80
#define IT87_CIR_TM_RESERVED  0x40
#define IT87_CIR_TM_TM_REG    0x3f

#define IT87_CIR_FIFO_SIZE 32

/* Baudratedivisor for IT87: power of 2: only 1,2,4 or 8) */
#define IT87_CIR_BAUDRATE_DIVISOR 0x1
#define IT87_CIR_DEFAULT_IOBASE 0x310
#define IT87_CIR_DEFAULT_IRQ    0x7
#define IT87_CIR_SPACE 0x00
#define IT87_CIR_PULSE 0xff
#define IT87_CIR_FREQ_MIN 27
#define IT87_CIR_FREQ_MAX 58
#define TIME_CONST (IT87_CIR_BAUDRATE_DIVISOR * 8000000ul / 115200ul)

/********************************* ITE IT87xx ************************/
