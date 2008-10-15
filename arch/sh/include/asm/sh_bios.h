#ifndef __ASM_SH_BIOS_H
#define __ASM_SH_BIOS_H

/*
 * Copyright (C) 2000 Greg Banks, Mitch Davis
 * C API to interface to the standard LinuxSH BIOS
 * usually from within the early stages of kernel boot.
 */


extern void sh_bios_console_write(const char *buf, unsigned int len);
extern void sh_bios_char_out(char ch);
extern int sh_bios_in_gdb_mode(void);
extern void sh_bios_gdb_detach(void);

extern void sh_bios_get_node_addr(unsigned char *node_addr);
extern void sh_bios_shutdown(unsigned int how);

#endif /* __ASM_SH_BIOS_H */
