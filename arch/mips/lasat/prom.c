/*
 * PROM interface routines.
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <asm/bootinfo.h>
#include <asm/lasat/lasat.h>
#include <asm/cpu.h>

#include "at93c.h"
#include <asm/lasat/eeprom.h>
#include "prom.h"

#define RESET_VECTOR	0xbfc00000
#define PROM_JUMP_TABLE_ENTRY(n) (*((u32 *)(RESET_VECTOR + 0x20) + n))
#define PROM_DISPLAY_ADDR	PROM_JUMP_TABLE_ENTRY(0)
#define PROM_PUTC_ADDR		PROM_JUMP_TABLE_ENTRY(1)
#define PROM_MONITOR_ADDR	PROM_JUMP_TABLE_ENTRY(2)

static void null_prom_printf(const char * fmt, ...)
{
}

static void null_prom_display(const char *string, int pos, int clear)
{
}

static void null_prom_monitor(void)
{
}

static void null_prom_putc(char c)
{
}

/* these are functions provided by the bootloader */
static void (* prom_putc)(char c) = null_prom_putc;
void (* prom_printf)(const char * fmt, ...) = null_prom_printf;
void (* prom_display)(const char *string, int pos, int clear) = 
		null_prom_display;
void (* prom_monitor)(void) = null_prom_monitor;

unsigned int lasat_ndelay_divider;

#define PROM_PRINTFBUF_SIZE 256
static char prom_printfbuf[PROM_PRINTFBUF_SIZE];

static void real_prom_printf(const char * fmt, ...)
{
	va_list ap;
	int len;
	char *c = prom_printfbuf;
	int i;

	va_start(ap, fmt);
	len = vsnprintf(prom_printfbuf, PROM_PRINTFBUF_SIZE, fmt, ap);
	va_end(ap);

	/* output overflowed the buffer */
	if (len < 0 || len > PROM_PRINTFBUF_SIZE)
		len = PROM_PRINTFBUF_SIZE;

	for (i=0; i < len; i++) {
		if (*c == '\n')
			prom_putc('\r');
		prom_putc(*c++);
	}
}

static void setup_prom_vectors(void)
{
	u32 version = *(u32 *)(RESET_VECTOR + 0x90);

	if (version >= 307) {
		prom_display = (void *)PROM_DISPLAY_ADDR;
		prom_putc = (void *)PROM_PUTC_ADDR;
		prom_printf = real_prom_printf;
		prom_monitor = (void *)PROM_MONITOR_ADDR;
	}
	prom_printf("prom vectors set up\n");
}

static struct at93c_defs at93c_defs[N_MACHTYPES] = {
	{(void *)AT93C_REG_100, (void *)AT93C_RDATA_REG_100, AT93C_RDATA_SHIFT_100,
	AT93C_WDATA_SHIFT_100, AT93C_CS_M_100, AT93C_CLK_M_100},
	{(void *)AT93C_REG_200, (void *)AT93C_RDATA_REG_200, AT93C_RDATA_SHIFT_200,
	AT93C_WDATA_SHIFT_200, AT93C_CS_M_200, AT93C_CLK_M_200},
};

void __init prom_init(void)
{
	int argc = fw_arg0;
	char **argv = (char **) fw_arg1;

	setup_prom_vectors();

	if (current_cpu_data.cputype == CPU_R5000) {
	        prom_printf("LASAT 200 board\n");
		mips_machtype = MACH_LASAT_200;
                lasat_ndelay_divider = LASAT_200_DIVIDER;
        } else {
	        prom_printf("LASAT 100 board\n");
		mips_machtype = MACH_LASAT_100;
                lasat_ndelay_divider = LASAT_100_DIVIDER;
        }

	at93c = &at93c_defs[mips_machtype];

	lasat_init_board_info();		/* Read info from EEPROM */

	mips_machgroup = MACH_GROUP_LASAT;

	/* Get the command line */
	if (argc > 0) {
		strncpy(arcs_cmdline, argv[0], CL_SIZE-1);
		arcs_cmdline[CL_SIZE-1] = '\0';
	}

	/* Set the I/O base address */
	set_io_port_base(KSEG1);

	/* Set memory regions */
	ioport_resource.start = 0;
	ioport_resource.end = 0xffffffff;	/* Wrong, fixme.  */

	add_memory_region(0, lasat_board_info.li_memsize, BOOT_MEM_RAM);
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}

const char *get_system_type(void)
{
	return lasat_board_info.li_bmstr;
}
