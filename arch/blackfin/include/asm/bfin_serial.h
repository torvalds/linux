/*
 * bfin_serial.h - Blackfin UART/Serial definitions
 *
 * Copyright 2006-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_ASM_SERIAL_H__
#define __BFIN_ASM_SERIAL_H__

#include <linux/serial_core.h>
#include <linux/spinlock.h>
#include <mach/anomaly.h>
#include <mach/bfin_serial.h>

#if defined(CONFIG_BFIN_UART0_CTSRTS) || \
    defined(CONFIG_BFIN_UART1_CTSRTS) || \
    defined(CONFIG_BFIN_UART2_CTSRTS) || \
    defined(CONFIG_BFIN_UART3_CTSRTS)
# if defined(BFIN_UART_BF54X_STYLE) || defined(BFIN_UART_BF60X_STYLE)
#  define CONFIG_SERIAL_BFIN_HARD_CTSRTS
# else
#  define CONFIG_SERIAL_BFIN_CTSRTS
# endif
#endif

struct circ_buf;
struct timer_list;
struct work_struct;

struct bfin_serial_port {
	struct uart_port port;
	unsigned int old_status;
	int tx_irq;
	int rx_irq;
	int status_irq;
#ifndef BFIN_UART_BF54X_STYLE
	unsigned int lsr;
#endif
#ifdef CONFIG_SERIAL_BFIN_DMA
	int tx_done;
	int tx_count;
	struct circ_buf rx_dma_buf;
	struct timer_list rx_dma_timer;
	int rx_dma_nrows;
	spinlock_t rx_lock;
	unsigned int tx_dma_channel;
	unsigned int rx_dma_channel;
	struct work_struct tx_dma_workqueue;
#elif ANOMALY_05000363
	unsigned int anomaly_threshold;
#endif
#if defined(CONFIG_SERIAL_BFIN_CTSRTS) || \
	defined(CONFIG_SERIAL_BFIN_HARD_CTSRTS)
	int cts_pin;
	int rts_pin;
#endif
};

#ifdef BFIN_UART_BF60X_STYLE

/* UART_CTL Masks */
#define UCEN                     0x1  /* Enable UARTx Clocks */
#define LOOP_ENA                 0x2  /* Loopback Mode Enable */
#define UMOD_MDB                 0x10  /* Enable MDB Mode */
#define UMOD_IRDA                0x20  /* Enable IrDA Mode */
#define UMOD_MASK                0x30  /* Uart Mode Mask */
#define WLS(x)                   (((x-5) & 0x03) << 8)  /* Word Length Select */
#define WLS_MASK                 0x300  /* Word length Select Mask */
#define WLS_OFFSET               8      /* Word length Select Offset */
#define STB                      0x1000  /* Stop Bits */
#define STBH                     0x2000  /* Half Stop Bits */
#define PEN                      0x4000  /* Parity Enable */
#define EPS                      0x8000  /* Even Parity Select */
#define STP                      0x10000  /* Stick Parity */
#define FPE                      0x20000  /* Force Parity Error On Transmit */
#define FFE                      0x40000  /* Force Framing Error On Transmit */
#define SB                       0x80000  /* Set Break */
#define LCR_MASK		 (SB | STP | EPS | PEN | STB | WLS_MASK)
#define FCPOL                    0x400000  /* Flow Control Pin Polarity */
#define RPOLC                    0x800000  /* IrDA RX Polarity Change */
#define TPOLC                    0x1000000  /* IrDA TX Polarity Change */
#define MRTS                     0x2000000  /* Manual Request To Send */
#define XOFF                     0x4000000  /* Transmitter Off */
#define ARTS                     0x8000000  /* Automatic Request To Send */
#define ACTS                     0x10000000  /* Automatic Clear To Send */
#define RFIT                     0x20000000  /* Receive FIFO IRQ Threshold */
#define RFRT                     0x40000000  /* Receive FIFO RTS Threshold */

/* UART_STAT Masks */
#define DR                       0x01  /* Data Ready */
#define OE                       0x02  /* Overrun Error */
#define PE                       0x04  /* Parity Error */
#define FE                       0x08  /* Framing Error */
#define BI                       0x10  /* Break Interrupt */
#define THRE                     0x20  /* THR Empty */
#define TEMT                     0x80  /* TSR and UART_THR Empty */
#define TFI                      0x100  /* Transmission Finished Indicator */

