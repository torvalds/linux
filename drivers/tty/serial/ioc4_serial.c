/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * This file contains a module version of the ioc4 serial driver. This
 * includes all the support functions needed (support functions, etc.)
 * and the serial driver itself.
 */
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/circ_buf.h>
#include <linux/serial_reg.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioc4.h>
#include <linux/serial_core.h>
#include <linux/slab.h>

/*
 * interesting things about the ioc4
 */

#define IOC4_NUM_SERIAL_PORTS	4	/* max ports per card */
#define IOC4_NUM_CARDS		8	/* max cards per partition */

#define	GET_SIO_IR(_n)	(_n == 0) ? (IOC4_SIO_IR_S0) : \
				(_n == 1) ? (IOC4_SIO_IR_S1) : \
				(_n == 2) ? (IOC4_SIO_IR_S2) : \
				(IOC4_SIO_IR_S3)

#define	GET_OTHER_IR(_n)  (_n == 0) ? (IOC4_OTHER_IR_S0_MEMERR) : \
				(_n == 1) ? (IOC4_OTHER_IR_S1_MEMERR) : \
				(_n == 2) ? (IOC4_OTHER_IR_S2_MEMERR) : \
				(IOC4_OTHER_IR_S3_MEMERR)


/*
 * All IOC4 registers are 32 bits wide.
 */

/*
 * PCI Memory Space Map
 */
#define IOC4_PCI_ERR_ADDR_L     0x000	/* Low Error Address */
#define IOC4_PCI_ERR_ADDR_VLD	        (0x1 << 0)
#define IOC4_PCI_ERR_ADDR_MST_ID_MSK    (0xf << 1)
#define IOC4_PCI_ERR_ADDR_MST_NUM_MSK   (0xe << 1)
#define IOC4_PCI_ERR_ADDR_MST_TYP_MSK   (0x1 << 1)
#define IOC4_PCI_ERR_ADDR_MUL_ERR       (0x1 << 5)
#define IOC4_PCI_ERR_ADDR_ADDR_MSK      (0x3ffffff << 6)

/* Interrupt types */
#define	IOC4_SIO_INTR_TYPE	0
#define	IOC4_OTHER_INTR_TYPE	1
#define	IOC4_NUM_INTR_TYPES	2

/* Bitmasks for IOC4_SIO_IR, IOC4_SIO_IEC, and IOC4_SIO_IES  */
#define IOC4_SIO_IR_S0_TX_MT	   0x00000001	/* Serial port 0 TX empty */
#define IOC4_SIO_IR_S0_RX_FULL	   0x00000002	/* Port 0 RX buf full */
#define IOC4_SIO_IR_S0_RX_HIGH	   0x00000004	/* Port 0 RX hiwat */
#define IOC4_SIO_IR_S0_RX_TIMER	   0x00000008	/* Port 0 RX timeout */
#define IOC4_SIO_IR_S0_DELTA_DCD   0x00000010	/* Port 0 delta DCD */
#define IOC4_SIO_IR_S0_DELTA_CTS   0x00000020	/* Port 0 delta CTS */
#define IOC4_SIO_IR_S0_INT	   0x00000040	/* Port 0 pass-thru intr */
#define IOC4_SIO_IR_S0_TX_EXPLICIT 0x00000080	/* Port 0 explicit TX thru */
#define IOC4_SIO_IR_S1_TX_MT	   0x00000100	/* Serial port 1 */
#define IOC4_SIO_IR_S1_RX_FULL	   0x00000200	/* */
#define IOC4_SIO_IR_S1_RX_HIGH	   0x00000400	/* */
#define IOC4_SIO_IR_S1_RX_TIMER	   0x00000800	/* */
#define IOC4_SIO_IR_S1_DELTA_DCD   0x00001000	/* */
#define IOC4_SIO_IR_S1_DELTA_CTS   0x00002000	/* */
#define IOC4_SIO_IR_S1_INT	   0x00004000	/* */
#define IOC4_SIO_IR_S1_TX_EXPLICIT 0x00008000	/* */
#define IOC4_SIO_IR_S2_TX_MT	   0x00010000	/* Serial port 2 */
#define IOC4_SIO_IR_S2_RX_FULL	   0x00020000	/* */
#define IOC4_SIO_IR_S2_RX_HIGH	   0x00040000	/* */
#define IOC4_SIO_IR_S2_RX_TIMER	   0x00080000	/* */
#define IOC4_SIO_IR_S2_DELTA_DCD   0x00100000	/* */
#define IOC4_SIO_IR_S2_DELTA_CTS   0x00200000	/* */
#define IOC4_SIO_IR_S2_INT	   0x00400000	/* */
#define IOC4_SIO_IR_S2_TX_EXPLICIT 0x00800000	/* */
#define IOC4_SIO_IR_S3_TX_MT	   0x01000000	/* Serial port 3 */
#define IOC4_SIO_IR_S3_RX_FULL	   0x02000000	/* */
#define IOC4_SIO_IR_S3_RX_HIGH	   0x04000000	/* */
#define IOC4_SIO_IR_S3_RX_TIMER	   0x08000000	/* */
#define IOC4_SIO_IR_S3_DELTA_DCD   0x10000000	/* */
#define IOC4_SIO_IR_S3_DELTA_CTS   0x20000000	/* */
#define IOC4_SIO_IR_S3_INT	   0x40000000	/* */
#define IOC4_SIO_IR_S3_TX_EXPLICIT 0x80000000	/* */

/* Per device interrupt masks */
#define IOC4_SIO_IR_S0		(IOC4_SIO_IR_S0_TX_MT | \
				 IOC4_SIO_IR_S0_RX_FULL | \
				 IOC4_SIO_IR_S0_RX_HIGH | \
				 IOC4_SIO_IR_S0_RX_TIMER | \
				 IOC4_SIO_IR_S0_DELTA_DCD | \
				 IOC4_SIO_IR_S0_DELTA_CTS | \
				 IOC4_SIO_IR_S0_INT | \
				 IOC4_SIO_IR_S0_TX_EXPLICIT)
#define IOC4_SIO_IR_S1		(IOC4_SIO_IR_S1_TX_MT | \
				 IOC4_SIO_IR_S1_RX_FULL | \
				 IOC4_SIO_IR_S1_RX_HIGH | \
				 IOC4_SIO_IR_S1_RX_TIMER | \
				 IOC4_SIO_IR_S1_DELTA_DCD | \
				 IOC4_SIO_IR_S1_DELTA_CTS | \
				 IOC4_SIO_IR_S1_INT | \
				 IOC4_SIO_IR_S1_TX_EXPLICIT)
#define IOC4_SIO_IR_S2		(IOC4_SIO_IR_S2_TX_MT | \
				 IOC4_SIO_IR_S2_RX_FULL | \
				 IOC4_SIO_IR_S2_RX_HIGH | \
				 IOC4_SIO_IR_S2_RX_TIMER | \
				 IOC4_SIO_IR_S2_DELTA_DCD | \
				 IOC4_SIO_IR_S2_DELTA_CTS | \
				 IOC4_SIO_IR_S2_INT | \
				 IOC4_SIO_IR_S2_TX_EXPLICIT)
#define IOC4_SIO_IR_S3		(IOC4_SIO_IR_S3_TX_MT | \
				 IOC4_SIO_IR_S3_RX_FULL | \
				 IOC4_SIO_IR_S3_RX_HIGH | \
				 IOC4_SIO_IR_S3_RX_TIMER | \
				 IOC4_SIO_IR_S3_DELTA_DCD | \
				 IOC4_SIO_IR_S3_DELTA_CTS | \
				 IOC4_SIO_IR_S3_INT | \
				 IOC4_SIO_IR_S3_TX_EXPLICIT)

/* Bitmasks for IOC4_OTHER_IR, IOC4_OTHER_IEC, and IOC4_OTHER_IES  */
#define IOC4_OTHER_IR_ATA_INT		0x00000001  /* ATAPI intr pass-thru */
#define IOC4_OTHER_IR_ATA_MEMERR	0x00000002  /* ATAPI DMA PCI error */
#define IOC4_OTHER_IR_S0_MEMERR		0x00000004  /* Port 0 PCI error */
#define IOC4_OTHER_IR_S1_MEMERR		0x00000008  /* Port 1 PCI error */
#define IOC4_OTHER_IR_S2_MEMERR		0x00000010  /* Port 2 PCI error */
#define IOC4_OTHER_IR_S3_MEMERR		0x00000020  /* Port 3 PCI error */
#define IOC4_OTHER_IR_KBD_INT		0x00000040  /* Keyboard/mouse */
#define IOC4_OTHER_IR_RESERVED		0x007fff80  /* Reserved */
#define IOC4_OTHER_IR_RT_INT		0x00800000  /* INT_OUT section output */
#define IOC4_OTHER_IR_GEN_INT		0xff000000  /* Generic pins */

#define IOC4_OTHER_IR_SER_MEMERR (IOC4_OTHER_IR_S0_MEMERR | IOC4_OTHER_IR_S1_MEMERR | \
				  IOC4_OTHER_IR_S2_MEMERR | IOC4_OTHER_IR_S3_MEMERR)

/* Bitmasks for IOC4_SIO_CR */
#define IOC4_SIO_CR_CMD_PULSE_SHIFT              0  /* byte bus strobe shift */
#define IOC4_SIO_CR_ARB_DIAG_TX0	0x00000000
#define IOC4_SIO_CR_ARB_DIAG_RX0	0x00000010
#define IOC4_SIO_CR_ARB_DIAG_TX1	0x00000020
#define IOC4_SIO_CR_ARB_DIAG_RX1	0x00000030
#define IOC4_SIO_CR_ARB_DIAG_TX2	0x00000040
#define IOC4_SIO_CR_ARB_DIAG_RX2	0x00000050
#define IOC4_SIO_CR_ARB_DIAG_TX3	0x00000060
#define IOC4_SIO_CR_ARB_DIAG_RX3	0x00000070
#define IOC4_SIO_CR_SIO_DIAG_IDLE	0x00000080  /* 0 -> active request among
							   serial ports (ro) */
/* Defs for some of the generic I/O pins */
#define IOC4_GPCR_UART0_MODESEL	   0x10	/* Pin is output to port 0
						   mode sel */
#define IOC4_GPCR_UART1_MODESEL	   0x20	/* Pin is output to port 1
						   mode sel */
#define IOC4_GPCR_UART2_MODESEL	   0x40	/* Pin is output to port 2
						   mode sel */
#define IOC4_GPCR_UART3_MODESEL	   0x80	/* Pin is output to port 3
						   mode sel */

#define IOC4_GPPR_UART0_MODESEL_PIN   4	/* GIO pin controlling
					   uart 0 mode select */
#define IOC4_GPPR_UART1_MODESEL_PIN   5	/* GIO pin controlling
					   uart 1 mode select */
#define IOC4_GPPR_UART2_MODESEL_PIN   6	/* GIO pin controlling
					   uart 2 mode select */
#define IOC4_GPPR_UART3_MODESEL_PIN   7	/* GIO pin controlling
					   uart 3 mode select */

/* Bitmasks for serial RX status byte */
#define IOC4_RXSB_OVERRUN       0x01	/* Char(s) lost */
#define IOC4_RXSB_PAR_ERR	0x02	/* Parity error */
#define IOC4_RXSB_FRAME_ERR	0x04	/* Framing error */
#define IOC4_RXSB_BREAK	        0x08	/* Break character */
#define IOC4_RXSB_CTS	        0x10	/* State of CTS */
#define IOC4_RXSB_DCD	        0x20	/* State of DCD */
#define IOC4_RXSB_MODEM_VALID   0x40	/* DCD, CTS, and OVERRUN are valid */
#define IOC4_RXSB_DATA_VALID    0x80	/* Data byte, FRAME_ERR PAR_ERR
					 * & BREAK valid */

/* Bitmasks for serial TX control byte */
#define IOC4_TXCB_INT_WHEN_DONE 0x20	/* Interrupt after this byte is sent */
#define IOC4_TXCB_INVALID	0x00	/* Byte is invalid */
#define IOC4_TXCB_VALID	        0x40	/* Byte is valid */
#define IOC4_TXCB_MCR	        0x80	/* Data<7:0> to modem control reg */
#define IOC4_TXCB_DELAY	        0xc0	/* Delay data<7:0> mSec */

/* Bitmasks for IOC4_SBBR_L */
#define IOC4_SBBR_L_SIZE	0x00000001  /* 0 == 1KB rings, 1 == 4KB rings */

/* Bitmasks for IOC4_SSCR_<3:0> */
#define IOC4_SSCR_RX_THRESHOLD  0x000001ff  /* Hiwater mark */
#define IOC4_SSCR_TX_TIMER_BUSY 0x00010000  /* TX timer in progress */
#define IOC4_SSCR_HFC_EN	0x00020000  /* Hardware flow control enabled */
#define IOC4_SSCR_RX_RING_DCD   0x00040000  /* Post RX record on delta-DCD */
#define IOC4_SSCR_RX_RING_CTS   0x00080000  /* Post RX record on delta-CTS */
#define IOC4_SSCR_DIAG	        0x00200000  /* Bypass clock divider for sim */
#define IOC4_SSCR_RX_DRAIN	0x08000000  /* Drain RX buffer to memory */
#define IOC4_SSCR_DMA_EN	0x10000000  /* Enable ring buffer DMA */
#define IOC4_SSCR_DMA_PAUSE	0x20000000  /* Pause DMA */
#define IOC4_SSCR_PAUSE_STATE   0x40000000  /* Sets when PAUSE takes effect */
#define IOC4_SSCR_RESET	        0x80000000  /* Reset DMA channels */

