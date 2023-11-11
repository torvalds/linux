#include <linux/spinlock.h>

#include <asm/checksum.h>
#include <asm/console.h>
#include <asm/page.h>
#include <asm/string.h>
#include <linux/uaccess.h>

#include <asm-generic/asm-prototypes.h>

extern void __divl(void);
extern void __reml(void);
extern void __divq(void);
extern void __remq(void);
extern void __divlu(void);
extern void __remlu(void);
extern void __divqu(void);
extern void __remqu(void);
extern unsigned long __udiv_qrnnd(unsigned long *, unsigned long, unsigned long , unsigned long);
