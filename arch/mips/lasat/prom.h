#ifndef __PROM_H
#define __PROM_H

extern void (*prom_display)(const char *string, int pos, int clear);
extern void (*prom_monitor)(void);

#endif /* __PROM_H */