/* All producer/comsumer pointers are the same bitfield */
#define IOC4_PROD_CONS_PTR_4K   0x00000ff8	/* For 4K buffers */
#define IOC4_PROD_CONS_PTR_1K   0x000003f8	/* For 1K buffers */
#define IOC4_PROD_CONS_PTR_OFF           3

/* Bitmasks for IOC4_SRCIR_<3:0> */
#define IOC4_SRCIR_ARM	        0x80000000	/* Arm RX timer */

/* Bitmasks for IOC4_SHADOW_<3:0> */
#define IOC4_SHADOW_DR	 0x00000001	/* Data ready */
#define IOC4_SHADOW_OE	 0x00000002	/* Overrun error */
#define IOC4_SHADOW_PE	 0x00000004	/* Parity error */
#define IOC4_SHADOW_FE	 0x00000008	/* Framing error */
#define IOC4_SHADOW_BI	 0x00000010	/* Break interrupt */
#define IOC4_SHADOW_THRE 0x00000020	/* Xmit holding register empty */
#define IOC4_SHADOW_TEMT 0x00000040	/* Xmit shift register empty */
#define IOC4_SHADOW_RFCE 0x00000080	/* Char in RX fifo has an error */
#define IOC4_SHADOW_DCTS 0x00010000	/* Delta clear to send */
#define IOC4_SHADOW_DDCD 0x00080000	/* Delta data carrier detect */
#define IOC4_SHADOW_CTS	 0x00100000	/* Clear to send */
#define IOC4_SHADOW_DCD	 0x00800000	/* Data carrier detect */
#define IOC4_SHADOW_DTR	 0x01000000	/* Data terminal ready */
#define IOC4_SHADOW_RTS	 0x02000000	/* Request to send */
#define IOC4_SHADOW_OUT1 0x04000000	/* 16550 OUT1 bit */
#define IOC4_SHADOW_OUT2 0x08000000	/* 16550 OUT2 bit */
#define IOC4_SHADOW_LOOP 0x10000000	/* Loopback enabled */

/* Bitmasks for IOC4_SRTR_<3:0> */
#define IOC4_SRTR_CNT	        0x00000fff	/* Reload value for RX timer */
#define IOC4_SRTR_CNT_VAL	0x0fff0000	/* Current value of RX timer */
#define IOC4_SRTR_CNT_VAL_SHIFT         16
#define IOC4_SRTR_HZ                 16000	/* SRTR clock frequency */

/* Serial port register map used for DMA and PIO serial I/O */
struct ioc4_serialregs {
	uint32_t sscr;
	uint32_t stpir;
	uint32_t stcir;
	uint32_t srpir;
	uint32_t srcir;
	uint32_t srtr;
	uint32_t shadow;
};

/* IOC4 UART register map */
struct ioc4_uartregs {
	char i4u_lcr;
	union {
		char iir;	/* read only */
		char fcr;	/* write only */
	} u3;
	union {
		char ier;	/* DLAB == 0 */
		char dlm;	/* DLAB == 1 */
	} u2;
	union {
		char rbr;	/* read only, DLAB == 0 */
		char thr;	/* write only, DLAB == 0 */
		char dll;	/* DLAB == 1 */
	} u1;
	char i4u_scr;
	char i4u_msr;
	char i4u_lsr;
	char i4u_mcr;
};

/* short names */
#define i4u_dll u1.dll
#define i4u_ier u2.ier
#define i4u_dlm u2.dlm
#define i4u_fcr u3.fcr

/* Serial port registers used for DMA serial I/O */
struct ioc4_serial {
	uint32_t sbbr01_l;
	uint32_t sbbr01_h;
	uint32_t sbbr23_l;
	uint32_t sbbr23_h;

	struct ioc4_serialregs port_0;
	struct ioc4_serialregs port_1;
	struct ioc4_serialregs port_2;
	struct ioc4_serialregs port_3;
	struct ioc4_uartregs uart_0;
	struct ioc4_uartregs uart_1;
	struct ioc4_uartregs uart_2;
	struct ioc4_uartregs uart_3;
};

/* UART clock speed */
#define IOC4_SER_XIN_CLK_66     66666667
#define IOC4_SER_XIN_CLK_33     33333333

#define IOC4_W_IES		0
#define IOC4_W_IEC		1

typedef void ioc4_intr_func_f(void *, uint32_t);
typedef ioc4_intr_func_f *ioc4_intr_func_t;

static unsigned int Num_of_ioc4_cards;

/* defining this will get you LOTS of great debug info */
//#define DEBUG_INTERRUPTS
#define DPRINT_CONFIG(_x...)	;
//#define DPRINT_CONFIG(_x...)	printk _x

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS	256

/* number of characters we want to transmit to the lower level at a time */
#define IOC4_MAX_CHARS	256
#define IOC4_FIFO_CHARS	255

/* Device name we're using */
#define DEVICE_NAME_RS232  "ttyIOC"
#define DEVICE_NAME_RS422  "ttyAIOC"
#define DEVICE_MAJOR	   204
#define DEVICE_MINOR_RS232 50
#define DEVICE_MINOR_RS422 84


/* register offsets */
#define IOC4_SERIAL_OFFSET	0x300

/* flags for next_char_state */
#define NCS_BREAK	0x1
#define NCS_PARITY	0x2
#define NCS_FRAMING	0x4
#define NCS_OVERRUN	0x8

/* cause we need SOME parameters ... */
#define MIN_BAUD_SUPPORTED	1200
#define MAX_BAUD_SUPPORTED	115200

/* protocol types supported */
#define PROTO_RS232	3
#define PROTO_RS422	7

/* Notification types */
#define N_DATA_READY	0x01
#define N_OUTPUT_LOWAT	0x02
#define N_BREAK		0x04
#define N_PARITY_ERROR	0x08
#define N_FRAMING_ERROR	0x10
#define N_OVERRUN_ERROR	0x20
#define N_DDCD		0x40
#define N_DCTS		0x80

#define N_ALL_INPUT	(N_DATA_READY | N_BREAK |			\
			 N_PARITY_ERROR | N_FRAMING_ERROR |		\
			 N_OVERRUN_ERROR | N_DDCD | N_DCTS)

#define N_ALL_OUTPUT	N_OUTPUT_LOWAT

#define N_ALL_ERRORS	(N_PARITY_ERROR | N_FRAMING_ERROR | N_OVERRUN_ERROR)

#define N_ALL		(N_DATA_READY | N_OUTPUT_LOWAT | N_BREAK |	\
			 N_PARITY_ERROR | N_FRAMING_ERROR |		\
			 N_OVERRUN_ERROR | N_DDCD | N_DCTS)

#define SER_DIVISOR(_x, clk)		(((clk) + (_x) * 8) / ((_x) * 16))
#define DIVISOR_TO_BAUD(div, clk)	((clk) / 16 / (div))

/* Some masks */
#define LCR_MASK_BITS_CHAR	(UART_LCR_WLEN5 | UART_LCR_WLEN6 \
					| UART_LCR_WLEN7 | UART_LCR_WLEN8)
#define LCR_MASK_STOP_BITS	(UART_LCR_STOP)

#define PENDING(_p)	(readl(&(_p)->ip_mem->sio_ir.raw) & _p->ip_ienb)
#define READ_SIO_IR(_p) readl(&(_p)->ip_mem->sio_ir.raw)

/* Default to 4k buffers */
#ifdef IOC4_1K_BUFFERS
#define RING_BUF_SIZE 1024
#define IOC4_BUF_SIZE_BIT 0
#define PROD_CONS_MASK IOC4_PROD_CONS_PTR_1K
#else
#define RING_BUF_SIZE 4096
#define IOC4_BUF_SIZE_BIT IOC4_SBBR_L_SIZE
#define PROD_CONS_MASK IOC4_PROD_CONS_PTR_4K
#endif

#define TOTAL_RING_BUF_SIZE (RING_BUF_SIZE * 4)

/*
 * This is the entry saved by the driver - one per card
 */

#define UART_PORT_MIN		0
#define UART_PORT_RS232		UART_PORT_MIN
#define UART_PORT_RS422		1
#define UART_PORT_COUNT		2	/* one for each mode */

struct ioc4_control {
	int ic_irq;
	struct {
		/* uart ports are allocated here - 1 for rs232, 1 for rs422 */
		struct uart_port icp_uart_port[UART_PORT_COUNT];
		/* Handy reference material */
		struct ioc4_port *icp_port;
	} ic_port[IOC4_NUM_SERIAL_PORTS];
	struct ioc4_soft *ic_soft;
};

/*
 * per-IOC4 data structure
 */
#define MAX_IOC4_INTR_ENTS	(8 * sizeof(uint32_t))
struct ioc4_soft {
	struct ioc4_misc_regs __iomem *is_ioc4_misc_addr;
	struct ioc4_serial __iomem *is_ioc4_serial_addr;

	/* Each interrupt type has an entry in the array */
	struct ioc4_intr_type {

		/*
		 * Each in-use entry in this array contains at least
		 * one nonzero bit in sd_bits; no two entries in this
		 * array have overlapping sd_bits values.
		 */
		struct ioc4_intr_info {
			uint32_t sd_bits;
			ioc4_intr_func_f *sd_intr;
			void *sd_info;
		} is_intr_info[MAX_IOC4_INTR_ENTS];

		/* Number of entries active in the above array */
		atomic_t is_num_intrs;
	} is_intr_type[IOC4_NUM_INTR_TYPES];

	/* is_ir_lock must be held while
	 * modifying sio_ie values, so
	 * we can be sure that sio_ie is
	 * not changing when we read it
	 * along with sio_ir.
	 */
	spinlock_t is_ir_lock;	/* SIO_IE[SC] mod lock */
};

/* Local port info for each IOC4 serial ports */
struct ioc4_port {
	struct uart_port *ip_port;	/* current active port ptr */
	/* Ptrs for all ports */
	struct uart_port *ip_all_ports[UART_PORT_COUNT];
	/* Back ptrs for this port */
	struct ioc4_control *ip_control;
	struct pci_dev *ip_pdev;
	struct ioc4_soft *ip_ioc4_soft;

	/* pci mem addresses */
	struct ioc4_misc_regs __iomem *ip_mem;
	struct ioc4_serial __iomem *ip_serial;
	struct ioc4_serialregs __iomem *ip_serial_regs;
	struct ioc4_uartregs __iomem *ip_uart_regs;

	/* Ring buffer page for this port */
	dma_addr_t ip_dma_ringbuf;
	/* vaddr of ring buffer */
	struct ring_buffer *ip_cpu_ringbuf;

	/* Rings for this port */
	struct ring *ip_inring;
	struct ring *ip_outring;

	/* Hook to port specific values */
	struct hooks *ip_hooks;

	spinlock_t ip_lock;

	/* Various rx/tx parameters */
	int ip_baud;
	int ip_tx_lowat;
	int ip_rx_timeout;

	/* Copy of notification bits */
	int ip_notify;

	/* Shadow copies of various registers so we don't need to PIO
	 * read them constantly
	 */
	uint32_t ip_ienb;	/* Enabled interrupts */
	uint32_t ip_sscr;
	uint32_t ip_tx_prod;
	uint32_t ip_rx_cons;
	int ip_pci_bus_speed;
	unsigned char ip_flags;
};

/* tx low water mark.  We need to notify the driver whenever tx is getting
 * close to empty so it can refill the tx buffer and keep things going.
 * Let's assume that if we interrupt 1 ms before the tx goes idle, we'll
 * have no trouble getting in more chars in time (I certainly hope so).
 */
#define TX_LOWAT_LATENCY      1000
#define TX_LOWAT_HZ          (1000000 / TX_LOWAT_LATENCY)
#define TX_LOWAT_CHARS(baud) (baud / 10 / TX_LOWAT_HZ)

/* Flags per port */
#define INPUT_HIGH	0x01
#define DCD_ON		0x02
#define LOWAT_WRITTEN	0x04
#define READ_ABORTED	0x08
#define PORT_ACTIVE	0x10
#define PORT_INACTIVE	0	/* This is the value when "off" */


/* Since each port has different register offsets and bitmasks
 * for everything, we'll store those that we need in tables so we
 * don't have to be constantly checking the port we are dealing with.
 */
struct hooks {
	uint32_t intr_delta_dcd;
	uint32_t intr_delta_cts;
	uint32_t intr_tx_mt;
	uint32_t intr_rx_timer;
	uint32_t intr_rx_high;
	uint32_t intr_tx_explicit;
	uint32_t intr_dma_error;
	uint32_t intr_clear;
	uint32_t intr_all;
	int rs422_select_pin;
};

static struct hooks hooks_array[IOC4_NUM_SERIAL_PORTS] = {
	/* Values for port 0 */
	{
	 IOC4_SIO_IR_S0_DELTA_DCD, IOC4_SIO_IR_S0_DELTA_CTS,
	 IOC4_SIO_IR_S0_TX_MT, IOC4_SIO_IR_S0_RX_TIMER,
	 IOC4_SIO_IR_S0_RX_HIGH, IOC4_SIO_IR_S0_TX_EXPLICIT,
	 IOC4_OTHER_IR_S0_MEMERR,
	 (IOC4_SIO_IR_S0_TX_MT | IOC4_SIO_IR_S0_RX_FULL |
	  IOC4_SIO_IR_S0_RX_HIGH | IOC4_SIO_IR_S0_RX_TIMER |
	  IOC4_SIO_IR_S0_DELTA_DCD | IOC4_SIO_IR_S0_DELTA_CTS |
	  IOC4_SIO_IR_S0_INT | IOC4_SIO_IR_S0_TX_EXPLICIT),
	 IOC4_SIO_IR_S0, IOC4_GPPR_UART0_MODESEL_PIN,
	 },

