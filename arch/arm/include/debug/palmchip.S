#include <linux/serial_reg.h>

#undef UART_TX
#undef UART_LSR
#undef UART_MSR

#define UART_TX 1
#define UART_LSR 7
#define UART_MSR 8

#include <debug/8250.S>
