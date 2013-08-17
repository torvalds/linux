/*
 * arch/arm/mach-shark/include/mach/uncompress.h
 * by Alexander Schulz
 *
 * derived from:
 * arch/arm/mach-footbridge/include/mach/uncompress.h
 * Copyright (C) 1996,1997,1998 Russell King
 */

#define SERIAL_BASE ((volatile unsigned char *)0x400003f8)

static inline void putc(int c)
{
	volatile int t;

	SERIAL_BASE[0] = c;
	t=0x10000;
	while (t--);
}

static inline void flush(void)
{
}

#ifdef DEBUG
static void putn(unsigned long z)
{
	int i;
	char x;

	putc('0');
	putc('x');
	for (i=0;i<8;i++) {
		x='0'+((z>>((7-i)*4))&0xf);
		if (x>'9') x=x-'0'+'A'-10;
		putc(x);
	}
}

static void putr()
{
	putc('\n');
	putc('\r');
}
#endif

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
