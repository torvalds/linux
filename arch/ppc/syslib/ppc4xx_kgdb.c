#include <linux/config.h>
#include <linux/types.h>
#include <asm/ibm4xx.h>
#include <linux/kernel.h>



#define LSR_DR		0x01 /* Data ready */
#define LSR_OE		0x02 /* Overrun */
#define LSR_PE		0x04 /* Parity error */
#define LSR_FE		0x08 /* Framing error */
#define LSR_BI		0x10 /* Break */
#define LSR_THRE	0x20 /* Xmit holding register empty */
#define LSR_TEMT	0x40 /* Xmitter empty */
#define LSR_ERR		0x80 /* Error */

#include <platforms/4xx/ibm_ocp.h>

extern struct NS16550* COM_PORTS[];
#ifndef NULL
#define NULL 0x00
#endif

static volatile struct NS16550 *kgdb_debugport = NULL;

volatile struct NS16550 *
NS16550_init(int chan)
{
	volatile struct NS16550 *com_port;
	int quot;
#ifdef BASE_BAUD
	quot = BASE_BAUD / 9600;
#else
	quot = 0x000c; /* 0xc = 9600 baud (on a pc) */
#endif

	com_port = (struct NS16550 *) COM_PORTS[chan];

	com_port->lcr = 0x00;
	com_port->ier = 0xFF;
	com_port->ier = 0x00;
	com_port->lcr = com_port->lcr | 0x80; /* Access baud rate */
	com_port->dll = ( quot & 0x00ff ); /* 0xc = 9600 baud */
	com_port->dlm = ( quot & 0xff00 ) >> 8;
	com_port->lcr = 0x03; /* 8 data, 1 stop, no parity */
	com_port->mcr = 0x00; /* RTS/DTR */
	com_port->fcr = 0x07; /* Clear & enable FIFOs */

	return( com_port );
}


void
NS16550_putc(volatile struct NS16550 *com_port, unsigned char c)
{
	while ((com_port->lsr & LSR_THRE) == 0)
		;
	com_port->thr = c;
	return;
}

unsigned char
NS16550_getc(volatile struct NS16550 *com_port)
{
	while ((com_port->lsr & LSR_DR) == 0)
		;
	return (com_port->rbr);
}

unsigned char
NS16550_tstc(volatile struct NS16550 *com_port)
{
	return ((com_port->lsr & LSR_DR) != 0);
}


#if defined(CONFIG_KGDB_TTYS0)
#define KGDB_PORT 0
#elif defined(CONFIG_KGDB_TTYS1)
#define KGDB_PORT 1
#elif defined(CONFIG_KGDB_TTYS2)
#define KGDB_PORT 2
#elif defined(CONFIG_KGDB_TTYS3)
#define KGDB_PORT 3
#else
#error "invalid kgdb_tty port"
#endif

void putDebugChar( unsigned char c )
{
	if ( kgdb_debugport == NULL )
		kgdb_debugport = NS16550_init(KGDB_PORT);
	NS16550_putc( kgdb_debugport, c );
}

int getDebugChar( void )
{
	if (kgdb_debugport == NULL)
		kgdb_debugport = NS16550_init(KGDB_PORT);

	return(NS16550_getc(kgdb_debugport));
}

void kgdb_interruptible(int enable)
{
	return;
}

void putDebugString(char* str)
{
	while (*str != '\0') {
		putDebugChar(*str);
		str++;
	}
	putDebugChar('\r');
	return;
}

void
kgdb_map_scc(void)
{
	printk("kgdb init \n");
	kgdb_debugport = NS16550_init(KGDB_PORT);
}
