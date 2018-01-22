#ifndef __NVIF_IF000C_H__
#define __NVIF_IF000C_H__
struct nvif_vmm_v0 {
	__u8  version;
	__u8  page_nr;
	__u8  pad02[6];
	__u64 addr;
	__u64 size;
	__u8  data[];
};

#define NVIF_VMM_V0_PAGE                                                   0x00
#define NVIF_VMM_V0_GET                                                    0x01
#define NVIF_VMM_V0_PUT                                                    0x02
#define NVIF_VMM_V0_MAP                                                    0x03
#define NVIF_VMM_V0_UNMAP                                                  0x04

struct nvif_vmm_page_v0 {
	__u8  version;
	__u8  index;
	__u8  shift;
	__u8  sparse;
	__u8  vram;
	__u8  host;
	__u8  comp;
	__u8  pad07[1];
};

struct nvif_vmm_get_v0 {
	__u8  version;
#define NVIF_VMM_GET_V0_ADDR                                               0x00
#define NVIF_VMM_GET_V0_PTES                                               0x01
#define NVIF_VMM_GET_V0_LAZY	                                           0x02
	__u8  type;
	__u8  sparse;
	__u8  page;
	__u8  align;
	__u8  pad05[3];
	__u64 size;
	__u64 addr;
};

struct nvif_vmm_put_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
};

struct nvif_vmm_map_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
	__u64 size;
	__u64 memory;
	__u64 offset;
	__u8  data[];
};

struct nvif_vmm_unmap_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
};
#endif
