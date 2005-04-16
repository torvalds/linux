/*
 * arch/mips/dec/decstation.c
 */

#define RELOC
#define INITRD
#define DEBUG_BOOT

/*
 * Magic number indicating REX PROM available on DECSTATION.
 */
#define	REX_PROM_MAGIC		0x30464354

#define REX_PROM_CLEARCACHE	0x7c/4
#define REX_PROM_PRINTF		0x30/4

#define VEC_RESET		0xBFC00000		/* Prom base address */
#define	PMAX_PROM_ENTRY(x)	(VEC_RESET+((x)*8))	/* Prom jump table */
#define	PMAX_PROM_PRINTF	PMAX_PROM_ENTRY(17)

#define PARAM	(k_start + 0x2000)

#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))

extern int _ftext, _end;		/* begin and end of kernel image */
extern void kernel_entry(int, char **, unsigned long, int *);

void * memcpy(void * dest, const void *src, unsigned int count)
{
	unsigned long *tmp = (unsigned long *) dest, *s = (unsigned long *) src;

	count >>= 2;
	while (count--)
		*tmp++ = *s++;

	return dest;
}

void dec_entry(int argc, char **argv,
	       unsigned long magic, int *prom_vec)
{
	void (*rex_clear_cache)(void);
	int (*prom_printf)(char *, ...);
	unsigned long k_start, len;

	/*
	 * The DS5100 leaves cpu with BEV enabled, clear it.
	 */
	asm(	"lui\t$8,0x3000\n\t"
		"mtc0\t$8,$12\n\t"
		".section\t.sdata\n\t"
		".section\t.sbss\n\t"
		".section\t.text"
		: : : "$8");

#ifdef DEBUG_BOOT
	if (magic == REX_PROM_MAGIC) {
	prom_printf = (int (*)(char *, ...)) *(prom_vec + REX_PROM_PRINTF);
	} else {
		prom_printf = (int (*)(char *, ...)) PMAX_PROM_PRINTF;
	}
	prom_printf("Launching kernel...\n");
#endif

	k_start = (unsigned long) (&kernel_entry) & 0xffff0000;

#ifdef RELOC
	/*
	 * Now copy kernel image to its destination.
	 */
	len = ((unsigned long) (&_end) - k_start);
	memcpy((void *)k_start, &_ftext, len);
#endif

	if (magic == REX_PROM_MAGIC) {
		rex_clear_cache = (void (*)(void)) * (prom_vec + REX_PROM_CLEARCACHE);
		rex_clear_cache();
	}

	kernel_entry(argc, argv, magic, prom_vec);
}
