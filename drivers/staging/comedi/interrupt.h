/*
    linux/interrupt.h compatibility header

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __COMPAT_LINUX_INTERRUPT_H_
#define __COMPAT_LINUX_INTERRUPT_H_

#include <linux/interrupt.h>

#include <linux/version.h>

#ifndef IRQ_NONE
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x) (void)(x)
#endif

#ifndef IRQF_DISABLED
#define IRQF_DISABLED           SA_INTERRUPT
#define IRQF_SAMPLE_RANDOM      SA_SAMPLE_RANDOM
#define IRQF_SHARED             SA_SHIRQ
#define IRQF_PROBE_SHARED       SA_PROBEIRQ
#define IRQF_PERCPU             SA_PERCPU
#ifdef SA_TRIGGER_MASK
#define IRQF_TRIGGER_NONE       0
#define IRQF_TRIGGER_LOW        SA_TRIGGER_LOW
#define IRQF_TRIGGER_HIGH       SA_TRIGGER_HIGH
#define IRQF_TRIGGER_FALLING    SA_TRIGGER_FALLING
#define IRQF_TRIGGER_RISING     SA_TRIGGER_RISING
#define IRQF_TRIGGER_MASK       SA_TRIGGER_MASK
#else
#define IRQF_TRIGGER_NONE       0
#define IRQF_TRIGGER_LOW        0
#define IRQF_TRIGGER_HIGH       0
#define IRQF_TRIGGER_FALLING    0
#define IRQF_TRIGGER_RISING     0
#define IRQF_TRIGGER_MASK       0
#endif
#endif

#define PT_REGS_ARG
#define PT_REGS_CALL
#define PT_REGS_NULL

#endif
