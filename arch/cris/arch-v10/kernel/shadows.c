/*
 * Various shadow registers. Defines for these are in include/asm-etrax100/io.h
 */

/* Shadows for internal Etrax-registers */

unsigned long genconfig_shadow;
unsigned long gen_config_ii_shadow;
unsigned long port_g_data_shadow;
unsigned char port_pa_dir_shadow;
unsigned char port_pa_data_shadow;
unsigned char port_pb_i2c_shadow;
unsigned char port_pb_config_shadow;
unsigned char port_pb_dir_shadow;
unsigned char port_pb_data_shadow;
unsigned long r_timer_ctrl_shadow;

/* Shadows for external I/O port registers.
 * These are only usable if there actually IS a latch connected
 * to the corresponding external chip-select pin.
 *
 * A common usage is that CSP0 controls LEDs and CSP4 video chips.
 */

unsigned long port_cse1_shadow;
unsigned long port_csp0_shadow;
unsigned long port_csp4_shadow;

/* Corresponding addresses for the ports.
 * These are initialized in arch/cris/mm/init.c using ioremap.
 */

volatile unsigned long *port_cse1_addr;
volatile unsigned long *port_csp0_addr;
volatile unsigned long *port_csp4_addr;

