#include "misc.h"

#ifdef CONFIG_EARLY_PRINTK

int early_serial_base;

#include "../early_serial_console.c"

#endif
