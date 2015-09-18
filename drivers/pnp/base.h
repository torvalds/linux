/*
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */

extern struct mutex pnp_lock;
extern const struct attribute_group *pnp_dev_groups[];
void *pnp_alloc(long size);

int pnp_register_protocol(struct pnp_protocol *protocol);
void pnp_unregister_protocol(struct pnp_protocol *protocol);

#define PNP_EISA_ID_MASK 0x7fffffff
void pnp_eisa_id_to_string(u32 id, char *str);
struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *, int id,
			      const char *pnpid);
struct pnp_card *pnp_alloc_card(struct pnp_protocol *, int id, char *pnpid);

int pnp_add_device(struct pnp_dev *dev);
struct pnp_id *pnp_add_id(struct pnp_dev *dev, const char *id);

int pnp_add_card(struct pnp_card *card);
void pnp_remove_card(struct pnp_card *card);
int pnp_add_card_device(struct pnp_card *card, struct pnp_dev *dev);
void pnp_remove_card_device(struct pnp_dev *dev);

struct pnp_port {
	resource_size_t min;	/* min base number */
	resource_size_t max;	/* max base number */
	resource_size_t align;	/* align boundary */
	resource_size_t size;	/* size of range */
	unsigned char flags;	/* port flags */
};

#define PNP_IRQ_NR 256
typedef struct { DECLARE_BITMAP(bits, PNP_IRQ_NR); } pnp_irq_mask_t;

struct pnp_irq {
	pnp_irq_mask_t map;	/* bitmap for IRQ lines */
	unsigned char flags;	/* IRQ flags */
};

struct pnp_dma {
	unsigned char map;	/* bitmask for DMA channels */
	unsigned char flags;	/* DMA flags */
};

struct pnp_mem {
	resource_size_t min;	/* min base number */
	resource_size_t max;	/* max base number */
	resource_size_t align;	/* align boundary */
	resource_size_t size;	/* size of range */
	unsigned char flags;	/* memory flags */
};

#define PNP_OPTION_DEPENDENT		0x80000000
#define PNP_OPTION_SET_MASK		0xffff
#define PNP_OPTION_SET_SHIFT		12
#define PNP_OPTION_PRIORITY_MASK	0xfff
#define PNP_OPTION_PRIORITY_SHIFT	0

#define PNP_RES_PRIORITY_PREFERRED	0
#define PNP_RES_PRIORITY_ACCEPTABLE	1
#define PNP_RES_PRIORITY_FUNCTIONAL	2
#define PNP_RES_PRIORITY_INVALID	PNP_OPTION_PRIORITY_MASK

struct pnp_option {
	struct list_head list;
	unsigned int flags;	/* independent/dependent, set, priority */

	unsigned long type;	/* IORESOURCE_{IO,MEM,IRQ,DMA} */
	union {
		struct pnp_port port;
		struct pnp_irq irq;
		struct pnp_dma dma;
		struct pnp_mem mem;
	} u;
};

int pnp_register_irq_resource(struct pnp_dev *dev, unsigned int option_flags,
			      pnp_irq_mask_t *map, unsigned char flags);
int pnp_register_dma_resource(struct pnp_dev *dev, unsigned int option_flags,
			      unsigned char map, unsigned char flags);
int pnp_register_port_resource(struct pnp_dev *dev, unsigned int option_flags,
			       resource_size_t min, resource_size_t max,
			       resource_size_t align, resource_size_t size,
			       unsigned char flags);
int pnp_register_mem_resource(struct pnp_dev *dev, unsigned int option_flags,
			      resource_size_t min, resource_size_t max,
			      resource_size_t align, resource_size_t size,
			      unsigned char flags);

static inline int pnp_option_is_dependent(struct pnp_option *option)
{
	return option->flags & PNP_OPTION_DEPENDENT ? 1 : 0;
}

static inline unsigned int pnp_option_set(struct pnp_option *option)
{
	return (option->flags >> PNP_OPTION_SET_SHIFT) & PNP_OPTION_SET_MASK;
}

static inline unsigned int pnp_option_priority(struct pnp_option *option)
{
	return (option->flags >> PNP_OPTION_PRIORITY_SHIFT) &
	    PNP_OPTION_PRIORITY_MASK;
}

static inline unsigned int pnp_new_dependent_set(struct pnp_dev *dev,
						 int priority)
{
	unsigned int flags;

	if (priority > PNP_RES_PRIORITY_FUNCTIONAL) {
		dev_warn(&dev->dev, "invalid dependent option priority %d "
			 "clipped to %d", priority,
			 PNP_RES_PRIORITY_INVALID);
		priority = PNP_RES_PRIORITY_INVALID;
	}

	flags = PNP_OPTION_DEPENDENT |
	    ((dev->num_dependent_sets & PNP_OPTION_SET_MASK) <<
		PNP_OPTION_SET_SHIFT) |
	    ((priority & PNP_OPTION_PRIORITY_MASK) <<
		PNP_OPTION_PRIORITY_SHIFT);

	dev->num_dependent_sets++;

	return flags;
}

char *pnp_option_priority_name(struct pnp_option *option);
void dbg_pnp_show_option(struct pnp_dev *dev, struct pnp_option *option);

void pnp_init_resources(struct pnp_dev *dev);

void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_options(struct pnp_dev *dev);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

int pnp_check_port(struct pnp_dev *dev, struct resource *res);
int pnp_check_mem(struct pnp_dev *dev, struct resource *res);
int pnp_check_irq(struct pnp_dev *dev, struct resource *res);
#ifdef CONFIG_ISA_DMA_API
int pnp_check_dma(struct pnp_dev *dev, struct resource *res);
#endif

char *pnp_resource_type_name(struct resource *res);
void dbg_pnp_show_resources(struct pnp_dev *dev, char *desc);

void pnp_free_resources(struct pnp_dev *dev);
unsigned long pnp_resource_type(struct resource *res);

struct pnp_resource {
	struct list_head list;
	struct resource res;
};

void pnp_free_resource(struct pnp_resource *pnp_res);

struct pnp_resource *pnp_add_resource(struct pnp_dev *dev,
				      struct resource *res);
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
struct pnp_resource *pnp_add_bus_resource(struct pnp_dev *dev,
					  resource_size_t start,
					  resource_size_t end);

extern int pnp_debug;

#if defined(CONFIG_PNP_DEBUG_MESSAGES)
#define pnp_dbg(dev, format, arg...)					\
	({ if (pnp_debug) dev_printk(KERN_DEBUG, dev, format, ## arg); 0; })
#else
#define pnp_dbg(dev, format, arg...)					\
	({ if (0) dev_printk(KERN_DEBUG, dev, format, ## arg); 0; })
#endif
