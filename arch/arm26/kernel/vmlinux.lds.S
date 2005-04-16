#include <linux/config.h>

#ifdef CONFIG_XIP_KERNEL

#include "vmlinux-arm26-xip.lds.in"

#else

#include "vmlinux-arm26.lds.in"

#endif