	/* Values for port 1 */
	{
	 IOC4_SIO_IR_S1_DELTA_DCD, IOC4_SIO_IR_S1_DELTA_CTS,
	 IOC4_SIO_IR_S1_TX_MT, IOC4_SIO_IR_S1_RX_TIMER,
	 IOC4_SIO_IR_S1_RX_HIGH, IOC4_SIO_IR_S1_TX_EXPLICIT,
	 IOC4_OTHER_IR_S1_MEMERR,
	 (IOC4_SIO_IR_S1_TX_MT | IOC4_SIO_IR_S1_RX_FULL |
	  IOC4_SIO_IR_S1_RX_HIGH | IOC4_SIO_IR_S1_RX_TIMER |
	  IOC4_SIO_IR_S1_DELTA_DCD | IOC4_SIO_IR_S1_DELTA_CTS |
	  IOC4_SIO_IR_S1_INT | IOC4_SIO_IR_S1_TX_EXPLICIT),
	 IOC4_SIO_IR_S1, IOC4_GPPR_UART1_MODESEL_PIN,
	 },

	/* Values for port 2 */
	{
	 IOC4_SIO_IR_S2_DELTA_DCD, IOC4_SIO_IR_S2_DELTA_CTS,
	 IOC4_SIO_IR_S2_TX_MT, IOC4_SIO_IR_S2_RX_TIMER,
	 IOC4_SIO_IR_S2_RX_HIGH, IOC4_SIO_IR_S2_TX_EXPLICIT,
	 IOC4_OTHER_IR_S2_MEMERR,
	 (IOC4_SIO_IR_S2_TX_MT | IOC4_SIO_IR_S2_RX_FULL |
	  IOC4_SIO_IR_S2_RX_HIGH | IOC4_SIO_IR_S2_RX_TIMER |
	  IOC4_SIO_IR_S2_DELTA_DCD | IOC4_SIO_IR_S2_DELTA_CTS |
	  IOC4_SIO_IR_S2_INT | IOC4_SIO_IR_S2_TX_EXPLICIT),
	 IOC4_SIO_IR_S2, IOC4_GPPR_UART2_MODESEL_PIN,
	 },

	/* Values for port 3 */
	{
	 IOC4_SIO_IR_S3_DELTA_DCD, IOC4_SIO_IR_S3_DELTA_CTS,
	 IOC4_SIO_IR_S3_TX_MT, IOC4_SIO_IR_S3_RX_TIMER,
	 IOC4_SIO_IR_S3_RX_HIGH, IOC4_SIO_IR_S3_TX_EXPLICIT,
	 IOC4_OTHER_IR_S3_MEMERR,
	 (IOC4_SIO_IR_S3_TX_MT | IOC4_SIO_IR_S3_RX_FULL |
	  IOC4_SIO_IR_S3_RX_HIGH | IOC4_SIO_IR_S3_RX_TIMER |
	  IOC4_SIO_IR_S3_DELTA_DCD | IOC4_SIO_IR_S3_DELTA_CTS |
	  IOC4_SIO_IR_S3_INT | IOC4_SIO_IR_S3_TX_EXPLICIT),
	 IOC4_SIO_IR_S3, IOC4_GPPR_UART3_MODESEL_PIN,
	 }
};

/* A ring buffer entry */
struct ring_entry {
	union {
		struct {
			uint32_t alldata;
			uint32_t allsc;
		} all;
		struct {
			char data[4];	/* data bytes */
			char sc[4];	/* status/control */
		} s;
	} u;
};

/* Test the valid bits in any of the 4 sc chars using "allsc" member */
#define RING_ANY_VALID \
	((uint32_t)(IOC4_RXSB_MODEM_VALID | IOC4_RXSB_DATA_VALID) * 0x01010101)

#define ring_sc     u.s.sc
#define ring_data   u.s.data
#define ring_allsc  u.all.allsc

/* Number of entries per ring buffer. */
#define ENTRIES_PER_RING (RING_BUF_SIZE / (int) sizeof(struct ring_entry))

/* An individual ring */
struct ring {
	struct ring_entry entries[ENTRIES_PER_RING];
};

/* The whole enchilada */
struct ring_buffer {
	struct ring TX_0_OR_2;
	struct ring RX_0_OR_2;
	struct ring TX_1_OR_3;
	struct ring RX_1_OR_3;
};

/* Get a ring from a port struct */
#define RING(_p, _wh)	&(((struct ring_buffer *)((_p)->ip_cpu_ringbuf))->_wh)

/* Infinite loop detection.
 */
#define MAXITER 10000000

/* Prototypes */
static void receive_chars(struct uart_port *);
static void handle_intr(void *arg, uint32_t sio_ir);

/*
 * port_is_active - determines if this port is currently active
 * @port: ptr to soft struct for this port
 * @uart_port: uart port to test for
 */
static inline int port_is_active(struct ioc4_port *port,
		struct uart_port *uart_port)
{
	if (port) {
		if ((port->ip_flags & PORT_ACTIVE)
					&& (port->ip_port == uart_port))
			return 1;
	}
	return 0;
}


/**
 * write_ireg - write the interrupt regs
 * @ioc4_soft: ptr to soft struct for this port
 * @val: value to write
 * @which: which register
 * @type: which ireg set
 */
static inline void
write_ireg(struct ioc4_soft *ioc4_soft, uint32_t val, int which, int type)
{
	struct ioc4_misc_regs __iomem *mem = ioc4_soft->is_ioc4_misc_addr;
	unsigned long flags;

	spin_lock_irqsave(&ioc4_soft->is_ir_lock, flags);

	switch (type) {
	case IOC4_SIO_INTR_TYPE:
		switch (which) {
		case IOC4_W_IES:
			writel(val, &mem->sio_ies.raw);
			break;

		case IOC4_W_IEC:
			writel(val, &mem->sio_iec.raw);
			break;
		}
		break;

	case IOC4_OTHER_INTR_TYPE:
		switch (which) {
		case IOC4_W_IES:
			writel(val, &mem->other_ies.raw);
			break;

		case IOC4_W_IEC:
			writel(val, &mem->other_iec.raw);
			break;
		}
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&ioc4_soft->is_ir_lock, flags);
}

/**
 * set_baud - Baud rate setting code
 * @port: port to set
 * @baud: baud rate to use
 */
static int set_baud(struct ioc4_port *port, int baud)
{
	int actual_baud;
	int diff;
	int lcr;
	unsigned short divisor;
	struct ioc4_uartregs __iomem *uart;

	divisor = SER_DIVISOR(baud, port->ip_pci_bus_speed);
	if (!divisor)
		return 1;
	actual_baud = DIVISOR_TO_BAUD(divisor, port->ip_pci_bus_speed);

	diff = actual_baud - baud;
	if (diff < 0)
		diff = -diff;

	/* If we're within 1%, we've found a match */
	if (diff * 100 > actual_baud)
		return 1;

	uart = port->ip_uart_regs;
	lcr = readb(&uart->i4u_lcr);
	writeb(lcr | UART_LCR_DLAB, &uart->i4u_lcr);
	writeb((unsigned char)divisor, &uart->i4u_dll);
	writeb((unsigned char)(divisor >> 8), &uart->i4u_dlm);
	writeb(lcr, &uart->i4u_lcr);
	return 0;
}


/**
 * get_ioc4_port - given a uart port, return the control structure
 * @port: uart port
 * @set: set this port as current
 */
static struct ioc4_port *get_ioc4_port(struct uart_port *the_port, int set)
{
	struct ioc4_driver_data *idd = dev_get_drvdata(the_port->dev);
	struct ioc4_control *control = idd->idd_serial_data;
	struct ioc4_port *port;
	int port_num, port_type;

	if (control) {
		for ( port_num = 0; port_num < IOC4_NUM_SERIAL_PORTS;
							port_num++ ) {
			port = control->ic_port[port_num].icp_port;
			if (!port)
				continue;
			for (port_type = UART_PORT_MIN;
						port_type < UART_PORT_COUNT;
						port_type++) {
				if (the_port == port->ip_all_ports
							[port_type]) {
					/* set local copy */
					if (set) {
						port->ip_port = the_port;
					}
					return port;
				}
			}
		}
	}
	return NULL;
}

/* The IOC4 hardware provides no atomic way to determine if interrupts
 * are pending since two reads are required to do so.  The handler must
 * read the SIO_IR and the SIO_IES, and take the logical and of the
 * two.  When this value is zero, all interrupts have been serviced and
 * the handler may return.
 *
 * This has the unfortunate "hole" that, if some other CPU or
 * some other thread or some higher level interrupt manages to
 * modify SIO_IE between our reads of SIO_IR and SIO_IE, we may
 * think we have observed SIO_IR&SIO_IE==0 when in fact this
 * condition never really occurred.
 *
 * To solve this, we use a simple spinlock that must be held
 * whenever modifying SIO_IE; holding this lock while observing
 * both SIO_IR and SIO_IE guarantees that we do not falsely
 * conclude that no enabled interrupts are pending.
 */

static inline uint32_t
pending_intrs(struct ioc4_soft *soft, int type)
{
	struct ioc4_misc_regs __iomem *mem = soft->is_ioc4_misc_addr;
	unsigned long flag;
	uint32_t intrs = 0;

	BUG_ON(!((type == IOC4_SIO_INTR_TYPE)
	       || (type == IOC4_OTHER_INTR_TYPE)));

	spin_lock_irqsave(&soft->is_ir_lock, flag);

	switch (type) {
	case IOC4_SIO_INTR_TYPE:
		intrs = readl(&mem->sio_ir.raw) & readl(&mem->sio_ies.raw);
		break;

	case IOC4_OTHER_INTR_TYPE:
		intrs = readl(&mem->other_ir.raw) & readl(&mem->other_ies.raw);

		/* Don't process any ATA interrupte */
		intrs &= ~(IOC4_OTHER_IR_ATA_INT | IOC4_OTHER_IR_ATA_MEMERR);
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&soft->is_ir_lock, flag);
	return intrs;
}

/**
 * port_init - Initialize the sio and ioc4 hardware for a given port
 *			called per port from attach...
 * @port: port to initialize
 */
static int inline port_init(struct ioc4_port *port)
{
	uint32_t sio_cr;
	struct hooks *hooks = port->ip_hooks;
	struct ioc4_uartregs __iomem *uart;

	/* Idle the IOC4 serial interface */
	writel(IOC4_SSCR_RESET, &port->ip_serial_regs->sscr);

	/* Wait until any pending bus activity for this port has ceased */
	do
		sio_cr = readl(&port->ip_mem->sio_cr.raw);
	while (!(sio_cr & IOC4_SIO_CR_SIO_DIAG_IDLE));

	/* Finish reset sequence */
	writel(0, &port->ip_serial_regs->sscr);

	/* Once RESET is done, reload cached tx_prod and rx_cons values
	 * and set rings to empty by making prod == cons
	 */
	port->ip_tx_prod = readl(&port->ip_serial_regs->stcir) & PROD_CONS_MASK;
	writel(port->ip_tx_prod, &port->ip_serial_regs->stpir);
	port->ip_rx_cons = readl(&port->ip_serial_regs->srpir) & PROD_CONS_MASK;
	writel(port->ip_rx_cons | IOC4_SRCIR_ARM, &port->ip_serial_regs->srcir);

	/* Disable interrupts for this 16550 */
	uart = port->ip_uart_regs;
	writeb(0, &uart->i4u_lcr);
	writeb(0, &uart->i4u_ier);

	/* Set the default baud */
	set_baud(port, port->ip_baud);

	/* Set line control to 8 bits no parity */
	writeb(UART_LCR_WLEN8 | 0, &uart->i4u_lcr);
					/* UART_LCR_STOP == 1 stop */

	/* Enable the FIFOs */
	writeb(UART_FCR_ENABLE_FIFO, &uart->i4u_fcr);
	/* then reset 16550 FIFOs */
	writeb(UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
			&uart->i4u_fcr);

	/* Clear modem control register */
	writeb(0, &uart->i4u_mcr);

	/* Clear deltas in modem status register */
	readb(&uart->i4u_msr);

	/* Only do this once per port pair */
	if (port->ip_hooks == &hooks_array[0]
			    || port->ip_hooks == &hooks_array[2]) {
		unsigned long ring_pci_addr;
		uint32_t __iomem *sbbr_l;
		uint32_t __iomem *sbbr_h;

		if (port->ip_hooks == &hooks_array[0]) {
			sbbr_l = &port->ip_serial->sbbr01_l;
			sbbr_h = &port->ip_serial->sbbr01_h;
		} else {
			sbbr_l = &port->ip_serial->sbbr23_l;
			sbbr_h = &port->ip_serial->sbbr23_h;
		}

		ring_pci_addr = (unsigned long __iomem)port->ip_dma_ringbuf;
		DPRINT_CONFIG(("%s: ring_pci_addr 0x%lx\n",
					__func__, ring_pci_addr));

		writel((unsigned int)((uint64_t)ring_pci_addr >> 32), sbbr_h);
		writel((unsigned int)ring_pci_addr | IOC4_BUF_SIZE_BIT, sbbr_l);
	}

	/* Set the receive timeout value to 10 msec */
	writel(IOC4_SRTR_HZ / 100, &port->ip_serial_regs->srtr);

	/* Set rx threshold, enable DMA */
	/* Set high water mark at 3/4 of full ring */
	port->ip_sscr = (ENTRIES_PER_RING * 3 / 4);
	writel(port->ip_sscr, &port->ip_serial_regs->sscr);

	/* Disable and clear all serial related interrupt bits */
	write_ireg(port->ip_ioc4_soft, hooks->intr_clear,
		       IOC4_W_IEC, IOC4_SIO_INTR_TYPE);
	port->ip_ienb &= ~hooks->intr_clear;
	writel(hooks->intr_clear, &port->ip_mem->sio_ir.raw);
	return 0;
}