#define ASTKY                    0x200  /* Address Sticky */
#define ADDR                     0x400  /* Address bit status */
#define RO			 0x800  /* Reception Ongoing */
#define SCTS                     0x1000  /* Sticky CTS */
#define CTS                      0x10000  /* Clear To Send */
#define RFCS                     0x20000  /* Receive FIFO Count Status */

/* UART_CLOCK Masks */
#define EDBO                     0x80000000 /* Enable Devide by One */

#else /* BFIN_UART_BF60X_STYLE */

/* UART_LCR Masks */
#define WLS(x)                   (((x)-5) & 0x03)  /* Word Length Select */
#define WLS_MASK                 0x03  /* Word length Select Mask */
#define WLS_OFFSET               0     /* Word length Select Offset */
#define STB                      0x04  /* Stop Bits */
#define PEN                      0x08  /* Parity Enable */
#define EPS                      0x10  /* Even Parity Select */
#define STP                      0x20  /* Stick Parity */
#define SB                       0x40  /* Set Break */
#define DLAB                     0x80  /* Divisor Latch Access */
#define LCR_MASK		 (SB | STP | EPS | PEN | STB | WLS_MASK)

/* UART_LSR Masks */
#define DR                       0x01  /* Data Ready */
#define OE                       0x02  /* Overrun Error */
#define PE                       0x04  /* Parity Error */
#define FE                       0x08  /* Framing Error */
#define BI                       0x10  /* Break Interrupt */
#define THRE                     0x20  /* THR Empty */
#define TEMT                     0x40  /* TSR and UART_THR Empty */
#define TFI                      0x80  /* Transmission Finished Indicator */

/* UART_MCR Masks */
#define XOFF                     0x01  /* Transmitter Off */
#define MRTS                     0x02  /* Manual Request To Send */
#define RFIT                     0x04  /* Receive FIFO IRQ Threshold */
#define RFRT                     0x08  /* Receive FIFO RTS Threshold */
#define LOOP_ENA                 0x10  /* Loopback Mode Enable */
#define FCPOL                    0x20  /* Flow Control Pin Polarity */
#define ARTS                     0x40  /* Automatic Request To Send */
#define ACTS                     0x80  /* Automatic Clear To Send */

/* UART_MSR Masks */
#define SCTS                     0x01  /* Sticky CTS */
#define CTS                      0x10  /* Clear To Send */
#define RFCS                     0x20  /* Receive FIFO Count Status */

/* UART_GCTL Masks */
#define UCEN                     0x01  /* Enable UARTx Clocks */
#define UMOD_IRDA                0x02  /* Enable IrDA Mode */
#define UMOD_MASK                0x02  /* Uart Mode Mask */
#define TPOLC                    0x04  /* IrDA TX Polarity Change */
#define RPOLC                    0x08  /* IrDA RX Polarity Change */
#define FPE                      0x10  /* Force Parity Error On Transmit */
#define FFE                      0x20  /* Force Framing Error On Transmit */

#endif /* BFIN_UART_BF60X_STYLE */

/* UART_IER Masks */
#define ERBFI                    0x01  /* Enable Receive Buffer Full Interrupt */
#define ETBEI                    0x02  /* Enable Transmit Buffer Empty Interrupt */
#define ELSI                     0x04  /* Enable RX Status Interrupt */
#define EDSSI                    0x08  /* Enable Modem Status Interrupt */
#define EDTPTI                   0x10  /* Enable DMA Transmit PIRQ Interrupt */
#define ETFI                     0x20  /* Enable Transmission Finished Interrupt */
#define ERFCI                    0x40  /* Enable Receive FIFO Count Interrupt */

