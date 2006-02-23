/*
 *  m32r_sio.h
 *
 *  Driver for M32R serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *  Based on drivers/serial/8250.h.
 *
 *  Copyright (C) 2001  Russell King.
 *  Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>

struct m32r_sio_probe {
	struct module	*owner;
	int		(*pci_init_one)(struct pci_dev *dev);
	void		(*pci_remove_one)(struct pci_dev *dev);
	void		(*pnp_init)(void);
};

int m32r_sio_register_probe(struct m32r_sio_probe *probe);
void m32r_sio_unregister_probe(struct m32r_sio_probe *probe);
void m32r_sio_get_irq_map(unsigned int *map);
void m32r_sio_suspend_port(int line);
void m32r_sio_resume_port(int line);

struct old_serial_port {
	unsigned int uart;
	unsigned int baud_base;
	unsigned int port;
	unsigned int irq;
	unsigned int flags;
	unsigned char io_type;
	unsigned char __iomem *iomem_base;
	unsigned short iomem_reg_shift;
};

#define _INLINE_ inline

#define PROBE_RSA	(1 << 0)
#define PROBE_ANY	(~0)

#define HIGH_BITS_OFFSET ((sizeof(long)-sizeof(int))*8)

#ifdef CONFIG_SERIAL_SIO_SHARE_IRQ
#define M32R_SIO_SHARE_IRQS 1
#else
#define M32R_SIO_SHARE_IRQS 0
#endif
