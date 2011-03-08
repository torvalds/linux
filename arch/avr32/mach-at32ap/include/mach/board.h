/*
 * Platform data definitions.
 */
#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

#include <linux/types.h>
#include <linux/serial.h>
#include <linux/platform_data/macb.h>

#define GPIO_PIN_NONE	(-1)

/*
 * Clock rates for various on-board oscillators. The number of entries
 * in this array is chip-dependent.
 */
extern unsigned long at32_board_osc_rates[];

/*
 * This used to add essential system devices, but this is now done
 * automatically. Please don't use it in new board code.
 */
static inline void __deprecated at32_add_system_devices(void)
{

}

#define ATMEL_MAX_UART	4
extern struct platform_device *atmel_default_console_device;

/* Flags for selecting USART extra pins */
#define	ATMEL_USART_RTS		0x01
#define	ATMEL_USART_CTS		0x02
#define	ATMEL_USART_CLK		0x04

struct atmel_uart_data {
	int		num;		/* port num */
	short		use_dma_tx;	/* use transmit DMA? */
	short		use_dma_rx;	/* use receive DMA? */
	void __iomem	*regs;		/* virtual base address, if any */
	struct serial_rs485	rs485;		/* rs485 settings */
};
void at32_map_usart(unsigned int hw_id, unsigned int line, int flags);
struct platform_device *at32_add_device_usart(unsigned int id);

struct platform_device *
at32_add_device_eth(unsigned int id, struct macb_platform_data *data);

struct spi_board_info;
struct platform_device *
at32_add_device_spi(unsigned int id, struct spi_board_info *b, unsigned int n);
void at32_spi_setup_slaves(unsigned int bus_num, struct spi_board_info *b, unsigned int n);

struct atmel_lcdfb_info;
struct platform_device *
at32_add_device_lcdc(unsigned int id, struct atmel_lcdfb_info *data,
		     unsigned long fbmem_start, unsigned long fbmem_len,
		     u64 pin_mask);

struct usba_platform_data;
struct platform_device *
at32_add_device_usba(unsigned int id, struct usba_platform_data *data);

struct ide_platform_data {
	u8      cs;
};
struct platform_device *
at32_add_device_ide(unsigned int id, unsigned int extint,
		    struct ide_platform_data *data);

/* mask says which PWM channels to mux */
struct platform_device *at32_add_device_pwm(u32 mask);

/* depending on what's hooked up, not all SSC pins will be used */
#define	ATMEL_SSC_TK		0x01
#define	ATMEL_SSC_TF		0x02
#define	ATMEL_SSC_TD		0x04
#define	ATMEL_SSC_TX		(ATMEL_SSC_TK | ATMEL_SSC_TF | ATMEL_SSC_TD)

#define	ATMEL_SSC_RK		0x10
#define	ATMEL_SSC_RF		0x20
#define	ATMEL_SSC_RD		0x40
#define	ATMEL_SSC_RX		(ATMEL_SSC_RK | ATMEL_SSC_RF | ATMEL_SSC_RD)

struct platform_device *
at32_add_device_ssc(unsigned int id, unsigned int flags);

struct i2c_board_info;
struct platform_device *at32_add_device_twi(unsigned int id,
					    struct i2c_board_info *b,
					    unsigned int n);

struct mci_platform_data;
struct platform_device *
at32_add_device_mci(unsigned int id, struct mci_platform_data *data);

struct ac97c_platform_data;
struct platform_device *
at32_add_device_ac97c(unsigned int id, struct ac97c_platform_data *data,
		      unsigned int flags);

struct atmel_abdac_pdata;
struct platform_device *
at32_add_device_abdac(unsigned int id, struct atmel_abdac_pdata *data);

struct platform_device *at32_add_device_psif(unsigned int id);

struct cf_platform_data {
	int	detect_pin;
	int	reset_pin;
	int	vcc_pin;
	int	ready_pin;
	u8	cs;
};
struct platform_device *
at32_add_device_cf(unsigned int id, unsigned int extint,
		struct cf_platform_data *data);

/* NAND / SmartMedia */
struct atmel_nand_data {
	int	enable_pin;	/* chip enable */
	int	det_pin;	/* card detect */
	int	rdy_pin;	/* ready/busy */
	u8	rdy_pin_active_low;	/* rdy_pin value is inverted */
	u8	ale;		/* address line number connected to ALE */
	u8	cle;		/* address line number connected to CLE */
	u8	bus_width_16;	/* buswidth is 16 bit */
	struct mtd_partition *parts;
	unsigned int	num_parts;
};
struct platform_device *
at32_add_device_nand(unsigned int id, struct atmel_nand_data *data);

#endif /* __ASM_ARCH_BOARD_H */
