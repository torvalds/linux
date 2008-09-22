/*
 * arch/arm/mach-footbridge/include/mach/irqs.h
 *
 * Copyright (C) 1998 Russell King
 * Copyright (C) 1998 Phil Blundell
 *
 * Changelog:
 *  20-Jan-1998	RMK	Started merge of EBSA286, CATS and NetWinder
 *  01-Feb-1999	PJB	ISA IRQs start at 0 not 16
 */
#include <asm/mach-types.h>

#define NR_IRQS			36
#define NR_DC21285_IRQS		16

#define _ISA_IRQ(x)		(0 + (x))
#define _ISA_INR(x)		((x) - 0)
#define _DC21285_IRQ(x)		(16 + (x))
#define _DC21285_INR(x)		((x) - 16)

/*
 * This is a list of all interrupts that the 21285
 * can generate and we handle.
 */
#define IRQ_CONRX		_DC21285_IRQ(0)
#define IRQ_CONTX		_DC21285_IRQ(1)
#define IRQ_TIMER1		_DC21285_IRQ(2)
#define IRQ_TIMER2		_DC21285_IRQ(3)
#define IRQ_TIMER3		_DC21285_IRQ(4)
#define IRQ_IN0			_DC21285_IRQ(5)
#define IRQ_IN1			_DC21285_IRQ(6)
#define IRQ_IN2			_DC21285_IRQ(7)
#define IRQ_IN3			_DC21285_IRQ(8)
#define IRQ_DOORBELLHOST	_DC21285_IRQ(9)
#define IRQ_DMA1		_DC21285_IRQ(10)
#define IRQ_DMA2		_DC21285_IRQ(11)
#define IRQ_PCI			_DC21285_IRQ(12)
#define IRQ_SDRAMPARITY		_DC21285_IRQ(13)
#define IRQ_I2OINPOST		_DC21285_IRQ(14)
#define IRQ_PCI_ABORT		_DC21285_IRQ(15)
#define IRQ_PCI_SERR		_DC21285_IRQ(16)
#define IRQ_DISCARD_TIMER	_DC21285_IRQ(17)
#define IRQ_PCI_DPERR		_DC21285_IRQ(18)
#define IRQ_PCI_PERR		_DC21285_IRQ(19)

#define IRQ_ISA_TIMER		_ISA_IRQ(0)
#define IRQ_ISA_KEYBOARD	_ISA_IRQ(1)
#define IRQ_ISA_CASCADE		_ISA_IRQ(2)
#define IRQ_ISA_UART2		_ISA_IRQ(3)
#define IRQ_ISA_UART		_ISA_IRQ(4)
#define IRQ_ISA_FLOPPY		_ISA_IRQ(6)
#define IRQ_ISA_PRINTER		_ISA_IRQ(7)
#define IRQ_ISA_RTC_ALARM	_ISA_IRQ(8)
#define IRQ_ISA_2		_ISA_IRQ(9)
#define IRQ_ISA_PS2MOUSE	_ISA_IRQ(12)
#define IRQ_ISA_HARDDISK1	_ISA_IRQ(14)
#define IRQ_ISA_HARDDISK2	_ISA_IRQ(15)

#define IRQ_MASK_UART_RX	(1 << 2)
#define IRQ_MASK_UART_TX	(1 << 3)
#define IRQ_MASK_TIMER1		(1 << 4)
#define IRQ_MASK_TIMER2		(1 << 5)
#define IRQ_MASK_TIMER3		(1 << 6)
#define IRQ_MASK_IN0		(1 << 8)
#define IRQ_MASK_IN1		(1 << 9)
#define IRQ_MASK_IN2		(1 << 10)
#define IRQ_MASK_IN3		(1 << 11)
#define IRQ_MASK_DOORBELLHOST	(1 << 15)
#define IRQ_MASK_DMA1		(1 << 16)
#define IRQ_MASK_DMA2		(1 << 17)
#define IRQ_MASK_PCI		(1 << 18)
#define IRQ_MASK_SDRAMPARITY	(1 << 24)
#define IRQ_MASK_I2OINPOST	(1 << 25)
#define IRQ_MASK_PCI_ABORT	((1 << 29) | (1 << 30))
#define IRQ_MASK_PCI_SERR	(1 << 23)
#define IRQ_MASK_DISCARD_TIMER	(1 << 27)
#define IRQ_MASK_PCI_DPERR	(1 << 28)
#define IRQ_MASK_PCI_PERR	(1 << 31)

/*
 * Netwinder interrupt allocations
 */
#define IRQ_NETWINDER_ETHER10	IRQ_IN0
#define IRQ_NETWINDER_ETHER100	IRQ_IN1
#define IRQ_NETWINDER_VIDCOMP	IRQ_IN2
#define IRQ_NETWINDER_PS2MOUSE	_ISA_IRQ(5)
#define IRQ_NETWINDER_IR	_ISA_IRQ(6)
#define IRQ_NETWINDER_BUTTON	_ISA_IRQ(10)
#define IRQ_NETWINDER_VGA	_ISA_IRQ(11)
#define IRQ_NETWINDER_SOUND	_ISA_IRQ(12)

#undef RTC_IRQ
#define RTC_IRQ		IRQ_ISA_RTC_ALARM
#define I8042_KBD_IRQ	IRQ_ISA_KEYBOARD
#define I8042_AUX_IRQ	(machine_is_netwinder() ? IRQ_NETWINDER_PS2MOUSE : IRQ_ISA_PS2MOUSE)
#define IRQ_FLOPPYDISK	IRQ_ISA_FLOPPY

#define irq_canonicalize(_i)	(((_i) == IRQ_ISA_CASCADE) ? IRQ_ISA_2 : _i)
