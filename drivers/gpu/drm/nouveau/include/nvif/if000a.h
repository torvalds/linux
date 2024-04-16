#ifndef __NVIF_IF000A_H__
#define __NVIF_IF000A_H__
struct nvif_mem_v0 {
	__u8  version;
	__u8  type;
	__u8  page;
	__u8  pad03[5];
	__u64 size;
	__u64 addr;
	__u8  data[];
};

struct nvif_mem_ram_vn {
};

struct nvif_mem_ram_v0 {
	__u8  version;
	__u8  pad01[7];
	dma_addr_t *dma;
	struct scatterlist *sgl;
};
#endif
