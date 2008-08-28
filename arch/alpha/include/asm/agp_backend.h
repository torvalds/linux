#ifndef _ALPHA_AGP_BACKEND_H
#define _ALPHA_AGP_BACKEND_H 1

typedef	union _alpha_agp_mode {
	struct {
		u32 rate : 3;
		u32 reserved0 : 1;
		u32 fw : 1;
		u32 fourgb : 1;
		u32 reserved1 : 2;
		u32 enable : 1;
		u32 sba : 1;
		u32 reserved2 : 14;
		u32 rq : 8;
	} bits;
	u32 lw;
} alpha_agp_mode;

typedef struct _alpha_agp_info {
	struct pci_controller *hose;
	struct {
		dma_addr_t bus_base;
		unsigned long size;
		void *sysdata;
	} aperture;
	alpha_agp_mode capability;
	alpha_agp_mode mode;
	void *private;
	struct alpha_agp_ops *ops;
} alpha_agp_info;

struct alpha_agp_ops {
	int (*setup)(alpha_agp_info *);
	void (*cleanup)(alpha_agp_info *);
	int (*configure)(alpha_agp_info *);
	int (*bind)(alpha_agp_info *, off_t, struct agp_memory *);
	int (*unbind)(alpha_agp_info *, off_t, struct agp_memory *);
	unsigned long (*translate)(alpha_agp_info *, dma_addr_t);
};


#endif /* _ALPHA_AGP_BACKEND_H */