#if defined(BFIN_UART_BF60X_STYLE)
# define OFFSET_REDIV            0x00  /* Version ID Register             */
# define OFFSET_CTL              0x04  /* Control Register                */
# define OFFSET_STAT             0x08  /* Status Register                 */
# define OFFSET_SCR              0x0C  /* SCR Scratch Register            */
# define OFFSET_CLK              0x10  /* Clock Rate Register             */
# define OFFSET_IER              0x14  /* Interrupt Enable Register       */
# define OFFSET_IER_SET          0x18  /* Set Interrupt Enable Register   */
# define OFFSET_IER_CLEAR        0x1C  /* Clear Interrupt Enable Register */
# define OFFSET_RBR              0x20  /* Receive Buffer register         */
# define OFFSET_THR              0x24  /* Transmit Holding register       */
#elif defined(BFIN_UART_BF54X_STYLE)
# define OFFSET_DLL              0x00  /* Divisor Latch (Low-Byte)        */
# define OFFSET_DLH              0x04  /* Divisor Latch (High-Byte)       */
# define OFFSET_GCTL             0x08  /* Global Control Register         */
# define OFFSET_LCR              0x0C  /* Line Control Register           */
# define OFFSET_MCR              0x10  /* Modem Control Register          */
# define OFFSET_LSR              0x14  /* Line Status Register            */
# define OFFSET_MSR              0x18  /* Modem Status Register           */
# define OFFSET_SCR              0x1C  /* SCR Scratch Register            */
# define OFFSET_IER_SET          0x20  /* Set Interrupt Enable Register   */
# define OFFSET_IER_CLEAR        0x24  /* Clear Interrupt Enable Register */
# define OFFSET_THR              0x28  /* Transmit Holding register       */
# define OFFSET_RBR              0x2C  /* Receive Buffer register         */
#else /* BF533 style */
# define OFFSET_THR              0x00  /* Transmit Holding register         */
# define OFFSET_RBR              0x00  /* Receive Buffer register           */
# define OFFSET_DLL              0x00  /* Divisor Latch (Low-Byte)          */
# define OFFSET_DLH              0x04  /* Divisor Latch (High-Byte)         */
# define OFFSET_IER              0x04  /* Interrupt Enable Register         */
# define OFFSET_IIR              0x08  /* Interrupt Identification Register */
# define OFFSET_LCR              0x0C  /* Line Control Register             */
# define OFFSET_MCR              0x10  /* Modem Control Register            */
# define OFFSET_LSR              0x14  /* Line Status Register              */
# define OFFSET_MSR              0x18  /* Modem Status Register             */
# define OFFSET_SCR              0x1C  /* SCR Scratch Register              */
# define OFFSET_GCTL             0x24  /* Global Control Register           */
/* code should not need IIR, so force build error if they use it */
# undef OFFSET_IIR
#endif

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m
struct bfin_uart_regs {
#if defined(BFIN_UART_BF60X_STYLE)
	u32 revid;
	u32 ctl;
	u32 stat;
	u32 scr;
	u32 clk;
	u32 ier;
	u32 ier_set;
	u32 ier_clear;
	u32 rbr;
	u32 thr;
	u32 taip;
	u32 tsr;
	u32 rsr;
	u32 txdiv;
	u32 rxdiv;
#elif defined(BFIN_UART_BF54X_STYLE)
	__BFP(dll);
	__BFP(dlh);
	__BFP(gctl);
	__BFP(lcr);
	__BFP(mcr);
	__BFP(lsr);
	__BFP(msr);
	__BFP(scr);
	__BFP(ier_set);
	__BFP(ier_clear);
	__BFP(thr);
	__BFP(rbr);
#else
	union {
		u16 dll;
		u16 thr;
		const u16 rbr;
	};
	const u16 __pad0;
	union {
		u16 dlh;
		u16 ier;
	};
	const u16 __pad1;
	const __BFP(iir);
	__BFP(lcr);
	__BFP(mcr);
	__BFP(lsr);
	__BFP(msr);
	__BFP(scr);
	const u32 __pad2;
	__BFP(gctl);
#endif
};
#undef __BFP

#define port_membase(uart)     (((struct bfin_serial_port *)(uart))->port.membase)

/*
#ifndef port_membase
# define port_membase(p) 0
#endif
*/
#ifdef BFIN_UART_BF60X_STYLE

#define UART_GET_CHAR(p)      bfin_read32(port_membase(p) + OFFSET_RBR)
#define UART_GET_CLK(p)       bfin_read32(port_membase(p) + OFFSET_CLK)
#define UART_GET_CTL(p)       bfin_read32(port_membase(p) + OFFSET_CTL)
#define UART_GET_GCTL(p)      UART_GET_CTL(p)
#define UART_GET_LCR(p)       UART_GET_CTL(p)
#define UART_GET_MCR(p)       UART_GET_CTL(p)
#if ANOMALY_05001001
#define UART_GET_STAT(p) \
({ \
	u32 __ret; \
	unsigned long flags; \
	flags = hard_local_irq_save(); \
	__ret = bfin_read32(port_membase(p) + OFFSET_STAT); \
	hard_local_irq_restore(flags); \
	__ret; \
})
#else
#define UART_GET_STAT(p)      bfin_read32(port_membase(p) + OFFSET_STAT)
#endif
#define UART_GET_MSR(p)       UART_GET_STAT(p)

