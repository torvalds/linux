/* $Id: ia32_ioctl.c,v 1.25 2002/10/11 07:17:06 ak Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include <linux/syscalls.h>
#include "compat_ioctl.c"
#include <asm/ia32.h>

#define CODE
#include "compat_ioctl.c"


#define HANDLE_IOCTL(cmd,handler) { (cmd), (ioctl_trans_handler_t)(handler) }, 
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)

struct ioctl_trans ioctl_start[] = { 
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
/* take care of sizeof(sizeof()) breakage */
}; 

int ioctl_table_size = ARRAY_SIZE(ioctl_start);

