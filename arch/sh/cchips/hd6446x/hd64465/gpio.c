/*
 * $Id: gpio.c,v 1.4 2003/05/19 22:24:18 lethal Exp $
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc
 *
 * GPIO pin support for HD64465 companion chip.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/hd64465/gpio.h>

#define _PORTOF(portpin)    (((portpin)>>3)&0x7)
#define _PINOF(portpin)     ((portpin)&0x7)

/* Register addresses parametrised on port */
#define GPIO_CR(port)	    (HD64465_REG_GPACR+((port)<<1))
#define GPIO_DR(port)	    (HD64465_REG_GPADR+((port)<<1))
#define GPIO_ICR(port)	    (HD64465_REG_GPAICR+((port)<<1))
#define GPIO_ISR(port)	    (HD64465_REG_GPAISR+((port)<<1))

#define GPIO_NPORTS 5

#define MODNAME "hd64465_gpio"

EXPORT_SYMBOL(hd64465_gpio_configure);
EXPORT_SYMBOL(hd64465_gpio_get_pin);
EXPORT_SYMBOL(hd64465_gpio_get_port);
EXPORT_SYMBOL(hd64465_gpio_register_irq);
EXPORT_SYMBOL(hd64465_gpio_set_pin);
EXPORT_SYMBOL(hd64465_gpio_set_port);
EXPORT_SYMBOL(hd64465_gpio_unregister_irq);

/* TODO: each port should be protected with a spinlock */


void hd64465_gpio_configure(int portpin, int direction)
{
    	unsigned short cr;
	unsigned int shift = (_PINOF(portpin)<<1);

	cr = inw(GPIO_CR(_PORTOF(portpin)));
	cr &= ~(3<<shift);
	cr |= direction<<shift;
	outw(cr, GPIO_CR(_PORTOF(portpin)));
}

void hd64465_gpio_set_pin(int portpin, unsigned int value)
{
    	unsigned short d;
	unsigned short mask = 1<<(_PINOF(portpin));
	
	d = inw(GPIO_DR(_PORTOF(portpin)));
	if (value)
	    d |= mask;
	else
	    d &= ~mask;
	outw(d, GPIO_DR(_PORTOF(portpin)));
}

unsigned int hd64465_gpio_get_pin(int portpin)
{
	return inw(GPIO_DR(_PORTOF(portpin))) & (1<<(_PINOF(portpin)));
}

/* TODO: for cleaner atomicity semantics, add a mask to this routine */

void hd64465_gpio_set_port(int port, unsigned int value)
{
	outw(value, GPIO_DR(port));
}

unsigned int hd64465_gpio_get_port(int port)
{
	return inw(GPIO_DR(port));
}


static struct {
    void (*func)(int portpin, void *dev);
    void *dev;
} handlers[GPIO_NPORTS * 8];

static irqreturn_t hd64465_gpio_interrupt(int irq, void *dev, struct pt_regs *regs)
{
    	unsigned short port, pin, isr, mask, portpin;
	
	for (port=0 ; port<GPIO_NPORTS ; port++) {
	    isr = inw(GPIO_ISR(port));
	    
	    for (pin=0 ; pin<8 ; pin++) {
	    	mask = 1<<pin;
	    	if (isr & mask) {
		    portpin = (port<<3)|pin;
		    if (handlers[portpin].func != 0)
		    	handlers[portpin].func(portpin, handlers[portpin].dev);
    	    	    else
		    	printk(KERN_NOTICE "unexpected GPIO interrupt, pin %c%d\n",
			    port+'A', (int)pin);
		}
	    }
	    
	    /* Write 1s back to ISR to clear it?  That's what the manual says.. */
	    outw(isr, GPIO_ISR(port));
	}

	return IRQ_HANDLED;
}

void hd64465_gpio_register_irq(int portpin, int mode,
	void (*handler)(int portpin, void *dev), void *dev)
{
    	unsigned long flags;
	unsigned short icr, mask;

	if (handler == 0)
	    return;
	    
	local_irq_save(flags);
	
	handlers[portpin].func = handler;
	handlers[portpin].dev = dev;

    	/*
	 * Configure Interrupt Control Register
	 */
	icr = inw(GPIO_ICR(_PORTOF(portpin)));
	mask = (1<<_PINOF(portpin));
	
	/* unmask interrupt */
	icr &= ~mask;
	
	/* set TS bit */
	mask <<= 8;
	icr &= ~mask;
	if (mode == HD64465_GPIO_RISING)
	    icr |= mask;
	    
	outw(icr, GPIO_ICR(_PORTOF(portpin)));

	local_irq_restore(flags);
}

void hd64465_gpio_unregister_irq(int portpin)
{
    	unsigned long flags;
	unsigned short icr;
	
	local_irq_save(flags);

    	/*
	 * Configure Interrupt Control Register
	 */
	icr = inw(GPIO_ICR(_PORTOF(portpin)));
	icr |= (1<<_PINOF(portpin));	/* mask interrupt */
	outw(icr, GPIO_ICR(_PORTOF(portpin)));

	handlers[portpin].func = 0;
	handlers[portpin].dev = 0;
	
	local_irq_restore(flags);
}

static int __init hd64465_gpio_init(void)
{
	if (!request_region(HD64465_REG_GPACR, 0x1000, MODNAME))
		return -EBUSY;
	if (request_irq(HD64465_IRQ_GPIO, hd64465_gpio_interrupt,
	    		SA_INTERRUPT, MODNAME, 0))
		goto out_irqfailed;

    	printk("HD64465 GPIO layer on irq %d\n", HD64465_IRQ_GPIO);

	return 0;

out_irqfailed:
	release_region(HD64465_REG_GPACR, 0x1000);

	return -EINVAL;
}

static void __exit hd64465_gpio_exit(void)
{
    	release_region(HD64465_REG_GPACR, 0x1000);
	free_irq(HD64465_IRQ_GPIO, 0);
}

module_init(hd64465_gpio_init);
module_exit(hd64465_gpio_exit);

MODULE_LICENSE("GPL");

