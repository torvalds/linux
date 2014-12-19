/*
 * livepatch.h - x86-specific Kernel Live Patching Core
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2014 SUSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASM_X86_LIVEPATCH_H
#define _ASM_X86_LIVEPATCH_H

#include <linux/module.h>
#include <linux/ftrace.h>

#ifdef CONFIG_LIVE_PATCHING
#ifndef CC_USING_FENTRY
#error Your compiler must support -mfentry for live patching to work
#endif
extern int klp_write_module_reloc(struct module *mod, unsigned long type,
				  unsigned long loc, unsigned long value);

static inline void klp_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->ip = ip;
}
#else
#error Live patching support is disabled; check CONFIG_LIVE_PATCHING
#endif

#endif /* _ASM_X86_LIVEPATCH_H */