/**
 * handle_dma_error_intr - service any pending DMA error interrupts for the
 *			given port - 2nd level called via sd_intr
 * @arg: handler arg
 * @other_ir: ioc4regs
 */
static void handle_dma_error_intr(void *arg, uint32_t other_ir)
{
	struct ioc4_port *port = (struct ioc4_port *)arg;
	struct hooks *hooks = port->ip_hooks;
	unsigned long flags;

	spin_lock_irqsave(&port->ip_lock, flags);

	/* ACK the interrupt */
	writel(hooks->intr_dma_error, &port->ip_mem->other_ir.raw);

	if (readl(&port->ip_mem->pci_err_addr_l.raw) & IOC4_PCI_ERR_ADDR_VLD) {
		printk(KERN_ERR
			"PCI error address is 0x%llx, "
				"master is serial port %c %s\n",
		     (((uint64_t)readl(&port->ip_mem->pci_err_addr_h)
							 << 32)
				| readl(&port->ip_mem->pci_err_addr_l.raw))
					& IOC4_PCI_ERR_ADDR_ADDR_MSK, '1' +
		     ((char)(readl(&port->ip_mem->pci_err_addr_l.raw) &
			     IOC4_PCI_ERR_ADDR_MST_NUM_MSK) >> 1),
		     (readl(&port->ip_mem->pci_err_addr_l.raw)
				& IOC4_PCI_ERR_ADDR_MST_TYP_MSK)
				? "RX" : "TX");

		if (readl(&port->ip_mem->pci_err_addr_l.raw)
						& IOC4_PCI_ERR_ADDR_MUL_ERR) {
			printk(KERN_ERR
				"Multiple errors occurred\n");
		}
	}
	spin_unlock_irqrestore(&port->ip_lock, flags);

	/* Re-enable DMA error interrupts */
	write_ireg(port->ip_ioc4_soft, hooks->intr_dma_error, IOC4_W_IES,
						IOC4_OTHER_INTR_TYPE);
}

/**
 * intr_connect - interrupt connect function
 * @soft: soft struct for this card
 * @type: interrupt type
 * @intrbits: bit pattern to set
 * @intr: handler function
 * @info: handler arg
 */
static void
intr_connect(struct ioc4_soft *soft, int type,
		  uint32_t intrbits, ioc4_intr_func_f * intr, void *info)
{
	int i;
	struct ioc4_intr_info *intr_ptr;

	BUG_ON(!((type == IOC4_SIO_INTR_TYPE)
	       || (type == IOC4_OTHER_INTR_TYPE)));

	i = atomic_inc_return(&soft-> is_intr_type[type].is_num_intrs) - 1;
	BUG_ON(!(i < MAX_IOC4_INTR_ENTS || (printk("i %d\n", i), 0)));

	/* Save off the lower level interrupt handler */
	intr_ptr = &soft->is_intr_type[type].is_intr_info[i];
	intr_ptr->sd_bits = intrbits;
	intr_ptr->sd_intr = intr;
	intr_ptr->sd_info = info;
}

/**
 * ioc4_intr - Top level IOC4 interrupt handler.
 * @irq: irq value
 * @arg: handler arg
 */

