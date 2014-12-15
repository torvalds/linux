extern int meson_cpu_kill(unsigned int cpu);
extern void meson_cpu_die(unsigned int cpu);
extern int meson_cpu_disable(unsigned int cpu);
extern void meson_secondary_startup(void);
extern void meson_set_cpu_ctrl_reg(int value);