#define UART_PUT_CHAR(p, v)   bfin_write32(port_membase(p) + OFFSET_THR, v)
#define UART_PUT_CLK(p, v)    bfin_write32(port_membase(p) + OFFSET_CLK, v)
#define UART_PUT_CTL(p, v)    bfin_write32(port_membase(p) + OFFSET_CTL, v)
#define UART_PUT_GCTL(p, v)   UART_PUT_CTL(p, v)
#define UART_PUT_LCR(p, v)    UART_PUT_CTL(p, v)
#define UART_PUT_MCR(p, v)    UART_PUT_CTL(p, v)
#define UART_PUT_STAT(p, v)   bfin_write32(port_membase(p) + OFFSET_STAT, v)

#define UART_CLEAR_IER(p, v)  bfin_write32(port_membase(p) + OFFSET_IER_CLEAR, v)
#define UART_GET_IER(p)       bfin_read32(port_membase(p) + OFFSET_IER)
#define UART_SET_IER(p, v)    bfin_write32(port_membase(p) + OFFSET_IER_SET, v)

#define UART_CLEAR_DLAB(p)    /* MMRs not muxed on BF60x */
#define UART_SET_DLAB(p)      /* MMRs not muxed on BF60x */

#define UART_CLEAR_LSR(p)     UART_PUT_STAT(p, -1)
#define UART_GET_LSR(p)       UART_GET_STAT(p)
#define UART_PUT_LSR(p, v)    UART_PUT_STAT(p, v)

/* This handles hard CTS/RTS */
#define BFIN_UART_CTSRTS_HARD
#define UART_CLEAR_SCTS(p)      UART_PUT_STAT(p, SCTS)
#define UART_GET_CTS(x)         (UART_GET_MSR(x) & CTS)
#define UART_DISABLE_RTS(x)     UART_PUT_MCR(x, UART_GET_MCR(x) & ~(ARTS | MRTS))
#define UART_ENABLE_RTS(x)      UART_PUT_MCR(x, UART_GET_MCR(x) | MRTS | ARTS)
#define UART_ENABLE_INTS(x, v)  UART_SET_IER(x, v)
#define UART_DISABLE_INTS(x)    UART_CLEAR_IER(x, 0xF)

#else /* BFIN_UART_BF60X_STYLE */

#define UART_GET_CHAR(p)      bfin_read16(port_membase(p) + OFFSET_RBR)
#define UART_GET_DLL(p)       bfin_read16(port_membase(p) + OFFSET_DLL)
#define UART_GET_DLH(p)       bfin_read16(port_membase(p) + OFFSET_DLH)
#define UART_GET_CLK(p)	      ((UART_GET_DLH(p) << 8) | UART_GET_DLL(p))
#define UART_GET_GCTL(p)      bfin_read16(port_membase(p) + OFFSET_GCTL)
#define UART_GET_LCR(p)       bfin_read16(port_membase(p) + OFFSET_LCR)
#define UART_GET_MCR(p)       bfin_read16(port_membase(p) + OFFSET_MCR)
#define UART_GET_MSR(p)       bfin_read16(port_membase(p) + OFFSET_MSR)

#define UART_PUT_CHAR(p, v)   bfin_write16(port_membase(p) + OFFSET_THR, v)
#define UART_PUT_DLL(p, v)    bfin_write16(port_membase(p) + OFFSET_DLL, v)
#define UART_PUT_DLH(p, v)    bfin_write16(port_membase(p) + OFFSET_DLH, v)
#define UART_PUT_CLK(p, v) do \
{\
UART_PUT_DLL(p, v & 0xFF); \
UART_PUT_DLH(p, (v >> 8) & 0xFF); } while (0);

#define UART_PUT_GCTL(p, v)   bfin_write16(port_membase(p) + OFFSET_GCTL, v)
#define UART_PUT_LCR(p, v)    bfin_write16(port_membase(p) + OFFSET_LCR, v)
#define UART_PUT_MCR(p, v)    bfin_write16(port_membase(p) + OFFSET_MCR, v)

#ifdef BFIN_UART_BF54X_STYLE

