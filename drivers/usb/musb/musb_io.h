/*
 * MUSB OTG driver register I/O
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __MUSB_LINUX_PLATFORM_ARCH_H__
#define __MUSB_LINUX_PLATFORM_ARCH_H__

#include <linux/io.h>

#ifndef CONFIG_BLACKFIN

/* NOTE:  these offsets are all in bytes */

static inline u16 musb_readw(const void __iomem *addr, unsigned offset)
	{ return readw_relaxed(addr + offset); }

static inline u32 musb_readl(const void __iomem *addr, unsigned offset)
	{ return readl_relaxed(addr + offset); }


static inline void musb_writew(void __iomem *addr, unsigned offset, u16 data)
	{ writew_relaxed(data, addr + offset); }

static inline void musb_writel(void __iomem *addr, unsigned offset, u32 data)
	{ writel_relaxed(data, addr + offset); }


#if defined(CONFIG_USB_MUSB_TUSB6010) || defined (CONFIG_USB_MUSB_TUSB6010_MODULE)

/*
 * TUSB6010 doesn't allow 8-bit access; 16-bit access is the minimum.
 */
static inline u8 musb_readb(const void __iomem *addr, unsigned offset)
{
	u16 tmp;
	u8 val;

	tmp = readw_relaxed(addr + (offset & ~1));
	if (offset & 1)
		val = (tmp >> 8);
	else
		val = tmp & 0xff;

	return val;
}

static inline void musb_writeb(void __iomem *addr, unsigned offset, u8 data)
{
	u16 tmp;

	tmp = readw_relaxed(addr + (offset & ~1));
	if (offset & 1)
		tmp = (data << 8) | (tmp & 0xff);
	else
		tmp = (tmp & 0xff00) | data;

	writew_relaxed(tmp, addr + (offset & ~1));
}

#else

static inline u8 musb_readb(const void __iomem *addr, unsigned offset)
	{ return readb_relaxed(addr + offset); }

static inline void musb_writeb(void __iomem *addr, unsigned offset, u8 data)
	{ writeb_relaxed(data, addr + offset); }

#endif	/* CONFIG_USB_MUSB_TUSB6010 */

#else

static inline u8 musb_readb(const void __iomem *addr, unsigned offset)
	{ return (u8) (bfin_read16(addr + offset)); }

static inline u16 musb_readw(const void __iomem *addr, unsigned offset)
	{ return bfin_read16(addr + offset); }

static inline u32 musb_readl(const void __iomem *addr, unsigned offset)
	{ return (u32) (bfin_read16(addr + offset)); }

static inline void musb_writeb(void __iomem *addr, unsigned offset, u8 data)
	{ bfin_write16(addr + offset, (u16) data); }

static inline void musb_writew(void __iomem *addr, unsigned offset, u16 data)
	{ bfin_write16(addr + offset, data); }

static inline void musb_writel(void __iomem *addr, unsigned offset, u32 data)
	{ bfin_write16(addr + offset, (u16) data); }

#endif /* CONFIG_BLACKFIN */

#endif
