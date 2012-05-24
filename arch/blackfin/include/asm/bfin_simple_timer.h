/*
 * Copyright 2006-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _bfin_simple_timer_h_
#define _bfin_simple_timer_h_

#include <linux/ioctl.h>

#define BFIN_SIMPLE_TIMER_IOCTL_MAGIC 't'

#define BFIN_SIMPLE_TIMER_SET_PERIOD _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC,  2)
#define BFIN_SIMPLE_TIMER_SET_WIDTH _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC,  3)
#define BFIN_SIMPLE_TIMER_SET_MODE _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC,  4)
#define BFIN_SIMPLE_TIMER_START      _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC,  6)
#define BFIN_SIMPLE_TIMER_STOP       _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC,  8)
#define BFIN_SIMPLE_TIMER_READ       _IO(BFIN_SIMPLE_TIMER_IOCTL_MAGIC, 10)

#endif
