/******************************************************************************
 * hypervisor.h
 *
 * Linux-specific hypervisor handling.
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _ASM_IA64_XEN_HYPERVISOR_H
#define _ASM_IA64_XEN_HYPERVISOR_H

#include <xen/interface/xen.h>
#include <xen/interface/version.h>	/* to compile feature.c */
#include <xen/features.h>		/* to comiple xen-netfront.c */
#include <asm/xen/hypercall.h>

/* xen_domain_type is set before executing any C code by early_xen_setup */
enum xen_domain_type {
	XEN_NATIVE,	/* running on bare hardware */
	XEN_PV_DOMAIN,	/* running in a PV domain */
	XEN_HVM_DOMAIN,	/* running in a Xen hvm domain*/
};

#ifdef CONFIG_XEN
extern enum xen_domain_type xen_domain_type;
#else
#define xen_domain_type		XEN_NATIVE
#endif

#define xen_domain()		(xen_domain_type != XEN_NATIVE)
#define xen_pv_domain()		(xen_domain() &&			\
				 xen_domain_type == XEN_PV_DOMAIN)
#define xen_hvm_domain()	(xen_domain() &&			\
				 xen_domain_type == XEN_HVM_DOMAIN)

#ifdef CONFIG_XEN_DOM0
#define xen_initial_domain()	(xen_pv_domain() &&			\
				 (xen_start_info->flags & SIF_INITDOMAIN))
#else
#define xen_initial_domain()	(0)
#endif


#ifdef CONFIG_XEN
extern struct shared_info *HYPERVISOR_shared_info;
extern struct start_info *xen_start_info;

void __init xen_setup_vcpu_info_placement(void);
void force_evtchn_callback(void);

/* for drivers/xen/balloon/balloon.c */
#ifdef CONFIG_XEN_SCRUB_PAGES
#define scrub_pages(_p, _n) memset((void *)(_p), 0, (_n) << PAGE_SHIFT)
#else
#define scrub_pages(_p, _n) ((void)0)
#endif

/* For setup_arch() in arch/ia64/kernel/setup.c */
void xen_ia64_enable_opt_feature(void);
#endif

#endif /* _ASM_IA64_XEN_HYPERVISOR_H */
