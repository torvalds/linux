/* This file is auto generated */

__weak int gator_events_armv6_init(void);
__weak int gator_events_armv7_init(void);
__weak int gator_events_block_init(void);
__weak int gator_events_irq_init(void);
__weak int gator_events_l2c310_init(void);
__weak int gator_events_mali_init(void);
__weak int gator_events_meminfo_init(void);
__weak int gator_events_mmaped_init(void);
__weak int gator_events_net_init(void);
__weak int gator_events_perf_pmu_init(void);
__weak int gator_events_sched_init(void);
__weak int gator_events_scorpion_init(void);

static int (*gator_events_list[])(void) = {
	gator_events_armv6_init,
	gator_events_armv7_init,
	gator_events_block_init,
	gator_events_irq_init,
	gator_events_l2c310_init,
	gator_events_mali_init,
	gator_events_meminfo_init,
	gator_events_mmaped_init,
	gator_events_net_init,
	gator_events_perf_pmu_init,
	gator_events_sched_init,
	gator_events_scorpion_init,
};
