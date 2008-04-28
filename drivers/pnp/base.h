extern spinlock_t pnp_lock;
void *pnp_alloc(long size);
#define PNP_EISA_ID_MASK 0x7fffffff
void pnp_eisa_id_to_string(u32 id, char *str);
struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *, int id, char *pnpid);
struct pnp_card *pnp_alloc_card(struct pnp_protocol *, int id, char *pnpid);
struct pnp_id *pnp_add_id(struct pnp_dev *dev, char *id);
struct pnp_id *pnp_add_card_id(struct pnp_card *card, char *id);
int pnp_interface_attach_device(struct pnp_dev *dev);
void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_option(struct pnp_option *option);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

int pnp_check_port(struct pnp_dev * dev, int idx);
int pnp_check_mem(struct pnp_dev * dev, int idx);
int pnp_check_irq(struct pnp_dev * dev, int idx);
int pnp_check_dma(struct pnp_dev * dev, int idx);

void dbg_pnp_show_resources(struct pnp_dev *dev, char *desc);

void pnp_init_resource(struct resource *res);
