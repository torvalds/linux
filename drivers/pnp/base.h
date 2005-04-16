extern struct bus_type pnp_bus_type;
extern spinlock_t pnp_lock;
void *pnp_alloc(long size);
int pnp_interface_attach_device(struct pnp_dev *dev);
void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_option(struct pnp_option *option);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

int pnp_check_port(struct pnp_dev * dev, int idx);
int pnp_check_mem(struct pnp_dev * dev, int idx);
int pnp_check_irq(struct pnp_dev * dev, int idx);
int pnp_check_dma(struct pnp_dev * dev, int idx);