static irqreturn_t ioc4_intr(int irq, void *arg)
{
	struct ioc4_soft *soft;
	uint32_t this_ir, this_mir;
	int xx, num_intrs = 0;
	int intr_type;
	int handled = 0;
	struct ioc4_intr_info *intr_info;

	soft = arg;
	for (intr_type = 0; intr_type < IOC4_NUM_INTR_TYPES; intr_type++) {
		num_intrs = (int)atomic_read(
				&soft->is_intr_type[intr_type].is_num_intrs);

		this_mir = this_ir = pending_intrs(soft, intr_type);

		/* Farm out the interrupt to the various drivers depending on
		 * which interrupt bits are set.
		 */
		for (xx = 0; xx < num_intrs; xx++) {
			intr_info = &soft->is_intr_type[intr_type].is_intr_info[xx];
			this_mir = this_ir & intr_info->sd_bits;
			if (this_mir) {
				/* Disable owned interrupts, call handler */
				handled++;
				write_ireg(soft, intr_info->sd_bits, IOC4_W_IEC,
								intr_type);
				intr_info->sd_intr(intr_info->sd_info, this_mir);
				this_ir &= ~this_mir;
			}
		}
	}
#ifdef DEBUG_INTERRUPTS
	{
		struct ioc4_misc_regs __iomem *mem = soft->is_ioc4_misc_addr;
		unsigned long flag;

		spin_lock_irqsave(&soft->is_ir_lock, flag);
		printk ("%s : %d : mem 0x%p sio_ir 0x%x sio_ies 0x%x "
				"other_ir 0x%x other_ies 0x%x mask 0x%x\n",
		     __func__, __LINE__,
		     (void *)mem, readl(&mem->sio_ir.raw),
		     readl(&mem->sio_ies.raw),
		     readl(&mem->other_ir.raw),
		     readl(&mem->other_ies.raw),
		     IOC4_OTHER_IR_ATA_INT | IOC4_OTHER_IR_ATA_MEMERR);
		spin_unlock_irqrestore(&soft->is_ir_lock, flag);
	}
#endif
	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * ioc4_attach_local - Device initialization.
 *			Called at *_attach() time for each
 *			IOC4 with serial ports in the system.
 * @idd: Master module data for this IOC4
 */
static int inline ioc4_attach_local(struct ioc4_driver_data *idd)
{
	struct ioc4_port *port;
	struct ioc4_port *ports[IOC4_NUM_SERIAL_PORTS];
	int port_number;
	uint16_t ioc4_revid_min = 62;
	uint16_t ioc4_revid;
	struct pci_dev *pdev = idd->idd_pdev;
	struct ioc4_control* control = idd->idd_serial_data;
	struct ioc4_soft *soft = control->ic_soft;
	void __iomem *ioc4_misc = idd->idd_misc_regs;
	void __iomem *ioc4_serial = soft->is_ioc4_serial_addr;

	/* IOC4 firmware must be at least rev 62 */
	pci_read_config_word(pdev, PCI_COMMAND_SPECIAL, &ioc4_revid);

	printk(KERN_INFO "IOC4 firmware revision %d\n", ioc4_revid);
	if (ioc4_revid < ioc4_revid_min) {
		printk(KERN_WARNING
		    "IOC4 serial not supported on firmware rev %d, "
				"please upgrade to rev %d or higher\n",
				ioc4_revid, ioc4_revid_min);
		return -EPERM;
	}
	BUG_ON(ioc4_misc == NULL);
	BUG_ON(ioc4_serial == NULL);

	/* Create port structures for each port */
	for (port_number = 0; port_number < IOC4_NUM_SERIAL_PORTS;
							port_number++) {
		port = kzalloc(sizeof(struct ioc4_port), GFP_KERNEL);
		if (!port) {
			printk(KERN_WARNING
				"IOC4 serial memory not available for port\n");
			goto free;
		}
		spin_lock_init(&port->ip_lock);

		/* we need to remember the previous ones, to point back to
		 * them farther down - setting up the ring buffers.
		 */
		ports[port_number] = port;

		/* Allocate buffers and jumpstart the hardware.  */
		control->ic_port[port_number].icp_port = port;
		port->ip_ioc4_soft = soft;
		port->ip_pdev = pdev;
		port->ip_ienb = 0;
		/* Use baud rate calculations based on detected PCI
		 * bus speed.  Simply test whether the PCI clock is
		 * running closer to 66MHz or 33MHz.
		 */
		if (idd->count_period/IOC4_EXTINT_COUNT_DIVISOR < 20) {
			port->ip_pci_bus_speed = IOC4_SER_XIN_CLK_66;
		} else {
			port->ip_pci_bus_speed = IOC4_SER_XIN_CLK_33;
		}
		port->ip_baud = 9600;
		port->ip_control = control;
		port->ip_mem = ioc4_misc;
		port->ip_serial = ioc4_serial;

		/* point to the right hook */
		port->ip_hooks = &hooks_array[port_number];

		/* Get direct hooks to the serial regs and uart regs
		 * for this port
		 */
		switch (port_number) {
		case 0:
			port->ip_serial_regs = &(port->ip_serial->port_0);
			port->ip_uart_regs = &(port->ip_serial->uart_0);
			break;
		case 1:
			port->ip_serial_regs = &(port->ip_serial->port_1);
			port->ip_uart_regs = &(port->ip_serial->uart_1);
			break;
		case 2:
			port->ip_serial_regs = &(port->ip_serial->port_2);
			port->ip_uart_regs = &(port->ip_serial->uart_2);
			break;
		default:
		case 3:
			port->ip_serial_regs = &(port->ip_serial->port_3);
			port->ip_uart_regs = &(port->ip_serial->uart_3);
			break;
		}

		/* ring buffers are 1 to a pair of ports */
		if (port_number && (port_number & 1)) {
			/* odd use the evens buffer */
			port->ip_dma_ringbuf =
					ports[port_number - 1]->ip_dma_ringbuf;
			port->ip_cpu_ringbuf =
					ports[port_number - 1]->ip_cpu_ringbuf;
			port->ip_inring = RING(port, RX_1_OR_3);
			port->ip_outring = RING(port, TX_1_OR_3);

		} else {
			if (port->ip_dma_ringbuf == 0) {
				port->ip_cpu_ringbuf = pci_alloc_consistent
					(pdev, TOTAL_RING_BUF_SIZE,
					&port->ip_dma_ringbuf);

			}
			BUG_ON(!((((int64_t)port->ip_dma_ringbuf) &
				(TOTAL_RING_BUF_SIZE - 1)) == 0));
			DPRINT_CONFIG(("%s : ip_cpu_ringbuf 0x%p "
						"ip_dma_ringbuf 0x%p\n",
					__func__,
					(void *)port->ip_cpu_ringbuf,
					(void *)port->ip_dma_ringbuf));
			port->ip_inring = RING(port, RX_0_OR_2);
			port->ip_outring = RING(port, TX_0_OR_2);
		}
		DPRINT_CONFIG(("%s : port %d [addr 0x%p] control 0x%p",
				__func__,
				port_number, (void *)port, (void *)control));
		DPRINT_CONFIG((" ip_serial_regs 0x%p ip_uart_regs 0x%p\n",
				(void *)port->ip_serial_regs,
				(void *)port->ip_uart_regs));

		/* Initialize the hardware for IOC4 */
		port_init(port);

		DPRINT_CONFIG(("%s: port_number %d port 0x%p inring 0x%p "
						"outring 0x%p\n",
				__func__,
				port_number, (void *)port,
				(void *)port->ip_inring,
				(void *)port->ip_outring));

		/* Attach interrupt handlers */
		intr_connect(soft, IOC4_SIO_INTR_TYPE,
				GET_SIO_IR(port_number),
				handle_intr, port);

		intr_connect(soft, IOC4_OTHER_INTR_TYPE,
				GET_OTHER_IR(port_number),
				handle_dma_error_intr, port);
	}
	return 0;

free:
	while (port_number)
		kfree(ports[--port_number]);
	return -ENOMEM;
}

/**
 * enable_intrs - enable interrupts
 * @port: port to enable
 * @mask: mask to use
 */
static void enable_intrs(struct ioc4_port *port, uint32_t mask)
{
	struct hooks *hooks = port->ip_hooks;

	if ((port->ip_ienb & mask) != mask) {
		write_ireg(port->ip_ioc4_soft, mask, IOC4_W_IES,
						IOC4_SIO_INTR_TYPE);
		port->ip_ienb |= mask;
	}

	if (port->ip_ienb)
		write_ireg(port->ip_ioc4_soft, hooks->intr_dma_error,
				IOC4_W_IES, IOC4_OTHER_INTR_TYPE);
}

/**
 * local_open - local open a port
 * @port: port to open
 */
static inline int local_open(struct ioc4_port *port)
{
	int spiniter = 0;

	port->ip_flags = PORT_ACTIVE;

	/* Pause the DMA interface if necessary */
	if (port->ip_sscr & IOC4_SSCR_DMA_EN) {
		writel(port->ip_sscr | IOC4_SSCR_DMA_PAUSE,
			&port->ip_serial_regs->sscr);
		while((readl(&port->ip_serial_regs-> sscr)
				& IOC4_SSCR_PAUSE_STATE) == 0) {
			spiniter++;
			if (spiniter > MAXITER) {
				port->ip_flags = PORT_INACTIVE;
				return -1;
			}
		}
	}

	/* Reset the input fifo.  If the uart received chars while the port
	 * was closed and DMA is not enabled, the uart may have a bunch of
	 * chars hanging around in its rx fifo which will not be discarded
	 * by rclr in the upper layer. We must get rid of them here.
	 */
	writeb(UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR,
				&port->ip_uart_regs->i4u_fcr);

	writeb(UART_LCR_WLEN8, &port->ip_uart_regs->i4u_lcr);
					/* UART_LCR_STOP == 1 stop */

	/* Re-enable DMA, set default threshold to intr whenever there is
	 * data available.
	 */
	port->ip_sscr &= ~IOC4_SSCR_RX_THRESHOLD;
	port->ip_sscr |= 1;	/* default threshold */

	/* Plug in the new sscr.  This implicitly clears the DMA_PAUSE
	 * flag if it was set above
	 */
	writel(port->ip_sscr, &port->ip_serial_regs->sscr);
	port->ip_tx_lowat = 1;
	return 0;
}

/**
 * set_rx_timeout - Set rx timeout and threshold values.
 * @port: port to use
 * @timeout: timeout value in ticks
 */
static inline int set_rx_timeout(struct ioc4_port *port, int timeout)
{
	int threshold;

	port->ip_rx_timeout = timeout;

	/* Timeout is in ticks.  Let's figure out how many chars we
	 * can receive at the current baud rate in that interval
	 * and set the rx threshold to that amount.  There are 4 chars
	 * per ring entry, so we'll divide the number of chars that will
	 * arrive in timeout by 4.
	 * So .... timeout * baud / 10 / HZ / 4, with HZ = 100.
	 */
	threshold = timeout * port->ip_baud / 4000;
	if (threshold == 0)
		threshold = 1;	/* otherwise we'll intr all the time! */

	if ((unsigned)threshold > (unsigned)IOC4_SSCR_RX_THRESHOLD)
		return 1;

	port->ip_sscr &= ~IOC4_SSCR_RX_THRESHOLD;
	port->ip_sscr |= threshold;

	writel(port->ip_sscr, &port->ip_serial_regs->sscr);

	/* Now set the rx timeout to the given value
	 * again timeout * IOC4_SRTR_HZ / HZ
	 */
	timeout = timeout * IOC4_SRTR_HZ / 100;
	if (timeout > IOC4_SRTR_CNT)
		timeout = IOC4_SRTR_CNT;

	writel(timeout, &port->ip_serial_regs->srtr);
	return 0;
}

/**
 * config_port - config the hardware
 * @port: port to config
 * @baud: baud rate for the port
 * @byte_size: data size
 * @stop_bits: number of stop bits
 * @parenb: parity enable ?
 * @parodd: odd parity ?
 */
static inline int
config_port(struct ioc4_port *port,
	    int baud, int byte_size, int stop_bits, int parenb, int parodd)
{
	char lcr, sizebits;
	int spiniter = 0;

	DPRINT_CONFIG(("%s: baud %d byte_size %d stop %d parenb %d parodd %d\n",
		__func__, baud, byte_size, stop_bits, parenb, parodd));

	if (set_baud(port, baud))
		return 1;

	switch (byte_size) {
	case 5:
		sizebits = UART_LCR_WLEN5;
		break;
	case 6:
		sizebits = UART_LCR_WLEN6;
		break;
	case 7:
		sizebits = UART_LCR_WLEN7;
		break;
	case 8:
		sizebits = UART_LCR_WLEN8;
		break;
	default:
		return 1;
	}

	/* Pause the DMA interface if necessary */
	if (port->ip_sscr & IOC4_SSCR_DMA_EN) {
		writel(port->ip_sscr | IOC4_SSCR_DMA_PAUSE,
			&port->ip_serial_regs->sscr);
		while((readl(&port->ip_serial_regs->sscr)
						& IOC4_SSCR_PAUSE_STATE) == 0) {
			spiniter++;
			if (spiniter > MAXITER)
				return -1;
		}
	}

	/* Clear relevant fields in lcr */
	lcr = readb(&port->ip_uart_regs->i4u_lcr);
	lcr &= ~(LCR_MASK_BITS_CHAR | UART_LCR_EPAR |
		 UART_LCR_PARITY | LCR_MASK_STOP_BITS);

	/* Set byte size in lcr */
	lcr |= sizebits;

	/* Set parity */
	if (parenb) {
		lcr |= UART_LCR_PARITY;
		if (!parodd)
			lcr |= UART_LCR_EPAR;
	}

	/* Set stop bits */
	if (stop_bits)
		lcr |= UART_LCR_STOP /* 2 stop bits */ ;

	writeb(lcr, &port->ip_uart_regs->i4u_lcr);

	/* Re-enable the DMA interface if necessary */
	if (port->ip_sscr & IOC4_SSCR_DMA_EN) {
		writel(port->ip_sscr, &port->ip_serial_regs->sscr);
	}
	port->ip_baud = baud;

	/* When we get within this number of ring entries of filling the
	 * entire ring on tx, place an EXPLICIT intr to generate a lowat
	 * notification when output has drained.
	 */
	port->ip_tx_lowat = (TX_LOWAT_CHARS(baud) + 3) / 4;
	if (port->ip_tx_lowat == 0)
		port->ip_tx_lowat = 1;

	set_rx_timeout(port, 2);

	return 0;
}

/**
 * do_write - Write bytes to the port.  Returns the number of bytes
 *			actually written. Called from transmit_chars
 * @port: port to use
 * @buf: the stuff to write
 * @len: how many bytes in 'buf'
 */
static inline int do_write(struct ioc4_port *port, char *buf, int len)
{
	int prod_ptr, cons_ptr, total = 0;
	struct ring *outring;
	struct ring_entry *entry;
	struct hooks *hooks = port->ip_hooks;

	BUG_ON(!(len >= 0));

	prod_ptr = port->ip_tx_prod;
	cons_ptr = readl(&port->ip_serial_regs->stcir) & PROD_CONS_MASK;
	outring = port->ip_outring;

	/* Maintain a 1-entry red-zone.  The ring buffer is full when
	 * (cons - prod) % ring_size is 1.  Rather than do this subtraction
	 * in the body of the loop, I'll do it now.
	 */
	cons_ptr = (cons_ptr - (int)sizeof(struct ring_entry)) & PROD_CONS_MASK;

	/* Stuff the bytes into the output */
	while ((prod_ptr != cons_ptr) && (len > 0)) {
		int xx;

		/* Get 4 bytes (one ring entry) at a time */
		entry = (struct ring_entry *)((caddr_t) outring + prod_ptr);

		/* Invalidate all entries */
		entry->ring_allsc = 0;

		/* Copy in some bytes */
		for (xx = 0; (xx < 4) && (len > 0); xx++) {
			entry->ring_data[xx] = *buf++;
			entry->ring_sc[xx] = IOC4_TXCB_VALID;
			len--;
			total++;
		}

		/* If we are within some small threshold of filling up the
		 * entire ring buffer, we must place an EXPLICIT intr here
		 * to generate a lowat interrupt in case we subsequently
		 * really do fill up the ring and the caller goes to sleep.
		 * No need to place more than one though.
		 */
		if (!(port->ip_flags & LOWAT_WRITTEN) &&
			((cons_ptr - prod_ptr) & PROD_CONS_MASK)
				<= port->ip_tx_lowat
					* (int)sizeof(struct ring_entry)) {
			port->ip_flags |= LOWAT_WRITTEN;
			entry->ring_sc[0] |= IOC4_TXCB_INT_WHEN_DONE;
		}

		/* Go on to next entry */
		prod_ptr += sizeof(struct ring_entry);
		prod_ptr &= PROD_CONS_MASK;
	}

	/* If we sent something, start DMA if necessary */
	if (total > 0 && !(port->ip_sscr & IOC4_SSCR_DMA_EN)) {
		port->ip_sscr |= IOC4_SSCR_DMA_EN;
		writel(port->ip_sscr, &port->ip_serial_regs->sscr);
	}

	/* Store the new producer pointer.  If tx is disabled, we stuff the
	 * data into the ring buffer, but we don't actually start tx.
	 */
	if (!uart_tx_stopped(port->ip_port)) {
		writel(prod_ptr, &port->ip_serial_regs->stpir);

		/* If we are now transmitting, enable tx_mt interrupt so we
		 * can disable DMA if necessary when the tx finishes.
		 */
		if (total > 0)
			enable_intrs(port, hooks->intr_tx_mt);
	}
	port->ip_tx_prod = prod_ptr;
	return total;
}

/**
 * disable_intrs - disable interrupts
 * @port: port to enable
 * @mask: mask to use
 */
static void disable_intrs(struct ioc4_port *port, uint32_t mask)
{
	struct hooks *hooks = port->ip_hooks;

	if (port->ip_ienb & mask) {
		write_ireg(port->ip_ioc4_soft, mask, IOC4_W_IEC,
					IOC4_SIO_INTR_TYPE);
		port->ip_ienb &= ~mask;
	}

	if (!port->ip_ienb)
		write_ireg(port->ip_ioc4_soft, hooks->intr_dma_error,
				IOC4_W_IEC, IOC4_OTHER_INTR_TYPE);
}

/**
 * set_notification - Modify event notification
 * @port: port to use
 * @mask: events mask
 * @set_on: set ?
 */
static int set_notification(struct ioc4_port *port, int mask, int set_on)
{
	struct hooks *hooks = port->ip_hooks;
	uint32_t intrbits, sscrbits;

	BUG_ON(!mask);

	intrbits = sscrbits = 0;

	if (mask & N_DATA_READY)
		intrbits |= (hooks->intr_rx_timer | hooks->intr_rx_high);
	if (mask & N_OUTPUT_LOWAT)
		intrbits |= hooks->intr_tx_explicit;
	if (mask & N_DDCD) {
		intrbits |= hooks->intr_delta_dcd;
		sscrbits |= IOC4_SSCR_RX_RING_DCD;
	}
	if (mask & N_DCTS)
		intrbits |= hooks->intr_delta_cts;

	if (set_on) {
		enable_intrs(port, intrbits);
		port->ip_notify |= mask;
		port->ip_sscr |= sscrbits;
	} else {
		disable_intrs(port, intrbits);
		port->ip_notify &= ~mask;
		port->ip_sscr &= ~sscrbits;
	}

	/* We require DMA if either DATA_READY or DDCD notification is
	 * currently requested. If neither of these is requested and
	 * there is currently no tx in progress, DMA may be disabled.
	 */
	if (port->ip_notify & (N_DATA_READY | N_DDCD))
		port->ip_sscr |= IOC4_SSCR_DMA_EN;
	else if (!(port->ip_ienb & hooks->intr_tx_mt))
		port->ip_sscr &= ~IOC4_SSCR_DMA_EN;

	writel(port->ip_sscr, &port->ip_serial_regs->sscr);
	return 0;
}

/**
 * set_mcr - set the master control reg
 * @the_port: port to use
 * @mask1: mcr mask
 * @mask2: shadow mask
 */
static inline int set_mcr(struct uart_port *the_port,
		int mask1, int mask2)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	uint32_t shadow;
	int spiniter = 0;
	char mcr;

	if (!port)
		return -1;

	/* Pause the DMA interface if necessary */
	if (port->ip_sscr & IOC4_SSCR_DMA_EN) {
		writel(port->ip_sscr | IOC4_SSCR_DMA_PAUSE,
			&port->ip_serial_regs->sscr);
		while ((readl(&port->ip_serial_regs->sscr)
					& IOC4_SSCR_PAUSE_STATE) == 0) {
			spiniter++;
			if (spiniter > MAXITER)
				return -1;
		}
	}
	shadow = readl(&port->ip_serial_regs->shadow);
	mcr = (shadow & 0xff000000) >> 24;

	/* Set new value */
	mcr |= mask1;
	shadow |= mask2;

	writeb(mcr, &port->ip_uart_regs->i4u_mcr);
	writel(shadow, &port->ip_serial_regs->shadow);

	/* Re-enable the DMA interface if necessary */
	if (port->ip_sscr & IOC4_SSCR_DMA_EN) {
		writel(port->ip_sscr, &port->ip_serial_regs->sscr);
	}
	return 0;
}

/**
 * ioc4_set_proto - set the protocol for the port
 * @port: port to use
 * @proto: protocol to use
 */
static int ioc4_set_proto(struct ioc4_port *port, int proto)
{
	struct hooks *hooks = port->ip_hooks;

	switch (proto) {
	case PROTO_RS232:
		/* Clear the appropriate GIO pin */
		writel(0, (&port->ip_mem->gppr[hooks->rs422_select_pin].raw));
		break;

	case PROTO_RS422:
		/* Set the appropriate GIO pin */
		writel(1, (&port->ip_mem->gppr[hooks->rs422_select_pin].raw));
		break;

	default:
		return 1;
	}
	return 0;
}

/**
 * transmit_chars - upper level write, called with ip_lock
 * @the_port: port to write
 */
static void transmit_chars(struct uart_port *the_port)
{
	int xmit_count, tail, head;
	int result;
	char *start;
	struct tty_struct *tty;
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	struct uart_state *state;

	if (!the_port)
		return;
	if (!port)
		return;

	state = the_port->state;
	tty = state->port.tty;

	if (uart_circ_empty(&state->xmit) || uart_tx_stopped(the_port)) {
		/* Nothing to do or hw stopped */
		set_notification(port, N_ALL_OUTPUT, 0);
		return;
	}

	head = state->xmit.head;
	tail = state->xmit.tail;
	start = (char *)&state->xmit.buf[tail];

	/* write out all the data or until the end of the buffer */
	xmit_count = (head < tail) ? (UART_XMIT_SIZE - tail) : (head - tail);
	if (xmit_count > 0) {
		result = do_write(port, start, xmit_count);
		if (result > 0) {
			/* booking */
			xmit_count -= result;
			the_port->icount.tx += result;
			/* advance the pointers */
			tail += result;
			tail &= UART_XMIT_SIZE - 1;
			state->xmit.tail = tail;
			start = (char *)&state->xmit.buf[tail];
		}
	}
	if (uart_circ_chars_pending(&state->xmit) < WAKEUP_CHARS)
		uart_write_wakeup(the_port);

	if (uart_circ_empty(&state->xmit)) {
		set_notification(port, N_OUTPUT_LOWAT, 0);
	} else {
		set_notification(port, N_OUTPUT_LOWAT, 1);
	}
}

