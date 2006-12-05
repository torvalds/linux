#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>
#include <linux/serial_8250.h>
#include <asm/io.h>
#include <asm/mpc10x.h>
#include <asm/ppc_sys.h>
#include <asm/prom.h>
#include <asm/termbits.h>

static void __iomem *avr_addr;
static unsigned long avr_clock;

static struct work_struct wd_work;

static void wd_stop(void *unused)
{
	const char string[] = "AAAAFFFFJJJJ>>>>VVVV>>>>ZZZZVVVVKKKK";
	int i = 0, rescue = 8;
	int len = strlen(string);

	while (rescue--) {
		int j;
		char lsr = in_8(avr_addr + UART_LSR);

		if (lsr & (UART_LSR_THRE | UART_LSR_TEMT)) {
			for (j = 0; j < 16 && i < len; j++, i++)
				out_8(avr_addr + UART_TX, string[i]);
			if (i == len) {
				/* Read "OK" back: 4ms for the last "KKKK"
				   plus a couple bytes back */
				msleep(7);
				printk("linkstation: disarming the AVR watchdog: ");
				while (in_8(avr_addr + UART_LSR) & UART_LSR_DR)
					printk("%c", in_8(avr_addr + UART_RX));
				break;
			}
		}
		msleep(17);
	}
	printk("\n");
}

#define AVR_QUOT(clock) ((clock) + 8 * 9600) / (16 * 9600)

void avr_uart_configure(void)
{
	unsigned char cval = UART_LCR_WLEN8;
	unsigned int quot = AVR_QUOT(avr_clock);

	if (!avr_addr || !avr_clock)
		return;

	out_8(avr_addr + UART_LCR, cval);			/* initialise UART */
	out_8(avr_addr + UART_MCR, 0);
	out_8(avr_addr + UART_IER, 0);

	cval |= UART_LCR_STOP | UART_LCR_PARITY | UART_LCR_EPAR;

	out_8(avr_addr + UART_LCR, cval);			/* Set character format */

	out_8(avr_addr + UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	out_8(avr_addr + UART_DLL, quot & 0xff);		/* LS of divisor */
	out_8(avr_addr + UART_DLM, quot >> 8);			/* MS of divisor */
	out_8(avr_addr + UART_LCR, cval);			/* reset DLAB */
	out_8(avr_addr + UART_FCR, UART_FCR_ENABLE_FIFO);	/* enable FIFO */
}

void avr_uart_send(const char c)
{
	if (!avr_addr || !avr_clock)
		return;

	out_8(avr_addr + UART_TX, c);
	out_8(avr_addr + UART_TX, c);
	out_8(avr_addr + UART_TX, c);
	out_8(avr_addr + UART_TX, c);
}

static void __init ls_uart_init(void)
{
	local_irq_disable();

#ifndef CONFIG_SERIAL_8250
	out_8(avr_addr + UART_FCR, UART_FCR_ENABLE_FIFO);	/* enable FIFO */
	out_8(avr_addr + UART_FCR, UART_FCR_ENABLE_FIFO |
	      UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);	/* clear FIFOs */
	out_8(avr_addr + UART_FCR, 0);
	out_8(avr_addr + UART_IER, 0);

	/* Clear up interrupts */
	(void) in_8(avr_addr + UART_LSR);
	(void) in_8(avr_addr + UART_RX);
	(void) in_8(avr_addr + UART_IIR);
	(void) in_8(avr_addr + UART_MSR);
#endif
	avr_uart_configure();

	local_irq_enable();
}

static int __init ls_uarts_init(void)
{
	struct device_node *avr;
	phys_addr_t phys_addr;
	int len;

	avr = of_find_node_by_path("/soc10x/serial@80004500");
	if (!avr)
		return -EINVAL;

	avr_clock = *(u32*)get_property(avr, "clock-frequency", &len);
	phys_addr = ((u32*)get_property(avr, "reg", &len))[0];

	if (!avr_clock || !phys_addr)
		return -EINVAL;

	avr_addr = ioremap(phys_addr, 32);
	if (!avr_addr)
		return -EFAULT;

	ls_uart_init();

	INIT_WORK(&wd_work, wd_stop, NULL);
	schedule_work(&wd_work);

	return 0;
}

late_initcall(ls_uarts_init);
