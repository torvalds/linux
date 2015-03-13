#ifndef __BACKPORT_ASM_IOCTLS_H
#define __BACKPORT_ASM_IOCTLS_H
#include_next <asm/ioctls.h>

#ifndef TIOCPKT_IOCTL
#define TIOCPKT_IOCTL 64
#endif

#endif /* __BACKPORT_ASM_IOCTLS_H */
