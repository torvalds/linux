/*
 * Definitions related to the reboot() system call,
 * shared between init.c and halt.c.
 */

#include <sys/reboot.h>

#ifndef RB_HALT_SYSTEM
# if defined(__linux__)
#  define RB_HALT_SYSTEM  0xcdef0123
#  define RB_ENABLE_CAD   0x89abcdef
#  define RB_DISABLE_CAD  0
#  define RB_POWER_OFF    0x4321fedc
#  define RB_AUTOBOOT     0x01234567
# elif defined(RB_HALT)
#  define RB_HALT_SYSTEM  RB_HALT
# endif
#endif

/* Stop system and switch power off if possible.  */
#ifndef RB_POWER_OFF
# if defined(RB_POWERDOWN)
#  define RB_POWER_OFF  RB_POWERDOWN
# elif defined(__linux__)
#  define RB_POWER_OFF  0x4321fedc
# else
#  warning "poweroff unsupported, using halt as fallback"
#  define RB_POWER_OFF  RB_HALT_SYSTEM
# endif
#endif
