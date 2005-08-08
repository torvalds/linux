#ifndef _PPC_BOOT_STRING_H_
#define _PPC_BOOT_STRING_H_

extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strcat(char *dest, const char *src);
extern int strcmp(const char *s1, const char *s2);
extern size_t strlen(const char *s);
extern size_t strnlen(const char *s, size_t count);

extern void *memset(void *s, int c, size_t n);
extern void *memmove(void *dest, const void *src, unsigned long n);
extern void *memcpy(void *dest, const void *src, unsigned long n);
extern int memcmp(const void *s1, const void *s2, size_t n);

#endif	/* _PPC_BOOT_STRING_H_ */
