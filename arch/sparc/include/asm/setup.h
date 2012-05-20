/*
 *	Just a place holder. 
 */

#ifndef _SPARC_SETUP_H
#define _SPARC_SETUP_H

#if defined(__sparc__) && defined(__arch64__)
# define COMMAND_LINE_SIZE 2048
#else
# define COMMAND_LINE_SIZE 256
#endif

#ifdef __KERNEL__

extern char reboot_command[];

#ifdef CONFIG_SPARC32
/* The CPU that was used for booting
 * Only sun4d + leon may have boot_cpu_id != 0
 */
extern unsigned char boot_cpu_id;

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_page_table;
extern unsigned long empty_zero_page;

extern int serial_console;
static inline int con_is_present(void)
{
	return serial_console ? 0 : 1;
}
#endif

extern void sun_do_break(void);
extern int stop_a_enabled;
extern int scons_pwroff;

#endif /* __KERNEL__ */

#endif /* _SPARC_SETUP_H */
