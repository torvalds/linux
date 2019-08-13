/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Machine vector for IA-64.
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Srinivasa Thirumalachar <sprasad@engr.sgi.com>
 * Copyright (C) Vijay Chander <vijay@engr.sgi.com>
 * Copyright (C) 1999-2001, 2003-2004 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _ASM_IA64_MACHVEC_H
#define _ASM_IA64_MACHVEC_H

#include <linux/types.h>

struct device;

typedef void ia64_mv_setup_t (char **);

extern void machvec_setup (char **);

# if defined (CONFIG_IA64_DIG)
#  include <asm/machvec_dig.h>
# elif defined (CONFIG_IA64_HP_ZX1)
#  include <asm/machvec_hpzx1.h>
# elif defined (CONFIG_IA64_SGI_UV)
#  include <asm/machvec_uv.h>
# elif defined (CONFIG_IA64_GENERIC)

# ifdef MACHVEC_PLATFORM_HEADER
#  include MACHVEC_PLATFORM_HEADER
# else
#  define ia64_platform_name	ia64_mv.name
#  define platform_setup	ia64_mv.setup
# endif

/* __attribute__((__aligned__(16))) is required to make size of the
 * structure multiple of 16 bytes.
 * This will fillup the holes created because of section 3.3.1 in
 * Software Conventions guide.
 */
struct ia64_machine_vector {
	const char *name;
	ia64_mv_setup_t *setup;
} __attribute__((__aligned__(16))); /* align attrib? see above comment */

#define MACHVEC_INIT(name)			\
{						\
	#name,					\
	platform_setup,				\
}

extern struct ia64_machine_vector ia64_mv;
extern void machvec_init (const char *name);
extern void machvec_init_from_cmdline(const char *cmdline);

# else
#  error Unknown configuration.  Update arch/ia64/include/asm/machvec.h.
# endif /* CONFIG_IA64_GENERIC */

/*
 * Define default versions so we can extend machvec for new platforms without having
 * to update the machvec files for all existing platforms.
 */
#ifndef platform_setup
# define platform_setup			machvec_setup
#endif

#endif /* _ASM_IA64_MACHVEC_H */
