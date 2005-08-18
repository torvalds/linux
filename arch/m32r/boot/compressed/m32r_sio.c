/*
 * arch/m32r/boot/compressed/m32r_sio.c
 *
 * 2003-02-12:	Takeo Takahashi
 *
 */

#include <linux/config.h>
#include <asm/processor.h>

static void putc(char c);

static int puts(const char *s)
{
	char c;
	while ((c = *s++)) putc(c);
	return 0;
}

#if defined(CONFIG_PLAT_M32700UT_Alpha) || defined(CONFIG_PLAT_M32700UT)
#include <asm/m32r.h>
#include <asm/io.h>

#define USE_FPGA_MAP	0

#if USE_FPGA_MAP
/*
 * fpga configuration program uses MMU, and define map as same as
 * M32104 uT-Engine board.
 */
#define BOOT_SIO0STS	(volatile unsigned short *)(0x02c00000 + 0x20006)
#define BOOT_SIO0TXB	(volatile unsigned short *)(0x02c00000 + 0x2000c)
#else
#undef PLD_BASE
#define PLD_BASE	0xa4c00000
#define BOOT_SIO0STS	PLD_ESIO0STS
#define BOOT_SIO0TXB	PLD_ESIO0TXB
#endif

static void putc(char c)
{
	while ((*BOOT_SIO0STS & 0x3) != 0x3)
		cpu_relax();
	if (c == '\n') {
		*BOOT_SIO0TXB = '\r';
		while ((*BOOT_SIO0STS & 0x3) != 0x3)
			cpu_relax();
	}
	*BOOT_SIO0TXB = c;
}
#else /* !(CONFIG_PLAT_M32700UT_Alpha) && !(CONFIG_PLAT_M32700UT) */
#if defined(CONFIG_PLAT_MAPPI2)
#define SIO0STS	(volatile unsigned short *)(0xa0efd000 + 14)
#define SIO0TXB	(volatile unsigned short *)(0xa0efd000 + 30)
#else
#define SIO0STS	(volatile unsigned short *)(0x00efd000 + 14)
#define SIO0TXB	(volatile unsigned short *)(0x00efd000 + 30)
#endif

static void putc(char c)
{
	while ((*SIO0STS & 0x1) == 0)
		cpu_relax();
	if (c == '\n') {
		*SIO0TXB = '\r';
		while ((*SIO0STS & 0x1) == 0)
			cpu_relax();
	}
	*SIO0TXB = c;
}
#endif
