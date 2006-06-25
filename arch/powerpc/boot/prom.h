#ifndef _PPC_BOOT_PROM_H_
#define _PPC_BOOT_PROM_H_

typedef void *phandle;
typedef void *ihandle;

extern int (*prom) (void *);
extern phandle chosen_handle;
extern ihandle stdout;

int	call_prom(const char *service, int nargs, int nret, ...);
int	call_prom_ret(const char *service, int nargs, int nret,
		      unsigned int *rets, ...);

extern int write(void *handle, void *ptr, int nb);
extern void *claim(unsigned long virt, unsigned long size, unsigned long aln);

static inline void exit(void)
{
	call_prom("exit", 0, 0);
}

static inline phandle finddevice(const char *name)
{
	return (phandle) call_prom("finddevice", 1, 1, name);
}

static inline int getprop(void *phandle, const char *name,
			  void *buf, int buflen)
{
	return call_prom("getprop", 4, 1, phandle, name, buf, buflen);
}


static inline int setprop(void *phandle, const char *name,
			  void *buf, int buflen)
{
	return call_prom("setprop", 4, 1, phandle, name, buf, buflen);
}

#endif				/* _PPC_BOOT_PROM_H_ */
