#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/tty_driver.h>
#include <linux/pci.h>
#include <linux/circ_buf.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <asm/serial.h>
#include <linux/interrupt.h>


#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/poll.h>


#define MP_TERMIOS  ktermios

#include "sb_mp_register.h"
#include "sb_ser_core.h"

#define DRIVER_VERSION  "1.1"
#define DRIVER_DATE     "2012/01/05"
#define DRIVER_AUTHOR  "SYSTEMBASE<tech@sysbas.com>"
#define DRIVER_DESC  "SystemBase PCI/PCIe Multiport Core"

#define SB_TTY_MP_MAJOR			54
#define PCI_VENDOR_ID_MULTIPORT		0x14A1

#define PCI_DEVICE_ID_MP1		0x4d01
#define PCI_DEVICE_ID_MP2		0x4d02
#define PCI_DEVICE_ID_MP4		0x4d04
#define PCI_DEVICE_ID_MP4A		0x4d54
#define PCI_DEVICE_ID_MP6		0x4d06
#define PCI_DEVICE_ID_MP6A		0x4d56
#define PCI_DEVICE_ID_MP8		0x4d08
#define PCI_DEVICE_ID_MP32		0x4d32
/* Parallel port */
#define PCI_DEVICE_ID_MP1P		0x4301
#define PCI_DEVICE_ID_MP2S1P		0x4303

#define PCIE_DEVICE_ID_MP1		0x4501
#define PCIE_DEVICE_ID_MP2		0x4502
#define PCIE_DEVICE_ID_MP4		0x4504
#define PCIE_DEVICE_ID_MP8		0x4508
#define PCIE_DEVICE_ID_MP32		0x4532

#define PCIE_DEVICE_ID_MP1E		0x4e01
#define PCIE_DEVICE_ID_MP2E		0x4e02
#define PCIE_DEVICE_ID_MP2B		0x4b02
#define PCIE_DEVICE_ID_MP4B		0x4b04
#define PCIE_DEVICE_ID_MP8B		0x4b08

#define PCI_DEVICE_ID_GT_MP4		0x0004
#define PCI_DEVICE_ID_GT_MP4A		0x0054
#define PCI_DEVICE_ID_GT_MP6		0x0006
#define PCI_DEVICE_ID_GT_MP6A		0x0056
#define PCI_DEVICE_ID_GT_MP8		0x0008
#define PCI_DEVICE_ID_GT_MP32		0x0032

#define PCIE_DEVICE_ID_GT_MP1		0x1501
#define PCIE_DEVICE_ID_GT_MP2		0x1502
#define PCIE_DEVICE_ID_GT_MP4		0x1504
#define PCIE_DEVICE_ID_GT_MP8		0x1508
#define PCIE_DEVICE_ID_GT_MP32		0x1532

#define PCI_DEVICE_ID_MP4M		0x4604  //modem

#define MAX_MP_DEV  8
#define BD_MAX_PORT 32 	/* Max serial port in one board */
#define MAX_MP_PORT 256 /* Max serial port in one PC */

#define PORT_16C105XA	3
#define PORT_16C105X	2
#define PORT_16C55X		1

#define ENABLE		1
#define DISABLE		0

/* ioctls */
#define TIOCGNUMOFPORT		0x545F
#define TIOCSMULTIECHO		0x5440
#define TIOCSPTPNOECHO		0x5441

#define TIOCGOPTIONREG		0x5461
#define TIOCGDISABLEIRQ		0x5462
#define TIOCGENABLEIRQ		0x5463
#define TIOCGSOFTRESET		0x5464
#define TIOCGSOFTRESETR		0x5465
#define TIOCGREGINFO		0x5466
#define TIOCGGETLSR		0x5467
#define TIOCGGETDEVID		0x5468
#define TIOCGGETBDNO		0x5469
#define TIOCGGETINTERFACE	0x546A
#define TIOCGGETREV		0x546B
#define TIOCGGETNRPORTS		0x546C
#define TIOCGGETPORTTYPE	0x546D
#define GETDEEPFIFO		0x54AA
#define SETDEEPFIFO		0x54AB
#define SETFCR			0x54BA
#define SETTTR			0x54B1
#define SETRTR			0x54B2
#define GETTTR			0x54B3
#define GETRTR			0x54B4

/* multi-drop mode related ioctl commands */
#define TIOCSMULTIDROP		0x5470
#define TIOCSMDADDR   		0x5471
#define TIOCGMDADDR   		0x5472
#define TIOCSENDADDR		0x5473


/* serial interface */
#define RS232		1 
#define RS422PTP	2
#define RS422MD		3
#define RS485NE		4
#define RS485ECHO	5

#define serial_inp(up, offset)      serial_in(up, offset)
#define serial_outp(up, offset, value)  serial_out(up, offset, value)
	
#define PASS_LIMIT  256
#define is_real_interrupt(irq)  ((irq) != 0)

#define PROBE_ANY   (~0)

