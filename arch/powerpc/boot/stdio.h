#ifndef _PPC_BOOT_STDIO_H_
#define _PPC_BOOT_STDIO_H_

extern int printf(const char *fmt, ...);

extern int sprintf(char *buf, const char *fmt, ...);

extern int vsprintf(char *buf, const char *fmt, va_list args);

#endif				/* _PPC_BOOT_STDIO_H_ */