/**
 * ioc4_change_speed - change the speed of the port
 * @the_port: port to change
 * @new_termios: new termios settings
 * @old_termios: old termios settings
 */
static void
ioc4_change_speed(struct uart_port *the_port,
		  struct ktermios *new_termios, struct ktermios *old_termios)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	int baud, bits;
	unsigned cflag, iflag;
	int new_parity = 0, new_parity_enable = 0, new_stop = 0, new_data = 8;
	struct uart_state *state = the_port->state;

	cflag = new_termios->c_cflag;
	iflag = new_termios->c_iflag;

	switch (cflag & CSIZE) {
	case CS5:
		new_data = 5;
		bits = 7;
		break;
	case CS6:
		new_data = 6;
		bits = 8;
		break;
	case CS7:
		new_data = 7;
		bits = 9;
		break;
	case CS8:
		new_data = 8;
		bits = 10;
		break;
	default:
		/* cuz we always need a default ... */
		new_data = 5;
		bits = 7;
		break;
	}
	if (cflag & CSTOPB) {
		bits++;
		new_stop = 1;
	}
	if (cflag & PARENB) {
		bits++;
		new_parity_enable = 1;
		if (cflag & PARODD)
			new_parity = 1;
	}
	baud = uart_get_baud_rate(the_port, new_termios, old_termios,
				MIN_BAUD_SUPPORTED, MAX_BAUD_SUPPORTED);
	DPRINT_CONFIG(("%s: returned baud %d\n", __func__, baud));

	/* default is 9600 */
	if (!baud)
		baud = 9600;

	if (!the_port->fifosize)
		the_port->fifosize = IOC4_FIFO_CHARS;
	the_port->timeout = ((the_port->fifosize * HZ * bits) / (baud / 10));
	the_port->timeout += HZ / 50;	/* Add .02 seconds of slop */

	the_port->ignore_status_mask = N_ALL_INPUT;

	state->port.low_latency = 1;

	if (iflag & IGNPAR)
		the_port->ignore_status_mask &= ~(N_PARITY_ERROR
						| N_FRAMING_ERROR);
	if (iflag & IGNBRK) {
		the_port->ignore_status_mask &= ~N_BREAK;
		if (iflag & IGNPAR)
			the_port->ignore_status_mask &= ~N_OVERRUN_ERROR;
	}
	if (!(cflag & CREAD)) {
		/* ignore everything */
		the_port->ignore_status_mask &= ~N_DATA_READY;
	}

	if (cflag & CRTSCTS) {
		port->ip_sscr |= IOC4_SSCR_HFC_EN;
	}
	else {
		port->ip_sscr &= ~IOC4_SSCR_HFC_EN;
	}
	writel(port->ip_sscr, &port->ip_serial_regs->sscr);

	/* Set the configuration and proper notification call */
	DPRINT_CONFIG(("%s : port 0x%p cflag 0%o "
		"config_port(baud %d data %d stop %d p enable %d parity %d),"
		" notification 0x%x\n",
	     __func__, (void *)port, cflag, baud, new_data, new_stop,
	     new_parity_enable, new_parity, the_port->ignore_status_mask));

	if ((config_port(port, baud,		/* baud */
			 new_data,		/* byte size */
			 new_stop,		/* stop bits */
			 new_parity_enable,	/* set parity */
			 new_parity)) >= 0) {	/* parity 1==odd */
		set_notification(port, the_port->ignore_status_mask, 1);
	}
}

/**
 * ic4_startup_local - Start up the serial port - returns >= 0 if no errors
 * @the_port: Port to operate on
 */
static inline int ic4_startup_local(struct uart_port *the_port)
{
	struct ioc4_port *port;
	struct uart_state *state;

	if (!the_port)
		return -1;

	port = get_ioc4_port(the_port, 0);
	if (!port)
		return -1;

	state = the_port->state;

	local_open(port);

	/* set the protocol - mapbase has the port type */
	ioc4_set_proto(port, the_port->mapbase);

	/* set the speed of the serial port */
	ioc4_change_speed(the_port, &state->port.tty->termios,
			  (struct ktermios *)0);

	return 0;
}

/*
 * ioc4_cb_output_lowat - called when the output low water mark is hit
 * @the_port: port to output
 */
static void ioc4_cb_output_lowat(struct uart_port *the_port)
{
	unsigned long pflags;

	/* ip_lock is set on the call here */
	if (the_port) {
		spin_lock_irqsave(&the_port->lock, pflags);
		transmit_chars(the_port);
		spin_unlock_irqrestore(&the_port->lock, pflags);
	}
}

/**
 * handle_intr - service any interrupts for the given port - 2nd level
 *			called via sd_intr
 * @arg: handler arg
 * @sio_ir: ioc4regs
 */
static void handle_intr(void *arg, uint32_t sio_ir)
{
	struct ioc4_port *port = (struct ioc4_port *)arg;
	struct hooks *hooks = port->ip_hooks;
	unsigned int rx_high_rd_aborted = 0;
	unsigned long flags;
	struct uart_port *the_port;
	int loop_counter;

	/* Possible race condition here: The tx_mt interrupt bit may be
	 * cleared without the intervention of the interrupt handler,
	 * e.g. by a write.  If the top level interrupt handler reads a
	 * tx_mt, then some other processor does a write, starting up
	 * output, then we come in here, see the tx_mt and stop DMA, the
	 * output started by the other processor will hang.  Thus we can
	 * only rely on tx_mt being legitimate if it is read while the
	 * port lock is held.  Therefore this bit must be ignored in the
	 * passed in interrupt mask which was read by the top level
	 * interrupt handler since the port lock was not held at the time
	 * it was read.  We can only rely on this bit being accurate if it
	 * is read while the port lock is held.  So we'll clear it for now,
	 * and reload it later once we have the port lock.
	 */
	sio_ir &= ~(hooks->intr_tx_mt);

	spin_lock_irqsave(&port->ip_lock, flags);

	loop_counter = MAXITER;	/* to avoid hangs */

	do {
		uint32_t shadow;

		if ( loop_counter-- <= 0 ) {
			printk(KERN_WARNING "IOC4 serial: "
					"possible hang condition/"
					"port stuck on interrupt.\n");
			break;
		}

		/* Handle a DCD change */
		if (sio_ir & hooks->intr_delta_dcd) {
			/* ACK the interrupt */
			writel(hooks->intr_delta_dcd,
				&port->ip_mem->sio_ir.raw);

			shadow = readl(&port->ip_serial_regs->shadow);

			if ((port->ip_notify & N_DDCD)
					&& (shadow & IOC4_SHADOW_DCD)
					&& (port->ip_port)) {
				the_port = port->ip_port;
				the_port->icount.dcd = 1;
				wake_up_interruptible
					    (&the_port->state->port.delta_msr_wait);
			} else if ((port->ip_notify & N_DDCD)
					&& !(shadow & IOC4_SHADOW_DCD)) {
				/* Flag delta DCD/no DCD */
				port->ip_flags |= DCD_ON;
			}
		}

		/* Handle a CTS change */
		if (sio_ir & hooks->intr_delta_cts) {
			/* ACK the interrupt */
			writel(hooks->intr_delta_cts,
					&port->ip_mem->sio_ir.raw);

			shadow = readl(&port->ip_serial_regs->shadow);

			if ((port->ip_notify & N_DCTS)
					&& (port->ip_port)) {
				the_port = port->ip_port;
				the_port->icount.cts =
					(shadow & IOC4_SHADOW_CTS) ? 1 : 0;
				wake_up_interruptible
					(&the_port->state->port.delta_msr_wait);
			}
		}

		/* rx timeout interrupt.  Must be some data available.  Put this
		 * before the check for rx_high since servicing this condition
		 * may cause that condition to clear.
		 */
		if (sio_ir & hooks->intr_rx_timer) {
			/* ACK the interrupt */
			writel(hooks->intr_rx_timer,
				&port->ip_mem->sio_ir.raw);

			if ((port->ip_notify & N_DATA_READY)
					&& (port->ip_port)) {
				/* ip_lock is set on call here */
				receive_chars(port->ip_port);
			}
		}

		/* rx high interrupt. Must be after rx_timer.  */
		else if (sio_ir & hooks->intr_rx_high) {
			/* Data available, notify upper layer */
			if ((port->ip_notify & N_DATA_READY)
						&& port->ip_port) {
				/* ip_lock is set on call here */
				receive_chars(port->ip_port);
			}

			/* We can't ACK this interrupt.  If receive_chars didn't
			 * cause the condition to clear, we'll have to disable
			 * the interrupt until the data is drained.
			 * If the read was aborted, don't disable the interrupt
			 * as this may cause us to hang indefinitely.  An
			 * aborted read generally means that this interrupt
			 * hasn't been delivered to the cpu yet anyway, even
			 * though we see it as asserted when we read the sio_ir.
			 */
			if ((sio_ir = PENDING(port)) & hooks->intr_rx_high) {
				if ((port->ip_flags & READ_ABORTED) == 0) {
					port->ip_ienb &= ~hooks->intr_rx_high;
					port->ip_flags |= INPUT_HIGH;
				} else {
					rx_high_rd_aborted++;
				}
			}
		}

		/* We got a low water interrupt: notify upper layer to
		 * send more data.  Must come before tx_mt since servicing
		 * this condition may cause that condition to clear.
		 */
		if (sio_ir & hooks->intr_tx_explicit) {
			port->ip_flags &= ~LOWAT_WRITTEN;

			/* ACK the interrupt */
			writel(hooks->intr_tx_explicit,
					&port->ip_mem->sio_ir.raw);

			if (port->ip_notify & N_OUTPUT_LOWAT)
				ioc4_cb_output_lowat(port->ip_port);
		}

		/* Handle tx_mt.  Must come after tx_explicit.  */
		else if (sio_ir & hooks->intr_tx_mt) {
			/* If we are expecting a lowat notification
			 * and we get to this point it probably means that for
			 * some reason the tx_explicit didn't work as expected
			 * (that can legitimately happen if the output buffer is
			 * filled up in just the right way).
			 * So send the notification now.
			 */
			if (port->ip_notify & N_OUTPUT_LOWAT) {
				ioc4_cb_output_lowat(port->ip_port);

				/* We need to reload the sio_ir since the lowat
				 * call may have caused another write to occur,
				 * clearing the tx_mt condition.
				 */
				sio_ir = PENDING(port);
			}

			/* If the tx_mt condition still persists even after the
			 * lowat call, we've got some work to do.
			 */
			if (sio_ir & hooks->intr_tx_mt) {

				/* If we are not currently expecting DMA input,
				 * and the transmitter has just gone idle,
				 * there is no longer any reason for DMA, so
				 * disable it.
				 */
				if (!(port->ip_notify
						& (N_DATA_READY | N_DDCD))) {
					BUG_ON(!(port->ip_sscr
							& IOC4_SSCR_DMA_EN));
					port->ip_sscr &= ~IOC4_SSCR_DMA_EN;
					writel(port->ip_sscr,
					   &port->ip_serial_regs->sscr);
				}

				/* Prevent infinite tx_mt interrupt */
				port->ip_ienb &= ~hooks->intr_tx_mt;
			}
		}
		sio_ir = PENDING(port);

		/* if the read was aborted and only hooks->intr_rx_high,
		 * clear hooks->intr_rx_high, so we do not loop forever.
		 */

		if (rx_high_rd_aborted && (sio_ir == hooks->intr_rx_high)) {
			sio_ir &= ~hooks->intr_rx_high;
		}
	} while (sio_ir & hooks->intr_all);

	spin_unlock_irqrestore(&port->ip_lock, flags);

	/* Re-enable interrupts before returning from interrupt handler.
	 * Getting interrupted here is okay.  It'll just v() our semaphore, and
	 * we'll come through the loop again.
	 */

	write_ireg(port->ip_ioc4_soft, port->ip_ienb, IOC4_W_IES,
							IOC4_SIO_INTR_TYPE);
}

/*
 * ioc4_cb_post_ncs - called for some basic errors
 * @port: port to use
 * @ncs: event
 */
static void ioc4_cb_post_ncs(struct uart_port *the_port, int ncs)
{
	struct uart_icount *icount;

	icount = &the_port->icount;

	if (ncs & NCS_BREAK)
		icount->brk++;
	if (ncs & NCS_FRAMING)
		icount->frame++;
	if (ncs & NCS_OVERRUN)
		icount->overrun++;
	if (ncs & NCS_PARITY)
		icount->parity++;
}

/**
 * do_read - Read in bytes from the port.  Return the number of bytes
 *			actually read.
 * @the_port: port to use
 * @buf: place to put the stuff we read
 * @len: how big 'buf' is
 */