static DEFINE_MUTEX(mp_mutex);
#define MP_MUTEX_LOCK(x) mutex_lock(&(x)) 
#define MP_MUTEX_UNLOCK(x) mutex_unlock(&(x)) 
#define MP_STATE_LOCK(x) mutex_lock(&((x)->mutex)) 
#define MP_STATE_UNLOCK(x) mutex_unlock(&((x)->mutex)) 
        

#define UART_LSR_SPECIAL    0x1E
        
#define HIGH_BITS_OFFSET        ((sizeof(long)-sizeof(int))*8)
#define uart_users(state)       ((state)->count + ((state)->info ? (state)->info->blocked_open : 0))


//#define MP_DEBUG 1
#undef MP_DEBUG

#ifdef MP_DEBUG
#define DPRINTK(x...)   printk(x)
#else
#define DPRINTK(x...)   do { } while (0)
#endif

#ifdef MP_DEBUG
#define DEBUG_AUTOCONF(fmt...)  printk(fmt)
#else
#define DEBUG_AUTOCONF(fmt...)  do { } while (0)
#endif

#ifdef MP_DEBUG
#define DEBUG_INTR(fmt...)  printk(fmt)
#else
#define DEBUG_INTR(fmt...)  do { } while (0)
#endif

#if defined(__i386__) && defined(CONFIG_M486)
#define SERIAL_INLINE
#endif
#ifdef SERIAL_INLINE
#define _INLINE_ inline
#else
#define _INLINE_
#endif

#define TYPE_POLL	1
#define TYPE_INTERRUPT	2


struct mp_device_t {
        unsigned short  device_id;
        unsigned char   revision;
        char            *name;
        unsigned long   uart_access_addr;
        unsigned long   option_reg_addr;
        unsigned long   reserved_addr[4];
        int             irq;
        int             nr_ports;
        int             poll_type;
};

typedef struct mppcibrd {
        char            *name;
        unsigned short  vendor_id;
        unsigned short  device_id;
} mppcibrd_t;

static mppcibrd_t mp_pciboards[] = {

        { "Multi-1 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP1} ,
        { "Multi-2 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP2} ,
        { "Multi-4 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP4} ,
        { "Multi-4 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP4A} ,
        { "Multi-6 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP6} ,
        { "Multi-6 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP6A} ,
        { "Multi-8 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP8} ,
        { "Multi-32 PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP32} ,

        { "Multi-1P PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP1P} ,
        { "Multi-2S1P PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP2S1P} ,

        { "Multi-4(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP4} ,
        { "Multi-4(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP4A} ,
        { "Multi-6(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP6} ,
        { "Multi-6(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP6A} ,
        { "Multi-8(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP8} ,
        { "Multi-32(GT) PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_GT_MP32} ,

        { "Multi-1 PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP1} ,
        { "Multi-2 PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP2} ,
        { "Multi-4 PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP4} ,
        { "Multi-8 PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP8} ,
        { "Multi-32 PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP32} ,

        { "Multi-1 PCIe E", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP1E} ,
        { "Multi-2 PCIe E", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP2E} ,
        { "Multi-2 PCIe B", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP2B} ,
        { "Multi-4 PCIe B", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP4B} ,
        { "Multi-8 PCIe B", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_MP8B} ,

        { "Multi-1(GT) PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_GT_MP1} ,
        { "Multi-2(GT) PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_GT_MP2} ,
        { "Multi-4(GT) PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_GT_MP4} ,
        { "Multi-8(GT) PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_GT_MP8} ,
        { "Multi-32(GT) PCIe", PCI_VENDOR_ID_MULTIPORT , PCIE_DEVICE_ID_GT_MP32} ,

        { "Multi-4M PCI", PCI_VENDOR_ID_MULTIPORT , PCI_DEVICE_ID_MP4M} ,
};

struct mp_port {
        struct sb_uart_port port;

        struct timer_list   timer;      /* "no irq" timer */
        struct list_head    list;       /* ports on this IRQ */
        unsigned int        capabilities;   /* port capabilities */
        unsigned short      rev;
        unsigned char       acr;
        unsigned char       ier;
        unsigned char       lcr;
        unsigned char       mcr;
        unsigned char       mcr_mask;   /* mask of user bits */
        unsigned char       mcr_force;  /* mask of forced bits */
        unsigned char       lsr_break_flag;

        void            (*pm)(struct sb_uart_port *port,
                        unsigned int state, unsigned int old);
        struct mp_device_t *device;
        unsigned long   interface_config_addr;
        unsigned long   option_base_addr;
        unsigned char   interface;
        unsigned char   poll_type;
};

struct irq_info {
        spinlock_t      lock;
        struct list_head    *head;
};

struct sb105x_uart_config {
	char    *name;
	int     dfl_xmit_fifo_size;
	int     flags;
};

static const struct sb105x_uart_config uart_config[] = {
        { "unknown",    1,  0 },
        { "16550A", 16, UART_CLEAR_FIFO | UART_USE_FIFO },
        { "SB16C1050",    128,    UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
        { "SB16C1050A",    128,    UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
};



