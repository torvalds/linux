#ifndef PM_RCAR_H
#define PM_RCAR_H

struct rcar_sysc_ch {
	unsigned long chan_offs;
	unsigned int chan_bit;
	unsigned int isr_bit;
};

int rcar_sysc_power_down(struct rcar_sysc_ch *sysc_ch);
int rcar_sysc_power_up(struct rcar_sysc_ch *sysc_ch);
bool rcar_sysc_power_is_off(struct rcar_sysc_ch *sysc_ch);
void __iomem *rcar_sysc_init(phys_addr_t base);

#endif /* PM_RCAR_H */
