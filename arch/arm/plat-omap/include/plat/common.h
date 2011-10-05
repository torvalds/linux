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

#include <linux/delay.h>

#include <plat/i2c.h>

struct sys_timer;

extern void omap_map_common_io(void);
extern struct sys_timer omap1_timer;
extern struct sys_timer omap2_timer;
extern struct sys_timer omap3_timer;
extern struct sys_timer omap3_secure_timer;
extern struct sys_timer omap4_timer;
extern bool omap_32k_timer_init(void);
extern int __init omap_init_clocksource_32k(void);
extern unsigned long long notrace omap_32k_sched_clock(void);

extern void omap_reserve(void);

void omap2420_init_early(void);
void omap2430_init_early(void);
void omap3430_init_early(void);
void omap35xx_init_early(void);
void omap3630_init_early(void);
void omap3_init_early(void);	/* Do not use this one */
void am35xx_init_early(void);
void ti816x_init_early(void);
void omap4430_init_early(void);

void omap_sram_init(void);

/*
 * IO bases for various OMAP processors
 * Except the tap base, rest all the io bases
 * listed are physical addresses.
 */
struct omap_globals {
	u32		class;		/* OMAP class to detect */
	void __iomem	*tap;		/* Control module ID code */
	void __iomem	*sdrc;           /* SDRAM Controller */
	void __iomem	*sms;            /* SDRAM Memory Scheduler */
	void __iomem	*ctrl;           /* System Control Module */
	void __iomem	*ctrl_pad;	/* PAD Control Module */
	void __iomem	*prm;            /* Power and Reset Management */
	void __iomem	*cm;             /* Clock Management */
	void __iomem	*cm2;
};

void omap2_set_globals_242x(void);
void omap2_set_globals_243x(void);
void omap2_set_globals_3xxx(void);
void omap2_set_globals_443x(void);
void omap2_set_globals_ti816x(void);

/* These get called from omap2_set_globals_xxxx(), do not call these */
void omap2_set_globals_tap(struct omap_globals *);
void omap2_set_globals_sdrc(struct omap_globals *);
void omap2_set_globals_control(struct omap_globals *);
void omap2_set_globals_prcm(struct omap_globals *);

void omap242x_map_io(void);
void omap243x_map_io(void);
void omap3_map_io(void);
void omap4_map_io(void);


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
extern struct device *omap2_get_iva_device(void);
extern struct device *omap2_get_l3_device(void);
extern struct device *omap4_get_dsp_device(void);

#endif /* __ARCH_ARM_MACH_OMAP_COMMON_H */