static inline int do_read(struct uart_port *the_port, unsigned char *buf,
				int len)
{
	int prod_ptr, cons_ptr, total;
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	struct ring *inring;
	struct ring_entry *entry;
	struct hooks *hooks;
	int byte_num;
	char *sc;
	int loop_counter;

	BUG_ON(!(len >= 0));
	BUG_ON(!port);
	hooks = port->ip_hooks;

	/* There is a nasty timing issue in the IOC4. When the rx_timer
	 * expires or the rx_high condition arises, we take an interrupt.
	 * At some point while servicing the interrupt, we read bytes from
	 * the ring buffer and re-arm the rx_timer.  However the rx_timer is
	 * not started until the first byte is received *after* it is armed,
	 * and any bytes pending in the rx construction buffers are not drained
	 * to memory until either there are 4 bytes available or the rx_timer
	 * expires.  This leads to a potential situation where data is left
	 * in the construction buffers forever - 1 to 3 bytes were received
	 * after the interrupt was generated but before the rx_timer was
	 * re-armed. At that point as long as no subsequent bytes are received
	 * the timer will never be started and the bytes will remain in the
	 * construction buffer forever.  The solution is to execute a DRAIN
	 * command after rearming the timer.  This way any bytes received before
	 * the DRAIN will be drained to memory, and any bytes received after
	 * the DRAIN will start the TIMER and be drained when it expires.
	 * Luckily, this only needs to be done when the DMA buffer is empty
	 * since there is no requirement that this function return all
	 * available data as long as it returns some.
	 */
	/* Re-arm the timer */
	writel(port->ip_rx_cons | IOC4_SRCIR_ARM, &port->ip_serial_regs->srcir);

	prod_ptr = readl(&port->ip_serial_regs->srpir) & PROD_CONS_MASK;
	cons_ptr = port->ip_rx_cons;

	if (prod_ptr == cons_ptr) {
		int reset_dma = 0;

		/* Input buffer appears empty, do a flush. */

		/* DMA must be enabled for this to work. */
		if (!(port->ip_sscr & IOC4_SSCR_DMA_EN)) {
			port->ip_sscr |= IOC4_SSCR_DMA_EN;
			reset_dma = 1;
		}

		/* Potential race condition: we must reload the srpir after
		 * issuing the drain command, otherwise we could think the rx
		 * buffer is empty, then take a very long interrupt, and when
		 * we come back it's full and we wait forever for the drain to
		 * complete.
		 */
		writel(port->ip_sscr | IOC4_SSCR_RX_DRAIN,
				&port->ip_serial_regs->sscr);
		prod_ptr = readl(&port->ip_serial_regs->srpir)
				& PROD_CONS_MASK;

		/* We must not wait for the DRAIN to complete unless there are
		 * at least 8 bytes (2 ring entries) available to receive the
		 * data otherwise the DRAIN will never complete and we'll
		 * deadlock here.
		 * In fact, to make things easier, I'll just ignore the flush if
		 * there is any data at all now available.
		 */
		if (prod_ptr == cons_ptr) {
			loop_counter = 0;
			while (readl(&port->ip_serial_regs->sscr) &
						IOC4_SSCR_RX_DRAIN) {
				loop_counter++;
				if (loop_counter > MAXITER)
					return -1;
			}

			/* SIGH. We have to reload the prod_ptr *again* since
			 * the drain may have caused it to change
			 */
			prod_ptr = readl(&port->ip_serial_regs->srpir)
							& PROD_CONS_MASK;
		}
		if (reset_dma) {
			port->ip_sscr &= ~IOC4_SSCR_DMA_EN;
			writel(port->ip_sscr, &port->ip_serial_regs->sscr);
		}
	}
	inring = port->ip_inring;
	port->ip_flags &= ~READ_ABORTED;

	total = 0;
	loop_counter = 0xfffff;	/* to avoid hangs */

	/* Grab bytes from the hardware */
	while ((prod_ptr != cons_ptr) && (len > 0)) {
		entry = (struct ring_entry *)((caddr_t)inring + cons_ptr);

		if ( loop_counter-- <= 0 ) {
			printk(KERN_WARNING "IOC4 serial: "
					"possible hang condition/"
					"port stuck on read.\n");
			break;
		}

		/* According to the producer pointer, this ring entry
		 * must contain some data.  But if the PIO happened faster
		 * than the DMA, the data may not be available yet, so let's
		 * wait until it arrives.
		 */
		if ((entry->ring_allsc & RING_ANY_VALID) == 0) {
			/* Indicate the read is aborted so we don't disable
			 * the interrupt thinking that the consumer is
			 * congested.
			 */
			port->ip_flags |= READ_ABORTED;
			len = 0;
			break;
		}

		/* Load the bytes/status out of the ring entry */
		for (byte_num = 0; byte_num < 4 && len > 0; byte_num++) {
			sc = &(entry->ring_sc[byte_num]);

			/* Check for change in modem state or overrun */
			if ((*sc & IOC4_RXSB_MODEM_VALID)
						&& (port->ip_notify & N_DDCD)) {
				/* Notify upper layer if DCD dropped */

				if ((port->ip_flags & DCD_ON)
						&& !(*sc & IOC4_RXSB_DCD)) {

					/* If we have already copied some data,
					 * return it.  We'll pick up the carrier
					 * drop on the next pass.  That way we
					 * don't throw away the data that has
					 * already been copied back to
					 * the caller's buffer.
					 */
					if (total > 0) {
						len = 0;
						break;
					}
					port->ip_flags &= ~DCD_ON;

					/* Turn off this notification so the
					 * carrier drop protocol won't see it
					 * again when it does a read.
					 */
					*sc &= ~IOC4_RXSB_MODEM_VALID;

					/* To keep things consistent, we need
					 * to update the consumer pointer so
					 * the next reader won't come in and
					 * try to read the same ring entries
					 * again. This must be done here before
					 * the dcd change.
					 */

					if ((entry->ring_allsc & RING_ANY_VALID)
									== 0) {
						cons_ptr += (int)sizeof
							(struct ring_entry);
						cons_ptr &= PROD_CONS_MASK;
					}
					writel(cons_ptr,
						&port->ip_serial_regs->srcir);
					port->ip_rx_cons = cons_ptr;

					/* Notify upper layer of carrier drop */
					if ((port->ip_notify & N_DDCD)
						   && port->ip_port) {
						the_port->icount.dcd = 0;
						wake_up_interruptible
						    (&the_port->state->
							port.delta_msr_wait);
					}

					/* If we had any data to return, we
					 * would have returned it above.
					 */
					return 0;
				}
			}
			if (*sc & IOC4_RXSB_MODEM_VALID) {
				/* Notify that an input overrun occurred */
				if ((*sc & IOC4_RXSB_OVERRUN)
				    && (port->ip_notify & N_OVERRUN_ERROR)) {
					ioc4_cb_post_ncs(the_port, NCS_OVERRUN);
				}
				/* Don't look at this byte again */
				*sc &= ~IOC4_RXSB_MODEM_VALID;
			}

			/* Check for valid data or RX errors */
			if ((*sc & IOC4_RXSB_DATA_VALID) &&
					((*sc & (IOC4_RXSB_PAR_ERR
							| IOC4_RXSB_FRAME_ERR
							| IOC4_RXSB_BREAK))
					&& (port->ip_notify & (N_PARITY_ERROR
							| N_FRAMING_ERROR
							| N_BREAK)))) {
				/* There is an error condition on the next byte.
				 * If we have already transferred some bytes,
				 * we'll stop here. Otherwise if this is the
				 * first byte to be read, we'll just transfer
				 * it alone after notifying the
				 * upper layer of its status.
				 */
				if (total > 0) {
					len = 0;
					break;
				} else {
					if ((*sc & IOC4_RXSB_PAR_ERR) &&
					   (port->ip_notify & N_PARITY_ERROR)) {
						ioc4_cb_post_ncs(the_port,
								NCS_PARITY);
					}
					if ((*sc & IOC4_RXSB_FRAME_ERR) &&
					   (port->ip_notify & N_FRAMING_ERROR)){
						ioc4_cb_post_ncs(the_port,
								NCS_FRAMING);
					}
					if ((*sc & IOC4_RXSB_BREAK)
					    && (port->ip_notify & N_BREAK)) {
							ioc4_cb_post_ncs
								    (the_port,
								     NCS_BREAK);
					}
					len = 1;
				}
			}
			if (*sc & IOC4_RXSB_DATA_VALID) {
				*sc &= ~IOC4_RXSB_DATA_VALID;
				*buf = entry->ring_data[byte_num];
				buf++;
				len--;
				total++;
			}
		}

		/* If we used up this entry entirely, go on to the next one,
		 * otherwise we must have run out of buffer space, so
		 * leave the consumer pointer here for the next read in case
		 * there are still unread bytes in this entry.
		 */
		if ((entry->ring_allsc & RING_ANY_VALID) == 0) {
			cons_ptr += (int)sizeof(struct ring_entry);
			cons_ptr &= PROD_CONS_MASK;
		}
	}

	/* Update consumer pointer and re-arm rx timer interrupt */
	writel(cons_ptr, &port->ip_serial_regs->srcir);
	port->ip_rx_cons = cons_ptr;

	/* If we have now dipped below the rx high water mark and we have
	 * rx_high interrupt turned off, we can now turn it back on again.
	 */
	if ((port->ip_flags & INPUT_HIGH) && (((prod_ptr - cons_ptr)
			& PROD_CONS_MASK) < ((port->ip_sscr &
				IOC4_SSCR_RX_THRESHOLD)
					<< IOC4_PROD_CONS_PTR_OFF))) {
		port->ip_flags &= ~INPUT_HIGH;
		enable_intrs(port, hooks->intr_rx_high);
	}
	return total;
}

/**
 * receive_chars - upper level read. Called with ip_lock.
 * @the_port: port to read from
 */
static void receive_chars(struct uart_port *the_port)
{
	unsigned char ch[IOC4_MAX_CHARS];
	int read_count, request_count = IOC4_MAX_CHARS;
	struct uart_icount *icount;
	struct uart_state *state = the_port->state;
	unsigned long pflags;

	/* Make sure all the pointers are "good" ones */
	if (!state)
		return;

	spin_lock_irqsave(&the_port->lock, pflags);

	request_count = tty_buffer_request_room(&state->port, IOC4_MAX_CHARS);

	if (request_count > 0) {
		icount = &the_port->icount;
		read_count = do_read(the_port, ch, request_count);
		if (read_count > 0) {
			tty_insert_flip_string(&state->port, ch, read_count);
			icount->rx += read_count;
		}
	}

	spin_unlock_irqrestore(&the_port->lock, pflags);

	tty_flip_buffer_push(&state->port);
}

/**
 * ic4_type - What type of console are we?
 * @port: Port to operate with (we ignore since we only have one port)
 *
 */
static const char *ic4_type(struct uart_port *the_port)
{
	if (the_port->mapbase == PROTO_RS232)
		return "SGI IOC4 Serial [rs232]";
	else
		return "SGI IOC4 Serial [rs422]";
}

/**
 * ic4_tx_empty - Is the transmitter empty?
 * @port: Port to operate on
 *
 */
static unsigned int ic4_tx_empty(struct uart_port *the_port)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	unsigned int ret = 0;

	if (port_is_active(port, the_port)) {
		if (readl(&port->ip_serial_regs->shadow) & IOC4_SHADOW_TEMT)
			ret = TIOCSER_TEMT;
	}
	return ret;
}

/**
 * ic4_stop_tx - stop the transmitter
 * @port: Port to operate on
 *
 */
static void ic4_stop_tx(struct uart_port *the_port)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);

	if (port_is_active(port, the_port))
		set_notification(port, N_OUTPUT_LOWAT, 0);
}

/**
 * null_void_function -
 * @port: Port to operate on
 *
 */
static void null_void_function(struct uart_port *the_port)
{
}

/**
 * ic4_shutdown - shut down the port - free irq and disable
 * @port: Port to shut down
 *
 */
static void ic4_shutdown(struct uart_port *the_port)
{
	unsigned long port_flags;
	struct ioc4_port *port;
	struct uart_state *state;

	port = get_ioc4_port(the_port, 0);
	if (!port)
		return;

	state = the_port->state;
	port->ip_port = NULL;

	wake_up_interruptible(&state->port.delta_msr_wait);

	if (state->port.tty)
		set_bit(TTY_IO_ERROR, &state->port.tty->flags);

	spin_lock_irqsave(&the_port->lock, port_flags);
	set_notification(port, N_ALL, 0);
	port->ip_flags = PORT_INACTIVE;
	spin_unlock_irqrestore(&the_port->lock, port_flags);
}

/**
 * ic4_set_mctrl - set control lines (dtr, rts, etc)
 * @port: Port to operate on
 * @mctrl: Lines to set/unset
 *
 */
static void ic4_set_mctrl(struct uart_port *the_port, unsigned int mctrl)
{
	unsigned char mcr = 0;
	struct ioc4_port *port;

	port = get_ioc4_port(the_port, 0);
	if (!port_is_active(port, the_port))
		return;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	set_mcr(the_port, mcr, IOC4_SHADOW_DTR);
}

/**
 * ic4_get_mctrl - get control line info
 * @port: port to operate on
 *
 */
