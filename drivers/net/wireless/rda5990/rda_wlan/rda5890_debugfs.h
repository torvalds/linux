#ifndef _RDA5890_DEBUGFS_H_
#define _RDA5890_DEBUGFS_H_

void rda5890_debugfs_init(void);
void rda5890_debugfs_remove(void);

void rda5890_debugfs_init_one(struct rda5890_private *priv);
void rda5890_debugfs_remove_one(struct rda5890_private *priv);

/* This is for SDIO testing */
void rda5890_sdio_test_card_to_host(char *buf, unsigned short len);

#endif
