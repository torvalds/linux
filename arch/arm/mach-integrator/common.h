#include <linux/amba/serial.h>
#ifdef CONFIG_ARCH_INTEGRATOR_AP
extern struct amba_pl010_data ap_uart_data;
#else
/* Not used without Integrator/AP support anyway */
struct amba_pl010_data ap_uart_data {};
#endif
void integrator_init_early(void);
int integrator_init(bool is_cp);
void integrator_reserve(void);
void integrator_restart(char, const char *);
void integrator_init_sysfs(struct device *parent, u32 id);
