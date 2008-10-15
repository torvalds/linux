/* $XFree86$ */
/* $XdotOrg$ */
/*
 * OS depending defines
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *		Silicon Integrated Systems, Inc. (used by permission)
 *
 */

#ifndef _SIS_OSDEF_H_
#define _SIS_OSDEF_H_

/* The choices are: */
#define SIS_LINUX_KERNEL		/* Linux kernel framebuffer */
#undef  SIS_XORG_XF86			/* XFree86/X.org */

#ifdef OutPortByte
#undef OutPortByte
#endif

#ifdef OutPortWord
#undef OutPortWord
#endif

#ifdef OutPortLong
#undef OutPortLong
#endif

#ifdef InPortByte
#undef InPortByte
#endif

#ifdef InPortWord
#undef InPortWord
#endif

#ifdef InPortLong
#undef InPortLong
#endif

/**********************************************************************/
/*  LINUX KERNEL                                                      */
/**********************************************************************/

#ifdef SIS_LINUX_KERNEL

#ifdef CONFIG_FB_SIS_300
#define SIS300
#endif

#ifdef CONFIG_FB_SIS_315
#define SIS315H
#endif

#if !defined(SIS300) && !defined(SIS315H)
#warning Neither CONFIG_FB_SIS_300 nor CONFIG_FB_SIS_315 is set
#warning sisfb will not work!
#endif

#define OutPortByte(p,v) outb((u8)(v),(SISIOADDRESS)(p))
#define OutPortWord(p,v) outw((u16)(v),(SISIOADDRESS)(p))
#define OutPortLong(p,v) outl((u32)(v),(SISIOADDRESS)(p))
#define InPortByte(p)    inb((SISIOADDRESS)(p))
#define InPortWord(p)    inw((SISIOADDRESS)(p))
#define InPortLong(p)    inl((SISIOADDRESS)(p))
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset_io(MemoryAddress, value, MemorySize)

#endif /* LINUX_KERNEL */

/**********************************************************************/
/*  XFree86/X.org                                                    */
/**********************************************************************/

#ifdef SIS_XORG_XF86

#define SIS300
#define SIS315H

#define OutPortByte(p,v) outSISREG((IOADDRESS)(p),(CARD8)(v))
#define OutPortWord(p,v) outSISREGW((IOADDRESS)(p),(CARD16)(v))
#define OutPortLong(p,v) outSISREGL((IOADDRESS)(p),(CARD32)(v))
#define InPortByte(p)    inSISREG((IOADDRESS)(p))
#define InPortWord(p)    inSISREGW((IOADDRESS)(p))
#define InPortLong(p)    inSISREGL((IOADDRESS)(p))
#define SiS_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)

#endif /* XF86 */

#endif  /* _OSDEF_H_ */
