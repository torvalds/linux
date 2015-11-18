#include <linux/mmc/sdio_func.h>

int wilc_sdio_init(void);
int wilc_sdio_cmd52(sdio_cmd52_t *cmd);
int wilc_sdio_cmd53(sdio_cmd53_t *cmd);

int wilc_sdio_enable_interrupt(struct wilc *);
void wilc_sdio_disable_interrupt(struct wilc *);