static unsigned int ic4_get_mctrl(struct uart_port *the_port)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);
	uint32_t shadow;
	unsigned int ret = 0;

	if (!port_is_active(port, the_port))
		return 0;

	shadow = readl(&port->ip_serial_regs->shadow);
	if (shadow & IOC4_SHADOW_DCD)
		ret |= TIOCM_CAR;
	if (shadow & IOC4_SHADOW_DR)
		ret |= TIOCM_DSR;
	if (shadow & IOC4_SHADOW_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

/**
 * ic4_start_tx - Start transmitter, flush any output
 * @port: Port to operate on
 *
 */
static void ic4_start_tx(struct uart_port *the_port)
{
	struct ioc4_port *port = get_ioc4_port(the_port, 0);

	if (port_is_active(port, the_port)) {
		set_notification(port, N_OUTPUT_LOWAT, 1);
		enable_intrs(port, port->ip_hooks->intr_tx_mt);
	}
}

/**
 * ic4_break_ctl - handle breaks
 * @port: Port to operate on
 * @break_state: Break state
 *
 */
static void ic4_break_ctl(struct uart_port *the_port, int break_state)
{
}

/**
 * ic4_startup - Start up the serial port
 * @port: Port to operate on
 *
 */
static int ic4_startup(struct uart_port *the_port)
{
	int retval;
	struct ioc4_port *port;
	struct ioc4_control *control;
	struct uart_state *state;
	unsigned long port_flags;

	if (!the_port)
		return -ENODEV;
	port = get_ioc4_port(the_port, 1);
	if (!port)
		return -ENODEV;
	state = the_port->state;

	control = port->ip_control;
	if (!control) {
		port->ip_port = NULL;
		return -ENODEV;
	}

	/* Start up the serial port */
	spin_lock_irqsave(&the_port->lock, port_flags);
	retval = ic4_startup_local(the_port);
	spin_unlock_irqrestore(&the_port->lock, port_flags);
	return retval;
}

/**
 * ic4_set_termios - set termios stuff
 * @port: port to operate on
 * @termios: New settings
 * @termios: Old
 *
 */
static void
ic4_set_termios(struct uart_port *the_port,
		struct ktermios *termios, struct ktermios *old_termios)
{
	unsigned long port_flags;

	spin_lock_irqsave(&the_port->lock, port_flags);
	ioc4_change_speed(the_port, termios, old_termios);
	spin_unlock_irqrestore(&the_port->lock, port_flags);
}

/**
 * ic4_request_port - allocate resources for port - no op....
 * @port: port to operate on
 *
 */
static int ic4_request_port(struct uart_port *port)
{
	return 0;
}

/* Associate the uart functions above - given to serial core */

static const struct uart_ops ioc4_ops = {
	.tx_empty	= ic4_tx_empty,
	.set_mctrl	= ic4_set_mctrl,
	.get_mctrl	= ic4_get_mctrl,
	.stop_tx	= ic4_stop_tx,
	.start_tx	= ic4_start_tx,
	.stop_rx	= null_void_function,
	.break_ctl	= ic4_break_ctl,
	.startup	= ic4_startup,
	.shutdown	= ic4_shutdown,
	.set_termios	= ic4_set_termios,
	.type		= ic4_type,
	.release_port	= null_void_function,
	.request_port	= ic4_request_port,
};

/*
 * Boot-time initialization code
 */

static struct uart_driver ioc4_uart_rs232 = {
	.owner		= THIS_MODULE,
	.driver_name	= "ioc4_serial_rs232",
	.dev_name	= DEVICE_NAME_RS232,
	.major		= DEVICE_MAJOR,
	.minor		= DEVICE_MINOR_RS232,
	.nr		= IOC4_NUM_CARDS * IOC4_NUM_SERIAL_PORTS,
};

static struct uart_driver ioc4_uart_rs422 = {
	.owner		= THIS_MODULE,
	.driver_name	= "ioc4_serial_rs422",
	.dev_name	= DEVICE_NAME_RS422,
	.major		= DEVICE_MAJOR,
	.minor		= DEVICE_MINOR_RS422,
	.nr		= IOC4_NUM_CARDS * IOC4_NUM_SERIAL_PORTS,
};


/**
 * ioc4_serial_remove_one - detach function
 *
 * @idd: IOC4 master module data for this IOC4
 */

static int ioc4_serial_remove_one(struct ioc4_driver_data *idd)
{
	int port_num, port_type;
	struct ioc4_control *control;
	struct uart_port *the_port;
	struct ioc4_port *port;
	struct ioc4_soft *soft;

	/* If serial driver did not attach, don't try to detach */
	control = idd->idd_serial_data;
	if (!control)
		return 0;

	for (port_num = 0; port_num < IOC4_NUM_SERIAL_PORTS; port_num++) {
		for (port_type = UART_PORT_MIN;
					port_type < UART_PORT_COUNT;
					port_type++) {
			the_port = &control->ic_port[port_num].icp_uart_port
							[port_type];
			if (the_port) {
				switch (port_type) {
				case UART_PORT_RS422:
					uart_remove_one_port(&ioc4_uart_rs422,
							the_port);
					break;
				default:
				case UART_PORT_RS232:
					uart_remove_one_port(&ioc4_uart_rs232,
							the_port);
					break;
				}
			}
		}
		port = control->ic_port[port_num].icp_port;
		/* we allocate in pairs */
		if (!(port_num & 1) && port) {
			pci_free_consistent(port->ip_pdev,
					TOTAL_RING_BUF_SIZE,
					port->ip_cpu_ringbuf,
					port->ip_dma_ringbuf);
			kfree(port);
		}
	}
	soft = control->ic_soft;
	if (soft) {
		free_irq(control->ic_irq, soft);
		if (soft->is_ioc4_serial_addr) {
			iounmap(soft->is_ioc4_serial_addr);
			release_mem_region((unsigned long)
			     soft->is_ioc4_serial_addr,
				sizeof(struct ioc4_serial));
		}
		kfree(soft);
	}
	kfree(control);
	idd->idd_serial_data = NULL;

	return 0;
}


/**
 * ioc4_serial_core_attach_rs232 - register with serial core
 *		This is done during pci probing
 * @pdev: handle for this card
 */
static inline int
ioc4_serial_core_attach(struct pci_dev *pdev, int port_type)
{
	struct ioc4_port *port;
	struct uart_port *the_port;
	struct ioc4_driver_data *idd = pci_get_drvdata(pdev);
	struct ioc4_control *control = idd->idd_serial_data;
	int port_num;
	int port_type_idx;
	struct uart_driver *u_driver;


	DPRINT_CONFIG(("%s: attach pdev 0x%p - control 0x%p\n",
			__func__, pdev, (void *)control));

	if (!control)
		return -ENODEV;

	port_type_idx = (port_type == PROTO_RS232) ? UART_PORT_RS232
						: UART_PORT_RS422;

	u_driver = (port_type == PROTO_RS232)	? &ioc4_uart_rs232
						: &ioc4_uart_rs422;

	/* once around for each port on this card */
	for (port_num = 0; port_num < IOC4_NUM_SERIAL_PORTS; port_num++) {
		the_port = &control->ic_port[port_num].icp_uart_port
							[port_type_idx];
		port = control->ic_port[port_num].icp_port;
		port->ip_all_ports[port_type_idx] = the_port;

		DPRINT_CONFIG(("%s: attach the_port 0x%p / port 0x%p : type %s\n",
				__func__, (void *)the_port,
				(void *)port,
				port_type == PROTO_RS232 ? "rs232" : "rs422"));

		/* membase, iobase and mapbase just need to be non-0 */
		the_port->membase = (unsigned char __iomem *)1;
		the_port->iobase = (pdev->bus->number << 16) |  port_num;
		the_port->line = (Num_of_ioc4_cards << 2) | port_num;
		the_port->mapbase = port_type;
		the_port->type = PORT_16550A;
		the_port->fifosize = IOC4_FIFO_CHARS;
		the_port->ops = &ioc4_ops;
		the_port->irq = control->ic_irq;
		the_port->dev = &pdev->dev;
		spin_lock_init(&the_port->lock);
		if (uart_add_one_port(u_driver, the_port) < 0) {
			printk(KERN_WARNING
		           "%s: unable to add port %d bus %d\n",
			       __func__, the_port->line, pdev->bus->number);
		} else {
			DPRINT_CONFIG(
			    ("IOC4 serial port %d irq = %d, bus %d\n",
			       the_port->line, the_port->irq, pdev->bus->number));
		}
	}
	return 0;
}

/**
 * ioc4_serial_attach_one - register attach function
 *		called per card found from IOC4 master module.
 * @idd: Master module data for this IOC4
 */
static int
ioc4_serial_attach_one(struct ioc4_driver_data *idd)
{
	unsigned long tmp_addr1;
	struct ioc4_serial __iomem *serial;
	struct ioc4_soft *soft;
	struct ioc4_control *control;
	int ret = 0;


	DPRINT_CONFIG(("%s (0x%p, 0x%p)\n", __func__, idd->idd_pdev,
							idd->idd_pci_id));

	/* PCI-RT does not bring out serial connections.
	 * Do not attach to this particular IOC4.
	 */
	if (idd->idd_variant == IOC4_VARIANT_PCI_RT)
		return 0;

	/* request serial registers */
	tmp_addr1 = idd->idd_bar0 + IOC4_SERIAL_OFFSET;

	if (!request_mem_region(tmp_addr1, sizeof(struct ioc4_serial),
					"sioc4_uart")) {
		printk(KERN_WARNING
			"ioc4 (%p): unable to get request region for "
				"uart space\n", (void *)idd->idd_pdev);
		ret = -ENODEV;
		goto out1;
	}
	serial = ioremap(tmp_addr1, sizeof(struct ioc4_serial));
	if (!serial) {
		printk(KERN_WARNING
			 "ioc4 (%p) : unable to remap ioc4 serial register\n",
				(void *)idd->idd_pdev);
		ret = -ENODEV;
		goto out2;
	}
	DPRINT_CONFIG(("%s : mem 0x%p, serial 0x%p\n",
				__func__, (void *)idd->idd_misc_regs,
				(void *)serial));

	/* Get memory for the new card */
	control = kzalloc(sizeof(struct ioc4_control), GFP_KERNEL);

	if (!control) {
		printk(KERN_WARNING "ioc4_attach_one"
		       ": unable to get memory for the IOC4\n");
		ret = -ENOMEM;
		goto out2;
	}
	idd->idd_serial_data = control;

	/* Allocate the soft structure */
	soft = kzalloc(sizeof(struct ioc4_soft), GFP_KERNEL);
	if (!soft) {
		printk(KERN_WARNING
		       "ioc4 (%p): unable to get memory for the soft struct\n",
		       (void *)idd->idd_pdev);
		ret = -ENOMEM;
		goto out3;
	}

	spin_lock_init(&soft->is_ir_lock);
	soft->is_ioc4_misc_addr = idd->idd_misc_regs;
	soft->is_ioc4_serial_addr = serial;

	/* Init the IOC4 */
	writel(0xf << IOC4_SIO_CR_CMD_PULSE_SHIFT,
	       &idd->idd_misc_regs->sio_cr.raw);

	/* Enable serial port mode select generic PIO pins as outputs */
	writel(IOC4_GPCR_UART0_MODESEL | IOC4_GPCR_UART1_MODESEL
		| IOC4_GPCR_UART2_MODESEL | IOC4_GPCR_UART3_MODESEL,
		&idd->idd_misc_regs->gpcr_s.raw);

	/* Clear and disable all serial interrupts */
	write_ireg(soft, ~0, IOC4_W_IEC, IOC4_SIO_INTR_TYPE);
	writel(~0, &idd->idd_misc_regs->sio_ir.raw);
	write_ireg(soft, IOC4_OTHER_IR_SER_MEMERR, IOC4_W_IEC,
		   IOC4_OTHER_INTR_TYPE);
	writel(IOC4_OTHER_IR_SER_MEMERR, &idd->idd_misc_regs->other_ir.raw);
	control->ic_soft = soft;

	/* Hook up interrupt handler */
	if (!request_irq(idd->idd_pdev->irq, ioc4_intr, IRQF_SHARED,
				"sgi-ioc4serial", soft)) {
		control->ic_irq = idd->idd_pdev->irq;
	} else {
		printk(KERN_WARNING
		    "%s : request_irq fails for IRQ 0x%x\n ",
			__func__, idd->idd_pdev->irq);
	}
	ret = ioc4_attach_local(idd);
	if (ret)
		goto out4;

	/* register port with the serial core - 1 rs232, 1 rs422 */

	ret = ioc4_serial_core_attach(idd->idd_pdev, PROTO_RS232);
	if (ret)
		goto out4;

	ret = ioc4_serial_core_attach(idd->idd_pdev, PROTO_RS422);
	if (ret)
		goto out5;

	Num_of_ioc4_cards++;

	return ret;

	/* error exits that give back resources */
out5:
	ioc4_serial_remove_one(idd);
	return ret;
out4:
	kfree(soft);
out3:
	kfree(control);
out2:
	if (serial)
		iounmap(serial);
	release_mem_region(tmp_addr1, sizeof(struct ioc4_serial));
out1:

	return ret;
}


static struct ioc4_submodule ioc4_serial_submodule = {
	.is_name = "IOC4_serial",
	.is_owner = THIS_MODULE,
	.is_probe = ioc4_serial_attach_one,
	.is_remove = ioc4_serial_remove_one,
};

/**
 * ioc4_serial_init - module init
 */
static int __init ioc4_serial_init(void)
{
	int ret;

	/* register with serial core */
	if ((ret = uart_register_driver(&ioc4_uart_rs232)) < 0) {
		printk(KERN_WARNING
			"%s: Couldn't register rs232 IOC4 serial driver\n",
			__func__);
		goto out;
	}
	if ((ret = uart_register_driver(&ioc4_uart_rs422)) < 0) {
		printk(KERN_WARNING
			"%s: Couldn't register rs422 IOC4 serial driver\n",
			__func__);
		goto out_uart_rs232;
	}

	/* register with IOC4 main module */
	ret = ioc4_register_submodule(&ioc4_serial_submodule);
	if (ret)
		goto out_uart_rs422;
	return 0;

out_uart_rs422:
	uart_unregister_driver(&ioc4_uart_rs422);
out_uart_rs232:
	uart_unregister_driver(&ioc4_uart_rs232);
out:
	return ret;
}

static void __exit ioc4_serial_exit(void)
{
	ioc4_unregister_submodule(&ioc4_serial_submodule);
	uart_unregister_driver(&ioc4_uart_rs232);
	uart_unregister_driver(&ioc4_uart_rs422);
}

late_initcall(ioc4_serial_init); /* Call only after tty init is done */
module_exit(ioc4_serial_exit);

MODULE_AUTHOR("Pat Gefre - Silicon Graphics Inc. (SGI) <pfg@sgi.com>");
MODULE_DESCRIPTION("Serial PCI driver module for SGI IOC4 Base-IO Card");
MODULE_LICENSE("GPL");
