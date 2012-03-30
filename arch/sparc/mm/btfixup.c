/* btfixup.c: Boot time code fixup and relocator, so that
 * we can get rid of most indirect calls to achieve single
 * image sun4c and srmmu kernel.
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/btfixup.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/cacheflush.h>

#define BTFIXUP_OPTIMIZE_NOP
#define BTFIXUP_OPTIMIZE_OTHER

extern char *srmmu_name;
static char version[] __initdata = "Boot time fixup v1.6. 4/Mar/98 Jakub Jelinek (jj@ultra.linux.cz). Patching kernel for ";
static char str_sun4c[] __initdata = "sun4c\n";
static char str_srmmu[] __initdata = "srmmu[%s]/";
static char str_iommu[] __initdata = "iommu\n";
static char str_iounit[] __initdata = "io-unit\n";

static int visited __initdata = 0;
extern unsigned int ___btfixup_start[], ___btfixup_end[], __init_begin[], __init_end[], __init_text_end[];
extern unsigned int _stext[], _end[], __start___ksymtab[], __stop___ksymtab[];
static char wrong_f[] __initdata = "Trying to set f fixup %p to invalid function %08x\n";
static char wrong_b[] __initdata = "Trying to set b fixup %p to invalid function %08x\n";
static char wrong_s[] __initdata = "Trying to set s fixup %p to invalid value %08x\n";
static char wrong_h[] __initdata = "Trying to set h fixup %p to invalid value %08x\n";
static char wrong_a[] __initdata = "Trying to set a fixup %p to invalid value %08x\n";
static char wrong[] __initdata = "Wrong address for %c fixup %p\n";
static char insn_f[] __initdata = "Fixup f %p refers to weird instructions at %p[%08x,%08x]\n";
static char insn_b[] __initdata = "Fixup b %p doesn't refer to a SETHI at %p[%08x]\n";
static char insn_s[] __initdata = "Fixup s %p doesn't refer to an OR at %p[%08x]\n";
static char insn_h[] __initdata = "Fixup h %p doesn't refer to a SETHI at %p[%08x]\n";
static char insn_a[] __initdata = "Fixup a %p doesn't refer to a SETHI nor OR at %p[%08x]\n";
static char insn_i[] __initdata = "Fixup i %p doesn't refer to a valid instruction at %p[%08x]\n";
static char fca_und[] __initdata = "flush_cache_all undefined in btfixup()\n";
static char wrong_setaddr[] __initdata = "Garbled CALL/INT patch at %p[%08x,%08x,%08x]=%08x\n";

#ifdef BTFIXUP_OPTIMIZE_OTHER
static void __init set_addr(unsigned int *addr, unsigned int q1, int fmangled, unsigned int value)
{
	if (!fmangled)
		*addr = value;
	else {
		unsigned int *q = (unsigned int *)q1;
		if (*addr == 0x01000000) {
			/* Noped */
			*q = value;
		} else if (addr[-1] == *q) {
			/* Moved */
			addr[-1] = value;
			*q = value;
		} else {
			prom_printf(wrong_setaddr, addr-1, addr[-1], *addr, *q, value);
			prom_halt();
		}
	}
}
#else
static inline void set_addr(unsigned int *addr, unsigned int q1, int fmangled, unsigned int value)
{
	*addr = value;
}
#endif

