#include "misc.h"

/* This might be accessed before .bss is cleared, so use .data instead. */
int early_serial_base __section(".data");

#include "../early_serial_console.c"
