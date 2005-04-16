/*
 * Copyright (C) Paul Mackerras 1997.
 * Copyright (C) Leigh Brown 2002.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

typedef void *prom_handle;
typedef void *ihandle;
typedef void *phandle;
typedef int (*prom_entry)(void *);

#define OF_INVALID_HANDLE	((prom_handle)-1UL)

extern prom_entry of_prom_entry;

/* function declarations */

void *	claim(unsigned int virt, unsigned int size, unsigned int align);
int	map(unsigned int phys, unsigned int virt, unsigned int size);
void	enter(void);
void	exit(void);
phandle	finddevice(const char *name);
int	getprop(phandle node, const char *name, void *buf, int buflen);
void	ofinit(prom_entry entry);
int	ofstdio(ihandle *stdin, ihandle *stdout, ihandle *stderr);
int	read(ihandle instance, void *buf, int buflen);
void	release(void *virt, unsigned int size);
int	write(ihandle instance, void *buf, int buflen);

/* inlines */

extern inline void pause(void)
{
	enter();
}
