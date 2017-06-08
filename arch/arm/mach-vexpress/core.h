bool vexpress_smp_init_ops(void);

extern const struct smp_operations vexpress_smp_dt_ops;

extern void vexpress_cpu_die(unsigned int cpu);
