/*
 * Header for code common to all OMAP2+ machines.
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

#ifndef __ARCH_ARM_MACH_OMAP2PLUS_COMMON_H
#define __ARCH_ARM_MACH_OMAP2PLUS_COMMON_H
#ifndef __ASSEMBLER__

#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/i2c-omap.h>

#include <asm/proc-fns.h>

#include "i2c.h"
#include "serial.h"

#include "usb.h"

#define OMAP_INTC_START		NR_IRQS

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP2)
int omap2_pm_init(void);
#else
static inline int omap2_pm_init(void)
{
	return 0;
}
#endif

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP3)
int omap3_pm_init(void);
#else
static inline int omap3_pm_init(void)
{
	return 0;
}
#endif

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP4)
int omap4_pm_init(void);
#else
static inline int omap4_pm_init(void)
{
	return 0;
}
#endif

#ifdef CONFIG_OMAP_MUX
int omap_mux_late_init(void);
#else
static inline int omap_mux_late_init(void)
{
	return 0;
}
#endif

extern void omap2_init_common_infrastructure(void);

extern struct sys_timer omap2_timer;
extern struct sys_timer omap3_timer;
extern struct sys_timer omap3_secure_timer;
extern struct sys_timer omap3_gp_timer;
extern struct sys_timer omap3_am33xx_timer;
extern struct sys_timer omap4_timer;
extern struct sys_timer omap5_timer;

void omap2420_init_early(void);
void omap2430_init_early(void);
void omap3430_init_early(void);
void omap35xx_init_early(void);
void omap3630_init_early(void);
void omap3_init_early(void);	/* Do not use this one */
void am33xx_init_early(void);
void am35xx_init_early(void);
void ti81xx_init_early(void);
void am33xx_init_early(void);
void omap4430_init_early(void);
void omap5_init_early(void);
void omap3_init_late(void);	/* Do not use this one */
void omap4430_init_late(void);
void omap2420_init_late(void);
void omap2430_init_late(void);
void omap3430_init_late(void);
void omap35xx_init_late(void);
void omap3630_init_late(void);
void am35xx_init_late(void);
void ti81xx_init_late(void);
void omap4430_init_late(void);
int omap2_common_pm_late_init(void);

#if defined(CONFIG_SOC_OMAP2420) || defined(CONFIG_SOC_OMAP2430)
void omap2xxx_restart(char mode, const char *cmd);
#else
static inline void omap2xxx_restart(char mode, const char *cmd)
{
}
#endif

#ifdef CONFIG_SOC_AM33XX
void am33xx_restart(char mode, const char *cmd);
#else
static inline void am33xx_restart(char mode, const char *cmd)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP3
void omap3xxx_restart(char mode, const char *cmd);
#else
static inline void omap3xxx_restart(char mode, const char *cmd)
{
}
#endif

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5)
void omap44xx_restart(char mode, const char *cmd);
#else
static inline void omap44xx_restart(char mode, const char *cmd)
{
}
#endif

/* This gets called from mach-omap2/io.c, do not call this */
void __init omap2_set_globals_tap(u32 class, void __iomem *tap);

void __init omap242x_map_io(void);
void __init omap243x_map_io(void);
void __init omap3_map_io(void);
void __init am33xx_map_io(void);
void __init omap4_map_io(void);
void __init omap5_map_io(void);
void __init ti81xx_map_io(void);

/* omap_barriers_init() is OMAP4 only */
void omap_barriers_init(void);

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

void omap2_init_irq(void);
void omap3_init_irq(void);
void ti81xx_init_irq(void);
extern int omap_irq_pending(void);
void omap_intc_save_context(void);
void omap_intc_restore_context(void);
void omap3_intc_suspend(void);
void omap3_intc_prepare_idle(void);
void omap3_intc_resume_idle(void);
void omap2_intc_handle_irq(struct pt_regs *regs);
void omap3_intc_handle_irq(struct pt_regs *regs);
void omap_intc_of_init(void);
void omap_gic_of_init(void);

#ifdef CONFIG_CACHE_L2X0
extern void __iomem *omap4_get_l2cache_base(void);
#endif

struct device_node;
#ifdef CONFIG_OF
int __init intc_of_init(struct device_node *node,
			     struct device_node *parent);
#else
int __init intc_of_init(struct device_node *node,
			     struct device_node *parent)
{
	return 0;
}
#endif

#ifdef CONFIG_SMP
extern void __iomem *omap4_get_scu_base(void);
#else
static inline void __iomem *omap4_get_scu_base(void)
{
	return NULL;
}
#endif

extern void __init gic_init_irq(void);
extern void gic_dist_disable(void);
extern bool gic_dist_disabled(void);
extern void gic_timer_retrigger(void);
extern void omap_smc1(u32 fn, u32 arg);
extern void __iomem *omap4_get_sar_ram_base(void);
extern void omap_do_wfi(void);

#ifdef CONFIG_SMP
/* Needed for secondary core boot */
extern void omap_secondary_startup(void);
extern void omap_secondary_startup_4460(void);
extern u32 omap_modify_auxcoreboot0(u32 set_mask, u32 clear_mask);
extern void omap_auxcoreboot_addr(u32 cpu_addr);
extern u32 omap_read_auxcoreboot0(void);

extern void omap4_cpu_die(unsigned int cpu);

extern struct smp_operations omap4_smp_ops;

extern void omap5_secondary_startup(void);
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_PM)
extern int omap4_mpuss_init(void);
extern int omap4_enter_lowpower(unsigned int cpu, unsigned int power_state);
extern int omap4_finish_suspend(unsigned long cpu_state);
extern void omap4_cpu_resume(void);
extern int omap4_hotplug_cpu(unsigned int cpu, unsigned int power_state);
extern u32 omap4_mpuss_read_prev_context_state(void);
#else
static inline int omap4_enter_lowpower(unsigned int cpu,
					unsigned int power_state)
{
	cpu_do_idle();
	return 0;
}

static inline int omap4_hotplug_cpu(unsigned int cpu, unsigned int power_state)
{
	cpu_do_idle();
	return 0;
}

static inline int omap4_mpuss_init(void)
{
	return 0;
}

static inline int omap4_finish_suspend(unsigned long cpu_state)
{
	return 0;
}

static inline void omap4_cpu_resume(void)
{}

static inline u32 omap4_mpuss_read_prev_context_state(void)
{
	return 0;
}
#endif

struct omap_sdrc_params;
extern void omap_sdrc_init(struct omap_sdrc_params *sdrc_cs0,
				      struct omap_sdrc_params *sdrc_cs1);
struct omap2_hsmmc_info;
extern int omap4_twl6030_hsmmc_init(struct omap2_hsmmc_info *controllers);
extern void omap_reserve(void);

struct omap_hwmod;
extern int omap_dss_reset(struct omap_hwmod *);

#endif /* __ASSEMBLER__ */
#endif /* __ARCH_ARM_MACH_OMAP2PLUS_COMMON_H */
