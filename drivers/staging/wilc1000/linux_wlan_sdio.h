extern struct sdio_func *local_sdio_func;
extern struct sdio_driver wilc_bus;

#include <linux/mmc/sdio_func.h>

int linux_sdio_init(void *);
void linux_sdio_deinit(void *);
int linux_sdio_cmd52(sdio_cmd52_t *cmd);
int linux_sdio_cmd53(sdio_cmd53_t *cmd);
int enable_sdio_interrupt(void);
void disable_sdio_interrupt(void);
int linux_sdio_set_max_speed(void);
int linux_sdio_set_default_speed(void);