void __init btfixup(void)
{
	unsigned int *p, *q;
	int type, count;
	unsigned insn;
	unsigned *addr;
	int fmangled = 0;
	void (*flush_cacheall)(void);
	
	if (!visited) {
		visited++;
		printk(version);
		if (ARCH_SUN4C)
			printk(str_sun4c);
		else {
			printk(str_srmmu, srmmu_name);
			if (sparc_cpu_model == sun4d)
				printk(str_iounit);
			else
				printk(str_iommu);
		}
	}
	for (p = ___btfixup_start; p < ___btfixup_end; ) {
		count = p[2];
		q = p + 3;
		switch (type = *(unsigned char *)p) {
		case 'f': 
			count = p[3];
			q = p + 4;
			if (((p[0] & 1) || p[1]) 
			    && ((p[1] & 3) || (unsigned *)(p[1]) < _stext || (unsigned *)(p[1]) >= _end)) {
				prom_printf(wrong_f, p, p[1]);
				prom_halt();
			}
			break;
		case 'b':
			if (p[1] < (unsigned long)__init_begin || p[1] >= (unsigned long)__init_text_end || (p[1] & 3)) {
				prom_printf(wrong_b, p, p[1]);
				prom_halt();
			}
			break;
		case 's':
			if (p[1] + 0x1000 >= 0x2000) {
				prom_printf(wrong_s, p, p[1]);
				prom_halt();
			}
			break;
		case 'h':
			if (p[1] & 0x3ff) {
				prom_printf(wrong_h, p, p[1]);
				prom_halt();
			}
			break;
		case 'a':
			if (p[1] + 0x1000 >= 0x2000 && (p[1] & 0x3ff)) {
				prom_printf(wrong_a, p, p[1]);
				prom_halt();
			}
			break;
		}
		if (p[0] & 1) {
			p[0] &= ~1;
			while (count) {
				fmangled = 0;
				addr = (unsigned *)*q;
				if (addr < _stext || addr >= _end) {
					prom_printf(wrong, type, p);
					prom_halt();
				}
				insn = *addr;
#ifdef BTFIXUP_OPTIMIZE_OTHER				
				if (type != 'f' && q[1]) {
					insn = *(unsigned int *)q[1];
					if (!insn || insn == 1)
						insn = *addr;
					else
						fmangled = 1;
				}
#endif
				switch (type) {
				case 'f':	/* CALL */
					if (addr >= __start___ksymtab && addr < __stop___ksymtab) {
						*addr = p[1];
						break;
					} else if (!q[1]) {
						if ((insn & 0xc1c00000) == 0x01000000) { /* SETHI */
							*addr = (insn & 0xffc00000) | (p[1] >> 10); break;
						} else if ((insn & 0xc1f82000) == 0x80102000) { /* OR X, %LO(i), Y */
							*addr = (insn & 0xffffe000) | (p[1] & 0x3ff); break;
						} else if ((insn & 0xc0000000) != 0x40000000) { /* !CALL */
				bad_f:
							prom_printf(insn_f, p, addr, insn, addr[1]);
							prom_halt();
						}
					} else if (q[1] != 1)
						addr[1] = q[1];
					if (p[2] == BTFIXUPCALL_NORM) {
				norm_f:	
						*addr = 0x40000000 | ((p[1] - (unsigned)addr) >> 2);
						q[1] = 0;
						break;
					}
#ifndef BTFIXUP_OPTIMIZE_NOP
					goto norm_f;
#else
					if (!(addr[1] & 0x80000000)) {
						if ((addr[1] & 0xc1c00000) != 0x01000000)	/* !SETHI */
							goto bad_f; /* CALL, Bicc, FBfcc, CBccc are weird in delay slot, aren't they? */
					} else {
						if ((addr[1] & 0x01800000) == 0x01800000) {
							if ((addr[1] & 0x01f80000) == 0x01e80000) {
								/* RESTORE */
								goto norm_f; /* It is dangerous to patch that */
							}
							goto bad_f;
						}
						if ((addr[1] & 0xffffe003) == 0x9e03e000) {
							/* ADD %O7, XX, %o7 */
							int displac = (addr[1] << 19);
							
							displac = (displac >> 21) + 2;
							*addr = (0x10800000) + (displac & 0x3fffff);
							q[1] = addr[1];
							addr[1] = p[2];
							break;
						}
						if ((addr[1] & 0x201f) == 0x200f || (addr[1] & 0x7c000) == 0x3c000)
							goto norm_f; /* Someone is playing bad tricks with us: rs1 or rs2 is o7 */
						if ((addr[1] & 0x3e000000) == 0x1e000000)
							goto norm_f; /* rd is %o7. We'd better take care. */
					}
					if (p[2] == BTFIXUPCALL_NOP) {
						*addr = 0x01000000;
						q[1] = 1;
						break;
					}
#ifndef BTFIXUP_OPTIMIZE_OTHER
					goto norm_f;
#else
					if (addr[1] == 0x01000000) {	/* NOP in the delay slot */
						q[1] = addr[1];
						*addr = p[2];
						break;
					}
					if ((addr[1] & 0xc0000000) != 0xc0000000) {
						/* Not a memory operation */
						if ((addr[1] & 0x30000000) == 0x10000000) {
							/* Ok, non-memory op with rd %oX */
							if ((addr[1] & 0x3e000000) == 0x1c000000)
								goto bad_f; /* Aiee. Someone is playing strange %sp tricks */
							if ((addr[1] & 0x3e000000) > 0x12000000 ||
							    ((addr[1] & 0x3e000000) == 0x12000000 &&
							     p[2] != BTFIXUPCALL_STO1O0 && p[2] != BTFIXUPCALL_SWAPO0O1) ||
							    ((p[2] & 0xffffe000) == BTFIXUPCALL_RETINT(0))) {
								/* Nobody uses the result. We can nop it out. */
								*addr = p[2];
								q[1] = addr[1];
								addr[1] = 0x01000000;
								break;
							}
							if ((addr[1] & 0xf1ffffe0) == 0x90100000) {
								/* MOV %reg, %Ox */
								if ((addr[1] & 0x3e000000) == 0x10000000 &&
								    (p[2] & 0x7c000) == 0x20000) {
								    	/* Ok, it is call xx; mov reg, %o0 and call optimizes
								    	   to doing something on %o0. Patch the patch. */
									*addr = (p[2] & ~0x7c000) | ((addr[1] & 0x1f) << 14);
									q[1] = addr[1];
									addr[1] = 0x01000000;
									break;
								}
								if ((addr[1] & 0x3e000000) == 0x12000000 &&
								    p[2] == BTFIXUPCALL_STO1O0) {
								    	*addr = (p[2] & ~0x3e000000) | ((addr[1] & 0x1f) << 25);
								    	q[1] = addr[1];
								    	addr[1] = 0x01000000;
								    	break;
								}
							}
						}
					}
					*addr = addr[1];
					q[1] = addr[1];
					addr[1] = p[2];
					break;
#endif /* BTFIXUP_OPTIMIZE_OTHER */
#endif /* BTFIXUP_OPTIMIZE_NOP */
				case 'b':	/* BLACKBOX */
					/* Has to be sethi i, xx */
					if ((insn & 0xc1c00000) != 0x01000000) {
						prom_printf(insn_b, p, addr, insn);
						prom_halt();
					} else {
						void (*do_fixup)(unsigned *);
						
						do_fixup = (void (*)(unsigned *))p[1];
						do_fixup(addr);
					}
					break;
				case 's':	/* SIMM13 */
					/* Has to be or %g0, i, xx */
					if ((insn & 0xc1ffe000) != 0x80102000) {
						prom_printf(insn_s, p, addr, insn);
						prom_halt();
					}
					set_addr(addr, q[1], fmangled, (insn & 0xffffe000) | (p[1] & 0x1fff));
					break;
				case 'h':	/* SETHI */
					/* Has to be sethi i, xx */
					if ((insn & 0xc1c00000) != 0x01000000) {
						prom_printf(insn_h, p, addr, insn);
						prom_halt();
					}
					set_addr(addr, q[1], fmangled, (insn & 0xffc00000) | (p[1] >> 10));
					break;
				case 'a':	/* HALF */
					/* Has to be sethi i, xx or or %g0, i, xx */
					if ((insn & 0xc1c00000) != 0x01000000 &&
					    (insn & 0xc1ffe000) != 0x80102000) {
						prom_printf(insn_a, p, addr, insn);
						prom_halt();
					}
					if (p[1] & 0x3ff)
						set_addr(addr, q[1], fmangled, 
							(insn & 0x3e000000) | 0x80102000 | (p[1] & 0x1fff));
					else
						set_addr(addr, q[1], fmangled, 
							(insn & 0x3e000000) | 0x01000000 | (p[1] >> 10));
					break;
				case 'i':	/* INT */
					if ((insn & 0xc1c00000) == 0x01000000) /* %HI */
						set_addr(addr, q[1], fmangled, (insn & 0xffc00000) | (p[1] >> 10));
					else if ((insn & 0x80002000) == 0x80002000) /* %LO */
						set_addr(addr, q[1], fmangled, (insn & 0xffffe000) | (p[1] & 0x3ff));
					else {
						prom_printf(insn_i, p, addr, insn);
						prom_halt();
					}
					break;
				}
				count -= 2;
				q += 2;
			}
		} else
			p = q + count;
	}
#ifdef CONFIG_SMP
	flush_cacheall = (void (*)(void))BTFIXUPVAL_CALL(local_flush_cache_all);
#else
	flush_cacheall = (void (*)(void))BTFIXUPVAL_CALL(flush_cache_all);
#endif
	if (!flush_cacheall) {
		prom_printf(fca_und);
		prom_halt();
	}
	(*flush_cacheall)();
}
