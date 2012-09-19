#include <asm/hardware/icst.h>

struct clk_icst_desc {
	const struct icst_params *params;
	struct icst_vco (*getvco)(void);
	void (*setvco)(struct icst_vco);
};

struct clk *icst_clk_register(struct device *dev,
			      const struct clk_icst_desc *desc);
