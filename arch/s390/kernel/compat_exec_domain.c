/*
 * Support for 32-bit Linux for S390 personality.
 *
 * Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Gerhard Tonn (ton@de.ibm.com)
 *
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/personality.h>
#include <linux/sched.h>

struct exec_domain s390_exec_domain;

static int __init
s390_init (void)
{
	s390_exec_domain.name = "Linux/s390";
	s390_exec_domain.handler = NULL;
	s390_exec_domain.pers_low = PER_LINUX32;
	s390_exec_domain.pers_high = PER_LINUX32;
	s390_exec_domain.signal_map = default_exec_domain.signal_map;
	s390_exec_domain.signal_invmap = default_exec_domain.signal_invmap;
	register_exec_domain(&s390_exec_domain);
	return 0;
}

__initcall(s390_init);
