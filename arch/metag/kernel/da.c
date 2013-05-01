/*
 * Meta DA JTAG debugger control.
 *
 * Copyright 2012 Imagination Technologies Ltd.
 */


#include <linux/io.h>
#include <linux/kernel.h>
#include <asm/da.h>
#include <asm/metag_mem.h>

bool _metag_da_present;

int __init metag_da_probe(void)
{
	_metag_da_present = (metag_in32(T0VECINT_BHALT) == 1);
	if (_metag_da_present)
		pr_info("DA present\n");
	else
		pr_info("DA not present\n");
	return 0;
}
