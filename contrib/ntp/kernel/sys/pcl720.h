/* Copyright (c) 1995 Vixie Enterprises
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Vixie Enterprises not be used in advertising or publicity
 * pertaining to distribution of the document or software without specific,
 * written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND VIXIE ENTERPRISES DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL VIXIE ENTERPRISES
 * BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef _PCL720_DEFINED
#define _PCL720_DEFINED

#define	pcl720_data(base,bit)	(base+(bit>>3))
#define pcl720_data_0_7(base)	(base+0)
#define pcl720_data_8_15(base)	(base+1)
#define pcl720_data_16_23(base)	(base+2)
#define pcl720_data_24_31(base)	(base+3)
#define pcl720_cntr(base,cntr)	(base+4+cntr)	/* cntr: 0..2 */
#define pcl720_cntr_0(base)	(base+4)
#define pcl720_cntr_1(base)	(base+5)
#define pcl720_cntr_2(base)	(base+6)
#define pcl720_ctrl(base)	(base+7)

#ifndef DEBUG_PCL720
#define	pcl720_inb(x)		inb(x)
#define	pcl720_outb(x,y)	outb(x,y)
#else
static unsigned char pcl720_inb(int addr) {
	unsigned char x = inb(addr);
	fprintf(DEBUG_PCL720, "inb(0x%x) -> 0x%x\n", addr, x);
	return (x);
}
static void pcl720_outb(int addr, unsigned char x) {
	outb(addr, x);
	fprintf(DEBUG_PCL720, "outb(0x%x, 0x%x)\n", addr, x);
}
#endif

#define	pcl720_load(Base,Cntr,Mode,Val) \
	({	register unsigned int	b = Base, c = Cntr, v = Val; \
		i8253_ctrl ctrl; \
		\
		ctrl.s.bcd = i8253_binary; \
		ctrl.s.mode = Mode; \
		ctrl.s.rl = i8253_lmb; \
		ctrl.s.cntr = c; \
		pcl720_outb(pcl720_ctrl(b), ctrl.i); \
		pcl720_outb(pcl720_cntr(b,c), v); \
		pcl720_outb(pcl720_cntr(b,c), v >> 8); \
		v; \
	})

#define	pcl720_read(Base,Cntr) \
	({	register unsigned int	b = Base, v; \
		i8253_ctrl ctrl; \
		\
		ctrl.s.rl = i8253_latch; \
		ctrl.s.cntr = i8253_cntr_0; \
		pcl720_outb(pcl720_ctrl(b), ctrl.i); \
		v = pcl720_inb(pcl720_cntr_0(b)); \
		v |= (pcl720_inb(pcl720_cntr_0(b)) << 8); \
		v; \
	})

#define	pcl720_input(Base) \
	({	register unsigned int	b = Base, v; \
		\
		v = pcl720_inb(pcl720_data_0_7(b)); \
		v |= (pcl720_inb(pcl720_data_8_15(b)) << 8); \
		v |= (pcl720_inb(pcl720_data_16_23(b)) << 16); \
		v |= (pcl720_inb(pcl720_data_24_31(b)) << 24); \
		v; \
	})

#define	pcl720_output(Base,Value) \
	({	register unsigned int	b = Base, v = Value; \
		\
		pcl720_outb(pcl720_data_0_7(b), v); \
		pcl720_outb(pcl720_data_8_15(b), v << 8); \
		pcl720_outb(pcl720_data_16_23(b), v << 16); \
		pcl720_outb(pcl720_data_24_31(b), v << 24); \
		v; \
	})

#endif /*_PCL720_DEFINED*/
