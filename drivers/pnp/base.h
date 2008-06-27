extern spinlock_t pnp_lock;
void *pnp_alloc(long size);

int pnp_register_protocol(struct pnp_protocol *protocol);
void pnp_unregister_protocol(struct pnp_protocol *protocol);

#define PNP_EISA_ID_MASK 0x7fffffff
void pnp_eisa_id_to_string(u32 id, char *str);
struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *, int id, char *pnpid);
struct pnp_card *pnp_alloc_card(struct pnp_protocol *, int id, char *pnpid);

int pnp_add_device(struct pnp_dev *dev);
struct pnp_id *pnp_add_id(struct pnp_dev *dev, char *id);
int pnp_interface_attach_device(struct pnp_dev *dev);

int pnp_add_card(struct pnp_card *card);
struct pnp_id *pnp_add_card_id(struct pnp_card *card, char *id);
void pnp_remove_card(struct pnp_card *card);
int pnp_add_card_device(struct pnp_card *card, struct pnp_dev *dev);
void pnp_remove_card_device(struct pnp_dev *dev);

struct pnp_port {
	unsigned short min;	/* min base number */
	unsigned short max;	/* max base number */
	unsigned char align;	/* align boundary */
	unsigned char size;	/* size of range */
	unsigned char flags;	/* port flags */
	unsigned char pad;	/* pad */
	struct pnp_port *next;	/* next port */
};

#define PNP_IRQ_NR 256
struct pnp_irq {
	DECLARE_BITMAP(map, PNP_IRQ_NR);	/* bitmask for IRQ lines */
	unsigned char flags;	/* IRQ flags */
	unsigned char pad;	/* pad */
	struct pnp_irq *next;	/* next IRQ */
};

struct pnp_dma {
	unsigned char map;	/* bitmask for DMA channels */
	unsigned char flags;	/* DMA flags */
	struct pnp_dma *next;	/* next port */
};

struct pnp_mem {
	unsigned int min;	/* min base number */
	unsigned int max;	/* max base number */
	unsigned int align;	/* align boundary */
	unsigned int size;	/* size of range */
	unsigned char flags;	/* memory flags */
	unsigned char pad;	/* pad */
	struct pnp_mem *next;	/* next memory resource */
};

#define PNP_RES_PRIORITY_PREFERRED	0
#define PNP_RES_PRIORITY_ACCEPTABLE	1
#define PNP_RES_PRIORITY_FUNCTIONAL	2
#define PNP_RES_PRIORITY_INVALID	65535

struct pnp_option {
	unsigned short priority;	/* priority */
	struct pnp_port *port;		/* first port */
	struct pnp_irq *irq;		/* first IRQ */
	struct pnp_dma *dma;		/* first DMA */
	struct pnp_mem *mem;		/* first memory resource */
	struct pnp_option *next;	/* used to chain dependent resources */
};

struct pnp_option *pnp_build_option(int priority);
struct pnp_option *pnp_register_independent_option(struct pnp_dev *dev);
struct pnp_option *pnp_register_dependent_option(struct pnp_dev *dev,
						 int priority);
int pnp_register_irq_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_irq *data);
int pnp_register_dma_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_dma *data);
int pnp_register_port_resource(struct pnp_dev *dev, struct pnp_option *option,
			       struct pnp_port *data);
int pnp_register_mem_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_mem *data);
void pnp_init_resources(struct pnp_dev *dev);

void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_option(struct pnp_option *option);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

int pnp_check_port(struct pnp_dev *dev, struct resource *res);
int pnp_check_mem(struct pnp_dev *dev, struct resource *res);
int pnp_check_irq(struct pnp_dev *dev, struct resource *res);
int pnp_check_dma(struct pnp_dev *dev, struct resource *res);

char *pnp_resource_type_name(struct resource *res);
void dbg_pnp_show_resources(struct pnp_dev *dev, char *desc);

void pnp_free_resources(struct pnp_dev *dev);
int pnp_resource_type(struct resource *res);

struct pnp_resource {
	struct list_head list;
	struct resource res;
};

void pnp_free_resource(struct pnp_resource *pnp_res);

struct pnp_resource *pnp_add_irq_resource(struct pnp_dev *dev, int irq,
					  int flags);
struct pnp_resource *pnp_add_dma_resource(struct pnp_dev *dev, int dma,
					  int flags);
struct pnp_resource *pnp_add_io_resource(struct pnp_dev *dev,
					 resource_size_t start,
					 resource_size_t end, int flags);
struct pnp_resource *pnp_add_mem_resource(struct pnp_dev *dev,
					  resource_size_t start,
					  resource_size_t end, int flags);
