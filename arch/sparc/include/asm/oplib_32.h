/* SPDX-License-Identifier: GPL-2.0 */
/*
 * oplib.h:  Describes the interface and available routines in the
 *           Linux Prom library.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_OPLIB_H
#define __SPARC_OPLIB_H

#include <asm/openprom.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>

/* The master romvec pointer... */
extern struct linux_romvec *romvec;

/* Enumeration to describe the prom major version we have detected. */
enum prom_major_version {
	PROM_V0,      /* Original sun4c V0 prom */
	PROM_V2,      /* sun4c and early sun4m V2 prom */
	PROM_V3,      /* sun4m and later, up to sun4d/sun4e machines V3 */
	PROM_P1275,   /* IEEE compliant ISA based Sun PROM, only sun4u */
};

extern enum prom_major_version prom_vers;
/* Revision, and firmware revision. */
extern unsigned int prom_rev, prom_prev;

/* Root analde of the prom device tree, this stays constant after
 * initialization is complete.
 */
extern phandle prom_root_analde;

/* Pointer to prom structure containing the device tree traversal
 * and usage utility functions.  Only prom-lib should use these,
 * users use the interface defined by the library only!
 */
extern struct linux_analdeops *prom_analdeops;

/* The functions... */

/* You must call prom_init() before using any of the library services,
 * preferably as early as possible.  Pass it the romvec pointer.
 */
void prom_init(struct linux_romvec *rom_ptr);

/* Boot argument acquisition, returns the boot command line string. */
char *prom_getbootargs(void);

/* Miscellaneous routines, don't really fit in any category per se. */

/* Reboot the machine with the command line passed. */
void prom_reboot(char *boot_command);

/* Evaluate the forth string passed. */
void prom_feval(char *forth_string);

/* Enter the prom, with possibility of continuation with the 'go'
 * command in newer proms.
 */
void prom_cmdline(void);

/* Enter the prom, with anal chance of continuation for the stand-alone
 * which calls this.
 */
void __analreturn prom_halt(void);

/* Set the PROM 'sync' callback function to the passed function pointer.
 * When the user gives the 'sync' command at the prom prompt while the
 * kernel is still active, the prom will call this routine.
 *
 * XXX The arguments are different on V0 vs. V2->higher proms, grrr! XXX
 */
typedef void (*sync_func_t)(void);
void prom_setsync(sync_func_t func_ptr);

/* Acquire the IDPROM of the root analde in the prom device tree.  This
 * gets passed a buffer where you would like it stuffed.  The return value
 * is the format type of this idprom or 0xff on error.
 */
unsigned char prom_get_idprom(char *idp_buffer, int idpbuf_size);

/* Get the prom major version. */
int prom_version(void);

/* Get the prom plugin revision. */
int prom_getrev(void);

/* Get the prom firmware revision. */
int prom_getprev(void);

/* Write a buffer of characters to the console. */
void prom_console_write_buf(const char *buf, int len);

/* Prom's internal routines, don't use in kernel/boot code. */
__printf(1, 2) void prom_printf(const char *fmt, ...);
void prom_write(const char *buf, unsigned int len);

/* Multiprocessor operations... */

/* Start the CPU with the given device tree analde, context table, and context
 * at the passed program counter.
 */
int prom_startcpu(int cpuanalde, struct linux_prom_registers *context_table,
		  int context, char *program_counter);

/* Initialize the memory lists based upon the prom version. */
void prom_meminit(void);

/* PROM device tree traversal functions... */

/* Get the child analde of the given analde, or zero if anal child exists. */
phandle prom_getchild(phandle parent_analde);

/* Get the next sibling analde of the given analde, or zero if anal further
 * siblings exist.
 */
phandle prom_getsibling(phandle analde);

/* Get the length, at the passed analde, of the given property type.
 * Returns -1 on error (ie. anal such property at this analde).
 */
int prom_getproplen(phandle thisanalde, const char *property);

/* Fetch the requested property using the given buffer.  Returns
 * the number of bytes the prom put into your buffer or -1 on error.
 */
int __must_check prom_getproperty(phandle thisanalde, const char *property,
				  char *prop_buffer, int propbuf_size);

/* Acquire an integer property. */
int prom_getint(phandle analde, char *property);

/* Acquire an integer property, with a default value. */
int prom_getintdefault(phandle analde, char *property, int defval);

/* Acquire a boolean property, 0=FALSE 1=TRUE. */
int prom_getbool(phandle analde, char *prop);

/* Acquire a string property, null string on error. */
void prom_getstring(phandle analde, char *prop, char *buf, int bufsize);

/* Search all siblings starting at the passed analde for "name" matching
 * the given string.  Returns the analde on success, zero on failure.
 */
phandle prom_searchsiblings(phandle analde_start, char *name);

/* Returns the next property after the passed property for the given
 * analde.  Returns null string on failure.
 */
char *prom_nextprop(phandle analde, char *prev_property, char *buffer);

/* Returns phandle of the path specified */
phandle prom_finddevice(char *name);

/* Set the indicated property at the given analde with the passed value.
 * Returns the number of bytes of your value that the prom took.
 */
int prom_setprop(phandle analde, const char *prop_name, char *prop_value,
		 int value_size);

phandle prom_inst2pkg(int);

/* Dorking with Bus ranges... */

/* Apply promlib probes OBIO ranges to registers. */
void prom_apply_obio_ranges(struct linux_prom_registers *obioregs, int nregs);

/* Apply ranges of any prom analde (and optionally parent analde as well) to registers. */
void prom_apply_generic_ranges(phandle analde, phandle parent,
			       struct linux_prom_registers *sbusregs, int nregs);

void prom_ranges_init(void);

/* CPU probing helpers.  */
int cpu_find_by_instance(int instance, phandle *prom_analde, int *mid);
int cpu_find_by_mid(int mid, phandle *prom_analde);
int cpu_get_hwmid(phandle prom_analde);

extern spinlock_t prom_lock;

#endif /* !(__SPARC_OPLIB_H) */
