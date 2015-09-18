#ifndef PM_RCAR_H
#define PM_RCAR_H

struct rcar_sysc_ch {
	u16 chan_offs;
	u8 chan_bit;
	u8 isr_bit;
};

int rcar_sysc_power_down(const struct rcar_sysc_ch *sysc_ch);
int rcar_sysc_power_up(const struct rcar_sysc_ch *sysc_ch);
bool rcar_sysc_power_is_off(const struct rcar_sysc_ch *sysc_ch);
void __iomem *rcar_sysc_init(phys_addr_t base);

#endif /* PM_RCAR_H */
