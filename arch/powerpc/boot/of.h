#ifndef _PPC_BOOT_OF_H_
#define _PPC_BOOT_OF_H_

#include "swab.h"

typedef void *phandle;
typedef u32 ihandle;

void of_init(void *promptr);
int of_call_prom(const char *service, int nargs, int nret, ...);
unsigned int of_claim(unsigned long virt, unsigned long size,
	unsigned long align);
void *of_vmlinux_alloc(unsigned long size);
void of_exit(void);
void *of_finddevice(const char *name);
int of_getprop(const void *phandle, const char *name, void *buf,
	       const int buflen);
int of_setprop(const void *phandle, const char *name, const void *buf,
	       const int buflen);

/* Console functions */
void of_console_init(void);

typedef u32			__be32;

#ifdef __LITTLE_ENDIAN__
#define cpu_to_be32(x) swab32(x)
#define be32_to_cpu(x) swab32(x)
#else
#define cpu_to_be32(x) (x)
#define be32_to_cpu(x) (x)
#endif

#define PROM_ERROR (-1u)

#endif /* _PPC_BOOT_OF_H_ */
