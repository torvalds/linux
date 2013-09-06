/*
 * ni_labpc register definitions.
*/

#ifndef _NI_LABPC_REGS_H
#define _NI_LABPC_REGS_H

/*
 * Register map (all registers are 8-bit)
 */
#define STAT1_REG		0x00	/* R: Status 1 reg */
#define STAT1_DAVAIL		(1 << 0)
#define STAT1_OVERRUN		(1 << 1)
#define STAT1_OVERFLOW		(1 << 2)
#define STAT1_CNTINT		(1 << 3)
#define STAT1_GATA0		(1 << 5)
#define STAT1_EXTGATA0		(1 << 6)
#define CMD1_REG		0x00	/* W: Command 1 reg */
#define CMD1_MA(x)		(((x) & 0x7) << 0)
#define CMD1_TWOSCMP		(1 << 3)
#define CMD1_GAIN(x)		(((x) & 0x7) << 4)
#define CMD1_SCANEN		(1 << 7)
#define CMD2_REG		0x01	/* W: Command 2 reg */
#define CMD2_PRETRIG		(1 << 0)
#define CMD2_HWTRIG		(1 << 1)
#define CMD2_SWTRIG		(1 << 2)
#define CMD2_TBSEL		(1 << 3)
#define CMD2_2SDAC0		(1 << 4)
#define CMD2_2SDAC1		(1 << 5)
#define CMD2_LDAC(x)		(1 << (6 + (x)))
#define CMD3_REG		0x02	/* W: Command 3 reg */
#define CMD3_DMAEN		(1 << 0)
#define CMD3_DIOINTEN		(1 << 1)
#define CMD3_DMATCINTEN		(1 << 2)
#define CMD3_CNTINTEN		(1 << 3)
#define CMD3_ERRINTEN		(1 << 4)
#define CMD3_FIFOINTEN		(1 << 5)
#define ADC_START_CONVERT_REG	0x03	/* W: Start Convert reg */
#define DAC_LSB_REG(x)		(0x04 + 2 * (x)) /* W: DAC0/1 LSB reg */
#define DAC_MSB_REG(x)		(0x05 + 2 * (x)) /* W: DAC0/1 MSB reg */
#define ADC_FIFO_CLEAR_REG	0x08	/* W: A/D FIFO Clear reg */
#define ADC_FIFO_REG		0x0a	/* R: A/D FIFO reg */
#define DMATC_CLEAR_REG		0x0a	/* W: DMA Interrupt Clear reg */
#define TIMER_CLEAR_REG		0x0c	/* W: Timer Interrupt Clear reg */
#define CMD6_REG		0x0e	/* W: Command 6 reg */
#define CMD6_NRSE		(1 << 0)
#define CMD6_ADCUNI		(1 << 1)
#define CMD6_DACUNI(x)		(1 << (2 + (x)))
#define CMD6_HFINTEN		(1 << 5)
#define CMD6_DQINTEN		(1 << 6)
#define CMD6_SCANUP		(1 << 7)
#define CMD4_REG		0x0f	/* W: Command 3 reg */
#define CMD4_INTSCAN		(1 << 0)
#define CMD4_EOIRCV		(1 << 1)
#define CMD4_ECLKDRV		(1 << 2)
#define CMD4_SEDIFF		(1 << 3)
#define CMD4_ECLKRCV		(1 << 4)
#define DIO_BASE_REG		0x10	/* R/W: 8255 DIO base reg */
#define COUNTER_A_BASE_REG	0x14	/* R/W: 8253 Counter A base reg */
#define COUNTER_B_BASE_REG	0x18	/* R/W: 8253 Counter B base reg */
#define CMD5_REG		0x1c	/* W: Command 5 reg */
#define CMD5_WRTPRT		(1 << 2)
#define CMD5_DITHEREN		(1 << 3)
#define CMD5_CALDACLD		(1 << 4)
#define CMD5_SCLK		(1 << 5)
#define CMD5_SDATA		(1 << 6)
#define CMD5_EEPROMCS		(1 << 7)
#define STAT2_REG		0x1d	/* R: Status 2 reg */
#define STAT2_PROMOUT		(1 << 0)
#define STAT2_OUTA1		(1 << 1)
#define STAT2_FIFONHF		(1 << 2)
#define INTERVAL_COUNT_REG	0x1e	/* W: Interval Counter Data reg */
#define INTERVAL_STROBE_REG	0x1f	/* W: Interval Counter Strobe reg */

#endif /* _NI_LABPC_REGS_H */
