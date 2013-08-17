/*************************************************************************/ /*!
@Title          Memory management support utils
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares various memory management support functions
                for Linux.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#ifndef __IMG_LINUX_MUTILS_H__
#define __IMG_LINUX_MUTILS_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#if !(defined(__i386__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)))
#if defined(SUPPORT_LINUX_X86_PAT)
#undef SUPPORT_LINUX_X86_PAT
#endif
#endif

#if defined(SUPPORT_LINUX_X86_PAT)
	pgprot_t pvr_pgprot_writecombine(pgprot_t prot);
	#define	PGPROT_WC(pv)	pvr_pgprot_writecombine(pv)
#else
	#if defined(__arm__) || defined(__sh__)
		#define	PGPROT_WC(pv)	pgprot_writecombine(pv)
	#else
		#if defined(__i386__) || defined(__x86_64) || defined(__mips__)
			#define	PGPROT_WC(pv)	pgprot_noncached(pv)
		#else
			#define PGPROT_WC(pv)	pgprot_noncached(pv)
			#error  Unsupported architecture!
		#endif
	#endif
#endif

#define	PGPROT_UC(pv)	pgprot_noncached(pv)

#if defined(__i386__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	#define	IOREMAP(pa, bytes)	ioremap_cache(pa, bytes)
#else	
	#if defined(__arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
		#define	IOREMAP(pa, bytes)	ioremap_cached(pa, bytes)
	#else
		#define IOREMAP(pa, bytes)	ioremap(pa, bytes)
	#endif
#endif

#if defined(SUPPORT_LINUX_X86_PAT)
	#if defined(SUPPORT_LINUX_X86_WRITECOMBINE)
		#define IOREMAP_WC(pa, bytes) ioremap_wc(pa, bytes)
	#else
		#define IOREMAP_WC(pa, bytes) ioremap_nocache(pa, bytes)
	#endif
#else
	#if defined(__arm__)
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
			#define IOREMAP_WC(pa, bytes) ioremap_wc(pa, bytes)
		#else
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
				#define	IOREMAP_WC(pa, bytes)	ioremap_nocache(pa, bytes)
			#else
				#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
					#define	IOREMAP_WC(pa, bytes)	__ioremap(pa, bytes, L_PTE_BUFFERABLE)
				#else
					#define IOREMAP_WC(pa, bytes)	__ioremap(pa, bytes, , L_PTE_BUFFERABLE, 1)
				#endif
			#endif
		#endif
	#else
		#define IOREMAP_WC(pa, bytes)	ioremap_nocache(pa, bytes)
	#endif
#endif

#define	IOREMAP_UC(pa, bytes)	ioremap_nocache(pa, bytes)

IMG_VOID PVRLinuxMUtilsInit(IMG_VOID);

#endif /* __IMG_LINUX_MUTILS_H__ */

