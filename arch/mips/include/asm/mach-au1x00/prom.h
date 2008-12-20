#ifndef __AU1X00_PROM_H
#define __AU1X00_PROM_H

extern int prom_argc;
extern char **prom_argv;
extern char **prom_envp;

extern void prom_init_cmdline(void);
extern char *prom_getcmdline(void);
extern char *prom_getenv(char *envname);
extern int prom_get_ethernet_addr(char *ethernet_addr);

#endif
