//#include <mach/dove.h>


#define LSR_THRE	0x20

static void putc(const char c)
{
	int i;

	for (i = 0; i < 0x1000; i++) {
		/* Transmit fifo not full? */
		/*if (*UART_LSR & LSR_THRE)
			break;*/
	}

	/* *UART_THR = c; */
}

static void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
