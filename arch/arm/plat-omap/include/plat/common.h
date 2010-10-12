/*
 * arch/arm/plat-omap/include/mach/common.h
 *
 * Header for code common to all OMAP machines.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_MACH_OMAP_COMMON_H
#define __ARCH_ARM_MACH_OMAP_COMMON_H

#include <plat/i2c.h>

struct sys_timer;

extern void omap_map_common_io(void);
extern struct sys_timer omap_timer;

extern void omap_reserve(void);

/*
 * IO bases for various OMAP processors
 * Except the tap base, rest all the io bases
 * listed are physical addresses.
 */
struct omap_globals {
	u32		class;		/* OMAP class to detect */
	void __iomem	*tap;		/* Control module ID code */
	unsigned long   sdrc;           /* SDRAM Controller */
	unsigned long   sms;            /* SDRAM Memory Scheduler */
	unsigned long   ctrl;           /* System Control Module */
	unsigned long   prm;            /* Power and Reset Management */
	unsigned long   cm;             /* Clock Management */
	unsigned long   cm2;
	unsigned long	uart1_phys;
	unsigned long	uart2_phys;
	unsigned long	uart3_phys;
	unsigned long	uart4_phys;
};

void omap2_set_globals_242x(void);
void omap2_set_globals_243x(void);
void omap2_set_globals_3xxx(void);
void omap2_set_globals_443x(void);

/* These get called from omap2_set_globals_xxxx(), do not call these */
void omap2_set_globals_tap(struct omap_globals *);
void omap2_set_globals_sdrc(struct omap_globals *);
void omap2_set_globals_control(struct omap_globals *);
void omap2_set_globals_prcm(struct omap_globals *);
void omap2_set_globals_uart(struct omap_globals *);

void omap3_map_io(void);

/**
 * omap_test_timeout - busy-loop, testing a condition
 * @cond: condition to test until it evaluates to true
 * @timeout: maximum number of microseconds in the timeout
 * @index: loop index (integer)
 *
 * Loop waiting for @cond to become true or until at least @timeout
 * microseconds have passed.  To use, define some integer @index in the
 * calling code.  After running, if @index == @timeout, then the loop has
 * timed out.
 */
#define omap_test_timeout(cond, timeout, index)			\
({								\
	for (index = 0; index < timeout; index++) {		\
		if (cond)					\
			break;					\
		udelay(1);					\
	}							\
})

extern struct device *omap2_get_mpuss_device(void);
extern struct device *omap2_get_dsp_device(void);
extern struct device *omap2_get_l3_device(void);

#endif /* __ARCH_ARM_MACH_OMAP_COMMON_H */
