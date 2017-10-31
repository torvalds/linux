#ifndef __NVIF_IF000A_H__
#define __NVIF_IF000A_H__

struct nvif_mem_ram_vn {
};

struct nvif_mem_ram_v0 {
	__u8  version;
	__u8  pad01[7];
	dma_addr_t *dma;
	struct scatterlist *sgl;
};
#endif
