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

#ifdef CONFIG_SPARC32
/* The CPU that was used for booting
 * Only sun4d + leon may have boot_cpu_id != 0
 */
extern unsigned char boot_cpu_id;
extern unsigned char boot_cpu_id4;
#endif

#endif /* __KERNEL__ */

#endif /* _SPARC_SETUP_H */
