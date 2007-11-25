#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_reg.h>
#include <asm/io.h>

#define PORT(offset) (0x3f8 + (offset))

static inline unsigned int serial_in(int offset)
{
	return inb(PORT(offset));
}

static inline void serial_out(int offset, int value)
{
	outb(value, PORT(offset));
}

int prom_putchar(char c)
{
	while ((serial_in(UART_LSR) & UART_LSR_THRE) == 0)
		;

	serial_out(UART_TX, c);

	return 1;
}