#define UART_CLEAR_IER(p, v)  bfin_write16(port_membase(p) + OFFSET_IER_CLEAR, v)
#define UART_GET_IER(p)       bfin_read16(port_membase(p) + OFFSET_IER_SET)
#define UART_SET_IER(p, v)    bfin_write16(port_membase(p) + OFFSET_IER_SET, v)

#define UART_CLEAR_DLAB(p)    /* MMRs not muxed on BF54x */
#define UART_SET_DLAB(p)      /* MMRs not muxed on BF54x */

#define UART_CLEAR_LSR(p)     bfin_write16(port_membase(p) + OFFSET_LSR, -1)
#define UART_GET_LSR(p)       bfin_read16(port_membase(p) + OFFSET_LSR)
#define UART_PUT_LSR(p, v)    bfin_write16(port_membase(p) + OFFSET_LSR, v)

/* This handles hard CTS/RTS */
#define BFIN_UART_CTSRTS_HARD
#define UART_CLEAR_SCTS(p)      bfin_write16((port_membase(p) + OFFSET_MSR), SCTS)
#define UART_GET_CTS(x)         (UART_GET_MSR(x) & CTS)
#define UART_DISABLE_RTS(x)     UART_PUT_MCR(x, UART_GET_MCR(x) & ~(ARTS | MRTS))
#define UART_ENABLE_RTS(x)      UART_PUT_MCR(x, UART_GET_MCR(x) | MRTS | ARTS)
#define UART_ENABLE_INTS(x, v)  UART_SET_IER(x, v)
#define UART_DISABLE_INTS(x)    UART_CLEAR_IER(x, 0xF)

#else /* BF533 style */

#define UART_CLEAR_IER(p, v)  UART_PUT_IER(p, UART_GET_IER(p) & ~(v))
#define UART_GET_IER(p)       bfin_read16(port_membase(p) + OFFSET_IER)
#define UART_PUT_IER(p, v)    bfin_write16(port_membase(p) + OFFSET_IER, v)
#define UART_SET_IER(p, v)    UART_PUT_IER(p, UART_GET_IER(p) | (v))

#define UART_CLEAR_DLAB(p)    do { UART_PUT_LCR(p, UART_GET_LCR(p) & ~DLAB); SSYNC(); } while (0)
#define UART_SET_DLAB(p)      do { UART_PUT_LCR(p, UART_GET_LCR(p) | DLAB); SSYNC(); } while (0)

#define get_lsr_cache(uart)    (((struct bfin_serial_port *)(uart))->lsr)
#define put_lsr_cache(uart, v) (((struct bfin_serial_port *)(uart))->lsr = (v))

/*
#ifndef put_lsr_cache
# define put_lsr_cache(p, v)
#endif
#ifndef get_lsr_cache
# define get_lsr_cache(p) 0
#endif
*/

/* The hardware clears the LSR bits upon read, so we need to cache
 * some of the more fun bits in software so they don't get lost
 * when checking the LSR in other code paths (TX).
 */
static inline void UART_CLEAR_LSR(void *p)
{
	put_lsr_cache(p, 0);
	bfin_write16(port_membase(p) + OFFSET_LSR, -1);
}
static inline unsigned int UART_GET_LSR(void *p)
{
	unsigned int lsr = bfin_read16(port_membase(p) + OFFSET_LSR);
	put_lsr_cache(p, get_lsr_cache(p) | (lsr & (BI|FE|PE|OE)));
	return lsr | get_lsr_cache(p);
}
static inline void UART_PUT_LSR(void *p, uint16_t val)
{
	put_lsr_cache(p, get_lsr_cache(p) & ~val);
}

/* This handles soft CTS/RTS */
#define UART_GET_CTS(x)        gpio_get_value((x)->cts_pin)
#define UART_DISABLE_RTS(x)    gpio_set_value((x)->rts_pin, 1)
#define UART_ENABLE_RTS(x)     gpio_set_value((x)->rts_pin, 0)
#define UART_ENABLE_INTS(x, v) UART_PUT_IER(x, v)
#define UART_DISABLE_INTS(x)   UART_PUT_IER(x, 0)

#endif /* BFIN_UART_BF54X_STYLE */

#endif /* BFIN_UART_BF60X_STYLE */

#ifndef BFIN_UART_TX_FIFO_SIZE
# define BFIN_UART_TX_FIFO_SIZE 2
#endif

#endif /* __BFIN_ASM_SERIAL_H__ */
