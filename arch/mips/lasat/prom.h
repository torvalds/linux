#ifndef PROM_H
#define PROM_H
extern void (* prom_display)(const char *string, int pos, int clear);
extern void (* prom_monitor)(void);
extern void (* prom_printf)(const char * fmt, ...);
#endif
