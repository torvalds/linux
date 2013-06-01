#ifndef __ASM_R8A7779_H__
#define __ASM_R8A7779_H__

#include <linux/sh_clk.h>
#include <linux/pm_domain.h>
#include <linux/sh_eth.h>
#include <linux/platform_data/usb-rcar-phy.h>

struct platform_device;

struct r8a7779_pm_ch {
	unsigned long chan_offs;
	unsigned int chan_bit;
	unsigned int isr_bit;
};

struct r8a7779_pm_domain {
	struct generic_pm_domain genpd;
	struct r8a7779_pm_ch ch;
};

static inline struct r8a7779_pm_ch *to_r8a7779_ch(struct generic_pm_domain *d)
{
	return &container_of(d, struct r8a7779_pm_domain, genpd)->ch;
}

extern void r8a7779_init_delay(void);
extern void r8a7779_init_irq(void);
extern void r8a7779_init_irq_extpin(int irlm);
extern void r8a7779_init_irq_dt(void);
extern void r8a7779_map_io(void);
extern void r8a7779_earlytimer_init(void);
extern void r8a7779_add_early_devices(void);
extern void r8a7779_add_standard_devices(void);
extern void r8a7779_add_standard_devices_dt(void);
extern void r8a7779_add_ether_device(struct sh_eth_plat_data *pdata);
extern void r8a7779_add_usb_phy_device(struct rcar_phy_platform_data *pdata);
extern void r8a7779_init_late(void);
extern void r8a7779_clock_init(void);
extern void r8a7779_pinmux_init(void);
extern void r8a7779_pm_init(void);
extern void r8a7779_register_twd(void);
extern int r8a7779_sysc_power_down(struct r8a7779_pm_ch *r8a7779_ch);
extern int r8a7779_sysc_power_up(struct r8a7779_pm_ch *r8a7779_ch);

#ifdef CONFIG_PM
extern void __init r8a7779_init_pm_domains(void);
#else
static inline void r8a7779_init_pm_domains(void) {}
#endif /* CONFIG_PM */

extern struct smp_operations r8a7779_smp_ops;

#endif /* __ASM_R8A7779_H__ */
