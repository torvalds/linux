#include <linux/suspend.h>

static int rockchip_suspend_enter(suspend_state_t state)
{
	cpu_do_idle();
	return 0;
}

static const struct platform_suspend_ops rockchip_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= rockchip_suspend_enter,
};

void __init rockchip_suspend_init(void)
{
	suspend_set_ops(&rockchip_suspend_ops);
}
