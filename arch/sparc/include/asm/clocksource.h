/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_SPARC_CLOCKSOURCE_H
#define _ASM_SPARC_CLOCKSOURCE_H

/* VDSO clocksources */
#define VCLOCK_NONE   0  /* Nothing userspace can do. */
#define VCLOCK_TICK   1  /* Use %tick.  */
#define VCLOCK_STICK  2  /* Use %stick. */

struct arch_clocksource_data {
	int vclock_mode;
};

#endif /* _ASM_SPARC_CLOCKSOURCE_H */
