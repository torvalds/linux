/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2010 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for Samsung SoC UART IRQ demux for S3C64XX and later
 */

struct s3c_uart_irq {
	void __iomem	*regs;
	unsigned int	 base_irq;
	unsigned int	 parent_irq;
};

