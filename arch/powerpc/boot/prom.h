#ifndef _PPC_BOOT_PROM_H_
#define _PPC_BOOT_PROM_H_

extern int (*prom) (void *);
extern void *chosen_handle;

extern void *stdin;
extern void *stdout;
extern void *stderr;

extern int write(void *handle, void *ptr, int nb);
extern int read(void *handle, void *ptr, int nb);
extern void exit(void);
extern void pause(void);
extern void *finddevice(const char *);
extern void *claim(unsigned long virt, unsigned long size, unsigned long align);
extern int getprop(void *phandle, const char *name, void *buf, int buflen);
#endif				/* _PPC_BOOT_PROM_H_ */
