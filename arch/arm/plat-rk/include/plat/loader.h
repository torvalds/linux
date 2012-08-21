#ifndef __PLAT_LOADER_H
#define __PLAT_LOADER_H

#define SYS_LOADER_REBOOT_FLAG   0x5242C300  //high 24 bits is tag, low 8 bits is type
#define SYS_KERNRL_REBOOT_FLAG   0xC3524200  //high 24 bits is tag, low 8 bits is type

enum {
    BOOT_NORMAL = 0, /* normal boot */
    BOOT_LOADER,     /* enter loader rockusb mode */
    BOOT_MASKROM,    /* enter maskrom rockusb mode (not support now) */
    BOOT_RECOVER,    /* enter recover */
    BOOT_NORECOVER,  /* do not enter recover */
    BOOT_SECONDOS,   /* boot second OS (not support now)*/
    BOOT_WIPEDATA,   /* enter recover and wipe data. */
    BOOT_WIPEALL,    /* enter recover and wipe all data. */
    BOOT_CHECKIMG,   /* check firmware img with backup part(in loader mode)*/
    BOOT_FASTBOOT,   /* enter fast boot mode (not support now) */
    BOOT_MAX         /* MAX VALID BOOT TYPE.*/
};

#endif
