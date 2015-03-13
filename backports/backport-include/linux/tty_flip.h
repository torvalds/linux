#ifndef __BACKPORT_TTY_FLIP_H
#define __BACKPORT_TTY_FLIP_H
#include_next <linux/tty_flip.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#define tty_flip_buffer_push(port) tty_flip_buffer_push((port)->tty)
#define tty_insert_flip_string(port, chars, size) tty_insert_flip_string((port)->tty, chars, size)
#endif

#endif /* __BACKPORT_TTY_FLIP_H */
