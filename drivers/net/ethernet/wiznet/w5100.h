/*
 * Ethernet driver for the WIZnet W5100 chip.
 *
 * Copyright (C) 2006-2008 WIZnet Co.,Ltd.
 * Copyright (C) 2012 Mike Sinkovsky <msink@permonline.ru>
 *
 * Licensed under the GPL-2 or later.
 */

enum {
	W5100,
	W5200,
	W5500,
};

struct w5100_ops {
	bool may_sleep;
	int chip_id;
	int (*read)(struct net_device *ndev, u32 addr);
	int (*write)(struct net_device *ndev, u32 addr, u8 data);
	int (*read16)(struct net_device *ndev, u32 addr);
	int (*write16)(struct net_device *ndev, u32 addr, u16 data);
	int (*readbulk)(struct net_device *ndev, u32 addr, u8 *buf, int len);
	int (*writebulk)(struct net_device *ndev, u32 addr, const u8 *buf,
			 int len);
	int (*reset)(struct net_device *ndev);
	int (*init)(struct net_device *ndev);
};

void *w5100_ops_priv(const struct net_device *ndev);

int w5100_probe(struct device *dev, const struct w5100_ops *ops,
		int sizeof_ops_priv, const void *mac_addr, int irq,
		int link_gpio);
int w5100_remove(struct device *dev);

extern const struct dev_pm_ops w5100_pm_ops;
