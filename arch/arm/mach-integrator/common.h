#include <linux/amba/serial.h>
extern struct amba_pl010_data integrator_uart_data;
void integrator_init_early(void);
int integrator_init(bool is_cp);
void integrator_reserve(void);
void integrator_restart(char, const char *);
