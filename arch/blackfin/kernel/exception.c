/* Basic functions for adding/removing custom exception handlers
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/module.h>
#include <asm/irq_handler.h>

int bfin_request_exception(unsigned int exception, void (*handler)(void))
{
	void (*curr_handler)(void);

	if (exception > 0x3F)
		return -EINVAL;

	curr_handler = ex_table[exception];

	if (curr_handler != ex_replaceable)
		return -EBUSY;

	ex_table[exception] = handler;

	return 0;
}
EXPORT_SYMBOL(bfin_request_exception);

int bfin_free_exception(unsigned int exception, void (*handler)(void))
{
	void (*curr_handler)(void);

	if (exception > 0x3F)
		return -EINVAL;

	curr_handler = ex_table[exception];

	if (curr_handler != handler)
		return -EBUSY;

	ex_table[exception] = ex_replaceable;

	return 0;
}
EXPORT_SYMBOL(bfin_free_exception);
